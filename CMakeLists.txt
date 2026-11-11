cmake_minimum_required(VERSION 3.14)
project(EndlessRunner CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Fetch and build raylib automatically (needs internet on first configure)
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(runner src/main.cpp)
target_link_libraries(runner PRIVATE raylib)

# Copy the assets folder next to the built executable (works for both
# single-config generators like Makefiles and multi-config ones like Visual Studio,
# where the exe ends up in build/Release rather than build/).
add_custom_command(TARGET runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:runner>/assets")
