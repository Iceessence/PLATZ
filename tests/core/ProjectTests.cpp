#include "Project.hpp"

#ifdef _WIN32
#include <process.h>
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <fstream>
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
    auto layout=Project::open(f.source);
    auto model=layout.objects().front();
    const auto start=layout.revision();
    rejects([&]{layout.repeat(model.id,start,2,{100000,0,0});});
    rejects([&]{layout.repeat(model.id,start,513,{});});
    rejects([&]{layout.repeat(model.id,start-1,2,{});});
    expect(layout.revision()==start && !layout.dirty(),"Rejected repeat changed source.");
    auto last=layout.repeat(model.id,start,3,{1.5f,2.0f,-1.0f});
    auto arranged=layout.objects();
    expect(layout.revision()==start+1 && layout.find(last)->position==Vec3{model.position.x+4.5f,model.position.y+6,model.position.z-3},
           "Repeat lost spacing or generated multiple revisions.");
    Runtime preview(layout); expect(preview.interact(last) && layout.objects()==arranged,"Runtime repeat isolation failed.");
    layout.undo(); expect(!layout.dirty(),"Repeat undo failed.");
    layout.redo(); layout.save(); expect(Project::open(f.source).objects()==arranged,"Repeat reopen changed source identities.");
    pass("linear repeat, all-or-nothing validation, spacing, runtime, undo and durable identities");
    Fixture meshFiles;
    auto meshes=Project::create(meshFiles.source,"Import checks");
    const auto originalId=meshes.importMesh(PLATZ_TEST_MESH,meshes.revision());
    const auto asset=meshes.find(originalId)->asset.data;
    expect(asset && asset->vertices().size()==18 && asset->indices().size()==18,"GLB geometry mismatch.");
    auto invalidSource=asset->source();
    invalidSource.back()=255;
    rejects([&]{MeshAsset::decode(invalidSource);});
    invalidSource.resize(20);
    rejects([&]{MeshAsset::decode(invalidSource);});
    auto copyId=meshes.duplicate(originalId,meshes.revision());
    meshes.undo(); expect(!meshes.find(copyId),"Mesh duplicate undo failed."); meshes.redo();
    meshes.save();
    auto restored=Project::open(meshFiles.source);
    expect(restored.objects()==meshes.objects() && restored.find(originalId)->asset.data==restored.find(copyId)->asset.data,
           "Restored instances lost shared asset or identity.");
    Runtime playImported(restored); expect(playImported.interact(copyId) && restored.objects()==meshes.objects(),"Mesh runtime isolation failed.");
#ifdef _WIN32
    expect(_wspawnl(_P_WAIT,executable.c_str(),executable.c_str(),L"--interrupt-assets",meshFiles.source.c_str(),nullptr)==73,
           "Mesh transaction crash test failed to run.");
    expect(Project::open(meshFiles.source).objects()==meshes.objects(),"Mesh data and project head did not recover atomically.");
#endif
    restored.remove(originalId,restored.revision());restored.remove(copyId,restored.revision());restored.save();
    auto recovery=Project::open(meshFiles.source); recovery.restorePrevious();
    expect(recovery.objects()==meshes.objects(),"Prior revision lost its embedded asset.");
    alter(meshFiles.source,"UPDATE assets SET source=zeroblob(512)");
    rejects([&]{auto corrupt=Project::open(meshFiles.source);corrupt.restorePrevious();});
    pass("GLB bounds, immutable shared geometry, atomic mesh recovery and corrupt-blob rejection");
}

