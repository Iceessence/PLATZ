#include "StudioWindow.hpp"
#include "StudioBuild.h"
#include "Appearance.hpp"
#include "RasterCanvas.hpp"
#include "Viewport.hpp"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QInputMethodEvent>
#include <QMenu>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QScreen>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QSpinBox>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

void expose(StudioWindow& window);

void selectSceneRow(QTreeWidget* scene, int row) {
    auto* item=scene->topLevelItem(row);
    check(item && !item->data(0,Qt::UserRole).toString().isEmpty(),"Expected a scene object row");
    scene->setCurrentItem(item);
}

QTreeWidgetItem* rowNamed(QTreeWidget* tree,const QString& name) {
    const auto rows=tree->findItems(name,Qt::MatchExactly|Qt::MatchRecursive,0);
    if (rows.size()!=1) throw std::runtime_error(("Expected one tree row called "+name).toStdString());
    return rows.front();
}

void respondToCommand(QAction* action,const std::function<void(QDialog&)>& respond) {
    check(action && action->isEnabled(),"Expected an enabled Studio command");
    bool answered=false;
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,[&] {
        auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) return;
        timer.stop();
        answered=true;
        try { respond(*modal); }
        catch (...) {failure=std::current_exception(); modal->reject();}
    });
    timer.start(20);
    action->trigger();
    if (failure) std::rethrow_exception(failure);
    check(answered,"Studio command did not open its dialog");
}

void nameViaDialog(QAction* action,const QString& name) {
    respondToCommand(action,[&](QDialog& dialog) {
        auto* input=qobject_cast<QInputDialog*>(&dialog);
        check(input,"Expected the folder naming dialog");
        input->setTextValue(name);
        input->accept();
    });
}

struct FolderCase { const char* tree; const char* prefix; const char* item; };
constexpr FolderCase folders[]={{"sceneTree","scene","Block"},{"meshLibrary","mesh","Source geometry"},
                                {"imageLibrary","image","Source pixels"}};

void openFolderPane(StudioWindow& window,const FolderCase& entry) {
    if (entry.prefix==std::string_view("scene")) return;
    auto* dock=control<QDockWidget>(window,"assetsDock");
    dock->show();
    control<QTabWidget>(window,"assetTabs")->setCurrentIndex(entry.prefix==std::string_view("mesh") ? 0 : 1);
}

void saveStudioEvidence(StudioWindow& window,const QString& destination,const QString& filename) {
    if (destination.isEmpty()) return;
    check(QDir().mkpath(destination),"Could not create the requested screenshot directory");
    window.raise();
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window),"Cannot capture the active Studio window");
    QApplication::processEvents();
    QTest::qWait(180);
    const auto shot=window.screen()->grabWindow(window.winId());
    check(!shot.isNull() && shot.save(QDir(destination).filePath(filename)),"Studio screenshot was not saved");
}

void checkSavedFolders(const QString& path,const QStringList& ids) {
    check(ids.size()==3,"Expected one folder identity per Studio panel");
    StudioWindow window;
    expose(window);
    check(window.openProject(path),"Failed to reopen organized project");
    for (int i=0;i<3;++i) {
        const auto& item=folders[i];
        openFolderPane(window,item);
        auto* tree=control<QTreeWidget>(window,item.tree);
        auto* source=rowNamed(tree,item.item);
        auto* parent=source->parent();
        check(parent && parent->text(0)=="Keep" && parent->data(0,Qt::UserRole+1).toString()==ids[i] &&
              parent->data(0,Qt::UserRole).toString().isEmpty(),
              "Relaunch lost the folder identity or source membership");
    }
    check(window.close(),"Restored organized project failed to close");
}

