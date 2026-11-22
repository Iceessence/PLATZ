#include "StudioWindow.hpp"
#include "Appearance.hpp"
#include "RasterCanvas.hpp"
#include "Viewport.hpp"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDockWidget>
#include <QInputMethodEvent>
#include <QMenu>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QSpinBox>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
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
    check(viewport.presentScene(),"Could not present the current Studio scene");
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
void exerciseTextures(QTemporaryDir& dir) {
    const auto file=dir.filePath("Textured slots.platz");
    const auto source=dir.filePath("Harbor signs.glb");
    check(QFile::copy(PLATZ_TEST_TEXTURE,source),"Could not create disposable textured source.");
    Capture saved;
    std::string objectId;
    {
        StudioWindow window;
        expose(window);
        fileAction(window,Qt::Key_N,file);
        chooseFile(source,[&]{control<QAction>(window,"importGlb")->trigger();});
        auto* tree=control<QListWidget>(window,"sceneTree");
        objectId=tree->currentItem()->data(Qt::UserRole).toString().toStdString();
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* toggle=control<QCheckBox>(window,"useBaseColorTexture");
        auto* picker=control<QComboBox>(window,"materialSlot");
        check(picker->count()==2 && toggle->isVisible() && toggle->isChecked(),"New texture is missing from its material slot.");
        enterText(control<QDoubleSpinBox>(window,"positionY"),"3.5");
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_F);
        const auto textured=settledFrame(*viewport);
        toggle->click();
        saved=settledFrame(*viewport);
        check(!toggle->isChecked() && textured.tokens==saved.tokens && textured.rgba!=saved.rgba,
              "Texture switch did not change the viewport with stable picking IDs.");
        picker->setCurrentIndex(1);
        check(toggle->isChecked(),"Disabling PNG affected the JPEG slot.");
        picker->setCurrentIndex(0);
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);
        check(toggle->isChecked() && settledFrame(*viewport).rgba==textured.rgba,"Undo did not restore sampled pixels.");
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);
        check(!toggle->isChecked() && settledFrame(*viewport).rgba==saved.rgba,"Redo lost the texture override.");
        auto* play=control<QPushButton>(window,"playButton");
        QTest::mouseClick(play,Qt::LeftButton);
        check(!toggle->isEnabled(),"Texture authoring remained active in Play.");
        const auto point=visiblePoint(settledFrame(*viewport),5,*viewport);
        QTest::mouseClick(viewport,Qt::LeftButton,{},point);
        check(settledFrame(*viewport).tokens!=saved.tokens,"Play did not rotate the textured object.");
        QTest::mouseClick(play,Qt::LeftButton);
        check(settledFrame(*viewport).rgba==saved.rgba,"Play altered authored texture state.");
        QTest::mouseClick(control<QPushButton>(window,"saveButton"),Qt::LeftButton);
        check(window.close(),"Textured project did not close after saving.");
    }
    check(QFile::remove(source),"Could not remove the disposable textured GLB.");
    {
        StudioWindow window;
        expose(window);
        fileAction(window,Qt::Key_O,file);
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* tree=control<QListWidget>(window,"sceneTree");
        tree->setCurrentRow(4);
        check(tree->currentItem()->data(Qt::UserRole).toString().toStdString()==objectId &&
              !control<QCheckBox>(window,"useBaseColorTexture")->isChecked(),"Reopen lost the object or texture override.");
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_F);
        const auto reopened=settledFrame(*viewport);
        check(reopened.tokens==saved.tokens && reopened.rgba==saved.rgba,"Reopened texture rendering changed.");
        check(window.close(),"Reopened texture project did not close.");
    }
    std::cout<<"Studio texture switching, undo/redo, Play isolation and pixel-identical reopen passed\n";
}

