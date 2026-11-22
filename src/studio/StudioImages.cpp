#include "StudioWindow.hpp"
#include "OrganizerTree.hpp"
#include <QStyle>
#include "RasterCanvas.hpp"
#include <QAction>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QSaveFile>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

QWidget* StudioWindow::buildImageLibrary() {
    auto* panel=new QWidget;
    auto* layout=new QVBoxLayout(panel);
    layout->setContentsMargins(8,8,8,8);
    imageList_=new OrganizerTree(StudioOrganization::Kind::Images,[this]{return organization_.get();},
                                 [this]{return project_ && !runtime_;});
    layout->addWidget(imageList_->makeControls(panel));
    imageList_->onOrganizationChanged=[this]{
        refreshImages();
        if (!preview_) syncInspector();
    };
    imageList_->setObjectName("imageLibrary");
    imageList_->setAccessibleName(tr("Project image assets"));
    imageList_->setHeaderLabels({tr("Image"),tr("Uses")});
    imageList_->setUniformRowHeights(true);
    imageList_->header()->setStretchLastSection(false);
    imageList_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    imageList_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    emptyImages_=new QLabel;
    emptyImages_->setWordWrap(true);
    emptyImages_->setProperty("role","minor");
    layout->addWidget(emptyImages_,1);
    layout->addWidget(imageList_,1);
    newImage_=new QPushButton(tr("New image…"));
    newImage_->setObjectName("newLibraryImage");
    connect(newImage_,&QPushButton::clicked,this,[this]{createImageAction_->trigger();});
    layout->addWidget(newImage_);
    auto* row=new QHBoxLayout;
    importImage_=new QPushButton(tr("Import…"));
    importImage_->setObjectName("importImagePng");
    importImage_->setAccessibleName(tr("Import PNG into the image library"));
    editImage_=new QPushButton(tr("Edit…"));
    editImage_->setObjectName("editNativeImage");
    manageImage_=new QPushButton(tr("Manage"));
    manageImage_->setAccessibleName(tr("Manage selected image"));
    row->addWidget(importImage_);
    row->addWidget(editImage_);
    row->addWidget(manageImage_);
    layout->addLayout(row);
    connect(imageList_,&QTreeWidget::itemSelectionChanged,this,[this]{updateImageActions();});
    connect(imageList_,&QTreeWidget::itemDoubleClicked,this,[this]{if (editImage_->isEnabled()) editImage(selectedImage());});
    connect(importImage_,&QPushButton::clicked,this,&StudioWindow::importPng);
    connect(editImage_,&QPushButton::clicked,this,[this]{editImage(selectedImage());});
    auto* menu=new QMenu(manageImage_);
    manageImage_->setMenu(menu);
    menu->addSection(tr("Selected material slot"));
    assignImageAction_=menu->addAction(tr("Assign to selected slot"),this,[this]{
        if (const auto target=currentImageTarget()) protectedCall([&]{
            project_->assignImage(*target,selectedImage(),project_->revision());
            refresh();
        });
    });
    assignImageAction_->setObjectName("assignLibraryImage");
    detachImageAction_=menu->addAction(tr("Detach from selected slot"),this,[this]{
        if (const auto target=currentImageTarget()) protectedCall([&]{
            project_->assignImage(*target,{},project_->revision());
            refresh();
        });
    });
    detachImageAction_->setObjectName("detachLibraryImage");
    uniqueImageAction_=menu->addAction(tr("Make selected slot unique…"),this,[this]{
        if (const auto target=currentImageTarget()) nameImage(selectedImage(),true,target);
    });
    menu->addSection(tr("Editable image source"));
    menu->addAction(tr("Rename image…"),this,[this]{nameImage(selectedImage(),false);});
    menu->addAction(tr("Duplicate image…"),this,[this]{nameImage(selectedImage(),true);});
    auto* exportAction=menu->addAction(tr("Export PNG…"),this,[this]{
        const auto id=selectedImage();
        if (project_ && !runtime_ && project_->imageLibrary().contains(id))
            exportPng(project_->imageLibrary().at(id).raster);
    });
    exportAction->setObjectName("exportLibraryPng");
    removeImageAction_=menu->addAction(tr("Remove unused image"),this,[this]{
        if (!project_ || runtime_ || selectedImage().empty()) return;
        protectedCall([&]{
            project_->removeImage(selectedImage(),project_->revision());
            refresh();
        });
    });
    imageList_->appendContextActions=[this,menu](QMenu& context){
        auto* edit=context.addAction(tr("Edit image…"),editImage_,&QPushButton::click);
        edit->setEnabled(editImage_->isEnabled());
        if (manageImage_->isEnabled()) context.addActions(menu->actions());
    };
    return panel;
}

