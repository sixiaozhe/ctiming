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

add_library(ctiming STATIC src/glob.c src/buffer.c src/trace.c src/symbols.c src/config.c src/runtime.c)
target_include_directories(ctiming PUBLIC include PRIVATE src)
target_compile_definitions(ctiming PRIVATE _GNU_SOURCE)
target_compile_options(ctiming PRIVATE -Wall -Wextra)
target_link_libraries(ctiming PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_executable(ctiming-info tools/ctiming-info.c)
target_include_directories(ctiming-info PRIVATE include src)
target_compile_options(ctiming-info PRIVATE -Wall -Wextra)
target_link_libraries(ctiming-info PRIVATE ctiming)

function(ct_add_test name file)
  add_executable(${name} ${file})
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_link_libraries(${name} PRIVATE ctiming)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_library(ctiming_analysis STATIC
  src/analyze/event.cpp
  src/analyze/calltree.cpp
  src/analyze/aggregate.cpp
  src/analyze/json.cpp
  src/analyze/analysis.cpp)
target_include_directories(ctiming_analysis PUBLIC src/analyze PRIVATE src)
target_compile_options(ctiming_analysis PRIVATE -Wall -Wextra)
target_link_libraries(ctiming_analysis PUBLIC ctiming)

function(ct_add_analysis_test name file)
  add_executable(${name} ${file})
  target_include_directories(${name} PRIVATE src src/analyze tests)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_link_libraries(${name} PRIVATE ctiming_analysis)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

set(CT_VIEWER_ASSETS
  viewer/viewer.css
  viewer/core.js
  viewer/overview.js
  viewer/flame.js
  viewer/graph.js
  viewer/trace.js
  viewer/callers.js
  viewer/main.js)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/viewer_assets.hpp
  COMMAND ${CMAKE_COMMAND}
          -DOUT=${CMAKE_BINARY_DIR}/viewer_assets.hpp
          "-DFILES=${CT_VIEWER_ASSETS}"
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_assets.cmake
  DEPENDS ${CT_VIEWER_ASSETS} ${CMAKE_SOURCE_DIR}/cmake/embed_assets.cmake
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM)
add_custom_target(ct_viewer_assets DEPENDS ${CMAKE_BINARY_DIR}/viewer_assets.hpp)

target_sources(ctiming_analysis PRIVATE src/analyze/html.cpp)
target_include_directories(ctiming_analysis PRIVATE ${CMAKE_BINARY_DIR})
add_dependencies(ctiming_analysis ct_viewer_assets)

ct_add_test(test_smoke tests/test_smoke.c)
ct_add_test(test_glob tests/test_glob.c)
ct_add_test(test_buffer tests/test_buffer.c)
ct_add_test(test_trace tests/test_trace.c)
ct_add_test(test_symbols tests/test_symbols.c)
ct_add_test(test_config tests/test_config.c)

ct_add_analysis_test(test_event tests/test_event.cpp)
ct_add_analysis_test(test_calltree tests/test_calltree.cpp)
ct_add_analysis_test(test_aggregate tests/test_aggregate.cpp)
ct_add_analysis_test(test_json tests/test_json.cpp)
ct_add_analysis_test(test_analysis tests/test_analysis.cpp)
ct_add_analysis_test(test_html tests/test_html.cpp)

add_executable(example_single examples/example_single.c)
target_compile_options(example_single PRIVATE -finstrument-functions -g -O0)
target_link_libraries(example_single PRIVATE ctiming)

add_test(
  NAME integration_single
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_test.sh
          $<TARGET_FILE:example_single> $<TARGET_FILE:ctiming-info>)

add_executable(ctiming-analyze tools/ctiming-analyze.cpp)
target_include_directories(ctiming-analyze PRIVATE src src/analyze)
target_link_libraries(ctiming-analyze PRIVATE ctiming_analysis)
target_compile_options(ctiming-analyze PRIVATE -Wall -Wextra)

add_test(
  NAME integration_analyze
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_analyze.sh
          $<TARGET_FILE:example_single> $<TARGET_FILE:ctiming-analyze>)

add_test(
  NAME integration_report
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_report.sh
          $<TARGET_FILE:example_single> $<TARGET_FILE:ctiming-analyze>)

add_executable(example_threads examples/example_threads.c)
target_compile_options(example_threads PRIVATE -finstrument-functions -g -O0 -pthread)
target_link_libraries(example_threads PRIVATE ctiming)

add_test(
  NAME integration_threads
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_threads.sh
          $<TARGET_FILE:example_threads> $<TARGET_FILE:ctiming-info>)

add_executable(example_complex examples/example_complex.cpp)
target_compile_options(example_complex PRIVATE -finstrument-functions -g -O0 -pthread)
target_link_libraries(example_complex PRIVATE ctiming)

add_test(
  NAME integration_complex
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_complex.sh
          $<TARGET_FILE:example_complex> $<TARGET_FILE:ctiming-analyze>)

add_executable(example_control examples/example_control.c)
target_compile_options(example_control PRIVATE -finstrument-functions -g -O0 -pthread)
target_link_libraries(example_control PRIVATE ctiming)

add_test(
  NAME integration_trace_env
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_trace_env.sh
          $<TARGET_FILE:example_control> $<TARGET_FILE:ctiming-analyze>)
