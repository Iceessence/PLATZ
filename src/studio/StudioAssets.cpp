#include "StudioWindow.hpp"
#include "Viewport.hpp"
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>

namespace {
QString pickGlb(QWidget* parent, const QString& title) {
    return QFileDialog::getOpenFileName(parent,title,{},QObject::tr("Binary glTF (*.glb)"),nullptr,
                                       QFileDialog::DontUseNativeDialog);
}
}

void StudioWindow::buildAssets() {
    assetsDock_=new QDockWidget(tr("Assets"),this);
    assetsDock_->setObjectName("assetsDock");
    auto* panel=new QWidget;
    auto* layout=new QVBoxLayout(panel);
    layout->setContentsMargins(8,8,8,8);
    layout->setSpacing(6);
    assetList_=new QTreeWidget;
    assetList_->setObjectName("meshLibrary");
    assetList_->setAccessibleName(tr("Embedded mesh assets"));
    assetList_->setHeaderLabels({tr("Mesh"),tr("Uses")});
    assetList_->setRootIsDecorated(false);
    assetList_->setItemsExpandable(false);
    assetList_->setUniformRowHeights(true);
    assetList_->header()->setStretchLastSection(false);
    assetList_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    assetList_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    assetList_->setMinimumHeight(75);
    layout->addWidget(assetList_,1);
    connect(assetList_,&QTreeWidget::currentItemChanged,this,[this]{updateAssetActions();});

    auto* actions=new QHBoxLayout;
    importAsset_=new QPushButton(tr("Import…"));
    importAsset_->setObjectName("importMeshAsset");
    importAsset_->setAccessibleName(tr("Import GLB into asset library"));
    placeAsset_=new QPushButton(tr("Add instance"));
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
    auto* saveSource=menu->addAction(tr("Save original GLB…"),this,[this]{
        if (!project_ || runtime_) return;
        const auto asset=project_->meshLibrary().find(selectedAsset());
        if (asset!=project_->meshLibrary().end()) writeGlbSource(asset->second.asset.data);
    });
    saveSource->setObjectName("saveLibraryGlb");
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
    removeAsset_=menu->addAction(tr("Remove unused mesh"),this,[this]{
        if (!project_ || runtime_ || selectedAsset().empty()) return;
        protectedCall([&]{
            project_->removeMeshAsset(selectedAsset(),project_->revision());
            refresh();
            notify(tr("Removed the unused mesh. Undo restores it."));
        });
    });
    removeAsset_->setObjectName("removeMeshAsset");
    assetsDock_->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea,assetsDock_);
    splitDockWidget(sceneDock_,assetsDock_,Qt::Vertical);
}

std::string StudioWindow::selectedAsset() const {
    const auto* item=assetList_->currentItem();
    return item ? item->data(0,Qt::UserRole).toString().toStdString() : std::string{};
}

void StudioWindow::refreshAssets(const std::string& preferred) {
    const auto selected=preferred.empty() ? selectedAsset() : preferred;
    const QSignalBlocker block(assetList_);
    assetList_->clear();
    if (project_) {
        std::map<std::string,int> uses;
        for (const auto& object : project_->objects())
            if (object.asset.data) ++uses[object.asset.data->digest()];
        for (const auto& [digest,mesh] : project_->meshLibrary()) {
            auto* item=new QTreeWidgetItem(assetList_);
            item->setText(0,QString::fromStdString(mesh.name));
            item->setData(0,Qt::UserRole,QString::fromStdString(digest));
            item->setData(1,Qt::DisplayRole,uses[digest]);
            item->setTextAlignment(1,Qt::AlignRight|Qt::AlignVCenter);
            item->setToolTip(0,tr("%1 vertices · %2 triangles · %3 material slots\nSHA-256 %4")
                .arg(mesh.asset.data->vertices().size()).arg(mesh.asset.data->indices().size()/3)
                .arg(mesh.asset.data->materialSlots().size())
                .arg(QString::fromStdString(digest)));
            if (digest==selected) assetList_->setCurrentItem(item);
        }
        assetList_->sortItems(0,Qt::AscendingOrder);
        if (!assetList_->currentItem() && assetList_->topLevelItemCount())
            assetList_->setCurrentItem(assetList_->topLevelItem(0));
    }
    updateAssetActions();
}

void StudioWindow::updateAssetActions() {
    const bool editable=project_ && !runtime_;
    const auto* item=assetList_->currentItem();
    const bool hasMesh=editable && item;
    const auto* object=hasMesh ? project_->find(selected_) : nullptr;
    const auto digest=selectedAsset();
    importAsset_->setEnabled(editable);
    placeAsset_->setEnabled(hasMesh);
    assignAsset_->setEnabled(object && (!object->asset.data || object->asset.data->digest()!=digest));
    manageAsset_->setEnabled(hasMesh);
    const int uses=item ? item->data(1,Qt::DisplayRole).toInt() : 0;
    replaceInstances_->setEnabled(hasMesh && uses>0);
    removeAsset_->setEnabled(hasMesh && uses==0);
}
