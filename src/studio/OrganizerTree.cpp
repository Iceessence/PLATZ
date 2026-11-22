#include "OrganizerTree.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int FolderRole=Qt::UserRole+1;
constexpr auto folderMime="application/x-platz-organizer-item";
QString sourceId(const QTreeWidgetItem* row) {
    return row ? row->data(0,Qt::UserRole).toString() : QString{};
}
QString folderId(const QTreeWidgetItem* row) {
    return row ? row->data(0,FolderRole).toString() : QString{};
}
void warn(QWidget* host,const std::exception& error) {
    QMessageBox::warning(host,QObject::tr("Organize project"),QString::fromUtf8(error.what()));
}
}

OrganizerTree::OrganizerTree(Kind kind,std::function<StudioOrganization*()> organization,
                             std::function<bool()> writable,QWidget* parent)
    : QTreeWidget(parent),kind_(kind),prefix_(kind==Kind::Scene ? "scene" : kind==Kind::Meshes ? "mesh" : "image"),
      organization_(std::move(organization)),writable_(std::move(writable)) {
    setSelectionMode(QAbstractItemView::SingleSelection);
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    setIndentation(16);
    setItemsExpandable(true);
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDropIndicatorShown(false);
    setAccessibleDescription(tr("Drag onto a folder to move an item. Use Organize for keyboard alternatives."));
    connect(this,&QTreeWidget::itemSelectionChanged,this,[this]{updateActions();});
    connect(this,&QTreeWidget::itemCollapsed,this,[this](auto* item) {
        if (!rebuilding_ && !filteredExpansion_) collapsed_.insert(folderId(item));
    });
    connect(this,&QTreeWidget::itemExpanded,this,[this](auto* item) {
        if (!rebuilding_ && !filteredExpansion_) collapsed_.remove(folderId(item));
    });
}

