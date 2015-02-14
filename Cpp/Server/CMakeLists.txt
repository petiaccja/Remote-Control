message(-Server)

# Input files
FILE(GLOB_RECURSE sources src/*.c*)
FILE(GLOB_RECURSE headers src/*.h*)

# Filters



# Project
add_executable(Server ${sources} ${headers})