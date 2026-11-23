#include "StudioWindow.hpp"
#include "Viewport.hpp"
#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

using platz::studio::TransformTool;

void StudioWindow::buildTransformControls(QWidget* host) {
    auto* tools=new QHBoxLayout(host);
    tools->setContentsMargins(8,0,8,5);
    tools->setSpacing(5);
    transformModes_=new QButtonGroup(host);
    const std::array<QString,4> names{tr("Select"),tr("Move"),tr("Rotate"),tr("Scale")};
    const std::array<const char*,4> ids{"toolSelect","toolMove","toolRotate","toolScale"};
    for (int i=0;i<4;++i) {
        auto* tool=new QToolButton(host);
        tool->setText(names[i]);
        tool->setObjectName(ids[i]);
        tool->setCheckable(true);
        tool->setChecked(i==0);
        tool->setToolTip(tr("%1 (%2 with the viewport focused)").arg(names[i]).arg(i+1));
        transformModes_->addButton(tool,i);
        tools->addWidget(tool);
    }
    connect(transformModes_,&QButtonGroup::idClicked,this,[this](int id){
        setTransformTool(static_cast<TransformTool>(id));
    });
    transformSpace_=new QComboBox(host);
    transformSpace_->setObjectName("transformSpace");
    transformSpace_->setAccessibleName(tr("Transform axes"));
    transformSpace_->addItems({tr("World"),tr("Local")});
    transformSpace_->setToolTip(tr("Move and Rotate can use world or object axes. Scale uses object axes."));
    tools->addSpacing(4);
    tools->addWidget(transformSpace_);
    connect(transformSpace_,&QComboBox::currentIndexChanged,this,[this](int index){
        localTransformAxes_=index==1;
        configureManipulator();
    });
    transformPivot_=new QComboBox(host);
    transformPivot_->setObjectName("transformPivot");
    transformPivot_->setAccessibleName(tr("Transform pivot"));
    transformPivot_->addItems({tr("Object pivot"),tr("Bounds center")});
    transformPivot_->setToolTip(tr("Transform around the object's origin or the center of its mesh bounds."));
    tools->addWidget(transformPivot_);
    connect(transformPivot_,&QComboBox::currentIndexChanged,this,[this]{configureManipulator();});
    transformSnap_=new QToolButton(host);
    transformSnap_->setObjectName("transformSnap");
    transformSnap_->setText(tr("Snap"));
    transformSnap_->setCheckable(true);
    transformSnap_->setToolTip(tr("Snap the active transform to the configured step."));
    tools->addWidget(transformSnap_);
    connect(transformSnap_,&QToolButton::toggled,this,[this]{configureManipulator();});
    transformStep_=new QDoubleSpinBox(host);
    transformStep_->setObjectName("transformStep");
    transformStep_->setAccessibleName(tr("Snap increment"));
    transformStep_->setDecimals(3);
    transformStep_->setRange(.001,1000);
    transformStep_->setKeyboardTracking(false);
    transformStep_->setToolTip(tr("Step size for the current transform tool."));
    tools->addWidget(transformStep_);
    tools->addStretch();
    connect(transformStep_,&QDoubleSpinBox::valueChanged,this,[this](double step){
        snapSteps_[static_cast<unsigned>(transformTool_)]=step;
        configureManipulator();
    });
}

void StudioWindow::setTransformTool(TransformTool tool) {
    transformTool_=tool;
    transformModes_->button(static_cast<int>(tool))->setChecked(true);
    configureManipulator();
}
void StudioWindow::configureManipulator() {
    const auto mode=static_cast<unsigned>(transformTool_);
    const bool manipulating=transformTool_!=TransformTool::Select;
    const bool scaling=transformTool_==TransformTool::Scale;
    const QSignalBlocker spaceSignals(transformSpace_);
    transformSpace_->setCurrentIndex(scaling || localTransformAxes_ ? 1 : 0);
    transformSpace_->setEnabled(manipulating && !scaling);
    transformPivot_->setEnabled(manipulating);
    transformSnap_->setEnabled(manipulating);
    transformStep_->setEnabled(manipulating && transformSnap_->isChecked());
    const std::array<QString,4> units{"",tr(" m"),"°","×"};
    const QSignalBlocker stepSignals(transformStep_);
    transformStep_->setSuffix(units[mode]);
    transformStep_->setValue(snapSteps_[mode]);
    if (viewport_) viewport_->setTransformSettings({transformTool_,transformSpace_->currentIndex()==1,
                                                     transformSnap_->isChecked(),float(snapSteps_[mode]),
                                                     transformPivot_->currentIndex()==1});
}