void exerciseOrganization(const QString& path,const QString& settingsDir,const QString& evidenceDir) {
    auto project=platz::Project::create(path.toStdWString(),"Folder qualification");
    project.createNativeMesh("Source geometry",platz::defaultNativeMesh(platz::NativeShape::Cylinder),false,project.revision());
    project.createImage("Source pixels",platz::RasterImage::create(1,1,{35,90,130,255}),{},project.revision());
    project.save();
    auto hash=[&] {
        QFile file(path);
        check(file.open(QIODevice::ReadOnly),"Failed to hash the folder test project");
        return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
    };
    const auto original=hash();
    QStringList ids;
    {
        StudioWindow window;
        expose(window);
        if (!evidenceDir.isEmpty()) window.resize(1520,970);
        check(window.openProject(path),"Could not open folder test project");
        check(control<QLabel>(window,"studioBuildVersion")->text().endsWith(QStringLiteral(PLATZ_VERSION)) &&
              control<QLabel>(window,"authoringMode")->text().contains("Project rev."),
              "Studio version and project revision were not presented separately");
        for (const auto& entry : folders) {
            openFolderPane(window,entry);
            auto* tree=control<QTreeWidget>(window,entry.tree);
            const QByteArray prefix(entry.prefix);
            auto* create=control<QAction>(window,(prefix+"NewFolder").constData());
            auto* move=control<QAction>(window,(prefix+"MoveToFolder").constData());
            auto* rename=control<QAction>(window,(prefix+"RenameFolder").constData());
            auto* remove=control<QAction>(window,(prefix+"RemoveFolder").constData());
            const auto id=rowNamed(tree,entry.item)->data(0,Qt::UserRole);
            tree->setCurrentItem(rowNamed(tree,entry.item));
            nameViaDialog(create,"Temporary name");
            tree->setCurrentItem(rowNamed(tree,"Temporary name"));
            nameViaDialog(rename,"Keep");
            auto* folder=rowNamed(tree,"Keep");
            ids.append(folder->data(0,Qt::UserRole+1).toString());
            check(!ids.back().isEmpty(),"Folder has no stable presentation identity");
            tree->setCurrentItem(rowNamed(tree,entry.item));
            respondToCommand(move,[&](QDialog& dialog){
                auto* choices=control<QTreeWidget>(dialog,"folderDestination");
                choices->setCurrentItem(rowNamed(choices,"Keep"));
                dialog.accept();
            });
            auto* source=rowNamed(tree,entry.item);
            check(source->parent()==rowNamed(tree,"Keep") && source->data(0,Qt::UserRole)==id,
                  "Moving a source to a folder changed its identity or its destination");
            auto* filter=control<QLineEdit>(window,(prefix+"Search").constData());
            filter->setText("nothing-matches-this");
            check(source->isHidden() || source->parent()->isHidden(),"Search kept an unrelated source visible");
            filter->setText(entry.item);
            source=rowNamed(tree,entry.item);
            check(!source->isHidden() && !source->parent()->isHidden(),"Filtered hierarchy hid a matching source");
            filter->clear();
            tree->setCurrentItem(rowNamed(tree,"Keep"));
            check(!remove->isEnabled(),"A populated folder offered empty-folder removal");
            if (entry.prefix==std::string_view("scene")) {
                check(control<QLabel>(window,"inspectorEmpty")->isVisible() &&
                      !control<QLineEdit>(window,"objectName")->isVisible(),
                      "A Scene folder inherited the prior object's Inspector");
            }
            nameViaDialog(create,"Nested");
            check(rowNamed(tree,"Nested")->parent()==rowNamed(tree,"Keep"),"New nested folder appeared at the root");
            if (!evidenceDir.isEmpty()) {
                rowNamed(tree,"Keep")->setExpanded(true);
                tree->setCurrentItem(rowNamed(tree,entry.item));
                saveStudioEvidence(window,evidenceDir,QString::fromLatin1(entry.prefix)+"-folders.png");
            }
            tree->setCurrentItem(rowNamed(tree,"Nested"));
            // Removal of an empty folder only changes personal organization.
            remove->trigger();
            check(tree->findItems("Nested",Qt::MatchExactly|Qt::MatchRecursive).isEmpty(),"Empty folder was not removed");
        }
        check(hash()==original && !control<QPushButton>(window,"saveButton")->isEnabled(),
              "Personal organization dirtied or altered the project source");
        check(window.close(),"Organized project did not close cleanly");
    }
    QSettings().sync();
    QProcess child;
    QStringList args{"--organization-reopen",path,settingsDir};
    child.start(QCoreApplication::applicationFilePath(),args+ids);
    check(child.waitForStarted(5000) && child.waitForFinished(25000),"Folder restart process did not complete");
    if (child.exitCode()!=0) throw std::runtime_error(child.readAllStandardError().toStdString());
    check(child.exitStatus()==QProcess::NormalExit && hash()==original,
          "Reopening organized sources changed the portable project");
    std::cout<<"Three panel folders, nested creation, move, relabel, removal, search and restart passed\n";
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

void clickViewport(Viewport* viewport,QPoint point) {
    QTest::mouseClick(viewport,Qt::LeftButton,{},point);
    check(QTest::qWaitFor([&]{return !viewport->selectionPending();},1000),"Viewport selection did not finish");
}

class StudioTestApplication : public QApplication {
public:
    using QApplication::QApplication;
    using Clock = std::chrono::steady_clock;

    Viewport* viewport{};
    QWidget* inspector{};
    unsigned inspectorPaints{}, submissions{};
    std::vector<double> moves, paints, latency;

    bool notify(QObject* receiver, QEvent* event) override {
        if (!viewport) return QApplication::notify(receiver,event);
        if (event->type()==QEvent::Paint) {
            if (auto* widget=qobject_cast<QWidget*>(receiver);
                widget && (widget==inspector || inspector->isAncestorOf(widget))) ++inspectorPaints;
        }
        if (receiver!=viewport) return QApplication::notify(receiver,event);
        const auto start=Clock::now();
        if (event->type()==QEvent::MouseMove) {
            if (!pendingInput_) pendingInput_=start;
            const auto delivered=QApplication::notify(receiver,event);
            moves.push_back(milliseconds(Clock::now()-start));
            return delivered;
        }
        if (event->type()!=QEvent::Paint) return QApplication::notify(receiver,event);
        const auto serial=viewport->submittedFrames();
        const auto delivered=QApplication::notify(receiver,event);
        const auto end=Clock::now();
        paints.push_back(milliseconds(end-start));
        const bool submitted=viewport->submittedFrames()!=serial;
        if (submitted) ++submissions;
        if (submitted && pendingInput_) {
            latency.push_back(milliseconds(end-*pendingInput_));
            pendingInput_.reset();
        }
        return delivered;
    }

    void reset() {
        moves.clear(); paints.clear(); latency.clear();
        inspectorPaints=submissions=0;
        pendingInput_.reset();
    }

private:
    static double milliseconds(Clock::duration duration) {
        return std::chrono::duration<double,std::milli>(duration).count();
    }
    std::optional<Clock::time_point> pendingInput_;
};

void profileViewportDrag(StudioTestApplication& app,QTemporaryDir& dir,bool dense,bool scaledScene,bool gates) {
    using Clock=StudioTestApplication::Clock;
    const auto path=dir.filePath("Drag profile.platz");
    check(QFile::copy(QString::fromUtf8(PLATZ_VALIDATION_PATH),path),"Cannot copy the drag profile project");
    if (scaledScene) {
        auto project=platz::Project::open(path.toStdWString());
        const auto silver=std::ranges::find(project.objects(),"Silver block",&platz::Object::name);
        check(silver!=project.objects().end(),"Missing scaling test source object");
        project.repeat(silver->id,project.revision(),512-static_cast<unsigned>(project.objects().size()),{2,0,0});
        project.save();
    }
    StudioWindow window;
    expose(window);
    const auto opened=window.openProject(path);
    check(opened,"Could not open drag profile copy");
    auto* view=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
    auto* tree=control<QTreeWidget>(window,"sceneTree");
    std::cout << "Scene objects " << tree->topLevelItemCount() << '\n';
    for (int row=0;row<tree->topLevelItemCount();++row)
        if (tree->topLevelItem(row)->text(0)=="Silver block") selectSceneRow(tree,row);
    QTest::keyClick(view,Qt::Key_2);
    const auto summary=[](const char* label,std::vector<double> values) {
        check(!values.empty(),"Drag profile did not collect a required timing sample");
        std::ranges::sort(values);
        const auto percentile=[&](double p) {return values[static_cast<size_t>(std::ceil(p*values.size()))-1];};
        std::cout << label << " ms p50/p95/p99/max " << percentile(.5) << '/' << percentile(.95)
                  << '/' << percentile(.99) << '/' << values.back() << '\n';
        return percentile(.95);
    };
    for (const QSize size : {QSize(1440,900),QSize(2560,1300)}) {
        window.resize(size);
        settledFrame(*view);
        const auto axis=view->manipulatorPaths().axes[0];
        check(axis.size()==2,"Drag profile requires a selected Move X handle");
        const QPointF from=axis[0].pixel*.25+axis[1].pixel*.75;
        const QPointF direction=(axis[1].pixel-axis[0].pixel)*.45;
        for (const int rate : {500,1000}) {
            QTest::mousePress(view,Qt::LeftButton,{},from.toPoint());
            check(view->dragging(),"Drag profile did not capture the transform handle");
            app.reset();
            app.viewport=view;
            app.inspector=control<QDoubleSpinBox>(window,"positionX")->parentWidget();
            while (app.inspector->parentWidget() && !qobject_cast<QDockWidget*>(app.inspector))
                app.inspector=app.inspector->parentWidget();
            const auto hwnd=reinterpret_cast<HWND>(view->winId());
            const auto dpr=view->devicePixelRatioF();
            unsigned posted=0;
            {
                std::jthread producer([=,&posted] {
                    const auto begin=Clock::now();
                    for (int i=0;i<rate;++i) {
                        std::this_thread::sleep_until(begin+std::chrono::microseconds(i*1000000/rate));
                        auto at=(from+direction*std::sin(i*12./rate))*dpr;
                        // Distinct consecutive positions exercise Qt's native event path even at 1 kHz.
                        if (dense) at.ry()+=i%2 ? 2 : -2;
                        if (PostMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(qRound(at.x()),qRound(at.y())))) ++posted;
                    }
                });
                QEventLoop stream;
                QTimer::singleShot(1075,Qt::PreciseTimer,&stream,&QEventLoop::quit);
                stream.exec();
            }
            app.viewport=nullptr;
            check(view->dragging(),"Native move stream lost the drag");
            QTest::keyClick(view,Qt::Key_Escape);
            QTest::mouseRelease(view,Qt::LeftButton,{},from.toPoint());
            check(posted==unsigned(rate) && app.moves.size()>=100 && app.latency.size()>=30,
                  "Visible drag profile received insufficient native input");
            std::cout << "Visible " << view->width() << 'x' << view->height() << " at " << view->screen()->refreshRate()
                      << " Hz, posted " << posted << " moves/s, Qt delivered " << app.moves.size()
                      << ", paints " << app.paints.size() << ", submissions " << app.submissions
                      << ", Inspector paints " << app.inspectorPaints << '\n';
            const auto dispatch=summary("Mouse dispatch",app.moves);
            summary("Viewport paint",app.paints);
            const auto latency=summary("Oldest move to submitted paint end",app.latency);
            if (gates) {
                check(app.inspectorPaints<=2*app.submissions,"Inspector exceeded two widget paints per submitted frame");
                const auto report=view->diagnostics();
                if (view->screen()->refreshRate()>239 && report.contains("RTX 5080") && report.contains("MAILBOX")) {
                    check(latency<4.2,"Visible drag exceeded 4.2 ms p95 input-to-submitted-paint budget");
                    check(dispatch<.15,"Visible drag exceeded 0.15 ms p95 mouse dispatch budget");
                }
            }
        }
    }
    QTest::keyClick(view,Qt::Key_1);
    const auto frame=settledFrame(*view);
    const auto silver=tree->indexOfTopLevelItem(tree->currentItem());
    const auto hit=visiblePoint(frame,silver+1,*view);
    const auto empty=visiblePoint(frame,0,*view);
    std::vector<double> clickDispatch, clickResult;
    for (int i=0;i<100;++i) {
        const auto point=i%2==0 ? empty : hit;
        QMouseEvent press(QEvent::MouseButtonPress,point,view->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,{});
        const auto start=Clock::now();
        QApplication::sendEvent(view,&press);
        clickDispatch.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
        while (view->selectionPending() && Clock::now()-start<std::chrono::milliseconds(100))
            QApplication::processEvents();
        check(!view->selectionPending() && tree->indexOfTopLevelItem(tree->currentItem())==(i%2==0 ? -1 : silver),"Visible click selected the wrong ID");
        clickResult.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
    }
    const auto dispatch=summary("Click dispatch (100 selected/background hits)",clickDispatch);
    summary("Click to Studio selection",clickResult);
    if (gates && view->diagnostics().contains("RTX 5080")) check(dispatch<1.,"Click dispatch exceeded 1 ms p95");
    std::cout << view->diagnostics().toStdString() << '\n';
}

