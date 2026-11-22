#include "StudioWindow.hpp"
#include "OrganizerTree.hpp"
#include <QStyle>
#include "Viewport.hpp"
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>

namespace {
QString pickGlb(QWidget* parent, const QString& title) {
    return QFileDialog::getOpenFileName(parent,title,{},QObject::tr("Binary glTF (*.glb)"));
}
}

void StudioWindow::buildAssets() {
    assetsDock_=new QDockWidget(tr("Assets"),this);
    assetsDock_->setObjectName("assetsDock");
    auto* panel=new QWidget;
    auto* layout=new QVBoxLayout(panel);
    layout->setContentsMargins(8,8,8,8);
    layout->setSpacing(6);
    assetList_=new OrganizerTree(StudioOrganization::Kind::Meshes,[this]{return organization_.get();},
                                 [this]{return project_ && !runtime_;});
    layout->addWidget(assetList_->makeControls(panel));
    assetList_->onOrganizationChanged=[this]{
        refreshAssets();
        if (!preview_) syncInspector();
    };
    assetList_->setObjectName("meshLibrary");
    assetList_->setAccessibleName(tr("Project mesh assets"));
    assetList_->setHeaderLabels({tr("Mesh"),tr("Uses")});
    assetList_->setUniformRowHeights(true);
    assetList_->header()->setStretchLastSection(false);
    assetList_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    assetList_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    assetList_->setMinimumHeight(75);
    layout->addWidget(assetList_,1);
    connect(assetList_,&QTreeWidget::itemSelectionChanged,this,[this]{updateAssetActions();});

    auto* actions=new QHBoxLayout;
    importAsset_=new QPushButton(tr("Import…"));
    importAsset_->setObjectName("importMeshAsset");
    importAsset_->setAccessibleName(tr("Import GLB into asset library"));
    placeAsset_=new QPushButton(tr("Place in scene"));
    placeAsset_->setObjectName("placeMeshAsset");
    manageAsset_=new QPushButton(tr("Manage"));
    manageAsset_->setAccessibleName(tr("Manage selected mesh asset"));
    actions->addWidget(importAsset_);
    actions->addWidget(placeAsset_,1);
    layout->addLayout(actions);
    auto* assignment=new QHBoxLayout;
    assignAsset_=new QPushButton(tr("Assign to selection"));
    assignAsset_->setObjectName("assignMeshAsset");
    assignAsset_->setAccessibleName(tr("Assign mesh to selected object"));
    assignment->addWidget(assignAsset_,1);
    assignment->addWidget(manageAsset_);
    layout->addLayout(assignment);

    connect(importAsset_,&QPushButton::clicked,this,[this]{
        if (!project_ || runtime_) return;
        const auto path=pickGlb(this,tr("Import mesh into library"));
        if (path.isEmpty()) return;
        protectedCall([&]{
            const auto before=project_->revision();
            const auto digest=project_->importMeshAsset(path.toStdWString(),before);
            refresh(digest);
            notify(before==project_->revision() ? tr("That GLB is already in the library.") :
                                                   tr("Mesh added to the library."));
        });
    });
    connect(placeAsset_,&QPushButton::clicked,this,[this]{
        if (!project_ || runtime_ || selectedAsset().empty()) return;
        protectedCall([&]{
            selected_=project_->instantiateMesh(selectedAsset(),project_->revision());
            refresh();
            viewport_->frameSelection();
            notify(tr("Added a mesh instance."));
        });
    });
    connect(assignAsset_,&QPushButton::clicked,this,[this]{
        if (!project_ || runtime_ || selectedAsset().empty() || selected_.empty()) return;
        protectedCall([&]{
            project_->assignMesh(selected_,selectedAsset(),project_->revision());
            refresh();
            notify(tr("Assigned the library mesh to the selected object."));
        });
    });

    auto* menu=new QMenu(manageAsset_);
    manageAsset_->setMenu(menu);
    menu->addSection(tr("Mesh source"));
    editMesh_=menu->addAction(tr("Edit mesh parameters…"),this,[this]{editNativeMesh(selectedAsset());});
    editMesh_->setObjectName("editNativeMesh");
    auto* rename=menu->addAction(tr("Rename asset…"),this,[this]{nameMesh(selectedAsset(),false);});
    rename->setObjectName("renameMeshAsset");
    copyMesh_=menu->addAction(tr("Duplicate native mesh…"),this,[this]{nameMesh(selectedAsset(),true);});
    copyMesh_->setObjectName("duplicateNativeMesh");
    auto* saveSource=menu->addAction(tr("Export GLB…"),this,[this]{
        if (!project_ || runtime_) return;
        const auto asset=project_->meshLibrary().find(selectedAsset());
        if (asset!=project_->meshLibrary().end()) writeGlbSource(asset->second.asset.data,asset->second.native.has_value());
    });
    saveSource->setObjectName("saveLibraryGlb");
    removeAsset_=menu->addAction(tr("Remove unused mesh"),this,[this]{
        if (!project_ || runtime_ || selectedAsset().empty()) return;
        protectedCall([&]{
            project_->removeMeshAsset(selectedAsset(),project_->revision());
            refresh();
            notify(tr("Removed the unused mesh. Undo restores it."));
        });
    });
    removeAsset_->setObjectName("removeMeshAsset");
    menu->addSection(tr("Scene instances"));
    detachMesh_=menu->addAction(tr("Make selected instance unique…"),this,[this]{nameMesh(selectedAsset(),true,selected_);});
    detachMesh_->setObjectName("detachNativeMesh");
    replaceInstances_=menu->addAction(tr("Replace all instances from GLB…"),this,[this]{
        if (!project_ || runtime_ || selectedAsset().empty()) return;
        const auto digest=selectedAsset();
        const auto revision=project_->revision();
        const auto path=pickGlb(this,tr("Choose a GLB to replace all instances"));
        if (path.isEmpty()) return;
        protectedCall([&]{
            const auto replacement=project_->replaceMeshInstances(digest,path.toStdWString(),revision);
            refresh(replacement);
            notify(replacement==digest ? tr("The chosen GLB matches the original mesh.") :
                                          tr("Replaced all instances. The original remains in Assets."));
        });
    });
    replaceInstances_->setObjectName("replaceMeshInstances");
    assetList_->appendContextActions=[this,menu](QMenu& context){
        auto* place=context.addAction(placeAsset_->text(),placeAsset_,&QPushButton::click);
        place->setEnabled(placeAsset_->isEnabled());
        auto* assign=context.addAction(assignAsset_->text(),assignAsset_,&QPushButton::click);
        assign->setEnabled(assignAsset_->isEnabled());
        if (manageAsset_->isEnabled()) context.addActions(menu->actions());
    };
    connect(assetList_,&QTreeWidget::itemDoubleClicked,this,[this]{
        if (editMesh_->isEnabled()) editMesh_->trigger();
    });
    assetTabs_=new QTabWidget;
    assetTabs_->setObjectName("assetTabs");
    assetTabs_->addTab(panel,tr("Meshes"));
    assetTabs_->addTab(buildImageLibrary(),tr("Images"));
    assetsDock_->setWidget(assetTabs_);
    addDockWidget(Qt::LeftDockWidgetArea,assetsDock_);
    splitDockWidget(sceneDock_,assetsDock_,Qt::Vertical);
}

