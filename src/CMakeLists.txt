cmake_minimum_required(VERSION 3.20)
project(MusicSorterCore C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)

file(GLOB APP_SOURCES
    "src/*.cpp"
    "src/*.c"
    "third_party/imgui/*.cpp"
    "third_party/sqlite3.c"
)

add_executable(MusicSorterApp WIN32 ${APP_SOURCES})

target_link_libraries(MusicSorterApp PRIVATE
    user32
    gdi32
    shell32
    comctl32
    ole32
    winmm
    wininet
    d3d11
    d3dcompiler
)

set_target_properties(MusicSorterApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# Test Suite
enable_testing()

file(GLOB TEST_SOURCES
    "tests/*.cpp"
)

add_executable(MusicSorterTests ${TEST_SOURCES})

target_link_libraries(MusicSorterTests PRIVATE
    user32
    wininet
    ole32
    shell32
)

set_target_properties(MusicSorterTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME FetchTests COMMAND MusicSorterTests)
