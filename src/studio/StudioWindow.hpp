#pragma once
#include "Project.hpp"
#include "TransformGizmo.hpp"
#include <QMainWindow>
#include <array>
#include <functional>
#include <optional>

class QAction;
class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QColor;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolBar;
class QToolButton;
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
    std::optional<platz::ObjectEdit> preview_;
    QWidget* previewEditor_{};
    bool previewInputValid_{};
    std::function<void()> stopScrubbing_;
    std::string selected_, beforePlay_;
    QStackedWidget* pages_{};
    Viewport* viewport_{};
    QWidget* hub_{};
    QWidget* inspector_{};
    QDockWidget *sceneDock_{}, *inspectorDock_{}, *assetsDock_{};
    QToolBar* tools_{};
    QWidget* transformControls_{};
    QButtonGroup* transformModes_{};
    QComboBox* transformSpace_{};
    QLabel* scaleSpace_{};
    QToolButton* transformSnap_{};
    QDoubleSpinBox* transformStep_{};
    std::array<double,4> snapSteps_{1.0,1.0,15.0,0.1};
    platz::studio::TransformTool transformTool_=platz::studio::TransformTool::Select;
    QListWidget* tree_{};
    QLineEdit *name_{}, *color_{};
    QComboBox* materialSlot_{};
    QCheckBox* useTexture_{};
    QLabel* textureInfo_{};
    QPushButton* resetSlot_{};
    std::string materialOwner_;
    std::array<QDoubleSpinBox*,9> axis_{};
    QLabel *projectTitle_{}, *mode_{}, *count_{}, *mesh_{}, *identity_{}, *backend_{}, *notice_{}, *recent_{}, *controls_{};
    QPushButton *play_{}, *save_{}, *add_{}, *swatch_{}, *recentOpen_{};
    QPushButton *saveGlb_{}, *locateMesh_{};
    QTreeWidget* assetList_{};
    QPushButton *importAsset_{}, *placeAsset_{}, *assignAsset_{}, *manageAsset_{};
    QAction *replaceInstances_{}, *removeAsset_{}, *editMesh_{}, *copyMesh_{}, *detachMesh_{};
    QMenu* createMenu_{};
    std::array<QAction*,3> nativeCreationActions_{};
    QAction *undo_{}, *redo_{}, *saveAction_{}, *restore_{}, *playAction_{}, *homeAction_{};
    QAction *duplicate_{}, *erase_{};
    QAction* repeat_{};
    void repeatObjects();
    void saveEmbeddedGlb();
    void writeGlbSource(std::shared_ptr<const platz::MeshAsset> source,bool native=false);
    void editNativeMesh(const std::string& key,platz::NativeShape shape=platz::NativeShape::Cube);
    void nameMesh(const std::string& key,bool copy,std::string detachObject={});
    QByteArray initialLayout_;
    bool refreshing_{};
    void buildHub();
    void buildStudio();
    void buildInspector();
    void buildAssets();
    void buildTransformControls(QWidget* host);
    void setTransformTool(platz::studio::TransformTool tool);
    void configureManipulator();
    bool beginPreview(const std::string& id);
    void previewTransform(const platz::studio::TransformValue& value);
    bool beginFieldPreview(QWidget* editor);
    void previewField(QWidget* editor, const std::function<void(platz::Object&)>& change);
    void finishFieldPreview(QWidget* editor);
    void finishPreview(bool commit);
    void cancelPreview();
    void previewColor(QWidget* editor, QColor color);
    void syncTransform(const platz::Object& object);
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
    void syncMaterial(const platz::Object* object);
    void applyEdit(const std::function<void(platz::Object&)>& change);
    void finishActiveField();
    void select(std::string id, bool fromViewport);
    void togglePlay();
    void protectedCall(const std::function<void()>& work);
    void notify(const QString& message);
    void resetPanels();
};
