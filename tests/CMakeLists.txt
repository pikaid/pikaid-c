# Test suite wiring.
#
# Every executable is a plain CTest test. Tests that depend on optional local
# tooling report a skip (exit 77) instead of silently passing.

set(PIKAID_TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src)

function(pikaid_apply_test_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(PIKAID_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Wcast-qual
            -Wwrite-strings)
        if(PIKAID_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
endfunction()

# Tests that link the installed-style library target.
function(pikaid_add_test target)
    add_executable(${target} ${ARGN} test_common.c)
    target_link_libraries(${target} PRIVATE pikaid::pikaid)
    target_compile_features(${target} PRIVATE c_std_99)
    target_include_directories(${target} PRIVATE ${PIKAID_TEST_INCLUDE_DIRS})
    pikaid_apply_test_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 120)
endfunction()

pikaid_add_test(pikaid_test_vectors test_vectors.c)
pikaid_add_test(pikaid_test_encoding test_encoding.c)
pikaid_add_test(pikaid_test_decoding test_decoding.c)
pikaid_add_test(pikaid_test_api test_api.c)
pikaid_add_test(pikaid_test_generate test_generate.c)
pikaid_add_test(pikaid_test_properties test_properties.c)

# Fault injection: compile the library core directly and supply test doubles
# for the internal entropy and clock symbols.
function(pikaid_add_injection_test target mode)
    add_executable(${target}
        test_injection.c
        test_common.c
        ${PROJECT_SOURCE_DIR}/src/pikaid.c)
    target_compile_definitions(${target} PRIVATE ${mode})
    target_compile_features(${target} PRIVATE c_std_99)
    target_include_directories(${target} PRIVATE ${PIKAID_TEST_INCLUDE_DIRS})
    pikaid_apply_test_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 120)
endfunction()

pikaid_add_injection_test(pikaid_test_injection_fixed
    PIKAID_TEST_MODE_FIXED)
pikaid_add_injection_test(pikaid_test_injection_entropy_failure
    PIKAID_TEST_MODE_ENTROPY_FAILURE)
pikaid_add_injection_test(pikaid_test_injection_clock_failure
    PIKAID_TEST_MODE_CLOCK_FAILURE)
pikaid_add_injection_test(pikaid_test_injection_clock_out_of_range
    PIKAID_TEST_MODE_CLOCK_OUT_OF_RANGE)

# Real platform layers, compiled directly so the test exercises the actual
# getrandom()/getentropy()/BCryptGenRandom and time() code paths.
add_executable(pikaid_test_internal
    test_internal.c
    test_common.c
    ${PROJECT_SOURCE_DIR}/src/pikaid.c
    ${PROJECT_SOURCE_DIR}/src/pikaid_entropy.c
    ${PROJECT_SOURCE_DIR}/src/pikaid_clock.c)
target_compile_features(pikaid_test_internal PRIVATE c_std_99)
target_include_directories(pikaid_test_internal PRIVATE ${PIKAID_TEST_INCLUDE_DIRS})
pikaid_apply_test_warnings(pikaid_test_internal)
if(WIN32)
    target_link_libraries(pikaid_test_internal PRIVATE bcrypt)
endif()
add_test(NAME pikaid_test_internal COMMAND pikaid_test_internal)
set_tests_properties(pikaid_test_internal PROPERTIES
    TIMEOUT 120
    SKIP_RETURN_CODE 77)

# Test-only JSON-Lines bridge for the official shared conformance suite. It is
# deliberately not installed and is not a CTest test: the external runner owns
# its stdin/stdout lifecycle. The mutant target is a deterministic negative
# control proving the runner fails a wrong wire result.
add_executable(pikaid_conformance_adapter conformance/pikaid_conformance_adapter.c)
target_link_libraries(pikaid_conformance_adapter PRIVATE pikaid::pikaid)
target_compile_features(pikaid_conformance_adapter PRIVATE c_std_99)
target_include_directories(pikaid_conformance_adapter PRIVATE ${PIKAID_TEST_INCLUDE_DIRS})
pikaid_apply_test_warnings(pikaid_conformance_adapter)

# The self-check drives every exported public entry point through the compiled
# test-only CLI; the wire protocol itself only covers the official operations.
add_test(NAME pikaid_conformance_adapter_self_check
    COMMAND pikaid_conformance_adapter --self-check)
set_tests_properties(pikaid_conformance_adapter_self_check PROPERTIES TIMEOUT 60)

add_executable(pikaid_conformance_adapter_mutant conformance/pikaid_conformance_adapter.c)
target_link_libraries(pikaid_conformance_adapter_mutant PRIVATE pikaid::pikaid)
target_compile_features(pikaid_conformance_adapter_mutant PRIVATE c_std_99)
target_compile_definitions(pikaid_conformance_adapter_mutant PRIVATE
    PIKAID_CONFORMANCE_MUTANT=1)
target_include_directories(pikaid_conformance_adapter_mutant PRIVATE ${PIKAID_TEST_INCLUDE_DIRS})
pikaid_apply_test_warnings(pikaid_conformance_adapter_mutant)

# Fixture regeneration check and optional JSON Schema validation. Both are
# best-effort by default: python3 is not a build dependency of the library.
# PIKAID_REQUIRE_SCHEMA_VALIDATION turns the schema check into a hard gate and
# fails at configure time if jsonschema is missing, rather than silently
# skipping the test.
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    add_test(NAME pikaid_vectors_generated_check
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/vectors/generate_vectors.py
            --json ${CMAKE_CURRENT_SOURCE_DIR}/vectors/pikaid-v1.json
            --output ${CMAKE_CURRENT_SOURCE_DIR}/vectors/pikaid_vectors.h
            --check)
    set_tests_properties(pikaid_vectors_generated_check PROPERTIES TIMEOUT 60)

    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import jsonschema"
        RESULT_VARIABLE PIKAID_JSONSCHEMA_RESULT
        OUTPUT_QUIET
        ERROR_QUIET)
    if(PIKAID_REQUIRE_SCHEMA_VALIDATION AND
       NOT PIKAID_JSONSCHEMA_RESULT EQUAL 0)
        message(FATAL_ERROR
            "PIKAID_REQUIRE_SCHEMA_VALIDATION=ON but Python jsonschema is not "
            "installed for ${Python3_EXECUTABLE}")
    endif()

    set(PIKAID_SCHEMA_COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/vectors/validate_schema.py
        --schema ${CMAKE_CURRENT_SOURCE_DIR}/vectors/pikaid-v1.schema.json
        --json ${CMAKE_CURRENT_SOURCE_DIR}/vectors/pikaid-v1.json)
    if(PIKAID_REQUIRE_SCHEMA_VALIDATION)
        list(APPEND PIKAID_SCHEMA_COMMAND --require)
    endif()
    add_test(NAME pikaid_vectors_schema_validation
        COMMAND ${PIKAID_SCHEMA_COMMAND})
    set_tests_properties(pikaid_vectors_schema_validation PROPERTIES
        TIMEOUT 60)
    if(NOT PIKAID_REQUIRE_SCHEMA_VALIDATION)
        set_tests_properties(pikaid_vectors_schema_validation PROPERTIES
            SKIP_RETURN_CODE 77)
    endif()
else()
    if(PIKAID_REQUIRE_SCHEMA_VALIDATION)
        message(FATAL_ERROR
            "PIKAID_REQUIRE_SCHEMA_VALIDATION=ON but python3 was not found")
    endif()
    message(STATUS
        "python3 not found: vector regeneration and schema checks are NOT RUN")
endif()
