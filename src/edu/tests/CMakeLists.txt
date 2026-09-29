set(EDU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(EDU_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/..")
# EduConfiguration.cpp is compiled with ConfigReader.cpp here only, because
# the daemon and greeter compile ConfigReader.cpp themselves.
set(EDU_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(EDU_SAMPLE_DIR "${CMAKE_SOURCE_DIR}/data/edu/questions")

function(edu_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${EDU_DIR} ${EDU_BINARY_DIR})
    target_compile_definitions(${name} PRIVATE
        EDU_SAMPLE_DIR="${EDU_SAMPLE_DIR}" EDU_TEST_DATA_DIR="${EDU_TEST_DATA_DIR}")
    # tests use plain string literals; the library code stays strict
    target_compile_options(${name} PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_FROM_BYTEARRAY)
    target_link_libraries(${name} PRIVATE edu-core Qt${QT_MAJOR_VERSION}::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

edu_add_test(AnswerTest)
edu_add_test(GeneratorsTest)
edu_add_test(QuestionBankTest)
edu_add_test(QuizSessionTest)
edu_add_test(CooldownTrackerTest)
edu_add_test(QuizPolicyTest)

edu_add_test(EduProtocolTest)
target_link_libraries(EduProtocolTest PRIVATE edu-protocol)

edu_add_test(QuizGateTest)
target_link_libraries(QuizGateTest PRIVATE edu-daemon edu-protocol Qt${QT_MAJOR_VERSION}::Network)

# Real Auth and AuthVerifier against fake sddm-helper scripts. The test Constants.h only
# provides LIBEXEC_INSTALL_DIR, so its directory must not hold anything else.
set(EDU_FAKE_LIBEXEC "${CMAKE_CURRENT_BINARY_DIR}/fakelibexec")
file(COPY fakehelper/sddm-helper DESTINATION "${EDU_FAKE_LIBEXEC}"
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
configure_file(AuthTestConstants.h.in ${CMAKE_CURRENT_BINARY_DIR}/authconstants/Constants.h @ONLY)
edu_add_test(AuthVerifierTest
    ${EDU_DIR}/daemon/AuthVerifier.cpp ${EDU_DIR}/daemon/AuthVerifier.h ${EDU_DIR}/daemon/PasswordVerifier.h
    ${CMAKE_SOURCE_DIR}/src/auth/Auth.cpp ${CMAKE_SOURCE_DIR}/src/auth/AuthRequest.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/AuthPrompt.cpp ${CMAKE_SOURCE_DIR}/src/common/SafeDataStream.cpp)
target_include_directories(AuthVerifierTest BEFORE PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/authconstants ${EDU_DIR}/daemon
    ${CMAKE_SOURCE_DIR}/src/auth ${CMAKE_SOURCE_DIR}/src/common)
target_compile_definitions(AuthVerifierTest PRIVATE FAKE_HELPER="${EDU_FAKE_LIBEXEC}/sddm-helper")
target_link_libraries(AuthVerifierTest PRIVATE Qt${QT_MAJOR_VERSION}::Network Qt${QT_MAJOR_VERSION}::Qml)

edu_add_test(EduConfigTest ${EDU_DIR}/EduConfiguration.cpp ${CMAKE_SOURCE_DIR}/src/common/ConfigReader.cpp)
target_include_directories(EduConfigTest PRIVATE ${CMAKE_SOURCE_DIR}/src/common)

# Test-only EduConstants.h that points the config at files under the build dir. It must come
# before the real one on the include path.
set(EDU_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/config/edu-lm.conf")
set(EDU_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/config/edu-lm.conf.d")
configure_file(${EDU_DIR}/EduConstants.h.in ${CMAKE_CURRENT_BINARY_DIR}/testconstants/EduConstants.h @ONLY)
target_include_directories(EduConfigTest BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/testconstants)

add_test(NAME EduValidateSamples COMMAND edu-lm-validate "${EDU_SAMPLE_DIR}")
add_test(NAME EduValidateRejectsBad COMMAND edu-lm-validate "${EDU_TEST_DATA_DIR}")
add_test(NAME EduValidateMissingPath COMMAND edu-lm-validate /nonexistent)
set_tests_properties(EduValidateMissingPath PROPERTIES WILL_FAIL TRUE)
set_tests_properties(EduValidateRejectsBad PROPERTIES WILL_FAIL TRUE)
