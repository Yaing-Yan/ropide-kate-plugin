cmake_minimum_required(VERSION 3.16)

project(ropide-kate-plugin
    VERSION 0.1.2
    DESCRIPTION "RopIDE for Kate - CASIO fx-991 CN X .rop editor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)
# 逐组件查找（不依赖 KF6 umbrella config，Arch 上无需额外元包）
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6TextEditor REQUIRED)
find_package(KF6XmlGui REQUIRED)

add_library(ropidekate MODULE
    src/rop.cpp
    src/compiler.cpp
    src/settings.cpp
    src/presets.cpp
    src/market.cpp
    src/emu.cpp
    src/ropcodeeditor.cpp
    src/roptoolview.cpp
    src/ropideplugin.cpp
    data/resources.qrc
)

set_target_properties(ropidekate PROPERTIES PREFIX "")
target_link_libraries(ropidekate PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::I18n
    KF6::TextEditor
    KF6::XmlGui
)

# 测试：编译器移植正确性（与 node 运行的 media/compiler.js 对拍）
add_executable(ropide-test-compiler EXCLUDE_FROM_ALL tests/test_compiler.cpp
    src/rop.cpp src/compiler.cpp)
target_include_directories(ropide-test-compiler PRIVATE src)
target_link_libraries(ropide-test-compiler PRIVATE Qt6::Core)

# 测试宿主：不装进 Kate 也能冒烟测试插件（加载插件 → 创建工具视图）
add_executable(ropide-test-host EXCLUDE_FROM_ALL tests/test_host.cpp
    src/rop.cpp src/compiler.cpp src/settings.cpp src/presets.cpp src/market.cpp
    src/emu.cpp src/ropcodeeditor.cpp src/roptoolview.cpp src/ropideplugin.cpp
    data/resources.qrc)
target_include_directories(ropide-test-host PRIVATE src)
target_link_libraries(ropide-test-host PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    KF6::CoreAddons KF6::ConfigCore KF6::I18n KF6::TextEditor KF6::XmlGui)

# 插件安装目录：默认随前缀（Arch 为 /usr/lib/qt6/plugins/kf6/ktexteditor），
# 用户安装可用 -DROPIDE_LIBDIR=lib64 之类覆盖，或直接用仓库里的 install.sh。
set(ROPIDE_LIBDIR "lib" CACHE STRING "Library dir name under the prefix (lib / lib64)")
set(ROPIDE_PLUGIN_DIR "${ROPIDE_LIBDIR}/qt6/plugins/kf6/ktexteditor" CACHE STRING
    "Install directory (relative to prefix) for the Kate plugin")
install(TARGETS ropidekate DESTINATION ${ROPIDE_PLUGIN_DIR})
