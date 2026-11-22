#include "StudioWindow.hpp"
#include "StudioOrganization.hpp"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
std::array<float,3> baseColor(const platz::Object& object,int slot) {
    const auto color=std::array{object.color.x,object.color.y,object.color.z};
    return slot<0 ? color : object.asset.data->colorForSlot(slot,color,object.slotColors);
}

bool canCopyAppearance(const platz::Object& source,int sourceSlot,const platz::Object& target,int targetSlot) {
    if (sourceSlot<0) return true;
    if (platz::slotImage(source.images,sourceSlot))
        return targetSlot>=0 && target.asset.data->supportsImageSlot(targetSlot);
    if (!source.asset.data->textureForSlot(sourceSlot,source.texturesOff)) return true;
    return targetSlot==sourceSlot && target.asset.data && target.asset.data->digest()==source.asset.data->digest();
}

void copyAppearance(const platz::Object& source,int sourceSlot,platz::Object& target,int targetSlot) {
    const auto color=baseColor(source,sourceSlot);
    if (targetSlot<0) {
        target.color={color[0],color[1],color[2]};
        return;
    }
    target.setSlotColor(targetSlot,color);
    const auto* sourceImage=sourceSlot<0 ? nullptr : platz::slotImage(source.images,sourceSlot);
    auto at=std::ranges::lower_bound(target.images,static_cast<unsigned>(targetSlot),{},&platz::SlotImage::slot);
    if (at!=target.images.end() && at->slot==static_cast<unsigned>(targetSlot)) at=target.images.erase(at);
    if (sourceImage) target.images.insert(at,{static_cast<unsigned>(targetSlot),sourceImage->imageId,sourceImage->raster});
    const bool embedded=sourceSlot>=0 && source.asset.data->textureForSlot(sourceSlot,source.texturesOff);
    if (target.asset.data->materialSlots().at(targetSlot).texture)
        target.setTextureEnabled(targetSlot,embedded);
}
}

void StudioWindow::buildInspectorSources(QVBoxLayout& layout) {
    auto* sourceLabel=new QLabel(tr("Mesh source"));
    sourceLabel->setProperty("role","minor");
    layout.addWidget(sourceLabel);
    meshSourcePicker_=new QComboBox;
    meshSourcePicker_->setObjectName("meshSourcePicker");
    meshSourcePicker_->setAccessibleName(tr("Mesh source for selected object"));
    meshSourcePicker_->setToolTip(tr("Replace this object's mesh using a project asset. The new mesh supplies its own material slots."));
    meshSourcePicker_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    meshSourcePicker_->setMinimumContentsLength(12);
    sourceLabel->setBuddy(meshSourcePicker_);
    layout.addWidget(meshSourcePicker_);
    connect(meshSourcePicker_,&QComboBox::activated,this,[this](int index){
        if (!project_ || runtime_ || refreshing_) return;
        const auto key=meshSourcePicker_->itemData(index).toString().toStdString();
        const auto object=selected_;
        if (key.empty()) return;
        finishActiveField();
        protectedCall([&]{
            project_->assignMesh(object,key,project_->revision());
            refresh(key);
            notify(tr("Updated the mesh source for this object."));
        });
        syncMeshSources(project_->find(selected_));
    });

    makeEditableMesh_=new QPushButton(tr("Make mesh editable"));
    makeEditableMesh_->setObjectName("makeEditableMesh");
    makeEditableMesh_->setToolTip(tr("Create an editable UV-mapped source for this primitive. Keep its transform and object ID."));
    connect(makeEditableMesh_,&QPushButton::clicked,this,[this]{
        if (!project_ || runtime_) return;
        finishActiveField();
        protectedCall([&]{
            const auto key=project_->makePrimitiveEditable(selected_,project_->revision());
            refresh(key);
            notify(tr("Created an editable mesh for this object."));
        });
    });
    layout.addWidget(makeEditableMesh_,0,Qt::AlignLeft);
    geometryActions_=new QToolButton;
    geometryActions_->setText(tr("Editable mesh"));
    geometryActions_->setPopupMode(QToolButton::InstantPopup);
    auto* menu=new QMenu(geometryActions_);
    geometryActions_->setMenu(menu);
    menu->addAction(tr("Edit shared mesh…"),this,[this]{
        if (!project_ || runtime_) return;
        if (const auto* object=project_->find(selected_); object && !object->asset.nativeId.empty())
            editNativeMesh(object->asset.nativeId);
    });
    menu->addAction(tr("Make mesh unique…"),this,[this]{
        if (!project_ || runtime_) return;
        if (const auto* object=project_->find(selected_); object && !object->asset.nativeId.empty())
            nameMesh(object->asset.nativeId,false,object->id);
    });
    layout.addWidget(geometryActions_,0,Qt::AlignLeft);
}

