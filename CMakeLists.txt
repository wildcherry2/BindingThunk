cmake_minimum_required(VERSION 4.1)
project(Thunk LANGUAGES CXX)
include(FetchContent)
include(CTest)

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

add_library(Thunk STATIC ${THUNK_PUBLIC_HEADERS} ${THUNK_SOURCES})
target_include_directories(Thunk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(Thunk PUBLIC asmjit)

option(THUNK_BUILD_EXAMPLE "Build the example executable" OFF)
if (THUNK_BUILD_EXAMPLE)
    add_executable(ThunkExample src/main.cpp)
    target_link_libraries(ThunkExample PRIVATE Thunk)
endif ()

if (BUILD_TESTING)
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
    target_link_libraries(ThunkTests PRIVATE Thunk GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(ThunkTests)
endif ()
