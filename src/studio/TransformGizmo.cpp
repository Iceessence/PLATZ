#include "TransformGizmo.hpp"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQuaternion>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace platz::studio {
namespace {
constexpr float handleLength=104.f;
constexpr float toRadians=std::numbers::pi_v<float>/180.f;
constexpr std::array<engine::Vec3,3> palette{
    engine::Vec3{.98f,.40f,.47f},engine::Vec3{.43f,.88f,.59f},engine::Vec3{.44f,.70f,1.f}};

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
constexpr int centerHandle=6;
constexpr int rollHandle=3;
constexpr std::array<std::array<int,2>,3> planeAxes{{{0,1},{0,2},{1,2}}};
constexpr engine::Vec3 neutral{.85f,.91f,.95f};

struct GlyphStrip { QRectF rect; float opacity; };

const std::array<std::vector<GlyphStrip>,3>& axisGlyphs(double dpr) {
    // Rasterize real type at device DPI once. Coverage strips travel through the same Vulkan overlay as the handles.
    struct Cache {
        double dpr{};
        QFont font;
        std::array<std::vector<GlyphStrip>,3> glyphs;
    };
    static thread_local Cache cache;
    auto font=QGuiApplication::font();
    font.setPixelSize(qRound(11*dpr));
    font.setBold(true);
    if (cache.dpr==dpr && cache.font==font) return cache.glyphs;
    cache.dpr=dpr;
    cache.font=font;
    const int side=qRound(18*dpr);
    for (int axis=0;axis<3;++axis) {
        QImage mask(side,side,QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        {
            QPainter p(&mask);
            p.setFont(font);
            p.setPen(Qt::white);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.drawText(mask.rect(),Qt::AlignCenter,QString(QChar('X'+axis)));
        }
        auto& strips=cache.glyphs[axis];
        strips.clear();
        for (int y=0;y<side;++y) {
            const auto* line=reinterpret_cast<const QRgb*>(mask.constScanLine(y));
            for (int x=0;x<side;) {
                const auto opacity=qAlpha(line[x]);
                int end=x+1;
                while (end<side && qAlpha(line[end])==opacity) ++end;
                if (opacity) strips.push_back({QRectF((x-side*.5)/dpr,(y-side*.5)/dpr,(end-x)/dpr,1./dpr),opacity/255.f});
                x=end;
            }
        }
    }
    return cache.glyphs;
}

float snap(float n,const TransformSettings& settings,bool modifier) {
    return settings.snapping!=modifier && settings.step>0 ? std::round(n/settings.step)*settings.step : n;
}
QVector3D pivotOffset(const TransformValue& transform,QVector3D localPoint) {
    return rotation(transform.rotation).rotatedVector(localPoint*vector(transform.scale));
}
std::optional<QVector3D> planePoint(const Projection& projection,QVector3D pivot,QVector3D normal,QPointF pixel) {
    const auto ray=projection.ray(pixel);
    const float alignment=QVector3D::dotProduct(ray,normal);
    if (std::abs(alignment)<.025f) return {};
    const float distance=QVector3D::dotProduct(pivot-projection.eye,normal)/alignment;
    if (distance<=0) return {};
    return projection.eye+ray*distance;
}
std::optional<GizmoPoint> projectPoint(const Projection& projection,QVector3D world,float opacity=1) {
    const auto pixel=projection.project(world);
    if (!pixel) return {};
    const float depth=projection.depth(world);
    const float ndc=projection.farPlane/(projection.farPlane-projection.nearPlane)*
                    (1-projection.nearPlane/depth);
    return GizmoPoint{*pixel,ndc,opacity};
}
std::array<QVector3D,2> tangents(QVector3D normal) {
    const auto seed=std::abs(normal.y())<.8f ? QVector3D(0,1,0) : QVector3D(1,0,0);
    const auto u=QVector3D::crossProduct(normal,seed).normalized();
    return {u,QVector3D::crossProduct(normal,u)};
}

std::vector<GizmoPoint> rotationRing(const Projection& projection,QVector3D pivot,QVector3D normal,
                                     float radius,bool screenRing) {
    constexpr int segments=96;
    std::vector<GizmoPoint> line;
    line.reserve(segments+1);
    const auto [u,v]=tangents(normal);
    const auto towardsEye=(projection.eye-pivot).normalized();
    for (int i=0;i<=segments;++i) {
        const float angle=float(i)*(2.f*std::numbers::pi_v<float>/segments);
        const auto radial=u*std::cos(angle)+v*std::sin(angle);
        const float front=QVector3D::dotProduct(radial,towardsEye);
        const float opacity=screenRing ? .72f : .22f+.78f*std::clamp((front+.12f)/.24f,0.f,1.f);
        const auto sample=projectPoint(projection,pivot+radial*radius,opacity);
        if (!sample) return {};
        line.push_back(*sample);
    }
    return line;
}
void capFace(std::vector<GizmoFace>& faces,const Projection& projection,QVector3D normal,
             QVector3D a,QVector3D b,QVector3D c,float opacity) {
    const auto eye=(projection.eye-(a+b+c)/3.f).normalized();
    if (QVector3D::dotProduct(normal,eye)<=0) return;
    const auto p=projectPoint(projection,a,opacity), q=projectPoint(projection,b,opacity),
               r=projectPoint(projection,c,opacity);
    if (p && q && r) {
        const auto keyLight=(projection.up+projection.right*.3f-projection.forward).normalized();
        faces.push_back({{*p,*q,*r},.70f+.30f*std::max(0.f,QVector3D::dotProduct(normal,keyLight))});
    }
}
void appendCap(std::vector<GizmoFace>& faces,const Projection& projection,QVector3D tip,QVector3D axis,
               float radius,float opacity,bool cone) {
    const auto [u,v]=tangents(axis);
    if (cone) {
        constexpr int segments=16;
        const float width=radius*.085f, height=radius*.19f;
        const auto base=tip-axis*height;
        for (int i=0;i<segments;++i) {
            const float a=2.f*std::numbers::pi_v<float>*i/segments, b=2.f*std::numbers::pi_v<float>*(i+1)/segments;
            const auto from=u*std::cos(a)+v*std::sin(a), to=u*std::cos(b)+v*std::sin(b);
            const auto normal=((from+to)*height+axis*width*2).normalized();
            capFace(faces,projection,normal,tip,base+from*width,base+to*width,opacity);
            capFace(faces,projection,-axis,base,base+from*width,base+to*width,opacity);
        }
        return;
    }
    const std::array basis{axis,u,v};
    const float half=radius*.063f;
    for (int side=0;side<3;++side) {
        const auto left=basis[(side+1)%3]*half, up=basis[(side+2)%3]*half;
        for (const float sign : {-1.f,1.f}) {
            const auto normal=basis[side]*sign;
            const auto center=tip+normal*half;
            capFace(faces,projection,normal,center-left-up,center+left-up,center+left+up,opacity);
            capFace(faces,projection,normal,center-left-up,center+left+up,center-left+up,opacity);
        }
    }
}
}

GizmoPaths gizmoPaths(const engine::Camera& camera,QSizeF extent,const TransformValue& object,
                      const TransformSettings& settings,Vec3 localPivot) {
    GizmoPaths paths;
    if (settings.tool==TransformTool::Select || extent.isEmpty()) return paths;
    const Projection projection(camera,extent);
    const auto pivot=vector(object.position)+pivotOffset(object,vector(localPivot));
    const auto center=projectPoint(projection,pivot);
    if (!center) return paths;
    paths.center=*center;
    paths.rotating=settings.tool==TransformTool::Rotate;
    const float radius=handleLength*projection.depth(pivot)/projection.focal;
    const auto towardsEye=(projection.eye-pivot).normalized();
    std::array<QVector3D,3> directions, facing;
    for (int axis=0;axis<3;++axis) {
        const auto normal=direction(object,settings,axis);
        directions[axis]=normal;
        const float facingEye=QVector3D::dotProduct(normal,towardsEye);
        facing[axis]=(facingEye<0 ? -normal : normal);
        if (paths.rotating) {
            if (std::abs(facingEye)>=.08f)
                paths.axes[axis]=rotationRing(projection,pivot,normal,radius,false);
        } else if (const auto tail=projectPoint(projection,pivot+normal*radius)) {
            const auto pixels=length(tail->pixel-center->pixel);
            const auto angle=std::acos(std::clamp(std::abs(facingEye),0.f,1.f))/toRadians;
            const auto fade=std::clamp((angle-5.f)/8.f,0.f,1.f);
            if (pixels<=1 || fade==0) continue;
            const float inset=float(std::min(17./pixels,.6));
            const auto start=projectPoint(projection,pivot+normal*(radius*inset),fade);
            if (start) paths.axes[axis]={*start,GizmoPoint{tail->pixel,tail->depth,fade}};
            appendCap(paths.caps[axis],projection,pivot+normal*radius,normal,radius,fade,settings.tool==TransformTool::Move);
        }
    }
    if (paths.rotating) {
        paths.roll=rotationRing(projection,pivot,towardsEye,radius*1.15f,true);
        return paths;
    }
    paths.hasCenter=true;
    for (std::size_t index=0;index<planeAxes.size();++index) {
        const auto [a,b]=planeAxes[index];
        const auto normal=QVector3D::crossProduct(directions[a],directions[b]);
        if (std::abs(QVector3D::dotProduct(normal,towardsEye))<.13f) continue;
        const auto u=facing[a]*radius, v=facing[b]*radius;
        auto& polygon=paths.planes[index];
        for (const auto& corner : {u*.27f+v*.27f,u*.49f+v*.27f,u*.49f+v*.49f,u*.27f+v*.49f}) {
            if (const auto point=projectPoint(projection,pivot+corner)) polygon.push_back(*point);
            else { polygon.clear(); break; }
        }
    }
    return paths;
}

int hitGizmo(const GizmoPaths& paths,QPointF pixel) {
    if (paths.hasCenter && length(pixel-paths.center.pixel)<14.) return centerHandle;
    for (size_t i=0;i<paths.planes.size();++i) {
        const auto& quad=paths.planes[i];
        if (quad.size()!=4) continue;
        bool positive=false,negative=false;
        for (size_t j=0;j<quad.size();++j) {
            const auto edge=quad[(j+1)%quad.size()].pixel-quad[j].pixel;
            const auto to= pixel-quad[j].pixel;
            const auto cross=edge.x()*to.y()-edge.y()*to.x();
            positive|=cross>0;
            negative|=cross<0;
        }
        if (!positive || !negative) return static_cast<int>(i)+3;
    }
    int hit=-1, back=-1;
    double nearest=paths.rotating ? 12. : 14., nearBack=8.;
    const auto pathHit=[&](const std::vector<GizmoPoint>& line,int handle) {
        for (size_t i=1;i<line.size();++i) {
            const auto& a=line[i-1];
            const auto& b=line[i];
            const auto delta=b.pixel-a.pixel;
            const double extent=QPointF::dotProduct(delta,delta);
            if (extent<=.0001) continue;
            const double along=std::clamp(QPointF::dotProduct(pixel-a.pixel,delta)/extent,0.,1.);
            const double distance=length(pixel-(a.pixel+delta*along));
            const float opacity=std::lerp(a.opacity,b.opacity,float(along));
            if (paths.rotating && opacity<.53f) {
                if (distance<nearBack) { nearBack=distance; back=handle; }
            } else if (opacity>=.23f && distance<nearest) {
                nearest=distance;
                hit=handle;
            }
        }
    };
    for (int axis=0;axis<3;++axis) pathHit(paths.axes[axis],axis);
    pathHit(paths.roll,rollHandle);
    return hit>=0 ? hit : back;
}

std::vector<engine::OverlayVertex> gizmoOverlay(const GizmoPaths& paths,QSizeF extent,
                                               TransformTool tool,int hovered,int active,double dpr) {
    std::vector<engine::OverlayVertex> vertices;
    if (extent.isEmpty() || tool==TransformTool::Select) return vertices;
    const double feather=1./dpr;
    auto vertex=[&](GizmoPoint p,engine::Vec3 color) {
        vertices.push_back({{float(2*p.pixel.x()/extent.width()-1),float(1-2*p.pixel.y()/extent.height())},
                            color,p.depth,p.opacity});
    };
    auto triangle=[&](GizmoPoint a,GizmoPoint b,GizmoPoint c,engine::Vec3 color) {
        vertex(a,color); vertex(b,color); vertex(c,color);
    };
    auto quad=[&](GizmoPoint a,GizmoPoint b,GizmoPoint c,GizmoPoint d,engine::Vec3 color) {
        triangle(a,b,c,color);
        triangle(a,c,d,color);
    };
    auto offset=[](GizmoPoint point,QPointF delta,float coverage=1) {
        point.pixel+=delta;
        point.opacity*=coverage;
        return point;
    };
    auto normal=[](QPointF along) {
        const auto len=length(along);
        return len>.001 ? QPointF(-along.y(),along.x())/len : QPointF{};
    };
    auto ribbon=[&](GizmoPoint a,GizmoPoint b,QPointF atA,QPointF atB,double width,engine::Vec3 color) {
        const auto ac=atA*(width*.5), af=atA*(width*.5+feather);
        const auto bc=atB*(width*.5), bf=atB*(width*.5+feather);
        quad(offset(a,ac),offset(a,-ac),offset(b,-bc),offset(b,bc),color);
        quad(offset(a,af,0),offset(a,ac),offset(b,bc),offset(b,bf,0),color);
        quad(offset(a,-ac),offset(a,-af,0),offset(b,-bf,0),offset(b,-bc),color);
    };
    auto stroke=[&](GizmoPoint a,GizmoPoint b,double width,engine::Vec3 color) {
        const auto side=normal(b.pixel-a.pixel);
        if (!side.isNull()) ribbon(a,b,side,side,width,color);
    };
    auto emphasized=[&](int id){return id==hovered || id==active;};
    auto tint=[&](engine::Vec3 color,int id) {
        if (emphasized(id)) for (auto& c : color) c=.72f*c+.28f;
        return color;
    };
    if (paths.rotationGuide.size()>1) {
        const auto color=active>=0 && active<3 ? palette[active] : neutral;
        const auto center=offset(paths.center,{},.24f);
        for (size_t i=1;i<paths.rotationGuide.size();++i) {
            const auto a=paths.rotationGuide[i-1], b=paths.rotationGuide[i];
            triangle(center,offset(a,{},.24f),offset(b,{},.24f),color);
            stroke(a,b,2,color);
        }
        stroke(paths.center,paths.rotationGuide.front(),1.4,color);
        stroke(paths.center,paths.rotationGuide.back(),2.1,color);
    }
    for (size_t i=0;i<paths.planes.size();++i) {
        const auto& polygon=paths.planes[i];
        if (polygon.size()!=4) continue;
        const auto [a,b]=planeAxes[i];
        engine::Vec3 color{};
        for (int j=0;j<3;++j) color[j]=(palette[a][j]+palette[b][j])*.5f;
        const int id=int(i)+3;
        color=tint(color,id);
        const auto fill=emphasized(id) ? .63f : (active<0 ? .28f : .15f);
        quad(offset(polygon[0],{},fill),offset(polygon[1],{},fill),offset(polygon[2],{},fill),
             offset(polygon[3],{},fill),color);
        for (size_t j=0;j<polygon.size();++j)
            stroke(offset(polygon[j],{},.7f),offset(polygon[(j+1)%polygon.size()],{},.7f),1.4,color);
    }
    auto line=[&](const std::vector<GizmoPoint>& points,int id,engine::Vec3 color) {
        if (points.size()<2) return;
        const float opacity=active>=0 && active!=id ? .56f : 1.f;
        color=tint(color,id);
        const auto last=points.size()-1;
        const bool closed=last>2 && length(points.back().pixel-points.front().pixel)<.01;
        const auto sideAt=[&](size_t i) {
            const auto previous=i==0 ? (closed ? last-1 : 0) : i-1;
            const auto next=i==last ? (closed ? 1 : last) : i+1;
            return normal(points[next].pixel-points[previous].pixel);
        };
        auto from=sideAt(0);
        for (size_t i=1;i<points.size();++i) {
            const auto to=sideAt(i);
            ribbon(offset(points[i-1],{},opacity),offset(points[i],{},opacity),from,to,emphasized(id) ? 5.1 : 3.5,color);
            from=to;
        }
    };
    for (int axis=0;axis<3;++axis) {
        line(paths.axes[axis],axis,palette[axis]);
        for (const auto& face : paths.caps[axis]) {
            auto color=tint(palette[axis],axis);
            for (auto& c : color) c*=face.light;
            const float opacity=active>=0 && active!=axis ? .56f : 1.f;
            triangle(offset(face.points[0],{},opacity),offset(face.points[1],{},opacity),
                     offset(face.points[2],{},opacity),color);
        }
    }
    if (paths.hasCenter) {
        const auto& glyphs=axisGlyphs(dpr);
        for (int axis=0;axis<3;++axis) {
            if (paths.axes[axis].size()<2) continue;
            const auto tip=paths.axes[axis].back();
            const auto away=tip.pixel-paths.center.pixel;
            const auto distance=length(away);
            if (distance<38.) continue;
            auto badge=offset(tip,away*(17./distance),active>=0 && active!=axis ? .55f : 1.f);
            // Align mask pixels to the framebuffer edges, including fractional monitor scaling.
            badge.pixel=QPointF(std::round(badge.pixel.x()*dpr)/dpr,std::round(badge.pixel.y()*dpr)/dpr);
            const auto color=tint(palette[axis],axis);
            constexpr engine::Vec3 badgeBackground{.074f,.097f,.12f};
            constexpr int sides=24;
            for (int i=0;i<sides;++i) {
                auto rim=[](int index) {
                    const auto angle=2.*std::numbers::pi*index/sides;
                    return QPointF(std::cos(angle)*8.2,std::sin(angle)*8.2);
                };
                const auto a=offset(badge,rim(i)), b=offset(badge,rim(i+1));
                triangle(badge,a,b,badgeBackground);
                stroke(offset(a,{},.61f),offset(b,{},.61f),.8,color);
            }
            for (const auto& strip : glyphs[axis]) {
                const auto& r=strip.rect;
                quad(offset(badge,r.topLeft(),strip.opacity),offset(badge,r.topRight(),strip.opacity),
                     offset(badge,r.bottomRight(),strip.opacity),offset(badge,r.bottomLeft(),strip.opacity),color);
            }
        }
    }
    line(paths.roll,rollHandle,neutral);
    if (paths.hasCenter) {
        const auto color=tint(neutral,centerHandle);
        const auto opacity=emphasized(centerHandle) ? .95f : .7f;
        if (tool==TransformTool::Scale) {
            const auto p=paths.center;
            quad(offset(p,{-7,-7},opacity),offset(p,{7,-7},opacity),offset(p,{7,7},opacity),offset(p,{-7,7},opacity),color);
            const std::array corners{QPointF(-7,-7),QPointF(7,-7),QPointF(7,7),QPointF(-7,7)};
            for (size_t j=0;j<corners.size();++j) stroke(offset(p,corners[j]),offset(p,corners[(j+1)%corners.size()]),1.5,color);
        } else {
            constexpr int segments=28;
            auto circle=[](int i) {
                const auto angle=2.*std::numbers::pi*i/segments;
                return QPointF(std::cos(angle)*9.,std::sin(angle)*9.);
            };
            for (int i=0;i<segments;++i) {
                const auto a=circle(i), b=circle(i+1);
                triangle(offset(paths.center,{},opacity),offset(paths.center,a,opacity),offset(paths.center,b,opacity),color);
                stroke(offset(paths.center,a),offset(paths.center,b),1.6,color);
            }
        }
    }
    return vertices;
}

std::optional<TransformDrag> TransformDrag::start(const engine::Camera& camera,QSizeF extent,
    TransformValue original,TransformSettings settings,int axis,QPointF cursor,Vec3 localPivot) {
    const bool rotating=settings.tool==TransformTool::Rotate;
    if (extent.isEmpty() || axis<0 || axis>(rotating ? rollHandle : centerHandle) ||
        settings.tool==TransformTool::Select) return {};
    const Projection projection(camera,extent);
    TransformDrag drag;
    drag.camera_=camera;
    drag.extent_=extent;
    drag.original_=drag.last_=original;
    drag.settings_=settings;
    drag.axis_=axis;
    drag.localPivot_=vector(localPivot);
    drag.pivot_=vector(original.position)+pivotOffset(original,drag.localPivot_);
    drag.radius_=handleLength*projection.depth(drag.pivot_)/projection.focal;
    if (drag.radius_<=.0000001f) return {};
    drag.lastCursor_=cursor;
    if (rotating || axis>=3) {
        if (axis<3) drag.direction_=direction(original,settings,axis);
        else if (rotating) drag.direction_=(projection.eye-drag.pivot_).normalized();
        else if (axis==centerHandle) drag.direction_=projection.forward;
        else {
            const auto [a,b]=planeAxes[axis-3];
            drag.direction_=QVector3D::crossProduct(direction(original,settings,a),direction(original,settings,b));
        }
        if (settings.tool==TransformTool::Scale && axis==centerHandle) return drag;
        const auto from=rotating ? radial(projection,drag.pivot_,drag.direction_,cursor) :
                                   planePoint(projection,drag.pivot_,drag.direction_,cursor);
        if (!from) return {};
        drag.lastPoint_=*from;
        if (rotating) drag.startRadial_=*from;
    } else {
        drag.direction_=direction(original,settings,axis);
        const auto from=axisParameter(projection,drag.pivot_,drag.direction_,cursor);
        if (!from) return {};
        drag.lastParameter_=*from;
    }
    return drag;
}

std::vector<GizmoPoint> TransformDrag::rotationGuide() const {
    if (settings_.tool!=TransformTool::Rotate || std::abs(degrees_)<.001f) return {};
    const Projection projection(camera_,extent_);
    const float turn=std::clamp(degrees_,-360.f,360.f);
    const int steps=static_cast<int>(std::ceil(std::abs(turn)/5.f));
    const float radius=radius_*(axis_==rollHandle ? 1.15f : 1.f)*.91f;
    std::vector<GizmoPoint> guide;
    guide.reserve(steps+1);
    for (int i=0;i<=steps;++i) {
        const auto radial=QQuaternion::fromAxisAndAngle(direction_,turn*i/steps).rotatedVector(startRadial_);
        if (const auto point=projectPoint(projection,pivot_+radial*radius)) guide.push_back(*point);
        else return {};
    }
    return guide;
}

TransformValue TransformDrag::move(QPointF cursor,bool snapModifier,bool precision) {
    auto updated=original_;
    const Projection projection(camera_,extent_);
    const float gain=precision ? .1f : 1.f;
    auto snapValue=[&](float n){return snap(n,settings_,snapModifier);};
    if (settings_.tool==TransformTool::Rotate) {
        const auto to=radial(projection,pivot_,direction_,cursor);
        if (!to) return last_;
        angle_+=gain*std::atan2(QVector3D::dotProduct(direction_,QVector3D::crossProduct(lastPoint_,*to)),
                                QVector3D::dotProduct(lastPoint_,*to))/toRadians;
        lastPoint_=*to;
        degrees_=snapValue(angle_);
        if (std::abs(degrees_)>.0001f)
            updated.rotation=euler(QQuaternion::fromAxisAndAngle(direction_,degrees_)*rotation(original_.rotation));
    } else {
        if (axis_<3) {
            const auto parameter=axisParameter(projection,pivot_,direction_,cursor);
            if (!parameter) return last_;
            distance_+=(*parameter-lastParameter_)*gain;
            lastParameter_=*parameter;
            travel_=direction_*distance_;
        } else if (settings_.tool==TransformTool::Scale && axis_==centerHandle) {
            const auto motion=cursor-lastCursor_;
            distance_+=float(motion.x()-motion.y())*gain/handleLength;
        } else {
            const auto point=planePoint(projection,pivot_,direction_,cursor);
            if (!point) return last_;
            travel_+=(*point-lastPoint_)*gain;
            lastPoint_=*point;
        }
        lastCursor_=cursor;
        if (settings_.tool==TransformTool::Move) {
            auto delta=travel_;
            if (axis_!=centerHandle && !qFuzzyIsNull(travel_.lengthSquared())) {
                const auto along=[&](QVector3D axis){
                    const auto coordinate=QVector3D::dotProduct(pivot_,axis);
                    return axis*(snapValue(coordinate+QVector3D::dotProduct(travel_,axis))-coordinate);
                };
                if (axis_<3) delta=along(direction_);
                else {
                    const auto [a,b]=planeAxes[axis_-3];
                    delta=along(direction(original_,settings_,a))+along(direction(original_,settings_,b));
                }
            } else if (axis_==centerHandle && !qFuzzyIsNull(travel_.lengthSquared())) {
                delta=projection.right*snapValue(QVector3D::dotProduct(travel_,projection.right))+
                      projection.up*snapValue(QVector3D::dotProduct(travel_,projection.up));
            }
            updated.position=value(vector(original_.position)+delta);
        } else {
            auto scale=vector(original_.scale);
            const auto rescale=[&](int axis) {
                const auto local=direction(original_,settings_,axis);
                const auto distance=QVector3D::dotProduct(travel_,local);
                if (std::abs(distance)>radius_*.0001f)
                    scale[axis]=std::clamp(snapValue(scale[axis]*(1+distance/radius_)),.001f,10000.f);
            };
            if (axis_<3) rescale(axis_);
            else if (axis_==centerHandle) {
                const auto factor=std::clamp(qFuzzyIsNull(distance_) ? 1.f : snapValue(1+distance_),
                    .001f/std::min({scale.x(),scale.y(),scale.z()}),10000.f/std::max({scale.x(),scale.y(),scale.z()}));
                scale*=factor;
            } else {
                const auto [a,b]=planeAxes[axis_-3];
                rescale(a);
                rescale(b);
            }
            updated.scale=value(scale);
        }
    }
    if (updated.rotation!=original_.rotation || updated.scale!=original_.scale)
        updated.position=value(pivot_-pivotOffset(updated,localPivot_));
    for (float* coordinate : {&updated.position.x,&updated.position.y,&updated.position.z})
        *coordinate=std::clamp(*coordinate,-100000.f,100000.f);
    return last_=updated;
}
}
