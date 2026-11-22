#include "StudioWindow.hpp"
#include "Viewport.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr std::array studioScales{85,100,115,125,140};
QString commandName(const QAction* action) {
    auto name=action->text();
    name.remove('&');
    return name;
}
QString shortcutText(const QKeySequence& key) {
    return key.toString(QKeySequence::NativeText);
}
}

std::array<QAction*,4> StudioWindow::configurableActions() const {
    return {saveAction_,playAction_,frameAction_,createImageAction_};
}
QString StudioWindow::shortcutConflict(const StudioPreferences& preferences) const {
    const auto configurable=configurableActions();
    for (size_t i=0;i<preferences.shortcuts.size();++i) {
        const auto& sequence=preferences.shortcuts[i];
        if (sequence.isEmpty()) continue;
        const auto chord=sequence[0];
        const auto key=chord.key();
        const auto modifiers=chord.keyboardModifiers();
        const bool frameLetter=i==2 && key>=Qt::Key_A && key<=Qt::Key_Z &&
                               key!=Qt::Key_W && key!=Qt::Key_A && key!=Qt::Key_S && key!=Qt::Key_D;
        const bool control=modifiers==Qt::ControlModifier || modifiers==(Qt::ControlModifier|Qt::ShiftModifier);
        const bool valid=sequence.count()==1 && key!=Qt::Key_unknown && key!=Qt::Key_Escape &&
            (control || (modifiers==Qt::NoModifier && (frameLetter || (key>=Qt::Key_F1 && key<=Qt::Key_F12))));
        if (!valid)
            return tr("Choose Ctrl with a key or F1–F12. Frame can also use a free viewport letter. Escape stays assigned to Stop and Cancel.");
        for (size_t earlier=0;earlier<i;++earlier) {
            if (sequence==preferences.shortcuts[earlier])
                return tr("%1 is already assigned to %2.").arg(shortcutText(sequence),commandName(configurable[earlier]));
        }
        for (const auto* action:findChildren<QAction*>()) {
            if (std::ranges::find(configurable,action)==configurable.end() && action->shortcuts().contains(sequence))
                return tr("%1 is already assigned to %2.").arg(shortcutText(sequence),commandName(action));
        }
        for (const auto* fixed:findChildren<QShortcut*>())
            if (fixed->keys().contains(sequence))
                return tr("%1 is reserved by a Studio control.").arg(shortcutText(sequence));
        for (auto standard:{QKeySequence::Cut,QKeySequence::Copy,QKeySequence::Paste,QKeySequence::SelectAll})
            if (QKeySequence::keyBindings(standard).contains(sequence))
                return tr("%1 is reserved for text editing.").arg(shortcutText(sequence));
    }
    return {};
}

void StudioWindow::loadStudioPreferences() {
    QSettings settings;
    settings.beginGroup("StudioPreferences/v1");
    preferences_.appearance.theme=static_cast<StudioTheme>(std::clamp(settings.value("theme",0).toInt(),0,2));
    preferences_.appearance.accent=static_cast<StudioAccent>(std::clamp(settings.value("accent",0).toInt(),0,3));
    const auto scale=settings.value("scale",100).toInt();
    preferences_.appearance.scale=std::ranges::find(studioScales,scale)!=studioScales.end() ? scale : 100;
    const auto keys=settings.value("shortcuts").toStringList();
    if (keys.size()==int(preferences_.shortcuts.size())) {
        for (size_t i=0;i<preferences_.shortcuts.size();++i)
            preferences_.shortcuts[i]=QKeySequence::fromString(keys[int(i)],QKeySequence::PortableText);
        if (!shortcutConflict(preferences_).isEmpty()) preferences_.shortcuts=StudioPreferences{}.shortcuts;
    }
    applyStudioPreferences();
}
void StudioWindow::applyStudioPreferences() {
    applyStudioAppearance(*qApp,preferences_.appearance);
    const auto actions=configurableActions();
    for (size_t i=0;i<actions.size();++i) actions[i]->setShortcut(preferences_.shortcuts[i]);
    save_->setToolTip(preferences_.shortcuts[0].isEmpty() ? tr("Save authoring revision") :
        tr("Save authoring revision (%1)").arg(shortcutText(preferences_.shortcuts[0])));
    play_->setToolTip(preferences_.shortcuts[1].isEmpty() ? tr("Enter or stop Play") :
        tr("Enter or stop Play (%1)").arg(shortcutText(preferences_.shortcuts[1])));
    syncControlHelp();
}
void StudioWindow::syncControlHelp() {
    const auto frameKey=shortcutText(preferences_.shortcuts[2]);
    auto tools=tr("Right drag to orbit   ·   Middle drag to pan   ·   Wheel to zoom");
    if (!frameKey.isEmpty()) tools+=tr("   ·   %1 frame").arg(frameKey);
    controls_->setText(runtime_ ? tr("Click to turn an object   ·   WASD move   ·   Esc stop") : tools);
    auto accessibility=tr("1 Select, 2 Move, 3 Rotate, 4 Scale. Escape cancels a drag. Arrow keys orbit and W A S D move the camera.");
    if (!frameKey.isEmpty()) accessibility+=tr(" %1 frames the selection.").arg(frameKey);
    viewport_->setAccessibleDescription(accessibility);
}
void StudioWindow::saveStudioPreferences() {
    QSettings settings;
    settings.beginGroup("StudioPreferences/v1");
    settings.setValue("theme",static_cast<int>(preferences_.appearance.theme));
    settings.setValue("accent",static_cast<int>(preferences_.appearance.accent));
    settings.setValue("scale",preferences_.appearance.scale);
    QStringList keys;
    for (const auto& key:preferences_.shortcuts) keys.append(key.toString(QKeySequence::PortableText));
    settings.setValue("shortcuts",keys);
    settings.sync();
    if (settings.status()!=QSettings::NoError) notify(tr("Could not save the Studio preferences."));
}

