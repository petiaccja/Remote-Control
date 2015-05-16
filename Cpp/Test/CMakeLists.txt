message("-Test")

# Input files
FILE(GLOB_RECURSE sources src/*.c*)
FILE(GLOB_RECURSE headers src/*.h*)

# Filters

# Project
add_executable(Test ${sources} ${headers})
set_property(TARGET Test PROPERTY CXX_STANDARD 11)

# Dependencies
if (COMPILER STREQUAL gcc)
	set(ADDITIONAL_LINKS pthread)
endif()

target_link_libraries(Test RemoteControlProtocol ${ADDITIONAL_LINKS})
