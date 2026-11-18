cmake_minimum_required(VERSION 3.10)
project(cdtransfer_regression C CXX)
set(YABAUSE_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/vs2026" CACHE PATH "Built Yabause tree (with Musashi and Play JIT)")
find_package(SDL2 CONFIG REQUIRED)

enable_testing()
foreach(test_target cdtransfer_regression scudma_regression)
add_executable(${test_target} ${test_target}.c)
set_target_properties(${test_target} PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(${test_target} PRIVATE ../../src)
target_compile_definitions(${test_target} PRIVATE HAVE_STDINT_H=1 HAVE_MUSASHI=1 HAVE_PLAY_JIT=1 HAVE_C99_VARIADIC_MACROS=1 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(${test_target} PRIVATE
    "${YABAUSE_BUILD_DIR}/src/$<CONFIG>/yabause.lib"
    "${YABAUSE_BUILD_DIR}/src/play/$<CONFIG>/CodeGen.lib"
    SDL2::SDL2 opengl32 glu32 vfw32)
add_custom_command(TARGET ${test_target} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:${test_target}>")
add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()