void exerciseViewportTransforms(QTemporaryDir& dir, bool scaled) {
    using namespace platz::studio;
    const auto path=dir.filePath("Viewport transforms.platz");
    const std::filesystem::path file(path.toStdWString());
    StudioWindow window;
    expose(window);
    fileAction(window,Qt::Key_N,path);
    auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
    if (scaled) check(viewport->devicePixelRatioF()>1,"High DPI test requires a scaled viewport");
    auto* tree=control<QListWidget>(window,"sceneTree");
    auto* revision=control<QLabel>(window,"authoringMode");
    auto* position=control<QDoubleSpinBox>(window,"positionX");
    auto* scale=control<QDoubleSpinBox>(window,"scaleX");
    auto* play=control<QPushButton>(window,"playButton");
    const auto original=platz::Project::open(file);
    const auto& object=original.objects().at(3);
    const auto originalFrame=settledFrame(*viewport);
    QTest::mouseClick(viewport,Qt::LeftButton,{},visiblePoint(originalFrame,4,*viewport));
    check(tree->currentRow()==3,"Viewport transform test failed to select the block");
    const auto originalCaption=revision->text();

    auto cursor=[&](QPoint point,Qt::MouseButtons buttons=Qt::LeftButton) {
        QMouseEvent move(QEvent::MouseMove,point,viewport->mapToGlobal(point),Qt::NoButton,buttons,Qt::NoModifier);
        QApplication::sendEvent(viewport,&move);
    };
    auto start=[&](QPoint point) {
        QTest::mousePress(viewport,Qt::LeftButton,{},point);
        check(viewport->dragging(),"Manipulator failed to capture a valid handle");
    };
    auto release=[&](QPoint point) {QTest::mouseRelease(viewport,Qt::LeftButton,{},point);};
    auto key=[&](Qt::Key code,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        viewport->setFocus();
        QTest::keyClick(viewport,code,modifiers);
        QApplication::processEvents();
    };
    auto lineDrag=[&]() {
        const auto paths=viewport->manipulatorPaths();
        const auto& line=paths.axes[0];
        check(line.size()==2,"Expected a projected X axis");
        const auto point=(line.front()*.3+line.back()*.7).toPoint();
        check(hitGizmo(paths,point)==0,"Expected an unambiguous X axis start");
        return std::pair{point,(line.back()+(line.back()-line.front())*.85).toPoint()};
    };
    key(Qt::Key_2);
    check(control<QToolButton>(window,"toolMove")->isChecked(),"Viewport keyboard shortcut failed");
    const auto [from,to]=lineDrag();
    const auto before=settledFrame(*viewport);
    check(before.tokens==originalFrame.tokens && before.rgba!=originalFrame.rgba,
          "Rendered viewport handles changed object IDs or failed to appear");
    start(from);
    release(from);
    check(revision->text()==originalCaption,"A zero-motion drag created a revision");
    start(from);
    const auto begin=std::chrono::steady_clock::now();
    cursor(to);
    const auto preview=settledFrame(*viewport);
    const auto measured=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    check(std::abs(position->value()-object.position.x)>.15 && preview.tokens!=before.tokens,
          "Live drag did not update the Inspector and GPU geometry");
    check(platz::Project::open(file).revision()==original.revision(),"Live preview wrote project data");
    key(Qt::Key_Escape);
    release(to);
    check(!viewport->dragging() && settledFrame(*viewport).tokens==before.tokens &&
          revision->text()==originalCaption,"Escape left a preview or revision");
    start(from);
    cursor(to);
    cursor(to,Qt::NoButton);
    release(to);
    check(!viewport->dragging() && revision->text()==originalCaption &&
          settledFrame(*viewport).tokens==before.tokens,"Lost mouse capture left a preview or revision");
    start(from);
    cursor(to);
    const double moved=position->value();
    release(to);
    check(!viewport->dragging() && position->value()==moved && revision->text()!=originalCaption,
          "Release did not commit the displayed move");
    key(Qt::Key_Z,Qt::ControlModifier);
    check(std::abs(position->value()-object.position.x)<.001 &&
          settledFrame(*viewport).tokens==before.tokens,"Drag required more than one Undo");
    key(Qt::Key_Y,Qt::ControlModifier);
    check(position->value()==moved && settledFrame(*viewport).tokens==preview.tokens,"Redo lost the move");

    key(Qt::Key_3);
    const auto rings=viewport->manipulatorPaths();
    int rotatedAxis=-1;
    QPoint ringStart, ringEnd;
    for (int axis:{1,2,0}) {
        const auto& points=rings.axes[axis];
        for (size_t index=0;index+9<points.size();++index) {
            const auto candidate=points[index].toPoint();
            if (hitGizmo(rings,candidate)!=axis || !viewport->rect().contains(candidate)) continue;
            ringStart=candidate;
            ringEnd=points[index+9].toPoint();
            rotatedAxis=axis;
            break;
        }
        if (rotatedAxis>=0) break;
    }
    check(rotatedAxis>=0,"Expected an accessible rotation ring");
    const auto unrotated=settledFrame(*viewport);
    start(ringStart);
    cursor(ringEnd);
    const auto rotated=settledFrame(*viewport);
    check(rotated.tokens!=unrotated.tokens,"Rotation ring did not change rendered geometry");
    release(ringEnd);
    check(settledFrame(*viewport).tokens==rotated.tokens,"Release did not preserve rotation");

    key(Qt::Key_4);
    QTest::mouseClick(control<QToolButton>(window,"transformSnap"),Qt::LeftButton);
    const auto [scaleFrom,scaleTo]=lineDrag();
    const auto unscaled=settledFrame(*viewport);
    start(scaleFrom);
    cursor(scaleTo);
    const auto scaledX=scale->value();
    const auto increment=control<QDoubleSpinBox>(window,"transformStep")->value();
    check(std::abs(scaledX-object.scale.x)>.05 &&
          std::abs(scaledX/increment-std::round(scaledX/increment))<.015,
          "Scale did not update the Inspector or honor its snap step");
    release(scaleTo);
    const auto authored=settledFrame(*viewport);
    check(authored.tokens!=unscaled.tokens,"Snapped Scale did not change rendered geometry");
    QTest::mouseClick(play,Qt::LeftButton);
    const auto runtimePaths=viewport->manipulatorPaths();
    check(!control<QToolButton>(window,"toolScale")->isEnabled() &&
          std::all_of(runtimePaths.axes.begin(),runtimePaths.axes.end(),[](const auto& axis){return axis.empty();}),
          "Authoring manipulator remained active during Play");
    key(Qt::Key_Escape);
    check(settledFrame(*viewport).tokens==authored.tokens,"Play altered the committed transform");
    key(Qt::Key_S,Qt::ControlModifier);
    const auto saved=platz::Project::open(file);
    const auto* edited=saved.find(object.id);
    check(edited && std::abs(edited->position.x-moved)<.001 && edited->rotation!=object.rotation &&
          std::abs(edited->scale.x-scaledX)<.001 && edited->scale.y==object.scale.y &&
          edited->scale.z==object.scale.z,"Save omitted or corrupted the committed transform");
    check(window.close(),"Could not close transformed project");
    StudioWindow reopened;
    expose(reopened);
    fileAction(reopened,Qt::Key_O,path);
    auto* view=static_cast<Viewport*>(control<QWidget>(reopened,"viewport"));
    check(settledFrame(*view).tokens==authored.tokens,"Reopen did not render the saved transforms");
    check(reopened.close(),"Could not close reopened transform project");
    std::cout<<"Studio Move/Rotate/Scale qualified at DPR "<<viewport->devicePixelRatioF()
             <<"; sampled move through synchronous Vulkan capture: "<<measured<<" ms\n";
}

