add_library(platz_assets STATIC AssetDigest.cpp MeshAsset.cpp NativeMesh.cpp RasterImage.cpp TextureDecode.cpp)
target_include_directories(platz_assets PRIVATE "${PROJECT_SOURCE_DIR}/third_party/cgltf")
target_link_libraries(platz_assets PRIVATE bcrypt ole32 windowscodecs)
