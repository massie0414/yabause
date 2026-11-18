cmake_minimum_required(VERSION 3.10)
project(vdp2vram_regression C)
enable_testing()
add_executable(vdp2vram_regression vdp2vram_regression.c ../../src/vdp2vram.c)
target_include_directories(vdp2vram_regression PRIVATE ../../src)
target_compile_definitions(vdp2vram_regression PRIVATE HAVE_STDINT_H=1 HAVE_C99_VARIADIC_MACROS=1 _CRT_SECURE_NO_WARNINGS)
add_test(NAME vdp2vram_regression COMMAND vdp2vram_regression)

option(VDP2VRAM_TEST_QT "Build the Qt viewer integration test and snapshot preview" OFF)
if(VDP2VRAM_TEST_QT)
    enable_language(CXX)
    find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL Multimedia)
    set(CMAKE_AUTOMOC ON)
    add_executable(vdp2vram_qt vdp2vram_qt.cpp ../../src/qt/ui/UIVdp2VramViewer.cpp
        ../../src/qt/ui/UIVdp2VramViewer.h ../../src/vdp2vram.c)
    target_include_directories(vdp2vram_qt PRIVATE ../../src ../../src/qt)
    target_compile_definitions(vdp2vram_qt PRIVATE HAVE_STDINT_H=1 HAVE_C99_VARIADIC_MACROS=1 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(vdp2vram_qt PRIVATE Qt5::Widgets Qt5::OpenGL Qt5::Multimedia)
    add_test(NAME vdp2vram_qt COMMAND vdp2vram_qt -platform offscreen)
    if(WIN32)
        get_target_property(_qmake Qt5::qmake IMPORTED_LOCATION)
        get_filename_component(_qtbin "${_qmake}" DIRECTORY)
        add_custom_command(TARGET vdp2vram_qt POST_BUILD
            COMMAND "${_qtbin}/windeployqt.exe" --no-translations --no-compiler-runtime
                --no-opengl-sw "$<TARGET_FILE:vdp2vram_qt>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_qtbin}/../plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:vdp2vram_qt>/platforms")
    endif()
endif()
