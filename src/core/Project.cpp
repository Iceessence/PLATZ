#include "Project.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstdio>
#include <stdexcept>
#include <set>
#include <unordered_map>

namespace platz {
namespace {
namespace fs = std::filesystem;
constexpr int maxObjects = 512;
constexpr int maxMeshes = 512;
constexpr int maxUndo = 128;
constexpr std::uintmax_t maxFileBytes = 64 * 1024 * 1024;
constexpr std::size_t maxMeshSourceBytes = 8u << 20;
constexpr int applicationId = 0x504C545A;
constexpr const char* assetsSql = "CREATE TABLE assets(key TEXT PRIMARY KEY,source BLOB NOT NULL)";
constexpr const char* meshLibrarySql =
    "CREATE TABLE mesh_library(rev INTEGER NOT NULL,key TEXT NOT NULL,name TEXT NOT NULL,PRIMARY KEY(rev,key))";
constexpr const char* nativeMeshesSql =
    "CREATE TABLE native_meshes(rev INTEGER NOT NULL,key TEXT NOT NULL,name TEXT NOT NULL,recipe INTEGER NOT NULL,"
    "shape INTEGER NOT NULL,x REAL NOT NULL,y REAL NOT NULL,z REAL NOT NULL,segments INTEGER NOT NULL,"
    "rings INTEGER NOT NULL,taper REAL NOT NULL,PRIMARY KEY(rev,key))";
constexpr const char* projectSql =
    "CREATE TABLE project(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
    "id TEXT NOT NULL,scene TEXT NOT NULL,name TEXT NOT NULL,head INTEGER NOT NULL,token TEXT NOT NULL)";
constexpr const char* objectsSql =
    "CREATE TABLE objects(rev INTEGER NOT NULL,seq INTEGER NOT NULL,id TEXT NOT NULL,name TEXT NOT NULL,"
    "mesh TEXT NOT NULL,px REAL NOT NULL,py REAL NOT NULL,pz REAL NOT NULL,rx REAL NOT NULL,ry REAL NOT NULL,"
    "rz REAL NOT NULL,sx REAL NOT NULL,sy REAL NOT NULL,sz REAL NOT NULL,cr REAL NOT NULL,cg REAL NOT NULL,"
    "cb REAL NOT NULL,PRIMARY KEY(rev,seq),UNIQUE(rev,id))";
constexpr const char* snapshotsSql =
    "CREATE TABLE snapshots(rev INTEGER PRIMARY KEY,object_count INTEGER NOT NULL CHECK(object_count>=0))";
constexpr const char* slotColorsSql =
    "CREATE TABLE slot_colors(rev INTEGER NOT NULL,object_id TEXT NOT NULL,slot INTEGER NOT NULL,"
    "r REAL NOT NULL,g REAL NOT NULL,b REAL NOT NULL,PRIMARY KEY(rev,object_id,slot))";
constexpr const char* textureOffSql =
    "CREATE TABLE texture_off(rev INTEGER NOT NULL,object_id TEXT NOT NULL,slot INTEGER NOT NULL,"
    "PRIMARY KEY(rev,object_id,slot))";

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string newId() {
    std::array<unsigned char, 16> bytes{};
    sqlite3_randomness(static_cast<int>(bytes.size()), bytes.data());
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto b : bytes) {
        result += digits[b >> 4];
        result += digits[b & 15];
    }
    return result;
}
bool validId(const std::string& id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void validateName(const std::string& name) {
    if (name.empty() || name.size() > 240 || name.find_first_not_of(' ') == std::string::npos)
        fail("Choose a name between 1 and 240 UTF-8 bytes.");
    // Reject control characters and malformed UTF-8 before names reach a UI or storage.
    const auto* data = reinterpret_cast<const unsigned char*>(name.data());
    for (size_t i = 0; i < name.size();) {
        unsigned int c = data[i++];
        if (c < 32 || c == 127) fail("Names cannot contain control characters.");
        if (c < 128) continue;
        int more = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
        if (c < 0xc2 || c > 0xf4 || i + more > name.size()) fail("Invalid UTF-8 name.");
        unsigned int scalar = c & (0x7f >> (more + 1));
        for (int j = 0; j < more; ++j) {
            unsigned int part = data[i++];
            if ((part & 0xc0) != 0x80) fail("Invalid UTF-8 name.");
            scalar = (scalar << 6) | (part & 63);
        }
        if ((more == 2 && scalar < 0x800) || (more == 3 && scalar < 0x10000) ||
            (scalar >= 0xd800 && scalar <= 0xdfff) || scalar > 0x10ffff ||
            (scalar >= 0x80 && scalar < 0xa0)) fail("Invalid Unicode character in name.");
    }
}
const char* meshKey(Mesh mesh) {
    switch (mesh) {
    case Mesh::Cube: return "builtin:cube/v1";
    case Mesh::Sphere: return "builtin:sphere/v1";
    case Mesh::Cylinder: return "builtin:cylinder/v1";
    }
    fail("Unsupported source mesh.");
}
Mesh parseMesh(const std::string& key) {
    for (Mesh m : {Mesh::Cube, Mesh::Sphere, Mesh::Cylinder}) if (key == meshKey(m)) return m;
    fail("This build cannot interpret a referenced source mesh.");
}
void validate(const Object& obj) {
    if (!validId(obj.id)) fail("Invalid scene object identity.");
    validateName(obj.name);
    if (obj.mesh==Mesh::Imported) {
        if (!obj.asset.data) fail("Imported object is missing its source mesh.");
    } else {
        meshKey(obj.mesh);
        if (obj.asset.data || !obj.asset.nativeId.empty()) fail("A built-in primitive cannot reference a library mesh.");
    }
    auto in = [](Vec3 v, float low, float high) {
        auto ok = [=](float f) { return std::isfinite(f) && f >= low && f <= high; };
        return ok(v.x) && ok(v.y) && ok(v.z);
    };
    if (!in(obj.position,-100000,100000)) fail("Position must be finite and within 100,000 metres.");
    if (!in(obj.rotation,-36000,36000)) fail("Rotation must be finite and within 36,000 degrees.");
    if (!in(obj.scale,0.001f,10000)) fail("Scale must be between 0.001 and 10,000.");
    if (!in(obj.color,0,1)) fail("Material sRGB channels must be between 0 and 1.");
    const auto slots=obj.asset.data ? obj.asset.data->materialSlots().size() : 0;
    for (size_t i=0;i<obj.slotColors.size();++i) {
        const auto& value=obj.slotColors[i];
        if (value.slot>=slots || (i && obj.slotColors[i-1].slot>=value.slot))
            fail("Invalid material slot reference.");
        if (!in({value.srgb[0],value.srgb[1],value.srgb[2]},0,1))
            fail("Material slot sRGB channels must be between 0 and 1.");
    }
    for (size_t i=0;i<obj.texturesOff.size();++i) {
        const auto slot=obj.texturesOff[i];
        if (slot>=slots || !obj.asset.data->materialSlots()[slot].hasSourceTexture() ||
            (i && obj.texturesOff[i-1]>=slot)) fail("Invalid base-color texture override.");
    }
}
void validateTextureBudget(const std::vector<Object>& objects) {
    std::set<std::pair<std::string_view,BaseColorTexture>> active;
    size_t bytes=0;
    for (const auto& object:objects) {
        const auto& asset=object.asset.data;
        if (!asset) continue;
        for (std::uint32_t slot=0;slot<asset->materialSlots().size();++slot) {
            const auto* texture=asset->textureForSlot(slot,object.texturesOff);
            if (texture && active.emplace(asset->digest(),*texture).second) bytes+=texture->rgbaSize();
        }
    }
    if (active.size()>BaseColorTexture::maxActive || bytes>BaseColorTexture::maxActiveBytes)
        fail("Active base-color textures exceed the scene's 64-image or 64 MiB limit.");
}

struct Database {
    sqlite3* db{};
    Database(const fs::path& path, int flags) {
        auto filename = path.u8string();
        if (sqlite3_open_v2(reinterpret_cast<const char*>(filename.c_str()), &db, flags, nullptr) != SQLITE_OK) {
            std::string why = db ? sqlite3_errmsg(db) : "allocation failed";
            sqlite3_close(db);
            fail("Cannot open project database: " + why);
        }
        sqlite3_busy_timeout(db, 1500);
        // A SQLite row includes the digest key and record overhead in addition to the GLB.
        sqlite3_limit(db, SQLITE_LIMIT_LENGTH, static_cast<int>(MeshAsset::maxBytes)+4096);
        sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
        // No application-defined SQL functions, extensions, or file-derived SQL are executed.
        if (sqlite3_exec(db,"PRAGMA trusted_schema=OFF; PRAGMA synchronous=FULL",nullptr,nullptr,nullptr) != SQLITE_OK) {
            std::string why=sqlite3_errmsg(db);
            sqlite3_close(db);
            fail("Cannot configure project database: " + why);
        }
    }
    Database(const Database&) = delete;
    ~Database() { sqlite3_close(db); } // SQLite rolls back any unfinished transaction.
    void exec(const char* sql) {
        if (sqlite3_exec(db,sql,nullptr,nullptr,nullptr) != SQLITE_OK)
            fail(std::string("Project database: ") + sqlite3_errmsg(db));
    }
};
struct Statement {
    Database& con;
    sqlite3_stmt* stmt{};
    Statement(Database& db, const char* sql) : con(db) {
        if (sqlite3_prepare_v2(con.db,sql,-1,&stmt,nullptr) != SQLITE_OK) fail(sqlite3_errmsg(con.db));
    }
    Statement(const Statement&) = delete;
    ~Statement() { sqlite3_finalize(stmt); }
    void check(int rc) { if (rc != SQLITE_OK) fail(sqlite3_errmsg(con.db)); }
    void bind(int i, const std::string& s) { check(sqlite3_bind_text(stmt,i,s.data(),static_cast<int>(s.size()),SQLITE_TRANSIENT)); }
    void bind(int i, std::int64_t n) { check(sqlite3_bind_int64(stmt,i,n)); }
    void bind(int i, double n) { check(sqlite3_bind_double(stmt,i,n)); }
    bool row() {
        int code = sqlite3_step(stmt);
        if (code == SQLITE_ROW) return true;
        if (code != SQLITE_DONE) fail(sqlite3_errmsg(con.db));
        return false;
    }
    void done() { if (row()) fail("Unexpected project query result."); }
    std::string str(int i) const {
        if (sqlite3_column_type(stmt,i) != SQLITE_TEXT) fail("Invalid project text field.");
        auto ptr = sqlite3_column_text(stmt,i);
        auto size = sqlite3_column_bytes(stmt,i);
        if (!ptr || size > 1024) fail("Project text exceeds the supported field size.");
        return {reinterpret_cast<const char*>(ptr),static_cast<size_t>(size)};
    }
    std::int64_t integer(int i) const {
        if (sqlite3_column_type(stmt,i) != SQLITE_INTEGER) fail("Invalid project integer field.");
        return sqlite3_column_int64(stmt,i);
    }
    float number(int i) const {
        auto type=sqlite3_column_type(stmt,i);
        if (type != SQLITE_FLOAT && type != SQLITE_INTEGER) fail("Invalid project number.");
        return static_cast<float>(sqlite3_column_double(stmt,i));
    }
};

std::int64_t scalar(Database& db, const char* sql) {
    Statement q(db,sql);
    if (!q.row()) fail("Missing project metadata.");
    return q.integer(0);
}
void checkFile(const fs::path& path) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > maxFileBytes)
        fail("Select a regular PLATZ project file within the supported 64 MiB limit.");
}
struct Metadata { std::string id,scene,name,token; std::uint64_t head; int version; };
Metadata readMeta(Database& db) {
    if (scalar(db,"PRAGMA application_id") != applicationId) fail("This file is not a PLATZ project.");
    auto version = scalar(db,"PRAGMA user_version");
    if (version<1 || version>7) fail("Unsupported PLATZ project version. Open it with a compatible build.");
    Statement schema(db,"SELECT name,sql FROM sqlite_schema WHERE sql IS NOT NULL ORDER BY name");
    auto checkTable = [&](const char* name, const char* definition) {
        if (!schema.row() || schema.str(0) != name || schema.str(1) != definition)
            fail("Unrecognized project schema. The original file was preserved.");
    };
    if (version>=3) checkTable("assets",assetsSql);
    if (version>=4) checkTable("mesh_library",meshLibrarySql);
    if (version>=7) checkTable("native_meshes",nativeMeshesSql);
    checkTable("objects", objectsSql);
    checkTable("project", projectSql);
    if (version>=5) checkTable("slot_colors",slotColorsSql);
    if (version >= 2) checkTable("snapshots", snapshotsSql);
    if (version>=6) checkTable("texture_off",textureOffSql);
    if (schema.row()) fail("Unrecognized project schema. The original file was preserved.");
    Statement q(db,"SELECT id,scene,name,token,head FROM project WHERE singleton=1");
    if (!q.row()) fail("Missing project identity.");
    auto head=q.integer(4);
    if (head < 1 || head == std::numeric_limits<std::int64_t>::max()) fail("Invalid source revision.");
    Metadata meta{q.str(0),q.str(1),q.str(2),q.str(3),static_cast<std::uint64_t>(head),static_cast<int>(version)};
    if (!validId(meta.id) || !validId(meta.scene) || !validId(meta.token) || meta.id == meta.scene)
        fail("Invalid project or scene identity.");
    validateName(meta.name);
    return meta;
}
LibraryMesh readMesh(Database& db, const std::string& key, std::string name, std::size_t& totalBytes) {
    validateName(name);
    Statement blob(db,"SELECT source FROM assets WHERE key=?");
    blob.bind(1,key);
    if (!blob.row() || sqlite3_column_type(blob.stmt,0)!=SQLITE_BLOB) fail("Missing embedded mesh.");
    const auto size=sqlite3_column_bytes(blob.stmt,0);
    if (size<=0 || size>MeshAsset::maxBytes) fail("Invalid embedded mesh size.");
    totalBytes+=size;
    if (totalBytes>maxMeshSourceBytes) fail("Embedded mesh library exceeds 8 MiB.");
    const auto* bytes=static_cast<const std::uint8_t*>(sqlite3_column_blob(blob.stmt,0));
    if (!bytes) fail("Could not read embedded mesh.");
    auto mesh=MeshAsset::decode({bytes,static_cast<size_t>(size)});
    if (key!="glb:"+mesh->digest()) fail("Embedded mesh digest mismatch.");
    return {std::move(name),{std::move(mesh)}};
}
void readNativeMeshes(Database& db,std::uint64_t rev,MeshLibrary& library,std::size_t& bytes) {
    Statement native(db,"SELECT key,name,recipe,shape,x,y,z,segments,rings,taper FROM native_meshes WHERE rev=? LIMIT 513");
    native.bind(1,static_cast<std::int64_t>(rev));
    while (native.row()) {
        if (library.size()>=maxMeshes) fail("Mesh library exceeds 512 assets.");
        const auto key=native.str(0);
        auto name=native.str(1);
        validateName(name);
        const auto shape=native.integer(3), segments=native.integer(7), rings=native.integer(8);
        if (!key.starts_with("native:") || !validId(key.substr(7)) || native.integer(2)!=1 ||
            shape<0 || shape>2 || segments<1 || segments>128 || rings<1 || rings>64)
            fail("Invalid native mesh identity or unsupported recipe.");
        const NativeMeshSource source{static_cast<NativeShape>(shape),
            {native.number(4),native.number(5),native.number(6)},
            static_cast<unsigned>(segments),static_cast<unsigned>(rings),native.number(9)};
        auto geometry=buildNativeMesh(source);
        bytes+=geometry->source().size();
        if (bytes>maxMeshSourceBytes) fail("Mesh library exceeds its 8 MiB geometry budget.");
        library.emplace(key,LibraryMesh{std::move(name),{std::move(geometry),key},source});
    }
}
std::vector<Object> readObjects(Database& db, std::uint64_t rev, int version, MeshLibrary& library) {
    std::size_t assetBytes=0;
    if (version>=4) {
        Statement catalog(db,"SELECT key,name FROM mesh_library WHERE rev=? LIMIT 513");
        catalog.bind(1,static_cast<std::int64_t>(rev));
        while (catalog.row()) {
            if (library.size()>=maxMeshes) fail("Mesh library exceeds 512 assets.");
            const auto key=catalog.str(0);
            auto entry=readMesh(db,key,catalog.str(1),assetBytes);
            const auto digest=entry.asset.data->digest();
            library.emplace(digest,std::move(entry));
        }
    }
    if (version>=7) readNativeMeshes(db,rev,library,assetBytes);
    Statement q(db,"SELECT id,name,mesh,px,py,pz,rx,ry,rz,sx,sy,sz,cr,cg,cb FROM objects WHERE rev=? ORDER BY seq LIMIT 513");
    q.bind(1,static_cast<std::int64_t>(rev));
    std::vector<Object> result;
    std::unordered_map<std::string,size_t> ids;
    while (q.row()) {
        if (result.size() >= maxObjects) fail("This build supports up to 512 scene objects.");
        const auto key=q.str(2);
        const bool imported=version>=3 && key.starts_with("glb:");
        const bool native=version>=7 && key.starts_with("native:");
        Object o{q.str(0),q.str(1),imported || native ? Mesh::Imported : parseMesh(key),{q.number(3),q.number(4),q.number(5)},
                 {q.number(6),q.number(7),q.number(8)},{q.number(9),q.number(10),q.number(11)},
                 {q.number(12),q.number(13),q.number(14)}};
        if (imported || native) {
            const auto identity=native ? key : key.substr(4);
            auto asset=library.find(identity);
            if (asset==library.end()) {
                if (version>=4) fail("Scene references a mesh outside its library.");
                asset=library.emplace(identity,readMesh(db,key,o.name,assetBytes)).first;
            }
            o.asset=asset->second.asset;
            if (version<5 && o.asset.data->materialSlots().front().sourceColor)
                o.slotColors.push_back({0,{o.color.x,o.color.y,o.color.z}});
            if (version<6) {
                const auto& slots=o.asset.data->materialSlots();
                for (std::uint32_t slot=0;slot<slots.size();++slot)
                    if(slots[slot].hasSourceTexture()) o.texturesOff.push_back(slot);
            }
        }
        if (!ids.emplace(o.id,result.size()).second) fail("Duplicate object identity in project.");
        result.push_back(std::move(o));
    }
    if (version>=5) {
        Statement colors(db,"SELECT object_id,slot,r,g,b FROM slot_colors WHERE rev=? ORDER BY object_id,slot");
        colors.bind(1,static_cast<std::int64_t>(rev));
        while (colors.row()) {
            const auto object=ids.find(colors.str(0));
            const auto index=colors.integer(1);
            if (object==ids.end() || index<0 || index>=MeshAsset::maxPrimitives)
                fail("Saved color references an invalid material slot.");
            result[object->second].slotColors.push_back(
                {static_cast<std::uint32_t>(index),{colors.number(2),colors.number(3),colors.number(4)}});
        }
    }
    if (version>=6) {
        Statement disabled(db,"SELECT object_id,slot FROM texture_off WHERE rev=? ORDER BY object_id,slot");
        disabled.bind(1,static_cast<std::int64_t>(rev));
        while (disabled.row()) {
            const auto object=ids.find(disabled.str(0));
            const auto index=disabled.integer(1);
            if (object==ids.end() || index<0 || index>=MeshAsset::maxPrimitives)
                fail("Saved texture override references an invalid slot.");
            result[object->second].texturesOff.push_back(static_cast<std::uint32_t>(index));
        }
    }
    for (const auto& object:result) validate(object);
    validateTextureBudget(result);
    if (version >= 2) {
        Statement count(db, "SELECT object_count FROM snapshots WHERE rev=?");
        count.bind(1,static_cast<std::int64_t>(rev));
        if (!count.row() || count.integer(0) != static_cast<std::int64_t>(result.size()))
            fail("Project revision is incomplete.");
    } else if (result.empty()) {
        fail("Project revision is incomplete.");
    }
    return result;
}
void requireCurrent(Database& db, const Metadata& meta, const Project& project, const std::string& token,
                    const std::vector<Object>& savedObjects, const MeshLibrary& savedMeshes) {
    constexpr auto conflict="The saved project changed on disk. Reopen it before saving changes.";
    if (meta.id != project.id() || meta.scene != project.sceneId() || meta.name != project.name() ||
        meta.head != project.savedRevision() || meta.token != token) fail(conflict);
    MeshLibrary observed;
    if (readObjects(db,meta.head,meta.version,observed)!=savedObjects || observed!=savedMeshes) fail(conflict);
}
void putLibrary(Database& db, const MeshLibrary& library, std::uint64_t rev) {
    Statement member(db,"INSERT INTO mesh_library VALUES(?,?,?)");
    Statement blob(db,"INSERT INTO assets(key,source) VALUES(?,?) ON CONFLICT(key) "
                      "DO UPDATE SET source=excluded.source WHERE assets.source<>excluded.source");
    for (const auto& [digest,mesh] : library) {
        if (mesh.native) continue;
        const auto key="glb:"+digest;
        const auto& bytes=mesh.asset.data->source();
        blob.bind(1,key);
        blob.check(sqlite3_bind_blob(blob.stmt,2,bytes.data(),static_cast<int>(bytes.size()),SQLITE_TRANSIENT));
        blob.done();
        blob.check(sqlite3_reset(blob.stmt));
        member.bind(1,static_cast<std::int64_t>(rev));
        member.bind(2,key);
        member.bind(3,mesh.name);
        member.done();
        member.check(sqlite3_reset(member.stmt));
    }
}
void putNativeMeshes(Database& db,const MeshLibrary& library,std::uint64_t rev) {
    Statement statement(db,"INSERT INTO native_meshes VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto& [key,mesh]:library) {
        if (!mesh.native) continue;
        const auto& source=*mesh.native;
        statement.bind(1,static_cast<std::int64_t>(rev));
        statement.bind(2,key);
        statement.bind(3,mesh.name);
        statement.bind(4,std::int64_t{1});
        statement.bind(5,static_cast<std::int64_t>(source.shape));
        for (int i=0;i<3;++i) statement.bind(6+i,source.size[i]);
        statement.bind(9,static_cast<std::int64_t>(source.segments));
        statement.bind(10,static_cast<std::int64_t>(source.rings));
        statement.bind(11,source.topScale);
        statement.done();
        statement.check(sqlite3_reset(statement.stmt));
    }
}
const LibraryMesh& findMesh(const MeshLibrary& library, std::string_view key) {
    const auto item=library.find(key);
    if (item==library.end()) fail("That mesh is no longer in the asset library.");
    return item->second;
}
void validateMeshBudget(const MeshLibrary& library,std::string_view replaced={},std::size_t inserted=0) {
    if (library.size()+(inserted && replaced.empty())>maxMeshes) fail("Mesh library exceeds 512 assets.");
    auto bytes=inserted;
    for (const auto& [key,mesh]:library)
        if (key!=replaced) bytes+=mesh.asset.data->source().size();
    if (bytes>maxMeshSourceBytes) fail("Mesh library exceeds its 8 MiB geometry budget.");
}
const LibraryMesh& findNativeMesh(const MeshLibrary& library,std::string_view key) {
    const auto& entry=findMesh(library,key);
    if (!entry.native) fail("That mesh is an imported GLB.");
    return entry;
}
std::string registerMesh(MeshLibrary& library, const fs::path& source) {
    auto mesh=MeshAsset::load(source);
    auto digest=mesh->digest();
    if (!library.contains(digest)) {
        const auto stem=source.stem().u8string();
        std::string name(stem.begin(),stem.end());
        validateName(name);
        library.emplace(digest,LibraryMesh{std::move(name),{std::move(mesh)}});
    }
    return digest;
}
void putObjects(Database& db, const std::vector<Object>& objects, std::uint64_t rev) {
    Statement snapshot(db,"INSERT INTO snapshots VALUES(?,?)");
    snapshot.bind(1,static_cast<std::int64_t>(rev));
    snapshot.bind(2,static_cast<std::int64_t>(objects.size()));
    snapshot.done();
    Statement ins(db,"INSERT INTO objects VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (size_t i=0; i<objects.size(); ++i) {
        auto& o=objects[i];
        ins.bind(1,static_cast<std::int64_t>(rev)); ins.bind(2,static_cast<std::int64_t>(i));
        ins.bind(3,o.id); ins.bind(4,o.name);
        ins.bind(5,!o.asset.nativeId.empty() ? o.asset.nativeId :
                   o.asset.data ? "glb:"+o.asset.data->digest() : std::string(meshKey(o.mesh)));
        int k=6;
        for (Vec3 v : {o.position,o.rotation,o.scale,o.color}) {
            ins.bind(k++,v.x); ins.bind(k++,v.y); ins.bind(k++,v.z);
        }
        ins.done();
        ins.check(sqlite3_reset(ins.stmt));
    }
}
void putSlotColors(Database& db, const std::vector<Object>& objects, std::uint64_t rev) {
    Statement write(db,"INSERT INTO slot_colors VALUES(?,?,?,?,?,?)");
    for (const auto& object:objects) {
        for (const auto& color:object.slotColors) {
            write.bind(1,static_cast<std::int64_t>(rev));
            write.bind(2,object.id);
            write.bind(3,static_cast<std::int64_t>(color.slot));
            for (int i=0;i<3;++i) write.bind(i+4,color.srgb[i]);
            write.done();
            write.check(sqlite3_reset(write.stmt));
        }
    }
}
void putTextureOverrides(Database& db, const std::vector<Object>& objects, std::uint64_t rev) {
    Statement write(db,"INSERT INTO texture_off VALUES(?,?,?)");
    for (const auto& object:objects) {
        for (const auto slot:object.texturesOff) {
            write.bind(1,static_cast<std::int64_t>(rev));
            write.bind(2,object.id);
            write.bind(3,static_cast<std::int64_t>(slot));
            write.done();
            write.check(sqlite3_reset(write.stmt));
        }
    }
}
std::filesystem::path reserveTemp(const fs::path& dest) {
    auto temp = dest;
    temp += ".building-" + newId();
#ifdef _WIN32
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("Could not reserve a new project file in this folder.");
    CloseHandle(file);
#else
    // Exclusive creation reserves this unique temporary name without truncating existing work.
    auto* file=std::fopen(temp.c_str(),"wbx");
    if (!file) fail("Could not reserve a new project file in this folder.");
    std::fclose(file);
#endif
    return temp;
}
struct PendingFile {
    fs::path path;
    ~PendingFile() { std::error_code ignored; fs::remove(path,ignored); }
};
void publishNew(const fs::path& temp, const fs::path& dest) {
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_WRITE_THROUGH))
        fail("Could not create project. Choose a writable folder and a new file name.");
