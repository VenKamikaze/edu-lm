configure_file("common/Constants.h.in" "common/Constants.h" IMMEDIATE @ONLY)

add_subdirectory(daemon)
add_subdirectory(greeter)
add_subdirectory(helper)
# EDU-LM: quiz gate logic and tools
enable_testing()
add_subdirectory(edu)
