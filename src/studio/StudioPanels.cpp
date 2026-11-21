#include "StudioWindow.hpp"
#include "Appearance.hpp"
#include "Viewport.hpp"
#include <QColorDialog>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>

namespace {
QLabel* label(const QString& text, const char* role = "") {
    auto* value = new QLabel(text);
    value->setProperty("role", role);
    return value;
}
QPushButton* button(const QString& text, const char* id = "") {
    auto* control = new QPushButton(text);
    control->setObjectName(id);
    control->setCursor(Qt::PointingHandCursor);
    return control;
}
void section(QVBoxLayout* layout, const QString& text) {
    layout->addSpacing(10);
    layout->addWidget(label(text,"section"));
}
QString validationPath() {
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../projects/PLATZ Validation.platz");
}
}

void StudioWindow::buildHub() {
    hub_ = new QWidget;
    hub_->setObjectName("projectHub");
    auto* outer = new QVBoxLayout(hub_);
    outer->setAlignment(Qt::AlignCenter);
    auto* card = new QFrame;
    card->setObjectName("hubCard");
    card->setMinimumWidth(550);
    card->setMaximumWidth(740);
    auto* body = new QVBoxLayout(card);
    body->setContentsMargins(38,36,38,36);
    body->setSpacing(12);
    auto* mark = new QHBoxLayout;
    auto* icon = new QLabel;
    icon->setPixmap(platzIcon().pixmap(34,34));
    mark->addWidget(icon);
    auto* brand = label("PLATZ");
    brand->setObjectName("brand");
    mark->addWidget(brand);
    mark->addStretch();
    body->addLayout(mark);
    body->addSpacing(17);
    auto* title = label(tr("Projects"));
    title->setStyleSheet("font-size: 34px; font-weight: 650;");
    body->addWidget(title);
    auto* commands = new QHBoxLayout;
    auto* create = button(tr("New project…"),"newProject");
    create->setProperty("accent",true);
    auto* open = button(tr("Open project…"),"openProject");
    commands->addWidget(create);
    commands->addWidget(open);
    commands->addStretch();
    body->addLayout(commands);
    connect(create,&QPushButton::clicked,this,&StudioWindow::newProject);
    connect(open,&QPushButton::clicked,this,&StudioWindow::chooseProject);
    section(body,tr("RECENT PROJECT"));
    recent_ = label({});
    recent_->setWordWrap(true);
    body->addWidget(recent_);
    recentOpen_ = button(tr("Reopen recent project"));
    body->addWidget(recentOpen_,0,Qt::AlignLeft);
    connect(recentOpen_,&QPushButton::clicked,this,[this]{
        openProject(QSettings().value("recentProject").toString());
    });
    section(body,tr("ENGINE VALIDATION"));
    auto* validation = button(tr("Open PLATZ Validation"),"openValidation");
    validation->setToolTip(QDir::toNativeSeparators(validationPath()));
    body->addWidget(validation,0,Qt::AlignLeft);
    connect(validation,&QPushButton::clicked,this,[this]{ openProject(validationPath()); });
    outer->addWidget(card);
    pages_->addWidget(hub_);
}

void StudioWindow::buildStudio() {
    tools_ = new QToolBar(tr("Project controls"));
    tools_->setObjectName("studioToolbar");
    tools_->setMovable(false);
    addToolBar(tools_);
    auto* brand = label("  PLATZ  ");
    brand->setObjectName("brand");
    tools_->addWidget(brand);
    tools_->addSeparator();
    projectTitle_ = label({});
    tools_->addWidget(projectTitle_);
    auto* spring = new QWidget;
    spring->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    tools_->addWidget(spring);
    save_ = button(tr("Save"),"saveButton");
    save_->setToolTip(tr("Save authoring revision (Ctrl+S)"));
    play_ = button(tr("Play"),"playButton");
    play_->setMinimumWidth(86);
    play_->setProperty("accent",true);
    play_->setToolTip(tr("Enter or stop Play (F5)"));
    tools_->addWidget(save_);
    tools_->addWidget(play_);
    connect(save_,&QPushButton::clicked,this,[this]{saveChanges();});
    connect(play_,&QPushButton::clicked,this,&StudioWindow::togglePlay);

    auto* center = new QWidget;
    auto* layout = new QVBoxLayout(center);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(14,6,10,6);
    auto* perspective = label(tr("Perspective"));
    perspective->setObjectName("sceneHeading");
    bar->addWidget(perspective);
    bar->addStretch();
    mode_ = label(tr("Authoring"),"minor");
    bar->addWidget(mode_);
    layout->addLayout(bar);
    viewport_ = new Viewport;
    layout->addWidget(viewport_,1);
    controls_ = label(tr("RMB orbit   ·   MMB pan   ·   Wheel zoom   ·   F frame"),"minor");
    controls_->setContentsMargins(10,6,6,6);
    layout->addWidget(controls_);
    pages_->addWidget(center);
    viewport_->onBackend = [this](QString adapter){backend_->setText(QString("Direct3D 12  ·  %1").arg(adapter));};
    viewport_->onError = [this](QString error){backend_->setText(tr("Renderer unavailable")); notify(error);};
    viewport_->onPick = [this](const std::string& id){ select(id,true); };

    sceneDock_ = new QDockWidget(tr("Scene"),this);
    sceneDock_->setObjectName("sceneDock");
    auto* side = new QWidget;
    auto* left = new QVBoxLayout(side);
    left->setContentsMargins(8,9,8,8);
    count_ = label({},"section");
    count_->setContentsMargins(4,0,0,2);
    left->addWidget(count_);
    tree_ = new QListWidget;
    tree_->setObjectName("sceneTree");
    tree_->setAccessibleName(tr("Scene objects"));
    tree_->setIconSize(QSize(19,19));
    left->addWidget(tree_,1);
    add_ = button(tr("+   Add object"),"addObject");
    auto* choices = new QMenu(add_);
    for (auto mesh : {platz::Mesh::Cube,platz::Mesh::Sphere,platz::Mesh::Cylinder}) {
        choices->addAction(QString::fromUtf8(platz::meshName(mesh)),this,[this,mesh]{
            if (runtime_ || !project_) return;
            protectedCall([&]{selected_=project_->add(mesh); refresh();});
        });
    }
    add_->setMenu(choices);
    left->addWidget(add_);
    sceneDock_->setMinimumWidth(152);
    sceneDock_->setWidget(side);
    addDockWidget(Qt::LeftDockWidgetArea,sceneDock_);
    connect(tree_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem* item){
        if (!refreshing_) select(item ? item->data(Qt::UserRole).toString().toStdString() : std::string{},false);
    });
    buildInspector();
    backend_=label(tr("Awaiting viewport"),"minor");
    backend_->setObjectName("activeBackend");
    statusBar()->addPermanentWidget(backend_);
    notice_=label({});
    notice_->setObjectName("notice");
    notice_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(notice_,1);
}