void captureTransformEvidence(QTemporaryDir& dir,const QString& destination) {
    const auto path=dir.filePath("Transform evidence.platz");
    check(QFile::copy(QString::fromUtf8(PLATZ_VALIDATION_PATH),path),"Cannot copy project for screenshot capture");
    StudioWindow window;
    expose(window);
    check(window.openProject(path),"Cannot open screenshot project");
    auto* view=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
    auto* tree=control<QTreeWidget>(window,"sceneTree");
    for (int row=0;row<tree->topLevelItemCount();++row)
        if (tree->topLevelItem(row)->text(0)=="Silver block") selectSceneRow(tree,row);
    check(QDir().mkpath(destination),"Cannot create screenshot directory");
    const std::array keys{Qt::Key_2,Qt::Key_3,Qt::Key_4};
    const std::array names{"move","rotate","scale"};
    for (size_t i=0;i<keys.size();++i) {
        QTest::keyClick(view,keys[i]);
        settledFrame(*view);
        QTest::qWait(100);
        const auto screenshot=view->screen()->grabWindow(window.winId());
        const auto filename=QString("%1-%2.png").arg(names[i]).arg(qRound(view->devicePixelRatioF()*100));
        check(!screenshot.isNull() && screenshot.save(QDir(destination).filePath(filename)),"Cannot save Studio screenshot");
    }
    control<QTabWidget>(window,"assetTabs")->setCurrentIndex(1);
    settledFrame(*view);
    QTest::qWait(100);
    auto name=QString("images-%1.png").arg(qRound(view->devicePixelRatioF()*100));
    check(view->screen()->grabWindow(window.winId()).save(QDir(destination).filePath(name)),
          "Cannot save image library screenshot");

}

