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
};
struct TransformValue {
    Vec3 position, rotation, scale;
    bool operator==(const TransformValue&) const = default;
};
struct GizmoPaths {
    QPointF center;
    std::array<std::vector<QPointF>,3> axes;
};
GizmoPaths gizmoPaths(const engine::Camera& camera,QSizeF extent,const TransformValue& value,
                      const TransformSettings& settings);
int hitGizmo(const GizmoPaths& paths,QPointF pixel);
std::vector<engine::OverlayVertex> gizmoOverlay(const GizmoPaths& paths,QSizeF extent,
                                                TransformTool tool,int hovered,int active);
class TransformDrag {
public:
    static std::optional<TransformDrag> start(const engine::Camera& camera,QSizeF extent,
        TransformValue value,TransformSettings settings,int axis,QPointF cursor);
    TransformValue move(QPointF cursor);
    int axis() const { return axis_; }
private:
    engine::Camera camera_;
    QSizeF extent_;
    TransformSettings settings_;
    TransformValue original_, last_;
    QVector3D direction_, lastRadial_;
    float radius_{}, initialParameter_{}, angle_{};
    int axis_{};
};
}
