include(CMakeParseArguments)
include(CompilerRTUtils)

function(get_aix_libatomic_default_link_flags link_flags export_list)
set(linkopts
  -Wl,-H512 -Wl,-D0
  -Wl,-T512 -Wl,-bhalt:4 -Wl,-bernotok
  -Wl,-bnoentry -Wl,-bexport:${export_list}
  -Wl,-bmodtype:SRE -Wl,-lc)
  # Add `-Wl,-G`. Quoted from release notes of cmake-3.16.0
  # > On AIX, runtime linking is no longer enabled by default.
  # See https://cmake.org/cmake/help/latest/release/3.16.html
  set(linkopts -Wl,-G ${linkopts})
  set(${link_flags} ${linkopts} PARENT_SCOPE)
endfunction()

function(get_aix_libatomic_type type)
  set(${type} MODULE PARENT_SCOPE)
endfunction()

macro(archive_aix_libatomic name libname)
  cmake_parse_arguments(LIB
    ""
    ""
    "ARCHS;PARENT_TARGET"
    ${ARGN})
  set(objects_to_archive "")
  foreach (arch ${LIB_ARCHS})
    if(CAN_TARGET_${arch})
      set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/${libname}-${arch}.dir")
      # FIXME: Target name should be kept consistent with definition
      # in AddCompilerRT.cmake added by
      # add_compiler_rt_runtime(<name> SHARED ...)
      set(target ${name}-dynamic-${arch})
      if(TARGET ${target})
        file(MAKE_DIRECTORY ${output_dir})
        add_custom_command(OUTPUT "${output_dir}/libatomic.so.1"
                           POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E
                           copy "$<TARGET_FILE:${target}>"
                                "${output_dir}/libatomic.so.1"
                           # If built with MODULE, F_LOADONLY is set.
                           # We have to remove this flag at POST_BUILD.
                           COMMAND ${CMAKE_STRIP} -X32_64 -E
                                "${output_dir}/libatomic.so.1"
                           DEPENDS ${target})
        list(APPEND objects_to_archive "${output_dir}/libatomic.so.1")
      endif()
    endif()
  endforeach()
  if(objects_to_archive)
    set(output_dir "")
    set(install_dir "")
    # If LLVM defines top level library directory, we want to deliver
    # libatomic.a at top level. See `llvm/cmake/modules/AddLLVM.cmake'
    # setting _install_rpath on AIX for reference.
    if(LLVM_LIBRARY_OUTPUT_INTDIR AND CMAKE_INSTALL_PREFIX)
      set(output_dir "${LLVM_LIBRARY_OUTPUT_INTDIR}")
      set(install_dir "${CMAKE_INSTALL_PREFIX}/lib${LLVM_LIBDIR_SUFFIX}")
    else()
      get_compiler_rt_output_dir(${COMPILER_RT_DEFAULT_TARGET_ARCH} output_dir)
      get_compiler_rt_install_dir(${COMPILER_RT_DEFAULT_TARGET_ARCH} install_dir)
    endif()
    add_custom_command(OUTPUT "${output_dir}/${libname}.a"
                       COMMAND ${CMAKE_AR} -X32_64 r "${output_dir}/${libname}.a"
                       ${objects_to_archive}
                       DEPENDS ${objects_to_archive})
    install(FILES "${output_dir}/${libname}.a"
            DESTINATION ${install_dir})
    add_custom_target(aix-${libname}
                      DEPENDS "${output_dir}/${libname}.a")
    add_dependencies(${LIB_PARENT_TARGET} aix-${libname})
  endif()
endmacro()