#else
    fs::create_hard_link(temp,dest);
#endif
}
Object makeObject(Mesh mesh, std::string name, Vec3 p, Vec3 s, Vec3 c) {
    return {newId(),std::move(name),mesh,p,{},s,c};
}
std::string placeMesh(std::vector<Object>& objects, const LibraryMesh& entry) {
    if (objects.size()>=maxObjects) fail("This build supports up to 512 scene objects.");
    const Vec3 position=entry.native ? Vec3{0,entry.native->size[1]*.5f,2.9f} : Vec3{};
    auto object=makeObject(Mesh::Imported,entry.name,position,{1,1,1},{0.74f,0.78f,0.80f});
    object.asset=entry.asset;
    objects.push_back(std::move(object));
    return objects.back().id;
}
std::string applyNativeGeometry(std::vector<Object>& objects,const MeshReference& mesh) {
    std::string first;
    for (auto& object:objects) {
        if (object.asset.key()!=mesh.key()) continue;
        if (first.empty()) first=object.id;
        object.asset=mesh;
    }
    return first;
}
} // namespace

const char* meshName(Mesh mesh) {
    switch (mesh) {
    case Mesh::Imported: return "Imported GLB";
    case Mesh::Cube: return "Cube";
    case Mesh::Sphere: return "Sphere";
    case Mesh::Cylinder: return "Cylinder";
    }
    fail("Unsupported source mesh.");
}

