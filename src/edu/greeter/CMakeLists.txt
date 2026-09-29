add_library(edu-greeter STATIC QuizClient.cpp MockDaemon.cpp)
target_include_directories(edu-greeter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(edu-greeter PUBLIC edu-daemon edu-protocol edu-core Qt${QT_MAJOR_VERSION}::Network)

# Needs SDDM's GreeterProxy and ConfigReader, so the greeter target compiles these itself; the
# greeter CMakeLists only links this target.
set(EDU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_library(edu-greeter-glue INTERFACE)
target_sources(edu-greeter-glue INTERFACE
    ${EDU_ROOT}/EduConfiguration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GreeterGlue.cpp)
target_include_directories(edu-greeter-glue INTERFACE
    ${EDU_ROOT} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/src/edu ${CMAKE_SOURCE_DIR}/src/greeter)
target_compile_definitions(edu-greeter-glue INTERFACE
    EDU_LM_SAMPLE_QUESTIONS_DIR="${CMAKE_SOURCE_DIR}/data/edu/questions")
target_link_libraries(edu-greeter-glue INTERFACE edu-greeter)
