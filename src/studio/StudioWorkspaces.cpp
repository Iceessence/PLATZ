#include "StudioWindow.hpp"
#include "Viewport.hpp"
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <algorithm>

namespace {
constexpr int layoutVersion=1;
constexpr int maxLayoutBytes=64*1024;
constexpr int maxCustomWorkspaces=16;
QByteArray boundedLayout(const QVariant& value) {
    const auto bytes=value.toByteArray();
    return bytes.size()<=maxLayoutBytes ? bytes : QByteArray{};
}
bool validWorkspaceName(const QString& name) {
    return !name.isEmpty() && name.size()<=48 &&
           std::none_of(name.begin(),name.end(),[](QChar character){return character.category()==QChar::Other_Control;});
}
bool customWorkspace(const QString& id) {
    if (id=="custom-legacy") return true;
    return id.size()==43 && id.startsWith("custom-") && !QUuid::fromString(id.mid(7)).isNull();
}
}

void StudioWindow::buildWorkspaceMenu() {
    workspaceMenu_=menuBar()->addMenu(tr("Work&space"));
    workspaceSwitchMenu_=workspaceMenu_->addMenu(tr("Switch workspace"));
    auto* save=workspaceMenu_->addAction(tr("Save workspace as…"),this,[this]{nameWorkspace(false);});
    save->setObjectName("saveWorkspaceAs");
    renameWorkspaceAction_=workspaceMenu_->addAction(tr("Rename workspace…"),this,[this]{nameWorkspace(true);});
    removeWorkspaceAction_=workspaceMenu_->addAction(tr("Remove workspace…"),this,&StudioWindow::removeWorkspace);
    workspaceMenu_->addSeparator();
    auto* reset=workspaceMenu_->addAction(tr("Reset workspace"),this,&StudioWindow::resetPanels);
    reset->setObjectName("resetWorkspace");
}

