#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <array>
#include <functional>

// Presentation state for one project identity in the current local Studio profile.
// The root of each independent namespace is represented by an empty parent ID.
class StudioOrganization final {
public:
    enum class Kind { Scene, Meshes, Images };
    struct Folder {
        QString id;
        QString name;
        QString parent;
    };

    explicit StudioOrganization(const QString& projectId);
    void reload();
    const QList<Folder>& folders(Kind kind) const;
    QString folderOf(Kind kind, const QString& itemId) const;
    QString createFolder(Kind kind, const QString& name, const QString& parent = {});
    void renameFolder(Kind kind, const QString& id, const QString& name);
    void moveFolder(Kind kind, const QString& id, const QString& parent);
    void moveItem(Kind kind, const QString& itemId, const QString& parent);
    void removeFolder(Kind kind, const QString& id);
    // The live set lets callers remove an otherwise empty folder after a source
    // was deleted. Undoing that deletion then presents the restored item at root.
    void removeFolder(Kind kind, const QString& id, const QSet<QString>& liveItems);

private:
    struct Scope {
        QList<Folder> folders;
        QHash<QString, QString> items;
    };
    QString projectKey_;
    std::array<Scope, 3> scopes_;

    static int index(Kind kind);
    QString key(Kind kind) const;
    static Scope decode(const QByteArray& data);
    static QByteArray encode(const Scope& scope);
    static Folder& requireFolder(Scope& scope, const QString& id);
    static void requireParent(const Scope& scope, const QString& parent);
    void change(Kind kind, const std::function<void(Scope&)>& edit);
    void remove(Kind kind, const QString& id, const QSet<QString>* liveItems);
};