void assetLibrarySuite() {
    Fixture assets;
    auto catalog=Project::create(assets.source,"Asset library checks");
    auto originalDigest=catalog.importMeshAsset(PLATZ_TEST_MESH,catalog.revision());
    const auto registered=catalog.revision();
    expect(catalog.importMeshAsset(PLATZ_TEST_MESH,registered)==originalDigest &&
           catalog.revision()==registered,"Importing matching bytes created another undo step.");
    catalog.save();
    auto current=Project::open(assets.source);
    expect(current.meshLibrary()==catalog.meshLibrary() && current.objects()==catalog.objects(),
           "Unused assets or objects changed on reopen.");
    auto instance=current.instantiateMesh(originalDigest,current.revision());
    const auto beforeAssignment=current.objects().at(3);
    rejects([&]{current.assignMesh(beforeAssignment.id,originalDigest,current.revision()-1);});
    current.assignMesh(beforeAssignment.id,originalDigest,current.revision());
    auto expected=beforeAssignment;
    expected.mesh=Mesh::Imported;
    expected.asset=current.meshLibrary().at(originalDigest).asset;
    expect(*current.find(expected.id)==expected,"Assignment changed the object identity or its properties.");
    rejects([&]{current.removeMeshAsset(originalDigest,current.revision());});
    const auto beforeReplacement=current.objects();
    Runtime running(current);
    const auto replacement=current.replaceMeshInstances(originalDigest,PLATZ_TEST_REPLACEMENT,current.revision());
    const auto afterReplacement=current.objects();
    expect(replacement!=originalDigest && current.meshLibrary().size()==2 && running.objects()==beforeReplacement,
           "Bulk replacement dropped the original source or changed Play data.");
    expect(current.find(instance)->asset.data->digest()==replacement &&
           current.find(expected.id)->asset.data==current.find(instance)->asset.data,
           "Bulk replacement missed an instance.");
    current.undo(); expect(current.objects()==beforeReplacement && current.meshLibrary().size()==1,
                           "Bulk replacement did not undo the whole edit.");
    current.redo(); expect(current.objects()==afterReplacement && current.meshLibrary().size()==2,
                           "Bulk replacement redo lost instance identity.");
    current.save();
    current.removeMeshAsset(originalDigest,current.revision());
    current.save();
    auto savedCatalog=Project::open(assets.source);
    expect(savedCatalog.meshLibrary().size()==1 && savedCatalog.objects()==afterReplacement,
           "Removal of an unused source did not persist independently of its objects.");
    savedCatalog.restorePrevious();
    expect(savedCatalog.meshLibrary().contains(originalDigest) && savedCatalog.objects()==afterReplacement,
           "Previous revision lost an unused asset that it still owns.");
    auto stale=Project::open(assets.source);
    alter(assets.source,"UPDATE mesh_library SET name='External rename' WHERE rev=(SELECT head FROM project)");
    stale.importMeshAsset(PLATZ_TEST_MESH,stale.revision());
    rejects([&]{stale.save();});
    expect(Project::open(assets.source).meshLibrary().at(replacement).name=="External rename",
           "A stale library save overwrote an external edit.");
    pass("persistent asset catalog, assignment, atomic bulk replacement, undo and revision recovery");

    Fixture migration;
    auto legacy=Project::create(migration.source,"Legacy v3 library");
    const auto legacyId=legacy.importMesh(PLATZ_TEST_MESH,legacy.revision());
    legacy.save();
    alter(migration.source,"DROP TABLE texture_off; DROP TABLE slot_colors; DROP TABLE mesh_library; PRAGMA user_version=3");
    auto migrated=Project::open(migration.source);
    expect(migrated.find(legacyId) && migrated.meshLibrary().contains(originalDigest),
           "Legacy project failed to discover its referenced mesh.");
    migrated.importMeshAsset(PLATZ_TEST_REPLACEMENT,migrated.revision());
    migrated.save();
    auto reopened=Project::open(migration.source);
    expect(reopened.meshLibrary()==migrated.meshLibrary() && reopened.objects()==legacy.objects(),
           "v3 migration changed object identities, references, or the new library.");
    reopened.restorePrevious();
    expect(reopened.meshLibrary()==legacy.meshLibrary() && reopened.objects()==legacy.objects(),
           "v3 migration lost the previous saved library.");
    pass("legacy v3 mesh paths migrate without changing objects or prior revisions");
}

