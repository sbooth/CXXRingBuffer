cmake_minimum_required(VERSION 3.15)
project(CXXRingBuffer VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(CXXRingBuffer
    Sources/CXXRingBuffer/RingBuffer.cpp
)
add_library(CXXRingBuffer::RingBuffer ALIAS CXXRingBuffer)

target_include_directories(CXXRingBuffer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Sources/CXXRingBuffer/include/CXXRingBuffer>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(CXXRingBuffer PRIVATE -Wall -Wextra)

include(GNUInstallDirs)

install(TARGETS CXXRingBuffer
    EXPORT CXXRingBufferTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT CXXRingBufferTargets
    FILE CXXRingBufferTargets.cmake
    NAMESPACE CXXRingBuffer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CXXRingBuffer
)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    enable_testing()

    add_executable(run_tests test/ring_buffer_test.cpp)

    target_link_libraries(run_tests
        PRIVATE
            CXXRingBuffer::RingBuffer
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(run_tests)
endif()
