# Build for the runtime interception helper library.

set(INTERCEPTION_SOURCES
  interception_linux.cpp
  interception_mac.cpp
  interception_win.cpp
  interception_type_test.cpp
  )

set(INTERCEPTION_HEADERS
  interception.h
  interception_linux.h
  interception_mac.h
  interception_win.h
  )

include_directories(..)

set(INTERCEPTION_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF INTERCEPTION_CFLAGS)

# Silence warnings in system headers with MSVC.
if(NOT CLANG_CL)
  append_list_if(COMPILER_RT_HAS_EXTERNAL_FLAG "/experimental:external;/external:W0;/external:anglebrackets" INTERCEPTION_CFLAGS)
endif()

add_compiler_rt_object_libraries(RTInterception
    OS ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
    SOURCES ${INTERCEPTION_SOURCES}
    ADDITIONAL_HEADERS ${INTERCEPTION_HEADERS}
    CFLAGS ${INTERCEPTION_CFLAGS})

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
