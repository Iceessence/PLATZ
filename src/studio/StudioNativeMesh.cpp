#include "StudioWindow.hpp"
#include "Viewport.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

void StudioWindow::editNativeMesh(const std::string& key,platz::NativeShape shape) {
    if (!project_ || runtime_) return;
    finishActiveField();
    const bool creating=key.empty();
    const auto item=project_->meshLibrary().find(key);
    if (!creating && (item==project_->meshLibrary().end() || !item->second.native)) return;
    const auto initial=creating ? platz::defaultNativeMesh(shape) : *item->second.native;
    const auto revision=project_->revision();
    const auto projectId=project_->id();
    const auto objectId=selected_;
    const auto instances=std::count_if(project_->objects().begin(),project_->objects().end(),
        [&](const auto& object){return !creating && object.asset.key()==key;});
    const auto shapeLabel=QString::fromUtf8(platz::nativeShapeName(initial.shape));
    QDialog dialog(this);
    dialog.setObjectName("nativeMeshDialog");
    dialog.setWindowTitle(creating ? tr("Create %1 mesh").arg(shapeLabel) :
                                    tr("Edit %1").arg(QString::fromStdString(item->second.name)));
    auto* layout=new QVBoxLayout(&dialog);
    auto* form=new QFormLayout;
    layout->addLayout(form);
    QLineEdit* name=nullptr;
    QCheckBox* place=nullptr;
    if (creating) {
        name=new QLineEdit(shapeLabel);
        name->setObjectName("nativeMeshName");
        name->setMaxLength(240);
        form->addRow(tr("Asset name"),name);
        place=new QCheckBox(tr("Place an instance in the scene"));
        place->setObjectName("placeNativeMesh");
        place->setEnabled(project_->objects().size()<512);
        place->setChecked(place->isEnabled());
        form->addRow(place);
    } else {
        auto* info=new QLabel(tr("%n linked instance(s) will use this geometry.",nullptr,static_cast<int>(instances)));
        info->setWordWrap(true);
        layout->insertWidget(0,info);
    }
    const bool cube=initial.shape==platz::NativeShape::Cube;
    const bool cylinder=initial.shape==platz::NativeShape::Cylinder;
    const auto labels=cube ? std::array{tr("Width"),tr("Height"),tr("Depth")} :
                            std::array{tr("X diameter"),tr("Height"),tr("Z diameter")};
    const std::array ids{"meshWidth","meshHeight","meshDepth"};
    std::array<QDoubleSpinBox*,3> sizes{};
    for (size_t axis=0;axis<sizes.size();++axis) {
        auto* field=new QDoubleSpinBox;
        field->setObjectName(ids[axis]);
        field->setRange(.01,100);
        field->setDecimals(3);
        field->setSuffix(tr(" m"));
        field->setSingleStep(.1);
        field->setValue(initial.size[axis]);
        form->addRow(labels[axis],field);
        sizes[axis]=field;
    }
    auto* segments=new QSpinBox;
    segments->setObjectName("meshSegments");
    segments->setRange(cube ? 1 : 3,cube ? 24 : 128);
    segments->setValue(initial.segments);
    form->addRow(cube ? tr("Divisions per face edge") : tr("Radial segments"),segments);
    QSpinBox* rings=nullptr;
    if (!cube) {
        rings=new QSpinBox;
        rings->setObjectName("meshRings");
        rings->setRange(cylinder ? 1 : 2,cylinder ? 32 : 64);
        rings->setValue(initial.rings);
        form->addRow(cylinder ? tr("Height divisions") : tr("Latitude divisions"),rings);
    }
    QDoubleSpinBox* taper=nullptr;
    if (cylinder) {
        taper=new QDoubleSpinBox;
        taper->setObjectName("meshTopScale");
        taper->setRange(.1,2);
        taper->setDecimals(3);
        taper->setSingleStep(.1);
        taper->setValue(initial.topScale);
        form->addRow(tr("Top diameter multiplier"),taper);
    }
    auto* status=new QLabel;
    status->setObjectName("meshPreviewStatus");
    status->setWordWrap(true);
    status->setMinimumWidth(245);
    layout->addWidget(status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    auto* accept=buttons->button(QDialogButtonBox::Ok);
    accept->setText(creating ? tr("Create mesh") : tr("Apply to asset"));
    accept->setObjectName("acceptNativeMesh");
    accept->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto source=[&]{
        auto value=initial;
        for (size_t axis=0;axis<sizes.size();++axis) value.size[axis]=float(sizes[axis]->value());
        value.segments=segments->value();
        if (rings) value.rings=rings->value();
        if (taper) value.topScale=float(taper->value());
        return value;
    };
    auto current=[&]{return project_ && !runtime_ && project_->id()==projectId && project_->revision()==revision;};
    bool ready=false;
    std::optional<platz::NativeMeshSource> presented;
    QString presentedStatus;
    auto updateAccept=[&]{
        const auto bytes=name ? name->text().toUtf8() : QByteArray{};
        accept->setEnabled(ready && (!name || (!name->text().trimmed().isEmpty() && bytes.size()<=240)));
    };
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(45);
    timer.setTimerType(Qt::PreciseTimer);
    auto refreshPreview=[&]{
        const auto value=source();
        if (presented==value && current()) {
            status->setText(presentedStatus);
            ready=true;
            updateAccept();
            return;
        }
        try {
            if (!current()) throw std::runtime_error("Project changed. Close this dialog and try again.");
            auto preview=project_->previewNativeMesh(key,value,revision);
            const auto selected=std::any_of(preview.objects.begin(),preview.objects.end(),[&](const auto& object){
                return object.id==objectId && object.asset.key()==key && !creating;}) ? objectId : preview.selected;
            viewport_->setScene(preview.objects,selected);
            if (!viewport_->presentScene()) {
                presented.reset();
                viewport_->setScene(project_->objects(),selected_);
                viewport_->retryRenderer();
                throw std::runtime_error("The GPU could not display this mesh. The authored scene was restored.");
            }
            presented=value;
            presentedStatus=tr("Preview ready · %1 vertices · %2 triangles")
               .arg(preview.geometry->vertices().size()).arg(preview.geometry->indices().size()/3);
            status->setText(presentedStatus);
            ready=true;
        } catch (const std::exception& error) {
            status->setText(QString::fromUtf8(error.what()));
            ready=false;
        }
        updateAccept();
    };
    auto schedule=[&]{
        ready=false;
        status->setText(tr("Updating preview…"));
        updateAccept();
        timer.start();
    };
    connect(&timer,&QTimer::timeout,&dialog,refreshPreview);
    for (auto* field:sizes) connect(field,&QDoubleSpinBox::valueChanged,&dialog,schedule);
    connect(segments,&QSpinBox::valueChanged,&dialog,schedule);
    if (rings) connect(rings,&QSpinBox::valueChanged,&dialog,schedule);
    if (taper) connect(taper,&QDoubleSpinBox::valueChanged,&dialog,schedule);
    if (name) connect(name,&QLineEdit::textChanged,&dialog,updateAccept);
    QTimer::singleShot(0,&dialog,refreshPreview);
    dialog.adjustSize();
    const auto right=mapToGlobal(QPoint(width()-dialog.width()-12,135));
    dialog.move(right);
    const bool confirmed=dialog.exec()==QDialog::Accepted && ready && presented==source() && current();
    timer.stop();
    if (!confirmed) {
        refresh(key);
        return;
    }
    try {
        if (creating) {
            auto result=project_->createNativeMesh(name->text().toStdString(),*presented,place->isChecked(),revision);
            if (!result.objectId.empty()) selected_=result.objectId;
            refresh(result.key);
            notify(tr("Created an editable mesh asset."));
        } else {
            project_->editNativeMesh(key,*presented,revision);
            refresh(key);
            notify(tr("Applied mesh parameters to linked instances."));
        }
    } catch (const std::exception& error) {
        refresh(key);
        notify(QString::fromUtf8(error.what()));
    }
}
