message("-server [Cpp]")

cmake_minimum_required(VERSION 2.6)

# Solution properties
set(PROJECT_NAME Remote-Control-Server)

project( ${PROJECT_NAME} )

# Paths
set(INCLUDE_PATH "../Externals/include")
include_directories(${INCLUDE_PATH})
set(LIBRARY_PATH "../Externals/lib")
link_directories(${LIBRARY_PATH}/Win32)

# Solution build configs
if(CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_CONFIGURATION_TYPES Debug Release)
 endif()

# Add project to solution
add_subdirectory(RemoteControl)
add_subdirectory(RemoteControlProtocol)
add_subdirectory(Server)
add_subdirectory(ServoDriver)

