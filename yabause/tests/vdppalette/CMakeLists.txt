cmake_minimum_required(VERSION 3.10)
project(vdppalette_regression C)
enable_testing()
set(palette_sources ../../src/vdppalette.c ../../src/vdp1debug.c ../../src/vdp2vram.c)
add_executable(vdppalette_regression vdppalette_regression.c ${palette_sources})
target_include_directories(vdppalette_regression PRIVATE ../../src)
target_compile_definitions(vdppalette_regression PRIVATE HAVE_STDINT_H=1 HAVE_C99_VARIADIC_MACROS=1)
add_test(NAME vdppalette_regression COMMAND vdppalette_regression)

option(VDPPALETTE_TEST_QT "Build the Qt palette viewer integration test" OFF)
if(VDPPALETTE_TEST_QT)
    enable_language(CXX)
    find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL Multimedia)
    set(CMAKE_AUTOMOC ON)
    add_executable(vdppalette_qt vdppalette_qt.cpp ${palette_sources}
        ../../src/qt/ui/UIVdpPaletteViewer.cpp ../../src/qt/ui/UIVdpPaletteViewer.h)
    target_include_directories(vdppalette_qt PRIVATE ../../src ../../src/qt)
    target_compile_definitions(vdppalette_qt PRIVATE HAVE_STDINT_H=1 HAVE_C99_VARIADIC_MACROS=1)
    target_link_libraries(vdppalette_qt PRIVATE Qt5::Widgets Qt5::OpenGL Qt5::Multimedia)
    add_test(NAME vdppalette_qt COMMAND vdppalette_qt --test -platform offscreen)
    if(WIN32)
        get_target_property(_qmake Qt5::qmake IMPORTED_LOCATION)
        get_filename_component(_qtbin "${_qmake}" DIRECTORY)
        add_custom_command(TARGET vdppalette_qt POST_BUILD
            COMMAND "${_qtbin}/windeployqt.exe" --no-translations --no-compiler-runtime
                --no-opengl-sw "$<TARGET_FILE:vdppalette_qt>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_qtbin}/../plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:vdppalette_qt>/platforms")
    endif()
endif()
