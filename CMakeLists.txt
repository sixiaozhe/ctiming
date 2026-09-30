cmake_minimum_required(VERSION 3.16)
project(ctiming C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)
enable_testing()

add_library(ctiming STATIC src/runtime.c)
target_include_directories(ctiming PUBLIC include PRIVATE src)
target_link_libraries(ctiming PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_executable(ctiming-info tools/ctiming-info.c)
target_include_directories(ctiming-info PRIVATE include src)
target_link_libraries(ctiming-info PRIVATE ctiming)

function(ct_add_test name file)
  add_executable(${name} ${file})
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(${name} PRIVATE ctiming)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ct_add_test(test_smoke tests/test_smoke.c)
