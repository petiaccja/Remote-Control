#-------------------------------------------------------------------------------
# Remote Control Server root CMake file
# Contains 
#	- platform detection
#	- solution configurations
#	- paths
# Additional subdirectories
#-------------------------------------------------------------------------------

message("Remote Control Server [Cpp]")

cmake_minimum_required(VERSION 3.2)


#---------------------------------------
# Create project
#---------------------------------------
set(PROJECT_NAME Remote-Control-Server)

project( ${PROJECT_NAME} )


#---------------------------------------
# Project properties, build configs
#---------------------------------------

# set output paths
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

# set build configurations
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE TYPE INTERNAL FORCE )


#---------------------------------------
# Platform detection
#---------------------------------------
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(REMCON_WINDOWS 1)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(REMCON_LINUX 1)
endif ()

if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
	set(REMCON_64BIT 1)
else ()
	set(REMCON_32BIT 1)
endif ()


#---------------------------------------
# Preprocessor definitions
#---------------------------------------
if (REMCON_64BIT)
	add_definitions(-DREMCON_64BIT)
else ()
	add_definitions(-DREMCON_32BIT)
endif()

if(REMCON_WINDOWS)
	add_definitions(-DREMCON_WINDOWS)
elseif(REMCON_LINUX)
	add_definitions(-DREMCON_LINUX)
endif()

#---------------------------------------
# Include and link paths
#---------------------------------------

include_directories(${CMAKE_CURRENT_LIST_DIR}/Externals/include)

# get platform
if (REMCON_WINDOWS)
	set(REMCON_LINK_OS "win")
elseif (REMCON_LINUX)
	set(REMCON_LINK_OS "linux")
else ()
	set(REMCON_LINK_OS "unknown")
endif () 

# get compiler
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	set(REMCON_LINK_COMPILER "gcc")
elseif ("MSVC" STREQUAL "${CMAKE_CXX_COMPILER_ID}")
	if (MSVC12)
		set(REMCON_LINK_COMPILER "msvc12")
	elseif (MSVC14)
		set(REMCON_LINK_COMPILER "msvc14")
	endif ()
else ()
	set(REMCON_LINK_COMPILER "unknown")
endif ()

# get architecture
if (REMCON_64BIT)
	set(REMCON_LINK_ARCHITECTURE "x64")
elseif (REMCON_32BIT)
	set(REMCON_LINK_ARCHITECTURE "x32")
else ()
	set(REMCON_LINK_ARCHITECTURE "unknown")
endif ()

# display information 
set(REMCON_LINK_PLATFORM "${REMCON_LINK_OS}_${REMCON_LINK_COMPILER}_${REMCON_LINK_ARCHITECTURE}")
message("Looking for externals in: ${REMCON_LINK_PLATFORM}")

# set up externals path
if ((${REMCON_LINK_PLATFORM} MATCHES ".*unknown.*") OR NOT IS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/Externals/lib/${REMCON_LINK_PLATFORM})
	set(REMCON_LINK_PATH custom)
else ()
	set(REMCON_LINK_PATH ${CMAKE_CURRENT_LIST_DIR}/Externals/lib/${REMCON_LINK_PLATFORM})
endif ()

link_directories(${REMCON_LINK_PATH})

message("External link path: ${REMCON_LINK_PATH}")

#---------------------------------------
# Add sub-folders
#---------------------------------------
include_directories(${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(RemoteControlServer)
add_subdirectory(RemoteControlProtocol)
add_subdirectory(Server)
add_subdirectory(ServoDriver)
add_subdirectory(Test)



