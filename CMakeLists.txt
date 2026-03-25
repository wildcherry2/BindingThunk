cmake_minimum_required(VERSION 4.1)
project(BindingThunk LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(FetchContent)
include(GNUInstallDirs)

option(THUNK_BUILD_SHARED "Build BindingThunk as a shared library" OFF)
option(THUNK_BUILD_EXAMPLE "Build the example executable" OFF)
option(THUNK_BUILD_TESTS "Build the unit tests" OFF)

set(THUNK_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/BindingThunk")

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
    include/BindingThunk/BindingThunk.hpp
    include/BindingThunk/Common.hpp
    include/BindingThunk/Context.hpp
    include/BindingThunk/Hashes.hpp
    include/BindingThunk/RestoreThunk.hpp
    include/BindingThunk/Types.hpp
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

add_library(BindingThunk ${THUNK_LIBRARY_TYPE})
add_library(BindingThunk::BindingThunk ALIAS BindingThunk)

target_sources(BindingThunk
    PRIVATE
        ${THUNK_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES ${THUNK_PUBLIC_HEADERS}
)

target_include_directories(BindingThunk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(BindingThunk
    PUBLIC
        asmjit::asmjit
)

target_compile_features(BindingThunk PUBLIC cxx_std_23)
target_compile_definitions(BindingThunk
    PUBLIC
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
)

if (THUNK_BUILD_SHARED)
    target_compile_definitions(BindingThunk
        PUBLIC
            THUNK_SHARED
        PRIVATE
            THUNK_EXPORTS
    )
endif ()

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BindingThunkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/BindingThunkConfig.cmake
    INSTALL_DESTINATION ${THUNK_INSTALL_CMAKEDIR}
)

install(TARGETS BindingThunk
    EXPORT BindingThunkTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT BindingThunkTargets
    FILE BindingThunkTargets.cmake
    NAMESPACE BindingThunk::
    DESTINATION ${THUNK_INSTALL_CMAKEDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/BindingThunkConfig.cmake
    DESTINATION ${THUNK_INSTALL_CMAKEDIR}
)

if (THUNK_BUILD_EXAMPLE)
    add_executable(BindingThunkExample src/main.cpp)
    target_link_libraries(BindingThunkExample PRIVATE BindingThunk::BindingThunk)
    target_compile_features(BindingThunkExample PRIVATE cxx_std_23)
endif ()

if (THUNK_BUILD_TESTS)
    enable_testing()
    target_compile_definitions(BindingThunk PUBLIC THUNK_ENABLE_TEST_HOOKS)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(BindingThunkTests
        include/BindingThunk/Tests.hpp
        src/Tests.cpp
    )
    target_link_libraries(BindingThunkTests PRIVATE BindingThunk::BindingThunk GTest::gtest)
    target_compile_features(BindingThunkTests PRIVATE cxx_std_23)

    include(GoogleTest)
    gtest_discover_tests(BindingThunkTests)
endif ()