void materialSuite() {
    Fixture fixture;
    auto project=Project::create(fixture.source,"Material slots");
    const auto id=project.importMesh(PLATZ_TEST_MATERIALS,project.revision());
    const auto original=*project.find(id);
    const auto asset=original.asset.data;
    expect(asset->primitives().size()==3 && asset->materialSlots().size()==2 &&
           asset->primitives()[0].slot==asset->primitives()[2].slot &&
           asset->primitives()[0].slot!=asset->primitives()[1].slot,"GLB primitive/material mapping is incorrect.");
    expect(asset->indices()[asset->primitives()[1].firstIndex]==36 &&
           asset->indices()[asset->primitives()[2].firstIndex]==72,"Primitive indices were not rebased.");
    const auto copper=asset->colorForSlot(0,{});
    expect(copper[0]>.90f && copper[0]<.93f && copper[1]>.32f && copper[1]<.34f,
           "Linear GLB base color was not converted to sRGB.");
    project.save();
    Runtime runtime(project);
    auto edited=original;
    edited.setSlotColor(0,std::array{.4f,.1f,.8f});
    auto invalid=edited;
    invalid.setSlotColor(2,std::array{.8f,.2f,.1f});
    rejects([&]{project.edit(invalid,project.revision());});
    invalid=edited;
    invalid.setSlotColor(0,std::array{.8f,-.2f,.1f});
    rejects([&]{project.edit(invalid,project.revision());});
    expect(!project.dirty(),"Rejected slot edits changed authoring data.");
    auto command=project.stage(edited,project.revision());
    project.apply(command);
    rejects([&]{project.apply(command);});
    expect(runtime.objects().back()==original,"Authoring slot edits leaked into Play.");
    project.undo(); expect(*project.find(id)==original,"Undo lost original slot colors.");
    project.redo(); expect(*project.find(id)==edited,"Redo lost the edited slot.");
    auto twin=project.duplicate(id,project.revision());
    expect(project.find(twin)->slotColors==edited.slotColors,"Duplication omitted material overrides.");
    auto reset=edited;
    reset.setSlotColor(0,std::nullopt);
    project.edit(reset,project.revision());
    expect(project.find(id)->slotColors.empty() && project.find(twin)->slotColors==edited.slotColors,
           "Reset changed the other instance of the same source.");
    project.undo();
    project.save();
    auto reopened=Project::open(fixture.source);
    expect(reopened.objects()==project.objects(),"Saved/imported material overrides did not round-trip.");
    auto replacement=reopened.importMeshAsset(PLATZ_TEST_MESH,reopened.revision());
    reopened.assignMesh(id,replacement,reopened.revision());
    expect(reopened.find(id)->slotColors.empty() && reopened.find(twin)->slotColors==edited.slotColors,
           "A replacement reused unrelated slot colors or changed the other instance.");
    reopened.restorePrevious();
    expect(*reopened.find(id)==original && !reopened.find(twin),"Previous revision had the wrong material edits.");
    pass("material groups, revision-bound validation, instance isolation, undo, assignment and persistence");

    Fixture legacyFixture;
    auto old=Project::create(legacyFixture.source,"Flat color from v4");
    const auto legacyId=old.importMesh(PLATZ_TEST_LEGACY_COLOR,old.revision());
    old.save();
    alter(legacyFixture.source,"DROP TABLE texture_off; DROP TABLE slot_colors; PRAGMA user_version=4");
    auto migrated=Project::open(legacyFixture.source);
    auto legacy=*migrated.find(legacyId);
    const std::array expected{legacy.color.x,legacy.color.y,legacy.color.z};
    expect(legacy.slotColors.size()==1 && legacy.slotColors.front().srgb==expected &&
           legacy.asset.data->colorForSlot(0,expected,legacy.slotColors)==expected,
           "Opening a v4 project changed the old flat object color.");
    auto named=legacy;
    named.name="Migrated object";
    migrated.edit(named,migrated.revision());
    migrated.save();
    auto upgraded=Project::open(legacyFixture.source);
    expect(upgraded.objects()==migrated.objects(),"v5 did not persist the migrated override.");
    upgraded.restorePrevious();
    expect(*upgraded.find(legacyId)==legacy,"Migration lost the prior revision's appearance.");
    pass("v4 flat-color appearance and previous revision preserved through schema v5 migration");
}

