cmake_minimum_required(VERSION 3.15)
project(CXXRingBuffer VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(CXXRingBuffer
    Sources/CXXRingBuffer/spsc/RingBuffer.cpp
    Sources/CXXRingBuffer/include/spsc/RingBuffer.hpp
    Sources/CXXRingBuffer/include/mpsc/RingBuffer.hpp
)
add_library(lockfree::RingBuffer ALIAS CXXRingBuffer)

target_include_directories(CXXRingBuffer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Sources/CXXRingBuffer/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(CXXRingBuffer PRIVATE /W4)
else()
    target_compile_options(CXXRingBuffer PRIVATE -Wall -Wextra)
endif()

include(GNUInstallDirs)

install(TARGETS CXXRingBuffer
    EXPORT CXXRingBufferTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY Sources/CXXRingBuffer/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT CXXRingBufferTargets
    FILE CXXRingBufferTargets.cmake
    NAMESPACE lockfree::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CXXRingBuffer
)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    enable_testing()

    add_executable(run_spsc_tests test/spsc_ring_buffer_test.cpp)
    add_executable(run_mpsc_tests test/mpsc_ring_buffer_test.cpp)

    target_link_libraries(run_spsc_tests
        PRIVATE
            lockfree::RingBuffer
            GTest::gtest_main
    )

    target_link_libraries(run_mpsc_tests
        PRIVATE
            lockfree::RingBuffer
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(run_spsc_tests)
    gtest_discover_tests(run_mpsc_tests)
endif()
