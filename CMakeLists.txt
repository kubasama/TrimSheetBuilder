cmake_minimum_required(VERSION 3.21)
project(TrimSheet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus) # desktop accent color via xdg-desktop-portal
endif()

# libzip: CMake package on Windows (vcpkg), pkg-config elsewhere
# (some distros ship a broken libzip CMake config without the zip tools)
if(WIN32)
    find_package(libzip CONFIG REQUIRED)
    set(LIBZIP_TARGET libzip::zip)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
    set(LIBZIP_TARGET PkgConfig::LIBZIP)
endif()

if(MSVC)
    add_compile_options(/utf-8 /permissive-)
endif()

# --- core: model, save/load, compositing, export (no widgets) ---------------
add_library(trimsheet_core STATIC
    src/core/Model.h
    src/core/Project.h        src/core/Project.cpp
    src/core/Commands.h       src/core/Commands.cpp
    src/core/Serializer.h     src/core/Serializer.cpp
    src/core/Compositor.h     src/core/Compositor.cpp
    src/core/Exporter.h       src/core/Exporter.cpp
    src/core/LayoutPresets.h  src/core/LayoutPresets.cpp
    src/core/ChannelPresets.h src/core/ChannelPresets.cpp
    src/io/TextureWatcher.h   src/io/TextureWatcher.cpp
)
target_include_directories(trimsheet_core PUBLIC src)
target_link_libraries(trimsheet_core PUBLIC Qt6::Core Qt6::Gui ${LIBZIP_TARGET})

# --- app ---------------------------------------------------------------------
qt_add_executable(trimsheet
    src/main.cpp
    src/io/ClipboardImport.h  src/io/ClipboardImport.cpp
    src/gui/MainWindow.h      src/gui/MainWindow.cpp
    src/gui/SheetView.h       src/gui/SheetView.cpp
    src/gui/ZoneItem.h        src/gui/ZoneItem.cpp
    src/gui/InspectorPanel.h  src/gui/InspectorPanel.cpp
    src/gui/NewSheetDialog.h  src/gui/NewSheetDialog.cpp
    src/gui/ExportPanel.h     src/gui/ExportPanel.cpp
    src/gui/OverlayBar.h      src/gui/OverlayBar.cpp
    src/gui/Snapper.h         src/gui/Snapper.cpp
    src/gui/Theme.h           src/gui/Theme.cpp
    src/gui/ChannelEditor.h   src/gui/ChannelEditor.cpp
    src/gui/Dialogs.h         src/gui/Dialogs.cpp
    src/gui/DragValue.h       src/gui/DragValue.cpp
    src/gui/AppIcon.h         src/gui/AppIcon.cpp
    src/gui/SliderField.h     src/gui/SliderField.cpp
    src/gui/Icons.h           src/gui/Icons.cpp
)
target_link_libraries(trimsheet PRIVATE trimsheet_core Qt6::Widgets)
if(TARGET Qt6::DBus)
    target_link_libraries(trimsheet PRIVATE Qt6::DBus)
endif()
target_compile_definitions(trimsheet PRIVATE TRIMSHEET_VERSION="${PROJECT_VERSION}")

if(WIN32)
    # GUI app (no console window), with the icon embedded in the .exe
    set_target_properties(trimsheet PROPERTIES WIN32_EXECUTABLE ON)
    target_sources(trimsheet PRIVATE packaging/windows/trimsheet.rc)
    install(TARGETS trimsheet RUNTIME DESTINATION .)
else()
    include(GNUInstallDirs)
    install(TARGETS trimsheet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/trimsheet.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/trimsheet.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES packaging/trimsheet.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

# --- tests -------------------------------------------------------------------
option(TRIMSHEET_BUILD_TESTS "Build unit tests" ON)
if(TRIMSHEET_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(test_core tests/test_core.cpp src/gui/Snapper.cpp)
    target_link_libraries(test_core PRIVATE trimsheet_core Qt6::Test)
    add_test(NAME test_core COMMAND test_core)
endif()
