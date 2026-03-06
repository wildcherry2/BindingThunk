cmake_minimum_required(VERSION 4.1)
project(Thunk)
include(FetchContent)

set(CMAKE_CXX_STANDARD 23)

FetchContent_Declare(
    asmjit
    GIT_REPOSITORY https://github.com/asmjit/asmjit.git
    GIT_TAG 64a88ed1d8abb2e2b17a938a5ce7c1b66dabb695
)

add_executable(Thunk src/main.cpp
        include/BindingThunk.hpp
        include/Common.hpp
        src/Common.cpp
        src/BindingThunk.cpp
        include/Context.hpp
        src/Context.cpp
        include/Hashes.hpp
        include/RestoreThunk.hpp
        src/RestoreThunk.cpp)
set(ASMJIT_STATIC TRUE)
FetchContent_MakeAvailable(asmjit)

target_include_directories(Thunk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(Thunk PUBLIC asmjit)