void exerciseTransformMath() {
    using namespace platz::studio;
    platz::engine::Camera camera;
    camera.target={0,0,0};
    camera.yaw=0;
    camera.pitch=0;
    camera.distance=10;
    const QSizeF extent(800,600);
    const auto pixelPerUnit=30/std::tan(platz::engine::perspectiveFovDegrees*std::numbers::pi/360);
    const TransformValue original{{-1.2f,0,0},{0,0,90},{1,2,3}};
    TransformSettings move{TransformTool::Move,false,true,.5f};
    auto paths=gizmoPaths(camera,extent,original,move);
    auto world=TransformDrag::start(camera,extent,original,move,0,paths.axes[0].back());
    check(world.has_value(),"Could not start world-space math test");
    check(world->move(paths.axes[0].back()).position==original.position,"Snapping moved a zero-distance drag");
    const auto left=world->move(paths.axes[0].back()-QPointF(pixelPerUnit*.61,0));
    check(std::abs(left.position.x+2)<.001 && left.position.y==0 && left.position.z==0,
          "Signed world-axis distance did not snap to the nearest negative coordinate");
    move.local=true;
    move.snapping=false;
    paths=gizmoPaths(camera,extent,original,move);
    auto local=TransformDrag::start(camera,extent,original,move,0,paths.axes[0].back());
    check(local.has_value(),"Could not start local-space math test");
    const auto up=local->move(paths.axes[0].back()-QPointF(0,pixelPerUnit));
    check(std::abs(up.position.x-original.position.x)<.001 && std::abs(up.position.y-1)<.001,
          "Local X did not translate along the rotated basis");
    const TransformSettings scale{TransformTool::Scale,false,false,1};
    paths=gizmoPaths(camera,extent,original,scale);
    auto scaling=TransformDrag::start(camera,extent,original,scale,0,paths.axes[0].back());
    check(scaling.has_value(),"Could not start scale math test");
    const auto enlarged=scaling->move(paths.axes[0].back()-QPointF(0,76));
    check(std::abs(enlarged.scale.x-2)<.001 && enlarged.scale.y==2 && enlarged.scale.z==3,
          "Nonuniform scale changed another local component");
    const TransformSettings rotate{TransformTool::Rotate,false,true,15};
    paths=gizmoPaths(camera,extent,original,rotate);
    auto rotation=TransformDrag::start(camera,extent,original,rotate,2,paths.axes[2].at(0));
    check(rotation.has_value(),"Could not start ring math test");
    check(std::abs(std::abs(rotation->move(paths.axes[2].at(16)).rotation.z)-180)<.01,
          "A quarter turn around Z did not compose with existing orientation");
}

