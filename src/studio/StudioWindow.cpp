#include "StudioWindow.hpp"
#include "OrganizerTree.hpp"
#include "Appearance.hpp"
#include "StudioBuild.h"
#include "Viewport.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <cmath>

StudioWindow::StudioWindow() {
    pages_ = new QStackedWidget;
    setCentralWidget(pages_);
    setWindowIcon(platzIcon());
    resize(1360,820);
    setMinimumSize(760,460);
    setDockNestingEnabled(true);
    buildHub();
    buildStudio();
    buildMenus();
    loadStudioPreferences();
    resizeDocks({sceneDock_, inspectorDock_},{220,292},Qt::Horizontal);
    resizeDocks({assetsDock_},{270},Qt::Vertical);
    initialLayout_ = saveState(1);
    loadWorkspaces();
    connect(qApp,&QGuiApplication::screenRemoved,this,[this]{
        QTimer::singleShot(0,this,&StudioWindow::recoverFloatingPanels);
    });
    toHub();
}

void StudioWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&Project"));
    file->addAction(tr("&New project…"),QKeySequence::New,this,&StudioWindow::newProject);
    file->addAction(tr("&Open project…"),QKeySequence::Open,this,&StudioWindow::chooseProject);
    saveAction_ = file->addAction(tr("&Save"),QKeySequence::Save,this,[this]{saveChanges();});
    restore_ = file->addAction(tr("Restore previous saved revision"),this,[this]{
        if (project_ && !runtime_) protectedCall([&]{
            project_->restorePrevious(); refresh(); notify(tr("Previous revision restored in Studio. Save to keep it."));
        });
    });
    file->addSeparator();
    homeAction_ = file->addAction(tr("Return to projects"),this,&StudioWindow::toHub);
    file->addAction(tr("Quit"),QKeySequence::Quit,this,&QWidget::close);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    undo_=edit->addAction(tr("Undo"),QKeySequence::Undo,this,[this]{
        if (project_ && !runtime_) protectedCall([&]{project_->undo(); refresh();});
    });
    redo_=edit->addAction(tr("Redo"),QKeySequence(Qt::CTRL|Qt::Key_Y),this,[this]{
        if (project_ && !runtime_) protectedCall([&]{project_->redo(); refresh();});
    });
    redo_->setShortcuts({QKeySequence(Qt::CTRL|Qt::Key_Y),QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_Z)});
    edit->addSeparator();
    duplicate_ = edit->addAction(tr("Duplicate selected object"),QKeySequence(Qt::CTRL|Qt::Key_D),this,[this]{
        if (project_ && !runtime_ && !selected_.empty()) protectedCall([&]{
            selected_=project_->duplicate(selected_,project_->revision()); refresh();
        });
    });
    erase_ = edit->addAction(tr("Delete selected object"),this,[this]{
        if (project_ && !runtime_ && !selected_.empty()) protectedCall([&]{
            project_->remove(selected_,project_->revision()); refresh();
        });
    });
    repeat_=edit->addAction(tr("Repeat objects…"),QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_D),this,&StudioWindow::repeatObjects);
    repeat_->setObjectName("repeatObjects");
    const auto renameSelected=[this]{
        if (selected_.empty() || runtime_) return;
        inspectorDock_->show();
        name_->setFocus();
        name_->selectAll();
    };
    tree_->appendContextActions=[this,renameSelected](QMenu& menu){
        menu.addSeparator();
        auto* rename=menu.addAction(tr("Rename object"),this,renameSelected);
        rename->setEnabled(!runtime_ && !selected_.empty());
        menu.addActions({duplicate_,repeat_,erase_});
    };
    auto* renameKey=new QShortcut(QKeySequence(Qt::Key_F2),tree_);
    renameKey->setContext(Qt::WidgetShortcut);
    connect(renameKey,&QShortcut::activated,this,renameSelected);
    auto* deleteKey = new QShortcut(QKeySequence::Delete,tree_);
    deleteKey->setContext(Qt::WidgetShortcut);
    connect(deleteKey,&QShortcut::activated,erase_,&QAction::trigger);
    createMenu_=menuBar()->addMenu(tr("&Create"));
    auto* geometry=createMenu_->addMenu(tr("3D Mesh"));
    const std::array shapes{platz::NativeShape::Cube,platz::NativeShape::Sphere,platz::NativeShape::Cylinder};
    const std::array names{tr("Cube"),tr("Sphere"),tr("Cylinder")};
    const std::array ids{"createNativeCube","createNativeSphere","createNativeCylinder"};
    for (size_t i=0;i<shapes.size();++i) {
        auto* action=geometry->addAction(names[i],this,[this,shape=shapes[i]]{editNativeMesh({},shape);});
        action->setObjectName(ids[i]);
        nativeCreationActions_[i]=action;
    }
    createImageAction_=createMenu_->addAction(tr("Image / Texture…"),this,[this]{editImage({});});
    createImageAction_->setObjectName("createNativeImage");
    auto* view = menuBar()->addMenu(tr("&View"));
    view->addAction(sceneDock_->toggleViewAction());
    view->addAction(inspectorDock_->toggleViewAction());
    view->addAction(assetsDock_->toggleViewAction());
    frameAction_=view->addAction(tr("Frame selection"),this,[this]{
        if (project_ && !viewport_->dragging()) viewport_->frameSelection();
    });
    frameAction_->setObjectName("frameSelection");
    frameAction_->setShortcutContext(Qt::WidgetShortcut);
    viewport_->addAction(frameAction_);
    view->addAction(tr("Reset camera"),this,[this]{if(project_) viewport_->resetView();});
    view->addAction(tr("GPU diagnostics"),this,[this]{
        QMessageBox details(QMessageBox::Information,tr("GPU diagnostics"),viewport_->diagnostics(),QMessageBox::Ok,this);
        details.setTextFormat(Qt::PlainText);
        details.setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
        details.exec();
    });
    view->addAction(tr("Reinitialize viewport"),this,[this]{if(project_) viewport_->retryRenderer();});
    view->addSeparator();
    auto* customize=view->addAction(tr("Customize Studio…"),this,&StudioWindow::customizeStudio);
    customize->setObjectName("customizeStudio");
    searchCommandsAction_=view->addAction(tr("Search Studio…"),QKeySequence(Qt::CTRL|Qt::Key_K),this,&StudioWindow::findCommands);
    searchCommandsAction_->setObjectName("findStudioCommands");
    buildWorkspaceMenu();
    auto* run = menuBar()->addMenu(tr("&Runtime"));
    playAction_=run->addAction(tr("Play"),QKeySequence(Qt::Key_F5),this,&StudioWindow::togglePlay);
    playAction_->setObjectName("playStudio");
    saveAction_->setObjectName("saveStudio");
    auto* stop = new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(stop,&QShortcut::activated,this,[this]{if(runtime_) togglePlay(); else cancelPreview();});
    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About PLATZ Studio"),this,[this]{
        QMessageBox about(QMessageBox::Information,tr("About PLATZ Studio"),
            tr("PLATZ Studio %1").arg(QStringLiteral(PLATZ_VERSION)),QMessageBox::Ok,this);
        about.setIconPixmap(platzIcon().pixmap(48,48));
        about.setInformativeText(tr("Vulkan 1.4 renderer\n\n%1").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
        about.setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
        about.exec();
    });
    help->addAction(tr("Studio controls"),this,[this]{
        QMessageBox::information(this,tr("Studio controls"),
            tr("Select an object in the scene list or click its surface. In the viewport, use 1 Select, 2 Move, 3 Rotate or 4 Scale. Drag a handle and release to apply. Escape cancels.\n\n"
               "Inspector transform and base-color edits preview as you type. Press Enter or leave the field to apply valid input. Drag a numeric axis label to scrub and hold Shift for finer adjustments. Escape restores the previous value.\n\n"
               "Right drag to orbit, middle drag to pan, or use the wheel to zoom. In the viewport the arrow keys orbit, WASD moves and Home resets the camera.\n\n"
               "Use the Play button to enter the runtime. Click an object or press Space to turn it. Escape returns to the authored scene.\n\n"
               "View → Customize Studio lists the active shortcuts. Ctrl+K finds commands, scene objects and assets."));
    });
}

