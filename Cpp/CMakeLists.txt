message("-server [Cpp]")

cmake_minimum_required(VERSION 2.6)

# Solution properties
set(PROJECT_NAME Remote-Control-Server)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR} )
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR} )
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR} )

project( ${PROJECT_NAME} )

# Compiler defs
add_definitions(-DSFML_STATIC)

# Paths
set(INCLUDE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Externals/include)
include_directories(${INCLUDE_PATH})
set(LIBRARY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Externals/lib)
link_directories(${LIBRARY_PATH})


# Solution build configs
if(CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_CONFIGURATION_TYPES Debug Release)
endif()

# Add project to solution
add_subdirectory(RemoteControl)
add_subdirectory(RemoteControlProtocol)
add_subdirectory(Server)
add_subdirectory(ServoDriver)