void textureSuite() {
    const auto asset=MeshAsset::load(PLATZ_TEST_TEXTURE);
    const auto& materials=asset->materialSlots();
    expect(materials.size()==2 && materials[0].texture && materials[1].texture,"Embedded textures were not parsed.");
    const auto& png=*materials[0].texture;
    const auto& jpeg=*materials[1].texture;
    expect(png.encoding==TextureEncoding::Png && png.width==32 && png.height==32 &&
           png.min==TextureFilter::Nearest && png.wrapU==TextureWrap::Clamp &&
           jpeg.encoding==TextureEncoding::Jpeg && jpeg.width==64 && jpeg.height==64 &&
           jpeg.mag==TextureFilter::Linear && jpeg.wrapV==TextureWrap::Repeat,"Image metadata or sampler mapping is incorrect.");
    expect(asset->primitives()[0].hasUv && asset->primitives()[1].hasUv &&
           asset->vertices()[2].uv==std::array{1.f,0.f} && asset->vertices()[4].uv==std::array{0.f,1.f},
           "Float or normalized TEXCOORD_0 was decoded incorrectly.");
    std::vector<std::uint8_t> pixels(png.rgbaSize());
    asset->readTexturePixels(png,pixels);
    expect(pixels[0]==240 && pixels[1]==32 && pixels[2]==28 && pixels[3]==255,"PNG RGBA or row order changed.");
    pixels.resize(jpeg.rgbaSize());
    asset->readTexturePixels(jpeg,pixels);
    expect(pixels[2]>pixels[1]+80 && pixels[2]>pixels[0]+80,"JPEG decode failed to preserve the expected blue.");
    rejects([&]{asset->readTexturePixels(jpeg,std::span(pixels).first(15));});

    auto damaged=asset->source();
    damaged[png.offset]=0;
    const auto flat=MeshAsset::decode(damaged);
    expect(!flat->materialSlots()[0].texture && flat->materialSlots()[0].hasSourceTexture() &&
           flat->materialSlots()[1].texture && flat->indices()==asset->indices(),
           "Malformed optional texture damaged unrelated geometry or materials.");
    auto oversized=asset->source();
    const std::array<std::uint8_t,4> enlargedWidth{0,0,4,1};
    const std::array<std::uint8_t,4> enlargedHeaderCrc{0x63,0x3c,0x61,0x98};
    std::copy(enlargedWidth.begin(),enlargedWidth.end(),oversized.begin()+png.offset+16);
    std::copy(enlargedHeaderCrc.begin(),enlargedHeaderCrc.end(),oversized.begin()+png.offset+29);
    const auto tooLarge=MeshAsset::decode(oversized);
    expect(!tooLarge->materialSlots()[0].texture && tooLarge->materialSlots()[0].textureNote.find("1024")!=std::string::npos,
           "Oversized PNG dimensions did not trigger a bounded flat-color fallback.");
    pass("embedded PNG/JPEG decoding, bounded pixel buffers, UV0 and malformed-image fallback");

    Fixture fixture;
    auto project=Project::create(fixture.source,"Texture overrides");
    const auto id=project.importMesh(PLATZ_TEST_TEXTURE,project.revision());
    project.save();
    const auto original=*project.find(id);
    Runtime running(project);
    auto changed=original;
    changed.setTextureEnabled(0,false);
    auto invalid=original;
    invalid.setTextureEnabled(2,false);
    rejects([&]{project.edit(invalid,project.revision());});
    project.edit(changed,project.revision());
    expect(!changed.asset.data->textureForSlot(0,changed.texturesOff) &&
           changed.asset.data->textureForSlot(1,changed.texturesOff) && running.objects().back()==original,
           "Texture toggle changed the other slot or the Play snapshot.");
    project.undo(); expect(*project.find(id)==original,"Undo did not restore the imported texture.");
    project.redo(); expect(*project.find(id)==changed,"Redo lost the texture override.");
    project.save();
    auto reopened=Project::open(fixture.source);
    expect(*reopened.find(id)==changed,"Texture override did not persist.");
    reopened.restorePrevious();
    expect(*reopened.find(id)==original,"Previous revision did not restore the texture.");

    Fixture legacyFixture;
    auto legacy=Project::create(legacyFixture.source,"Legacy flat preview");
    const auto legacyId=legacy.importMesh(PLATZ_TEST_TEXTURE,legacy.revision());
    legacy.save();
    alter(legacyFixture.source,"DROP TABLE texture_off; PRAGMA user_version=5");
    auto migrated=Project::open(legacyFixture.source);
    auto migratedObject=*migrated.find(legacyId);
    expect(migratedObject.texturesOff==std::vector<std::uint32_t>{0,1},
           "Opening legacy source activated textures that were previously flat.");
    migratedObject.setTextureEnabled(0,true);
    migrated.edit(migratedObject,migrated.revision());
    migrated.save();
    auto v6=Project::open(legacyFixture.source);
    expect(*v6.find(legacyId)==migratedObject,"Migration failed to save the explicit texture toggle.");
    v6.restorePrevious();
    expect(v6.find(legacyId)->texturesOff==std::vector<std::uint32_t>{0,1},"Migration changed the legacy revision.");
    pass("texture override undo, Play isolation, persistence and schema v5 flat-appearance migration");
}