QString StudioWindow::assetChoiceLabel(bool image,const QString& id,QString name) const {
    if (!organization_) return name;
    const auto kind=image ? StudioOrganization::Kind::Images : StudioOrganization::Kind::Meshes;
    const auto& folders=organization_->folders(kind);
    auto parent=organization_->folderOf(kind,id);
    QStringList path;
    while (!parent.isEmpty()) {
        const auto folder=std::ranges::find(folders,parent,&StudioOrganization::Folder::id);
        if (folder==folders.end()) break;
        path.prepend(folder->name);
        parent=folder->parent;
    }
    return path.isEmpty() ? name : name+"    ·    "+path.join(" / ");
}

void StudioWindow::syncMeshSources(const platz::Object* object) {
    const QSignalBlocker blocker(meshSourcePicker_);
    meshSourcePicker_->clear();
    geometryActions_->setVisible(object && !object->asset.nativeId.empty() && !runtime_);
    makeEditableMesh_->setVisible(object && !object->asset.data && !runtime_);
    if (!object || !project_) return;
    if (!object->asset.data)
        meshSourcePicker_->addItem(tr("Built-in %1").arg(QString::fromUtf8(platz::meshName(object->mesh))),QString{});
    std::vector<std::pair<QString,QString>> choices;
    for (const auto& [key,source]:project_->meshLibrary()) {
        const auto id=QString::fromStdString(key);
        choices.emplace_back(assetChoiceLabel(false,id,QString::fromStdString(source.name)),id);
    }
    std::ranges::sort(choices,[](const auto& a,const auto& b){return QString::localeAwareCompare(a.first,b.first)<0;});
    for (const auto& [label,id]:choices) meshSourcePicker_->addItem(label,id);
    const auto key=QString::fromStdString(std::string(object->asset.key()));
    meshSourcePicker_->setCurrentIndex(meshSourcePicker_->findData(key));
    meshSourcePicker_->setEnabled(!runtime_ && meshSourcePicker_->count()>1);
}

void StudioWindow::populateAppearanceSources() {
    auto* menu=appearanceSource_->menu();
    menu->clear();
    if (runtime_ || !project_) return;
    const auto* object=project_->find(selected_);
    if (!object) return;
    const auto targetId=object->id;
    const int slot=object->asset.data ? materialSlot_->currentIndex() : -1;
    const auto buildAction=[&](QMenu& parent,QString title,const platz::Object& source,int sourceSlot){
        parent.addAction(title,this,[this,sourceId=source.id,sourceSlot,targetId,slot]{
            finishActiveField();
            if (!project_ || runtime_ || selected_!=targetId) return;
            const auto* source=project_->find(sourceId);
            if (!source || (sourceSlot>=0 && (!source->asset.data ||
                static_cast<size_t>(sourceSlot)>=source->asset.data->materialSlots().size()))) return;
            applyEdit([&](platz::Object& target){
                if (canCopyAppearance(*source,sourceSlot,target,slot)) copyAppearance(*source,sourceSlot,target,slot);
            });
        });
    };
    for (const auto& source:project_->objects()) {
        if (source.id==targetId) continue;
        const auto name=QString::fromStdString(source.name);
        if (!source.asset.data) {
            buildAction(*menu,name,source,-1);
            continue;
        }
        const auto& materials=source.asset.data->materialSlots();
        QMenu* group=nullptr;
        for (int index=0;index<static_cast<int>(materials.size());++index) {
            if (!canCopyAppearance(source,index,*object,slot)) continue;
            if (materials.size()==1) {
                buildAction(*menu,name,source,index);
            } else {
                if (!group) group=menu->addMenu(name);
                const auto material=QString::fromStdString(materials[index].name).simplified();
                buildAction(*group,material.isEmpty() ? tr("Slot %1").arg(index+1) : material,source,index);
            }
        }
    }
    if (menu->isEmpty()) menu->addAction(tr("No compatible sources"))->setEnabled(false);
}
