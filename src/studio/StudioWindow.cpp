#include "StudioWindow.hpp"
#include "Appearance.hpp"
#include "Viewport.hpp"
#include <QAction>
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
#include <QToolBar>
#include <algorithm>

StudioWindow::StudioWindow() {
    pages_ = new QStackedWidget;
    setCentralWidget(pages_);
    setWindowIcon(platzIcon());
    resize(1360,820);
    setDockNestingEnabled(true);
    buildHub();
    buildStudio();
    buildMenus();
    resizeDocks({sceneDock_, inspectorDock_},{220,292},Qt::Horizontal);
    resizeDocks({assetsDock_},{270},Qt::Vertical);
    initialLayout_ = saveState(1);
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
    tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    tree_->addActions({duplicate_,repeat_,erase_});
    auto* deleteKey = new QShortcut(QKeySequence::Delete,tree_);
    deleteKey->setContext(Qt::WidgetShortcut);
    connect(deleteKey,&QShortcut::activated,erase_,&QAction::trigger);
    auto* view = menuBar()->addMenu(tr("&View"));
    view->addAction(sceneDock_->toggleViewAction());
    view->addAction(inspectorDock_->toggleViewAction());
    view->addAction(assetsDock_->toggleViewAction());
    view->addAction(tr("Reset panel layout"),this,&StudioWindow::resetPanels);
    view->addAction(tr("Frame selection"),this,[this]{if(project_) viewport_->frameSelection();});
    view->addAction(tr("Reset camera"),this,[this]{if(project_) viewport_->resetView();});
    view->addAction(tr("GPU diagnostics"),this,[this]{
        QMessageBox details(QMessageBox::Information,tr("GPU diagnostics"),viewport_->diagnostics(),QMessageBox::Ok,this);
        details.setTextFormat(Qt::PlainText);
        details.setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
        details.exec();
    });
    view->addAction(tr("Reinitialize viewport"),this,[this]{if(project_) viewport_->retryRenderer();});
    auto* run = menuBar()->addMenu(tr("&Runtime"));
    playAction_=run->addAction(tr("Play"),QKeySequence(Qt::Key_F5),this,&StudioWindow::togglePlay);
    auto* stop = new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(stop,&QShortcut::activated,this,[this]{if(runtime_) togglePlay();});
    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Studio controls"),this,[this]{
        QMessageBox::information(this,tr("Studio controls"),
            tr("Select an object in the scene list or click its surface. Edit a value in the Inspector and press Enter.\n\n"
               "Right drag to orbit, middle drag to pan, or use the wheel to zoom. With the viewport focused, use arrow keys to orbit, WASD to move, F to frame the selection and Home to reset.\n\n"
               "Use F5 for Play. Click an object or press Space to turn it. Escape returns to the authored scene. Ctrl+S saves the project."));
    });
}

