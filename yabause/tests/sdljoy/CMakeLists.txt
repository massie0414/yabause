cmake_minimum_required(VERSION 3.10)
project(sdljoy_regression C)

find_package(SDL2 2.0.14 CONFIG REQUIRED)
enable_testing()

# Link the actual input backend and Saturn controller implementation, without
# booting the emulator or requiring a BIOS, a game, or a physical controller.
add_executable(sdljoy_regression
    sdljoy_regression.c
    ../../src/persdljoy.c
    ../../src/peripheral.c)
target_compile_definitions(sdljoy_regression PRIVATE
    HAVE_LIBSDL=1 HAVE_LIBSDL2=1 HAVE_STDINT_H=1 HAVE_C99_VARIADIC_MACROS=1 SDL_MAIN_HANDLED)
target_include_directories(sdljoy_regression PRIVATE ../../src)
target_link_libraries(sdljoy_regression PRIVATE SDL2::SDL2)
if (MSVC)
    target_compile_definitions(sdljoy_regression PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if (WIN32)
    add_custom_command(TARGET sdljoy_regression POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:sdljoy_regression>"
        VERBATIM)
endif()
add_test(NAME sdljoy_regression COMMAND sdljoy_regression)
