message("-Test")

# Input files
FILE(GLOB_RECURSE sources src/*.c*)
FILE(GLOB_RECURSE headers src/*.h*)

# Filters

# Project
add_executable(Test ${sources} ${headers})

# Dependencies
target_link_libraries(Test RemoteControlProtocol)