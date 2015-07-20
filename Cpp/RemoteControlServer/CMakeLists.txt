#-------------------------------------------------------------------------------
# Remote Control Server
# The core classes of the server application.
#-------------------------------------------------------------------------------

message("-RemoteControlServer")

# Input files
FILE(GLOB_RECURSE sources *.c*)
FILE(GLOB_RECURSE headers *.h*)

# Filters



# Project
add_library(RemoteControlServer STATIC ${sources} ${headers})