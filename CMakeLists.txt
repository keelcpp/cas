cmake_minimum_required(VERSION 3.20)
project(keel_cas VERSION 0.1.0 LANGUAGES CXX)

option(KEEL_CAS_NO_EXCEPTIONS_RTTI "Compile keel_cas without C++ exceptions and RTTI" OFF)
option(KEEL_CAS_BUILD_TESTS "Build keel_cas unit tests and header purity checks" ${PROJECT_IS_TOP_LEVEL})

if(NOT TARGET keel::keel)
    find_package(keel QUIET CONFIG)
endif()
if(NOT TARGET keel::keel)
    include(FetchContent)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../keel/CMakeLists.txt" AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_KEEL)
        set(FETCHCONTENT_SOURCE_DIR_KEEL "${CMAKE_CURRENT_SOURCE_DIR}/../keel" CACHE PATH "Local keel checkout")
    endif()
    if(KEEL_CAS_NO_EXCEPTIONS_RTTI)
        set(KEEL_NO_EXCEPTIONS_RTTI ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        keel
        GIT_REPOSITORY https://github.com/keelcpp/keel.git
        GIT_TAG        5d80fa7d6984e7325fc8d5317c5db13830381429
    )
    FetchContent_MakeAvailable(keel)
endif()

if(NOT TARGET keel::bignum)
    find_package(keel_bignum QUIET CONFIG)
    if(TARGET keel::keel_bignum AND NOT TARGET keel::bignum)
        add_library(keel::bignum ALIAS keel::keel_bignum)
    endif()
endif()
if(NOT TARGET keel::bignum)
    include(FetchContent)
    set(KEEL_BIGNUM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(KEEL_CAS_NO_EXCEPTIONS_RTTI)
        set(KEEL_BIGNUM_NO_EXCEPTIONS_RTTI ON CACHE BOOL "" FORCE)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bignum/CMakeLists.txt" AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_KEEL_BIGNUM)
        set(FETCHCONTENT_SOURCE_DIR_KEEL_BIGNUM "${CMAKE_CURRENT_SOURCE_DIR}/../bignum" CACHE PATH "Local keel_bignum checkout")
    endif()
    FetchContent_Declare(
        keel_bignum
        GIT_REPOSITORY https://github.com/keelcpp/bignum.git
        GIT_TAG        f773bfd14f29ac523f60cf2b6001400026a10d5a
    )
    FetchContent_MakeAvailable(keel_bignum)
endif()

add_library(keel_cas STATIC
    src/core/expr.cpp
    src/core/diff.cpp
    src/core/parser.cpp
    src/core/series.cpp
    src/core/simplify.cpp
    src/core/symsolve.cpp
    src/poly/upoly.cpp
    src/poly/sturm.cpp
    src/poly/solve.cpp
    src/poly/algebraic.cpp
    src/poly/modcpoly.cpp
    src/poly/fppoly.cpp
    src/poly/zassenhaus.cpp
    src/poly/groebner.cpp
    src/integrate/ratint.cpp
    src/integrate/verify.cpp
    src/linalg/symmatrix.cpp
    src/ode/dsolve.cpp
    src/rubi/rules.cpp
    src/transform/fourier.cpp
    src/transform/laplace.cpp
    src/transform/ztrans.cpp
)
add_library(keel::cas ALIAS keel_cas)

target_link_libraries(keel_cas PUBLIC keel::keel keel::bignum)

target_compile_features(keel_cas PUBLIC cxx_std_20)
set_target_properties(keel_cas PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(keel_cas PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(keel_cas PRIVATE /W4 /utf-8 /MP /bigobj)
    if(KEEL_CAS_NO_EXCEPTIONS_RTTI)
        string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        target_compile_options(keel_cas PUBLIC /EHs-c- /GR-)
        target_compile_definitions(keel_cas PUBLIC _HAS_EXCEPTIONS=0)
    endif()
else()
    target_compile_options(keel_cas PRIVATE -Wall -Wextra -Wpedantic)
    if(KEEL_CAS_NO_EXCEPTIONS_RTTI)
        target_compile_options(keel_cas PUBLIC -fno-exceptions -fno-rtti)
    endif()
endif()

if(COMMAND keel_enforce_guidelines)
    keel_enforce_guidelines(keel_cas
        SCOPE ALL
        BAN_RAW_INTS ALL
        BAN_RAW_FLOATS ALL
        HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
        STANDALONE_HEADERS ${KEEL_CAS_BUILD_TESTS}
        TEST_NAME test_guidelines_headers
        WARNINGS OFF
    )
endif()

include(CMakePackageConfigHelpers)
install(TARGETS keel_cas EXPORT keel_casTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/keel DESTINATION include)
install(EXPORT keel_casTargets
    FILE keel_casTargets.cmake
    NAMESPACE keel::
    DESTINATION lib/cmake/keel_cas
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/keel_casConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/keel_casConfig.cmake"
    INSTALL_DESTINATION lib/cmake/keel_cas
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/keel_casConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/keel_casConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/keel_casConfigVersion.cmake"
    DESTINATION lib/cmake/keel_cas
)

if(KEEL_CAS_BUILD_TESTS)
    enable_testing()

    add_executable(test_cas tests/test_cas.cpp)
    target_link_libraries(test_cas PRIVATE keel::cas)
    if(MSVC)
        target_compile_options(test_cas PRIVATE /W4 /utf-8 /MP /bigobj)
    else()
        target_compile_options(test_cas PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME test_cas COMMAND test_cas)

    get_target_property(KEEL_CORE_INC_DIRS keel::keel INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(KEEL_BIGNUM_INC_DIRS keel::bignum INTERFACE_INCLUDE_DIRECTORIES)
    set(KEEL_DEP_INC_CLEAN "")
    foreach(IDIR IN LISTS KEEL_CORE_INC_DIRS KEEL_BIGNUM_INC_DIRS)
        string(REGEX REPLACE "\\$<BUILD_INTERFACE:([^>]+)>" "\\1" IDIR_STRIPPED "${IDIR}")
        if(NOT IDIR_STRIPPED MATCHES "\\$<INSTALL_INTERFACE:" AND EXISTS "${IDIR_STRIPPED}")
            list(APPEND KEEL_DEP_INC_CLEAN "${IDIR_STRIPPED}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES KEEL_DEP_INC_CLEAN)
    string(REPLACE ";" "|" KEEL_DEP_INC_JOINED "${KEEL_DEP_INC_CLEAN}")
    string(REPLACE ";" "|" KEEL_STD_INC_JOINED "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")

    add_test(
        NAME test_header_purity
        COMMAND ${CMAKE_COMMAND}
            "-DKEEL_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include"
            "-DKEEL_DEP_INC_DIRS=${KEEL_DEP_INC_JOINED}"
            "-DCOMPILER_PATH=${CMAKE_CXX_COMPILER}"
            "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DSTD_INC_DIRS=${KEEL_STD_INC_JOINED}"
            "-DCMAKE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckHeaderPurity.cmake"
    )
endif()