QWidget* OrganizerTree::makeControls(QWidget* host) {
    auto* bar=new QWidget(host);
    auto* row=new QHBoxLayout(bar);
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(4);
    search_=new QLineEdit(bar);
    search_->setObjectName(prefix_+"Search");
    search_->setPlaceholderText(kind_==Kind::Scene ? tr("Search scene…") :
                                kind_==Kind::Meshes ? tr("Search meshes…") : tr("Search images…"));
    search_->setAccessibleName(search_->placeholderText());
    search_->setToolTip(tr("Search names, folders and source details"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(42);
    row->addWidget(search_,1);
    organize_=new QToolButton(bar);
    organize_->setText(tr("Organize"));
    organize_->setObjectName(prefix_+"Organize");
    organize_->setAccessibleName(tr("Organize %1").arg(kind_==Kind::Scene ? tr("scene") :
                                                      kind_==Kind::Meshes ? tr("meshes") : tr("images")));
    organize_->setPopupMode(QToolButton::InstantPopup);
    auto* menu=new QMenu(organize_);
    auto add=[&](const QString& title,const QString& suffix,auto action) {
        auto* result=menu->addAction(title,this,action);
        result->setObjectName(prefix_+suffix);
        return result;
    };
    newFolder_=add(tr("New folder…"),"NewFolder",[this]{nameFolder(false);});
    move_=add(tr("Move to folder…"),"MoveToFolder",[this]{moveToFolder();});
    renameFolder_=add(tr("Rename folder…"),"RenameFolder",[this]{nameFolder(true);});
    removeFolder_=add(tr("Remove empty folder"),"RemoveFolder",[this]{removeFolder();});
    if (kind_!=Kind::Scene) {
        menu->addSeparator();
        filters_=menu->addMenu(tr("Filter by use"));
        auto* group=new QActionGroup(filters_);
        for (auto mode:{UseFilter::All,UseFilter::Used,UseFilter::Unused}) {
            QString label=mode==UseFilter::All ? tr("All sources") :
                          mode==UseFilter::Used ? tr("Used in scene") : tr("Unused");
            auto* a=filters_->addAction(label);
            group->addAction(a);
            a->setCheckable(true);
            if (mode==UseFilter::All) { allSources_=a; a->setChecked(true); }
            connect(a,&QAction::triggered,this,[this,mode]{ useFilter_=mode; applyFilter(); });
        }
    }
    organize_->setMenu(menu);
    row->addWidget(organize_);
    connect(menu,&QMenu::aboutToShow,this,[this]{updateActions();});
    connect(search_,&QLineEdit::textChanged,this,[this]{applyFilter();});
    return bar;
}

bool OrganizerTree::active() const { return organization_() && writable_(); }
QTreeWidgetItem* OrganizerTree::selectedRow() const {
    const auto selected=selectedItems();
    return selected.empty() ? nullptr : selected.front();
}
QString OrganizerTree::selectedFolder() const { return folderId(selectedRow()); }
QString OrganizerTree::selectedSource() const {
    return selectedRow() && !selectedRow()->isHidden() ? sourceId(selectedRow()) : QString{};
}
QString OrganizerTree::selectedParent() const {
    if (const auto folder=selectedFolder(); !folder.isEmpty()) return folder;
    return selectedRow() ? folderId(selectedRow()->parent()) : QString{};
}
QTreeWidgetItem* OrganizerTree::sourceItem(const QString& id) const { return sources_.value(id); }

void OrganizerTree::beginRefresh() {
    ++generation_;
    rebuilding_=true;
    lastFolder_=selectedFolder();
    lastSource_=selectedSource();
    folders_.clear();
    sources_.clear();
    clear();
    auto* store=organization_();
    if (!store) {
        collapsed_.clear();
        return;
    }
    auto groups=store->folders(kind_);
    std::ranges::sort(groups,[](const auto& a,const auto& b) {
        const int order=QString::localeAwareCompare(a.name,b.name);
        return order==0 ? a.id<b.id : order<0;
    });
    const QIcon folderIcon=style()->standardIcon(QStyle::SP_DirIcon);
    for (const auto& group:groups) {
        auto* item=new QTreeWidgetItem;
        item->setText(0,group.name);
        item->setData(0,FolderRole,group.id);
        item->setIcon(0,folderIcon);
        item->setToolTip(0,tr("Organizer folder · %1").arg(group.name));
        item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsDropEnabled);
        folders_.insert(group.id,item);
    }
    for (const auto& group:groups) {
        auto* item=folders_.value(group.id);
        if (auto* parent=folders_.value(group.parent)) parent->addChild(item);
        else addTopLevelItem(item);
    }
}
QTreeWidgetItem* OrganizerTree::addSource(const QString& id,const QString& name) {
    auto* item=new QTreeWidgetItem;
    auto* store=organization_();
    auto* parent=store ? folders_.value(store->folderOf(kind_,id)) : nullptr;
    if (parent) parent->addChild(item);
    else addTopLevelItem(item);
    item->setData(0,Qt::UserRole,id);
    item->setText(0,name);
    item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsDragEnabled);
    sources_.insert(id,item);
    return item;
}
void OrganizerTree::endRefresh(const QString& preferred,bool selectFirst) {
    // Respect canonical Scene index order inside each group. Library rows sort by display name.
    if (kind_!=Kind::Scene) {
        auto order=[](QTreeWidgetItem* a,QTreeWidgetItem* b) {
            const bool folderA=!folderId(a).isEmpty(),folderB=!folderId(b).isEmpty();
            if (folderA!=folderB) return folderA;
            const int compared=QString::localeAwareCompare(a->text(0),b->text(0));
            return compared ? compared<0 : sourceId(a)<sourceId(b);
        };
        auto sortChildren=[&](QTreeWidgetItem* parent) {
            auto children=parent->takeChildren();
            std::stable_sort(children.begin(),children.end(),order);
            parent->addChildren(children);
        };
        for (auto* group:folders_) sortChildren(group);
        sortChildren(invisibleRootItem());
    }
    applyFilter();
    auto* choice=folders_.value(pendingFolder_);
    if (!choice && !preferred.isEmpty()) choice=sources_.value(preferred);
    if (!choice) choice=folders_.value(lastFolder_);
    if (!choice) choice=sources_.value(lastSource_);
    if (choice && choice->isHidden()) choice=nullptr;
    if (!choice && selectFirst) {
        for (QTreeWidgetItemIterator it(this,QTreeWidgetItemIterator::NotHidden);*it;++it) {
            if (!sourceId(*it).isEmpty()) { choice=*it; break; }
        }
    }
    if (choice) setCurrentItem(choice);
    if (!pendingFolder_.isEmpty() && choice) revealItem(choice);
    pendingFolder_.clear();
    rebuilding_=false;
    updateActions();
}
void OrganizerTree::revealItem(QTreeWidgetItem* item) {
    for (auto* p=item->parent();p;p=p->parent()) p->setExpanded(true);
    setCurrentItem(item);
    scrollToItem(item);
}
void OrganizerTree::revealSource(const QString& id) {
    auto* item=sources_.value(id);
    if (!item) return;
    if (item->isHidden()) {
        useFilter_=UseFilter::All;
        if (allSources_) allSources_->setChecked(true);
        if (search_) search_->clear();
        applyFilter();
    }
    revealItem(item);
}
void OrganizerTree::applyFilter() {
    auto* previousSelection=selectedRow();
    const QString query=search_ ? search_->text().trimmed() : QString{};
    filteredExpansion_=!query.isEmpty() || useFilter_!=UseFilter::All;
    const auto visit=[&](auto&& self,QTreeWidgetItem* item,bool matchingParent)->bool {
        bool nameMatches=matchingParent || query.isEmpty() || item->text(0).contains(query,Qt::CaseInsensitive);
        if (!folderId(item).isEmpty()) {
            bool childVisible=false;
            for (int n=0;n<item->childCount();++n) childVisible=self(self,item->child(n),nameMatches) || childVisible;
            bool visible=childVisible || (nameMatches && useFilter_==UseFilter::All);
            item->setHidden(!visible);
            item->setExpanded(filteredExpansion_ ? childVisible : !collapsed_.contains(folderId(item)));
            return visible;
        }
        const int uses=item->data(1,Qt::DisplayRole).toInt();
        const bool matchesUse=useFilter_==UseFilter::All || (useFilter_==UseFilter::Used ? uses>0 : uses==0);
        const bool visible=matchesUse && (nameMatches || item->toolTip(0).contains(query,Qt::CaseInsensitive)
                                           || sourceId(item).contains(query,Qt::CaseInsensitive));
        item->setHidden(!visible);
        return visible;
    };
    for (int n=0;n<topLevelItemCount();++n) visit(visit,topLevelItem(n),false);
    if (previousSelection && previousSelection->isHidden()) { clearSelection(); setCurrentItem(nullptr); }
    updateActions();
}
void OrganizerTree::updateActions() {
    if (!organize_) return;
    const bool canEdit=active();
    const bool folder=!selectedFolder().isEmpty();
    organize_->setEnabled(organization_()!=nullptr);
    newFolder_->setEnabled(canEdit);
    renameFolder_->setEnabled(canEdit && folder);
    move_->setEnabled(canEdit && selectedRow() && (!folders_.isEmpty() || selectedRow()->parent()));
    removeFolder_->setEnabled(canEdit && folder && !selectedRow()->childCount());
}

