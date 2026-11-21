add_library(platz_core STATIC Project.cpp)
target_link_libraries(platz_core PUBLIC platz_assets)
target_include_directories(platz_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(platz_core PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(platz_core PRIVATE winsqlite3)
else()
    find_package(SQLite3 REQUIRED)
    target_link_libraries(platz_core PRIVATE SQLite::SQLite3)
endif()

if(BUILD_TESTING)
    add_executable(platz_core_tests ../../tests/core/ProjectTests.cpp)
    target_link_libraries(platz_core_tests PRIVATE platz_core)
    target_compile_definitions(platz_core_tests PRIVATE
        PLATZ_TEST_MESH="${PROJECT_SOURCE_DIR}/tests/assets/Oblique spire.glb"
        PLATZ_TEST_REPLACEMENT="${PROJECT_SOURCE_DIR}/tests/assets/Low spire.glb"
        PLATZ_TEST_MATERIALS="${PROJECT_SOURCE_DIR}/tests/assets/Color study.glb"
        PLATZ_TEST_LEGACY_COLOR="${PROJECT_SOURCE_DIR}/tests/assets/Legacy colored block.glb"
        PLATZ_TEST_TEXTURE="${PROJECT_SOURCE_DIR}/tests/assets/Harbor signs.glb")
    add_test(NAME core-project COMMAND platz_core_tests)
endif()
