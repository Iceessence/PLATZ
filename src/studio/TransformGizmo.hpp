#pragma once

#include "Project.hpp"
#include "renderer.hpp"
#include <QPointF>
#include <QSizeF>
#include <QVector3D>
#include <array>
#include <optional>
#include <vector>

namespace platz::studio {
enum class TransformTool { Select, Move, Rotate, Scale };
struct TransformSettings {
    TransformTool tool=TransformTool::Select;
    bool local{}, snapping{};
    float step=1.f;
    bool boundsPivot{};
};
struct TransformValue {
    Vec3 position, rotation, scale;
    bool operator==(const TransformValue&) const = default;
};
struct GizmoPoint {
    QPointF pixel;
    float depth{}, opacity{1.f};
};
struct GizmoFace {
    std::array<GizmoPoint,3> points;
    float light{};
};
struct GizmoPaths {
    GizmoPoint center;
    bool hasCenter{}, rotating{};
    std::array<std::vector<GizmoPoint>,3> axes, planes;
    std::array<std::vector<GizmoFace>,3> caps;
    std::vector<GizmoPoint> roll, rotationGuide;
};
GizmoPaths gizmoPaths(const engine::Camera& camera,QSizeF extent,const TransformValue& value,
                      const TransformSettings& settings,Vec3 localPivot={});
int hitGizmo(const GizmoPaths& paths,QPointF pixel);
std::vector<engine::OverlayVertex> gizmoOverlay(const GizmoPaths& paths,QSizeF extent,
                                                TransformTool tool,int hovered,int active,double dpr=1.0);
class TransformDrag {
public:
    static std::optional<TransformDrag> start(const engine::Camera& camera,QSizeF extent,
        TransformValue value,TransformSettings settings,int axis,QPointF cursor,Vec3 localPivot={});
    TransformValue move(QPointF cursor,bool snapModifier=false,bool precision=false);
    int axis() const { return axis_; }
    float degrees() const { return degrees_; }
    std::vector<GizmoPoint> rotationGuide() const;
private:
    engine::Camera camera_;
    QSizeF extent_;
    TransformSettings settings_;
    TransformValue original_, last_;
    QVector3D pivot_, localPivot_, direction_, lastPoint_, startRadial_, travel_;
    QPointF lastCursor_;
    float radius_{}, lastParameter_{}, distance_{}, angle_{}, degrees_{};
    int axis_{};
};
}
