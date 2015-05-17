message("-RemoteControlProtocol")

# Input files
FILE(GLOB_RECURSE sources src/*.c*)
FILE(GLOB_RECURSE headers src/*.h*)


# Filters

# Project
add_library(RemoteControlProtocol STATIC ${sources} ${headers})
set_property(TARGET RemoteControlProtocol PROPERTY CXX_STANDARD 11)

# Compiler defs
add_definitions(-DSFML_STATIC)

# Dependencies
if (WINDOWS_BUILD)
	set(ADDITIONAL_LINKS "ws2_32")
elseif (${COMPILER} STREQUAL "gcc")
	set(ADDITIONAL_LINKS "pthread")
endif()

if (CMAKE_C_FLAGS_DEBUG)
	set(LINKS sfml-network-s-d sfml-system-s-d)
	message(debug)
else ()
	message(release)
	set(LINKS sfml-network-s sfml-system-s)
endif()


target_link_libraries(RemoteControlProtocol ${LINKS} ${ADDITIONAL_LINKS})



