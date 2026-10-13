cmake_minimum_required(VERSION 3.22)
project(lmcpp VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(nlohmann_json 3.2.0 REQUIRED)
add_library(lmcpp_core STATIC src/lmcpp.cpp)
target_include_directories(lmcpp_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(lmcpp_core PROPERTIES EXPORT_NAME core)
target_link_libraries(lmcpp_core PUBLIC nlohmann_json::nlohmann_json)
target_link_options(lmcpp_core INTERFACE -static-libstdc++ -static-libgcc)
add_executable(toy examples/toy/main.cpp)
target_link_libraries(toy PRIVATE lmcpp_core)
set_target_properties(toy PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
option(LMCPP_BUILD_TESTS "Build the lmcpp test suite" ON)
set(LMAPI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lmapi" CACHE PATH "Path to lmapi checkout")
if(LMCPP_BUILD_TESTS)
  enable_testing()
  add_executable(lmcpp_tests tests/core.cpp)
  target_link_libraries(lmcpp_tests PRIVATE lmcpp_core)
  add_test(NAME lmcpp_core COMMAND lmcpp_tests)
  if(EXISTS "${LMAPI_DIR}/conformance")
    add_executable(lmcpp_conformance tests/conformance.cpp)
    target_link_libraries(lmcpp_conformance PRIVATE lmcpp_core)
    add_test(NAME lmcpp_conformance COMMAND lmcpp_conformance "${LMAPI_DIR}/conformance")
  endif()
endif()

configure_package_config_file(cmake/lmcppConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/lmcppConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lmcpp)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/lmcppConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS lmcpp_core EXPORT lmcppTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT lmcppTargets FILE lmcppTargets.cmake NAMESPACE lmcpp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lmcpp)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lmcppConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/lmcppConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lmcpp)
