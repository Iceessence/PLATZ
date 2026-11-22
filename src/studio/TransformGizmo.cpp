#include "TransformGizmo.hpp"
#include <QQuaternion>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace platz::studio {
namespace {
constexpr float handleLength=76.f;
constexpr float toRadians=std::numbers::pi_v<float>/180.f;
constexpr std::array<engine::Vec3,3> palette{
    engine::Vec3{.98f,.40f,.47f},engine::Vec3{.43f,.88f,.59f},engine::Vec3{.44f,.70f,1.f}};
constexpr engine::Vec3 edge{.06f,.09f,.11f};

QVector3D vector(Vec3 v) { return {v.x,v.y,v.z}; }
QVector3D vector(engine::Vec3 v) { return {v[0],v[1],v[2]}; }
Vec3 value(QVector3D v) { return {v.x(),v.y(),v.z()}; }

struct Projection {
    QSizeF size;
    QVector3D eye, forward, right, up;
    float focal, nearPlane, farPlane;
    Projection(const engine::Camera& camera,QSizeF extent)
        : size(extent), focal(float(extent.height()*.5)/std::tan(engine::perspectiveFovDegrees*.5f*toRadians)),
          nearPlane(camera.nearPlane()), farPlane(camera.farPlane()) {
        const float cp=std::cos(camera.pitch);
        forward=-QVector3D(std::sin(camera.yaw)*cp,std::sin(camera.pitch),std::cos(camera.yaw)*cp);
        eye=vector(camera.target)-forward*camera.distance;
        right=QVector3D::crossProduct(forward,{0,1,0}).normalized();
        up=QVector3D::crossProduct(right,forward);
    }
    float depth(QVector3D point) const { return QVector3D::dotProduct(point-eye,forward); }
    std::optional<QPointF> project(QVector3D point) const {
        const auto dist=depth(point);
        if (dist<=nearPlane || dist>=farPlane) return std::nullopt;
        const auto offset=point-eye;
        const float factor=focal/dist;
        return QPointF(size.width()*.5+QVector3D::dotProduct(offset,right)*factor,
                       size.height()*.5-QVector3D::dotProduct(offset,up)*factor);
    }
    QVector3D ray(QPointF pixel) const {
        return (forward+right*float((pixel.x()-size.width()*.5)/focal)-
                up*float((pixel.y()-size.height()*.5)/focal)).normalized();
    }
};

QQuaternion rotation(Vec3 angles) {
    return QQuaternion::fromAxisAndAngle(0,0,1,angles.z)*
           QQuaternion::fromAxisAndAngle(0,1,0,angles.y)*
           QQuaternion::fromAxisAndAngle(1,0,0,angles.x);
}
Vec3 euler(const QQuaternion& quaternion) {
    const auto basis=quaternion.normalized().toRotationMatrix();
    const auto sine=std::clamp(-basis(2,0),-1.f,1.f);
    const auto y=std::asin(sine);
    if (std::abs(sine)>.999999f)
        return {std::atan2(sine*basis(0,1),basis(1,1))/toRadians,y/toRadians,0};
    return {std::atan2(basis(2,1),basis(2,2))/toRadians,y/toRadians,
            std::atan2(basis(1,0),basis(0,0))/toRadians};
}
QVector3D direction(const TransformValue& object,const TransformSettings& settings,int axis) {
    QVector3D normal;
    normal[axis]=1.f;
    return settings.local || settings.tool==TransformTool::Scale ? rotation(object.rotation).rotatedVector(normal) :
                                                                  normal;
}
std::optional<float> axisParameter(const Projection& projection,QVector3D origin,QVector3D axis,QPointF pixel) {
    const auto ray=projection.ray(pixel);
    const auto offset=origin-projection.eye;
    const float alignment=QVector3D::dotProduct(axis,ray);
    const float denominator=1.f-alignment*alignment;
    if (denominator<.005f) return {};
    return (alignment*QVector3D::dotProduct(ray,offset)-QVector3D::dotProduct(axis,offset))/denominator;
}
std::optional<QVector3D> radial(const Projection& projection,QVector3D origin,QVector3D axis,QPointF pixel) {
    const auto ray=projection.ray(pixel);
    const float alignment=QVector3D::dotProduct(ray,axis);
    if (std::abs(alignment)<.025f) return {};
    const float depth=QVector3D::dotProduct(origin-projection.eye,axis)/alignment;
    const auto offset=projection.eye+ray*depth-origin;
    if (depth<=0 || offset.lengthSquared()<.000000001f) return {};
    return offset.normalized();
}
double length(QPointF point) { return std::hypot(point.x(),point.y()); }
float snap(float n,const TransformSettings& settings) {
    return settings.snapping && settings.step>0 ? std::round(n/settings.step)*settings.step : n;
}
}

