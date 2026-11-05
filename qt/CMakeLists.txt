cmake_minimum_required(VERSION 3.21)

project(NTE-SceneSeeker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Multimedia Sql)

add_executable(NTE-SceneSeeker
    src/main.cpp
    src/AssetPaths.h
    src/Coordinates.h
    src/MapView.h
    src/MapView.cpp
    src/BackgroundCarousel.h
    src/BackgroundCarousel.cpp
    src/CoverTitleWidget.h
    src/CoverTitleWidget.cpp
    src/MapOverlay.h
    src/MapOverlay.cpp
    src/MultiplayerSession.h
    src/MultiplayerSession.cpp
    src/GameWindow.h
    src/GameWindow.cpp
    src/Logging.h
    src/Logging.cpp
    src/BgmPlayer.h
    src/BgmPlayer.cpp
    src/SoundPlayer.h
    src/SoundPlayer.cpp
    src/Score.h
    src/Score.cpp
    src/bank/MediaPack.h
    src/bank/MediaPack.cpp
    src/bank/QuestionBank.h
    src/bank/QuestionBank.cpp
)

target_link_libraries(NTE-SceneSeeker PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::Multimedia
    Qt6::Sql
)

if(MINGW)
    target_compile_options(NTE-SceneSeeker PRIVATE
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

set_target_properties(NTE-SceneSeeker PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "NTE-SceneSeeker"
)

# 构建后补齐 Network / Multimedia DLL，保证直接双击 exe 也能启动。
if(WIN32)
    add_custom_command(TARGET NTE-SceneSeeker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Network>
            $<TARGET_FILE:Qt6::Multimedia>
            $<TARGET_FILE:Qt6::Sql>
            $<TARGET_FILE_DIR:NTE-SceneSeeker>
    )

    # SQLite 驱动插件（读 questions.nteq 用）
    get_target_property(_nte_qt_core2 Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _nte_qt_core2)
        get_target_property(_nte_qt_core2 Qt6::Core IMPORTED_LOCATION)
    endif()
    if(_nte_qt_core2)
        get_filename_component(_nte_qt_bin2 "${_nte_qt_core2}" DIRECTORY)
        get_filename_component(_nte_qt_root2 "${_nte_qt_bin2}" DIRECTORY)
        set(_nte_sql_driver "${_nte_qt_root2}/plugins/sqldrivers/qsqlite.dll")
        if(EXISTS "${_nte_sql_driver}")
            add_custom_command(TARGET NTE-SceneSeeker POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:NTE-SceneSeeker>/sqldrivers
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_nte_sql_driver}"
                    $<TARGET_FILE_DIR:NTE-SceneSeeker>/sqldrivers
            )
        endif()
    endif()

    # 开发运行目录补齐多媒体插件与 FFmpeg 运行库（BGM / 音效依赖）。
    # 这些文件 Qt 安装里本来就有，只是不会自动出现在 build/qt 里。
    # 路径从 Qt6Core.dll 的实际位置推导，避免依赖未定义的变量。
    get_target_property(_nte_qt_core Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _nte_qt_core)
        get_target_property(_nte_qt_core Qt6::Core IMPORTED_LOCATION)
    endif()
    if(_nte_qt_core)
        get_filename_component(_nte_qt_bin "${_nte_qt_core}" DIRECTORY)
        get_filename_component(_nte_qt_root "${_nte_qt_bin}" DIRECTORY)
        set(_nte_qt_plugins "${_nte_qt_root}/plugins/multimedia")
        file(GLOB NTE_MEDIA_PLUGINS "${_nte_qt_plugins}/*.dll")
        file(GLOB NTE_FFMPEG_LIBS
            "${_nte_qt_bin}/avcodec-*.dll"
            "${_nte_qt_bin}/avformat-*.dll"
            "${_nte_qt_bin}/avutil-*.dll"
            "${_nte_qt_bin}/swresample-*.dll"
            "${_nte_qt_bin}/swscale-*.dll")
    endif()
    if(NTE_MEDIA_PLUGINS)
        add_custom_command(TARGET NTE-SceneSeeker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:NTE-SceneSeeker>/multimedia
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${NTE_MEDIA_PLUGINS}
                $<TARGET_FILE_DIR:NTE-SceneSeeker>/multimedia
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${NTE_FFMPEG_LIBS}
                $<TARGET_FILE_DIR:NTE-SceneSeeker>
        )
    endif()

    # 开发构建目录也做一次 windeployqt，避免直接双击 build/qt/exe 时
    # 从系统 PATH 误加载旧版 libstdc++ / 缺少 Qt 平台插件。
    if(_nte_qt_bin)
        set(_nte_windeployqt "${_nte_qt_bin}/windeployqt.exe")
        if(EXISTS "${_nte_windeployqt}")
            add_custom_command(TARGET NTE-SceneSeeker POST_BUILD
                COMMAND "${_nte_windeployqt}"
                    --release --compiler-runtime --no-translations --no-opengl-sw
                    --dir "$<TARGET_FILE_DIR:NTE-SceneSeeker>"
                    "$<TARGET_FILE:NTE-SceneSeeker>"
            )
        endif()
    endif()
endif()


