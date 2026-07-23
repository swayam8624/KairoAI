cmake_minimum_required(VERSION 3.28)
project(KairoAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(KAIRO_AI_BUILD_TESTS "Build KairoAI tests" ${PROJECT_IS_TOP_LEVEL})
option(KAIRO_AI_BUILD_CLOUD_PROVIDER "Build the pinned OpenAI-compatible HTTP adapter" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

add_library(KairoAI)
add_library(Kairo::AI ALIAS KairoAI)
target_sources(KairoAI PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    AIContracts.cppm AIProvider.cppm AIToolPolicy.cppm MockAIProvider.cppm DeviceAgent.cppm KairoAI.cppm)

if(KAIRO_AI_BUILD_CLOUD_PROVIDER)
    include(FetchContent)
    set(KAIRO_AI_CPR_REVISION da40186618909b1a7363d4e4495aa899c6e0eb75)
    set(KAIRO_AI_JSON_REVISION 65ee68451d8eb2b5f3a30b410476ab83deb3289b)
    set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CPR_BUILD_TESTS_SSL OFF CACHE BOOL "" FORCE)
    # CPR 1.12's bundled curl path unconditionally builds libpsl through
    # Meson. Use the platform curl package to keep Kairo's CMake-only build and
    # avoid adding an unrelated build-language prerequisite.
    set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
    if(APPLE AND EXISTS "/opt/homebrew/opt/curl/include/curl/curl.h")
        # CMake's Xcode-SDK curl target injects the SDK usr/include directory
        # ahead of Homebrew libc++, which breaks Clang's fundamental typedefs.
        # Prefer Homebrew's keg-only curl when it is available.
        set(CURL_INCLUDE_DIR "/opt/homebrew/opt/curl/include" CACHE PATH "" FORCE)
        set(CURL_LIBRARY "/opt/homebrew/opt/curl/lib/libcurl.dylib" CACHE FILEPATH "" FORCE)
    endif()
    FetchContent_Declare(cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG ${KAIRO_AI_CPR_REVISION}
        GIT_SHALLOW TRUE)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG ${KAIRO_AI_JSON_REVISION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(cpr nlohmann_json)

    add_library(KairoAICloud)
    add_library(Kairo::AICloud ALIAS KairoAICloud)
    target_sources(KairoAICloud PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
        OpenAICompatibleProvider.cppm CprChatTransport.cppm KairoAICloud.cppm)
    target_link_libraries(KairoAICloud PUBLIC KairoAI cpr::cpr nlohmann_json::nlohmann_json)
endif()

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
    add_executable(KairoAIDeviceAgentTests tests/DeviceAgentTests.cpp)
    target_link_libraries(KairoAIDeviceAgentTests PRIVATE KairoAI Catch2::Catch2WithMain)
    add_test(NAME KairoAIDeviceAgentTests COMMAND KairoAIDeviceAgentTests)
    if(TARGET KairoAICloud)
        add_executable(KairoAICloudTests tests/KairoAICloudTests.cpp)
        target_link_libraries(KairoAICloudTests PRIVATE KairoAICloud Catch2::Catch2WithMain)
        add_test(NAME KairoAICloudTests COMMAND KairoAICloudTests)
    endif()
endif()
