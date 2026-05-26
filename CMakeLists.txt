cmake_minimum_required(VERSION 3.16)

project(img2tikz C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(img2tikz src/img2tikz.c)

if(MSVC)
  target_compile_options(img2tikz PRIVATE /W4 /WX)
else()
  target_compile_options(img2tikz PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(img2tikz PRIVATE -Wno-maybe-uninitialized)
  endif()
  target_link_libraries(img2tikz PRIVATE m)
endif()

option(IMG2TIKZ_BUILD_TESTS "Build unit tests" ON)

if(IMG2TIKZ_BUILD_TESTS AND NOT WIN32)
  enable_testing()

  add_executable(img2tikz_tests tests/test_img2tikz.c)

  if(MSVC)
    target_compile_options(img2tikz_tests PRIVATE /W4 /WX)
  else()
    target_compile_options(img2tikz_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      target_compile_options(img2tikz_tests PRIVATE -Wno-maybe-uninitialized)
    endif()
    target_link_libraries(img2tikz_tests PRIVATE m)
  endif()

  add_test(NAME img2tikz_unit_tests COMMAND img2tikz_tests)
endif()
