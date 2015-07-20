#-------------------------------------------------------------------------------
# Test project.
# Contains various tests for modules or classes of the whole solution.
#-------------------------------------------------------------------------------

message("-Test")

# Input files
FILE(GLOB_RECURSE sources *.c*)
FILE(GLOB_RECURSE headers *.h*)

# Filters

# Project
add_executable(Test ${sources} ${headers})
set_property(TARGET Test PROPERTY CXX_STANDARD 11)

# Dependencies
if (REMCON_LINK_COMPILER STREQUAL "gcc")
	set(ADDITIONAL_LINKS pthread)
endif()

target_link_libraries(Test RemoteControlProtocol RemoteControlServer ${ADDITIONAL_LINKS})
