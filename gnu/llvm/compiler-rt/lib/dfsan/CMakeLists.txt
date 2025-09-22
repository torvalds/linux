include_directories(..)

# Runtime library sources and build flags.
set(DFSAN_RTL_SOURCES
  dfsan.cpp
  dfsan_allocator.cpp
  dfsan_chained_origin_depot.cpp
  dfsan_custom.cpp
  dfsan_interceptors.cpp
  dfsan_new_delete.cpp
  dfsan_thread.cpp
  )

set(DFSAN_RTL_HEADERS
  dfsan.h
  dfsan_allocator.h
  dfsan_chained_origin_depot.h
  dfsan_flags.inc
  dfsan_flags.h
  dfsan_platform.h
  dfsan_thread.h
  )

set(DFSAN_COMMON_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF DFSAN_COMMON_CFLAGS)
# Prevent clang from generating libc calls.
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding DFSAN_COMMON_CFLAGS)

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format DFSAN_COMMON_CFLAGS)

# Static runtime library.
add_compiler_rt_component(dfsan)

foreach(arch ${DFSAN_SUPPORTED_ARCH})
  set(DFSAN_CFLAGS ${DFSAN_COMMON_CFLAGS})
  append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE DFSAN_CFLAGS)
  add_compiler_rt_runtime(clang_rt.dfsan
    STATIC
    ARCHS ${arch}
    SOURCES ${DFSAN_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>
    ADDITIONAL_HEADERS ${DFSAN_RTL_HEADERS}
    CFLAGS ${DFSAN_CFLAGS}
    PARENT_TARGET dfsan)
  add_sanitizer_rt_symbols(clang_rt.dfsan
    ARCHS ${arch}
    EXTRA dfsan.syms.extra)
  add_dependencies(dfsan
    clang_rt.dfsan-${arch}-symbols)
endforeach()

set(dfsan_abilist_dir ${COMPILER_RT_OUTPUT_DIR}/share)
set(dfsan_abilist_filename ${dfsan_abilist_dir}/dfsan_abilist.txt)
add_custom_target(dfsan_abilist ALL
  DEPENDS ${dfsan_abilist_filename})
add_custom_command(OUTPUT ${dfsan_abilist_filename}
                   VERBATIM
                   COMMAND
                    ${CMAKE_COMMAND} -E make_directory ${dfsan_abilist_dir}
                   COMMAND
                    ${CMAKE_COMMAND} -E cat
                         ${CMAKE_CURRENT_SOURCE_DIR}/done_abilist.txt
                         ${CMAKE_CURRENT_SOURCE_DIR}/libc_ubuntu1404_abilist.txt
                         > ${dfsan_abilist_filename}
                   DEPENDS done_abilist.txt libc_ubuntu1404_abilist.txt)
add_dependencies(dfsan dfsan_abilist)
install(FILES ${dfsan_abilist_filename}
        DESTINATION ${COMPILER_RT_INSTALL_DATA_DIR})