void Object::setSlotColor(std::uint32_t slot, std::optional<std::array<float,3>> srgb) {
    auto match=std::lower_bound(slotColors.begin(),slotColors.end(),slot,
        [](const SlotColor& color,std::uint32_t target){return color.slot<target;});
    if (match!=slotColors.end() && match->slot==slot) {
        if (srgb) match->srgb=*srgb;
        else slotColors.erase(match);
    } else if (srgb) {
        slotColors.insert(match,{slot,*srgb});
    }
}

void Object::setTextureEnabled(std::uint32_t slot,bool enabled) {
    auto match=std::lower_bound(texturesOff.begin(),texturesOff.end(),slot);
    if (match!=texturesOff.end() && *match==slot) {
        if (enabled) texturesOff.erase(match);
    } else if (!enabled) {
        texturesOff.insert(match,slot);
    }
}

Project Project::create(const fs::path& path, const std::string& name) {
    validateName(name);
    Project p;
    p.path_=fs::absolute(path);
    if (fs::exists(p.path_)) fail("A file already exists at this location. Choose a new project name.");
    p.id_=newId(); p.sceneId_=newId(); p.diskToken_=newId();
    p.name_=name; p.head_=p.revision_=1;
    p.scene_.objects={
        makeObject(Mesh::Cube,"Platform",{0,-0.15f,0},{6.6f,0.3f,4.4f},{0.24f,0.28f,0.30f}),
        makeObject(Mesh::Cylinder,"Column",{-1.55f,1.1f,-0.25f},{1.45f,2.2f,1.45f},{0.30f,0.71f,0.55f}),
        makeObject(Mesh::Sphere,"Sphere",{0.20f,0.82f,0.74f},{1.64f,1.64f,1.64f},{0.96f,0.64f,0.27f}),
        makeObject(Mesh::Cube,"Block",{1.62f,0.70f,-0.76f},{1.32f,1.40f,1.32f},{0.76f,0.82f,0.84f})
    };
    p.scene_.objects.back().rotation.y=16;
    p.saved_=p.scene_;
    PendingFile temp{reserveTemp(p.path_)};
    {
        Database db(temp.path,SQLITE_OPEN_READWRITE);
        db.exec("BEGIN IMMEDIATE");
        db.exec("PRAGMA application_id=0x504C545A; PRAGMA user_version=2");
        db.exec(projectSql); db.exec(objectsSql); db.exec(snapshotsSql);
        Statement q(db,"INSERT INTO project VALUES(1,?,?,?,?,?)");
        q.bind(1,p.id_); q.bind(2,p.sceneId_); q.bind(3,name);
        q.bind(4,static_cast<std::int64_t>(p.head_)); q.bind(5,p.diskToken_);
        q.done(); putObjects(db,p.scene_.objects,p.head_); db.exec("COMMIT");
    }
    publishNew(temp.path,p.path_);
    return p;
}

