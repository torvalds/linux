foreach(arch ${SANITIZER_COMMON_SUPPORTED_ARCH})
  get_target_flags_for_arch(${arch} TARGET_CFLAGS)

  set(RTSanitizerCommonSymbolizerInternalDir
      "${CMAKE_CURRENT_BINARY_DIR}/RTSanitizerCommonSymbolizerInternal.${arch}")
  add_custom_command(OUTPUT ${RTSanitizerCommonSymbolizerInternalDir}
                     COMMAND ${CMAKE_COMMAND} -E make_directory ${RTSanitizerCommonSymbolizerInternalDir})

  add_custom_command(OUTPUT RTSanitizerCommonSymbolizerInternal.${arch}.o
                     DEPENDS ${RTSanitizerCommonSymbolizerInternalDir} 
                             sanitizer_wrappers.cpp
                             sanitizer_symbolize.cpp
                             scripts/build_symbolizer.sh
                     WORKING_DIRECTORY ${RTSanitizerCommonSymbolizerInternalDir}
                     COMMAND FLAGS=${TARGET_CFLAGS}
                             CLANG=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang
                             ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_symbolizer.sh
                             ${CMAKE_CURRENT_BINARY_DIR}/RTSanitizerCommonSymbolizerInternal.${arch}.o
                     USES_TERMINAL)

  add_custom_target(RTSanitizerCommonSymbolizerInternalObj.${arch}
                    DEPENDS RTSanitizerCommonSymbolizerInternal.${arch}.o)

  set_property(TARGET RTSanitizerCommonSymbolizerInternal.${arch}
               PROPERTY IMPORTED_OBJECTS ${CMAKE_CURRENT_BINARY_DIR}/RTSanitizerCommonSymbolizerInternal.${arch}.o)
endforeach()
