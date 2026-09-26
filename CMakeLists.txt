cmake_minimum_required(VERSION 3.16)
project(TMD56Monitor VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(LIBSERIALPORT REQUIRED IMPORTED_TARGET libserialport)

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

set(TMD_WARNINGS -Wall -Wextra -Wpedantic)

set(TMD_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${TMD_GENERATED_DIR})
configure_file(src/version.h.in ${TMD_GENERATED_DIR}/version.h @ONLY)

set(TMD_RESOURCE_C ${CMAKE_CURRENT_BINARY_DIR}/tmd-resources.c)
add_custom_command(
    OUTPUT ${TMD_RESOURCE_C}
    COMMAND ${GLIB_COMPILE_RESOURCES}
            --target=${TMD_RESOURCE_C}
            --generate-source
            --sourcedir=${CMAKE_SOURCE_DIR}/resources
            ${CMAKE_SOURCE_DIR}/resources/resources.gresource.xml
    DEPENDS
        ${CMAKE_SOURCE_DIR}/resources/resources.gresource.xml
        ${CMAKE_SOURCE_DIR}/resources/style.css
        ${CMAKE_SOURCE_DIR}/resources/icons/tmd56-monitor.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/tmd56-monitor-128.png
    VERBATIM
)

function(tmd_apply_c_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${TMD_GENERATED_DIR}
    )
    target_compile_options(${target} PRIVATE ${TMD_WARNINGS})
    if(WIN32)
        target_compile_definitions(${target} PRIVATE _CRT_DECLARE_NONSTDC_NAMES=1)
    else()
        target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
endfunction()

if(WIN32)
    set(TMD_RUNTIME_DIR ".")
else()
    set(TMD_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}")
endif()

add_executable(tmd56-monitor
    src/main.c
    src/self_test.c
    src/app.c
    src/measurement.c
    src/logger.c
    src/history_buffer.c
    src/platform/user_paths.c
    src/sources/simulator_source.c
    src/sources/replay_parser.c
    src/sources/replay_source.c
    src/sources/tmd56_source.c
    src/ui/main_window.c
    src/ui/live_plot.c
    ${TMD_RESOURCE_C}
)
tmd_apply_c_target(tmd56-monitor)
set_source_files_properties(${TMD_RESOURCE_C} PROPERTIES COMPILE_OPTIONS "-Wno-pedantic")
target_link_libraries(tmd56-monitor PRIVATE
    PkgConfig::GTK4
    PkgConfig::GLIB2
    PkgConfig::CAIRO
    PkgConfig::LIBSERIALPORT
)
if(NOT WIN32)
    target_link_libraries(tmd56-monitor PRIVATE m)
endif()
if(WIN32)
    set(TMD_ICON_PATH "${CMAKE_SOURCE_DIR}/resources/icons/tmd56-monitor.ico")
    string(REPLACE "\\" "/" TMD_ICON_PATH "${TMD_ICON_PATH}")
    configure_file(
        packaging/windows/tmd56-monitor.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/tmd56-monitor.rc
        @ONLY
    )
    target_sources(tmd56-monitor PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/tmd56-monitor.rc)
    # Release builds are GUI programs. Debug builds keep a console for diagnostics.
    set_target_properties(tmd56-monitor PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release,MinSizeRel,RelWithDebInfo>
    )
endif()

install(TARGETS tmd56-monitor RUNTIME DESTINATION ${TMD_RUNTIME_DIR})

if(WIN32)
    install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
    install(CODE "
execute_process(
  COMMAND bash \"${CMAKE_SOURCE_DIR}/packaging/windows/bundle-runtime.sh\" \"\${CMAKE_INSTALL_PREFIX}\"
  RESULT_VARIABLE tmd_bundle_rc
)
if(NOT tmd_bundle_rc EQUAL 0)
  message(FATAL_ERROR \"Windows runtime bundling failed\")
endif()
")
else()
    install(FILES packaging/linux/tmd56-monitor.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/com.tmd56.Monitor.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES resources/icons/tmd56-monitor.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(size 16 22 24 32 48 64 128 256 512)
        install(FILES resources/icons/${size}x${size}/tmd56-monitor.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    endforeach()
    install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

include(packaging/cpack.cmake)

enable_testing()

add_executable(test_history_buffer
    tests/test_history_buffer.c
    src/history_buffer.c
    src/measurement.c
)
tmd_apply_c_target(test_history_buffer)
if(NOT WIN32)
    target_link_libraries(test_history_buffer PRIVATE m)
endif()
add_test(NAME history_buffer COMMAND test_history_buffer)

add_executable(test_replay_parser
    tests/test_replay_parser.c
    src/sources/replay_parser.c
    src/sources/replay_source.c
    src/measurement.c
)
tmd_apply_c_target(test_replay_parser)
target_compile_definitions(test_replay_parser PRIVATE
    TMD_EXAMPLE_PATH="${CMAKE_SOURCE_DIR}/examples/sample_tmd_export.txt"
    TMD_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
if(NOT WIN32)
    target_link_libraries(test_replay_parser PRIVATE m)
endif()
add_test(NAME replay_parser COMMAND test_replay_parser)

add_executable(test_logger
    tests/test_logger.c
    src/logger.c
    src/measurement.c
)
tmd_apply_c_target(test_logger)
if(NOT WIN32)
    target_link_libraries(test_logger PRIVATE m)
endif()
add_test(NAME logger COMMAND test_logger)

add_executable(test_acquisition
    tests/test_acquisition.c
    src/app.c
    src/measurement.c
    src/logger.c
    src/history_buffer.c
    src/platform/user_paths.c
    src/sources/simulator_source.c
    src/sources/replay_parser.c
    src/sources/replay_source.c
    src/sources/tmd56_source.c
)
tmd_apply_c_target(test_acquisition)
target_link_libraries(test_acquisition PRIVATE PkgConfig::GLIB2 PkgConfig::LIBSERIALPORT)
if(NOT WIN32)
    target_link_libraries(test_acquisition PRIVATE m)
endif()
add_test(NAME acquisition COMMAND test_acquisition)

add_executable(test_user_paths
    tests/test_user_paths.c
    src/platform/user_paths.c
)
tmd_apply_c_target(test_user_paths)
target_link_libraries(test_user_paths PRIVATE PkgConfig::GLIB2)
add_test(NAME user_paths COMMAND test_user_paths)