Project Project::open(const fs::path& path) {
    Project p;
    p.path_=fs::absolute(path); checkFile(p.path_);
    Database db(p.path_,SQLITE_OPEN_READWRITE);
    db.exec("BEGIN");
    auto meta=readMeta(db);
    p.id_=meta.id; p.sceneId_=meta.scene; p.name_=meta.name; p.head_=p.revision_=meta.head;
    p.diskToken_=meta.token;
    p.scene_.objects=readObjects(db,meta.head,meta.version,p.scene_.meshes);
    p.saved_=p.scene_;
    p.hasPrevious_ = scalar(db,meta.version == 1 ? "SELECT count(DISTINCT rev) FROM objects" :
                                                   "SELECT count(*) FROM snapshots") > 1;
    db.exec("COMMIT"); return p;
}
const Object* Project::find(const std::string& id) const {
    auto it = std::find_if(scene_.objects.begin(),scene_.objects.end(),[&](const Object& o){ return o.id == id; });
    return it == scene_.objects.end() ? nullptr : &*it;
}
bool Project::dirty() const { return scene_ != saved_; }
void Project::requireRevision(std::uint64_t expectedRevision) const {
    if (expectedRevision != revision_) fail("The source revision changed. Review the edit again.");
}
void Project::requireRevisionCapacity() const {
    if (revision_ >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-1)
        fail("Project revision limit reached.");
}
ObjectEdit Project::stage(const Object& replacement, std::uint64_t expectedRevision) const {
    requireRevision(expectedRevision);
    auto* old=find(replacement.id);
    if (!old) fail("The target object does not exist in this scene.");
    validate(replacement);
    if (replacement.asset.data) findMesh(scene_.meshes,replacement.asset.key());
    return {id_,sceneId_,revision_,*old,replacement};
}
void Project::replace(SceneState next) {
    if (next == scene_) return;
    requireRevisionCapacity();
    validateMeshBudget(next.meshes);
    validateTextureBudget(next.objects);
    undo_.push_back(scene_);
    if (undo_.size() > maxUndo) undo_.erase(undo_.begin());
    scene_=std::move(next); redo_.clear(); ++revision_;
}
void Project::apply(const ObjectEdit& proposal) {
    if (proposal.projectId != id_ || proposal.sceneId != sceneId_ || proposal.expectedRevision != revision_)
        fail("Proposal refers to a different scene or revision.");
    auto* old=find(proposal.before.id);
    if (!old || *old != proposal.before || proposal.after.id != old->id)
        fail("Proposal has invalid identity or outdated source values.");
    auto changed=proposal.after;
    if (changed.asset.data) changed.asset=findMesh(scene_.meshes,changed.asset.key()).asset;
    validate(changed);
    auto next=scene_;
    next.objects[static_cast<size_t>(old-scene_.objects.data())]=std::move(changed);
    replace(std::move(next));
}
void Project::edit(const Object& replacement, std::uint64_t expectedRevision) {
    apply(stage(replacement,expectedRevision));
}
std::string Project::add(Mesh mesh) {
    if(mesh==Mesh::Imported) fail("Choose a GLB file to add an imported mesh.");
    if (scene_.objects.size() >= maxObjects) fail("This build supports up to 512 scene objects.");
    auto o=makeObject(mesh, std::string(meshName(mesh))+" "+std::to_string(scene_.objects.size()+1),
                      {0,0.5f,0},{1,1,1},{0.47f,0.72f,0.68f});
    auto next=scene_; next.objects.push_back(o); replace(std::move(next)); return o.id;
}
std::string Project::importMesh(const std::filesystem::path& source,std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    auto next=scene_;
    const auto digest=registerMesh(next.meshes,source);
    const auto id=placeMesh(next.objects,findMesh(next.meshes,digest));
    replace(std::move(next));
    return id;
}
std::string Project::importMeshAsset(const std::filesystem::path& source, std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    auto next=scene_;
    auto digest=registerMesh(next.meshes,source);
    replace(std::move(next));
    return digest;
}
std::string Project::instantiateMesh(const std::string& digest, std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    auto next=scene_;
    auto id=placeMesh(next.objects,findMesh(next.meshes,digest));
    replace(std::move(next));
    return id;
}
void Project::assignMesh(const std::string& objectId, const std::string& digest, std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    const auto* object=find(objectId);
    if (!object) fail("The target object does not exist in this scene.");
    auto replacement=*object;
    replacement.mesh=Mesh::Imported;
    replacement.asset=findMesh(scene_.meshes,digest).asset;
    if (replacement.asset!=object->asset) {
        replacement.slotColors.clear();
        replacement.texturesOff.clear();
    }
    edit(replacement,expectedRevision);
}
std::string Project::replaceMeshInstances(const std::string& digest, const std::filesystem::path& source,
                                          std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    const auto& original=findMesh(scene_.meshes,digest).asset;
    auto next=scene_;
    const auto newDigest=registerMesh(next.meshes,source);
    const auto& replacement=findMesh(next.meshes,newDigest).asset;
    bool used=false;
    for (auto& object: next.objects) {
        if (object.asset==original) {
            if (object.asset!=replacement) {
                object.slotColors.clear();
                object.texturesOff.clear();
            }
            object.asset=replacement;
            used=true;
        }
    }
    if (!used) fail("The selected mesh has no instances to replace.");
    replace(std::move(next));
    return newDigest;
}
void Project::removeMeshAsset(const std::string& digest, std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    const auto& asset=findMesh(scene_.meshes,digest).asset;
    if (std::any_of(scene_.objects.begin(),scene_.objects.end(),[&](const Object& o){return o.asset==asset;}))
        fail("Reassign or remove all instances before removing this asset.");
    auto next=scene_;
    next.meshes.erase(digest);
    replace(std::move(next));
}
NativeMeshPreview Project::previewNativeMesh(const std::string& key,const NativeMeshSource& source,
                                              std::uint64_t expectedRevision) const {
    requireRevision(expectedRevision);
    if (!key.empty()) findNativeMesh(scene_.meshes,key);
    auto mesh=buildNativeMesh(source);
    validateMeshBudget(scene_.meshes,key,mesh->source().size());
    NativeMeshPreview preview{scene_.objects,{},mesh};
    const MeshReference reference{mesh,key};
    if (!key.empty()) preview.selected=applyNativeGeometry(preview.objects,reference);
    if (preview.selected.empty()) {
        if (preview.objects.size()==maxObjects) preview.objects.clear();
        preview.selected=placeMesh(preview.objects,{nativeShapeName(source.shape),reference,source});
    }
    return preview;
}
CreatedMesh Project::createNativeMesh(std::string name,const NativeMeshSource& source,bool place,
                                      std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    validateName(name);
    auto mesh=buildNativeMesh(source);
    auto next=scene_;
    CreatedMesh result{"native:"+newId(),{}};
    const auto& entry=next.meshes.emplace(result.key,LibraryMesh{std::move(name),{mesh,result.key},source}).first->second;
    if (place) result.objectId=placeMesh(next.objects,entry);
    replace(std::move(next));
    return result;
}
void Project::editNativeMesh(const std::string& key,const NativeMeshSource& source,std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    if (findNativeMesh(scene_.meshes,key).native==source) return;
    auto geometry=buildNativeMesh(source);
    auto next=scene_;
    auto& entry=next.meshes.at(key);
    entry.native=source;
    entry.asset.data=std::move(geometry);
    applyNativeGeometry(next.objects,entry.asset);
    replace(std::move(next));
}
void Project::renameMeshAsset(const std::string& key,std::string name,std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    findMesh(scene_.meshes,key);
    validateName(name);
    auto next=scene_;
    next.meshes.at(key).name=std::move(name);
    replace(std::move(next));
}
std::string Project::copyNativeMesh(const std::string& key,std::string name,const std::string& detachObject,
                                    std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    validateName(name);
    auto entry=findNativeMesh(scene_.meshes,key);
    auto next=scene_;
    entry.name=std::move(name);
    entry.asset.nativeId="native:"+newId();
    auto identity=entry.asset.nativeId;
    if (!detachObject.empty()) {
        const auto* source=find(detachObject);
        if (!source || source->asset.key()!=key) fail("The active object does not use this native mesh.");
        next.objects[static_cast<size_t>(source-scene_.objects.data())].asset=entry.asset;
    }
    next.meshes.emplace(identity,std::move(entry));
    replace(std::move(next));
    return identity;
}
std::string Project::duplicate(const std::string& objectId, std::uint64_t expectedRevision) {
    return repeat(objectId, expectedRevision, 1, {});
}
std::vector<Object> Project::previewRepeat(const std::string& objectId, std::uint64_t expectedRevision,
                                           unsigned copies, Vec3 spacing) const {
    requireRevision(expectedRevision);
    const Object* source = find(objectId);
    if (!source) fail("The target object does not exist in this scene.");
    if (copies == 0 || copies > maxObjects - scene_.objects.size())
        fail("The scene must fit within 512 objects including all copies.");
    std::vector<Object> result;
    result.reserve(copies);
    for (unsigned i = 1; i <= copies; ++i) {
        Object copy = *source;
        copy.id = newId();
        const auto suffix = copies == 1 ? std::string(" copy") : " copy " + std::to_string(i);
        if (copy.name.size() + suffix.size() > 240) {
            auto end = 240 - suffix.size();
            while ((static_cast<unsigned char>(copy.name[end]) & 0xc0) == 0x80) --end;
            copy.name.resize(end);
        }
        copy.name += suffix;
        copy.position = {source->position.x + i*spacing.x, source->position.y + i*spacing.y,
                         source->position.z + i*spacing.z};
        validate(copy);
        result.push_back(std::move(copy));
    }
    return result;
}
std::string Project::repeat(const std::string& objectId, std::uint64_t expectedRevision,
                            unsigned copies, Vec3 spacing) {
    auto additions = previewRepeat(objectId, expectedRevision, copies, spacing);
    const auto index = find(objectId) - scene_.objects.data();
    auto next = scene_;
    next.objects.insert(next.objects.begin()+index+1, additions.begin(), additions.end());
    replace(std::move(next));
    return additions.back().id;
}
void Project::remove(const std::string& objectId, std::uint64_t expectedRevision) {
    requireRevision(expectedRevision);
    const Object* source = find(objectId);
    if (!source) fail("The target object does not exist in this scene.");
    auto next = scene_;
    next.objects.erase(next.objects.begin() + (source - scene_.objects.data()));
    replace(std::move(next));
}
void Project::undo() {
    if (!canUndo()) return;
    requireRevisionCapacity();
    redo_.push_back(scene_); scene_=std::move(undo_.back()); undo_.pop_back(); ++revision_;
}
void Project::redo() {
    if (!canRedo()) return;
    requireRevisionCapacity();
    undo_.push_back(scene_); scene_=std::move(redo_.back()); redo_.pop_back(); ++revision_;
}
void Project::restorePrevious() {
    if (!hasPrevious_) fail("This project has no previous saved revision.");
    checkFile(path_); Database db(path_,SQLITE_OPEN_READWRITE);
    db.exec("BEGIN"); auto meta=readMeta(db);
    requireCurrent(db,meta,*this,diskToken_,saved_.objects,saved_.meshes);
    Statement prev(db,meta.version == 1 ? "SELECT MAX(rev) FROM objects WHERE rev < ?" :
                                         "SELECT MAX(rev) FROM snapshots WHERE rev < ?");
    prev.bind(1,static_cast<std::int64_t>(head_));
    if (!prev.row()) fail("Previous saved revision unavailable.");
    SceneState previous;
    previous.objects=readObjects(db,prev.integer(0),meta.version,previous.meshes);
    db.exec("COMMIT");
    replace(std::move(previous));
}
void Project::save() {
    checkFile(path_);
    Database db(path_,SQLITE_OPEN_READWRITE);
    db.exec("BEGIN IMMEDIATE");
    auto meta=readMeta(db);
    requireCurrent(db,meta,*this,diskToken_,saved_.objects,saved_.meshes);
    if (!dirty()) { db.exec("COMMIT"); return; }
    auto nextToken=newId();
    auto savedCopy=scene_;
    if (meta.version == 1) {
        db.exec(snapshotsSql);
        db.exec("INSERT INTO snapshots SELECT rev,count(*) FROM objects GROUP BY rev");
        db.exec("PRAGMA user_version=2");
    }
    const bool hasLibrary=meta.version>=3 || !scene_.meshes.empty();
    const bool hasNative=meta.version>=7 || std::any_of(scene_.meshes.begin(),scene_.meshes.end(),
        [](const auto& entry){return entry.second.native.has_value();});
    if (hasLibrary) {
        if (meta.version<4) {
            if (meta.version<3) db.exec(assetsSql);
            db.exec(meshLibrarySql);
            putLibrary(db,saved_.meshes,head_);
        }
        if (meta.version<5) {
            db.exec(slotColorsSql);
            putSlotColors(db,saved_.objects,head_);
        }
        if (meta.version<6) {
            db.exec(textureOffSql);
            putTextureOverrides(db,saved_.objects,head_);
            db.exec("PRAGMA user_version=6");
        }
        putLibrary(db,scene_.meshes,revision_);
        putSlotColors(db,scene_.objects,revision_);
        putTextureOverrides(db,scene_.objects,revision_);
    }
    if (hasNative) {
        if (meta.version<7) {
            db.exec(nativeMeshesSql);
            db.exec("PRAGMA user_version=7");
        }
        putNativeMeshes(db,scene_.meshes,revision_);
        Statement pruneNative(db,"DELETE FROM native_meshes WHERE rev < ?");
        pruneNative.bind(1,static_cast<std::int64_t>(head_));
        pruneNative.done();
    }
    putObjects(db,scene_.objects,revision_);
    Statement update(db,"UPDATE project SET head=?,token=? WHERE singleton=1 AND token=?");
    update.bind(1,static_cast<std::int64_t>(revision_)); update.bind(2,nextToken);
    update.bind(3,diskToken_); update.done();
    if (sqlite3_changes(db.db) != 1) fail("Project save conflict.");
    Statement prune(db,"DELETE FROM objects WHERE rev < ?");
    prune.bind(1,static_cast<std::int64_t>(head_)); prune.done();
    Statement pruneSnapshots(db,"DELETE FROM snapshots WHERE rev < ?");
    pruneSnapshots.bind(1,static_cast<std::int64_t>(head_)); pruneSnapshots.done();
    if (hasLibrary) {
        Statement pruneLibrary(db,"DELETE FROM mesh_library WHERE rev < ?");
        pruneLibrary.bind(1,static_cast<std::int64_t>(head_));
        pruneLibrary.done();
        Statement pruneColors(db,"DELETE FROM slot_colors WHERE rev < ?");
        pruneColors.bind(1,static_cast<std::int64_t>(head_));
        pruneColors.done();
        Statement pruneTextures(db,"DELETE FROM texture_off WHERE rev < ?");
        pruneTextures.bind(1,static_cast<std::int64_t>(head_));
        pruneTextures.done();
        db.exec("DELETE FROM assets WHERE key NOT IN (SELECT key FROM mesh_library)");
    }
    if(scalar(db,"PRAGMA page_count")*scalar(db,"PRAGMA page_size")>maxFileBytes)
        fail("Project exceeds the supported file size.");
    db.exec("COMMIT");
    head_=revision_; diskToken_=std::move(nextToken); hasPrevious_=true; saved_=std::move(savedCopy);
}
Runtime::Runtime(const Project& p) : objects_(p.objects()), sourceRevision_(p.revision()) {}
bool Runtime::interact(const std::string& id) {
    for (auto& o : objects_) {
        if (o.id == id) {
            // Sphere and vertical cylinder yaw can be visually symmetric; tilt also shows the input.
            o.rotation.y=std::fmod(o.rotation.y+30.0f,360.0f);
            o.rotation.z=std::fmod(o.rotation.z+12.0f,360.0f);
            return true;
        }
    }
    return false;
}
} // namespace platz