std::string StudioWindow::selectedAsset() const {
    return assetList_->selectedSource().toStdString();
}

void StudioWindow::refreshAssets(const std::string& preferred) {
    const QSignalBlocker block(assetList_);
    assetList_->beginRefresh();
    if (project_) {
        std::map<std::string_view,int> uses;
        for (const auto& object : project_->objects())
            if (object.asset.data) ++uses[object.asset.key()];
        for (const auto& [key,mesh] : project_->meshLibrary()) {
            auto* item=assetList_->addSource(QString::fromStdString(key),QString::fromStdString(mesh.name));
            item->setIcon(0,style()->standardIcon(mesh.native ? QStyle::SP_FileDialogDetailedView : QStyle::SP_FileIcon));
            item->setData(1,Qt::DisplayRole,uses[key]);
            item->setTextAlignment(1,Qt::AlignRight|Qt::AlignVCenter);
            item->setToolTip(0,tr("%1 vertices · %2 triangles · %3 material slots\n%4")
                .arg(mesh.asset.data->vertices().size()).arg(mesh.asset.data->indices().size()/3)
                .arg(mesh.asset.data->materialSlots().size())
                .arg(mesh.native ? tr("Editable %1 · %2").arg(QString::fromUtf8(platz::nativeShapeName(mesh.native->shape)),
                                                              QString::fromStdString(key)) :
                                   tr("Imported GLB · SHA-256 %1").arg(QString::fromStdString(key))));
        }
    }
    assetList_->endRefresh(QString::fromStdString(preferred));
    if (!preferred.empty()) assetList_->revealSource(QString::fromStdString(preferred));
    updateAssetActions();
    refreshImages();
}

void StudioWindow::updateAssetActions() {
    const bool editable=project_ && !runtime_;
    const auto selectedRows=assetList_->selectedItems();
    const auto* item=selectedRows.isEmpty() ? nullptr : selectedRows.front();
    const bool hasMesh=editable && item && project_->meshLibrary().contains(selectedAsset());
    const auto* object=hasMesh ? project_->find(selected_) : nullptr;
    const auto key=selectedAsset();
    const auto* library=hasMesh ? &project_->meshLibrary() : nullptr;
    const bool native=library && library->contains(key) && library->at(key).native.has_value();
    importAsset_->setEnabled(editable);
    placeAsset_->setEnabled(hasMesh);
    assignAsset_->setEnabled(object && object->asset.key()!=key);
    manageAsset_->setEnabled(hasMesh);
    editMesh_->setEnabled(native);
    copyMesh_->setEnabled(native);
    detachMesh_->setEnabled(native && object && object->asset.key()==key);
    const int uses=item ? item->data(1,Qt::DisplayRole).toInt() : 0;
    replaceInstances_->setEnabled(hasMesh && uses>0);
    removeAsset_->setEnabled(hasMesh && uses==0);
}

void StudioWindow::nameMesh(const std::string& key,bool copy,std::string detachObject) {
    if (!project_ || runtime_ || !project_->meshLibrary().contains(key)) return;
    finishActiveField();
    const auto revision=project_->revision();
    const auto projectId=project_->id();
    const auto name=QString::fromStdString(project_->meshLibrary().at(key).name);
    const auto title=!detachObject.empty() ? tr("Make instance unique") :
                          copy ? tr("Duplicate native mesh") : tr("Rename mesh asset");
    bool accepted=false;
    const auto result=QInputDialog::getText(this,title,tr("Asset name"),QLineEdit::Normal,
                                          copy ? name+tr(" copy") : name,&accepted);
    if (!accepted || !project_ || runtime_ || project_->id()!=projectId) return;
    protectedCall([&]{
        auto selection=key;
        if (copy) selection=project_->copyNativeMesh(key,result.toStdString(),detachObject,revision);
        else project_->renameMeshAsset(key,result.toStdString(),revision);
        refresh(selection);
        notify(detachObject.empty() ? (copy ? tr("Duplicated the native mesh.") : tr("Renamed the mesh asset.")) :
                                      tr("The selected instance now has independent editable geometry."));
    });
}
