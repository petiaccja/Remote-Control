message("-RemoteControlProtocol")

# Input files
FILE(GLOB_RECURSE sources src/*.c*)
FILE(GLOB_RECURSE headers src/*.h*)


# Filters

# Project
add_library(RemoteControlProtocol STATIC ${sources} ${headers})

# Dependencies
target_link_libraries(RemoteControlProtocol freetype glew jpeg openal32 sfml-audio-s-d sfml-graphics-s-d sfml-main-d sfml-network-s-d sfml-system-s-d sfml-window-s-d sndfile ws2_32)