void nativeGeometrySuite(const fs::path& executable) {
    for (auto shape:{NativeShape::Cube,NativeShape::Sphere,NativeShape::Cylinder}) {
        auto source=defaultNativeMesh(shape);
        source.size={1.25f,3.2f,.75f};
        source.segments=shape==NativeShape::Cube ? 2 : 12;
        if (shape==NativeShape::Cylinder) {source.rings=3; source.topScale=.6f;}
        const auto mesh=buildNativeMesh(source);
        const auto same=MeshAsset::decode(mesh->source());
        expect(same->digest()==mesh->digest() && buildNativeMesh(source)->digest()==mesh->digest(),
               "Generated GLB failed deterministic rebuild or reimport.");
        expect(mesh->primitives().size()==1 && mesh->primitives().front().hasUv &&
               mesh->materialSlots().size()==1,"Native geometry lost UV0 or its material slot.");
        const auto& vertices=mesh->vertices();
        for (const auto& vertex:vertices) {
            expect(std::abs(std::hypot(vertex.normal[0],vertex.normal[1],vertex.normal[2])-1)<.001f &&
                   std::all_of(vertex.uv.begin(),vertex.uv.end(),[](float uv){return uv>=0 && uv<=1;}),
                   "Generated normal or UV was invalid.");
        }
        const auto& indices=mesh->indices();
        const auto expected=shape==NativeShape::Cube ? 12*source.segments*source.segments :
                            shape==NativeShape::Sphere ? 2*source.segments*(source.rings-1) :
                            2*source.segments*(source.rings+1);
        expect(indices.size()==3*expected,"Generated mesh had an incorrect face count.");
        for (size_t i=0;i<indices.size();i+=3) {
            const auto& a=vertices.at(indices[i]);
            const auto& b=vertices.at(indices[i+1]);
            const auto& c=vertices.at(indices[i+2]);
            auto ab=b.position, ac=c.position;
            for (int axis=0;axis<3;++axis) {
                ab[axis]-=a.position[axis]; ac[axis]-=a.position[axis];
            }
            float facing=0;
            for (int axis=0;axis<3;++axis) {
                const int u=(axis+1)%3,v=(axis+2)%3;
                facing+=(ab[u]*ac[v]-ab[v]*ac[u])*(a.normal[axis]+b.normal[axis]+c.normal[axis]);
            }
            expect(facing>0,"Generated a degenerate or inward-wound triangle.");
        }
        expect(std::abs(mesh->maxCorner()[1]-source.size[1]*.5f)<.001f,"Wrong height bounds.");
        auto invalid=source;
        invalid.size[0]=std::numeric_limits<float>::infinity();
        rejects([&]{buildNativeMesh(invalid);});
        invalid=source;
        invalid.segments=129;
        rejects([&]{buildNativeMesh(invalid);});
    }
    auto maximum=defaultNativeMesh(NativeShape::Sphere);
    maximum.segments=128;
    maximum.rings=64;
    const auto dense=buildNativeMesh(maximum);
    expect(dense->indices().size()==128*63*6,"Maximum sphere did not fit the supported limits.");
    pass("native cube, sphere and cylinder topology, UVs, normals, winding, bounds and GLB reimport");

    Fixture roundtrip;
    roundtrip.copy.replace_extension(".glb");
    {
        std::ofstream exportFile(roundtrip.copy,std::ios::binary);
        const auto& bytes=dense->source();
        expect(bytes.size()>65536 &&
               bool(exportFile.write(reinterpret_cast<const char*>(bytes.data()),bytes.size())),
               "Could not stage high-detail native GLB export.");
    }
    auto importedProject=Project::create(roundtrip.source,"GLB round trip");
    const auto importedDense=importedProject.importMesh(roundtrip.copy,importedProject.revision());
    importedProject.save();
    const auto reopenedDense=Project::open(roundtrip.source);
    expect(reopenedDense.find(importedDense)->asset.data->source()==dense->source(),
           "Large generated GLB was not embedded byte for byte.");
    pass("high-detail native export reimports and saves as exact embedded GLB");

    Fixture fixture;
    auto project=Project::create(fixture.source,"Native sources");
    const auto imported=project.importMesh(PLATZ_TEST_TEXTURE,project.revision());
    project.save();
    const auto original=*project.find(imported);
    const auto legacyHead=project.savedRevision();
    auto source=defaultNativeMesh(NativeShape::Cylinder);
    const auto unplaced=project.previewNativeMesh({},source,project.revision());
    expect(unplaced.objects.size()==project.objects().size()+1 && !project.dirty(),
           "Draft mesh leaked into saved project state.");
    const auto shared=project.createNativeMesh("Shared cylinder",source,true,project.revision());
    const auto linked=project.instantiateMesh(shared.key,project.revision());
    auto tinted=*project.find(linked);
    tinted.position={2.5f,1.f,3.f};
    tinted.setSlotColor(0,std::array{.1f,.5f,.9f});
    project.edit(tinted,project.revision());
    const auto assigned=project.objects().front().id;
    project.assignMesh(assigned,shared.key,project.revision());
    const auto copy=project.copyNativeMesh(shared.key,"Unlinked cylinder",{},project.revision());
    expect(copy!=shared.key && project.meshLibrary().at(copy).asset.data->digest()==tinted.asset.data->digest(),
           "Identical native geometry did not retain separate stable identities.");
    project.renameMeshAsset(shared.key,"Column source",project.revision());
    const auto prior=project.revision();
    Runtime runtime(project);
    auto change=source;
    change.size={1.75f,2.1f,1.5f};
    change.segments=8;
    change.topScale=.45f;
    const auto preview=project.previewNativeMesh(shared.key,change,prior);
    expect(!preview.selected.empty() && project.revision()==prior && *project.find(linked)==tinted,
           "Parameter preview changed authoritative state.");
    project.editNativeMesh(shared.key,change,prior);
    rejects([&]{project.editNativeMesh(shared.key,source,prior);});
    const auto revised=*project.find(linked);
    expect(revised.asset.key()==shared.key && revised.asset.data->digest()==preview.geometry->digest() &&
           revised.position==tinted.position && revised.slotColors==tinted.slotColors &&
           project.find(assigned)->asset==revised.asset && project.find(shared.objectId)->asset==revised.asset &&
           project.meshLibrary().at(copy).native==source,
           "Shared edit disturbed identities, overrides, dependencies or an independent copy.");
    expect(std::find(runtime.objects().begin(),runtime.objects().end(),tinted)!=runtime.objects().end(),
           "Already-running scene observed a native source edit.");
    project.undo();
    expect(*project.find(linked)==tinted,"Undo did not restore the source and its linked instances together.");
    project.redo();
    expect(*project.find(linked)==revised,"Redo did not restore the revised mesh.");
    rejects([&]{project.removeMeshAsset(shared.key,project.revision());});
    const auto unique=project.copyNativeMesh(shared.key,"Unique instance",linked,project.revision());
    expect(project.find(linked)->asset.key()==unique && project.find(linked)->slotColors==tinted.slotColors &&
           project.find(shared.objectId)->asset.key()==shared.key,"Detach failed to move just the selected reference.");
    rejects([&]{project.copyNativeMesh(shared.key,"Wrong source",linked,project.revision());});
    project.removeMeshAsset(copy,project.revision());
    project.save();
    const auto reopened=Project::open(fixture.source);
    expect(reopened.meshLibrary()==project.meshLibrary() && reopened.objects()==project.objects() &&
           reopened.meshLibrary().at(shared.key).native==change && *reopened.find(imported)==original,
           "Native source, linkage or legacy GLB changed across the v6-to-v7 save.");
    auto restored=reopened;
    restored.restorePrevious();
    expect(restored.meshLibrary().size()==1 && restored.savedRevision()>legacyHead &&
           *restored.find(imported)==original,"Restoring the v6 snapshot damaged legacy content.");
    pass("native stable identities, linked edits, detach, Undo/Redo, Play and schema v6-to-v7 reopen");

#ifdef _WIN32
    expect(_wspawnl(_P_WAIT,executable.c_str(),executable.c_str(),L"--interrupt-native",fixture.source.c_str(),nullptr)==73,
           "Could not execute the native source crash-injection process.");
    expect(Project::open(fixture.source).objects()==project.objects() &&
           Project::open(fixture.source).meshLibrary()==project.meshLibrary(),
           "An uncommitted native source write escaped SQLite rollback.");
    pass("interrupted native source save rolls back");
#endif
    alter(fixture.source,"UPDATE native_meshes SET recipe=99");
    rejects([&]{Project::open(fixture.source);});
    pass("unknown native recipe rejected");
}
}

