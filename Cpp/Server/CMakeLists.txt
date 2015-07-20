#-------------------------------------------------------------------------------
# Server
# The main program to connect the libraries and run the server.
#-------------------------------------------------------------------------------

message("-Server")

# Input files
FILE(GLOB_RECURSE sources *.c*)
FILE(GLOB_RECURSE headers *.h*)

# Filters

# Project
add_executable(Server ${sources} ${headers})
set_property(TARGET Server PROPERTY CXX_STANDARD 11)

# Dependencies
target_link_libraries(Server RemoteControlProtocol)
