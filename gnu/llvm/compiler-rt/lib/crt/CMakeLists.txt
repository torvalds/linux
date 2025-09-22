if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.13.4)
  if ("${CMAKE_VERSION}" VERSION_LESS "3.20.0")
    message(WARNING
      "Your CMake version is ${CMAKE_VERSION}. Starting with LLVM 17.0.0, the "
      "minimum version of CMake required to build LLVM will become 3.20.0, and "
      "using an older CMake will become an error. Please upgrade your CMake to "
      "at least 3.20.0 now to avoid issues in the future!")
  endif()

  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  project(CompilerRTCRT C)
  set(COMPILER_RT_STANDALONE_BUILD TRUE)
  set(COMPILER_RT_CRT_STANDALONE_BUILD TRUE)

  set(COMPILER_RT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

  set(LLVM_COMMON_CMAKE_UTILS "${COMPILER_RT_SOURCE_DIR}/../cmake")

  # Add path for custom modules
  list(INSERT CMAKE_MODULE_PATH 0
    "${COMPILER_RT_SOURCE_DIR}/cmake"
    "${COMPILER_RT_SOURCE_DIR}/cmake/Modules"
    "${LLVM_COMMON_CMAKE_UTILS}"
    "${LLVM_COMMON_CMAKE_UTILS}/Modules"
    )

  include(base-config-ix)
  include(CompilerRTUtils)

  load_llvm_config()
  construct_compiler_rt_default_triple()

  include(SetPlatformToolchainTools)
  include(AddCompilerRT)
endif()

include(crt-config-ix)

if(COMPILER_RT_HAS_CRT)
  add_compiler_rt_component(crt)

  include(CheckSectionExists)
  check_section_exists(".init_array" COMPILER_RT_HAS_INITFINI_ARRAY
    SOURCE "volatile int x;\n__attribute__((constructor)) void f(void) {x = 0;}\nint main(void) { return 0; }\n")

  append_list_if(COMPILER_RT_HAS_STD_C11_FLAG -std=c11 CRT_CFLAGS)
  append_list_if(COMPILER_RT_HAS_INITFINI_ARRAY -DCRT_HAS_INITFINI_ARRAY CRT_CFLAGS)
  append_list_if(COMPILER_RT_CRT_USE_EH_FRAME_REGISTRY -DEH_USE_FRAME_REGISTRY CRT_CFLAGS)
  append_list_if(COMPILER_RT_HAS_FPIC_FLAG -fPIC CRT_CFLAGS)
  append_list_if(COMPILER_RT_HAS_WNO_PEDANTIC -Wno-pedantic CRT_CFLAGS)
  if (COMPILER_RT_HAS_FCF_PROTECTION_FLAG)
    append_list_if(COMPILER_RT_ENABLE_CET -fcf-protection=full CRT_CFLAGS)
  endif()

  foreach(arch ${CRT_SUPPORTED_ARCH})
    add_compiler_rt_runtime(clang_rt.crtbegin
      OBJECT
      ARCHS ${arch}
      SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/crtbegin.c
      CFLAGS ${CRT_CFLAGS}
      PARENT_TARGET crt)
    add_compiler_rt_runtime(clang_rt.crtend
      OBJECT
      ARCHS ${arch}
      SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/crtend.c
      CFLAGS ${CRT_CFLAGS}
      PARENT_TARGET crt)
  endforeach()
endif()
