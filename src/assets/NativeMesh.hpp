#pragma once
#include "MeshAsset.hpp"

namespace platz {
enum class NativeShape { Cube, Sphere, Cylinder };

struct NativeMeshSource {
    NativeShape shape=NativeShape::Cube;
    std::array<float,3> size{1,1,1};
    unsigned segments=1, rings=1;
    float topScale=1;
    bool operator==(const NativeMeshSource&) const = default;
};

NativeMeshSource defaultNativeMesh(NativeShape shape);
const char* nativeShapeName(NativeShape shape);
// Recipe 1. Produces an exportable GLB and validates it through the existing mesh decoder.
std::shared_ptr<const MeshAsset> buildNativeMesh(const NativeMeshSource& source);
}
