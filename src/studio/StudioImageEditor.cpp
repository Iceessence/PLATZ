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
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
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
    if (!project_ || runtime_ || (!id.empty() && !project_->imageLibrary().contains(id))) return;
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
    QDialog dialog(this);
    dialog.setObjectName("imageEditor");
    dialog.setWindowTitle(creating ? tr("Create Image / Texture") :
                                    tr("Edit %1").arg(QString::fromStdString(project_->imageLibrary().at(id).name)));
    auto* panel=new QVBoxLayout(&dialog);
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
        row->addWidget(bind);
        panel->addLayout(row);
    } else {
        name->setParent(&dialog);
        name->hide();
        bind->setParent(&dialog);
        bind->hide();
    }
    auto* canvas=new RasterCanvas(std::move(initial));
    auto* tools=new QHBoxLayout;
    auto* group=new QButtonGroup(&dialog);
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
    connect(group,&QButtonGroup::idClicked,&dialog,[canvas](int index){
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
    connect(undo,&QToolButton::clicked,&dialog,[canvas]{canvas->undo();});
    connect(redo,&QToolButton::clicked,&dialog,[canvas]{canvas->redo();});
    auto* undoKey=new QShortcut(QKeySequence::Undo,&dialog);
    auto* redoKey=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_Y),&dialog);
    connect(undoKey,&QShortcut::activated,undo,&QToolButton::click);
    connect(redoKey,&QShortcut::activated,redo,&QToolButton::click);
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
    connect(zoom,&QComboBox::currentIndexChanged,&dialog,[canvas,zoom]{canvas->setZoom(zoom->currentData().toInt());});
    auto* grid=new QCheckBox(tr("Grid"));
    grid->setChecked(true);
    connect(grid,&QCheckBox::toggled,&dialog,[canvas](bool value){canvas->setGrid(value);});
    colorRow->addWidget(zoom);
    colorRow->addWidget(grid);
    panel->addLayout(colorRow);
    QColor brush("#58CCAC");
    auto setBrush=[&](QColor value){
        brush=value;
        canvas->setColor(value);
        const QSignalBlocker block(color);
        color->setText(rgbaHex(value));
    };
    connect(color,&QLineEdit::editingFinished,&dialog,[&]{
        const auto value=rgbaColor(color->text());
        setBrush(value.isValid() ? value : brush);
    });
    connect(swatch,&QPushButton::clicked,&dialog,[&]{
        const auto chosen=QColorDialog::getColor(brush,&dialog,tr("Brush RGBA"),
            QColorDialog::ShowAlphaChannel|QColorDialog::DontUseNativeDialog);
        if (chosen.isValid()) setBrush(chosen);
    });
    canvas->sampled=setBrush;

    auto* scroll=new QScrollArea;
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidget(canvas);
    scroll->setMinimumSize(420,300);
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
    connect(exportDraft,&QPushButton::clicked,&dialog,[&]{
        protectedCall([&]{exportPng(rasterSource(canvas->image()));});
    });
    panel->addLayout(dimensions);
    auto resizeImage=[&]{canvas->resizeImage({width->value(),height->value()});};
    connect(width,&QSpinBox::valueChanged,&dialog,resizeImage);
    connect(height,&QSpinBox::valueChanged,&dialog,resizeImage);
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
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);

    auto current=[&]{return project_ && !runtime_ && project_->id()==projectId && project_->revision()==revision;};
    auto assignment=[&]{return bind->isChecked() ? target : std::nullopt;};
    platz::Raster draft;
    bool scenePreview=false;
    auto verifyName=[&]{
        apply->setEnabled(draft && (!creating || (!name->text().trimmed().isEmpty() && name->text().toUtf8().size()<=240)));
    };
    auto restoreScene=[&]{
        if (scenePreview && project_) viewport_->setScene(project_->objects(),selected_);
        scenePreview=false;
    };
    auto preview=[&]{
        try {
            if (!current()) throw std::runtime_error("Project changed. Close this editor and try again.");
            auto raster=rasterSource(canvas->image());
            const auto binding=assignment();
            auto objects=project_->previewImage(id,raster,binding,revision);
            if (linked || binding) {
                viewport_->setScene(objects,selected_);
                scenePreview=true;
                if (!viewport_->presentScene()) {
                    restoreScene();
                    viewport_->retryRenderer();
                    throw std::runtime_error("The GPU preview failed. The authored scene was restored.");
                }
                status->setText(tr("Viewport preview · Alpha is kept in source and PNG. The 3D material is opaque."));
            } else {
                restoreScene();
                status->setText(tr("RGBA source · Assign it to a mesh material slot with UV0."));
            }
            draft=std::move(raster);
        } catch (const std::exception& error) {
            draft.reset();
            status->setText(QString::fromUtf8(error.what()));
        }
        verifyName();
    };
    QTimer timer;
    timer.setInterval(55);
    timer.setTimerType(Qt::PreciseTimer);
    timer.setSingleShot(true);
    connect(&timer,&QTimer::timeout,&dialog,preview);
    auto schedule=[&]{
        draft.reset();
        status->setText(tr("Updating preview…"));
        verifyName();
        if (!timer.isActive()) timer.start();
    };
    canvas->changed=[&]{
        const QSignalBlocker blockWidth(width),blockHeight(height);
        width->setValue(canvas->image().width());
        height->setValue(canvas->image().height());
        undo->setEnabled(canvas->canUndo());
        redo->setEnabled(canvas->canRedo());
        schedule();
    };
    connect(bind,&QCheckBox::toggled,&dialog,schedule);
    connect(name,&QLineEdit::textChanged,&dialog,verifyName);
    QTimer::singleShot(0,&dialog,preview);
    dialog.resize(520,540);
    dialog.move(mapToGlobal(QPoint(std::max(0,this->width()-dialog.width()-12),100)));
    const bool confirmed=dialog.exec()==QDialog::Accepted && draft && current();
    timer.stop();
    restoreScene();
    canvas->changed={};
    if (!confirmed) return;
    protectedCall([&]{
        auto selected=id;
        if (creating) selected=project_->createImage(name->text().toStdString(),draft,assignment(),revision);
        else project_->editImage(id,draft,revision);
        refresh();
        showImage(selected);
        notify(creating ? tr("Created an editable image source.") : tr("Applied image edits to linked slots."));
    });
}