int main(int argc, char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--interrupt-assets")
            alter(argv[2],"BEGIN IMMEDIATE; DELETE FROM texture_off; DELETE FROM slot_colors; DELETE FROM mesh_library; DELETE FROM assets; "
                          "UPDATE project SET head=777",true);
        if (argc==3 && std::string(argv[1])=="--interrupt-native")
            alter(argv[2],"BEGIN IMMEDIATE; DELETE FROM native_meshes; UPDATE project SET head=777",true);
        if(argc==3 && std::string(argv[1])=="--extend-validation-textures") {
            auto scene=Project::open(argv[2]);
            const auto id=scene.importMesh(PLATZ_TEST_TEXTURE,scene.revision());
            auto object=*scene.find(id);
            object.position={2.3f,0,1.7f};
            object.scale={.55f,.68f,.55f};
            scene.edit(object,scene.revision());
            scene.save();
            return 0;
        }
        if(argc==3 && std::string(argv[1])=="--extend-validation-materials") {
            auto scene=Project::open(argv[2]);
            const auto id=scene.importMesh(PLATZ_TEST_MATERIALS,scene.revision());
            auto object=*scene.find(id);
            object.position={-0.25f,0,1.55f};
            object.scale={.82f,.82f,.82f};
            scene.edit(object,scene.revision());
            scene.save();
            return 0;
        }
        if(argc==3 && std::string(argv[1])=="--extend-validation-library") {
            auto scene=Project::open(argv[2]);
            scene.importMeshAsset(PLATZ_TEST_REPLACEMENT,scene.revision());
            scene.save();
            return 0;
        }
        if(argc==3 && std::string(argv[1])=="--extend-validation-mesh") {
            auto p=Project::open(argv[2]);
            for(const auto& o:p.objects()) if(o.asset.data) throw std::runtime_error("Validation mesh already present.");
            auto id=p.importMesh(PLATZ_TEST_MESH,p.revision());
            auto obj=*p.find(id); obj.position={-2.35f,0,1.23f}; obj.color={0.84f,0.42f,0.32f};
            p.edit(obj,p.revision()); p.save(); return 0;
        }
        if (argc==3 && std::string(argv[1])=="--validation") { generateValidation(argv[2]); return 0; }
        if (argc==3 && std::string(argv[1])=="--interrupt") {
            alter(argv[2],"PRAGMA synchronous=FULL; BEGIN IMMEDIATE; UPDATE objects SET name='interrupted'; UPDATE project SET head=777",true);
        }
        suite(std::filesystem::absolute(argv[0]));
        assetLibrarySuite();
        materialSuite();
        textureSuite();
        nativeGeometrySuite(std::filesystem::absolute(argv[0]));
        std::cout<<passed<<" Core scenarios passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;
    }
}