void exerciseInspectorPreviews(QTemporaryDir& dir) {
    const auto path=dir.filePath("Inspector previews.platz");
    const std::filesystem::path file(path.toStdWString());
    StudioWindow window;
    expose(window);
    fileAction(window,Qt::Key_N,path);
    const auto source=platz::Project::open(file);
    const auto& object=source.objects().at(3);
    auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
    auto* tree=control<QListWidget>(window,"sceneTree");
    tree->setCurrentRow(3);
    auto* color=control<QLineEdit>(window,"materialColor");
    auto* spin=control<QDoubleSpinBox>(window,"positionX");
    auto* editor=spin->findChild<QLineEdit*>();
    auto* mode=control<QLabel>(window,"authoringMode");
    const auto initial=settledFrame(*viewport);
    const auto revision=mode->text();
    const auto originalColor=color->text();
    const auto originalNumber=editor->text();
    auto draft=[](QLineEdit* field,const QString& value){
        field->setFocus();
        QTest::keyClick(field,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClicks(field,value);
    };
    auto key=[&](Qt::Key code,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
        viewport->setFocus();
        QTest::keyClick(viewport,code,modifiers);
    };
    const auto editedX=spin->locale().toString(object.position.x+.65,'f',2);
    draft(editor,editedX);
    const auto moved=settledFrame(*viewport);
    check(moved.tokens!=initial.tokens && mode->text()!=revision,"Typed number did not preview before Enter");
    QTest::keyClick(editor,Qt::Key_Escape);
    check(editor->text()==originalNumber && mode->text()==revision &&
          settledFrame(*viewport).tokens==initial.tokens,"Escape did not restore typed numeric preview");
    draft(editor,"-");
    key(Qt::Key_Home);
    check(editor->text()==originalNumber && mode->text()==revision,
          "Leaving an incomplete number did not restore the exact authoring value");
    draft(editor,editedX);
    QTest::keyClick(editor,Qt::Key_Return);
    check(settledFrame(*viewport).tokens==moved.tokens,"Enter changed the rendered numeric preview");
    key(Qt::Key_Z,Qt::ControlModifier);
    check(settledFrame(*viewport).tokens==initial.tokens,"Typing created more than one Undo");
    key(Qt::Key_Y,Qt::ControlModifier);

    auto* scrub=control<QLabel>(window,"positionXScrub");
    const QPoint from=scrub->rect().center(), to=from+QPoint(31,0);
    auto scrubMove=[&]{
        QTest::mousePress(scrub,Qt::LeftButton,{},from);
        QMouseEvent move(QEvent::MouseMove,to,scrub->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(scrub,&move);
    };
    scrubMove();
    const auto scrubbed=settledFrame(*viewport);
    check(scrubbed.tokens!=moved.tokens,"Numeric label scrub did not preview");
    key(Qt::Key_Escape);
    QTest::mouseRelease(scrub,Qt::LeftButton,{},to);
    check(settledFrame(*viewport).tokens==moved.tokens && QWidget::mouseGrabber()!=scrub,
          "Cancelling a scrub left a visible change or active grab");
    scrubMove();
    QTest::mouseRelease(scrub,Qt::LeftButton,{},to);
    check(settledFrame(*viewport).tokens==scrubbed.tokens,"Releasing the scrub failed to commit");
    key(Qt::Key_Z,Qt::ControlModifier);
    check(settledFrame(*viewport).tokens==moved.tokens,"Numeric scrub created more than one Undo");

    draft(color,"#3BC9ED");
    const auto tinted=settledFrame(*viewport);
    check(tinted.tokens==moved.tokens && tinted.rgba!=moved.rgba,
          "Typing a complete color did not preview before Enter");
    QTest::keyClick(color,Qt::Key_Escape);
    check(color->text()==originalColor && settledFrame(*viewport).rgba==moved.rgba,
          "Cancelling a color draft did not restore the original pixels");
    draft(color,"#3BC");
    QTest::keyClick(color,Qt::Key_Return);
    check(color->text()==originalColor && settledFrame(*viewport).rgba==moved.rgba,
          "Invalid color commit changed authored pixels");
    for (const auto apply : {false,true}) {
        bool sawPreview=false;
        QTimer::singleShot(30,[&]{
            auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->setCurrentColor(QColor("#3BC9ED"));
            sawPreview=settledFrame(*viewport).rgba==tinted.rgba;
            if (apply) dialog->accept(); else dialog->reject();
        });
        QTest::mouseClick(control<QPushButton>(window,"colorPicker"),Qt::LeftButton);
        check(sawPreview,"Live color dialog preview did not match hex presentation");
        check(settledFrame(*viewport).rgba==(apply?tinted.rgba:moved.rgba),
              "Color dialog did not apply or cancel the displayed preview");
    }
    key(Qt::Key_Z,Qt::ControlModifier);
    check(settledFrame(*viewport).rgba==moved.rgba,"Color picker did not produce one Undo entry");
    key(Qt::Key_Y,Qt::ControlModifier);
    check(platz::Project::open(file).objects()==source.objects(),"Inspector drafts escaped to disk");
    key(Qt::Key_S,Qt::ControlModifier);
    const auto saved=platz::Project::open(file);
    const auto* committed=saved.find(object.id);
    check(committed && std::abs(committed->position.x-(object.position.x+.65f))<.001 &&
          committed->color!=object.color,"Saved Inspector source did not match the committed preview");
    check(window.close(),"Inspector preview test project did not close");
    StudioWindow reopened;
    expose(reopened);
    fileAction(reopened,Qt::Key_O,path);
    auto* view=static_cast<Viewport*>(control<QWidget>(reopened,"viewport"));
    control<QListWidget>(reopened,"sceneTree")->setCurrentRow(3);
    const auto reloaded=settledFrame(*view);
    check(reloaded.tokens==tinted.tokens && reloaded.rgba==tinted.rgba,"Inspector draft save/reopen changed pixels");
    check(reopened.close(),"Reopened Inspector preview project did not close");
    std::cout<<"Live numeric input, scrubbing and color previews passed with cancellation and persisted undoable commits\n";
}

void exerciseNativeGeometry(QTemporaryDir& dir) {
    const auto path=dir.filePath("Native geometry validation copy.platz");
    const std::filesystem::path file(path.toStdWString());
    check(QFile::copy(QString::fromUtf8(PLATZ_VALIDATION_PATH),path),"Cannot copy validation project for native geometry");
    const auto original=platz::Project::open(file);
    StudioWindow window;
    expose(window);
    fileAction(window,Qt::Key_O,path);
    auto* view=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
    auto* tree=control<QListWidget>(window,"sceneTree");
    auto* library=control<QTreeWidget>(window,"meshLibrary");
    auto* play=control<QPushButton>(window,"playButton");
    auto getKey=[&]{return library->currentItem()->data(0,Qt::UserRole).toString().toStdString();};
    const auto count=tree->count();
    const auto start=settledFrame(*view);
    using Clock=std::chrono::steady_clock;
    auto parameters=[&](const char* action,bool commit,const std::function<void(QWidget&)>& configure) {
        std::optional<Capture> before, after;
        std::exception_ptr failure;
        Clock::time_point changed;
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        QTimer observer;
        QObject::connect(&observer,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName()!="nativeMeshDialog") return;
            if (Clock::now()>deadline) {
                failure=std::make_exception_ptr(std::runtime_error("Native mesh preview never became ready"));
                dialog->reject();
                observer.stop();
                return;
            }
            try {
                auto* accept=control<QPushButton>(*dialog,"acceptNativeMesh");
                if (!accept->isEnabled()) return;
                if (!before) {
                    before=settledFrame(*view);
                    changed=Clock::now();
                    configure(*dialog);
                    return;
                }
                after=settledFrame(*view);
                std::cout<<"Native preview input through synchronous capture (includes debounce): "
                         <<std::chrono::duration<double,std::milli>(Clock::now()-changed).count()<<" ms\n";
                observer.stop();
                if (commit) QTest::mouseClick(accept,Qt::LeftButton);
                else QTest::keyClick(dialog,Qt::Key_Escape);
            } catch (...) {
                failure=std::current_exception();
                observer.stop();
                dialog->reject();
            }
        });
        observer.start(15);
        control<QAction>(window,action)->trigger();
        observer.stop();
        if (failure) std::rethrow_exception(failure);
        check(before && after && before->tokens!=after->tokens,"Native parameters did not update Vulkan geometry live");
        return *after;
    };
    parameters("createNativeCube",false,[](QWidget& dialog){
        control<QDoubleSpinBox>(dialog,"meshWidth")->setValue(1.85);
        control<QSpinBox>(dialog,"meshSegments")->setValue(3);
    });
    check(tree->count()==count && library->topLevelItemCount()==int(original.meshLibrary().size()) &&
          settledFrame(*view).rgba==start.rgba,"Cancelling creation did not restore the exact authored scene");
    const auto created=parameters("createNativeCylinder",true,[](QWidget& dialog){
        control<QLineEdit>(dialog,"nativeMeshName")->setText("Editable taper");
        control<QDoubleSpinBox>(dialog,"meshHeight")->setValue(1.7);
        control<QDoubleSpinBox>(dialog,"meshTopScale")->setValue(.55);
        control<QSpinBox>(dialog,"meshSegments")->setValue(12);
    });
    check(tree->count()==count+1 && library->currentItem()->data(1,Qt::DisplayRole).toInt()==1 &&
          settledFrame(*view).tokens==created.tokens,"Create did not place the previewed reusable geometry");
    const auto key=getKey();
    const auto firstId=tree->currentItem()->data(Qt::UserRole).toString().toStdString();
    check(key.starts_with("native:"),"Asset library did not expose a native stable identity");
    QTest::mouseClick(control<QPushButton>(window,"placeMeshAsset"),Qt::LeftButton);
    check(library->currentItem()->data(1,Qt::DisplayRole).toInt()==2,"Second instance did not link to the asset");
    const auto secondId=tree->currentItem()->data(Qt::UserRole).toString().toStdString();
    enterText(control<QDoubleSpinBox>(window,"positionX"),"2.60");
    enterText(control<QLineEdit>(window,"materialColor"),"#38C9AB");
    view->setFocus();
    QTest::keyClick(view,Qt::Key_Home);
    const auto beforeEdit=settledFrame(*view);
    const auto edited=parameters("editNativeMesh",true,[](QWidget& dialog){
        control<QDoubleSpinBox>(dialog,"meshWidth")->setValue(1.45);
        control<QDoubleSpinBox>(dialog,"meshTopScale")->setValue(1.3);
        control<QSpinBox>(dialog,"meshSegments")->setValue(7);
    });
    check(edited.tokens!=beforeEdit.tokens && getKey()==key && library->currentItem()->data(1,Qt::DisplayRole).toInt()==2 &&
          control<QLineEdit>(window,"materialColor")->text()=="#38C9AB" &&
          control<QDoubleSpinBox>(window,"positionX")->value()==2.6 &&
          settledFrame(*view).tokens==edited.tokens,"Native edit failed to preserve instance identity, material or transform");
    view->setFocus();
    QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier);
    check(settledFrame(*view).rgba==beforeEdit.rgba,"One Undo did not restore shared geometry in both instances");
    QTest::keyClick(view,Qt::Key_Y,Qt::ControlModifier);
    check(settledFrame(*view).rgba==edited.rgba,"Redo did not restore the edited native geometry");

    QTest::mouseClick(play,Qt::LeftButton);
    check(!control<QAction>(window,"createNativeCylinder")->isEnabled() &&
          !control<QAction>(window,"editNativeMesh")->isEnabled(),"Native geometry controls remained active in Play");
    const auto runtime=settledFrame(*view);
    QTest::mouseClick(view,Qt::LeftButton,{},visiblePoint(runtime,count+2,*view));
    check(settledFrame(*view).tokens!=runtime.tokens,"Play did not interact with the native mesh");
    QTest::keyClick(view,Qt::Key_Escape);
    check(settledFrame(*view).rgba==edited.rgba,"Play changed authored native geometry");
    QTest::mouseClick(control<QPushButton>(window,"locateMeshSource"),Qt::LeftButton);
    check(getKey()==key,"Locate Source did not select the correct editable asset");

    auto nameAsset=[&](const char* action,const QString& name) {
        QTimer observer;
        bool answered=false;
        QObject::connect(&observer,&QTimer::timeout,[&]{
            auto* input=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!input) return;
            input->setTextValue(name);
            answered=true;
            observer.stop();
            input->accept();
        });
        observer.start(15);
        control<QAction>(window,action)->trigger();
        observer.stop();
        check(answered,"Native asset action did not open its naming dialog");
    };
    nameAsset("renameMeshAsset","Workshop taper");
    check(getKey()==key && library->currentItem()->text(0)=="Workshop taper","Rename changed the native ID");
    nameAsset("duplicateNativeMesh","Independent taper");
    const auto independent=getKey();
    check(independent!=key && library->currentItem()->data(1,Qt::DisplayRole).toInt()==0,
          "Duplicated source had incorrect instance references");
    QTest::mouseClick(control<QPushButton>(window,"locateMeshSource"),Qt::LeftButton);
    nameAsset("detachNativeMesh","Unique taper");
    check(getKey()!=key && getKey()!=independent && library->currentItem()->data(1,Qt::DisplayRole).toInt()==1 &&
          settledFrame(*view).rgba==edited.rgba,"Making an instance unique disturbed its geometry or tint");
    view->setFocus();
    QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier);
    QTest::mouseClick(control<QPushButton>(window,"locateMeshSource"),Qt::LeftButton);
    check(getKey()==key && library->currentItem()->data(1,Qt::DisplayRole).toInt()==2,
          "Undo did not restore the original shared asset relationship");

    parameters("createNativeSphere",true,[](QWidget& dialog){
        control<QCheckBox>(dialog,"placeNativeMesh")->setChecked(false);
        control<QDoubleSpinBox>(dialog,"meshWidth")->setValue(1.4);
        control<QSpinBox>(dialog,"meshSegments")->setValue(128);
        control<QSpinBox>(dialog,"meshRings")->setValue(64);
    });
    const auto sphereKey=getKey();
    check(sphereKey!=key && library->currentItem()->data(1,Qt::DisplayRole).toInt()==0 &&
          tree->count()==count+2 && settledFrame(*view).rgba==edited.rgba,
          "Library-only creation left a transient instance in the authored scene");
    QTest::mouseClick(control<QPushButton>(window,"locateMeshSource"),Qt::LeftButton);

    const auto glb=dir.filePath("Generated taper.glb");
    chooseFile(glb,[&]{QTest::mouseClick(control<QPushButton>(window,"saveEmbeddedGlb"),Qt::LeftButton);});
    const auto exported=platz::MeshAsset::load(std::filesystem::path(glb.toStdWString()));
    check(exported->primitives().front().hasUv && exported->indices().size()==84,
          "Generated GLB did not round-trip its expected UVs and triangles");
    view->setFocus();
    QTest::keyClick(view,Qt::Key_S,Qt::ControlModifier);
    auto saved=platz::Project::open(file);
    check(saved.find(firstId) && saved.find(secondId) && saved.find(firstId)->asset==saved.find(secondId)->asset &&
          saved.find(secondId)->asset.key()==key && saved.find(secondId)->asset.data->digest()==exported->digest(),
          "Saved instances did not retain shared source or current export geometry");
    check(saved.meshLibrary().at(sphereKey).native->segments==128 &&
          saved.meshLibrary().at(independent).asset.data->digest()==exported->digest() &&
          saved.meshLibrary().at(key).name=="Workshop taper","Saving lost an unused source or its name");
    for (const auto& object:original.objects())
        check(saved.find(object.id) && *saved.find(object.id)==object,"Native editing changed a legacy object");
    check(window.close(),"Native geometry scene did not close after saving");
    StudioWindow reopened;
    expose(reopened);
    fileAction(reopened,Qt::Key_O,path);
    auto* reopenedView=static_cast<Viewport*>(control<QWidget>(reopened,"viewport"));
    const auto pixels=settledFrame(*reopenedView);
    check(pixels.tokens==edited.tokens,"Reopened native geometry changed its rendered coverage");
    auto* assets=control<QTreeWidget>(reopened,"meshLibrary");
    for (int i=0;i<assets->topLevelItemCount();++i) {
        auto* item=assets->topLevelItem(i);
        if (item->data(0,Qt::UserRole).toString().toStdString()==key) assets->setCurrentItem(item);
    }
    check(control<QAction>(reopened,"editNativeMesh")->isEnabled() && assets->currentItem()->data(1,Qt::DisplayRole).toInt()==2,
          "Reopened native source was not editable or lost instances");
    check(reopened.close(),"Reopened native geometry scene did not close");
    std::cout<<"Studio native create, cancel, linked edit, Undo/Redo, Play, GLB export and v7 reopen passed\n";
}

