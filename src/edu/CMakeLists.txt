set(EDU_CONFIG_FILE "${CMAKE_INSTALL_FULL_SYSCONFDIR}/edu-lm/edu-lm.conf" CACHE PATH "Path of the edu-lm config file")
set(EDU_CONFIG_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/edu-lm/edu-lm.conf.d" CACHE PATH "Path of the edu-lm config drop-in directory")
set(EDU_QUESTIONS_DIR "${CMAKE_INSTALL_FULL_DATADIR}/edu-lm/questions" CACHE PATH "Directory of installed question packs")
set(EDU_EXTRA_QUESTIONS_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/edu-lm/questions.d" CACHE PATH "Directory of site-specific question packs")
configure_file(EduConstants.h.in EduConstants.h @ONLY)

add_library(edu-core STATIC
    core/Answer.cpp
    core/CooldownTracker.cpp
    core/Generators.cpp
    core/QuestionBank.cpp
    core/QuizPolicy.cpp
    core/QuizRules.cpp
    core/QuizSession.cpp)
target_include_directories(edu-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(edu-core PUBLIC Qt${QT_MAJOR_VERSION}::Core)

add_subdirectory(protocol)
add_subdirectory(daemon)
add_subdirectory(greeter)

add_executable(edu-lm-validate tools/edu-lm-validate.cpp)
target_include_directories(edu-lm-validate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(edu-lm-validate PRIVATE edu-core)
install(TARGETS edu-lm-validate DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Root-only, as the packs hold answer keys
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/edu/questions/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/edu-lm/questions"
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE
        DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        FILES_MATCHING PATTERN "*.json")

add_subdirectory(tests)