GizmoPaths gizmoPaths(const engine::Camera& camera,QSizeF extent,const TransformValue& object,
                      const TransformSettings& settings) {
    GizmoPaths paths;
    if (settings.tool==TransformTool::Select || extent.isEmpty()) return paths;
    const Projection projection(camera,extent);
    const auto pivot=vector(object.position);
    const auto center=projection.project(pivot);
    if (!center) return paths;
    paths.center=*center;
    const float radius=handleLength*projection.depth(pivot)/projection.focal;
    for (int axis=0;axis<3;++axis) {
        const auto normal=direction(object,settings,axis);
        auto& line=paths.axes[axis];
        if (settings.tool==TransformTool::Rotate) {
            if (std::abs(QVector3D::dotProduct(normal,projection.ray(*center)))<.12f) continue;
            const auto seed=std::abs(normal.y())<.8f ? QVector3D(0,1,0) : QVector3D(1,0,0);
            const auto u=QVector3D::crossProduct(normal,seed).normalized(), v=QVector3D::crossProduct(normal,u);
            constexpr int segments=64;
            for (int i=0;i<=segments;++i) {
                const float angle=float(i)*(2.f*std::numbers::pi_v<float>/segments);
                const auto point=projection.project(pivot+(u*std::cos(angle)+v*std::sin(angle))*radius);
                if (!point) { line.clear(); break; }
                line.push_back(*point);
            }
        } else if (const auto tail=projection.project(pivot+normal*radius)) {
            const auto delta=*tail-*center;
            const auto len=length(delta);
            if (len>=22) line={*center+delta*(13./len),*tail};
        }
    }
    return paths;
}

int hitGizmo(const GizmoPaths& paths,QPointF pixel) {
    int hit=-1;
    double nearest=9.0;
    for (int axis=0;axis<3;++axis) {
        const auto& line=paths.axes[axis];
        for (size_t i=1;i<line.size();++i) {
            const auto delta=line[i]-line[i-1];
            const double extent=QPointF::dotProduct(delta,delta);
            if (extent<=.0001) continue;
            const double along=std::clamp(QPointF::dotProduct(pixel-line[i-1],delta)/extent,0.,1.);
            const double distance=length(pixel-(line[i-1]+delta*along));
            if (distance<nearest) { nearest=distance; hit=axis; }
        }
    }
    return hit;
}

