#include "StudioOrganization.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSettings>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace {
constexpr qsizetype maxFolders = 1024;
constexpr qsizetype maxMemberships = 65536;
constexpr qsizetype maxStateBytes = 12 * 1024 * 1024;

[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }

QString hash(const QString& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool validId(const QString& value) {
    return !value.isEmpty() && value.size() <= 256 &&
           std::none_of(value.begin(), value.end(), [](QChar c) {
               return c.isSpace() || c.category() == QChar::Other_Control;
           });
}

QString checkedName(const QString& input) {
    auto name = input.trimmed();
    if (name.isEmpty() || name.size() > 120 || std::any_of(name.begin(), name.end(), [](QChar c) {
            return c.category() == QChar::Other_Control || c == '/' || c == '\\';
        }))
        fail("Choose a folder name of 1 to 120 characters without slashes or control characters.");
    return name;
}

void verifyLocation(const QHash<QString, QString>& parents, QString parent, const QString& movingId) {
    // Membership depth is bounded independently of the number of source assets.
    QSet<QString> seen;
    while (!parent.isEmpty()) {
        if (parent == movingId || seen.contains(parent))
            fail("A folder cannot be moved into itself or one of its descendants.");
        const auto next = parents.constFind(parent);
        if (next == parents.cend()) fail("The destination folder no longer exists.");
        seen.insert(parent);
        if (seen.size() >= 64) fail("This layout supports up to 64 nested folder levels.");
        parent = next.value();
    }
}
}

int StudioOrganization::index(Kind kind) {
    switch (kind) {
    case Kind::Scene: return 0;
    case Kind::Meshes: return 1;
    case Kind::Images: return 2;
    }
    fail("Unknown Studio organization scope.");
}

StudioOrganization::StudioOrganization(const QString& projectId) {
    if (!validId(projectId)) fail("A project identity is required for folder organization.");
    projectKey_ = QStringLiteral("organization/v1/") + hash(projectId);
    reload();
}

QString StudioOrganization::key(Kind kind) const {
    static const std::array<QString,3> names{"scene","meshes","images"};
    return projectKey_ + '/' + names[index(kind)];
}

void StudioOrganization::reload() {
    QSettings settings;
    settings.setFallbacksEnabled(false);
    settings.sync();
    for (auto kind : {Kind::Scene, Kind::Meshes, Kind::Images}) {
        const auto stored = settings.value(key(kind)).toByteArray();
        // Invalid profile data must never prevent opening the authored project.
        // A subsequent edit preserves the unreadable bytes alongside their replacement.
        try { scopes_[index(kind)] = decode(stored); }
        catch (const std::runtime_error&) { scopes_[index(kind)] = {}; }
    }
}

const QList<StudioOrganization::Folder>& StudioOrganization::folders(Kind kind) const {
    return scopes_[index(kind)].folders;
}

QString StudioOrganization::folderOf(Kind kind, const QString& itemId) const {
    return scopes_[index(kind)].items.value(itemId);
}

StudioOrganization::Scope StudioOrganization::decode(const QByteArray& data) {
    Scope scope;
    if (data.isEmpty()) return scope;
    if (data.size() > maxStateBytes) fail("Folder preferences exceed the supported size.");
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data,&error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) fail("Invalid folder preferences.");
    const auto root = doc.object();
    if (!root.value("folders").isArray() || !root.value("items").isObject())
        fail("Incomplete folder preferences.");
    const auto folders = root.value("folders").toArray();
    const auto items = root.value("items").toObject();
    if (folders.size() > maxFolders || items.size() > maxMemberships) fail("Folder preferences exceed supported limits.");
    QHash<QString,QString> parents;
    for (const auto& entry : folders) {
        const auto obj = entry.toObject();
        Folder folder{obj.value("id").toString(), obj.value("name").toString(), obj.value("parent").toString()};
        if (!validId(folder.id) || !obj.value("parent").isString() || parents.contains(folder.id))
            fail("Invalid folder identity.");
        folder.name = checkedName(folder.name);
        parents.insert(folder.id,folder.parent);
        scope.folders.append(std::move(folder));
    }
    for (const auto& folder : scope.folders) verifyLocation(parents,folder.parent,folder.id);
    for (auto it = items.begin(); it != items.end(); ++it) {
        auto destination = it.value().toString();
        if (!validId(it.key()) || !parents.contains(destination)) fail("Invalid item folder.");
        scope.items.insert(it.key(), destination);
    }
    return scope;
}

QByteArray StudioOrganization::encode(const Scope& scope) {
    QJsonArray folders;
    for (const auto& folder : scope.folders)
        folders.append(QJsonObject{{"id",folder.id},{"name",folder.name},{"parent",folder.parent}});
    QJsonObject items;
    for (auto it = scope.items.cbegin(); it != scope.items.cend(); ++it)
        items.insert(it.key(),it.value());
    return QJsonDocument(QJsonObject{{"folders",folders},{"items",items}}).toJson(QJsonDocument::Compact);
}

