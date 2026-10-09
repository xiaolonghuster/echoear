# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/echoear-core/CMakeLists.txt")
    message(FATAL_ERROR "EchoEar core is missing. Run: git submodule update --init --recursive")
endif()
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/external/echoear-core")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.5.1")
project(xiaozhi)
