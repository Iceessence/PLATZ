#include "NativeMesh.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace platz {
namespace {
using Point=std::array<float,3>;
struct Geometry {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t count() const { return static_cast<std::uint32_t>(vertices.size()); }
    void triangle(unsigned a,unsigned b,unsigned c) { indices.insert(indices.end(),{a,b,c}); }
    void strip(unsigned first,unsigned columns,unsigned rows,bool sphere=false) {
        const auto stride=columns+1;
        for (unsigned row=0;row<rows;++row) {
            for (unsigned col=0;col<columns;++col) {
                const auto a=first+row*stride+col, b=a+1, c=a+stride, d=c+1;
                if (!sphere || row!=0) triangle(a,b,c);
                if (!sphere || row+1!=rows) triangle(b,d,c);
            }
        }
    }
};
void validate(const NativeMeshSource& source) {
    for (float size:source.size)
        if (!std::isfinite(size) || size<.01f || size>100.f)
            throw std::runtime_error("Mesh dimensions must be between 0.01 and 100 metres.");
    const auto segments=source.segments, rings=source.rings;
    switch (source.shape) {
    case NativeShape::Cube:
        if (segments>=1 && segments<=24 && rings==1 && source.topScale==1) return;
        break;
    case NativeShape::Sphere:
        if (segments>=3 && segments<=128 && rings>=2 && rings<=64 && source.topScale==1) return;
        break;
    case NativeShape::Cylinder:
        if (segments>=3 && segments<=128 && rings>=1 && rings<=32 &&
            source.topScale>=.1f && source.topScale<=2.f) return;
        break;
    }
    throw std::runtime_error("Invalid tessellation or cylinder taper for the native mesh.");
}
Point unit(Point p) {
    const auto length=std::hypot(p[0],p[1],p[2]);
    for (auto& v:p) v/=length;
    return p;
}
Geometry box(const NativeMeshSource& source) {
    Geometry mesh;
    const auto n=source.segments;
    for (int axis=0;axis<3;++axis) {
        for (float sign:{-1.f,1.f}) {
            const auto first=mesh.count();
            const int uAxis=(axis+1)%3, vAxis=(axis+2)%3;
            for (unsigned row=0;row<=n;++row) {
                const float v=float(row)/n;
                for (unsigned col=0;col<=n;++col) {
                    const float u=float(col)/n;
                    Point position{}, normal{};
                    normal[axis]=sign;
                    position[axis]=sign*source.size[axis]*.5f;
                    position[uAxis]=(u-.5f)*source.size[uAxis];
                    position[vAxis]=sign*(v-.5f)*source.size[vAxis];
                    mesh.vertices.push_back({position,normal,{u,1-v}});
                }
            }
            mesh.strip(first,n,n);
        }
    }
    return mesh;
}
std::array<float,2> radial(unsigned column,unsigned count) {
    if (column==0 || column==count) return {1,0};
    const auto angle=2.f*std::numbers::pi_v<float>*float(column)/count;
    return {std::cos(angle),std::sin(angle)};
}
Geometry roundShape(const NativeMeshSource& source) {
    Geometry mesh;
    const auto columns=source.segments, rows=source.rings;
    const auto [width,height,depth]=source.size;
    const bool sphere=source.shape==NativeShape::Sphere;
    for (unsigned row=0;row<=rows;++row) {
        const float v=float(row)/rows;
        const auto latitude=v*std::numbers::pi_v<float>;
        const auto radius=sphere ? (row==0 || row==rows ? 0.f : std::sin(latitude)) :
                                   std::lerp(source.topScale,1.f,v);
        const auto y=sphere ? std::cos(latitude) : 1.f-2.f*v;
        for (unsigned col=0;col<=columns;++col) {
            const auto [x,z]=radial(col,columns);
            const Point p{x*radius*width*.5f,y*height*.5f,z*radius*depth*.5f};
            const Point n=sphere ? Point{x*radius/width,y/height,z*radius/depth} :
                                   Point{x/width,(1.f-source.topScale)*.5f/height,z/depth};
            mesh.vertices.push_back({p,unit(n),{float(col)/columns,v}});
        }
    }
    mesh.strip(0,columns,rows,sphere);
    if (sphere) return mesh;
    for (float sign:{-1.f,1.f}) {
        const auto center=mesh.count();
        const float y=sign*height*.5f, cap=sign>0 ? source.topScale : 1.f;
        mesh.vertices.push_back({{0,y,0},{0,sign,0},{.5f,.5f}});
        for (unsigned col=0;col<columns;++col) {
            const auto [x,z]=radial(col,columns);
            mesh.vertices.push_back({{x*cap*width*.5f,y,z*cap*depth*.5f},{0,sign,0},{.5f+x*.5f,.5f+z*.5f}});
            const auto current=center+1+col, next=center+1+(col+1)%columns;
            if (sign>0) mesh.triangle(center,next,current);
            else mesh.triangle(center,current,next);
        }
    }
    return mesh;
}
void word(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    for (unsigned shift=0;shift<32;shift+=8)
        bytes.push_back(static_cast<std::uint8_t>(value>>shift));
}
std::vector<std::uint8_t> asGlb(const Geometry& mesh) {
    constexpr unsigned stride=32;
    const auto vertexBytes=stride*mesh.count();
    const auto indexBytes=4u*static_cast<unsigned>(mesh.indices.size());
    const auto binaryBytes=vertexBytes+indexBytes;
    auto low=mesh.vertices.front().position, high=low;
    for (const auto& vertex:mesh.vertices) {
        for (unsigned axis=0;axis<3;++axis) {
            low[axis]=std::min(low[axis],vertex.position[axis]);
            high[axis]=std::max(high[axis],vertex.position[axis]);
        }
    }
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json.precision(std::numeric_limits<float>::max_digits10);
    auto bounds=[&](const Point& p){json<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';};
    json<<R"({"asset":{"version":"2.0","generator":"PLATZ Native Mesh 1"},"scene":0,"scenes":[{"nodes":[0]}],)"
        <<R"("nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}],)"
        <<R"("buffers":[{"byteLength":)"<<binaryBytes
        <<R"(}],"bufferViews":[{"buffer":0,"target":34962,"byteStride":32,"byteLength":)"<<vertexBytes
        <<R"(},{"buffer":0,"target":34963,"byteOffset":)"<<vertexBytes<<R"(,"byteLength":)"<<indexBytes
        <<R"(}],"accessors":[{"bufferView":0,"componentType":5126,"type":"VEC3","count":)"<<mesh.count()
        <<R"(,"min":)";
    bounds(low);
    json<<R"(,"max":)";
    bounds(high);
    json<<R"(},{"bufferView":0,"byteOffset":12,"componentType":5126,"type":"VEC3","count":)"<<mesh.count()
        <<R"(},{"bufferView":0,"byteOffset":24,"componentType":5126,"type":"VEC2","count":)"<<mesh.count()
        <<R"(},{"bufferView":1,"componentType":5125,"type":"SCALAR","count":)"<<mesh.indices.size()<<"}]}";
    auto metadata=json.str();
    while (metadata.size()%4) metadata.push_back(' ');
    const auto jsonBytes=static_cast<unsigned>(metadata.size());
    std::vector<std::uint8_t> result;
    result.reserve(28+jsonBytes+binaryBytes);
    for (auto field:{0x46546c67u,2u,28u+jsonBytes+binaryBytes,jsonBytes,0x4e4f534au}) word(result,field);
    result.insert(result.end(),metadata.begin(),metadata.end());
    word(result,binaryBytes);
    word(result,0x004e4942u);
    auto floats=[&](const auto& values){
        for (float f:values) word(result,std::bit_cast<std::uint32_t>(f));
    };
    for (const auto& vertex:mesh.vertices) {
        floats(vertex.position);
        floats(vertex.normal);
        floats(vertex.uv);
    }
    for (auto index:mesh.indices) word(result,index);
    return result;
}
}

NativeMeshSource defaultNativeMesh(NativeShape shape) {
    switch (shape) {
    case NativeShape::Cube: return {};
    case NativeShape::Sphere: return {shape,{1,1,1},32,16};
    case NativeShape::Cylinder: return {shape,{1,1,1},32,1};
    }
    throw std::runtime_error("Unknown native mesh shape.");
}
const char* nativeShapeName(NativeShape shape) {
    switch (shape) {
    case NativeShape::Cube: return "Cube";
    case NativeShape::Sphere: return "Sphere";
    case NativeShape::Cylinder: return "Cylinder";
    }
    throw std::runtime_error("Unknown native mesh shape.");
}
std::shared_ptr<const MeshAsset> buildNativeMesh(const NativeMeshSource& source) {
    validate(source);
    return MeshAsset::decode(asGlb(source.shape==NativeShape::Cube ? box(source) : roundShape(source)));
}
}
