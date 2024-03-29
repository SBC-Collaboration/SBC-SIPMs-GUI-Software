include(cmake/CPM.cmake)

set(SIPM_ANALYSIS_LIB_DIR ../../../../SiPMCharacterization/GUIAnalysisTools
        CACHE FILEPATH "Directory of where the SiPM Characterization/analysis lib is located" )

# OpenGL
find_package(OpenGL REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Armadillo 11.2.3 REQUIRED)

# What is SYSTEM?
# From CMAKE documentation
# If the SYSTEM option is given, the compiler will be told the directories are
# meant as system include directories on some platforms. Signalling this
# setting might achieve effects such as the compiler skipping warnings, or
# these fixed-install system files not being considered in dependency
# calculations - see compiler docs.
include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})

# GL3W
CPMAddPackage("gh:skaslev/gl3w#5f8d7fd")

# GLFW
CPMAddPackage(NAME glfw
  GIT_TAG dd8a678
  GITHUB_REPOSITORY glfw/glfw
  OPTIONS "GLFW_BUILD_EXAMPLES OFF" "GLFW_BUILD_TESTS OFF" "GLFW_BUILD_DOCS OFF"
  "GLFW_INSTALL OFF" "GLFW_DOCUMENT_INTERNALS OFF")

# Libraries
if(USE_VULKAN)
  find_package(Vulkan REQUIRED)
  set(IMGUI_LIBRARIES "${IMGUI_LIBRARIES};Vulkan::Vulkan")
  # include_directories( ${VULKAN_INCLUDE_DIR} )
  include_directories(SYSTEM ${GLFW_DIR}/deps)
endif()

include(cmake/ImGUIDependencies.cmake)

# serial
# https://github.com/wjwwood/serial
CPMAddPackage(NAME serial
  GIT_TAG 1.2.1
  GITHUB_REPOSITORY wjwwood/serial
  DOWNLOAD_ONLY YES)

if(serial_ADDED)
  add_library(serial STATIC
    $<BUILD_INTERFACE:${serial_SOURCE_DIR}>/src/serial.cc
    $<BUILD_INTERFACE:${serial_SOURCE_DIR}>/src/impl/win.cc
    $<BUILD_INTERFACE:${serial_SOURCE_DIR}>/src/impl/unix.cc
    $<BUILD_INTERFACE:${serial_SOURCE_DIR}>/src/impl/list_ports/list_ports_win.cc
    $<BUILD_INTERFACE:${serial_SOURCE_DIR}>/src/impl/list_ports/list_ports_linux.cc
    $<BUILD_INTERFACE:${serial_SOURCE_DIR}>/src/impl/list_ports/list_ports_osx.cc)
  if(LINUX)
    target_link_libraries(serial PRIVATE)
  else()
    target_link_libraries(serial PRIVATE setupapi)
  endif()
  target_include_directories(serial PUBLIC SYSTEM
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${serial_SOURCE_DIR}>/include)
endif()

# Use vulkan/opengl32 headers from glfw:
# include_directories(${GLFW_DIR}/deps)

# json serializer
# https://github.com/nlohmann/json
CPMAddPackage("gh:nlohmann/json@3.10.5")

# spdlog
# https://github.com/gabime/spdlog
CPMAddPackage(NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  VERSION 1.8.2)

# readerwriterqueue
CPMAddPackage("gh:cameron314/readerwriterqueue@1.0.6")

# concurrentqueue
# its a multi-user of the above library
# We use the download_only option here because the library has some warnings
# which drown this project warnings
CPMAddPackage(NAME concurrentqueue
  GITHUB_REPOSITORY cameron314/concurrentqueue
  VERSION 1.0.3
  DOWNLOAD_ONLY YES)
if(concurrentqueue_ADDED)
  add_library(concurrentqueue INTERFACE IMPORTED)
  target_include_directories(concurrentqueue SYSTEM INTERFACE
    $<BUILD_INTERFACE:${concurrentqueue_SOURCE_DIR}>/)
endif()

CPMAddPackage(NAME tomlplusplus
  VERSION 3.2.0
  GITHUB_REPOSITORY marzer/tomlplusplus
  DOWNLOAD_ONLY YES)
if(tomlplusplus_ADDED)
  add_library(tomlplusplus INTERFACE IMPORTED)
  target_include_directories(tomlplusplus SYSTEM INTERFACE
    $<BUILD_INTERFACE:${tomlplusplus_SOURCE_DIR}>/)
endif()

CPMAddPackage(NAME date
  VERSION 3.0.1
  GITHUB_REPOSITORY HowardHinnant/date
  DOWNLOAD_ONLY YES)
if(date_ADDED)
  add_library(date INTERFACE IMPORTED)
  target_include_directories(date SYSTEM INTERFACE
    $<BUILD_INTERFACE:${date_SOURCE_DIR}>/include)
endif()

CPMAddPackage(NAME Sipmanalysis SOURCE_DIR ${SIPM_ANALYSIS_LIB_DIR})

# CPMAddPackage(NAME Sipmanalysis SOURCE_DIR ../../../../SiPMCharacteriazation)