void StudioWindow::notify(const QString& message) {
    notice_->setText(message);
    notice_->setToolTip(message);
    notice_->setAccessibleName(message);
}
void StudioWindow::protectedCall(const std::function<void()>& work) {
    try { work(); }
    catch (const std::exception& error) {
        syncInspector();
        notify(QString::fromUtf8(error.what()));
    }
}
void StudioWindow::finishActiveField() {
    if (stopScrubbing_) finishPreview(false);
    auto* field=focusWidget();
    if (field && inspector_->isAncestorOf(field)) field->clearFocus();
    if (previewEditor_) finishFieldPreview(previewEditor_);
}
bool StudioWindow::saveChanges() {
    if (!project_ || runtime_) return false;
    viewport_->cancelDrag();
    finishActiveField();
    try {
        project_->save();
        refresh();
        notify(tr("Saved revision %1").arg(project_->savedRevision()));
        return true;
    } catch (const std::exception& error) {
        notify(QString::fromUtf8(error.what()));
        return false;
    }
}
bool StudioWindow::confirmLeave() {
    viewport_->cancelDrag();
    finishActiveField();
    if (runtime_) togglePlay();
    if (!project_ || !project_->dirty()) return true;
    auto choice=QMessageBox::warning(this,tr("Unsaved project"),tr("Save changes to %1?").arg(QString::fromStdString(project_->name())),
          QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    return choice==QMessageBox::Discard || (choice==QMessageBox::Save && saveChanges());
}
StudioWindow::~StudioWindow() = default;

void StudioWindow::toHub() {
    if (!confirmLeave()) return;
    captureWorkspace();
    runtime_.reset(); project_.reset(); organization_.reset(); selected_.clear();
    pages_->setCurrentWidget(hub_);
    sceneDock_->hide(); inspectorDock_->hide(); assetsDock_->hide(); tools_->hide();
    sceneDock_->toggleViewAction()->setEnabled(false);
    inspectorDock_->toggleViewAction()->setEnabled(false);
    assetsDock_->toggleViewAction()->setEnabled(false);
    refreshAssets();
    createMenu_->setEnabled(false);
    workspaceMenu_->setEnabled(false);
    for (auto* action:nativeCreationActions_) action->setEnabled(false);
    createImageAction_->setEnabled(false);
    frameAction_->setEnabled(false);
    for (auto* action : {saveAction_,restore_,undo_,redo_,duplicate_,repeat_,erase_,playAction_,homeAction_}) action->setEnabled(false);
    backend_->hide();
    const auto path=QSettings().value("recentProject").toString();
    const bool available=QFileInfo(path).isFile();
    recent_->setText(available ? QDir::toNativeSeparators(path) : tr("No recent project."));
    recentOpen_->setVisible(available);
    setWindowTitle(tr("Projects · PLATZ"));
    notify({});
}

void StudioWindow::attach(platz::Project project) {
    const bool fromHub=!project_;
    runtime_.reset();
    project_=std::move(project);
    organization_=std::make_unique<StudioOrganization>(QString::fromStdString(project_->id()));
    selected_=project_->objects().empty() ? std::string{} : project_->objects().front().id;
    pages_->setCurrentIndex(1);
    if (fromHub) {
        applyWorkspace();
    }
    workspaceMenu_->setEnabled(true);
    sceneDock_->toggleViewAction()->setEnabled(true);
    inspectorDock_->toggleViewAction()->setEnabled(true);
    assetsDock_->toggleViewAction()->setEnabled(true);
    backend_->show();
    viewport_->resetView();
    QSettings().setValue("recentProject",QString::fromStdWString(project_->path().wstring()));
    refresh();
    notify(tr("Opened revision %1").arg(project_->savedRevision()));
}
bool StudioWindow::openProject(const QString& path) {
    if(path.isEmpty() || !confirmLeave()) return false;
    try {
        attach(platz::Project::open(std::filesystem::path(path.toStdWString())));
        return true;
    } catch(const std::exception& error) {
        notify(tr("Open failed · %1").arg(QString::fromUtf8(error.what())));
        return false;
    }
}
void StudioWindow::chooseProject() {
    auto path=QFileDialog::getOpenFileName(this,tr("Open PLATZ project"),{},tr("PLATZ projects (*.platz)"));
    if(!path.isEmpty()) openProject(path);
}
void StudioWindow::newProject() {
    auto path=QFileDialog::getSaveFileName(this,tr("Create PLATZ project"),{},tr("PLATZ projects (*.platz)"),nullptr,
                                            QFileDialog::DontConfirmOverwrite);
    if(path.isEmpty() || !confirmLeave()) return;
    if(!path.endsWith(".platz",Qt::CaseInsensitive)) path+=".platz";
    protectedCall([&]{
        attach(platz::Project::create(std::filesystem::path(path.toStdWString()),QFileInfo(path).completeBaseName().toStdString()));
    });
}
void StudioWindow::closeEvent(QCloseEvent* event) {
    if (!confirmLeave()) {event->ignore();return;}
    captureWorkspace();
    event->accept();
}

void StudioWindow::refresh(const std::string& preferredAsset) {
    if (!project_) return;
    if (stopScrubbing_) {
        stopScrubbing_();
        stopScrubbing_={};
    }
    preview_.reset();
    previewEditor_=nullptr;
    previewInputValid_=false;
    QScopedValueRollback<bool> refreshing(refreshing_,true);
    const auto& objects=runtime_ ? runtime_->objects() : project_->objects();
    if (!selected_.empty() && !project_->find(selected_))
        selected_=objects.empty() ? std::string{} : objects.front().id;
    refreshSceneTree();
    projectTitle_->setText(QString::fromStdString(project_->name()));
    const auto title=QString::fromStdString(project_->name());
    projectTitle_->setText(projectTitle_->fontMetrics().elidedText(title,Qt::ElideRight,224));
    projectTitle_->setToolTip(title);
    auto suffix = project_->dirty() ? tr("  ·  Unsaved") : QString{};
    setWindowTitle(title+suffix+tr(" · PLATZ Studio"));
    mode_->setText(runtime_ ? tr("Play  ·  runtime copy") : tr("Authoring  ·  Project rev. %1%2").arg(project_->revision()).arg(suffix));
    play_->setText(runtime_ ? tr("■  Stop") : tr("▶  Play"));
    playAction_->setText(runtime_ ? tr("Stop") : tr("Play"));
    syncControlHelp();
    frameAction_->setEnabled(true);
    playAction_->setEnabled(true);
    transformControls_->setVisible(!runtime_);
    transformControls_->setEnabled(!runtime_);
    createMenu_->setEnabled(!runtime_);
    for (auto* action:nativeCreationActions_) action->setEnabled(!runtime_);
    createImageAction_->setEnabled(!runtime_);
    homeAction_->setEnabled(true);
    add_->setEnabled(!runtime_);
    undo_->setEnabled(!runtime_ && project_->canUndo());
    redo_->setEnabled(!runtime_ && project_->canRedo());
    saveAction_->setEnabled(!runtime_);
    restore_->setEnabled(!runtime_ && project_->canRestorePrevious());
    syncInspector();
    syncSaveState();
    refreshAssets(preferredAsset);
    viewport_->setScene(objects,selected_);
}
void StudioWindow::refreshSceneTree() {
    if (!project_) return;
    const QSignalBlocker blocker(tree_);
    const auto& objects=runtime_ ? runtime_->objects() : project_->objects();
    tree_->beginRefresh();
    for (const auto& object:objects) {
        auto* item=tree_->addSource(QString::fromStdString(object.id),QString::fromStdString(object.name));
        item->setIcon(0,meshIcon(object.mesh,object.color));
        item->setToolTip(0,QString::fromUtf8(platz::meshName(object.mesh)));
    }
    tree_->endRefresh(QString::fromStdString(selected_),false);
    count_->setText(objects.size()==1 ? tr("1 object") : tr("%1 objects").arg(objects.size()));
}

void StudioWindow::syncInspector() {
    QScopedValueRollback<bool> refreshing(refreshing_,true);
    const platz::Object* object=nullptr;
    if (project_) {
        const auto& objects=runtime_ ? runtime_->objects() : project_->objects();
        const auto it=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.id==selected_;});
        if(it!=objects.end()) object=&*it;
    }
    const bool editable=object && !runtime_;
    const bool embedded=object && object->asset.data;
    const bool native=embedded && !object->asset.nativeId.empty();
    saveGlb_->setVisible(embedded && !runtime_);
    saveGlb_->setText(native ? tr("Export mesh GLB…") : tr("Save original GLB…"));
    saveGlb_->setToolTip(native ? tr("Export the generated geometry. The editable source stays in the project.") :
                                tr("Save the exact imported bytes from the project."));
    locateMesh_->setVisible(embedded && !runtime_);
    locateMesh_->setText(native ? tr("Locate editable source") : tr("Locate imported asset"));
    inspectorDetails_->setVisible(object);
    inspectorEmpty_->setVisible(!object);
    inspectorDetails_->setEnabled(editable);
    syncMeshSources(object);
    duplicate_->setEnabled(editable);
    repeat_->setEnabled(editable);
    erase_->setEnabled(editable);
    updateAssetActions();
    syncMaterial(object);
    if (!object) {
        name_->clear(); mesh_->setText(tr("Select an object")); identity_->clear();
        mesh_->setToolTip({});
        for(auto* field:axis_) field->setValue(0);
        return;
    }
    name_->setText(QString::fromStdString(object->name));
    mesh_->setText(tr("%1 mesh").arg(QString::fromUtf8(platz::meshName(object->mesh))));
    if (embedded) mesh_->setText(tr("%1  ·  %2 vertices  ·  %3 triangles")
        .arg(native ? tr("Native mesh") : tr("GLB"))
        .arg(object->asset.data->vertices().size()).arg(object->asset.data->indices().size()/3));
    mesh_->setToolTip(object->asset.data ? QString::fromStdString("SHA-256 "+object->asset.data->digest()) : QString{});
    identity_->setText(QString::fromStdString(object->id));
    syncTransform(*object);
}
void StudioWindow::syncTransform(const platz::Object& object,bool changedOnly) {
    const QScopedValueRollback<bool> refreshing(refreshing_,true);
    const std::array vectors{object.position,object.rotation,object.scale};
    for (std::size_t g=0;g<vectors.size();++g) {
        const auto [x,y,z]=vectors[g];
        const std::array values{x,y,z};
        for (size_t component=0;component<values.size();++component) {
            auto* field=axis_[g*3+component];
            const double value=std::round(double(values[component])*1000.0)/1000.0;
            if (!changedOnly || value!=field->value()) field->setValue(value);
        }
    }
}
void StudioWindow::syncSaveState() {
    bool savable=false;
    if (project_ && !runtime_) {
        savable=project_->dirty() || (preview_ && previewEditor_ && previewInputValid_ && preview_->before!=preview_->after);
        if (!savable && name_->isModified()) {
            if (const auto* object=project_->find(selected_)) {
                auto draft=*object;
                draft.name=name_->text().toStdString();
                if (draft.name!=object->name) {
                    try {
                        project_->stage(draft,project_->revision());
                        savable=true;
                    } catch (const std::runtime_error&) {
                        // The same project validation runs when the user finishes the edit.
                    }
                }
            }
        }
    }
    save_->setEnabled(savable);
}
void StudioWindow::syncMaterial(const platz::Object* object) {
    const QSignalBlocker block(materialSlot_);
    const QSignalBlocker textureSignals(useTexture_);
    const QSignalBlocker imageSignals(imageSlot_);
    const auto* asset=object ? object->asset.data.get() : nullptr;
    const auto owner=asset ? object->id+asset->digest() : std::string{};
    materialSlot_->setVisible(asset);
    resetSlot_->setVisible(asset);
    if (owner!=materialOwner_) {
        materialOwner_=owner;
        materialSlot_->clear();
        if (asset) {
            const auto& materials=asset->materialSlots();
            for (size_t i=0;i<materials.size();++i) {
                const auto name=QString::fromStdString(materials[i].name).simplified();
                materialSlot_->addItem(name.isEmpty() ? tr("Slot %1").arg(i+1) :
                                                        tr("%1. %2").arg(i+1).arg(name));
            }
        }
    }
    const int index=materialSlot_->currentIndex();
    const auto* slot=asset && index>=0 ? &asset->materialSlots()[index] : nullptr;
    const auto* image=slot ? platz::slotImage(object->images,index) : nullptr;
    const bool compatible=slot && asset->supportsImageSlot(index);
    imageSlot_->clear();
    if (compatible && project_) {
        imageSlot_->addItem(tr("Default texture"),QString{});
        std::vector<std::pair<QString,QString>> choices;
        for (const auto& [key,entry]:project_->imageLibrary()) {
            const auto id=QString::fromStdString(key);
            choices.emplace_back(assetChoiceLabel(true,id,QString::fromStdString(entry.name)),id);
        }
        std::ranges::sort(choices,[](const auto& a,const auto& b){return QString::localeAwareCompare(a.first,b.first)<0;});
        for (const auto& [label,id]:choices) imageSlot_->addItem(label,id);
        if (image) imageSlot_->setCurrentIndex(imageSlot_->findData(QString::fromStdString(image->imageId)));
    }
    imageSlot_->setVisible(compatible);
    locateImage_->setVisible(image && !runtime_);
    makeImageUnique_->setVisible(image && !runtime_);
    updateImageActions();
    appearanceSource_->setVisible(object && !runtime_ && project_->objects().size()>1);
    const bool usesTexture=image || (slot && asset->textureForSlot(index,object->texturesOff));
    useTexture_->setVisible(slot && slot->texture && !image);
    useTexture_->setEnabled(!runtime_ && slot && slot->texture);
    useTexture_->setChecked(usesTexture);
    textureInfo_->setVisible(image || (slot && slot->hasSourceTexture()));
    color_->setToolTip(usesTexture ? tr("This base color tints the texture.") : QString{});
    if (!object) {
        color_->clear();
        swatch_->setStyleSheet({});
        resetSlot_->setEnabled(false);
        return;
    }
    auto value=std::array{object->color.x,object->color.y,object->color.z};
    const bool overridden=slot && std::any_of(object->slotColors.begin(),object->slotColors.end(),
        [index](const auto& color){return color.slot==static_cast<unsigned>(index);});
    resetSlot_->setEnabled(overridden && !runtime_);
    if (slot) {
        value=asset->colorForSlot(index,value,object->slotColors);
        if (image) {
            textureInfo_->setText(tr("Project image · %1 × %2").arg(image->raster->width()).arg(image->raster->height()));
        } else if (slot->texture) {
            const auto format=slot->texture->encoding==platz::TextureEncoding::Png ? "PNG" : "JPEG";
            textureInfo_->setText(tr("Embedded %1 · %2 × %3").arg(format).arg(slot->texture->width).arg(slot->texture->height));
        } else if (slot->hasSourceTexture()) {
            textureInfo_->setText(tr("Flat color · %1").arg(QString::fromStdString(slot->textureNote)));
        }
    }
    const auto hex=QColor::fromRgbF(value[0],value[1],value[2]).name().toUpper();
    color_->setText(hex);
    swatch_->setStyleSheet(QString("background-color: %1").arg(hex));
}
void StudioWindow::applyEdit(const std::function<void(platz::Object&)>& change) {
    if (refreshing_ || runtime_ || !project_) return;
    if (auto original=project_->find(selected_)) protectedCall([&]{
        auto edited=*original;
        change(edited);
        if (edited==*original) return;
        project_->edit(edited,project_->revision()); refresh();
        notify(tr("Unsaved changes"));
    });
}
void StudioWindow::select(std::string id,bool fromViewport) {
    if (!project_ || refreshing_) return;
    // A viewport event can refer to the image immediately preceding a scene update.
    if (!id.empty() && !project_->find(id)) return;
    if (preview_) cancelPreview();
    selected_=id;
    if (runtime_ && fromViewport && runtime_->interact(id)) notify(tr("Turned runtime object"));
    QScopedValueRollback<bool> refreshing(refreshing_,true);
    if (id.empty()) {
        if (fromViewport) {
            tree_->clearSelection();
            tree_->setCurrentItem(nullptr);
        }
    } else if (tree_->selectedSource()!=QString::fromStdString(id) || fromViewport) {
        tree_->revealSource(QString::fromStdString(id));
    }
    syncInspector();
    syncSaveState();
    viewport_->setScene(runtime_ ? runtime_->objects() : project_->objects(),selected_);
}
void StudioWindow::togglePlay() {
    if (!project_) return;
    viewport_->cancelDrag();
    finishActiveField();
    if (runtime_) {
        runtime_.reset(); selected_=beforePlay_; viewport_->endPlay();
        notify(tr("Returned to the authored scene"));
    } else {
        beforePlay_=selected_; runtime_.emplace(*project_); viewport_->beginPlay();
        notify(tr("Play started from revision %1").arg(runtime_->sourceRevision()));
    }
    refresh();
    viewport_->setFocus();
}
