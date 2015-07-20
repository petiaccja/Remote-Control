#-------------------------------------------------------------------------------
# Remote Control Protocol
# A specialized network protocol implemented over UDP. Helps RC control connections.
#-------------------------------------------------------------------------------

message("-RemoteControlProtocol")

# Input files
FILE(GLOB_RECURSE sources *.c*)
FILE(GLOB_RECURSE headers *.h*)


# Filters

# Project
add_library(RemoteControlProtocol STATIC ${sources} ${headers})
set_property(TARGET RemoteControlProtocol PROPERTY CXX_STANDARD 11)

# Compiler defs
add_definitions(-DSFML_STATIC)

# Dependencies
if (REMCON_WINDOWS)
	set(ADDITIONAL_LINKS "ws2_32")
elseif (${REMCON_LINK_COMPILER} STREQUAL "gcc")
	set(ADDITIONAL_LINKS "pthread")
endif()

# Linker
target_link_libraries(RemoteControlProtocol 
	debug sfml-network-s-d
	debug sfml-system-s-d
	optimized sfml-network-s
	optimized sfml-system-s
	${ADDITIONAL_LINKS})



