#include "Project.hpp"

#ifdef _WIN32
#include <process.h>
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace platz;
namespace fs=std::filesystem;
int passed=0;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void pass(const char* scenario) { ++passed; std::cout << "PASS " << scenario << '\n'; }
template<class Fn> void rejects(Fn fn) {
    try { fn(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("An invalid operation succeeded.");
}
void alter(const fs::path& path, const char* sql, bool interrupt=false) {
    sqlite3* db=nullptr;
    auto name=path.u8string();
    expect(sqlite3_open_v2(reinterpret_cast<const char*>(name.c_str()),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,
           "Fixture database open failed.");
    auto status=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);
    if (status != SQLITE_OK) {
        std::string why=sqlite3_errmsg(db); sqlite3_close(db); throw std::runtime_error(why);
    }
    if (interrupt) {
        expect(sqlite3_db_cacheflush(db)==SQLITE_OK,"Could not flush uncommitted data.");
        std::_Exit(73); // Simulate losing an editor process after dirty pages reached the database.
    }
    sqlite3_close(db);
}
void generateValidation(const fs::path& path) {
    auto p=Project::create(path,"PLATZ Validation");
    const char* names[]{"Display plinth","Jade column","Amber sphere","Silver block"};
    for (int i=0; i<4; ++i) {
        auto object=p.objects().at(i); object.name=names[i]; p.edit(object,p.revision());
    }
    p.save();
    auto reopened=Project::open(path);
    expect(reopened.objects()==p.objects(),"Validation source did not round trip.");
    for (const auto& object:p.objects()) std::cout<<object.id<<" "<<object.name<<'\n';
}
struct Fixture {
    fs::path dir=fs::temp_directory_path()/("platz-core-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path source=dir/"Project.platz", copy=dir/u8"\u8907\u672c.platz";
    Fixture() { expect(fs::create_directory(dir),"Unable to reserve test directory."); }
    ~Fixture() {
        std::error_code ec;
        for (auto file : {source,copy}) {
            fs::remove(file,ec); file += "-journal"; fs::remove(file,ec);
        }
        fs::remove(dir,ec);
    }
};
void suite(const fs::path& executable) {
    Fixture f;
    auto p=Project::create(f.source,"Material study");
    auto saved=Project::open(f.source);
    expect(saved.id()==p.id() && saved.sceneId()==p.sceneId() && saved.objects()==p.objects() && !p.dirty(),
           "Initial project was not stored correctly.");
    rejects([&]{ Project::create(f.source,"Accidental replacement"); });
    expect(Project::open(f.source).id()==p.id(),"Create replaced an existing project.");
    pass("ordinary project creation, stable references and no-clobber");

    auto original=p.objects();
    auto target=original.at(1);
    target.name=original.at(0).name;
    target.position.x+=1.125f; target.color={0.5f,0.4f,0.1f};
    auto proposal=p.stage(target,p.revision());
    expect(!p.dirty(),"Staging changed source.");
    p.apply(proposal);
    expect(p.find(target.id) && *p.find(target.id)==target && p.objects().at(0)==original.at(0),
           "Duplicate display names affected identity.");
    rejects([&]{p.apply(proposal);});
    auto invalid=p.stage(target,p.revision());
    invalid.sceneId="different"; rejects([&]{p.apply(invalid);});
    invalid=p.stage(target,p.revision()); invalid.after.id=original.front().id;
    rejects([&]{p.apply(invalid);});
    pass("non-mutating proposal, identity, stale revision and wrong-scene guards");

    auto invalidObj=target;
    invalidObj.position.y=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{p.edit(invalidObj,p.revision());});
    invalidObj=target; invalidObj.scale.z=0; rejects([&]{p.edit(invalidObj,p.revision());});
    invalidObj=target; invalidObj.color.x=1.01f; rejects([&]{p.edit(invalidObj,p.revision());});
    invalidObj=target; invalidObj.name=std::string("a\0b",3); rejects([&]{p.edit(invalidObj,p.revision());});
    invalidObj=target; invalidObj.name=std::string("\xc0\x80",2); rejects([&]{p.edit(invalidObj,p.revision());});
    expect(*p.find(target.id)==target,"Rejected edit modified source.");
    pass("invalid transforms, color and malformed text rejected atomically");

    auto added=p.add(Mesh::Sphere);
    expect(p.find(added) && p.canUndo(),"Add did not enter undo history.");
    auto beforeUndo=p.revision(); p.undo();
    expect(!p.find(added) && p.revision()>beforeUndo && p.canRedo(),"Undo failed.");
    p.undo(); expect(!p.dirty() && p.objects()==original,"Undo could not reach saved state.");
    p.redo(); p.redo(); expect(p.find(added) && *p.find(target.id)==target,"Redo failed.");
    pass("typed edits, add, monotonic undo/redo and dirty tracking");

    auto edited=p.objects();
    { Runtime play(p);
      expect(play.sourceRevision()==p.revision() && play.objects()==edited,"Runtime projection failed.");
      expect(play.interact(target.id) && !play.interact("missing"),"Gameplay identity lookup failed.");
      expect(play.objects()!=edited && p.objects()==edited,"Play mutated source.");
    }
    expect(p.objects()==edited,"Stopping Play leaked runtime changes.");
    pass("Play interaction, source lineage and Stop isolation");

    p.save();
    auto reload=Project::open(f.source);
    expect(reload.objects()==edited && reload.id()==p.id() && reload.sceneId()==p.sceneId() &&
           !reload.dirty() && reload.savedRevision()==p.revision(),"Saved state differs after reopening.");
    saved.add(Mesh::Cube); rejects([&]{saved.save();});
    expect(saved.dirty() && Project::open(f.source).objects()==edited,"Concurrent stale overwrite succeeded.");
    pass("save/reopen including Unicode-capable paths and stale-writer protection");

    expect(reload.canRestorePrevious(),"Previous revision absent.");
    reload.restorePrevious(); expect(reload.objects()==original && reload.dirty(),"Restore failed.");
    expect(Project::open(f.source).objects()==edited,"Restore implicitly committed.");
    reload.undo(); expect(reload.objects()==edited && !reload.dirty(),"Restore was not undoable.");
    pass("previous durable revision and reversible recovery");

#ifdef _WIN32
    auto result=_wspawnl(_P_WAIT,executable.c_str(),executable.c_str(),L"--interrupt",f.source.c_str(),nullptr);
    expect(result==73,"Could not execute crash-injection process.");
    expect(Project::open(f.source).objects()==edited,"Uncommitted transaction survived process interruption.");
    pass("forced process exit after flushing partial transaction, hot-journal recovery");
#endif

    fs::copy_file(f.source,f.copy);
    alter(f.copy,"PRAGMA user_version=999"); rejects([&]{ Project::open(f.copy); });
    fs::copy_file(f.source,f.copy,fs::copy_options::overwrite_existing);
    alter(f.copy,"CREATE TRIGGER unknown_trigger AFTER UPDATE ON project BEGIN SELECT 1; END");
    rejects([&]{ Project::open(f.copy); });
    fs::copy_file(f.source,f.copy,fs::copy_options::overwrite_existing);
    alter(f.copy,"UPDATE objects SET mesh='builtin:future/v3'"); rejects([&]{ Project::open(f.copy); });
    expect(Project::open(f.source).objects()==edited,"Compatibility failure damaged original.");
    pass("unknown schema, executable schema additions and missing primitive rejection");

    // Use a genuine v1 layout, including a retained prior revision. Opening it is read-only
    // with respect to schema. Migration and publication share the first explicit changed save.
    fs::copy_file(f.source,f.copy,fs::copy_options::overwrite_existing);
    alter(f.copy,"DROP TABLE snapshots; PRAGMA user_version=1");
    auto legacy=Project::open(f.copy);
    expect(legacy.objects()==edited && legacy.canRestorePrevious(),"Lost v1 source or history.");
    auto source=legacy.objects().at(1);
    auto duplicateId=legacy.duplicate(source.id,legacy.revision());
    Object expected=source;
    expected.id=duplicateId; expected.name += " copy";
    expect(duplicateId!=source.id && *legacy.find(duplicateId)==expected &&
           *legacy.find(source.id)==source,"Duplicate source mapping failed.");
    const auto afterDuplicate=legacy.objects();
    rejects([&]{legacy.remove(source.id,legacy.revision()-1);});
    rejects([&]{legacy.duplicate(source.id,legacy.revision()-1);});
    rejects([&]{legacy.remove("missing",legacy.revision());});
    rejects([&]{legacy.duplicate("missing",legacy.revision());});
    expect(legacy.objects()==afterDuplicate,"Rejected structure edit mutated source.");
    legacy.undo(); expect(!legacy.find(duplicateId),"Duplicate undo failed.");
    legacy.redo(); expect(legacy.objects()==afterDuplicate,"Duplicate redo changed identity or ordering.");
    legacy.save();
    auto structure=Project::open(f.copy);
    expect(structure.objects()==afterDuplicate && structure.sceneId()==p.sceneId() && structure.id()==p.id(),
           "v1 migration lost source or identities.");
    structure.restorePrevious(); expect(structure.objects()==edited,"Migrated previous revision lost.");
    structure.undo();
    pass("duplicate with stable new identity, stale/unknown target rejection and v1-to-v2 save");

    structure.remove(source.id,structure.revision());
    expect(!structure.find(source.id) && *structure.find(duplicateId)==expected,"Delete affected the wrong object.");
    structure.undo(); expect(structure.objects()==afterDuplicate,"Delete undo changed identity, source or order.");
    structure.redo(); expect(!structure.find(source.id) && structure.find(duplicateId),"Delete redo failed.");
    while (!structure.objects().empty()) structure.remove(structure.objects().back().id,structure.revision());
    structure.save();
    auto empty=Project::open(f.copy);
    expect(empty.objects().empty() && empty.canRestorePrevious(),"Saved empty scene was not recoverable.");
    expect(Runtime(empty).objects().empty(),"Empty runtime contains phantom entities.");
    empty.restorePrevious(); expect(empty.objects()==afterDuplicate,"Empty scene lost recovery data.");
    empty.save();
    auto filled=Project::open(f.copy);
    filled.restorePrevious(); expect(filled.objects().empty(),"Could not restore an empty earlier scene.");
    pass("delete all, undo/redo, persist and restore populated and empty scenes");

    auto largeName=filled.add(Mesh::Cylinder);
    auto renamed=*filled.find(largeName);
    renamed.name=std::string(233,'K')+"\xe2\x98\x80\xe2\x98\x80";
    filled.edit(renamed,filled.revision());
    auto longCopy=filled.duplicate(largeName,filled.revision());
    expect(filled.find(longCopy)->name==std::string(233,'K')+" copy","Duplicate split a UTF-8 code point.");
    pass("max-length Unicode object duplication on a Unicode project path");

    auto outside=Project::open(f.copy);
    auto outsider=outside.objects().front();
    outside.remove(outsider.id,outside.revision());
    alter(f.copy,"UPDATE objects SET name='Other editor' WHERE rev=(SELECT head FROM project) AND seq=0");
    rejects([&]{outside.save();});
    expect(Project::open(f.copy).objects().front().name=="Other editor","External edit was overwritten.");
    alter(f.copy,"DELETE FROM objects WHERE rev=(SELECT head FROM project) AND seq=0");
    rejects([&]{Project::open(f.copy);});
    pass("out-of-band source edits and incomplete snapshot counts detected");
}
}

int main(int argc, char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--validation") { generateValidation(argv[2]); return 0; }
        if (argc==3 && std::string(argv[1])=="--interrupt") {
            alter(argv[2],"PRAGMA synchronous=FULL; BEGIN IMMEDIATE; UPDATE objects SET name='interrupted'; UPDATE project SET head=777",true);
        }
        suite(std::filesystem::absolute(argv[0]));
        std::cout<<passed<<" Core scenarios passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;
    }
}
