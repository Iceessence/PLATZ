#include "StudioWindow.hpp"
#include "Appearance.hpp"
#include "Viewport.hpp"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QInputMethodEvent>
#include <QMenu>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QSpinBox>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using platz::engine::Capture;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T>
T* control(QWidget& window, const char* name) {
    auto* value = window.findChild<T*>(QLatin1String(name));
    if (!value) throw std::runtime_error(std::string("Missing Studio control: ") + name);
    return value;
}

Capture settledFrame(Viewport& viewport) {
    // Flush the view invalidation triggered by the preceding normal input event.
    QApplication::processEvents();
    viewport.repaint();
    return viewport.captureFrame();
}

QPoint visiblePoint(const Capture& frame, std::uint32_t token, const Viewport& viewport) {
    // Use an interior location so fractional monitor scale cannot put this click on an edge.
    for (unsigned y = 4; y + 4 < frame.height; ++y) {
        for (unsigned x = 4; x + 4 < frame.width; ++x) {
            const auto at = [&](int dx, int dy) { return frame.tokens[(y + dy) * frame.width + x + dx]; };
            if (at(0,0) == token && at(-3,0) == token && at(3,0) == token &&
                at(0,-3) == token && at(0,3) == token)
                return QPoint(qRound(x / viewport.devicePixelRatioF()), qRound(y / viewport.devicePixelRatioF()));
        }
    }
    throw std::runtime_error("Expected test object has no visible interior pixels");
}

void enterText(QWidget* widget, const QString& value) {
    widget->setFocus();
    QTest::keyClick(widget, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(widget, value);
    QTest::keyClick(widget, Qt::Key_Return);
    QApplication::processEvents();
}

void expose(StudioWindow& window) {
    window.resize(1280, 800);
    window.show();
    window.activateWindow();
    check(QTest::qWaitForWindowExposed(&window), "Studio did not expose a native window");
}

void chooseFile(const QString& path, const std::function<void()>& action) {
    bool answered = false;
    QTimer observer;
    QObject::connect(&observer, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* editor = dialog->findChild<QLineEdit*>("fileNameEdit");
        if (!editor) { observer.stop(); dialog->reject(); return; }
        observer.stop();
        answered = true;
        editor->setText(QDir::toNativeSeparators(path));
        static_cast<QDialog*>(dialog)->accept();
        if (dialog->isVisible()) dialog->reject();
    });
    observer.start(25);
    action();
    QApplication::processEvents();
    check(answered, "Studio project button did not open its file chooser");
}
void fileAction(StudioWindow& window, Qt::Key shortcut, const QString& path) {
    chooseFile(path,[&]{
        QTest::mouseClick(control<QPushButton>(window,shortcut==Qt::Key_N ? "newProject" : "openProject"),Qt::LeftButton);
    });
}

