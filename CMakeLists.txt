###
### CMake settings
###
cmake_minimum_required(VERSION 2.6)
cmake_policy(SET CMP0012 NEW)


###
### Project settings
###
project(SOS_OPT)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
set(SOS_OPT_VERSION_MAJOR "1")
set(SOS_OPT_VERSION_MINOR "1")
set(SOS_OPT_VERSION_PATCH "1")
set(SOS_OPT_VERSION "${SOS_OPT_VERSION_MAJOR}.${SOS_OPT_VERSION_MINOR}.${SOS_OPT_VERSION_PATCH}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

###
### Sources, headers, directories and libs
###
include_directories(${SOS_OPT_SOURCE_DIR}/include)
include_directories(${SOS_OPT_SOURCE_DIR}/src)

set(lib-sources
        "src/bidirectional-ibfs.cpp"
        "src/parametric-ibfs.cpp"
        "src/sospd.cpp"
        "src/source-ibfs.cpp"
        "src/submodular-functions.cpp"
        "src/submodular-ibfs.cpp"
)

set(lib-sources ${lib-sources})

###
### Build configuration
###

###
### Target: libsos-opt
###

add_library(sos-opt STATIC ${lib-sources})
target_compile_features(sos-opt PUBLIC cxx_std_11)
set_target_properties(sos-opt PROPERTIES
        CXX_EXTENSIONS OFF
)


###
### Subdirectories
###

add_subdirectory(test)