void StudioWindow::resetPanels() {
    if (!project_) return;
    restoreState(initialLayout_,1);
    sceneDock_->show();
    inspectorDock_->show();
    assetsDock_->show();
    tools_->show();
    resizeDocks({sceneDock_,inspectorDock_},{220,292},Qt::Horizontal);
    resizeDocks({assetsDock_},{270},Qt::Vertical);
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
    auto* field=focusWidget();
    if (field && inspector_->isAncestorOf(field)) field->clearFocus();
}
bool StudioWindow::saveChanges() {
    if (!project_ || runtime_) return false;
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
    finishActiveField();
    if (runtime_) togglePlay();
    if (!project_ || !project_->dirty()) return true;
    auto choice=QMessageBox::warning(this,tr("Unsaved project"),tr("Save changes to %1?").arg(QString::fromStdString(project_->name())),
          QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    return choice==QMessageBox::Discard || (choice==QMessageBox::Save && saveChanges());
}
void StudioWindow::toHub() {
    if (!confirmLeave()) return;
    if (project_) QSettings().setValue("studioLayout",saveState(1));
    runtime_.reset(); project_.reset(); selected_.clear();
    pages_->setCurrentWidget(hub_);
    sceneDock_->hide(); inspectorDock_->hide(); assetsDock_->hide(); tools_->hide();
    sceneDock_->toggleViewAction()->setEnabled(false);
    inspectorDock_->toggleViewAction()->setEnabled(false);
    assetsDock_->toggleViewAction()->setEnabled(false);
    refreshAssets();
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
    selected_=project_->objects().empty() ? std::string{} : project_->objects().front().id;
    pages_->setCurrentIndex(1);
    if (fromHub) {
        resetPanels();
        const auto saved = QSettings().value("studioLayout").toByteArray();
        if (!saved.isEmpty() && !restoreState(saved,1)) resetPanels();
        tools_->show();
    }
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
    auto path=QFileDialog::getOpenFileName(this,tr("Open PLATZ project"),{},tr("PLATZ projects (*.platz)"),nullptr,QFileDialog::DontUseNativeDialog);
    if(!path.isEmpty()) openProject(path);
}
void StudioWindow::newProject() {
    auto path=QFileDialog::getSaveFileName(this,tr("Create PLATZ project"),{},tr("PLATZ projects (*.platz)"),nullptr,
                                            QFileDialog::DontConfirmOverwrite|QFileDialog::DontUseNativeDialog);
    if(path.isEmpty() || !confirmLeave()) return;
    if(!path.endsWith(".platz",Qt::CaseInsensitive)) path+=".platz";
    protectedCall([&]{
        attach(platz::Project::create(std::filesystem::path(path.toStdWString()),QFileInfo(path).completeBaseName().toStdString()));
    });
}
void StudioWindow::closeEvent(QCloseEvent* event) {
    if (!confirmLeave()) {event->ignore();return;}
    if (project_) QSettings().setValue("studioLayout",saveState(1));
    event->accept();
}

void StudioWindow::refresh(const std::string& preferredAsset) {
    if (!project_) return;
    QScopedValueRollback<bool> refreshing(refreshing_,true);
    const auto& objects=runtime_ ? runtime_->objects() : project_->objects();
    if (!selected_.empty() && !project_->find(selected_))
        selected_=objects.empty() ? std::string{} : objects.front().id;
    tree_->clear();
    for (const auto& o : objects) {
        auto* item=new QListWidgetItem(meshIcon(o.mesh,o.color),QString::fromStdString(o.name),tree_);
        item->setData(Qt::UserRole,QString::fromStdString(o.id));
        item->setToolTip(QString::fromUtf8(platz::meshName(o.mesh))+" · "+QString::fromStdString(o.id));
        if (o.id==selected_) tree_->setCurrentItem(item);
    }
    count_->setText(tr("WORLD   /   %1 OBJECTS").arg(objects.size()));
    projectTitle_->setText(QString::fromStdString(project_->name()));
    auto suffix = project_->dirty() ? tr("  ·  Unsaved") : QString{};
    setWindowTitle(projectTitle_->text()+suffix+tr(" · PLATZ Studio"));
    mode_->setText(runtime_ ? tr("Play  ·  runtime copy") : tr("Authoring  ·  revision %1%2").arg(project_->revision()).arg(suffix));
    play_->setText(runtime_ ? tr("■  Stop") : tr("▶  Play"));
    playAction_->setText(runtime_ ? tr("Stop") : tr("Play"));
    controls_->setText(runtime_ ? tr("Click to turn an object   ·   WASD move   ·   Esc stop") :
                                   tr("RMB orbit   ·   MMB pan   ·   Wheel zoom   ·   F frame"));
    playAction_->setEnabled(true);
    homeAction_->setEnabled(true);
    add_->setEnabled(!runtime_);
    undo_->setEnabled(!runtime_ && project_->canUndo());
    redo_->setEnabled(!runtime_ && project_->canRedo());
    save_->setEnabled(!runtime_ && project_->dirty());
    saveAction_->setEnabled(!runtime_);
    restore_->setEnabled(!runtime_ && project_->canRestorePrevious());
    syncInspector();
    refreshAssets(preferredAsset);
    viewport_->setScene(objects,selected_);
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
    saveGlb_->setVisible(embedded && !runtime_);
    inspector_->setEnabled(editable);
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
    if(object->asset.data) mesh_->setText(tr("GLB  ·  %1 vertices  ·  %2 triangles")
        .arg(object->asset.data->vertices().size()).arg(object->asset.data->indices().size()/3));
    mesh_->setToolTip(object->asset.data ? QString::fromStdString("SHA-256 "+object->asset.data->digest()) : QString{});
    identity_->setText(QString::fromStdString(object->id));
    const std::array vectors{object->position,object->rotation,object->scale};
    for (std::size_t g=0;g<vectors.size();++g) {
        const auto v=vectors[g];
        axis_[g*3]->setValue(v.x); axis_[g*3+1]->setValue(v.y); axis_[g*3+2]->setValue(v.z);
    }
}
void StudioWindow::syncMaterial(const platz::Object* object) {
    const QSignalBlocker block(materialSlot_);
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
    if (!object) {
        color_->clear();
        swatch_->setStyleSheet({});
        resetSlot_->setEnabled(false);
        return;
    }
    auto value=std::array{object->color.x,object->color.y,object->color.z};
    const int index=materialSlot_->currentIndex();
    const bool hasSlot=asset && index>=0;
    const bool overridden=hasSlot && std::any_of(object->slotColors.begin(),object->slotColors.end(),
        [index](const auto& color){return color.slot==static_cast<unsigned>(index);});
    resetSlot_->setEnabled(overridden && !runtime_);
    if (hasSlot) value=asset->colorForSlot(index,value,object->slotColors);
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
void StudioWindow::select(const std::string& id,bool fromViewport) {
    if (!project_ || refreshing_) return;
    // A viewport event can refer to the image immediately preceding a scene update.
    if (!id.empty() && !project_->find(id)) return;
    selected_=id;
    if (runtime_ && fromViewport && runtime_->interact(id)) notify(tr("Turned runtime object"));
    QScopedValueRollback<bool> refreshing(refreshing_,true);
    if(fromViewport) {
        int row=-1;
        for(int i=0;i<tree_->count();++i) if (tree_->item(i)->data(Qt::UserRole).toString().toStdString()==id) {row=i;break;}
        tree_->setCurrentRow(row);
    }
    syncInspector();
    viewport_->setScene(runtime_ ? runtime_->objects() : project_->objects(),selected_);
}
void StudioWindow::togglePlay() {
    if (!project_) return;
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
