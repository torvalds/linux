add_compiler_rt_component(ctx_profile)

set(CTX_PROFILE_SOURCES
  CtxInstrProfiling.cpp
  )

set(CTX_PROFILE_HEADERS
  CtxInstrContextNode.h
  CtxInstrProfiling.h
  )

include_directories(..)
include_directories(../../include)

# We don't use the C++ Standard Library here, so avoid including it by mistake.
append_list_if(COMPILER_RT_HAS_NOSTDINCXX_FLAG -nostdinc++ EXTRA_FLAGS)

# __sanitizer_siginfo
append_list_if(COMPILER_RT_HAS_WGNU_ANONYMOUS_STRUCT_FLAG -Wno-gnu-anonymous-struct EXTRA_FLAGS)

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()

add_compiler_rt_runtime(clang_rt.ctx_profile
  STATIC
  ARCHS ${CTX_PROFILE_SUPPORTED_ARCH}
  OBJECT_LIBS RTSanitizerCommon RTSanitizerCommonLibc
  CFLAGS ${EXTRA_FLAGS}
  SOURCES ${CTX_PROFILE_SOURCES}
  ADDITIONAL_HEADERS ${CTX_PROFILE_HEADERS}
  PARENT_TARGET ctx_profile)
