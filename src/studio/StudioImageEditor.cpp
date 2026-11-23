#include "StudioWindow.hpp"
#include "RasterCanvas.hpp"
#include "Viewport.hpp"
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QDockWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace {
struct ImageDraft {
    QColor brush{"#58CCAC"};
    platz::Raster raster;
    bool scenePreview{};
};

QString rgbaHex(QColor color) {
    return (color.name()+QString("%1").arg(color.alpha(),2,16,QChar('0'))).toUpper();
}
QColor rgbaColor(const QString& hex) {
    static const QRegularExpression pattern("^#[0-9a-fA-F]{8}$");
    if (!pattern.match(hex).hasMatch()) return {};
    return QColor(QString("#")+hex.right(2)+hex.mid(1,6));
}
}

void StudioWindow::editImage(const std::string& id) {
    if (!project_ || runtime_ || imageEditor_ || (!id.empty() && !project_->imageLibrary().contains(id))) return;
    viewport_->cancelDrag();
    finishActiveField();
    const auto revision=project_->revision();
    const auto projectId=project_->id();
    const bool creating=id.empty();
    const auto target=creating ? currentImageTarget() : std::nullopt;
    const auto linked=std::any_of(project_->objects().begin(),project_->objects().end(),[&](const auto& object){
        return std::any_of(object.images.begin(),object.images.end(),[&](const auto& image){return image.imageId==id;});
    });
    QImage initial(32,32,QImage::Format_RGBA8888);
    initial.fill(Qt::transparent);
    if (!creating) initial=canvasImage(project_->imageLibrary().at(id).raster);
    auto* editor=new QWidget;
    editor->setObjectName("imageEditor");
    editor->setAccessibleName(tr("Image editor"));
    auto* panel=new QVBoxLayout(editor);
    panel->setContentsMargins(12,12,12,12);
    auto* heading=new QLabel(creating ? tr("New image") :
                                  tr("Paint %1").arg(QString::fromStdString(project_->imageLibrary().at(id).name)));
    heading->setProperty("role","panelHeading");
    heading->setWordWrap(true);
    panel->addWidget(heading);
    const auto state=std::make_shared<ImageDraft>();
    auto* name=new QLineEdit(creating ? tr("Untitled image") : QString::fromStdString(project_->imageLibrary().at(id).name));
    name->setObjectName("imageName");
    name->setAccessibleName(tr("Image name"));
    name->setMaxLength(240);
    auto* bind=new QCheckBox(tr("Assign to selected material slot"));
    bind->setObjectName("bindNewImage");
    bind->setVisible(target.has_value());
    bind->setChecked(target.has_value());
    if (creating) {
        auto* row=new QHBoxLayout;
        row->addWidget(new QLabel(tr("Name")));
        row->addWidget(name,1);
        panel->addLayout(row);
        panel->addWidget(bind);
    } else {
        name->setParent(editor);
        name->hide();
        bind->setParent(editor);
        bind->hide();
    }
    auto* canvas=new RasterCanvas(std::move(initial));
    auto* tools=new QHBoxLayout;
    auto* group=new QButtonGroup(editor);
    const std::array labels{tr("Pencil"),tr("Eraser"),tr("Fill"),tr("Eyedropper")};
    const std::array names{"imagePencil","imageEraser","imageFill","imageEyedropper"};
    for (int index=0;index<int(labels.size());++index) {
        auto* button=new QToolButton;
        button->setObjectName(names[index]);
        button->setText(labels[index]);
        button->setCheckable(true);
        button->setChecked(index==0);
        group->addButton(button,index);
        tools->addWidget(button);
    }
    connect(group,&QButtonGroup::idClicked,editor,[canvas](int index){
        canvas->setTool(static_cast<RasterCanvas::Tool>(index));
    });
    tools->addStretch(1);
    auto* undo=new QToolButton;
    auto* redo=new QToolButton;
    undo->setObjectName("undoImageStroke");
    redo->setObjectName("redoImageStroke");
    undo->setText(tr("Undo"));
    redo->setText(tr("Redo"));
    undo->setEnabled(false);
    redo->setEnabled(false);
    tools->addWidget(undo);
    tools->addWidget(redo);
    connect(undo,&QToolButton::clicked,editor,[canvas]{canvas->undo();});
    connect(redo,&QToolButton::clicked,editor,[canvas]{canvas->redo();});
    auto* undoKey=new QShortcut(QKeySequence::Undo,editor);
    auto* redoKey=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_Y),editor);
    connect(undoKey,&QShortcut::activated,undo,&QToolButton::click);
    connect(redoKey,&QShortcut::activated,redo,&QToolButton::click);
    undoKey->setContext(Qt::WidgetWithChildrenShortcut);
    redoKey->setContext(Qt::WidgetWithChildrenShortcut);
    panel->addLayout(tools);

    auto* colorRow=new QHBoxLayout;
    auto* color=new QLineEdit("#58CCACFF");
    color->setObjectName("imageBrushRgba");
    color->setAccessibleName(tr("Brush color in RRGGBBAA order"));
    color->setMaxLength(9);
    color->setToolTip(tr("Eight hexadecimal digits in red, green, blue, alpha order."));
    auto* swatch=new QPushButton(tr("Color…"));
    swatch->setObjectName("imageBrushPicker");
    colorRow->addWidget(swatch);
    colorRow->addWidget(color);
    colorRow->addStretch(1);
    auto* zoom=new QComboBox;
    zoom->setObjectName("imageZoom");
    zoom->setAccessibleName(tr("Canvas zoom"));
    for (int scale:{1,2,4,8,16,24}) zoom->addItem(tr("%1%").arg(scale*100),scale);
    const int startZoom=canvas->image().width()>128 || canvas->image().height()>128 ? 1 : 3;
    zoom->setCurrentIndex(startZoom);
    canvas->setZoom(zoom->currentData().toInt());
    connect(zoom,&QComboBox::currentIndexChanged,editor,[canvas,zoom]{canvas->setZoom(zoom->currentData().toInt());});
    auto* grid=new QCheckBox(tr("Grid"));
    grid->setChecked(true);
    connect(grid,&QCheckBox::toggled,editor,[canvas](bool value){canvas->setGrid(value);});
    colorRow->addWidget(zoom);
    colorRow->addWidget(grid);
    panel->addLayout(colorRow);
    auto setBrush=[state,canvas,color](QColor value){
        state->brush=value;
        canvas->setColor(value);
        const QSignalBlocker block(color);
        color->setText(rgbaHex(value));
    };
    connect(color,&QLineEdit::editingFinished,editor,[state,color,setBrush]{
        const auto value=rgbaColor(color->text());
        setBrush(value.isValid() ? value : state->brush);
    });
    connect(swatch,&QPushButton::clicked,editor,[state,editor,setBrush]{
        const auto chosen=QColorDialog::getColor(state->brush,editor,tr("Brush RGBA"),
            QColorDialog::ShowAlphaChannel|QColorDialog::DontUseNativeDialog);
        if (chosen.isValid()) setBrush(chosen);
    });
    canvas->sampled=setBrush;

    auto* scroll=new QScrollArea;
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidget(canvas);
    canvas->revealPixel=[scroll](QPoint pixel){scroll->ensureVisible(pixel.x(),pixel.y(),12,12);};
    scroll->setMinimumSize(205,180);
    panel->addWidget(scroll,1);
    auto* dimensions=new QHBoxLayout;
    auto* width=new QSpinBox;
    auto* height=new QSpinBox;
    width->setObjectName("imageWidth");
    height->setObjectName("imageHeight");
    width->setAccessibleName(tr("Image width in pixels"));
    height->setAccessibleName(tr("Image height in pixels"));
    for (auto* field:{width,height}) {
        field->setRange(1,platz::RasterImage::maxSide);
        field->setKeyboardTracking(false);
    }
    width->setValue(canvas->image().width());
    height->setValue(canvas->image().height());
    dimensions->addWidget(new QLabel(tr("Pixels")));
    dimensions->addWidget(width);
    dimensions->addWidget(new QLabel("×"));
    dimensions->addWidget(height);
    dimensions->addStretch(1);
    auto* exportDraft=new QPushButton(tr("Export PNG…"));
    exportDraft->setObjectName("exportDraftPng");
    dimensions->addWidget(exportDraft);
    connect(exportDraft,&QPushButton::clicked,editor,[this,canvas]{
        protectedCall([&]{exportPng(rasterSource(canvas->image()));});
    });
    panel->addLayout(dimensions);
    auto resizeImage=[canvas,width,height]{canvas->resizeImage({width->value(),height->value()});};
    connect(width,&QSpinBox::valueChanged,editor,resizeImage);
    connect(height,&QSpinBox::valueChanged,editor,resizeImage);
    auto* status=new QLabel;
    status->setObjectName("imagePreviewStatus");
    status->setWordWrap(true);
    panel->addWidget(status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    auto* apply=buttons->button(QDialogButtonBox::Ok);
    apply->setText(creating ? tr("Create image") : tr("Apply to image"));
    apply->setObjectName("acceptNativeImage");
    apply->setEnabled(false);
    apply->setAutoDefault(false);
    panel->addWidget(buttons);
    auto* discard=buttons->button(QDialogButtonBox::Cancel);
    discard->setObjectName("discardImageDraft");
    discard->setText(tr("Discard draft"));
    connect(discard,&QPushButton::clicked,this,&StudioWindow::closeImageEditor);

    const auto current=[this,editor,projectId,revision]{
        return imageEditor_==editor && project_ && !runtime_ && project_->id()==projectId && project_->revision()==revision;
    };
    const auto assignment=[bind,target]{return bind->isChecked() ? target : std::nullopt;};
    const auto verifyName=[state,apply,creating,name]{
        apply->setEnabled(state->raster && (!creating || (!name->text().trimmed().isEmpty() && name->text().toUtf8().size()<=240)));
    };
    const auto preview=[this,canvas,state,status,id,revision,linked,current,assignment,verifyName]{
        if (!current()) return;
        try {
            auto raster=rasterSource(canvas->image());
            const auto binding=assignment();
            auto objects=project_->previewImage(id,raster,binding,revision);
            if (linked || binding) {
                viewport_->setScene(objects,selected_);
                state->scenePreview=true;
                status->setText(tr("Live base-color preview · Alpha is retained in the image and its PNG."));
            } else {
                if (state->scenePreview) viewport_->setScene(project_->objects(),selected_);
                state->scenePreview=false;
                status->setText(tr("Assign this image to a UV-mapped material slot to see it in the viewport."));
            }
            state->raster=std::move(raster);
        } catch (const std::exception& error) {
            if (state->scenePreview) viewport_->setScene(project_->objects(),selected_);
            state->scenePreview=false;
            state->raster.reset();
            status->setText(QString::fromUtf8(error.what()));
        }
        verifyName();
    };
    auto* timer=new QTimer(editor);
    timer->setInterval(16);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setSingleShot(true);
    connect(timer,&QTimer::timeout,editor,preview);
    const auto schedule=[timer,state,status,verifyName]{
        state->raster.reset();
        status->setText(tr("Updating preview…"));
        verifyName();
        if (!timer->isActive()) timer->start();
    };
    canvas->changed=[width,height,canvas,undo,redo,schedule]{
        const QSignalBlocker blockWidth(width),blockHeight(height);
        width->setValue(canvas->image().width());
        height->setValue(canvas->image().height());
        undo->setEnabled(canvas->canUndo());
        redo->setEnabled(canvas->canRedo());
        schedule();
    };
    connect(bind,&QCheckBox::toggled,editor,schedule);
    connect(name,&QLineEdit::textChanged,editor,verifyName);
    connect(apply,&QPushButton::clicked,editor,[this,id,name,state,creating,revision,current,assignment,status]{
        if (!current() || !state->raster) return;
        try {
            const auto selected=creating ? project_->createImage(name->text().toStdString(),state->raster,assignment(),revision) : id;
            if (!creating) project_->editImage(id,state->raster,revision);
            closeImageEditor();
            showImage(selected);
            notify(creating ? tr("Created an editable image source.") : tr("Applied image edits to linked slots."));
        } catch (const std::exception& error) {
            status->setText(QString::fromUtf8(error.what()));
        }
    });

    beforePaintLayout_=saveState(1);
    imageEditor_=editor;
    workSurface_->insertWidget(0,editor);
    workSurface_->setSizes({std::max(360,workSurface_->width()/2),std::max(260,workSurface_->width()/2)});
    refresh();
    editor->show();
    if (creating) {
        name->setFocus();
        name->selectAll();
    } else {
        group->buttons().front()->setFocus();
    }
    QTimer::singleShot(0,editor,preview);
}

void StudioWindow::closeImageEditor() {
    if (!imageEditor_) return;
    auto* editor=std::exchange(imageEditor_,nullptr);
    editor->setObjectName({});
    editor->hide();
    editor->deleteLater();
    refresh();
    restoreState(std::exchange(beforePaintLayout_,QByteArray{}),1);
    viewport_->setFocus();
}
