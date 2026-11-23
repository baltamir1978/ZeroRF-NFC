cmake_minimum_required(VERSION 3.22)
project(zeronfc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# --- LVGL (interfaz) ---
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)

# LVGL "lean": sólo software + framebuffer, sin librerías externas.
target_compile_definitions(lvgl PUBLIC USE_DESKTOP=0)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
    target_compile_options(lvgl PRIVATE -Wno-stringop-overflow)
endif()

# --- Ejecutable ---
add_executable(${PROJECT_NAME}
    src/ui/gui_main.cpp
    src/ui/evdev_keypad.cpp
    src/ui/nfc_ops.cpp
    src/st25r3916.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${lvgl_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
)
target_link_libraries(${PROJECT_NAME} PRIVATE lvgl pthread m)

# --- Instalación / empaquetado .deb ---
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(APP_PACKAGE_DESCRIPTION "RF / NFC reader for the Cardputer Zero (ST25R3916)")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/app.desktop.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.desktop" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/applications")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/store/icon.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/share/images"
    RENAME "zeronfc.png")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/zeronfc-sudoers"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/zeronfc")

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "ZeroRF-NFC")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "ZeroRF-NFC_${PROJECT_VERSION}_rev1_arm64")
set(CPACK_DEBIAN_PACKAGE_NAME "zerorf-nfc")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "baltamir1978 <baltamir1978@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_PACKAGE_DESCRIPTION}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libgcc-s1, sudo")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/prerm")
include(CPack)