StudioOrganization::Folder& StudioOrganization::requireFolder(Scope& scope, const QString& id) {
    auto found = std::find_if(scope.folders.begin(),scope.folders.end(),[&](const Folder& f){ return f.id == id; });
    if (found == scope.folders.end()) fail("The selected folder no longer exists.");
    return *found;
}

void StudioOrganization::requireParent(const Scope& scope, const QString& parent) {
    if (parent.isEmpty()) return;
    if (std::none_of(scope.folders.cbegin(),scope.folders.cend(),[&](const Folder& f){ return f.id == parent; }))
        fail("The destination folder no longer exists.");
}

void StudioOrganization::change(Kind kind, const std::function<void(Scope&)>& edit) {
    QSettings settings;
    settings.setFallbacksEnabled(false);
    const auto scopeKey = key(kind);
    // QSettings safely publishes individual values. Serialize the whole read/edit/write
    // transaction as well, including across two editors using the same local profile.
    const auto lockKey = hash(settings.fileName() + QChar(0) + scopeKey);
    QLockFile lock(QDir::temp().filePath("platz-folders-" + lockKey + ".lock"));
    if (!lock.tryLock(1000)) fail("Another Studio window is updating these folders. Try again.");
    settings.sync();
    if (settings.status() != QSettings::NoError) fail("Studio cannot read the local folder preferences.");
    const auto old = settings.value(scopeKey).toByteArray();
    Scope next;
    bool recovered = false;
    try { next = decode(old); }
    catch (const std::runtime_error&) { recovered = true; }
    edit(next);
    const auto data = encode(next);
    if (data.size() > maxStateBytes) fail("Folder preferences exceed the supported size.");
    if (recovered) settings.setValue(scopeKey + "-recovery",old);
    settings.setValue(scopeKey,data);
    settings.sync();
    if (settings.status() != QSettings::NoError) fail("Studio could not save the local folder preferences.");
    scopes_[index(kind)] = std::move(next);
}

QString StudioOrganization::createFolder(Kind kind, const QString& name, const QString& parent) {
    const auto label = checkedName(name);
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    change(kind,[&](Scope& scope){
        if (scope.folders.size() >= maxFolders) fail("This view supports up to 1024 folders.");
        requireParent(scope,parent);
        scope.folders.append({id,label,parent});
        QHash<QString,QString> parents;
        for (const auto& f : scope.folders) parents.insert(f.id,f.parent);
        verifyLocation(parents,parent,id);
    });
    return id;
}

void StudioOrganization::renameFolder(Kind kind, const QString& id, const QString& name) {
    const auto label = checkedName(name);
    change(kind,[&](Scope& scope){ requireFolder(scope,id).name = label; });
}

void StudioOrganization::moveFolder(Kind kind, const QString& id, const QString& parent) {
    change(kind,[&](Scope& scope){
        auto& moving = requireFolder(scope,id);
        requireParent(scope,parent);
        moving.parent = parent;
        QHash<QString,QString> parents;
        for (const auto& f : scope.folders) parents.insert(f.id,f.parent);
        // Check descendants too, so moving a deep subtree cannot break the nesting limit.
        for (const auto& f : scope.folders) verifyLocation(parents,f.parent,f.id);
    });
}

void StudioOrganization::moveItem(Kind kind, const QString& itemId, const QString& parent) {
    if (!validId(itemId)) fail("Choose a source item with a stable identity.");
    change(kind,[&](Scope& scope){
        requireParent(scope,parent);
        if (parent.isEmpty()) { scope.items.remove(itemId); return; }
        if (!scope.items.contains(itemId) && scope.items.size() >= maxMemberships)
            fail("This view exceeds the supported number of organized items.");
        scope.items.insert(itemId,parent);
    });
}

void StudioOrganization::removeFolder(Kind kind, const QString& id) { remove(kind,id,nullptr); }
void StudioOrganization::removeFolder(Kind kind, const QString& id, const QSet<QString>& liveItems) {
    remove(kind,id,&liveItems);
}
void StudioOrganization::remove(Kind kind, const QString& id, const QSet<QString>* liveItems) {
    change(kind,[&](Scope& scope){
        requireFolder(scope,id);
        for (const auto& f : scope.folders)
            if (f.parent == id) fail("Move the child folders before removing this folder.");
        for (auto it = scope.items.begin(); it != scope.items.end();) {
            if (it.value() != id) { ++it; continue; }
            if (!liveItems || liveItems->contains(it.key())) fail("Move the items before removing this folder.");
            it = scope.items.erase(it);
        }
        scope.folders.removeIf([&](const Folder& f){ return f.id == id; });
    });
}
