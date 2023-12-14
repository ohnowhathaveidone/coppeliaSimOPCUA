cmake_minimum_required(VERSION 2.8.11)
project(simExtOpen62541Wrapper)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_CXX_STANDARD 11)

find_package(Boost REQUIRED)
find_package(Boost COMPONENTS regex REQUIRED)

# uncomment following if plugin uses Qt:
#find_package(Qt5Core REQUIRED)
#find_package(Qt5Gui REQUIRED)
#find_package(Qt5Widgets REQUIRED)
#find_package(Qt5PrintSupport REQUIRED)

if(NOT LIBPLUGIN_DIR)
    if(DEFINED ENV{COPPELIASIM_ROOT_DIR})
        set(LIBPLUGIN_DIR $ENV{COPPELIASIM_ROOT_DIR}/programming/libPlugin)
    else()
        set(LIBPLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libPlugin)
    endif()
endif()
set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
    ${LIBPLUGIN_DIR}/cmake)
find_package(CoppeliaSim 4.0.0.0 REQUIRED)

include_directories(${COPPELIASIM_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external)
include_directories(${Boost_INCLUDE_DIRS})

coppeliasim_generate_stubs(${CMAKE_CURRENT_BINARY_DIR}/generated XML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/callbacks.xml LUA_FILE ${CMAKE_CURRENT_SOURCE_DIR}/simExtPluginSkeletonNG.lua)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h ESCAPE_QUOTES)

set(SOURCES
    plugin.cpp
    ${COPPELIASIM_EXPORTED_SOURCES}
)
set(OBJS 
    ${OPEN62541OBJECTFILE}
)


add_library(simExtOpen62541Wrapper SHARED ${SOURCES})
target_link_libraries(simExtOpen62541Wrapper ${Boost_LIBRARIES} ${COPPELIASIM_LIBRARIES} ${OBJS} Boost::regex)
#target_link_libraries(simExtOpen62541Wrapper ${Boost_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::PrintSupport ${COPPELIASIM_LIBRARIES})
