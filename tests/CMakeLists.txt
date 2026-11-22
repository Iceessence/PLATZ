find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(platz_gpu_checks gpu_checks.cpp)
target_link_libraries(platz_gpu_checks PRIVATE platz_engine platz_core)
target_compile_definitions(platz_gpu_checks PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(platz_gpu_checks PRIVATE
    PLATZ_TEST_MESH="${PROJECT_SOURCE_DIR}/tests/assets/Oblique spire.glb"
    PLATZ_TEST_MATERIALS="${PROJECT_SOURCE_DIR}/tests/assets/Color study.glb"
    PLATZ_TEST_TEXTURE="${PROJECT_SOURCE_DIR}/tests/assets/Harbor signs.glb")
add_test(NAME hardware_frame_readback COMMAND platz_gpu_checks)
add_test(NAME hardware_gpu_assisted COMMAND platz_gpu_checks)
add_test(NAME validation_scene_profile COMMAND platz_gpu_checks --profile "${PROJECT_SOURCE_DIR}/projects/PLATZ Validation.platz")
set_tests_properties(validation_scene_profile PROPERTIES LABELS "hardware;benchmark" TIMEOUT 60
    ENVIRONMENT "PLATZ_VK_VALIDATION=0"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:platz_sdl3>")
set_tests_properties(hardware_frame_readback hardware_gpu_assisted PROPERTIES LABELS "hardware" TIMEOUT 90)

add_executable(platz_studio_checks studio_checks.cpp)
target_link_libraries(platz_studio_checks PRIVATE platz_studio Qt6::Test)
target_compile_definitions(platz_studio_checks PRIVATE
    PLATZ_TEST_MESH="${PROJECT_SOURCE_DIR}/tests/assets/Oblique spire.glb"
    PLATZ_TEST_REPLACEMENT="${PROJECT_SOURCE_DIR}/tests/assets/Low spire.glb"
    PLATZ_TEST_MATERIALS="${PROJECT_SOURCE_DIR}/tests/assets/Color study.glb"
    PLATZ_TEST_TEXTURE="${PROJECT_SOURCE_DIR}/tests/assets/Harbor signs.glb")
target_compile_definitions(platz_studio_checks PRIVATE PLATZ_VALIDATION_PATH="${PROJECT_SOURCE_DIR}/projects/PLATZ Validation.platz")
add_test(NAME studio_edit_play_reopen COMMAND platz_studio_checks)
add_test(NAME studio_transform_highdpi COMMAND platz_studio_checks --gizmo-only)
set(studio_checks studio_edit_play_reopen studio_transform_highdpi)
set_tests_properties(${studio_checks} PROPERTIES LABELS "hardware;interactive" TIMEOUT 90 RUN_SERIAL TRUE)
set_tests_properties(${studio_checks} PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
set(all_gpu_checks ${studio_checks} hardware_frame_readback hardware_gpu_assisted)
set_property(TEST ${all_gpu_checks} APPEND PROPERTY
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:platz_sdl3>")
set_tests_properties(${all_gpu_checks} PROPERTIES
    FAIL_REGULAR_EXPRESSION "PLATZ Vulkan ERROR|SYNC-HAZARD|Validation Error:"
    ENVIRONMENT "PLATZ_VK_VALIDATION=1;VK_LAYER_PATH=${PROJECT_SOURCE_DIR}/.deps/graphics/sdk/Bin;VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true")
set_property(TEST hardware_gpu_assisted APPEND PROPERTY ENVIRONMENT "VK_KHRONOS_VALIDATION_GPUAV_ENABLE=true")
set_property(TEST studio_transform_highdpi APPEND PROPERTY ENVIRONMENT "QT_SCALE_FACTOR=1.5")
set_target_properties(platz_gpu_checks platz_studio_checks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>")
add_executable(platz_process_checks process_checks.cpp)
target_link_libraries(platz_process_checks PRIVATE platz_core Qt6::Core ole32 oleaut32 uiautomationcore)
set_target_properties(platz_process_checks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>")
add_test(NAME studio_process_restart COMMAND platz_process_checks "$<TARGET_FILE:PLATZ>" "${PROJECT_SOURCE_DIR}/projects/PLATZ Validation.platz")
set_tests_properties(studio_process_restart PROPERTIES TIMEOUT 45 LABELS "interactive" RUN_SERIAL TRUE ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
