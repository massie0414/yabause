cmake_minimum_required(VERSION 3.10)
project(scspmix_regression C CXX)
set(YABAUSE_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/vs2026" CACHE PATH "Built Yabause tree (with Musashi and Play JIT)")
find_package(SDL2 CONFIG REQUIRED)

enable_testing()
add_executable(scspmix_regression scspmix_regression.c)
set_target_properties(scspmix_regression PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(scspmix_regression PRIVATE ../../src)
target_compile_definitions(scspmix_regression PRIVATE HAVE_STDINT_H=1 HAVE_MUSASHI=1 HAVE_PLAY_JIT=1 HAVE_C99_VARIADIC_MACROS=1 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(scspmix_regression PRIVATE
    "${YABAUSE_BUILD_DIR}/src/$<CONFIG>/yabause.lib"
    "${YABAUSE_BUILD_DIR}/src/play/$<CONFIG>/CodeGen.lib"
    SDL2::SDL2 opengl32 glu32 vfw32)
add_custom_command(TARGET scspmix_regression POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:scspmix_regression>")
add_test(NAME scspmix_regression COMMAND scspmix_regression)
