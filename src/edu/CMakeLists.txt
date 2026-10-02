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

# Reads config exactly as the daemon does, so it compiles the same config sources.
add_library(edu-check STATIC tools/EduCheck.cpp EduConfiguration.cpp ${CMAKE_SOURCE_DIR}/src/common/ConfigReader.cpp)
target_include_directories(edu-check PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/src/common)
target_link_libraries(edu-check PUBLIC edu-core)
add_executable(edu-lm-check tools/edu-lm-check.cpp)
target_link_libraries(edu-lm-check PRIVATE edu-check)
install(TARGETS edu-lm-check DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Configuration. The main file is comments only: it is read after the drop-in directory,
# so any key set in it would override the drop-ins. Full paths, because the relative
# CMAKE_INSTALL_SYSCONFDIR would land in /usr/etc with a /usr prefix.
get_filename_component(EDU_CONFIG_FILE_DIR "${EDU_CONFIG_FILE}" DIRECTORY)
get_filename_component(EDU_CONFIG_FILE_NAME "${EDU_CONFIG_FILE}" NAME)
configure_file("${CMAKE_SOURCE_DIR}/data/edu/edu-lm.conf.in" "${CMAKE_CURRENT_BINARY_DIR}/edu-lm.conf" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/edu-lm.conf" DESTINATION "${EDU_CONFIG_FILE_DIR}"
        RENAME "${EDU_CONFIG_FILE_NAME}"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
# Named explicitly so a restrictive build umask cannot make them private
foreach(dir "${EDU_CONFIG_FILE_DIR}" "${CMAKE_INSTALL_FULL_DATADIR}/edu-lm" "${CMAKE_INSTALL_FULL_DATADIR}/edu-lm/examples")
    install(DIRECTORY DESTINATION "${dir}"
            DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
install(DIRECTORY DESTINATION "${EDU_CONFIG_DIR}"
        DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
# Root-only: site packs hold answer keys
install(DIRECTORY DESTINATION "${EDU_EXTRA_QUESTIONS_DIR}"
        DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

# SDDM reads this after Arch's default.conf in the same directory (files are read in name
# order). /etc/sddm.conf.d and /etc/sddm.conf still override it.
install(FILES "${CMAKE_SOURCE_DIR}/data/edu/sddm-edu-lm.conf" DESTINATION "${SYSTEM_CONFIG_DIR}"
        RENAME edu-lm.conf)

if(SYSTEMD_FOUND AND DEFINED SYSTEMD_SYSUSERS_DIR)
    install(FILES "${CMAKE_SOURCE_DIR}/data/edu/edu-lm-sysusers.conf" DESTINATION "${SYSTEMD_SYSUSERS_DIR}"
            RENAME edu-lm.conf)
endif()

# Opt-in examples, not active after install
install(FILES "${CMAKE_SOURCE_DIR}/data/edu/pam/edu-lm-access.conf"
        "${CMAKE_SOURCE_DIR}/data/edu/examples/example-pack.json"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/edu-lm/examples")

# Root-only, as the packs hold answer keys
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/edu/questions/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/edu-lm/questions"
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE
        DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        FILES_MATCHING PATTERN "*.json")

add_subdirectory(tests)
