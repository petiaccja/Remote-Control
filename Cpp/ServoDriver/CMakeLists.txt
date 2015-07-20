#-------------------------------------------------------------------------------
# Servo Driver.
# Code to run servos on the GPIO pins.
#-------------------------------------------------------------------------------

message("-ServoDriver")

# Input files
FILE(GLOB_RECURSE sources *.c*)
FILE(GLOB_RECURSE headers *.h*)

# Filters



# Project
add_library(ServoDriver STATIC ${sources} ${headers})