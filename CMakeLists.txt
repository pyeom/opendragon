cmake_minimum_required(VERSION 3.16)
project(opendragon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(opendragon
  src/main.cpp
  src/protocol.cpp
  src/config.cpp
  src/models.cpp
  src/keycodes.cpp)

if(WIN32)
  target_sources(opendragon PRIVATE src/transport_win.cpp)
  target_link_libraries(opendragon PRIVATE hid setupapi)
  if(MINGW)
    target_link_options(opendragon PRIVATE -static)
  endif()
else()
  target_sources(opendragon PRIVATE src/transport_linux.cpp)
endif()

target_compile_options(opendragon PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

include(GNUInstallDirs)
install(TARGETS opendragon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX)
  install(FILES udev/70-opendragon.rules DESTINATION lib/udev/rules.d)
endif()

enable_testing()
add_executable(opendragon_tests tests/tests.cpp src/protocol.cpp src/config.cpp src/models.cpp src/keycodes.cpp
  $<IF:$<BOOL:${WIN32}>,src/transport_win.cpp,src/transport_linux.cpp>)
target_include_directories(opendragon_tests PRIVATE src)
if(WIN32)
  target_link_libraries(opendragon_tests PRIVATE hid setupapi)
endif()
add_test(NAME unit COMMAND opendragon_tests ${CMAKE_SOURCE_DIR}/docs/dumps)
