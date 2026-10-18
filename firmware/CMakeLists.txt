# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-error=missing-braces)
add_compile_options(-Wno-unused-variable)

# The image's app description carries FIRMWARE_VERSION, so the Command Center can read the version of
# a .bin file before updating (fw.*, R6.1). keyboard_config.h stays the one place to change it.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/main/keyboard_config.h" _fw_line REGEX "#define FIRMWARE_VERSION ")
string(REGEX REPLACE ".*\"([^\"]*)\".*" "\\1" PROJECT_VER "${_fw_line}")

project(DeepDeck)

message(STATUS "Building: root")