cmake_minimum_required(VERSION 3.10)
project(rotationlinescreen_regression C CXX)
set(YABAUSE_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/vs2026" CACHE PATH "Built Yabause tree")
find_package(SDL2 CONFIG REQUIRED)
enable_testing()
add_executable(rotationlinescreen_regression rotationlinescreen_regression.c)
set_target_properties(rotationlinescreen_regression PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(rotationlinescreen_regression PRIVATE ../../src)
target_compile_definitions(rotationlinescreen_regression PRIVATE HAVE_STDINT_H=1 HAVE_MUSASHI=1 HAVE_PLAY_JIT=1 HAVE_C99_VARIADIC_MACROS=1 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(rotationlinescreen_regression PRIVATE
    "${YABAUSE_BUILD_DIR}/src/$<CONFIG>/yabause.lib"
    "${YABAUSE_BUILD_DIR}/src/play/$<CONFIG>/CodeGen.lib"
    SDL2::SDL2 opengl32 glu32 vfw32)
add_custom_command(TARGET rotationlinescreen_regression POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:rotationlinescreen_regression>")
add_test(NAME rotationlinescreen_regression COMMAND rotationlinescreen_regression)
