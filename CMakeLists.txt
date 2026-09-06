cmake_minimum_required(VERSION 3.24)

project(EngineResidency
  VERSION 1.0.0
  DESCRIPTION "C++20 runtime for governing serving-engine residency and preparedness"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ER_BUILD_TRANSPORT "Build the reference TCP coordinator/worker transport" ON)
option(ER_BUILD_CUDA "Build the real CUDA reference engine (requires CUDA)" ON)
option(ER_BUILD_TESTS "Build tests" ON)
option(ER_BUILD_BENCHMARKS "Build benchmarks" ON)
option(ER_BUILD_EXAMPLES "Build examples" ON)
option(ER_BUILD_TOOLS "Build CLI inspection tool" ON)
option(ER_ENABLE_ASAN "Build with AddressSanitizer on supported toolchains" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # Strict first-party warnings, treated as errors. Do NOT globally suppress.
  add_compile_options(/W4 /WX /permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS=1)
  add_compile_definitions(NOMINMAX=1)
endif()

if(ER_ENABLE_ASAN)
  if(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
  endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(er_core STATIC
  src/engine/registry.cpp
  src/engine/definition.cpp
  src/authority/authority.cpp
  src/readiness/readiness.cpp
  src/components/components.cpp
  src/preparation/preparation.cpp
  src/standby/standby.cpp
  src/activation/activation.cpp
  src/drain/drain.cpp
  src/replacement/replacement.cpp
  src/economics/economics.cpp
  src/persistence/persistence.cpp
  src/core/snapshots.cpp
  src/adapters/adapters.cpp
  src/backend/cpu_backend.cpp
  src/runtime.cpp
)
target_include_directories(er_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(er_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(er_core PUBLIC cxx_std_20)
# The installed target name is EngineResidency::EngineResidency.
set_target_properties(er_core PROPERTIES EXPORT_NAME EngineResidency)

# ---------------------------------------------------------------------------
# Transport (reference TCP coordinator/workers/controller)
# ---------------------------------------------------------------------------
if(ER_BUILD_TRANSPORT)
  add_library(er_transport STATIC
    src/protocol/frame.cpp
    src/protocol/message.cpp
    src/protocol/net.cpp
    src/coordinator/coordinator.cpp
    src/coordinator/session.cpp
  )
  target_include_directories(er_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(er_transport PUBLIC cxx_std_20)
  target_include_directories(er_transport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(er_transport PUBLIC er_core ws2_32)
endif()

# ---------------------------------------------------------------------------
# CUDA reference engine
# ---------------------------------------------------------------------------
if(ER_BUILD_CUDA)
  find_package(CUDAToolkit REQUIRED)
  enable_language(CUDA)
  add_library(er_cuda STATIC
    cuda/cuda_engine.cpp
    cuda/cuda_kernels.cu
  )
  target_include_directories(er_cuda PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(er_cuda PUBLIC er_core CUDA::cudart)
  set_target_properties(er_cuda PROPERTIES
    CUDA_ARCHITECTURES 120
    CUDA_STANDARD 17)
endif()

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
if(ER_BUILD_TRANSPORT)
  add_executable(er_coordinator src/tools/coordinator_main.cpp)
  target_include_directories(er_coordinator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(er_coordinator PRIVATE er_transport er_core)
  add_executable(er_worker src/tools/worker_main.cpp)
  target_link_libraries(er_worker PRIVATE er_transport er_core)
  if(ER_BUILD_CUDA)
    target_link_libraries(er_worker PRIVATE er_cuda)
    target_compile_definitions(er_worker PRIVATE ER_HAS_CUDA=1)
  endif()
  add_executable(er_controller src/tools/controller_main.cpp)
  target_link_libraries(er_controller PRIVATE er_transport er_core)
  if(ER_BUILD_CUDA)
    target_link_libraries(er_controller PRIVATE er_cuda)
  endif()
endif()

if(ER_BUILD_TOOLS)
  add_executable(er_cli src/tools/cli_main.cpp)
  target_link_libraries(er_cli PRIVATE er_core)
  if(ER_BUILD_TRANSPORT)
    target_link_libraries(er_cli PRIVATE er_transport)
  endif()
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(ER_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(ER_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ER_BUILD_TESTS AND BUILD_TESTING)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Installation and export
# ---------------------------------------------------------------------------
set(ER_EXPORT_TARGETS er_core)
if(ER_BUILD_TRANSPORT)
  list(APPEND ER_EXPORT_TARGETS er_transport)
endif()
if(ER_BUILD_CUDA)
  list(APPEND ER_EXPORT_TARGETS er_cuda)
endif()

install(TARGETS ${ER_EXPORT_TARGETS}
  EXPORT EngineResidencyTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT EngineResidencyTargets
  NAMESPACE EngineResidency::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EngineResidency
  FILE EngineResidencyTargets.cmake)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EngineResidencyConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/EngineResidencyConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EngineResidency)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/EngineResidencyConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/EngineResidencyConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/EngineResidencyConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EngineResidency)

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/EngineResidency)