void OrganizerTree::nameFolder(bool rename) {
    if (!active() || (rename && selectedFolder().isEmpty())) return;
    const auto folder=selectedFolder(),parent=selectedParent();
    const auto generation=generation_;
    bool accepted=false;
    const QString name=QInputDialog::getText(this,rename ? tr("Rename folder") : tr("New folder"),
        tr("Folder name"),QLineEdit::Normal,rename ? selectedRow()->text(0) : QString{},&accepted);
    if (!accepted || generation!=generation_ || !active()) return;
    try {
        if (rename) {
            organization_()->renameFolder(kind_,folder,name);
            pendingFolder_=folder;
        } else {
            pendingFolder_=organization_()->createFolder(kind_,name,parent);
        }
        if (search_) search_->clear();
        if (allSources_) allSources_->setChecked(true);
        useFilter_=UseFilter::All;
        if (onOrganizationChanged) onOrganizationChanged();
    } catch (const std::exception& error) { warn(this,error); }
}

void OrganizerTree::removeFolder() {
    const auto id=selectedFolder();
    if (!active() || id.isEmpty() || selectedRow()->childCount()) return;
    try {
        QSet<QString> live(sources_.keyBegin(),sources_.keyEnd());
        pendingFolder_=folderId(selectedRow()->parent());
        organization_()->removeFolder(kind_,id,live);
        if (onOrganizationChanged) onOrganizationChanged();
    } catch (const std::exception& error) { pendingFolder_.clear(); warn(this,error); }
}
bool OrganizerTree::relocate(const QString& folder,const QString& source,const QString& destination) {
    if (!active() || (folder.isEmpty() && source.isEmpty())) return false;
    try {
        if (!folder.isEmpty()) {
            organization_()->moveFolder(kind_,folder,destination);
            pendingFolder_=folder;
        } else organization_()->moveItem(kind_,source,destination);
        if (onOrganizationChanged) onOrganizationChanged();
        if (!source.isEmpty()) revealSource(source);
        return true;
    } catch (const std::exception& error) { pendingFolder_.clear(); warn(this,error); return false; }
}
void OrganizerTree::moveToFolder() {
    const auto folder=selectedFolder(),source=selectedSource();
    if (!active() || (folder.isEmpty() && source.isEmpty())) return;
    const auto generation=generation_;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Move %1").arg(selectedRow()->text(0)));
    dialog.setMinimumWidth(290);
    auto* layout=new QVBoxLayout(&dialog);
    auto* tree=new QTreeWidget(&dialog);
    tree->setObjectName("folderDestination");
    tree->setAccessibleName(tr("Destination folder"));
    tree->setHeaderHidden(true);
    auto* root=new QTreeWidgetItem(tree,QStringList{kind_==Kind::Scene ? tr("Scene root") : tr("Library root")});
    root->setIcon(0,style()->standardIcon(QStyle::SP_DirIcon));
    QHash<QString,QTreeWidgetItem*> destinations{{QString{},root}};
    const auto copy=[&](auto&& self,QTreeWidgetItem* from,QTreeWidgetItem* to)->void {
        for (int i=0;i<from->childCount();++i) {
            auto* child=from->child(i);
            const auto id=folderId(child);
            if (id.isEmpty() || id==folder) continue;
            auto* entry=new QTreeWidgetItem(to,{child->text(0)});
            entry->setData(0,Qt::UserRole,id);
            entry->setIcon(0,child->icon(0));
            destinations.insert(id,entry);
            self(self,child,entry);
        }
    };
    copy(copy,invisibleRootItem(),root);
    tree->expandAll();
    const auto previous=folderId(selectedRow()->parent());
    tree->setCurrentItem(destinations.value(previous,root));
    layout->addWidget(tree);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Move"));
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(tree,&QTreeWidget::itemDoubleClicked,&dialog,&QDialog::accept);
    layout->addWidget(buttons);
    if (dialog.exec()!=QDialog::Accepted || !tree->currentItem() || generation_!=generation) return;
    const auto dest=sourceId(tree->currentItem());
    if (dest!=previous) relocate(folder,source,dest);
}
void OrganizerTree::contextMenuEvent(QContextMenuEvent* event) {
    if (event->reason()!=QContextMenuEvent::Keyboard) {
        auto* row=itemAt(viewport()->mapFromGlobal(event->globalPos()));
        if (row) setCurrentItem(row,0,QItemSelectionModel::ClearAndSelect);
        else { clearSelection(); setCurrentItem(nullptr); }
    }
    updateActions();
    QMenu menu(this);
    if (!selectedSource().isEmpty() && appendContextActions) appendContextActions(menu);
    if (!menu.isEmpty()) menu.addSeparator();
    menu.addAction(newFolder_);
    if (selectedRow()) menu.addAction(move_);
    if (!selectedFolder().isEmpty()) {
        menu.addAction(renameFolder_);
        menu.addAction(removeFolder_);
    }
    auto pos=event->globalPos();
    if (event->reason()==QContextMenuEvent::Keyboard && selectedRow())
        pos=viewport()->mapToGlobal(visualItemRect(selectedRow()).center());
    menu.exec(pos);
}
void OrganizerTree::startDrag(Qt::DropActions) {
    if (!active() || !selectedRow()) return;
    dragFolder_=selectedFolder();
    dragSource_=selectedSource();
    if (dragFolder_.isEmpty() && dragSource_.isEmpty()) return;
    dragGeneration_=generation_;
    QDrag drag(this);
    auto* mime=new QMimeData;
    mime->setData(folderMime,prefix_.toUtf8());
    drag.setMimeData(mime);
    const auto icon=selectedRow()->icon(0);
    if (!icon.isNull()) drag.setPixmap(icon.pixmap(24,24));
#ifdef _WIN32
    const bool hardwareDrag=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0 || (GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
#else
    const bool hardwareDrag=true;
#endif
    if (hardwareDrag) {
        drag.exec(Qt::MoveAction);
    } else {
        struct SyntheticDragFilter final : QObject {
            OrganizerTree* tree;
            QMimeData* mime;
            QEventLoop* loop;
            bool entered{};
            bool eventFilter(QObject* watched,QEvent* event) override {
                if (event->type()==QEvent::KeyPress &&
                    static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
                    QDragLeaveEvent leave;
                    tree->dragLeaveEvent(&leave);
                    loop->quit();
                    return true;
                }
                if (watched!=tree->viewport()) return false;
                if (event->type()==QEvent::MouseMove) {
                    const auto* mouse=static_cast<QMouseEvent*>(event);
                    if (!entered) {
                        QDragEnterEvent enter(mouse->position().toPoint(),Qt::MoveAction,mime,
                                              mouse->buttons(),mouse->modifiers());
                        tree->dragEnterEvent(&enter);
                        entered=true;
                    }
                    QDragMoveEvent move(mouse->position().toPoint(),Qt::MoveAction,mime,
                                        mouse->buttons(),mouse->modifiers());
                    tree->dragMoveEvent(&move);
                    return true;
                }
                if (event->type()==QEvent::MouseButtonRelease) {
                    const auto* mouse=static_cast<QMouseEvent*>(event);
                    if (!entered) {
                        QDragEnterEvent enter(mouse->position().toPoint(),Qt::MoveAction,mime,
                                              mouse->buttons(),mouse->modifiers());
                        tree->dragEnterEvent(&enter);
                        entered=true;
                    }
                    QDragMoveEvent move(mouse->position().toPoint(),Qt::MoveAction,mime,
                                        mouse->buttons(),mouse->modifiers());
                    tree->dragMoveEvent(&move);
                    QDropEvent drop(mouse->position(),Qt::MoveAction,mime,
                                    mouse->buttons(),mouse->modifiers());
                    tree->dropEvent(&drop);
                    loop->quit();
                    return false;
                }
                return false;
            }
        };
        QEventLoop loop;
        SyntheticDragFilter filter;
        filter.tree=this;
        filter.mime=mime;
        filter.loop=&loop;
        qApp->installEventFilter(&filter);
        loop.exec();
        qApp->removeEventFilter(&filter);
    }
    dragFolder_.clear();
    dragSource_.clear();
    finishMove();
}
bool OrganizerTree::validDrop(QDropEvent* event,QString& destination) const {
    if ((event->source()!=this && event->source()!=nullptr) || !event->mimeData()->hasFormat(folderMime) ||
        event->mimeData()->data(folderMime)!=prefix_.toUtf8() || !active() ||
        dragGeneration_!=generation_ || (dragFolder_.isEmpty() && dragSource_.isEmpty())) return false;
    auto* target=itemAt(event->position().toPoint());
    destination=folderId(target);
    if (target && destination.isEmpty()) return false;
    for (auto* p=target;p;p=p->parent())
        if (!dragFolder_.isEmpty() && folderId(p)==dragFolder_) return false;
    auto* moved=dragFolder_.isEmpty() ? sources_.value(dragSource_) : folders_.value(dragFolder_);
    return moved && folderId(moved->parent())!=destination;
}
void OrganizerTree::dragEnterEvent(QDragEnterEvent* event) {
    if ((event->source()==this || event->source()==nullptr) && event->mimeData()->hasFormat(folderMime) &&
        event->mimeData()->data(folderMime)==prefix_.toUtf8() && active()) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}
void OrganizerTree::dragMoveEvent(QDragMoveEvent* event) {
    QString target;
    draggingOver_=validDrop(event,target);
    hoverFolder_=target;
    if (draggingOver_) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
    viewport()->update();
}
void OrganizerTree::finishMove() { draggingOver_=false; hoverFolder_.clear(); viewport()->update(); }
void OrganizerTree::dragLeaveEvent(QDragLeaveEvent* event) { finishMove(); event->accept(); }
void OrganizerTree::dropEvent(QDropEvent* event) {
    QString target;
    bool accepted=validDrop(event,target) && relocate(dragFolder_,dragSource_,target);
    if (accepted) {event->setDropAction(Qt::MoveAction);event->accept();}
    else event->ignore();
    finishMove();
}
void OrganizerTree::paintEvent(QPaintEvent* event) {
    QTreeWidget::paintEvent(event);
    if (!draggingOver_) return;
    QPainter painter(viewport());
    painter.setPen(QPen(palette().color(QPalette::WindowText),2));
    auto* folder=folders_.value(hoverFolder_);
    const auto rect=folder ? visualItemRect(folder) : viewport()->rect();
    painter.drawRect(rect.adjusted(1,1,-2,-2));
}