void StudioWindow::buildInspector() {
    inspectorDock_ = new QDockWidget(tr("Inspector"),this);
    inspectorDock_->setObjectName("inspectorDock");
    inspectorDock_->setMinimumWidth(250);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    inspector_=new QWidget;
    auto* box = new QVBoxLayout(inspector_);
    box->setContentsMargins(14,12,14,12);
    box->setSpacing(7);
    box->addWidget(label(tr("OBJECT"),"section"));
    name_=new QLineEdit;
    name_->setMaxLength(120);
    name_->setAccessibleName(tr("Object name"));
    name_->setObjectName("objectName");
    box->addWidget(name_);
    mesh_=label({},"minor");
    box->addWidget(mesh_);
    connect(name_,&QLineEdit::editingFinished,this,[this]{
        applyEdit([this](platz::Object& o){o.name=name_->text().trimmed().toStdString();});
    });

    section(box,tr("TRANSFORM"));
    const std::array<QString,3> groups{tr("Position"),tr("Rotation"),tr("Scale")};
    const std::array<QString,3> units{"m","°","×"};
    const std::array<QString,3> axes{"X","Y","Z"};
    for (int group=0;group<3;++group) {
        auto* header = label(groups[group] + "  ·  " + units[group],"minor");
        box->addWidget(header);
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        for (int coordinate=0;coordinate<3;++coordinate) {
            auto* field = new QDoubleSpinBox;
            const int index=group*3+coordinate;
            axis_[index]=field;
            field->setObjectName(groups[group].toLower()+axes[coordinate]);
            field->setAccessibleName(groups[group]+" "+axes[coordinate]);
            field->setToolTip(field->accessibleName());
            field->setDecimals(3);
            field->setButtonSymbols(QAbstractSpinBox::NoButtons);
            field->setRange(group==2 ? 0.001 : -100000,100000);
            field->setSingleStep(group==1 ? 5.0 : 0.1);
            field->setKeyboardTracking(false);
            field->setMinimumWidth(56);
            auto* col=new QVBoxLayout;
            col->setSpacing(2);
            col->addWidget(label(axes[coordinate],"section"));
            col->addWidget(field);
            row->addLayout(col);
            connect(field,&QDoubleSpinBox::editingFinished,this,[this,group,coordinate,field]{
                applyEdit([&](platz::Object& o){
                    auto& v = group==0 ? o.position : (group==1 ? o.rotation : o.scale);
                    auto& component = coordinate==0 ? v.x : (coordinate==1 ? v.y : v.z);
                    component=static_cast<float>(field->value());
                });
            });
        }
        box->addLayout(row);
    }
    section(box,tr("MATERIAL"));
    box->addWidget(label(tr("Base color  ·  sRGB"),"minor"));
    auto* material = new QHBoxLayout;
    swatch_=button({},"colorPicker");
    swatch_->setAccessibleName(tr("Choose base color"));
    swatch_->setToolTip(tr("Choose base color"));
    swatch_->setFixedWidth(36);
    color_=new QLineEdit;
    color_->setObjectName("materialColor");
    color_->setAccessibleName(tr("Base color hex"));
    color_->setMaxLength(7);
    material->addWidget(swatch_);
    material->addWidget(color_,1);
    box->addLayout(material);
    auto setColor = [this](QColor value) {
        if (!value.isValid()) { notify(tr("Enter a six-digit hex color, for example #89D6AE.")); syncInspector(); return; }
        applyEdit([value](platz::Object& o){o.color={float(value.redF()),float(value.greenF()),float(value.blueF())};});
    };
    connect(color_,&QLineEdit::editingFinished,this,[this,setColor]{
        if (!color_->isModified()) return;
        const auto text = color_->text();
        setColor(text.size()==7 && text.startsWith('#') ? QColor(text) : QColor());
    });
    connect(swatch_,&QPushButton::clicked,this,[this,setColor]{
        QColor value=QColorDialog::getColor(QColor(color_->text()),this,tr("Base color"),QColorDialog::DontUseNativeDialog);
        if (value.isValid()) setColor(value);
    });
    section(box,tr("SOURCE ID"));
    identity_=label({},"minor");
    identity_->setObjectName("objectIdentity");
    identity_->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
    identity_->setWordWrap(true);
    box->addWidget(identity_);
    box->addStretch();
    scroll->setWidget(inspector_);
    inspectorDock_->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea,inspectorDock_);
}
