add_library(platz_engine STATIC renderer.cpp)
target_compile_features(platz_engine PUBLIC cxx_std_20)
target_include_directories(platz_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(platz_engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(platz_engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(platz_engine PRIVATE d3d12 dxgi dxguid)
get_filename_component(kits_root "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
file(GLOB sdk_bins LIST_DIRECTORIES true "${kits_root}/bin/10.*")
list(SORT sdk_bins COMPARE NATURAL ORDER DESCENDING)
find_program(PLATZ_DXC NAMES dxc HINTS ENV WindowsSdkVerBinPath ${sdk_bins} PATH_SUFFIXES x64 REQUIRED)
foreach(stage IN ITEMS vs ps)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/scene_${stage}.h")
    add_custom_command(OUTPUT "${header}"
        COMMAND "${PLATZ_DXC}" -nologo -WX -Ges -O3 -T ${stage}_6_0 -E ${stage}Main
            -Vn scene_${stage} -Fh "${header}" "${CMAKE_CURRENT_SOURCE_DIR}/scene.hlsl"
        DEPENDS scene.hlsl VERBATIM)
    target_sources(platz_engine PRIVATE "${header}")
endforeach()
