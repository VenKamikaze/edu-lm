add_library(edu-protocol STATIC EduProtocol.cpp)
target_include_directories(edu-protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(edu-protocol PUBLIC Qt${QT_MAJOR_VERSION}::Core)