StudioWindow::Workspace* StudioWindow::workspace(const QString& id) {
    const auto match=std::find_if(workspaces_.begin(),workspaces_.end(),[&](const auto& candidate){return candidate.id==id;});
    return match==workspaces_.end() ? nullptr : &*match;
}
void StudioWindow::loadWorkspaces() {
    workspaces_={{"modeling",tr("Modeling")},{"materials",tr("Materials")},{"texture",tr("Texture"),{},{},1,1},
                 {"playtest",tr("Playtest")}};
    QSettings settings;
    const auto legacy=boundedLayout(settings.value("studioLayout"));
    settings.beginGroup("StudioWorkspaces/v1");
    const bool initialized=settings.value("initialized",false).toBool();
    activeWorkspace_=settings.value("active","modeling").toString();
    settings.beginGroup("layouts");
    for (auto& builtIn:workspaces_) {
        const auto record=settings.value(builtIn.id).toMap();
        builtIn.layout=boundedLayout(record.value("layout"));
        builtIn.tab=std::clamp(record.value("tab",builtIn.originalTab).toInt(),0,1);
    }
    int customCount=0;
    for (const auto& id:settings.childKeys()) {
        if (customCount>=maxCustomWorkspaces) break;
        if (!customWorkspace(id)) continue;
        const auto record=settings.value(id).toMap();
        const auto name=record.value("name").toString().trimmed();
        if (!validWorkspaceName(name)) continue;
        workspaces_.push_back({id,name,boundedLayout(record.value("layout")),boundedLayout(record.value("original")),
            std::clamp(record.value("tab").toInt(),0,1),std::clamp(record.value("originalTab").toInt(),0,1)});
        ++customCount;
    }
    if (!initialized && !legacy.isEmpty() && !workspace("custom-legacy")) {
        activeWorkspace_="custom-legacy";
        workspaces_.push_back({activeWorkspace_,tr("Custom"),legacy,legacy});
    }
    if (!workspace(activeWorkspace_)) activeWorkspace_="modeling";
    rebuildWorkspacePicker();
}
void StudioWindow::persistWorkspaces(const QString& updated,const QString& removed) {
    QSettings settings;
    settings.beginGroup("StudioWorkspaces/v1");
    if (const auto* item=updated.isEmpty() ? nullptr : workspace(updated))
        settings.setValue("layouts/"+updated,QVariantMap{{"name",item->name},{"layout",item->layout},
            {"original",item->original},{"tab",item->tab},{"originalTab",item->originalTab}});
    if (!removed.isEmpty()) settings.remove("layouts/"+removed);
    settings.setValue("initialized",true);
    settings.setValue("active",activeWorkspace_);
    settings.sync();
    if (settings.status()!=QSettings::NoError) notify(tr("Could not save the workspace preferences."));
}
void StudioWindow::captureWorkspace() {
    auto* current=project_ ? workspace(activeWorkspace_) : nullptr;
    if (!current) return;
    current->layout=saveState(layoutVersion);
    current->tab=assetTabs_->currentIndex();
    persistWorkspaces(current->id);
}
void StudioWindow::rebuildWorkspacePicker() {
    const QSignalBlocker blocker(workspacePicker_);
    workspacePicker_->clear();
    workspaceSwitchMenu_->clear();
    for (const auto& item:workspaces_) {
        workspacePicker_->addItem(item.name,item.id);
        auto* action=workspaceSwitchMenu_->addAction(item.name,this,[this,id=item.id]{switchWorkspace(id);});
        action->setData(item.id);
        action->setCheckable(true);
        action->setChecked(item.id==activeWorkspace_);
    }
    workspacePicker_->setCurrentIndex(workspacePicker_->findData(activeWorkspace_));
    renameWorkspaceAction_->setEnabled(customWorkspace(activeWorkspace_));
    removeWorkspaceAction_->setEnabled(customWorkspace(activeWorkspace_));
}
void StudioWindow::applyWorkspacePreset(const QString& id) {
    restoreState(initialLayout_,layoutVersion);
    sceneDock_->setVisible(id!="texture" && id!="playtest");
    inspectorDock_->setVisible(id!="playtest");
    assetsDock_->setVisible(id!="playtest");
    if (id=="materials") {
        resizeDocks({sceneDock_,inspectorDock_},{220,384},Qt::Horizontal);
        resizeDocks({assetsDock_},{334},Qt::Vertical);
        QTimer::singleShot(0,this,[this]{
            if (!project_ || activeWorkspace_!="materials") return;
            auto* scroll=inspectorDock_->findChild<QScrollArea*>("inspectorScroll");
            if (auto* section=scroll->findChild<QWidget*>("inspectorMaterialToggle"))
                scroll->ensureWidgetVisible(section,0,32);
        });
    } else {
        resizeDocks({sceneDock_,inspectorDock_},{220,292},Qt::Horizontal);
        resizeDocks({assetsDock_},{270},Qt::Vertical);
    }
    if (id=="texture") resizeDocks({assetsDock_,inspectorDock_},{274,315},Qt::Horizontal);
    assetTabs_->setCurrentIndex(id=="texture" ? 1 : 0);
}
void StudioWindow::applyWorkspace() {
    auto* current=workspace(activeWorkspace_);
    if (!current) return;
    if (!current->layout.isEmpty() && restoreState(current->layout,layoutVersion)) {
        assetTabs_->setCurrentIndex(current->tab);
    } else if (!current->original.isEmpty() && restoreState(current->original,layoutVersion)) {
        assetTabs_->setCurrentIndex(current->originalTab);
    } else {
        applyWorkspacePreset(current->id);
    }
    tools_->show();
    syncWorkspaceTools();
    workspacePicker_->setToolTip(tr("Active workspace: %1. Switch to another layout here.").arg(current->name));
    {
        const QSignalBlocker blocker(workspacePicker_);
        workspacePicker_->setCurrentIndex(workspacePicker_->findData(activeWorkspace_));
    }
    for (auto* item:workspaceSwitchMenu_->actions()) item->setChecked(item->data()==activeWorkspace_);
    renameWorkspaceAction_->setEnabled(customWorkspace(activeWorkspace_));
    removeWorkspaceAction_->setEnabled(customWorkspace(activeWorkspace_));
    QTimer::singleShot(0,this,&StudioWindow::recoverFloatingPanels);
}
void StudioWindow::switchWorkspace(const QString& id) {
    if (!project_ || id==activeWorkspace_ || !workspace(id)) return;
    viewport_->cancelDrag();
    finishActiveField();
    captureWorkspace();
    activeWorkspace_=id;
    applyWorkspace();
    persistWorkspaces();
}
void StudioWindow::resetPanels() {
    auto* current=project_ ? workspace(activeWorkspace_) : nullptr;
    if (!current) return;
    viewport_->cancelDrag();
    finishActiveField();
    current->layout=QByteArray{};
    current->tab=current->originalTab;
    applyWorkspace();
    captureWorkspace();
    notify(tr("Reset %1 workspace.").arg(current->name));
}
void StudioWindow::nameWorkspace(bool rename) {
    auto* current=project_ ? workspace(activeWorkspace_) : nullptr;
    if (!current || (rename && !customWorkspace(current->id))) return;
    if (!rename && workspaces_.size()>=4+maxCustomWorkspaces) {
        notify(tr("Remove a custom workspace before saving another."));
        return;
    }
    const auto title=rename ? tr("Rename workspace") : tr("Save workspace as");
    bool accepted=false;
    const auto name=QInputDialog::getText(this,title,tr("Workspace name"),QLineEdit::Normal,
                                          rename ? current->name : QString{},&accepted).trimmed();
    if (!accepted) return;
    if (!validWorkspaceName(name) || std::any_of(workspaces_.begin(),workspaces_.end(),[&](const auto& item){
            return (!rename || item.id!=activeWorkspace_) && !item.name.compare(name,Qt::CaseInsensitive);
        })) {
        notify(tr("Use a unique workspace name of 1–48 characters."));
        return;
    }
    captureWorkspace();
    if (rename) {
        current->name=name;
    } else {
        const auto id="custom-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        workspaces_.push_back({id,name,current->layout,current->layout,current->tab,current->tab});
        activeWorkspace_=id;
    }
    rebuildWorkspacePicker();
    persistWorkspaces(activeWorkspace_);
    notify(tr("Saved workspace %1.").arg(name));
}
void StudioWindow::removeWorkspace() {
    auto* current=project_ ? workspace(activeWorkspace_) : nullptr;
    if (!current || !customWorkspace(current->id)) return;
    if (QMessageBox::question(this,tr("Remove workspace"),tr("Remove the saved layout %1?").arg(current->name),
                              QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
    const auto id=activeWorkspace_;
    workspaces_.removeIf([&](const auto& item){return item.id==id;});
    activeWorkspace_="modeling";
    rebuildWorkspacePicker();
    applyWorkspace();
    persistWorkspaces({},id);
}
void StudioWindow::recoverFloatingPanels() {
    if (!project_ || !isVisible()) return;
    const auto screens=QGuiApplication::screens();
    for (auto* dock:{sceneDock_,assetsDock_,inspectorDock_}) {
        if (!dock->isFloating() || dock->isHidden()) continue;
        const auto frame=dock->frameGeometry();
        const QRect title(frame.topLeft(),QSize(frame.width(),30));
        if (std::any_of(screens.begin(),screens.end(),[&](const auto* display){
            const auto visible=display->availableGeometry().intersected(title);
            return visible.width()>=56 && visible.height()>=16;
        })) continue;
        if (auto* display=screen()) dock->move(display->availableGeometry().topLeft()+QPoint(30,50));
    }
}