void StudioWindow::customizeStudio() {
    finishActiveField();
    QDialog dialog(this);
    dialog.setObjectName("customizeStudioDialog");
    dialog.setWindowTitle(tr("Customize Studio"));
    auto* layout=new QVBoxLayout(&dialog);
    auto* pages=new QTabWidget;
    pages->setObjectName("studioPreferencesTabs");
    layout->addWidget(pages);
    auto* appearancePage=new QWidget;
    auto* appearance=new QFormLayout(appearancePage);
    auto* themes=new QComboBox;
    themes->setObjectName("studioTheme");
    themes->addItems({tr("Slate"),tr("Light"),tr("High contrast")});
    appearance->addRow(tr("&Theme"),themes);
    auto* accents=new QComboBox;
    accents->setObjectName("studioAccent");
    accents->addItems({tr("Mint"),tr("Sky"),tr("Amber"),tr("Rose")});
    appearance->addRow(tr("A&ccent"),accents);
    auto* scale=new QComboBox;
    scale->setObjectName("studioUiScale");
    for (int size:studioScales) scale->addItem(tr("%1%").arg(size),size);
    appearance->addRow(tr("&Interface scale"),scale);
    std::array<QCheckBox*,3> panels{};
    const std::array docks{sceneDock_,inspectorDock_,assetsDock_};
    if (project_) {
        auto* panelRow=new QWidget;
        auto* panelLayout=new QVBoxLayout(panelRow);
        panelLayout->setContentsMargins(0,0,0,0);
        for (size_t i=0;i<docks.size();++i) {
            panels[i]=new QCheckBox(docks[i]->windowTitle());
            panels[i]->setChecked(!docks[i]->isHidden());
            panelLayout->addWidget(panels[i]);
        }
        appearance->addRow(tr("Workspace panels"),panelRow);
    }
    pages->addTab(appearancePage,tr("Appearance"));

    auto* shortcutPage=new QWidget;
    auto* shortcutLayout=new QFormLayout(shortcutPage);
    std::array<QKeySequenceEdit*,4> edits{};
    const std::array names{tr("Save project"),tr("Play / Stop"),tr("Frame selection (viewport)"),tr("Create image / texture")};
    const std::array ids{"shortcutSave","shortcutPlay","shortcutFrame","shortcutImage"};
    for (size_t i=0;i<edits.size();++i) {
        edits[i]=new QKeySequenceEdit;
        edits[i]->setObjectName(ids[i]);
        edits[i]->setMaximumSequenceLength(1);
        edits[i]->setClearButtonEnabled(true);
        edits[i]->setAccessibleName(names[i]);
        shortcutLayout->addRow(names[i],edits[i]);
    }
    auto* hint=new QLabel(tr("Choose Ctrl with a key or F1–F12. Frame may use a free viewport letter. Escape always stops Play or cancels an edit."));
    hint->setWordWrap(true);
    shortcutLayout->addRow(hint);
    pages->addTab(shortcutPage,tr("Shortcuts"));

    auto* warning=new QLabel;
    warning->setObjectName("studioShortcutConflict");
    warning->setAccessibleName(tr("Shortcut validation"));
    warning->setProperty("role","warning");
    warning->setWordWrap(true);
    warning->hide();
    layout->addWidget(warning);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults);
    auto* apply=buttons->button(QDialogButtonBox::Ok);
    apply->setObjectName("applyStudioPreferences");
    apply->setText(tr("Apply"));
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);

    auto draft=[&]{
        StudioPreferences result;
        result.appearance={static_cast<StudioTheme>(themes->currentIndex()),static_cast<StudioAccent>(accents->currentIndex()),
                           scale->currentData().toInt()};
        for (size_t i=0;i<edits.size();++i) result.shortcuts[i]=edits[i]->keySequence();
        return result;
    };
    auto setFields=[&](const StudioPreferences& value){
        themes->setCurrentIndex(static_cast<int>(value.appearance.theme));
        accents->setCurrentIndex(static_cast<int>(value.appearance.accent));
        const auto index=scale->findData(value.appearance.scale);
        scale->setCurrentIndex(index<0 ? scale->findData(100) : index);
        for (size_t i=0;i<edits.size();++i) edits[i]->setKeySequence(value.shortcuts[i]);
    };
    auto validate=[&]{
        const auto message=shortcutConflict(draft());
        warning->setText(message);
        warning->setVisible(!message.isEmpty());
        apply->setEnabled(message.isEmpty());
    };
    auto preview=[&]{applyStudioAppearance(*qApp,draft().appearance);};
    setFields(preferences_);
    for (auto* field:{themes,accents,scale}) connect(field,&QComboBox::currentIndexChanged,&dialog,preview);
    for (auto* field:edits) connect(field,&QKeySequenceEdit::keySequenceChanged,&dialog,validate);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&]{setFields({});});
    validate();
    dialog.resize(560,350);
    const auto accepted=dialog.exec()==QDialog::Accepted;
    const auto next=draft();
    if (!accepted || !shortcutConflict(next).isEmpty()) {
        applyStudioAppearance(*qApp,preferences_.appearance);
        return;
    }
    preferences_=next;
    applyStudioPreferences();
    saveStudioPreferences();
    if (project_) {
        for (size_t i=0;i<docks.size();++i) docks[i]->setVisible(panels[i]->isChecked());
        captureWorkspace();
    }
}

