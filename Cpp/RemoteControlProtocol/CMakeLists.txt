message(-RemoteControlProtocol)

# Input files
FILE(GLOB_RECURSE sources src/*.c*)
FILE(GLOB_RECURSE headers src/*.h*)


# Filters

# Project
add_library(RemoteControlProtocol STATIC ${sources} ${headers})