void exerciseImport(QTemporaryDir& dir) {
    const auto project=dir.filePath("Mesh scene.platz");
    const auto source=dir.filePath("Spire.glb");
    check(QFile::copy(PLATZ_TEST_MESH,source),"Could not prepare local import source");
    Capture expected;
    std::string id;
    {
        StudioWindow window;
        expose(window);
        fileAction(window,Qt::Key_N,project);
        chooseFile(source,[&]{control<QAction>(window,"importGlb")->trigger();});
        auto* tree=control<QListWidget>(window,"sceneTree");
        check(tree->count()==5 && tree->currentRow()==4,"GLB import did not select a new scene object");
        id=tree->currentItem()->data(Qt::UserRole).toString().toStdString();
        enterText(control<QDoubleSpinBox>(window,"positionY"),"3.5");
        auto* vp=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        vp->setFocus(); QTest::keyClick(vp,Qt::Key_F);
        expected=settledFrame(*vp);
        auto point=visiblePoint(expected,5,*vp);
        QTest::mouseClick(vp,Qt::LeftButton,{},point);
        check(control<QLineEdit>(window,"objectName")->text()=="Spire","GPU pick missed the imported mesh");
        auto* play=control<QPushButton>(window,"playButton");
        QTest::mouseClick(play,Qt::LeftButton); QTest::mouseClick(vp,Qt::LeftButton,{},point);
        check(settledFrame(*vp).tokens!=expected.tokens,"Imported mesh did not turn during Play");
        QTest::mouseClick(play,Qt::LeftButton);
        check(settledFrame(*vp).rgba==expected.rgba,"Stop failed to restore imported geometry");
        QTest::mouseClick(control<QPushButton>(window,"saveButton"),Qt::LeftButton);
        check(window.close(),"Imported project did not close");
    }
    check(QFile::remove(source),"Could not remove disposable source GLB");
    {
        StudioWindow window; expose(window); fileAction(window,Qt::Key_O,project);
        auto* tree=control<QListWidget>(window,"sceneTree");
        check(tree->count()==5 && tree->item(4)->data(Qt::UserRole).toString().toStdString()==id,"Imported identity lost on reopen");
        tree->setCurrentRow(4);
        auto* vp=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        vp->setFocus(); QTest::keyClick(vp,Qt::Key_F);
        auto actual=settledFrame(*vp);
        check(actual.tokens==expected.tokens && actual.rgba==expected.rgba,"Embedded mesh render changed without source GLB");
        const auto recovered=dir.filePath("Recovered.glb");
        auto* exportButton=control<QPushButton>(window,"saveEmbeddedGlb");
        check(exportButton->isVisible(),"Original GLB button is unavailable");
        chooseFile(recovered,[&]{QTest::mouseClick(exportButton,Qt::LeftButton);});
        QFile original(PLATZ_TEST_MESH), extracted(recovered);
        check(original.open(QIODevice::ReadOnly) && extracted.open(QIODevice::ReadOnly) &&
              original.readAll()==extracted.readAll(),"Extracted bytes differ from the original GLB");
        extracted.close();
        const auto occupied=dir.filePath("Already here.glb");
        QFile marker(occupied);
        check(marker.open(QIODevice::WriteOnly) && marker.write("keep")==4,"Could not prepare an occupied export path");
        marker.close();
        chooseFile(occupied,[&]{QTest::mouseClick(exportButton,Qt::LeftButton);});
        check(marker.open(QIODevice::ReadOnly) && marker.readAll()=="keep","Export replaced an existing file");
        check(window.close(),"Saving source bytes made an unrelated project edit");
    }
    std::cout<<"GLB import, Vulkan pick, isolated Play, pixel-identical reopen and byte-exact no-clobber extraction passed\n";
}
void exerciseAssetLibrary(QTemporaryDir& dir) {
    const auto file=dir.filePath("Asset library.platz");
    std::string objectId;
    std::vector<platz::Object> expectedObjects;
    Capture expectedFrame;
    {
        StudioWindow window;
        expose(window);
        fileAction(window,Qt::Key_N,file);
        auto* library=control<QTreeWidget>(window,"meshLibrary");
        auto* scene=control<QListWidget>(window,"sceneTree");
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* place=control<QPushButton>(window,"placeMeshAsset");
        auto* assign=control<QPushButton>(window,"assignMeshAsset");
        auto* replace=control<QAction>(window,"replaceMeshInstances");
        auto* remove=control<QAction>(window,"removeMeshAsset");
        chooseFile(PLATZ_TEST_MESH,[&]{
            QTest::mouseClick(control<QPushButton>(window,"importMeshAsset"),Qt::LeftButton);
        });
        check(library->isVisible() && library->topLevelItemCount()==1 && scene->count()==4 &&
              library->currentItem()->data(1,Qt::DisplayRole).toInt()==0,
              "Library import did not create a zero-use asset");
        QTest::mouseClick(place,Qt::LeftButton);
        objectId=scene->currentItem()->data(Qt::UserRole).toString().toStdString();
        check(scene->count()==5 && library->currentItem()->data(1,Qt::DisplayRole).toInt()==1,
              "Add instance failed to update the scene and reference count");
        enterText(control<QDoubleSpinBox>(window,"positionY"),"3.5");
        scene->setCurrentRow(3);
        const auto blockId=scene->currentItem()->data(Qt::UserRole);
        QTest::mouseClick(assign,Qt::LeftButton);
        check(scene->currentItem()->data(Qt::UserRole)==blockId &&
              library->currentItem()->data(1,Qt::DisplayRole).toInt()==2 && !remove->isEnabled(),
              "Assign replaced object identity or let a referenced mesh be removed");
        scene->setCurrentRow(4);
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_F);
        const auto before=settledFrame(*viewport);
        chooseFile(PLATZ_TEST_REPLACEMENT,[&]{replace->trigger();});
        expectedFrame=settledFrame(*viewport);
        check(expectedFrame.tokens!=before.tokens && library->topLevelItemCount()==2 &&
              library->currentItem()->text(0)=="Low spire" &&
              library->currentItem()->data(1,Qt::DisplayRole).toInt()==2,
              "Bulk replacement did not change the GPU mesh or update both references");
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);
        check(settledFrame(*viewport).tokens==before.tokens && library->topLevelItemCount()==1,
              "Undo left the replacement in the scene or catalog");
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);
        check(settledFrame(*viewport).tokens==expectedFrame.tokens,"Redo changed the replacement shape");
        for (int row=0;row<library->topLevelItemCount();++row)
            if (library->topLevelItem(row)->text(0)=="Oblique spire")
                library->setCurrentItem(library->topLevelItem(row));
        check(remove->isEnabled(),"Unused original asset could not be removed");
        remove->trigger();
        check(library->topLevelItemCount()==1,"Removing an unused asset left it in the library");

        auto* play=control<QPushButton>(window,"playButton");
        QTest::mouseClick(play,Qt::LeftButton);
        check(!place->isEnabled() && !assign->isEnabled() && !replace->isEnabled(),
              "Assets dock left authoring actions enabled during Play");
        const auto point=visiblePoint(settledFrame(*viewport),5,*viewport);
        QTest::mouseClick(viewport,Qt::LeftButton,{},point);
        check(settledFrame(*viewport).tokens!=expectedFrame.tokens,"Replaced mesh did not interact in Play");
        QTest::mouseClick(play,Qt::LeftButton);
        check(settledFrame(*viewport).rgba==expectedFrame.rgba,"Play altered the authored mesh after Stop");
        QTest::mouseClick(control<QPushButton>(window,"saveButton"),Qt::LeftButton);
        expectedObjects=platz::Project::open(file.toStdWString()).objects();
        check(window.close(),"Library project did not close after Save");
    }
    {
        StudioWindow window;
        expose(window);
        fileAction(window,Qt::Key_O,file);
        auto* library=control<QTreeWidget>(window,"meshLibrary");
        auto* scene=control<QListWidget>(window,"sceneTree");
        const auto saved=platz::Project::open(file.toStdWString());
        check(saved.find(objectId) && saved.objects()==expectedObjects && library->topLevelItemCount()==1 &&
              library->currentItem()->text(0)=="Low spire" &&
              library->currentItem()->data(1,Qt::DisplayRole).toInt()==2 && scene->count()==5,
              "Reopening lost the applied library, object IDs, or reference counts");
        check(window.close(),"Reopened library project did not close");
    }
    std::cout<<"Assets panel import, placement, assignment, GPU replacement, undo/redo, Play, removal and reopen passed\n";
}
void exerciseMaterialSlots(QTemporaryDir& dir) {
    const auto file=dir.filePath("Material slots.platz");
    const auto authored="#5A26C8";
    std::string objectId;
    Capture painted;
    {
        StudioWindow window;
        expose(window);
        fileAction(window,Qt::Key_N,file);
        chooseFile(PLATZ_TEST_MATERIALS,[&]{control<QAction>(window,"importGlb")->trigger();});
        auto* tree=control<QListWidget>(window,"sceneTree");
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* materials=control<QComboBox>(window,"materialSlot");
        auto* color=control<QLineEdit>(window,"materialColor");
        auto* reset=control<QPushButton>(window,"resetSlotColor");
        check(tree->count()==5 && materials->isVisible() && materials->count()==2 &&
              materials->itemText(0).contains("Copper") && materials->itemText(1).contains("Lagoon"),
              "Imported material slots were not listed in the Inspector.");
        objectId=tree->currentItem()->data(Qt::UserRole).toString().toStdString();
        enterText(control<QDoubleSpinBox>(window,"positionY"),"3.5");
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_F);
        const auto base=settledFrame(*viewport);
        const auto sourceColor=color->text();
        materials->setCurrentIndex(1);
        const auto lagoon=color->text();
        check(sourceColor!=lagoon && !reset->isEnabled(),"GLB defaults are not exposed correctly.");
        materials->setCurrentIndex(0);
        enterText(color,authored);
        painted=settledFrame(*viewport);
        check(base.tokens==painted.tokens && base.rgba!=painted.rgba && reset->isEnabled(),
              "Painting a slot did not recolor its geometry.");
        materials->setCurrentIndex(1);
        check(color->text()==lagoon && !reset->isEnabled(),"A slot edit changed the other slot.");
        materials->setCurrentIndex(0);
        reset->click();
        check(color->text()==sourceColor && settledFrame(*viewport).rgba==base.rgba,
              "Reset did not restore the original GPU color.");
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);
        check(settledFrame(*viewport).rgba==painted.rgba,"Reset could not be undone.");
        auto* play=control<QPushButton>(window,"playButton");
        QTest::mouseClick(play,Qt::LeftButton);
        check(!materials->isEnabled(),"Material authoring remained active during Play.");
        const auto point=visiblePoint(settledFrame(*viewport),5,*viewport);
        QTest::mouseClick(viewport,Qt::LeftButton,{},point);
        check(settledFrame(*viewport).tokens!=painted.tokens,"Play did not turn the multi-primitive object.");
        QTest::mouseClick(play,Qt::LeftButton);
        check(settledFrame(*viewport).rgba==painted.rgba,"Play changed authoring material or transforms.");
        QTest::mouseClick(control<QPushButton>(window,"saveButton"),Qt::LeftButton);
        check(window.close(),"Material project did not close after Save.");
    }
    {
        StudioWindow window;
        expose(window);
        fileAction(window,Qt::Key_O,file);
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* tree=control<QListWidget>(window,"sceneTree");
        tree->setCurrentRow(4);
        check(tree->currentItem()->data(Qt::UserRole).toString().toStdString()==objectId &&
              control<QLineEdit>(window,"materialColor")->text()==authored,
              "Reopened project lost the object ID or slot color.");
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_F);
        const auto reopened=settledFrame(*viewport);
        check(reopened.tokens==painted.tokens && reopened.rgba==painted.rgba,
              "Reopened material rendering differs from saved appearance.");
        check(window.close(),"Reopened material project did not close.");
    }
    std::cout<<"Material slot selection, editing, reset/undo, Play and pixel-identical reopen passed\n";
}
void exercise(const QString& path, bool create) {
    const std::filesystem::path file(path.toStdWString());
    std::string projectId, sceneId, editedId;
    std::vector<platz::Object> savedObjects;
    Capture expected;
    {
        StudioWindow window;
        expose(window);
        fileAction(window, create ? Qt::Key_N : Qt::Key_O, path);
        const auto before = platz::Project::open(file);
        projectId = before.id();
        sceneId = before.sceneId();
        check(before.objects().size() >= 4, "Scene has insufficient distinct objects");
        auto* viewport = static_cast<Viewport*>(control<QWidget>(window, "viewport"));
        auto* tree = control<QListWidget>(window, "sceneTree");
        auto* name = control<QLineEdit>(window, "objectName");
        auto* position = control<QDoubleSpinBox>(window, "positionX");
        auto* number=position->findChild<QLineEdit*>();
        check(number && number->visibleRegion().boundingRect().width()>=35,"Inspector numeric editor is clipped");
        auto* color = control<QLineEdit>(window, "materialColor");
        auto* play = control<QPushButton>(window, "playButton");
        check(viewport->isVisible() && tree->count() == int(before.objects().size()),
              "Opened project failed to populate the visible Studio");
        auto frame = settledFrame(*viewport);
        const auto initialCount=tree->count();
        tree->setCurrentRow(1);
        const auto originalFrame=settledFrame(*viewport);
        for (const bool apply : {false,true}) {
            bool previewVisible=false;
            QTimer::singleShot(30,[&]{
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog) return;
                control<QSpinBox>(*dialog,"repeatCount")->setValue(2);
                control<QDoubleSpinBox>(*dialog,"spacingY")->setValue(0.4);
                previewVisible=settledFrame(*viewport).tokens!=originalFrame.tokens;
                if (apply) dialog->accept(); else dialog->reject();
            });
            control<QAction>(window,"repeatObjects")->trigger();
            check(previewVisible,"Array preview did not reach the renderer");
            check(tree->count()==initialCount+(apply?2:0),"Array creation/cancellation changed wrong object count");
        }
        const auto arrayIds=std::array{tree->item(2)->data(Qt::UserRole),tree->item(3)->data(Qt::UserRole)};
        QTest::keyClick(tree,Qt::Key_Z,Qt::ControlModifier);
        check(tree->count()==initialCount,"Array undo was not atomic");
        QTest::keyClick(tree,Qt::Key_Y,Qt::ControlModifier);
        check(tree->item(2)->data(Qt::UserRole)==arrayIds[0] && tree->item(3)->data(Qt::UserRole)==arrayIds[1],
              "Array redo replaced copy identities");
        QTest::keyClick(tree,Qt::Key_Z,Qt::ControlModifier);
        // Add through Studio's creation menu. The file stays at its opened revision.
        auto* add = control<QPushButton>(window, "addObject");
        check(add->menu() && add->menu()->findChild<QAction*>("importGlb"), "Creation menu is unavailable");
        add->menu()->actions().front()->trigger();
        check(tree->count()==int(before.objects().size()+1), "Add object failed");
        const auto addedId=tree->currentItem()->data(Qt::UserRole).toString();
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_D,Qt::ControlModifier);
        check(tree->count()==int(before.objects().size()+2) && tree->currentItem()->data(Qt::UserRole).toString()!=addedId,
              "Duplicate did not select a new stable identity");
        tree->setFocus();
        QTest::keyClick(tree,Qt::Key_Delete);
        check(tree->count()==int(before.objects().size()+1),"Delete failed");
        // Undo delete, duplicate and add to return to the reference scene.
        for(int i=0;i<3;++i) QTest::keyClick(tree,Qt::Key_Z,Qt::ControlModifier);
        check(tree->count()==int(before.objects().size()),"Undo did not restore original object count");
        auto* dock=control<QDockWidget>(window,"inspectorDock");
        dock->toggleViewAction()->trigger();
        check(!dock->isVisible(),"Inspector docking visibility control failed");
        dock->toggleViewAction()->trigger();
        check(dock->isVisible(),"Inspector failed to reopen");
        frame=settledFrame(*viewport);
        QTest::keyClick(viewport, Qt::Key_Right);
        check(settledFrame(*viewport).tokens != frame.tokens, "Editor camera input did not update the GPU image");
        QTest::keyClick(viewport, Qt::Key_Home);
        frame = settledFrame(*viewport);
        // Select two different depth-tested objects by actual viewport clicks.
        for (unsigned token : {2u, 4u}) {
            const auto point = visiblePoint(frame, token, *viewport);
            QTest::mouseClick(viewport, Qt::LeftButton, {}, point);
            check(name->text().toStdString() == before.objects()[token - 1].name,
                  "Viewport picking and inspector target disagree");
            frame = settledFrame(*viewport);
        }
        const auto& target = before.objects()[3];
        editedId = target.id;
        name->setFocus();
        name->selectAll();
        QInputMethodEvent composed;
        composed.setCommitString(QString::fromUtf8("色彩"));
        QApplication::sendEvent(name,&composed);
        QTest::keyClick(name,Qt::Key_Return);
        check(name->text()==QString::fromUtf8("色彩"),"Unicode input-method commit failed");
        enterText(name, "Qualification object");
        enterText(position, QString::number(target.position.x + 0.35, 'f', 2));
        check(settledFrame(*viewport).tokens != frame.tokens, "Inspector transform failed to move rendered geometry");
        auto uncolored = settledFrame(*viewport);
        enterText(color, "#d32cbe");
        auto recolored = settledFrame(*viewport);
        check(recolored.tokens == uncolored.tokens && recolored.rgba != uncolored.rgba,
              "Inspector color failed to change the rendered material");
        viewport->setFocus();
        QTest::keyClick(viewport, Qt::Key_Z, Qt::ControlModifier);
        check(settledFrame(*viewport).rgba == uncolored.rgba, "Undo did not restore the GPU frame");
        QTest::keyClick(viewport, Qt::Key_Y, Qt::ControlModifier);
        check(settledFrame(*viewport).rgba == recolored.rgba, "Redo did not recover the material");
        check(platz::Project::open(file).objects() == before.objects(),
              "An unsaved inspector edit escaped into persisted source");
        QTest::mouseClick(play, Qt::LeftButton);
        check(!name->isEnabled() || name->isReadOnly(), "Authoring inspector remained editable during Play");
        auto runtime = settledFrame(*viewport);
        QTest::mouseClick(viewport, Qt::LeftButton, {}, visiblePoint(runtime, 4, *viewport));
        check(settledFrame(*viewport).rgba != runtime.rgba, "Runtime interaction failed to change visible output");
        QTest::keyClick(viewport, Qt::Key_W);
        check(settledFrame(*viewport).tokens != runtime.tokens, "Runtime navigation did not change the projection");
        QTest::keyClick(viewport, Qt::Key_Escape);
        check(settledFrame(*viewport).rgba == recolored.rgba, "Stopping Play did not restore authored rendering");
        QTest::keyClick(viewport, Qt::Key_S, Qt::ControlModifier);
        const auto saved = platz::Project::open(file);
        check(saved.id() == projectId && saved.sceneId() == sceneId, "Save replaced source identity");
        auto edited = saved.find(editedId);
        check(edited && edited->name == "Qualification object" &&
              std::abs(edited->position.x - (target.position.x + 0.35f)) < 0.001f,
              "Save omitted authored fields or included runtime state");
        savedObjects = saved.objects();
        for (const auto& object : before.objects())
            if (object.id != editedId)
                check(saved.find(object.id) && *saved.find(object.id) == object,
                      "Saving an edit changed an unrelated object");
        expected = recolored;
        check(window.close(), "Studio did not close after Save");
    }
    {
        StudioWindow reopened;
        expose(reopened);
        fileAction(reopened, Qt::Key_O, path);
        auto* viewport = static_cast<Viewport*>(control<QWidget>(reopened, "viewport"));
        auto frame = settledFrame(*viewport);
        QTest::mouseClick(viewport, Qt::LeftButton, {}, visiblePoint(frame, 4, *viewport));
        frame = settledFrame(*viewport);
        check(frame.width == expected.width && frame.height == expected.height &&
              frame.rgba == expected.rgba && frame.tokens == expected.tokens,
              "Reopened rendering or object identities differ from edited source");
        const auto saved = platz::Project::open(file);
        check(saved.id() == projectId && saved.sceneId() == sceneId && saved.objects() == savedObjects &&
              control<QLineEdit>(reopened, "objectName")->text() == "Qualification object",
              "Reopened Studio source or selected identity changed");
        check(reopened.close(), "Reopened Studio failed to close");
    }
    std::cout << (create ? "Ordinary project" : "Validation copy") << ": create/open, two viewport picks, "
        "inspector, GPU edit, undo/redo, Play/click/move/Stop, save and close/reopen passed\n";
}
}

int main(int argc, char** argv) {
    try {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication app(argc, argv);
        configureAppearance(app);
        app.setQuitOnLastWindowClosed(false);
        app.setOrganizationName("PLATZ-Tests");
        app.setApplicationName("StudioVerification");
        QTemporaryDir temp;
        check(temp.isValid(), "Cannot create isolated test-project directory");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        exercise(temp.filePath("Ordinary.platz"), true);
        const QString copy = temp.filePath("Validation copy.platz");
        check(QFile::copy(QString::fromUtf8(PLATZ_VALIDATION_PATH), copy), "Cannot copy permanent validation project");
        exercise(copy, false);
        exerciseImport(temp);
        exerciseAssetLibrary(temp);
        exerciseMaterialSlots(temp);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
