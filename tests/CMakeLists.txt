find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(platz_gpu_checks gpu_checks.cpp)
target_link_libraries(platz_gpu_checks PRIVATE platz_engine)
target_compile_definitions(platz_gpu_checks PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME hardware_frame_readback COMMAND platz_gpu_checks)
set_tests_properties(hardware_frame_readback PROPERTIES LABELS "hardware")

add_executable(platz_studio_checks studio_checks.cpp)
target_link_libraries(platz_studio_checks PRIVATE platz_studio Qt6::Test)
target_compile_definitions(platz_studio_checks PRIVATE PLATZ_VALIDATION_PATH="${PROJECT_SOURCE_DIR}/projects/PLATZ Validation.platz")
add_test(NAME studio_edit_play_reopen COMMAND platz_studio_checks)
set_tests_properties(studio_edit_play_reopen PROPERTIES LABELS "hardware;interactive" TIMEOUT 90)
set_tests_properties(studio_edit_play_reopen PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
set_target_properties(platz_gpu_checks platz_studio_checks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>")
