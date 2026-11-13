cmake_minimum_required(VERSION 3.10)
project(sndsdl_regression C)
find_package(SDL2 CONFIG REQUIRED)
enable_testing()

# The deterministic test supplies the SDL device functions while compiling the
# production backend against SDL's real headers.
add_executable(sndsdl_regression sndsdl_regression.c ../../src/sndsdl.c)
target_include_directories(sndsdl_regression PRIVATE ../../src ${SDL2_INCLUDE_DIRS})
target_compile_definitions(sndsdl_regression PRIVATE
    HAVE_LIBSDL=1 HAVE_LIBSDL2=1 HAVE_STDINT_H=1 HAVE_C99_VARIADIC_MACROS=1 SDL_MAIN_HANDLED)
add_test(NAME sndsdl_regression COMMAND sndsdl_regression)

# Also exercise the callback on a real SDL audio thread, without playing sound.
add_executable(sndsdl_threaded sndsdl_threaded.c ../../src/sndsdl.c)
target_include_directories(sndsdl_threaded PRIVATE ../../src)
target_compile_definitions(sndsdl_threaded PRIVATE
    HAVE_LIBSDL=1 HAVE_LIBSDL2=1 HAVE_STDINT_H=1 HAVE_C99_VARIADIC_MACROS=1 SDL_MAIN_HANDLED)
target_link_libraries(sndsdl_threaded PRIVATE SDL2::SDL2)
if (MSVC)
    target_compile_definitions(sndsdl_regression PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(sndsdl_threaded PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if (WIN32)
    add_custom_command(TARGET sndsdl_threaded POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:sndsdl_threaded>"
        VERBATIM)
endif()
add_test(NAME sndsdl_threaded COMMAND sndsdl_threaded)