void StudioWindow::findCommands() {
    QDialog dialog(this);
    dialog.setObjectName("studioCommandSearch");
    dialog.setWindowTitle(tr("Find commands"));
    auto* layout=new QVBoxLayout(&dialog);
    auto* search=new QLineEdit;
    search->setObjectName("studioCommandFilter");
    search->setAccessibleName(tr("Find available Studio commands"));
    search->setPlaceholderText(tr("Search commands, for example texture, frame or workspace"));
    auto* results=new QListWidget;
    results->setObjectName("studioCommandResults");
    results->setAccessibleName(tr("Matching Studio commands"));
    layout->addWidget(search);
    layout->addWidget(results,1);
    struct Entry { QString section, name; QAction* action; };
    QList<Entry> entries;
    const auto collect=[&](const auto& self,const QList<QAction*>& actions,const QString& section)->void {
        for (auto* action:actions) {
            if (action->isSeparator() || !action->isVisible() || !action->isEnabled() || action==searchCommandsAction_) continue;
            const auto name=commandName(action);
            if (action->menu()) self(self,action->menu()->actions(),section.isEmpty() ? name : section+" › "+name);
            else entries.append({section,name,action});
        }
    };
    collect(collect,menuBar()->actions(),{});
    auto filter=[&]{
        const auto input=search->text().simplified();
        const auto terms=input.split(' ',Qt::SkipEmptyParts);
        results->clear();
        for (int i=0;i<entries.size();++i) {
            const auto& item=entries[i];
            const auto path=item.section+" › "+item.name;
            if (!std::ranges::all_of(terms,[&](const QString& term){return path.contains(term,Qt::CaseInsensitive);})) continue;
            const auto key=shortcutText(item.action->shortcut());
            auto* row=new QListWidgetItem(key.isEmpty() ? path : path+"    "+key,results);
            row->setData(Qt::UserRole,i);
        }
        results->setCurrentRow(results->count() ? 0 : -1);
    };
    auto run=[&]{if (results->currentItem()) dialog.accept();};
    connect(search,&QLineEdit::textChanged,&dialog,filter);
    connect(search,&QLineEdit::returnPressed,&dialog,run);
    connect(results,&QListWidget::itemActivated,&dialog,run);
    filter();
    dialog.resize(560,410);
    search->setFocus();
    if (dialog.exec()!=QDialog::Accepted || !results->currentItem()) return;
    auto* action=entries.at(results->currentItem()->data(Qt::UserRole).toInt()).action;
    if (action->isEnabled()) action->trigger();
}
