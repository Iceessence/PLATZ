#include "OrganizerTree.hpp"
#include <QApplication>
#include <QDrag>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename Op>
void rejected(Op op) {
    try { op(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Accepted an invalid StudioOrganization operation.");
}
void checkStore() {
    using Kind=StudioOrganization::Kind;
    StudioOrganization first("sameProject"),second("sameProject"),other("unrelatedProject");
    const auto group=first.createFolder(Kind::Scene,"Group");
    const auto nested=first.createFolder(Kind::Scene,"Nested",group);
    first.moveItem(Kind::Scene,"object",nested);
    second.renameFolder(Kind::Scene,group,"Renamed");
    second.moveItem(Kind::Scene,"other",group);
    first.reload();
    require(first.folders(Kind::Scene).front().name=="Renamed","Peer rename did not persist.");
    require(first.folderOf(Kind::Scene,"object")==nested && first.folderOf(Kind::Scene,"other")==group,
            "Optimistic peer update lost folder membership.");
    require(first.folderOf(Kind::Meshes,"object").isEmpty() && other.folders(Kind::Scene).empty(),
            "Folder namespaces or project identities leaked.");
    const auto mesh=first.createFolder(Kind::Meshes,"Group");
    first.moveItem(Kind::Meshes,"object",mesh);
    rejected([&]{first.moveFolder(Kind::Scene,group,nested);});
    rejected([&]{first.moveFolder(Kind::Scene,group,group);});
    rejected([&]{first.removeFolder(Kind::Scene,group);});
    rejected([&]{first.removeFolder(Kind::Scene,nested);});
    rejected([&]{first.removeFolder(Kind::Scene,nested,QSet<QString>{"object"});});
    rejected([&]{first.moveItem(Kind::Images,"object",group);});
    rejected([&]{first.createFolder(Kind::Images,"  ");});
    first.removeFolder(Kind::Scene,nested,QSet<QString>{});
    require(first.folderOf(Kind::Scene,"object").isEmpty(),"Removing folder of deleted source left stale membership.");
    first.moveItem(Kind::Scene,"other",{});
    first.removeFolder(Kind::Scene,group);
    second.reload();
    require(second.folders(Kind::Scene).empty() && second.folderOf(Kind::Meshes,"object")==mesh,
            "Scope removal affected another namespace.");
    const auto image=second.createFolder(Kind::Images,"Extra");
    second.moveFolder(Kind::Images,image,{});
    StudioOrganization fresh("sameProject");
    require(fresh.folders(Kind::Images).front().id==image,"Fresh reader missed saved image folder.");
    QSettings settings;
    const auto imageKey=settings.allKeys().filter("/images").at(0);
    settings.setValue(imageKey,"broken");
    settings.sync();
    fresh.reload();
    require(fresh.folders(Kind::Images).empty() && fresh.folderOf(Kind::Meshes,"object")==mesh,
            "Corrupt folder scope was not isolated.");
    fresh.createFolder(Kind::Images,"Recovered");
    settings.sync();
    require(settings.value(imageKey+"-recovery").toByteArray()=="broken",
            "Corrupt folder data was not preserved in -recovery.");
}
void run() {
    QTemporaryDir profile;
    require(profile.isValid(),"Could not isolate the Studio profile.");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QCoreApplication::setOrganizationName("PLATZ Tests");
    QCoreApplication::setApplicationName("Organizer drag");
    checkStore();

    using Kind=StudioOrganization::Kind;
    StudioOrganization store("drag-fixture");
    const auto target=store.createFolder(Kind::Meshes,"Models");
    QWidget surface;
    auto* layout=new QVBoxLayout(&surface);
    auto* tree=new OrganizerTree(Kind::Meshes,[&]{return &store;},[]{return true;},&surface);
    layout->addWidget(tree->makeControls(&surface));
    layout->addWidget(tree);
    auto populate=[&]{
        tree->beginRefresh();
        tree->addSource("mesh-id","Tower");
        tree->endRefresh();
    };
    tree->onOrganizationChanged=populate;
    populate();
    surface.resize(360,270);
    surface.show();
    surface.activateWindow();
    QTest::qWait(50);
    auto* window=surface.windowHandle();
    require(window!=nullptr,"Test surface did not open.");
    auto* item=tree->sourceItem("mesh-id");
    auto* folder=tree->topLevelItem(0);
    require(folder && folder->data(0,Qt::UserRole+1)==target,"Fixture folder identity is missing.");
    const auto point=[&](QTreeWidgetItem* row){
        return window->mapFromGlobal(tree->viewport()->mapToGlobal(tree->visualItemRect(row).center()));
    };
    const auto from=point(item),to=point(folder);
    auto drag=[&](QPoint start,QPoint end) {
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        // QDrag::exec runs a nested Qt event loop and receives actual platform drag/drop events.
        QTimer::singleShot(75,&surface,[=]{QTest::mouseMove(window,end);});
        QTimer::singleShot(130,&surface,[=]{QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);});
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog,&QTimer::timeout,&surface,[]{QDrag::cancel();});
        watchdog.start(2500);
        QTest::mouseMove(window,start+QPoint(QApplication::startDragDistance()+3,0));
        QTest::qWait(170);
    };
    drag(from,to);
    require(store.folderOf(Kind::Meshes,"mesh-id")==target,"Pointer drag did not move the source into its folder.");
    auto* moved=tree->sourceItem("mesh-id");
    require(moved && moved->parent() && moved->parent()->data(0,Qt::UserRole+1)==target,
            "The refreshed tree disagrees with stored membership.");
    require(StudioOrganization("drag-fixture").folderOf(Kind::Meshes,"mesh-id")==target,
            "An independently opened profile lost the dragged source.");
    const auto rootPoint=window->mapFromGlobal(tree->viewport()->mapToGlobal(tree->viewport()->rect().bottomLeft()+QPoint(15,-12)));
    drag(point(moved),rootPoint);
    require(store.folderOf(Kind::Meshes,"mesh-id").isEmpty() && !tree->sourceItem("mesh-id")->parent(),
            "Pointer drag to the root left the source inside a folder.");
}
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        run();
        std::cout<<"Pointer drag moved a mesh source into and out of a folder through QDrag and persisted its identity.\n";
    } catch (const std::exception& error) {std::cerr<<error.what()<<"\n";return 1;}
}