bool StudioWindow::beginPreview(const std::string& id) {
    if (refreshing_ || !project_ || runtime_ || imageEditor_ || preview_ || id!=selected_) return false;
    if (const auto* object=project_->find(id)) {
        protectedCall([&]{preview_=project_->stage(*object,project_->revision());});
        if (preview_) {
            transformInspectorClock_.invalidate();
            mode_->setText(tr("Preview  ·  Escape to cancel"));
        }
        return preview_.has_value();
    }
    return false;
}
bool StudioWindow::beginFieldPreview(QWidget* editor) {
    if (refreshing_) return false;
    if (preview_) return previewEditor_==editor;
    if (!beginPreview(selected_)) return false;
    previewEditor_=editor;
    previewInputValid_=false;
    return true;
}
void StudioWindow::previewField(QWidget* editor,const std::function<void(platz::Object&)>& change) {
    if (!beginFieldPreview(editor)) return;
    if (preview_->expectedRevision!=project_->revision() || preview_->before.id!=selected_) {
        finishPreview(false);
        notify(tr("Selection or revision changed. The preview was cancelled."));
        return;
    }
    auto& edit=*preview_;
    edit.after=edit.before;
    previewInputValid_=static_cast<bool>(change);
    if (change) change(edit.after);
    viewport_->previewObject(edit.after);
    syncSaveState();
}
void StudioWindow::finishFieldPreview(QWidget* editor) {
    if (refreshing_ || previewEditor_!=editor) return;
    const bool valid=previewInputValid_;
    finishPreview(valid);
    if (!valid) notify(tr("Incomplete or invalid input. The previous value was restored."));
}
void StudioWindow::previewColor(QWidget* editor,QColor color) {
    if (!color.isValid()) {
        previewField(editor,{});
        if (previewEditor_==editor) swatch_->setStyleSheet({});
        return;
    }
    const auto slot=materialSlot_->currentIndex();
    const std::array rgb{float(color.redF()),float(color.greenF()),float(color.blueF())};
    previewField(editor,[slot,rgb,color](platz::Object& object){
        if (object.asset.data && slot>=0) {
            const auto source=object.asset.data->colorForSlot(
                slot,{object.color.x,object.color.y,object.color.z},object.slotColors);
            if (QColor::fromRgbF(source[0],source[1],source[2]).name()!=color.name())
                object.setSlotColor(slot,rgb);
        } else if (QColor::fromRgbF(object.color.x,object.color.y,object.color.z).name()!=color.name()) {
            object.color={rgb[0],rgb[1],rgb[2]};
        }
    });
    if (previewEditor_==editor)
        swatch_->setStyleSheet(QString("background-color: %1").arg(color.name()));
}
void StudioWindow::previewTransform(const platz::studio::TransformValue& value) {
    if (!preview_ || !project_) return;
    if (preview_->expectedRevision!=project_->revision() || preview_->before.id!=selected_) {
        viewport_->cancelDrag();
        notify(tr("Selection or revision changed. The transform was cancelled."));
        return;
    }
    auto& object=preview_->after;
    object.position=value.position;
    object.rotation=value.rotation;
    object.scale=value.scale;
    viewport_->previewObject(object,false);
    // The GPU preview follows each paint. The separate Qt backing store needs fewer numeric repaints.
    if (!transformInspectorClock_.isValid() || transformInspectorClock_.hasExpired(49)) {
        syncTransform(object,true);
        const auto& original=preview_->before;
        if (transformTool_==TransformTool::Rotate) {
            controls_->setText(tr("Rotation %1°  ·  Ctrl toggles snap  ·  Shift precision  ·  Right-click to cancel")
                                  .arg(viewport_->dragAngle(),0,'f',1));
        } else {
            const bool scale=transformTool_==TransformTool::Scale;
            const auto x=scale ? value.scale.x/original.scale.x : value.position.x-original.position.x;
            const auto y=scale ? value.scale.y/original.scale.y : value.position.y-original.position.y;
            const auto z=scale ? value.scale.z/original.scale.z : value.position.z-original.position.z;
            controls_->setText((scale ? tr("Scale  X %1×  Y %2×  Z %3×") : tr("Move  X %1 m  Y %2 m  Z %3 m"))
                                  .arg(x,0,'f',2).arg(y,0,'f',2).arg(z,0,'f',2));
        }
        transformInspectorClock_.start();
    }
}
void StudioWindow::finishPreview(bool commit) {
    if (!preview_) return;
    auto edit=std::move(*preview_);
    preview_.reset();
    previewEditor_=nullptr;
    previewInputValid_=false;
    if (commit && edit.before!=edit.after && project_ && !runtime_) {
        try {
            project_->apply(edit);
            refresh();
            notify(tr("Edit applied."));
            return;
        } catch (const std::exception& error) {
            refresh();
            notify(QString::fromUtf8(error.what()));
            return;
        }
    }
    refresh();
}
void StudioWindow::cancelPreview() {
    viewport_->cancelDrag();
    finishPreview(false);
}
