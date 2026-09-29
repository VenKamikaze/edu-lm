add_library(edu-daemon STATIC QuizGate.cpp PasswordVerifier.h)
target_include_directories(edu-daemon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(edu-daemon PUBLIC edu-core edu-protocol Qt${QT_MAJOR_VERSION}::Network)

# Needs SDDM's Auth and ConfigReader, so the sddm target compiles these itself; the daemon
# CMakeLists only links this target.
set(EDU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_library(edu-daemon-glue INTERFACE)
target_sources(edu-daemon-glue INTERFACE
    ${EDU_ROOT}/EduConfiguration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AuthVerifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GateFactory.cpp)
target_include_directories(edu-daemon-glue INTERFACE ${EDU_ROOT} ${CMAKE_BINARY_DIR}/src/edu)
target_link_libraries(edu-daemon-glue INTERFACE edu-daemon edu-protocol edu-core)
