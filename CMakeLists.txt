cmake_minimum_required(VERSION 3.20)
project(bilimusic_cpp VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# 依赖全部内嵌在 third_party/：imgui(MIT)、glfw(zlib, 源码编译)、nlohmann/json(MIT)。
# 不依赖 Qt / vcpkg / Visual Studio，MinGW-w64 + CMake 即可构建。
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw)
if (WIN32)
    target_link_libraries(imgui PUBLIC opengl32)
endif()

option(BILIMUSIC_CONSOLE "Build with a console window (development builds)" OFF)

# 应用版本号（exe 版本资源、启动日志、安装器共用）
add_compile_definitions(BILIMUSIC_VERSION="${PROJECT_VERSION}")

set(APP_SOURCES
    src/main.cpp
    src/app/App.cpp
    src/media/VlcEngine.cpp
    src/media/AppVolume.cpp
    src/media/AudioSpectrum.cpp
    src/data/LibraryStore.cpp
    src/util/AppLog.cpp
    src/util/Md5.cpp
    src/util/QrCode.cpp
    src/util/ShareLink.cpp
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    src/net/HttpClient.cpp
    src/bili/BiliService.cpp
    third_party/qrcodegen/qrcodegen.cpp
    src/ui/Theme.cpp
    src/ui/Components.cpp
    src/ui/ImageCache.cpp
    src/ui/EmojiAtlas.cpp
    src/platform/SystemTray.cpp
    src/ui/TitleBar.cpp
    src/ui/WindowFrame.cpp
    src/ui/Sidebar.cpp
    src/ui/PlaylistPage.cpp
    src/ui/PlayBar.cpp
)

if (BILIMUSIC_CONSOLE)
    add_executable(bilimusic ${APP_SOURCES})
else()
    add_executable(bilimusic WIN32 ${APP_SOURCES})
endif()

target_include_directories(bilimusic PRIVATE src third_party third_party/qrcodegen third_party/stb
                                  third_party/miniz)
target_compile_definitions(bilimusic PRIVATE MINIZ_NO_ARCHIVE_APIS)
target_link_libraries(bilimusic PRIVATE imgui)

if (MINGW)
    # 静态链接 gcc/stdc++/winpthread：否则 exe 依赖 MinGW 的运行时 DLL，
    # 双击运行时找不到 libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll 会直接闪退。
    target_link_options(bilimusic PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if (WIN32)
    # comdlg32 = 打开文件对话框；shell32 / ole32 留给后续托盘与拖放
    # d2d1/dwrite/windowscodecs：彩色 emoji（Segoe UI Emoji）光栅化
    target_link_libraries(bilimusic PRIVATE comdlg32 shell32 ole32 uuid user32 gdi32 winhttp
        advapi32 d2d1 dwrite windowscodecs)
endif()

# Windows exe 图标资源（与 Java 版同款 bilimusic.ico）
if (WIN32)
    enable_language(RC)
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/icons/bilimusic.ico")
    configure_file(src/resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(bilimusic PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

# 图标字体在运行时加载：复制到 exe 旁边的 assets/（和 Java 版把资源打进 jar 等价）
add_custom_command(TARGET bilimusic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:bilimusic>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/icons/MaterialIcons-Regular.ttf"
            "$<TARGET_FILE_DIR:bilimusic>/assets/MaterialIcons-Regular.ttf"
    COMMENT "Copying icon font next to the executable")
