cmake_minimum_required(VERSION 3.28)
project(KairoAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(KAIRO_AI_BUILD_TESTS "Build KairoAI tests" ${PROJECT_IS_TOP_LEVEL})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

add_library(KairoAI)
add_library(Kairo::AI ALIAS KairoAI)
target_sources(KairoAI PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    AIContracts.cppm AIProvider.cppm MockAIProvider.cppm KairoAI.cppm)

if(KAIRO_AI_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "")
    FetchContent_MakeAvailable(Catch2)
    add_executable(KairoAITests tests/KairoAITests.cpp)
    target_link_libraries(KairoAITests PRIVATE KairoAI Catch2::Catch2WithMain)
    add_test(NAME KairoAITests COMMAND KairoAITests)
endif()