std::vector<engine::OverlayVertex> gizmoOverlay(const GizmoPaths& paths,QSizeF extent,
                                               TransformTool tool,int hovered,int active) {
    std::vector<engine::OverlayVertex> vertices;
    if (extent.isEmpty()) return vertices;
    auto triangle=[&](QPointF a,QPointF b,QPointF c,engine::Vec3 color) {
        for (const auto point:{a,b,c})
            vertices.push_back({{float(2*point.x()/extent.width()-1),float(1-2*point.y()/extent.height())},color});
    };
    auto stroke=[&](QPointF a,QPointF b,double width,engine::Vec3 color) {
        const auto along=b-a;
        const auto len=length(along);
        if (len<.001) return;
        const auto normal=QPointF(-along.y(),along.x())*(width*.5/len);
        triangle(a+normal,a-normal,b-normal,color);
        triangle(a+normal,b-normal,b+normal,color);
    };
    for (int outline=1;outline>=0;--outline) {
        for (int axis=0;axis<3;++axis) {
            const auto& line=paths.axes[axis];
            if (line.empty()) continue;
            auto color=palette[axis];
            const bool emphasized=axis==active || axis==hovered;
            if (emphasized) for (auto& c:color) c=.7f*c+.3f;
            if (outline) color=edge;
            for (size_t i=1;i<line.size();++i)
                stroke(line[i-1],line[i],outline ? 6.0 : (emphasized ? 3.8 : 2.6),color);
            if (tool==TransformTool::Rotate) continue;
            const auto end=line.back(), delta=end-line.front(), direction=delta/length(delta);
            const auto perpendicular=QPointF(-direction.y(),direction.x());
            const auto cap=outline ? 6.5 : 4.8;
            if (tool==TransformTool::Move)
                triangle(end+direction*2,end-direction*12+perpendicular*cap,
                         end-direction*12-perpendicular*cap,color);
            else {
                const auto along=direction*cap, side=perpendicular*cap;
                triangle(end-along-side,end+along-side,end+along+side,color);
                triangle(end-along-side,end+along+side,end-along+side,color);
            }
            const auto label=end+direction*14;
            if (axis==0 || axis==2)
                stroke(label+QPointF(-3,-4),label+QPointF(3,4),outline ? 4 : 1.8,color);
            if (axis<=1)
                stroke(label+QPointF(3,-4),label+QPointF(axis==0 ? -3 : 0,axis==0 ? 4 : 0),outline ? 4 : 1.8,color);
            if (axis==1) {
                stroke(label+QPointF(-3,-4),label,outline ? 4 : 1.8,color);
                stroke(label,label+QPointF(0,4),outline ? 4 : 1.8,color);
            } else if (axis==2) {
                stroke(label+QPointF(-3,-4),label+QPointF(3,-4),outline ? 4 : 1.8,color);
                stroke(label+QPointF(-3,4),label+QPointF(3,4),outline ? 4 : 1.8,color);
            }
        }
    }
    return vertices;
}

std::optional<TransformDrag> TransformDrag::start(const engine::Camera& camera,QSizeF extent,
    TransformValue original,TransformSettings settings,int axis,QPointF cursor) {
    if (extent.isEmpty() || axis<0 || axis>2 || settings.tool==TransformTool::Select) return {};
    const Projection projection(camera,extent);
    TransformDrag drag;
    drag.camera_=camera;
    drag.extent_=extent;
    drag.original_=original;
    drag.last_=original;
    drag.settings_=settings;
    drag.axis_=axis;
    drag.direction_=direction(original,settings,axis);
    drag.radius_=handleLength*projection.depth(vector(original.position))/projection.focal;
    if (drag.radius_<=.0000001f) return {};
    if (settings.tool==TransformTool::Rotate) {
        const auto from=radial(projection,vector(original.position),drag.direction_,cursor);
        if (!from) return {};
        drag.lastRadial_=*from;
    } else {
        const auto from=axisParameter(projection,vector(original.position),drag.direction_,cursor);
        if (!from) return {};
        drag.initialParameter_=*from;
    }
    return drag;
}

TransformValue TransformDrag::move(QPointF cursor) {
    auto updated=original_;
    const Projection projection(camera_,extent_);
    const auto origin=vector(original_.position);
    if (settings_.tool==TransformTool::Rotate) {
        const auto to=radial(projection,origin,direction_,cursor);
        if (!to) return last_;
        angle_+=std::atan2(QVector3D::dotProduct(direction_,QVector3D::crossProduct(lastRadial_,*to)),
                          QVector3D::dotProduct(lastRadial_,*to))/toRadians;
        lastRadial_=*to;
        const auto degrees=snap(angle_,settings_);
        if (std::abs(degrees)>.0001f)
            updated.rotation=euler(QQuaternion::fromAxisAndAngle(direction_,degrees)*rotation(original_.rotation));
        return last_=updated;
    }
    const auto parameter=axisParameter(projection,origin,direction_,cursor);
    if (!parameter) return last_;
    const float distance=*parameter-initialParameter_;
    if (std::abs(distance)<radius_*.0001f) return last_=updated;
    if (settings_.tool==TransformTool::Move) {
        const auto coordinate=QVector3D::dotProduct(origin,direction_);
        const auto target=origin+direction_*(snap(coordinate+distance,settings_)-coordinate);
        updated.position={std::clamp(target.x(),-100000.f,100000.f),std::clamp(target.y(),-100000.f,100000.f),
                          std::clamp(target.z(),-100000.f,100000.f)};
    } else {
        auto scale=vector(original_.scale);
        scale[axis_]=std::clamp(snap(scale[axis_]*(1.f+distance/radius_),settings_),.001f,10000.f);
        updated.scale=value(scale);
    }
    return last_=updated;
}
}
