#pragma once
#include "StudioOrganization.hpp"
#include <QHash>
#include <QSet>
#include <QTreeWidget>
#include <functional>

class QLineEdit;
class QMenu;
class QToolButton;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

// Personal project organization. Source IDs in column 0 UserRole always refer to authored content.
// Folder rows carry their own presentation identity in UserRole + 1 and never act as an asset.
class OrganizerTree final : public QTreeWidget {
public:
    using Kind = StudioOrganization::Kind;
    OrganizerTree(Kind kind, std::function<StudioOrganization*()> organization,
                  std::function<bool()> writable, QWidget* parent = nullptr);
    QWidget* makeControls(QWidget* host);
    void beginRefresh();
    QTreeWidgetItem* addSource(const QString& id, const QString& name);
    void endRefresh(const QString& preferred = {}, bool selectFirst = true);
    QTreeWidgetItem* sourceItem(const QString& id) const;
    QString selectedSource() const;
    void revealSource(const QString& id);
    std::function<void()> onOrganizationChanged;
    std::function<void(QMenu&)> appendContextActions;

protected:
    void startDrag(Qt::DropActions supported) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    enum class UseFilter { All, Used, Unused };
    Kind kind_;
    QString prefix_;
    std::function<StudioOrganization*()> organization_;
    std::function<bool()> writable_;
    QHash<QString,QTreeWidgetItem*> folders_,sources_;
    QSet<QString> collapsed_;
    QString lastFolder_,lastSource_,pendingFolder_,dragFolder_,dragSource_,hoverFolder_;
    bool draggingOver_{}, rebuilding_{}, filteredExpansion_{};
    quint64 generation_{},dragGeneration_{};
    QLineEdit* search_{};
    QToolButton* organize_{};
    UseFilter useFilter_{UseFilter::All};
    QAction *newFolder_{}, *renameFolder_{}, *removeFolder_{}, *move_{};
    QMenu* filters_{};
    QAction* allSources_{};
    QTreeWidgetItem* selectedRow() const;
    QString selectedFolder() const;
    QString selectedParent() const;
    bool active() const;
    void applyFilter();
    void updateActions();
    void nameFolder(bool rename);
    void moveToFolder();
    void removeFolder();
    bool relocate(const QString& folder,const QString& source,const QString& destination);
    bool validDrop(QDropEvent* event,QString& destination) const;
    void finishMove();
    void revealItem(QTreeWidgetItem* item);
};
