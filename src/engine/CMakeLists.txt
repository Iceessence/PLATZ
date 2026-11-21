set(PLATZ_GRAPHICS "${PROJECT_SOURCE_DIR}/.deps/graphics")
set(PLATZ_SDK "${PLATZ_GRAPHICS}/sdk")
set(PLATZ_SDL "${PLATZ_GRAPHICS}/sdl/SDL3-3.4.20")
set(PLATZ_SLANG "${PLATZ_GRAPHICS}/slang/bin/slangc.exe")
set(PLATZ_VALIDATOR "${PLATZ_SDK}/Bin/spirv-val.exe")
if(NOT EXISTS "${PLATZ_VALIDATOR}" OR NOT EXISTS "${PLATZ_SLANG}")
    message(FATAL_ERROR "Run tools/setup-graphics.ps1 to prepare the pinned graphics development kit.")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

add_library(platz_sdl3 SHARED IMPORTED GLOBAL)
set_target_properties(platz_sdl3 PROPERTIES
    IMPORTED_LOCATION "${PLATZ_SDL}/lib/x64/SDL3.dll"
    IMPORTED_IMPLIB "${PLATZ_SDL}/lib/x64/SDL3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${PLATZ_SDL}/include")

set(shader_header "${CMAKE_CURRENT_BINARY_DIR}/scene_spv.hpp")
set(shader_compiler "${PROJECT_SOURCE_DIR}/tools/compile_shaders.py")
add_custom_command(OUTPUT "${shader_header}"
    COMMAND "${Python3_EXECUTABLE}" "${shader_compiler}" "${CMAKE_CURRENT_SOURCE_DIR}/scene.slang"
        "${shader_header}" --compiler "${PLATZ_SLANG}" --validator "${PLATZ_VALIDATOR}"
    DEPENDS scene.slang color.slangh "${shader_compiler}" "${PLATZ_SLANG}" "${PLATZ_VALIDATOR}"
    COMMENT "Compile, reflect and validate Vulkan 1.4 scene shaders" VERBATIM)

add_library(platz_engine STATIC renderer.cpp vulkan_device.cpp "${shader_header}")
target_include_directories(platz_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(platz_engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    "${PLATZ_SDK}/Include" "${PLATZ_GRAPHICS}/vma/VulkanMemoryAllocator-3.5.0/include")
target_compile_definitions(platz_engine PRIVATE VK_USE_PLATFORM_WIN32_KHR)
target_link_libraries(platz_engine PRIVATE "${PLATZ_SDK}/Lib/vulkan-1.lib" platz_sdl3 platz_assets)

if(BUILD_TESTING)
    add_test(NAME shader_toolchain COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/shader_checks.py" "${PROJECT_SOURCE_DIR}" "${PLATZ_GRAPHICS}")
endif()
