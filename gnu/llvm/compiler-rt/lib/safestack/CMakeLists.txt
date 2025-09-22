add_compiler_rt_component(safestack)

set(SAFESTACK_SOURCES
  safestack.cpp
  )

include_directories(..)

set(SAFESTACK_CFLAGS ${SANITIZER_COMMON_CFLAGS})

foreach(arch ${SAFESTACK_SUPPORTED_ARCH})
  add_compiler_rt_runtime(clang_rt.safestack
    STATIC
    ARCHS ${arch}
    SOURCES ${SAFESTACK_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
    # Intentionally does not deppend on sanitizer_common,
    # to keep runtime trivial, and acceptable for security
    # sensitive applications.
    CFLAGS ${SAFESTACK_CFLAGS}
    PARENT_TARGET safestack)
endforeach()
