include_directories(..)

set(LSAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF LSAN_CFLAGS)

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format LSAN_CFLAGS)

set(LSAN_COMMON_SOURCES
  lsan_common.cpp
  lsan_common_fuchsia.cpp
  lsan_common_linux.cpp
  lsan_common_mac.cpp
  )

set(LSAN_SOURCES
  lsan.cpp
  lsan_allocator.cpp
  lsan_fuchsia.cpp
  lsan_interceptors.cpp
  lsan_linux.cpp
  lsan_mac.cpp
  lsan_malloc_mac.cpp
  lsan_posix.cpp
  lsan_preinit.cpp
  lsan_thread.cpp
  )

set(LSAN_HEADERS
  lsan.h
  lsan_allocator.h
  lsan_common.h
  lsan_flags.inc
  lsan_thread.h
  )

set(LSAN_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Used by asan/hwasan even without COMPILER_RT_HAS_LSAN.
add_compiler_rt_object_libraries(RTLSanCommon
    OS ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${LSAN_COMMON_SUPPORTED_ARCH}
    SOURCES ${LSAN_COMMON_SOURCES}
    ADDITIONAL_HEADERS ${LSAN_HEADERS}
    CFLAGS ${LSAN_CFLAGS})

if(COMPILER_RT_HAS_LSAN)
  add_compiler_rt_component(lsan)
  if(APPLE)
    set(LSAN_LINK_LIBS ${SANITIZER_COMMON_LINK_LIBS})

    add_weak_symbols("lsan" WEAK_SYMBOL_LINK_FLAGS)
    add_weak_symbols("sanitizer_common" WEAK_SYMBOL_LINK_FLAGS)

    add_compiler_rt_runtime(clang_rt.lsan
      SHARED
      OS ${LSAN_SUPPORTED_OS}
      ARCHS ${LSAN_SUPPORTED_ARCH}
      SOURCES ${LSAN_SOURCES}
      ADDITIONAL_HEADERS ${LSAN_HEADERS}
      OBJECT_LIBS RTLSanCommon
                  RTInterception
                  RTSanitizerCommon
                  RTSanitizerCommonLibc
                  RTSanitizerCommonCoverage
                  RTSanitizerCommonSymbolizer
      CFLAGS ${LSAN_CFLAGS}
      LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS} ${WEAK_SYMBOL_LINK_FLAGS}
      LINK_LIBS ${LSAN_LINK_LIBS}
      PARENT_TARGET lsan)
  else()
    foreach(arch ${LSAN_SUPPORTED_ARCH})
      add_compiler_rt_runtime(clang_rt.lsan
        STATIC
        ARCHS ${arch}
        SOURCES ${LSAN_SOURCES}
                $<TARGET_OBJECTS:RTInterception.${arch}>
                $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
                $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
                $<TARGET_OBJECTS:RTSanitizerCommonCoverage.${arch}>
                $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
                $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>
                $<TARGET_OBJECTS:RTLSanCommon.${arch}>
        ADDITIONAL_HEADERS ${LSAN_HEADERS}
        CFLAGS ${LSAN_CFLAGS}
        PARENT_TARGET lsan)
    endforeach()
  endif()
endif()