void exerciseNativeImages(QTemporaryDir& dir) {
    using Clock=std::chrono::steady_clock;
    const auto path=dir.filePath("Editable image copy.platz");
    const std::filesystem::path file(path.toStdWString());
    check(QFile::copy(QString::fromUtf8(PLATZ_VALIDATION_PATH),path),"Cannot copy validation project for image authoring");
    const auto original=platz::Project::open(file);
    StudioWindow window;
    expose(window);
    fileAction(window,Qt::Key_O,path);
    auto* view=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
    auto* tree=control<QListWidget>(window,"sceneTree");
    auto* images=control<QTreeWidget>(window,"imageLibrary");
    auto* tabs=control<QTabWidget>(window,"assetTabs");
    auto* material=control<QComboBox>(window,"baseColorImage");
    auto key=[&](Qt::Key code,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
        view->setFocus();
        QTest::keyClick(view,code,modifiers);
    };
    auto acceptMesh=[&](const char* action,bool adjust){
        QTimer timer;
        bool accepted=false, updated=false;
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        QObject::connect(&timer,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName()!="nativeMeshDialog") return;
            if (Clock::now()>deadline) { timer.stop(); dialog->reject(); return; }
            auto* accept=dialog->findChild<QPushButton*>("acceptNativeMesh");
            if (!accept->isEnabled()) return;
            if (adjust && !updated) {
                updated=true;
                dialog->findChild<QDoubleSpinBox*>("meshWidth")->setValue(1.5);
                return;
            }
            accepted=true;
            timer.stop();
            QTest::mouseClick(accept,Qt::LeftButton);
        });
        timer.start(20);
        control<QAction>(window,action)->trigger();
        timer.stop();
        check(accepted,"Could not create the native mesh for the image workflow");
    };
    acceptMesh("createNativeCube",false);
    const auto cubeId=tree->currentItem()->data(Qt::UserRole).toString().toStdString();
    const auto baseline=settledFrame(*view);
    auto visitEditor=[&](const std::function<void()>& open,bool apply,const std::function<void(QWidget&,RasterCanvas&)>& edit) {
        QTimer watcher;
        std::optional<Capture> before,after;
        QImage output;
        std::exception_ptr failure;
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        Clock::time_point edited;
        QObject::connect(&watcher,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName()!="imageEditor") return;
            if (Clock::now()>deadline) {
                failure=std::make_exception_ptr(std::runtime_error("Image preview did not settle"));
                watcher.stop();
                dialog->reject();
                return;
            }
            try {
                auto* accept=control<QPushButton>(*dialog,"acceptNativeImage");
                if (!accept->isEnabled()) return;
                auto* canvas=static_cast<RasterCanvas*>(control<QWidget>(*dialog,"imageCanvas"));
                if (!before) {
                    before=settledFrame(*view);
                    watcher.stop();
                    edit(*dialog,*canvas);
                    edited=Clock::now();
                    watcher.start(15);
                    return;
                }
                watcher.stop();
                after=settledFrame(*view);
                output=canvas->image();
                std::cout<<"Native image final input to synchronous preview capture: "
                         <<std::chrono::duration<double,std::milli>(Clock::now()-edited).count()<<" ms\n";
                if (apply) QTest::mouseClick(accept,Qt::LeftButton);
                else QTest::keyClick(dialog,Qt::Key_Escape);
            } catch (...) {
                failure=std::current_exception();
                watcher.stop();
                dialog->reject();
            }
        });
        watcher.start(15);
        open();
        watcher.stop();
        if (failure) std::rethrow_exception(failure);
        check(before && after && before->tokens==after->tokens && before->rgba!=after->rgba,
              "Canvas draft did not change the Vulkan material while preserving geometry");
        return std::pair{output,*after};
    };
    auto brush=[&](QWidget& dialog,const QString& hex,const char* tool) {
        auto* color=control<QLineEdit>(dialog,"imageBrushRgba");
        color->setFocus();
        color->setText(hex);
        QTest::mouseClick(control<QToolButton>(dialog,tool),Qt::LeftButton);
    };
    const auto created=visitEditor([&]{control<QAction>(window,"createNativeImage")->trigger();},true,
        [&](QWidget& dialog,RasterCanvas& canvas){
            check(control<QCheckBox>(dialog,"bindNewImage")->isChecked(),"Native mesh slot did not offer a live assignment");
            control<QLineEdit>(dialog,"imageName")->setText("Harbor paintwork");
            control<QSpinBox>(dialog,"imageWidth")->setValue(8);
            control<QSpinBox>(dialog,"imageHeight")->setValue(8);
            brush(dialog,"#D84F31FF","imageFill");
            QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(4,4));
            check(canvas.image().pixelColor(7,7)==QColor("#D84F31"),"Connected fill missed an image pixel");
            brush(dialog,"#16DDB396","imagePencil");
            const QPoint from(12,12), to(44,12);
            QTest::mousePress(&canvas,Qt::LeftButton,{},from);
            QMouseEvent move(QEvent::MouseMove,to,canvas.mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(&canvas,&move);
            QTest::mouseRelease(&canvas,Qt::LeftButton,{},to);
            check(canvas.image().pixelColor(3,1)==QColor(22,221,179,150),"Pencil skipped interpolated pixels");
            QTest::mouseClick(control<QToolButton>(dialog,"undoImageStroke"),Qt::LeftButton);
            check(canvas.image().pixelColor(3,1)==QColor("#D84F31"),"Canvas Undo did not revert the whole stroke");
            QTest::mouseClick(control<QToolButton>(dialog,"redoImageStroke"),Qt::LeftButton);
            QTest::mouseClick(control<QToolButton>(dialog,"imageEyedropper"),Qt::LeftButton);
            QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(28,12));
            check(control<QLineEdit>(dialog,"imageBrushRgba")->text()=="#16DDB396","Eyedropper lost RGBA channels");
            QTest::mouseClick(control<QToolButton>(dialog,"imageEraser"),Qt::LeftButton);
            QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(52,52));
            check(canvas.image().pixelColor(6,6)==QColor(0,0,0,0),"Eraser did not clear a pixel");
            brush(dialog,"#1280FA00","imagePencil");
            QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(44,44));
            check(canvas.image().pixelColor(5,5)==QColor(18,128,250,0),"Canvas lost RGB of a transparent pixel");
        });
    const auto id=images->currentItem()->data(0,Qt::UserRole).toString();
    check(images->topLevelItemCount()==1 && images->currentItem()->data(1,Qt::DisplayRole).toInt()==1 &&
          material->currentData().toString()==id && settledFrame(*view).rgba==created.second.rgba,
          "Creating the image did not publish its current material assignment");
    check(platz::Project::open(file).objects()==original.objects(),"Draft image changed the on-disk validation copy");
    key(Qt::Key_Z,Qt::ControlModifier);
    check(images->topLevelItemCount()==0,"One project Undo did not remove the created image");
    check(settledFrame(*view).rgba==baseline.rgba,"Image creation Undo did not restore the baseline Vulkan pixels");
    key(Qt::Key_Y,Qt::ControlModifier);
    tabs->setCurrentIndex(1);
    auto edit=[&]{QTest::mouseClick(control<QPushButton>(window,"editNativeImage"),Qt::LeftButton);};
    auto addStripe=[&](QWidget& dialog,RasterCanvas& canvas){
        brush(dialog,"#FFD52CC0","imagePencil");
        QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(12,20));
        QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(20,20));
        QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(28,20));
    };
    visitEditor(edit,false,addStripe);
    check(settledFrame(*view).rgba==created.second.rgba,"Cancelling image editing left draft pixels in the viewport");
    const auto retinted=visitEditor(edit,true,addStripe);
    const auto expected=rasterSource(retinted.first);
    key(Qt::Key_Z,Qt::ControlModifier);
    check(settledFrame(*view).rgba==created.second.rgba,"One project Undo did not restore the source texture");
    key(Qt::Key_Y,Qt::ControlModifier);
    check(settledFrame(*view).rgba==retinted.second.rgba,"Redo failed to restore edited texture pixels");

    const auto png=dir.filePath("Harbor paintwork exported.png");
    chooseFile(png,[&]{control<QAction>(window,"exportLibraryPng")->trigger();});
    const auto pngRaster=rasterSource(QImage(png).convertToFormat(QImage::Format_RGBA8888));
    check(platz::sameRaster(pngRaster,expected),"PNG export lost image dimensions or RGBA");
    chooseFile(png,[&]{QTest::mouseClick(control<QPushButton>(window,"importImagePng"),Qt::LeftButton);});
    const auto imported=images->currentItem()->data(0,Qt::UserRole).toString().toStdString();
    check(imported!=id.toStdString() && images->topLevelItemCount()==2 &&
          images->currentItem()->data(1,Qt::DisplayRole).toInt()==0,"PNG import did not produce a separate editable source");
    QTest::mouseClick(control<QPushButton>(window,"locateImageSource"),Qt::LeftButton);
    check(images->currentItem()->data(0,Qt::UserRole).toString()==id,"Locate Image selected the wrong source");
    tree->setCurrentRow(6);
    material->setCurrentIndex(material->findData(id));
    check(images->currentItem()->data(1,Qt::DisplayRole).toInt()==2,"A second material slot did not link to the image");
    control<QAction>(window,"detachLibraryImage")->trigger();
    check(images->currentItem()->data(1,Qt::DisplayRole).toInt()==1,"Detaching an image did not restore the prior slot binding");
    tree->setCurrentRow(tree->count()-1);
    QTest::mouseClick(control<QPushButton>(window,"playButton"),Qt::LeftButton);
    check(!control<QAction>(window,"createNativeImage")->isEnabled() &&
          !control<QPushButton>(window,"editNativeImage")->isEnabled(),"Image authoring remained enabled during Play");
    const auto play=settledFrame(*view);
    QTest::mouseClick(view,Qt::LeftButton,{},visiblePoint(play,tree->count(),*view));
    check(settledFrame(*view).tokens!=play.tokens,"Textured native mesh failed to interact in Play");
    key(Qt::Key_Escape);
    check(settledFrame(*view).rgba==retinted.second.rgba,"Play changed the authored image");
    tabs->setCurrentIndex(0);
    acceptMesh("editNativeMesh",true);
    const auto reshaped=settledFrame(*view);
    check(reshaped.tokens!=retinted.second.tokens && material->currentData().toString()==id,
          "Editing native geometry did not preserve its pixel source");
    key(Qt::Key_S,Qt::ControlModifier);
    const auto saved=platz::Project::open(file);
    check(platz::sameRaster(saved.imageLibrary().at(id.toStdString()).raster,expected) &&
          platz::sameRaster(saved.imageLibrary().at(imported).raster,expected) &&
          saved.find(cubeId)->images.front().imageId==id.toStdString(),"Reopen lost exact pixels or assignment");
    check(window.close(),"Image authoring project did not close after save");
    StudioWindow reopened;
    expose(reopened);
    fileAction(reopened,Qt::Key_O,path);
    control<QListWidget>(reopened,"sceneTree")->setCurrentRow(int(saved.objects().size())-1);
    auto* reopenedView=static_cast<Viewport*>(control<QWidget>(reopened,"viewport"));
    check(settledFrame(*reopenedView).rgba==reshaped.rgba &&
          control<QComboBox>(reopened,"baseColorImage")->currentData().toString()==id,
          "Reopened Studio lost the material image or rendered different pixels");
    view=reopenedView;
    QTest::mouseClick(control<QPushButton>(reopened,"locateImageSource"),Qt::LeftButton);
    visitEditor([&]{QTest::mouseClick(control<QPushButton>(reopened,"editNativeImage"),Qt::LeftButton);},false,
        [&](QWidget& dialog,RasterCanvas& canvas){
            brush(dialog,"#5A99F0FF","imageFill");
            QTest::mouseClick(&canvas,Qt::LeftButton,{},QPoint(4,4));
        });
    check(settledFrame(*reopenedView).rgba==reshaped.rgba,"Cancelling a reopened draft changed the authored pixels");
    check(reopened.close(),"Reopened image authoring project did not close");
    std::cout<<"Studio canvas stroke, fill, sample, erase, preview, PNG reimport, Play and v8 reopen passed\n";
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
        if (app.arguments().contains("--gizmo-only")) {
            exerciseViewportTransforms(temp,true);
            return 0;
        }
        if (app.arguments().contains("--native-mesh-only")) {
            exerciseNativeGeometry(temp);
            return 0;
        }
        if (app.arguments().contains("--image-only")) {
            exerciseNativeImages(temp);
            return 0;
        }
        exerciseTransformMath();
        exerciseViewportTransforms(temp,false);
        exerciseInspectorPreviews(temp);
        exercise(temp.filePath("Ordinary.platz"), true);
        const QString copy = temp.filePath("Validation copy.platz");
        check(QFile::copy(QString::fromUtf8(PLATZ_VALIDATION_PATH), copy), "Cannot copy permanent validation project");
        exercise(copy, false);
        exerciseImport(temp);
        exerciseAssetLibrary(temp);
        exerciseMaterialSlots(temp);
        exerciseTextures(temp);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
