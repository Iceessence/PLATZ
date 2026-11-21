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
#include <unordered_set>

namespace platz {
namespace {
namespace fs = std::filesystem;
constexpr int maxObjects = 512;
constexpr int maxUndo = 128;
constexpr std::uintmax_t maxFileBytes = 64 * 1024 * 1024;
constexpr int applicationId = 0x504C545A;
constexpr const char* projectSql =
    "CREATE TABLE project(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
    "id TEXT NOT NULL,scene TEXT NOT NULL,name TEXT NOT NULL,head INTEGER NOT NULL,token TEXT NOT NULL)";
constexpr const char* objectsSql =
    "CREATE TABLE objects(rev INTEGER NOT NULL,seq INTEGER NOT NULL,id TEXT NOT NULL,name TEXT NOT NULL,"
    "mesh TEXT NOT NULL,px REAL NOT NULL,py REAL NOT NULL,pz REAL NOT NULL,rx REAL NOT NULL,ry REAL NOT NULL,"
    "rz REAL NOT NULL,sx REAL NOT NULL,sy REAL NOT NULL,sz REAL NOT NULL,cr REAL NOT NULL,cg REAL NOT NULL,"
    "cb REAL NOT NULL,PRIMARY KEY(rev,seq),UNIQUE(rev,id))";

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
    meshKey(obj.mesh);
    auto in = [](Vec3 v, float low, float high) {
        auto ok = [=](float f) { return std::isfinite(f) && f >= low && f <= high; };
        return ok(v.x) && ok(v.y) && ok(v.z);
    };
    if (!in(obj.position,-100000,100000)) fail("Position must be finite and within 100,000 metres.");
    if (!in(obj.rotation,-36000,36000)) fail("Rotation must be finite and within 36,000 degrees.");
    if (!in(obj.scale,0.001f,10000)) fail("Scale must be between 0.001 and 10,000.");
    if (!in(obj.color,0,1)) fail("Material sRGB channels must be between 0 and 1.");
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
        sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 65536);
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
struct Metadata { std::string id,scene,name,token; std::uint64_t head; };
Metadata readMeta(Database& db) {
    if (scalar(db,"PRAGMA application_id") != applicationId) fail("This file is not a PLATZ project.");
    if (scalar(db,"PRAGMA user_version") != 1) fail("Unsupported PLATZ project version. Open it with a compatible build.");
    Statement schema(db,"SELECT name,sql FROM sqlite_schema WHERE sql IS NOT NULL ORDER BY name");
    if (!schema.row() || schema.str(0) != "objects" || schema.str(1) != objectsSql ||
        !schema.row() || schema.str(0) != "project" || schema.str(1) != projectSql || schema.row())
        fail("Unrecognized project schema. The original file was preserved.");
    Statement q(db,"SELECT id,scene,name,token,head FROM project WHERE singleton=1");
    if (!q.row()) fail("Missing project identity.");
    auto head=q.integer(4);
    if (head < 1 || head == std::numeric_limits<std::int64_t>::max()) fail("Invalid source revision.");
    Metadata meta{q.str(0),q.str(1),q.str(2),q.str(3),static_cast<std::uint64_t>(head)};
    if (!validId(meta.id) || !validId(meta.scene) || !validId(meta.token) || meta.id == meta.scene)
        fail("Invalid project or scene identity.");
    validateName(meta.name);
    return meta;
}
void requireCurrent(const Metadata& meta, const std::string& id, const std::string& token) {
    if (meta.id != id || meta.token != token)
        fail("Another editor saved or replaced this project. Reopen it before saving changes.");
}
std::vector<Object> readObjects(Database& db, std::uint64_t rev) {
    Statement q(db,"SELECT id,name,mesh,px,py,pz,rx,ry,rz,sx,sy,sz,cr,cg,cb FROM objects WHERE rev=? ORDER BY seq LIMIT 513");
    q.bind(1,static_cast<std::int64_t>(rev));
    std::vector<Object> result;
    std::unordered_set<std::string> ids;
    while (q.row()) {
        if (result.size() >= maxObjects) fail("This build supports up to 512 scene objects.");
        Object o{q.str(0),q.str(1),parseMesh(q.str(2)),{q.number(3),q.number(4),q.number(5)},
                 {q.number(6),q.number(7),q.number(8)},{q.number(9),q.number(10),q.number(11)},
                 {q.number(12),q.number(13),q.number(14)}};
        validate(o);
        if (!ids.insert(o.id).second) fail("Duplicate object identity in project.");
        result.push_back(std::move(o));
    }
    if (result.empty()) fail("Project revision is incomplete.");
    return result;
}
void putObjects(Database& db, const std::vector<Object>& objects, std::uint64_t rev) {
    Statement ins(db,"INSERT INTO objects VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (size_t i=0; i<objects.size(); ++i) {
        auto& o=objects[i];
        ins.bind(1,static_cast<std::int64_t>(rev)); ins.bind(2,static_cast<std::int64_t>(i));
        ins.bind(3,o.id); ins.bind(4,o.name); ins.bind(5,meshKey(o.mesh));
        int k=6;
        for (Vec3 v : {o.position,o.rotation,o.scale,o.color}) {
            ins.bind(k++,v.x); ins.bind(k++,v.y); ins.bind(k++,v.z);
        }
        ins.done();
        ins.check(sqlite3_reset(ins.stmt));
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
} // namespace

const char* meshName(Mesh mesh) {
    switch (mesh) {
    case Mesh::Cube: return "Cube";
    case Mesh::Sphere: return "Sphere";
    case Mesh::Cylinder: return "Cylinder";
    }
    fail("Unsupported source mesh.");
}

Project Project::create(const fs::path& path, const std::string& name) {
    validateName(name);
    Project p;
    p.path_=fs::absolute(path);
    if (fs::exists(p.path_)) fail("A file already exists at this location. Choose a new project name.");
    p.id_=newId(); p.sceneId_=newId(); p.diskToken_=newId();
    p.name_=name; p.head_=p.revision_=1;
    p.objects_={
        makeObject(Mesh::Cube,"Platform",{0,-0.15f,0},{6.6f,0.3f,4.4f},{0.24f,0.28f,0.30f}),
        makeObject(Mesh::Cylinder,"Column",{-1.55f,1.1f,-0.25f},{1.45f,2.2f,1.45f},{0.30f,0.71f,0.55f}),
        makeObject(Mesh::Sphere,"Sphere",{0.20f,0.82f,0.74f},{1.64f,1.64f,1.64f},{0.96f,0.64f,0.27f}),
        makeObject(Mesh::Cube,"Block",{1.62f,0.70f,-0.76f},{1.32f,1.40f,1.32f},{0.76f,0.82f,0.84f})
    };
    p.objects_.back().rotation.y=16;
    p.saved_=p.objects_;
    PendingFile temp{reserveTemp(p.path_)};
    {
        Database db(temp.path,SQLITE_OPEN_READWRITE);
        db.exec("BEGIN IMMEDIATE");
        db.exec("PRAGMA application_id=0x504C545A; PRAGMA user_version=1");
        db.exec(projectSql); db.exec(objectsSql);
        Statement q(db,"INSERT INTO project VALUES(1,?,?,?,?,?)");
        q.bind(1,p.id_); q.bind(2,p.sceneId_); q.bind(3,name);
        q.bind(4,static_cast<std::int64_t>(p.head_)); q.bind(5,p.diskToken_);
        q.done(); putObjects(db,p.objects_,p.head_); db.exec("COMMIT");
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
    p.diskToken_=meta.token; p.objects_=readObjects(db,meta.head); p.saved_=p.objects_;
    p.hasPrevious_ = scalar(db,"SELECT count(DISTINCT rev) FROM objects") > 1;
    db.exec("COMMIT"); return p;
}
const Object* Project::find(const std::string& id) const {
    auto it = std::find_if(objects_.begin(),objects_.end(),[&](const Object& o){ return o.id == id; });
    return it == objects_.end() ? nullptr : &*it;
}
bool Project::dirty() const { return objects_ != saved_; }
ObjectEdit Project::stage(const Object& replacement, std::uint64_t expectedRevision) const {
    if (expectedRevision != revision_) fail("The source revision changed. Review the edit again.");
    auto* old=find(replacement.id);
    if (!old) fail("The target object does not exist in this scene.");
    validate(replacement);
    return {id_,sceneId_,revision_,*old,replacement};
}
void Project::replace(std::vector<Object> next) {
    if (next == objects_) return;
    if (revision_ >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-1)
        fail("Project revision limit reached.");
    undo_.push_back(objects_);
    if (undo_.size() > maxUndo) undo_.erase(undo_.begin());
    objects_=std::move(next); redo_.clear(); ++revision_;
}
void Project::apply(const ObjectEdit& proposal) {
    if (proposal.projectId != id_ || proposal.sceneId != sceneId_ || proposal.expectedRevision != revision_)
        fail("Proposal refers to a different scene or revision.");
    auto* old=find(proposal.before.id);
    if (!old || *old != proposal.before || proposal.after.id != old->id)
        fail("Proposal has invalid identity or outdated source values.");
    validate(proposal.after);
    auto next=objects_;
    next[static_cast<size_t>(old-objects_.data())]=proposal.after;
    replace(std::move(next));
}
void Project::edit(const Object& replacement, std::uint64_t expectedRevision) {
    apply(stage(replacement,expectedRevision));
}
std::string Project::add(Mesh mesh) {
    if (objects_.size() >= maxObjects) fail("This build supports up to 512 scene objects.");
    auto o=makeObject(mesh, std::string(meshName(mesh))+" "+std::to_string(objects_.size()+1),
                      {0,0.5f,0},{1,1,1},{0.47f,0.72f,0.68f});
    auto next=objects_; next.push_back(o); replace(std::move(next)); return o.id;
}
void Project::undo() {
    if (!canUndo()) return;
    redo_.push_back(objects_); objects_=std::move(undo_.back()); undo_.pop_back(); ++revision_;
}
void Project::redo() {
    if (!canRedo()) return;
    undo_.push_back(objects_); objects_=std::move(redo_.back()); redo_.pop_back(); ++revision_;
}
void Project::restorePrevious() {
    if (!hasPrevious_) fail("This project has no previous saved revision.");
    checkFile(path_); Database db(path_,SQLITE_OPEN_READWRITE);
    db.exec("BEGIN"); auto meta=readMeta(db); requireCurrent(meta,id_,diskToken_);
    Statement prev(db,"SELECT MAX(rev) FROM objects WHERE rev < ?");
    prev.bind(1,static_cast<std::int64_t>(head_));
    if (!prev.row()) fail("Previous saved revision unavailable.");
    auto previous=readObjects(db,prev.integer(0)); db.exec("COMMIT");
    replace(std::move(previous));
}
void Project::save() {
    checkFile(path_);
    Database db(path_,SQLITE_OPEN_READWRITE);
    db.exec("BEGIN IMMEDIATE");
    auto meta=readMeta(db); requireCurrent(meta,id_,diskToken_);
    if (!dirty()) { db.exec("COMMIT"); return; }
    auto nextToken=newId();
    auto savedCopy=objects_;
    putObjects(db,objects_,revision_);
    Statement update(db,"UPDATE project SET head=?,token=? WHERE singleton=1 AND token=?");
    update.bind(1,static_cast<std::int64_t>(revision_)); update.bind(2,nextToken);
    update.bind(3,diskToken_); update.done();
    if (sqlite3_changes(db.db) != 1) fail("Project save conflict.");
    Statement prune(db,"DELETE FROM objects WHERE rev < ?");
    prune.bind(1,static_cast<std::int64_t>(head_)); prune.done();
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
