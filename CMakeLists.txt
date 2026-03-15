cmake_minimum_required(VERSION 4.1)
project(Thunk LANGUAGES CXX)

include(FetchContent)

option(THUNK_BUILD_SHARED "Build Thunk as a shared library" OFF)
option(THUNK_BUILD_EXAMPLE "Build the example executable" OFF)
option(THUNK_BUILD_TESTS "Build the unit tests" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
    asmjit
    GIT_REPOSITORY https://github.com/asmjit/asmjit.git
    GIT_TAG 64a88ed1d8abb2e2b17a938a5ce7c1b66dabb695
)

set(ASMJIT_STATIC TRUE)
FetchContent_MakeAvailable(asmjit)

set(THUNK_PUBLIC_HEADERS
    include/BindingThunk.hpp
    include/Common.hpp
    include/Context.hpp
    include/Hashes.hpp
    include/RestoreThunk.hpp
)

set(THUNK_SOURCES
    src/BindingThunk.cpp
    src/Common.cpp
    src/Context.cpp
    src/RestoreThunk.cpp
)

set(THUNK_LIBRARY_TYPE STATIC)
if (THUNK_BUILD_SHARED)
    set(THUNK_LIBRARY_TYPE SHARED)
endif ()

add_library(Thunk ${THUNK_LIBRARY_TYPE})
add_library(Thunk::Thunk ALIAS Thunk)

target_sources(Thunk
    PRIVATE
        ${THUNK_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES ${THUNK_PUBLIC_HEADERS}
)

target_include_directories(Thunk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Thunk
    PUBLIC
        asmjit
)

target_compile_features(Thunk PUBLIC cxx_std_23)
target_compile_definitions(Thunk
    PUBLIC
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
)

if (THUNK_BUILD_SHARED)
    target_compile_definitions(Thunk
        PUBLIC
            THUNK_SHARED
        PRIVATE
            THUNK_EXPORTS
    )
endif ()

if (THUNK_BUILD_EXAMPLE)
    add_executable(ThunkExample src/main.cpp)
    target_link_libraries(ThunkExample PRIVATE Thunk::Thunk)
    target_compile_features(ThunkExample PRIVATE cxx_std_23)
endif ()

if (THUNK_BUILD_TESTS)
    enable_testing()
    target_compile_definitions(Thunk PUBLIC THUNK_ENABLE_TEST_HOOKS)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(ThunkTests
        include/Tests.hpp
        src/Tests.cpp
    )
    target_link_libraries(ThunkTests PRIVATE Thunk::Thunk GTest::gtest)
    target_compile_features(ThunkTests PRIVATE cxx_std_23)

    include(GoogleTest)
    gtest_discover_tests(ThunkTests)
endif ()