void chooseFile(const QString& path, const std::function<void()>& action,
                QMessageBox::StandardButton overwrite=QMessageBox::NoButton) {
    bool answered = false;
    bool confirmed = false;
    QTimer confirmation;
    QObject::connect(&confirmation,&QTimer::timeout,[&]{
        auto* prompt=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!prompt) return;
        confirmation.stop();
        auto* choice=prompt->button(overwrite);
        confirmed=choice!=nullptr;
        if (choice) choice->click(); else prompt->reject();
    });
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
    if (overwrite!=QMessageBox::NoButton) confirmation.start(25);
    action();
    QApplication::processEvents();
    check(answered, "Studio project button did not open its file chooser");
    check(overwrite==QMessageBox::NoButton || confirmed,"Existing file did not trigger overwrite confirmation");
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
        auto* tree=control<QTreeWidget>(window,"sceneTree");
        check(tree->topLevelItemCount()==5 && tree->indexOfTopLevelItem(tree->currentItem())==4,"GLB import did not select a new scene object");
        id=tree->currentItem()->data(0,Qt::UserRole).toString().toStdString();
        enterText(control<QDoubleSpinBox>(window,"positionY"),"3.5");
        auto* vp=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        vp->setFocus(); QTest::keyClick(vp,Qt::Key_F);
        expected=settledFrame(*vp);
        auto point=visiblePoint(expected,5,*vp);
        clickViewport(vp,point);
        check(control<QLineEdit>(window,"objectName")->text()=="Spire","GPU pick missed the imported mesh");
        auto* play=control<QPushButton>(window,"playButton");
        QTest::mouseClick(play,Qt::LeftButton); clickViewport(vp,point);
        check(settledFrame(*vp).tokens!=expected.tokens,"Imported mesh did not turn during Play");
        QTest::mouseClick(play,Qt::LeftButton);
        check(settledFrame(*vp).rgba==expected.rgba,"Stop failed to restore imported geometry");
        QTest::mouseClick(control<QPushButton>(window,"saveButton"),Qt::LeftButton);
        check(window.close(),"Imported project did not close");
    }
    check(QFile::remove(source),"Could not remove disposable source GLB");
    {
        StudioWindow window; expose(window); fileAction(window,Qt::Key_O,project);
        auto* tree=control<QTreeWidget>(window,"sceneTree");
        check(tree->topLevelItemCount()==5 && tree->topLevelItem(4)->data(0,Qt::UserRole).toString().toStdString()==id,"Imported identity lost on reopen");
        selectSceneRow(tree,4);
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
        chooseFile(occupied,[&]{QTest::mouseClick(exportButton,Qt::LeftButton);},QMessageBox::No);
        check(marker.open(QIODevice::ReadOnly) && marker.readAll()=="keep","Export replaced an existing file");
        marker.close();
        chooseFile(occupied,[&]{QTest::mouseClick(exportButton,Qt::LeftButton);},QMessageBox::Yes);
        check(marker.open(QIODevice::ReadOnly) && original.seek(0) && marker.readAll()==original.readAll(),
              "Approved overwrite did not replace the GLB with the original bytes");
        check(window.close(),"Saving source bytes made an unrelated project edit");
    }
    std::cout<<"GLB import, Vulkan pick, isolated Play, pixel-identical reopen and confirmed byte-exact overwrite passed\n";
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
        auto* scene=control<QTreeWidget>(window,"sceneTree");
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* place=control<QPushButton>(window,"placeMeshAsset");
        auto* assign=control<QPushButton>(window,"assignMeshAsset");
        auto* replace=control<QAction>(window,"replaceMeshInstances");
        auto* remove=control<QAction>(window,"removeMeshAsset");
        chooseFile(PLATZ_TEST_MESH,[&]{
            QTest::mouseClick(control<QPushButton>(window,"importMeshAsset"),Qt::LeftButton);
        });
        check(library->isVisible() && library->topLevelItemCount()==1 && scene->topLevelItemCount()==4 &&
              library->currentItem()->data(1,Qt::DisplayRole).toInt()==0,
              "Library import did not create a zero-use asset");
        QTest::mouseClick(place,Qt::LeftButton);
        objectId=scene->currentItem()->data(0,Qt::UserRole).toString().toStdString();
        check(scene->topLevelItemCount()==5 && library->currentItem()->data(1,Qt::DisplayRole).toInt()==1,
              "Add instance failed to update the scene and reference count");
        enterText(control<QDoubleSpinBox>(window,"positionY"),"3.5");
        selectSceneRow(scene,3);
        const auto blockId=scene->currentItem()->data(0,Qt::UserRole);
        QTest::mouseClick(assign,Qt::LeftButton);
        check(scene->currentItem()->data(0,Qt::UserRole)==blockId &&
              library->currentItem()->data(1,Qt::DisplayRole).toInt()==2 && !remove->isEnabled(),
              "Assign replaced object identity or let a referenced mesh be removed");
        selectSceneRow(scene,4);
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
        clickViewport(viewport,point);
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
        auto* scene=control<QTreeWidget>(window,"sceneTree");
        const auto saved=platz::Project::open(file.toStdWString());
        check(saved.find(objectId) && saved.objects()==expectedObjects && library->topLevelItemCount()==1 &&
              library->currentItem()->text(0)=="Low spire" &&
              library->currentItem()->data(1,Qt::DisplayRole).toInt()==2 && scene->topLevelItemCount()==5,
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
        auto* tree=control<QTreeWidget>(window,"sceneTree");
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* materials=control<QComboBox>(window,"materialSlot");
        auto* color=control<QLineEdit>(window,"materialColor");
        auto* reset=control<QPushButton>(window,"resetSlotColor");
        check(tree->topLevelItemCount()==5 && materials->isVisible() && materials->count()==2 &&
              materials->itemText(0).contains("Copper") && materials->itemText(1).contains("Lagoon"),
              "Imported material slots were not listed in the Inspector.");
        objectId=tree->currentItem()->data(0,Qt::UserRole).toString().toStdString();
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
        clickViewport(viewport,point);
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
        auto* tree=control<QTreeWidget>(window,"sceneTree");
        selectSceneRow(tree,4);
        check(tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==objectId &&
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
        auto* tree=control<QTreeWidget>(window,"sceneTree");
        objectId=tree->currentItem()->data(0,Qt::UserRole).toString().toStdString();
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
        clickViewport(viewport,point);
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
        auto* tree=control<QTreeWidget>(window,"sceneTree");
        selectSceneRow(tree,4);
        check(tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==objectId &&
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
    auto* tree=control<QTreeWidget>(window,"sceneTree");
    auto* revision=control<QLabel>(window,"authoringMode");
    auto* position=control<QDoubleSpinBox>(window,"positionX");
    auto* scale=control<QDoubleSpinBox>(window,"scaleX");
    auto* play=control<QPushButton>(window,"playButton");
    const auto original=platz::Project::open(file);
    const auto& object=original.objects().at(3);
    const auto originalFrame=settledFrame(*viewport);
    clickViewport(viewport,visiblePoint(originalFrame,4,*viewport));
    check(tree->indexOfTopLevelItem(tree->currentItem())==3,"Viewport transform test failed to select the block");
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
        const auto point=(line.front().pixel*.3+line.back().pixel*.7).toPoint();
        check(hitGizmo(paths,point)==0,"Expected an unambiguous X axis start");
        return std::pair{point,(line.back().pixel+(line.back().pixel-line.front().pixel)*.85).toPoint()};
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
    settledFrame(*viewport);
    QTest::mousePress(viewport,Qt::RightButton,{},to);
    QTest::mouseRelease(viewport,Qt::RightButton,{},to);
    release(to);
    check(!viewport->dragging() && settledFrame(*viewport).tokens==before.tokens && revision->text()==originalCaption,
          "Right-click failed to cancel the viewport transform");
    start(from);
    cursor(to);
    settledFrame(*viewport);
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
            const auto candidate=points[index].pixel.toPoint();
            if (hitGizmo(rings,candidate)!=axis || !viewport->rect().contains(candidate)) continue;
            ringStart=candidate;
            ringEnd=points[index+9].pixel.toPoint();
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
    settledFrame(*viewport);
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
    auto world=TransformDrag::start(camera,extent,original,move,0,paths.axes[0].back().pixel);
    check(world.has_value(),"Could not start world-space math test");
    check(world->move(paths.axes[0].back().pixel).position==original.position,"Snapping moved a zero-distance drag");
    const auto left=world->move(paths.axes[0].back().pixel-QPointF(pixelPerUnit*.61,0));
    check(std::abs(left.position.x+2)<.001 && left.position.y==0 && left.position.z==0,
          "Signed world-axis distance did not snap to the nearest negative coordinate");
    move.local=true;
    move.snapping=false;
    paths=gizmoPaths(camera,extent,original,move);
    auto local=TransformDrag::start(camera,extent,original,move,0,paths.axes[0].back().pixel);
    check(local.has_value(),"Could not start local-space math test");
    const auto up=local->move(paths.axes[0].back().pixel-QPointF(0,pixelPerUnit));
    check(std::abs(up.position.x-original.position.x)<.001 && std::abs(up.position.y-1)<.001,
          "Local X did not translate along the rotated basis");
    const TransformSettings scale{TransformTool::Scale,false,false,1};
    paths=gizmoPaths(camera,extent,original,scale);
    auto scaling=TransformDrag::start(camera,extent,original,scale,0,paths.axes[0].back().pixel);
    check(scaling.has_value(),"Could not start scale math test");
    const auto enlarged=scaling->move(paths.axes[0].back().pixel-QPointF(0,104));
    check(std::abs(enlarged.scale.x-2)<.001 && enlarged.scale.y==2 && enlarged.scale.z==3,
          "Nonuniform scale changed another local component");
    const TransformSettings rotate{TransformTool::Rotate,false,true,15};
    paths=gizmoPaths(camera,extent,original,rotate);
    auto rotation=TransformDrag::start(camera,extent,original,rotate,2,paths.axes[2].front().pixel);
    check(rotation.has_value(),"Could not start ring math test");
    check(std::abs(std::abs(rotation->move(paths.axes[2].at(24).pixel).rotation.z)-180)<.01,
          "A quarter turn around Z did not compose with existing orientation");
    check(rotation->rotationGuide().size()>10,"Rotation did not generate a visible angle sector");

    camera.yaw=.69f;
    camera.pitch=.52f;
    const TransformValue object{{0,0,0},{0,0,0},{1,2,3}};
    auto handlePoint=[](const auto& polygon) {
        QPointF center;
        for (const auto& point : polygon) center+=point.pixel;
        return center/double(polygon.size());
    };
    const std::array<unsigned,3> passiveAxis{2,1,0};
    for (const auto tool : {TransformTool::Move,TransformTool::Scale}) {
        const TransformSettings settings{tool,false,false,.5f};
        paths=gizmoPaths(camera,extent,object,settings);
        for (int axis=0;axis<3;++axis) {
            const auto& line=paths.axes[axis];
            check(!line.empty() && hitGizmo(paths,line.back().pixel)==axis,"A 3D axis cap could not be hit");
            check(TransformDrag::start(camera,extent,object,settings,axis,line.back().pixel).has_value(),
                  "A 3D axis cap could not be dragged");
        }
        for (int plane=0;plane<3;++plane) {
            check(paths.planes[plane].size()==4,"Expected each plane to be visible from the test camera");
            const auto center=handlePoint(paths.planes[plane]);
            check(hitGizmo(paths,center)==plane+3,"A plane handle could not be hit");
            auto drag=TransformDrag::start(camera,extent,object,settings,plane+3,center);
            check(drag.has_value(),"A plane handle could not be dragged");
            const auto changed=drag->move(center+QPointF(17,29));
            const auto fixed=passiveAxis[plane];
            const std::array positions{changed.position.x,changed.position.y,changed.position.z};
            const std::array scales{changed.scale.x,changed.scale.y,changed.scale.z};
            if (tool==TransformTool::Move)
                check(changed.position!=object.position && positions[fixed]==0,"Plane Move changed the perpendicular axis");
            else
                check(changed.scale!=object.scale && scales[fixed]==fixed+1,"Plane Scale changed the perpendicular axis");
        }
        check(hitGizmo(paths,paths.center.pixel)==6,"Center handle lost hit-test priority");
        auto drag=TransformDrag::start(camera,extent,object,settings,6,paths.center.pixel);
        check(drag.has_value(),"Center handle could not be dragged");
        const auto moved=drag->move(paths.center.pixel+QPointF(52,0));
        if (tool==TransformTool::Move)
            check(moved.position!=object.position && moved.scale==object.scale,"Screen-plane Move failed");
        else
            check(moved.scale==platz::Vec3{1.5f,3.f,4.5f},"Uniform Scale did not preserve proportions");
    }
    paths=gizmoPaths(camera,extent,object,rotate);
    for (int ring=0;ring<4;++ring) {
        const auto& points=ring==3 ? paths.roll : paths.axes[ring];
        const auto front=std::ranges::find_if(points,[&](const auto& point){return hitGizmo(paths,point.pixel)==ring;});
        check(points.size()==97 && front!=points.end(),"Rotation ring is incomplete or cannot be hit");
        auto drag=TransformDrag::start(camera,extent,object,rotate,ring,front->pixel);
        const auto target=points[(front-points.begin()+40)%96].pixel;
        check(drag && drag->move(target).rotation!=object.rotation,"Picked rotation ring did not rotate the object");
    }
    const TransformSettings boundsScale{TransformTool::Scale,false,false,1,true};
    const platz::Vec3 localPivot{0,1,0};
    paths=gizmoPaths(camera,extent,object,boundsScale,localPivot);
    auto bounds=TransformDrag::start(camera,extent,object,boundsScale,6,paths.center.pixel,localPivot);
    check(bounds.has_value(),"Bounds center handle could not start");
    const auto fine=bounds->move(paths.center.pixel+QPointF(104,0),false,true);
    check(std::abs(fine.scale.x-1.1f)<.001f && std::abs(fine.position.y+fine.scale.y-object.scale.y)<.001f,
          "Precision scaling did not keep the bounds center stationary");
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
    auto* tree=control<QTreeWidget>(window,"sceneTree");
    selectSceneRow(tree,3);
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
    auto* save=control<QPushButton>(window,"saveButton");
    draft(color,"#3D474");
    check(!save->isEnabled(),"Invalid hex made the clean project savable");
    QTest::keyClick(color,Qt::Key_Return);
    auto* scale=control<QDoubleSpinBox>(window,"scaleX");
    auto* scaleEditor=scale->findChild<QLineEdit*>();
    draft(scaleEditor,"-2.5");
    check(scaleEditor->text()=="-2.5" && !save->isEnabled(),"Invalid scale dropped its sign or enabled Save");
    QTest::keyClick(scaleEditor,Qt::Key_Return);
    check(std::abs(scale->value()-object.scale.x)<.001 && !save->isEnabled(),"Invalid scale altered authoring state");
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
    selectSceneRow(control<QTreeWidget>(reopened,"sceneTree"),3);
    const auto reloaded=settledFrame(*view);
    check(reloaded.tokens==tinted.tokens && reloaded.rgba==tinted.rgba,"Inspector draft save/reopen changed pixels");
    check(reopened.close(),"Reopened Inspector preview project did not close");
    std::cout<<"Live numeric input, scrubbing and color previews passed with cancellation and persisted undoable commits\n";
}

void exerciseInspectorSources(QTemporaryDir& dir,const QString& evidenceDir) {
    const auto path=dir.filePath("Inspector source selection.platz");
    auto initial=platz::Project::create(path.toStdWString(),"Source selection");
    auto shared=initial.createNativeMesh("Shared cylinder",platz::defaultNativeMesh(platz::NativeShape::Cylinder),
                                         true,initial.revision());
    const auto independent=initial.createNativeMesh("Other mesh",platz::defaultNativeMesh(platz::NativeShape::Sphere),
                                                    false,initial.revision());
    const auto sibling=initial.instantiateMesh(shared.key,initial.revision());
    // Distinct display names make it possible to verify this via normal user selection.
    auto target=*initial.find(shared.objectId);
    target.name="First cylinder";
    initial.edit(target,initial.revision());
    auto other=*initial.find(sibling);
    other.name="Second cylinder";
    initial.edit(other,initial.revision());
    initial.save();
    StudioWindow window;
    expose(window);
    if (!evidenceDir.isEmpty()) window.resize(1520,970);
    check(window.openProject(path),"Could not open Inspector source test");
    auto* tree=control<QTreeWidget>(window,"sceneTree");
    tree->setCurrentItem(rowNamed(tree,"First cylinder"));
    auto* picker=control<QComboBox>(window,"meshSourcePicker");
    auto* position=control<QDoubleSpinBox>(window,"positionX");
    auto* color=control<QLineEdit>(window,"materialColor");
    auto section=[&](const char* toggle,QWidget* content) {
        auto* button=control<QToolButton>(window,toggle);
        check(button->isVisible() && button->isChecked() && content->isVisible(),"Inspector section missing");
        button->click();
        check(!content->isVisible(),"Collapsed Inspector section kept editable fields visible");
        button->click();
        check(content->isVisible(),"Expanding the Inspector section lost its fields");
    };
    section("inspectorTransformToggle",position);
    section("inspectorMeshToggle",picker);
    section("inspectorMaterialToggle",color);
    check(picker->currentData().toString().toStdString()==shared.key,"Inspector displayed the wrong mesh source");
    const auto index=picker->findData(QString::fromStdString(independent.key));
    check(index>=0,"Inspector source chooser omitted available native geometry");
    picker->showPopup();
    picker->view()->setCurrentIndex(picker->model()->index(index,0));
    QTest::keyClick(picker->view(),Qt::Key_Return);
    QApplication::processEvents();
    check(picker->currentData().toString().toStdString()==independent.key,"Selecting a library mesh did not rebind the instance");
    saveStudioEvidence(window,evidenceDir,"inspector-mesh-material.png");
    auto* view=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
    view->setFocus();
    QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier);
    check(picker->currentData().toString().toStdString()==shared.key,"Undo failed to restore the prior source");
    QTest::keyClick(view,Qt::Key_Y,Qt::ControlModifier);
    control<QAction>(window,"saveStudio")->trigger();
    const auto rebound=platz::Project::open(path.toStdWString());
    const auto* result=rebound.find(shared.objectId);
    check(result && result->asset.key()==independent.key && result->name==target.name &&
          result->position==target.position && result->rotation==target.rotation && result->scale==target.scale &&
          rebound.find(sibling) && *rebound.find(sibling)==other,
          "Reassigning one mesh changed its identity, transform or another instance");
    tree->setCurrentItem(rowNamed(tree,"Block"));
    auto* choose=control<QToolButton>(window,"copyAppearance");
    auto* menu=choose->menu();
    check(menu,"Inspector is missing the appearance source menu");
    // The list reflects current project objects when opened.
    menu->popup(choose->mapToGlobal(QPoint(0,choose->height())));
    QApplication::processEvents();
    const auto actions=menu->actions();
    auto from=std::find_if(actions.begin(),actions.end(),[](QAction* item){
        return item->text()=="Sphere";
    });
    if (from==actions.end()) {menu->hide(); throw std::runtime_error("Appearance source did not list the sphere");}
    menu->hide();
    (*from)->trigger();
    control<QAction>(window,"saveStudio")->trigger();
    const auto painted=platz::Project::open(path.toStdWString());
    auto block=std::ranges::find(painted.objects(),"Block",&platz::Object::name);
    auto sphere=std::ranges::find(painted.objects(),"Sphere",&platz::Object::name);
    check(block!=painted.objects().end() && sphere!=painted.objects().end() && block->color==sphere->color &&
          *sphere==*initial.find(sphere->id) && initial.find(block->id)->color!=block->color,
          "Appearance copy changed its source or failed to update the target");
    auto* image=control<QComboBox>(window,"baseColorImage");
    auto* convert=control<QPushButton>(window,"makeEditableMesh");
    check(convert->isVisible() && !image->isVisible(),"Built-in object has incorrect conversion or image controls");
    QTest::mouseClick(convert,Qt::LeftButton);
    check(image->isVisible() && !convert->isVisible(),"Conversion did not expose the native image slot");
    control<QAction>(window,"saveStudio")->trigger();
    const auto converted=platz::Project::open(path.toStdWString());
    const auto* authored=converted.find(block->id);
    check(authored && authored->asset.data && converted.meshLibrary().at(std::string(authored->asset.key())).native &&
          authored->name==block->name && authored->position==block->position && authored->rotation==block->rotation &&
          authored->scale==block->scale && authored->color==block->color,"Conversion lost authored instance properties");
    view->setFocus();
    QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier);
    check(convert->isVisible() && !image->isVisible(),"Undo failed to restore the built-in mesh");
    control<QAction>(window,"saveStudio")->trigger();
    const auto reverted=platz::Project::open(path.toStdWString());
    check(reverted.objects()==painted.objects() && reverted.meshLibrary()==painted.meshLibrary(),
          "Undo leaked the converted mesh source or altered other objects");
    const auto frame=settledFrame(*view);
    clickViewport(view,visiblePoint(frame,0,*view));
    check(control<QLabel>(window,"inspectorEmpty")->isVisible() &&
          !picker->isVisible() && !color->isVisible() && !position->isVisible(),
          "Empty selection retained editable properties from a previous object");
    check(window.close(),"Inspector source test failed to close");
    std::cout<<"Inspector disclosure, selected mesh source, independent instance, appearance copy and no-selection checks passed\n";
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
    auto* tree=control<QTreeWidget>(window,"sceneTree");
    auto* library=control<QTreeWidget>(window,"meshLibrary");
    auto* play=control<QPushButton>(window,"playButton");
    auto getKey=[&]{return library->currentItem()->data(0,Qt::UserRole).toString().toStdString();};
    const auto count=tree->topLevelItemCount();
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
    check(tree->topLevelItemCount()==count && library->topLevelItemCount()==int(original.meshLibrary().size()) &&
          settledFrame(*view).rgba==start.rgba,"Cancelling creation did not restore the exact authored scene");
    const auto created=parameters("createNativeCylinder",true,[](QWidget& dialog){
        control<QLineEdit>(dialog,"nativeMeshName")->setText("Editable taper");
        control<QDoubleSpinBox>(dialog,"meshHeight")->setValue(1.7);
        control<QDoubleSpinBox>(dialog,"meshTopScale")->setValue(.55);
        control<QSpinBox>(dialog,"meshSegments")->setValue(12);
    });
    check(tree->topLevelItemCount()==count+1 && library->currentItem()->data(1,Qt::DisplayRole).toInt()==1 &&
          settledFrame(*view).tokens==created.tokens,"Create did not place the previewed reusable geometry");
    const auto key=getKey();
    const auto firstId=tree->currentItem()->data(0,Qt::UserRole).toString().toStdString();
    check(key.starts_with("native:"),"Asset library did not expose a native stable identity");
    QTest::mouseClick(control<QPushButton>(window,"placeMeshAsset"),Qt::LeftButton);
    check(library->currentItem()->data(1,Qt::DisplayRole).toInt()==2,"Second instance did not link to the asset");
    const auto secondId=tree->currentItem()->data(0,Qt::UserRole).toString().toStdString();
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
    clickViewport(view,visiblePoint(runtime,count+2,*view));
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
          tree->topLevelItemCount()==count+2 && settledFrame(*view).rgba==edited.rgba,
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
    auto* tree=control<QTreeWidget>(window,"sceneTree");
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
    const auto cubeId=tree->currentItem()->data(0,Qt::UserRole).toString().toStdString();
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
    tabs->setCurrentIndex(1);
    const auto created=visitEditor([&]{control<QPushButton>(window,"newLibraryImage")->click();},true,
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
    auto overwritePng=[&]{control<QAction>(window,"exportLibraryPng")->trigger();};
    QFile marker(png);
    check(marker.open(QIODevice::WriteOnly|QIODevice::Truncate) && marker.write("keep")==4,"Cannot prepare occupied PNG");
    marker.close();
    chooseFile(png,overwritePng,QMessageBox::No);
    check(marker.open(QIODevice::ReadOnly) && marker.readAll()=="keep","Declined PNG overwrite changed the target");
    marker.close();
    chooseFile(png,overwritePng,QMessageBox::Yes);
    check(platz::sameRaster(rasterSource(QImage(png).convertToFormat(QImage::Format_RGBA8888)),expected),
          "Approved PNG overwrite did not preserve RGBA");
    chooseFile(png,[&]{QTest::mouseClick(control<QPushButton>(window,"importImagePng"),Qt::LeftButton);});
    const auto imported=images->currentItem()->data(0,Qt::UserRole).toString().toStdString();
    check(imported!=id.toStdString() && images->topLevelItemCount()==2 &&
          images->currentItem()->data(1,Qt::DisplayRole).toInt()==0,"PNG import did not produce a separate editable source");
    QTest::mouseClick(control<QPushButton>(window,"locateImageSource"),Qt::LeftButton);
    check(images->currentItem()->data(0,Qt::UserRole).toString()==id,"Locate Image selected the wrong source");
    selectSceneRow(tree,6);
    material->setCurrentIndex(material->findData(id));
    check(images->currentItem()->data(1,Qt::DisplayRole).toInt()==2,"A second material slot did not link to the image");
    control<QAction>(window,"detachLibraryImage")->trigger();
    check(images->currentItem()->data(1,Qt::DisplayRole).toInt()==1,"Detaching an image did not restore the prior slot binding");
    selectSceneRow(tree,tree->topLevelItemCount()-1);
    QTest::mouseClick(control<QPushButton>(window,"playButton"),Qt::LeftButton);
    check(!control<QAction>(window,"createNativeImage")->isEnabled() &&
          !control<QPushButton>(window,"editNativeImage")->isEnabled(),"Image authoring remained enabled during Play");
    const auto play=settledFrame(*view);
    clickViewport(view,visiblePoint(play,tree->topLevelItemCount(),*view));
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
    selectSceneRow(control<QTreeWidget>(reopened,"sceneTree"),int(saved.objects().size())-1);
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

void exerciseWorkspaces(QTemporaryDir& dir,bool highDpi) {
    const auto path=dir.filePath("Workspace review.platz");
    check(QFile::copy(QString::fromUtf8(PLATZ_VALIDATION_PATH),path),"Cannot copy validation for workspace checks");
    auto sourceHash=[&]{
        QFile file(path);
        check(file.open(QIODevice::ReadOnly),"Could not hash the workspace test project");
        return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
    };
    const auto original=sourceHash();
    const auto externalLayoutKey="StudioWorkspaces/v1/layouts/custom-11111111-1111-4111-8111-111111111111";
    const auto icon=platzIcon();
    for (int size:{16,24,32,48,64,128,256}) {
        check(icon.availableSizes().contains(QSize(size,size)),"Qt brand resource is missing a required size");
        const auto image=icon.pixmap(QSize(size,size),1.0).toImage();
        auto at=[&](int x,int y){return image.pixelColor(x*size/64,y*size/64);};
        const auto stem=at(21,44), bowl=at(43,25), counter=at(31,25);
        check(image.size()==QSize(size,size) && image.pixelColor(0,0).alpha()==0 &&
              stem.green()<90 && bowl.green()<110 && counter.green()>180,
              "Brand lost the P stem, bowl, counter or transparency at a rendered size");
    }
    QByteArray legacyLayout;
    {
        StudioWindow seed;
        expose(seed);
        check(seed.openProject(path),"Could not open workspace test project");
        auto* inspector=control<QDockWidget>(seed,"inspectorDock");
        seed.addDockWidget(Qt::LeftDockWidgetArea,inspector);
        control<QDockWidget>(seed,"assetsDock")->hide();
        legacyLayout=seed.saveState(1);
        check(seed.close(),"Could not close workspace seed");
    }
    QSettings().remove("StudioWorkspaces/v1");
    QSettings().setValue("studioLayout",legacyLayout);
    auto withModal=[&](QAction* command,const char* name,const std::function<void(QDialog&)>& act) {
        bool answered=false;
        std::exception_ptr error;
        QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            answered=true;
            timer.stop();
            try {
                check(!name || dialog->objectName()==name,"Unexpected workspace dialog");
                act(*dialog);
                if (dialog->isVisible()) throw std::runtime_error("Workspace test dialog remained open");
            } catch (...) {error=std::current_exception(); dialog->reject();}
        });
        timer.start(20);
        command->trigger();
        timer.stop();
        if (error) std::rethrow_exception(error);
        check(answered,"The workspace command did not expose its dialog");
    };
    {
        StudioWindow window;
        expose(window);
        check(window.openProject(path),"Could not open migrated layout");
        auto* inspector=control<QDockWidget>(window,"inspectorDock");
        auto* assets=control<QDockWidget>(window,"assetsDock");
        auto* scene=control<QDockWidget>(window,"sceneDock");
        auto* picker=control<QComboBox>(window,"studioWorkspace");
        auto* viewport=static_cast<Viewport*>(control<QWidget>(window,"viewport"));
        auto* play=control<QPushButton>(window,"playButton");
        if (highDpi) check(viewport->devicePixelRatioF()>1,"Workspace test requires high DPI scaling");
        check(picker->currentText()=="Custom" && window.dockWidgetArea(inspector)==Qt::LeftDockWidgetArea && assets->isHidden(),
              "Existing single-layout preferences did not migrate to Custom");
        inspector->setFloating(true);
        inspector->move(-5000,-5000);
        withModal(control<QAction>(window,"saveWorkspaceAs"),nullptr,[](QDialog& dialog){
            auto* input=qobject_cast<QInputDialog*>(&dialog);
            check(input!=nullptr,"Save As did not ask for a workspace name");
            input->setTextValue(QString::fromUtf8("Mesh Review"));
            input->accept();
        });
        const auto custom=picker->currentData().toString();
        check(picker->currentText()=="Mesh Review" && custom.startsWith("custom-"),"Named layout was not selected or saved");
        picker->setCurrentIndex(picker->findData("texture"));
        check(!assets->isHidden() && scene->isHidden() &&
              control<QTabWidget>(window,"assetTabs")->currentIndex()==1,"Texture workspace did not expose the image library");
        picker->setCurrentIndex(picker->findData("materials"));
        check(!inspector->isHidden() && !inspector->isFloating() && !scene->isHidden(),"Materials workspace did not restore its panels");
        picker->setCurrentIndex(picker->findData(custom));
        QApplication::processEvents();
        check(assets->isHidden() && inspector->isFloating(),"Switching away lost the custom layout");
        const auto onScreen=std::ranges::any_of(QGuiApplication::screens(),[&](const auto* display){
            return display->availableGeometry().intersects(inspector->frameGeometry());
        });
        check(onScreen,"Floating layout was stranded outside the available screens");
        auto* customize=control<QAction>(window,"customizeStudio");
        withModal(customize,"customizeStudioDialog",[](QDialog& dialog){
            control<QComboBox>(dialog,"studioTheme")->setCurrentIndex(1);
            check(qApp->palette().color(QPalette::Window)==QColor("#eef3f3"),"Light theme did not preview on the application");
            dialog.reject();
        });
        check(qApp->palette().color(QPalette::Window)==QColor("#141a1f"),"Cancel retained the appearance draft");
        withModal(customize,"customizeStudioDialog",[](QDialog& dialog){
            auto* apply=control<QPushButton>(dialog,"applyStudioPreferences");
            auto* playKey=control<QKeySequenceEdit>(dialog,"shortcutPlay");
            auto* frameKey=control<QKeySequenceEdit>(dialog,"shortcutFrame");
            auto* warning=control<QLabel>(dialog,"studioShortcutConflict");
            playKey->setKeySequence(QKeySequence("Ctrl+S"));
            check(!apply->isEnabled() && warning->text().contains("already assigned"),"Duplicate shortcut was accepted");
            playKey->setKeySequence(QKeySequence("Ctrl+K"));
            check(!apply->isEnabled() && warning->text().contains("Search Studio"),"Command search binding could be stolen");
            playKey->setKeySequence(QKeySequence(Qt::Key_F6));
            frameKey->setKeySequence(QKeySequence(Qt::Key_W));
            check(!apply->isEnabled(),"Frame binding could override camera movement");
            frameKey->setKeySequence(QKeySequence(Qt::Key_G));
            control<QComboBox>(dialog,"studioTheme")->setCurrentIndex(2);
            control<QComboBox>(dialog,"studioAccent")->setCurrentIndex(2);
            auto* size=control<QComboBox>(dialog,"studioUiScale");
            size->setCurrentIndex(size->findData(125));
            check(apply->isEnabled(),"Valid custom shortcuts were rejected");
            QTest::mouseClick(apply,Qt::LeftButton);
        });
        check(qApp->font().pointSizeF()==12.5 && qApp->palette().color(QPalette::Window)==QColor("#0a1116") &&
              qApp->styleSheet().contains("#f3c57b"),"Theme, accent and type scale did not apply");
        auto searchFor=[&](const QString& query) {
            withModal(control<QAction>(window,"findStudioCommands"),"studioCommandSearch",[&](QDialog& dialog){
                auto* input=control<QLineEdit>(dialog,"studioCommandFilter");
                input->setText(query);
                check(control<QListWidget>(dialog,"studioCommandResults")->count()==1,"Studio search did not resolve its target");
                QTest::keyClick(input,Qt::Key_Return);
            });
        };
        check(control<QPushButton>(window,"studioSearchButton")->isVisible(),"Project toolbar concealed Studio search");
        searchFor("Scene Silver block");
        check(control<QLineEdit>(window,"objectName")->text()=="Silver block","Search failed to inspect the selected object");
        picker->setCurrentIndex(picker->findData("modeling"));
        searchFor("Meshes Low spire");
        check(assets->isVisible() && control<QTabWidget>(window,"assetTabs")->currentIndex()==0,
              "Mesh search did not reveal the mesh library");
        withModal(control<QAction>(window,"findStudioCommands"),"studioCommandSearch",[](QDialog& dialog){
            auto* search=control<QLineEdit>(dialog,"studioCommandFilter");
            search->setText("playtest");
            check(control<QListWidget>(dialog,"studioCommandResults")->count()==1,"Command filter did not find a real workspace action");
            QTest::keyClick(search,Qt::Key_Return);
        });
        check(picker->currentData().toString()=="playtest" && scene->isHidden() && assets->isHidden() &&
              inspector->isHidden() && play->isVisible(),"Playtest layout concealed runtime controls");
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_F5);
        check(play->text().contains("Play"),"Old binding still activated Play");
        QTest::keyClick(viewport,Qt::Key_F6);
        check(play->text().contains("Stop") && !control<QWidget>(window,"toolMove")->isVisible(),
              "Play did not switch to runtime controls");
        QTest::keyClick(viewport,Qt::Key_Escape);
        check(play->text().contains("Play"),"Escape did not restore authoring with custom shortcuts");
        picker->setCurrentIndex(picker->findData(custom));
        check(window.close(),"Workspace review did not close");
    }
    {
        StudioWindow reopened;
        expose(reopened);
        check(reopened.openProject(path),"Workspace project failed to reopen");
        auto* picker=control<QComboBox>(reopened,"studioWorkspace");
        auto* assets=control<QDockWidget>(reopened,"assetsDock");
        auto* inspector=control<QDockWidget>(reopened,"inspectorDock");
        check(picker->currentText()=="Mesh Review" && inspector->isFloating() && assets->isHidden(),
              "Recreated window lost its named workspace or panel state");
        check(qApp->font().pointSizeF()==12.5 && qApp->palette().color(QPalette::Window)==QColor("#0a1116") &&
              control<QAction>(reopened,"frameSelection")->shortcut()==QKeySequence(Qt::Key_G) &&
              control<QAction>(reopened,"playStudio")->shortcut()==QKeySequence(Qt::Key_F6),
              "Preferences or remapped keys did not survive reopening");
        picker->setCurrentIndex(picker->findData("modeling"));
        auto* view=static_cast<Viewport*>(control<QWidget>(reopened,"viewport"));
        const auto unframed=settledFrame(*view);
        view->setFocus();
        QTest::keyClick(view,Qt::Key_F);
        check(settledFrame(*view).tokens==unframed.tokens,"Old viewport frame binding was still active");
        QTest::keyClick(view,Qt::Key_G);
        check(settledFrame(*view).tokens!=unframed.tokens,"Remapped viewport frame key did not execute");
        assets->hide();
        inspector->setFloating(true);
        control<QAction>(reopened,"resetWorkspace")->trigger();
        check(!assets->isHidden() && !inspector->isFloating() && reopened.dockWidgetArea(inspector)==Qt::RightDockWidgetArea,
              "Reset Workspace failed to restore its default dock arrangement");
        withModal(control<QAction>(reopened,"customizeStudio"),"customizeStudioDialog",[](QDialog& dialog){
            auto* buttons=dialog.findChild<QDialogButtonBox*>();
            QTest::mouseClick(buttons->button(QDialogButtonBox::RestoreDefaults),Qt::LeftButton);
            QTest::mouseClick(control<QPushButton>(dialog,"applyStudioPreferences"),Qt::LeftButton);
        });
        check(qApp->font().pointSizeF()==10 && control<QAction>(reopened,"playStudio")->shortcut()==QKeySequence(Qt::Key_F5),
              "Restore Defaults failed to reapply appearance and shortcuts");
        QSettings().setValue(externalLayoutKey,QVariantMap{{"name","Second window"},{"layout",legacyLayout}});
        check(reopened.close(),"Restored workspace did not close");
    }
    check(QSettings().contains(externalLayoutKey),"Saving an active layout erased another window's workspace");
    QSettings().setValue("StudioWorkspaces/v1/layouts/texture",QVariantMap{{"layout",QByteArray("invalid data")}});
    QSettings().setValue("StudioWorkspaces/v1/active","texture");
    QSettings().setValue("StudioPreferences/v1/shortcuts",QStringList{"Ctrl+K","F5","W",""});
    {
        StudioWindow recovered;
        expose(recovered);
        check(recovered.openProject(path),"Malformed preferences prevented a normal project open");
        check(control<QComboBox>(recovered,"studioWorkspace")->currentData().toString()=="texture" &&
              !control<QDockWidget>(recovered,"assetsDock")->isHidden() &&
              control<QTabWidget>(recovered,"assetTabs")->currentIndex()==1 &&
              control<QAction>(recovered,"saveStudio")->shortcut()==QKeySequence(QKeySequence::Save),
              "Invalid layout or conflicting shortcut could not recover to usable defaults");
        check(recovered.close(),"Recovered workspace did not close");
    }
    check(sourceHash()==original,"Personal UI preferences modified portable project bytes");
    std::cout<<"Workspace migration, presets, named docks, search, themes, key remapping, reset and P icon passed\n";
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
        auto* tree = control<QTreeWidget>(window, "sceneTree");
        auto* name = control<QLineEdit>(window, "objectName");
        auto* position = control<QDoubleSpinBox>(window, "positionX");
        auto* number=position->findChild<QLineEdit*>();
        check(number && number->visibleRegion().boundingRect().width()>=35,"Inspector numeric editor is clipped");
        auto* identity=control<QLabel>(window,"objectIdentity");
        auto* reveal=control<QToolButton>(window,"toggleObjectId");
        check(identity->isHidden(),"Object metadata should start collapsed");
        reveal->click();
        check(identity->isVisible() && identity->text()==QString::fromStdString(before.objects().front().id),
              "Object metadata disclosure showed the wrong identifier");
        reveal->click();
        auto* color = control<QLineEdit>(window, "materialColor");
        auto* play = control<QPushButton>(window, "playButton");
        check(viewport->isVisible() && tree->topLevelItemCount() == int(before.objects().size()),
              "Opened project failed to populate the visible Studio");
        auto frame = settledFrame(*viewport);
        const auto initialCount=tree->topLevelItemCount();
        selectSceneRow(tree,1);
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
            check(tree->topLevelItemCount()==initialCount+(apply?2:0),"Array creation/cancellation changed wrong object count");
        }
        const auto arrayIds=std::array{tree->topLevelItem(2)->data(0,Qt::UserRole),tree->topLevelItem(3)->data(0,Qt::UserRole)};
        QTest::keyClick(tree,Qt::Key_Z,Qt::ControlModifier);
        check(tree->topLevelItemCount()==initialCount,"Array undo was not atomic");
        QTest::keyClick(tree,Qt::Key_Y,Qt::ControlModifier);
        check(tree->topLevelItem(2)->data(0,Qt::UserRole)==arrayIds[0] && tree->topLevelItem(3)->data(0,Qt::UserRole)==arrayIds[1],
              "Array redo replaced copy identities");
        QTest::keyClick(tree,Qt::Key_Z,Qt::ControlModifier);
        // Add through Studio's creation menu. The file stays at its opened revision.
        auto* add = control<QPushButton>(window, "addObject");
        check(add->menu() && add->menu()->findChild<QAction*>("importGlb"), "Creation menu is unavailable");
        add->menu()->actions().front()->trigger();
        check(tree->topLevelItemCount()==int(before.objects().size()+1), "Add object failed");
        const auto addedId=tree->currentItem()->data(0,Qt::UserRole).toString();
        viewport->setFocus();
        QTest::keyClick(viewport,Qt::Key_D,Qt::ControlModifier);
        check(tree->topLevelItemCount()==int(before.objects().size()+2) && tree->currentItem()->data(0,Qt::UserRole).toString()!=addedId,
              "Duplicate did not select a new stable identity");
        tree->setFocus();
        QTest::keyClick(tree,Qt::Key_Delete);
        check(tree->topLevelItemCount()==int(before.objects().size()+1),"Delete failed");
        // Undo delete, duplicate and add to return to the reference scene.
        for(int i=0;i<3;++i) QTest::keyClick(tree,Qt::Key_Z,Qt::ControlModifier);
        check(tree->topLevelItemCount()==int(before.objects().size()),"Undo did not restore original object count");
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
            clickViewport(viewport,point);
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
        clickViewport(viewport,visiblePoint(runtime, 4, *viewport));
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
        clickViewport(viewport,visiblePoint(frame, 4, *viewport));
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
        StudioTestApplication app(argc, argv);
        configureAppearance(app);
        app.setQuitOnLastWindowClosed(false);
        app.setOrganizationName("PLATZ-Tests");
        app.setApplicationName("StudioVerification");
        QTemporaryDir temp;
        check(temp.isValid(), "Cannot create isolated test-project directory");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        if (app.arguments().value(1)=="--organization-reopen") {
            check(app.arguments().size()==7,"Folder restart needs project, preferences and three identities");
            QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,app.arguments()[3]);
            checkSavedFolders(app.arguments()[2],app.arguments().mid(4));
            return 0;
        }
        if (app.arguments().contains("--organization")) {
            const int flag=app.arguments().indexOf("--evidence-dir");
            const QString destination=flag>=0 ? app.arguments().value(flag+1) : QString{};
            check(flag<0 || !destination.isEmpty(),"Expected a directory after --evidence-dir");
            exerciseOrganization(temp.filePath("Organized.platz"),temp.path(),destination);
            exerciseInspectorSources(temp,destination);
            return 0;
        }
        if (app.arguments().contains("--drag-profile")) {
            profileViewportDrag(app,temp,app.arguments().contains("--dense"),app.arguments().contains("--scaled-scene"),
                                app.arguments().contains("--gates"));
            return 0;
        }
        const auto evidence=app.arguments().indexOf("--gizmo-evidence");
        if (evidence>=0) {
            check(evidence+1<app.arguments().size(),"Expected a screenshot directory");
            captureTransformEvidence(temp,app.arguments().at(evidence+1));
            return 0;
        }
        if (app.arguments().contains("--gizmo-only")) {
            exerciseTransformMath();
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
        if (app.arguments().contains("--workspaces")) {
            exerciseWorkspaces(temp,app.arguments().contains("--highdpi"));
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
