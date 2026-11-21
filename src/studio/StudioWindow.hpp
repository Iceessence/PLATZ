#pragma once
#include "Project.hpp"
#include <QMainWindow>
#include <array>
#include <functional>
#include <optional>

class QAction;
class QCloseEvent;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QToolBar;
class QTreeWidget;
class Viewport;

class StudioWindow final : public QMainWindow {
public:
    StudioWindow();
    bool openProject(const QString& path);
protected:
    void closeEvent(QCloseEvent*) override;
private:
    std::optional<platz::Project> project_;
    std::optional<platz::Runtime> runtime_;
    std::string selected_, beforePlay_;
    QStackedWidget* pages_{};
    Viewport* viewport_{};
    QWidget* hub_{};
    QWidget* inspector_{};
    QDockWidget *sceneDock_{}, *inspectorDock_{}, *assetsDock_{};
    QToolBar* tools_{};
    QListWidget* tree_{};
    QLineEdit *name_{}, *color_{};
    std::array<QDoubleSpinBox*,9> axis_{};
    QLabel *projectTitle_{}, *mode_{}, *count_{}, *mesh_{}, *identity_{}, *backend_{}, *notice_{}, *recent_{}, *controls_{};
    QPushButton *play_{}, *save_{}, *add_{}, *swatch_{}, *recentOpen_{};
    QPushButton* saveGlb_{};
    QTreeWidget* assetList_{};
    QPushButton *importAsset_{}, *placeAsset_{}, *assignAsset_{}, *manageAsset_{};
    QAction *replaceInstances_{}, *removeAsset_{};
    QAction *undo_{}, *redo_{}, *saveAction_{}, *restore_{}, *playAction_{}, *homeAction_{};
    QAction *duplicate_{}, *erase_{};
    QAction* repeat_{};
    void repeatObjects();
    void saveEmbeddedGlb();
    void writeGlbSource(std::shared_ptr<const platz::MeshAsset> source);
    QByteArray initialLayout_;
    bool refreshing_{};
    void buildHub();
    void buildStudio();
    void buildInspector();
    void buildAssets();
    void refreshAssets(const std::string& preferred = {});
    void updateAssetActions();
    std::string selectedAsset() const;
    void buildMenus();
    bool confirmLeave();
    bool saveChanges();
    void newProject();
    void chooseProject();
    void attach(platz::Project project);
    void toHub();
    void refresh(const std::string& preferredAsset = {});
    void syncInspector();
    void applyEdit(const std::function<void(platz::Object&)>& change);
    void finishActiveField();
    void select(const std::string& id, bool fromViewport);
    void togglePlay();
    void protectedCall(const std::function<void()>& work);
    void notify(const QString& message);
    void resetPanels();
};