std::optional<platz::ImageTarget> StudioWindow::currentImageTarget() const {
    const auto* object=project_ && !runtime_ ? project_->find(selected_) : nullptr;
    const int slot=materialSlot_ ? materialSlot_->currentIndex() : -1;
    if (!object || !object->asset.data || slot<0 || !object->asset.data->supportsImageSlot(slot)) return {};
    return platz::ImageTarget{object->id,static_cast<unsigned>(slot)};
}
std::string StudioWindow::selectedImage() const {
    return imageList_->selectedSource().toStdString();
}
void StudioWindow::refreshImages(const std::string& preferred) {
    const QSignalBlocker blocker(imageList_);
    imageList_->beginRefresh();
    if (project_) {
        std::map<std::string_view,int> uses;
        for (const auto& object:project_->objects())
            for (const auto& image:object.images) ++uses[image.imageId];
        for (const auto& [id,image]:project_->imageLibrary()) {
            auto* item=imageList_->addSource(QString::fromStdString(id),QString::fromStdString(image.name));
            const QImage pixels(image.raster->pixels().data(),int(image.raster->width()),int(image.raster->height()),
                                int(image.raster->width()*4),QImage::Format_RGBA8888);
            item->setIcon(0,QIcon(QPixmap::fromImage(pixels.scaled(24,24,Qt::KeepAspectRatio,Qt::FastTransformation))));
            item->setData(1,Qt::DisplayRole,uses[id]);
            item->setTextAlignment(1,Qt::AlignRight|Qt::AlignVCenter);
            item->setToolTip(0,tr("RGBA · %1 × %2\nSource ID %3")
                .arg(image.raster->width()).arg(image.raster->height()).arg(QString::fromStdString(id)));
        }
    }
    imageList_->endRefresh(QString::fromStdString(preferred));
    if (!preferred.empty()) imageList_->revealSource(QString::fromStdString(preferred));
    const bool empty=!project_ || project_->imageLibrary().empty();
    emptyImages_->setVisible(empty);
    imageList_->setVisible(imageList_->topLevelItemCount()>0);
    if (empty) {
        bool embedded=false;
        if (project_) for (const auto& [key,entry]:project_->meshLibrary()) {
            const auto& materials=entry.asset.data->materialSlots();
            if (std::ranges::any_of(materials,[](const auto& material){return material.texture.has_value();})) {
                embedded=true;
                break;
            }
        }
        emptyImages_->setText(embedded ? tr("No editable images yet. Imported textures are embedded in their meshes.") :
                                         tr("Create an image to paint, or import a PNG."));
    }
    updateImageActions();
}
void StudioWindow::updateImageActions() {
    if (!imageList_) return;
    const bool editable=project_ && !runtime_;
    const auto selectedRows=imageList_->selectedItems();
    const auto* selected=selectedRows.isEmpty() ? nullptr : selectedRows.front();
    const bool available=editable && selected && project_->imageLibrary().contains(selectedImage());
    const auto target=currentImageTarget();
    const auto* used=target ? platz::slotImage(project_->find(target->objectId)->images,target->slot) : nullptr;
    const bool owns=used && used->imageId==selectedImage();
    newImage_->setEnabled(editable);
    importImage_->setEnabled(editable);
    editImage_->setEnabled(available);
    manageImage_->setEnabled(available);
    assignImageAction_->setEnabled(available && target && !owns);
    detachImageAction_->setEnabled(available && owns);
    uniqueImageAction_->setEnabled(available && owns);
    removeImageAction_->setEnabled(available && selected->data(1,Qt::DisplayRole).toInt()==0);
}
void StudioWindow::showImage(const std::string& id) {
    assetsDock_->show();
    assetTabs_->setCurrentIndex(1);
    refreshImages(id);
    imageList_->setFocus();
}
void StudioWindow::nameImage(const std::string& id,bool copy,std::optional<platz::ImageTarget> target) {
    if (!project_ || runtime_ || !project_->imageLibrary().contains(id)) return;
    finishActiveField();
    const auto revision=project_->revision();
    const auto projectId=project_->id();
    const auto entry=project_->imageLibrary().at(id);
    auto initial=QString::fromStdString(entry.name);
    if (copy) initial+=tr(" copy");
    const auto title=target ? tr("Make selected image slot unique") : copy ? tr("Duplicate image") : tr("Rename image");
    bool accepted=false;
    const auto name=QInputDialog::getText(this,title,tr("Image name"),QLineEdit::Normal,initial,&accepted);
    if (!accepted || !project_ || runtime_ || project_->id()!=projectId) return;
    protectedCall([&]{
        auto selected=id;
        if (copy) selected=project_->createImage(name.toStdString(),entry.raster,target,revision);
        else project_->renameImage(id,name.toStdString(),revision);
        refresh();
        showImage(selected);
    });
}
void StudioWindow::importPng() {
    if (!project_ || runtime_) return;
    finishActiveField();
    const auto revision=project_->revision();
    const auto projectId=project_->id();
    const auto path=QFileDialog::getOpenFileName(this,tr("Import PNG image"),{},tr("PNG images (*.png)"));
    if (path.isEmpty() || !project_ || runtime_ || project_->id()!=projectId) return;
    protectedCall([&]{
        QFile input(path);
        constexpr qint64 maxPng=2u<<20;
        if (!QFileInfo(path).isFile() || !input.open(QIODevice::ReadOnly) || input.size()>maxPng)
            throw std::runtime_error("Select a regular PNG file of at most 2 MiB.");
        auto bytes=input.read(maxPng+1);
        if (bytes.size()>maxPng) throw std::runtime_error("Select a PNG file of at most 2 MiB.");
        const auto view=std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),size_t(bytes.size()));
        const auto size=platz::inspectTexture(view,platz::TextureEncoding::Png);
        if (size[0]>platz::RasterImage::maxSide || size[1]>platz::RasterImage::maxSide)
            throw std::runtime_error("PNG images must be 512 × 512 pixels or smaller.");
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer,"png");
        const auto decoded=reader.read().convertToFormat(QImage::Format_RGBA8888);
        if (decoded.isNull() || decoded.width()!=int(size[0]) || decoded.height()!=int(size[1]))
            throw std::runtime_error("Could not decode this PNG.");
        const auto id=project_->createImage(QFileInfo(path).completeBaseName().toStdString(),
                                            rasterSource(decoded),{},revision);
        refresh();
        showImage(id);
        notify(tr("Imported PNG as an editable project image."));
    });
}
void StudioWindow::exportPng(platz::Raster raster) {
    QFileDialog dialog(this,tr("Export PNG"),{},tr("PNG images (*.png)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("png");
    if (dialog.exec()!=QDialog::Accepted) return;
    const auto path=dialog.selectedFiles().constFirst();
    QSaveFile file(path);
    QImageWriter writer(&file,"png");
    if (!file.open(QIODevice::WriteOnly) || !writer.write(canvasImage(raster)) || !file.commit()) {
        notify(tr("Could not export PNG. Choose a writable destination."));
        return;
    }
    notify(tr("Exported the RGBA pixels as PNG."));
}
