message("Remote Control Server [Cpp]")

cmake_minimum_required(VERSION 3.0)


# Solution properties
set(PROJECT_NAME Remote-Control-Server)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

project( ${PROJECT_NAME} )

# Platform detection
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(WINDOWS_BUILD 1)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(LINUX_BUILD 1)
endif ()

if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
	set(ARCH_64BIT 1)
else ()
	set(ARCH_32BIT 1)
endif ()

# Get platform/compiler/architecture specific link directory

# get platform
set(PLATFORM "any")
if (WINDOWS_BUILD)
	set(PLATFORM "win")
elseif (LINUX_BUILD)
	set(PLATFORM "linux")
endif () 

# get compiler
set(COMPILER "any")
if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
	set(COMPILER "gcc")
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
	message("MSVC " ${CMAKE_CXX_COMPILER_VERSION})
	if (MSVC12)
		set(COMPILER "msvc12")
	elseif (MSVC14)
		set(COMPILER "msvc14")
	endif ()
endif ()

# get architecture
if (ARCH_64BIT)
	set(ARCHITECTURE "x64")
elseif (ARCH_32BIT)
	set(ARCHITECTURE "x32")
elseif (NOT ARCHITECTURE)
	set(ARCHITECTURE "any")
endif ()

set(EXTERNAL_FOLDER ${PLATFORM}_${COMPILER}_${ARCHITECTURE})

# display platform
message("Target platform is: " ${PLATFORM})
message("Compiler is: " ${COMPILER})
message("Target architecture is: " ${ARCHITECTURE})

# Paths
set(INCLUDE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Externals/include)
include_directories(${INCLUDE_PATH})
set(LIBRARY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Externals/lib/${EXTERNAL_FOLDER})
link_directories(${LIBRARY_PATH})
link_directories(${CMAKE_BINARY_DIR}/bin)

message("External libs in: " ${LIBRARY_PATH})

# Solution build configs
if(CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_CONFIGURATION_TYPES Debug Release)
endif()


# Add project to solution
add_subdirectory(RemoteControl)
add_subdirectory(RemoteControlProtocol)
add_subdirectory(Server)
add_subdirectory(ServoDriver)
add_subdirectory(Test)

