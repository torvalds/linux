add_compiler_rt_component(cfi)

if(OS_NAME MATCHES "Linux" OR OS_NAME MATCHES "FreeBSD" OR OS_NAME MATCHES "NetBSD")
  set(CFI_SOURCES
    cfi.cpp
    )

  include_directories(..)

  set(CFI_CFLAGS
    ${SANITIZER_COMMON_CFLAGS}
  )

  # Too many existing bugs, needs cleanup.
  append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format CFI_CFLAGS)

  set(CFI_DIAG_CFLAGS
    -DCFI_ENABLE_DIAG=1
  )

  foreach(arch ${CFI_SUPPORTED_ARCH})
    add_compiler_rt_runtime(clang_rt.cfi
      STATIC
      ARCHS ${arch}
      SOURCES ${CFI_SOURCES}
      OBJECT_LIBS RTInterception
                  RTSanitizerCommon
                  RTSanitizerCommonLibc
      CFLAGS ${CFI_CFLAGS}
      PARENT_TARGET cfi)
    add_compiler_rt_runtime(clang_rt.cfi_diag
      STATIC
      ARCHS ${arch}
      SOURCES ${CFI_SOURCES}
      OBJECT_LIBS RTInterception
                  RTSanitizerCommon
                  RTSanitizerCommonLibc
                  RTSanitizerCommonCoverage
                  RTSanitizerCommonSymbolizer
                  RTUbsan
      CFLAGS ${CFI_CFLAGS} ${CFI_DIAG_CFLAGS}
      PARENT_TARGET cfi)
  endforeach()
endif()

add_compiler_rt_resource_file(cfi_ignorelist cfi_ignorelist.txt cfi)
