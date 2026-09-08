cmake_minimum_required(VERSION 3.20)
project(MusicSorterCore C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

set(CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/AcousticAnalyzer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/AudioEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DatabaseManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CoreEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/HttpServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3.c"
)

add_executable(music-sorter-core ${CORE_SOURCES})

target_link_libraries(music-sorter-core PRIVATE
    ws2_32
    wininet
    ole32
    shell32
    winmm
    user32
)

set_target_properties(music-sorter-core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    OUTPUT_NAME "music-sorter-core"
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
