include(CompilerRTUtils)

set(SANITIZER_GEN_DYNAMIC_LIST
  ${COMPILER_RT_SOURCE_DIR}/lib/sanitizer_common/scripts/gen_dynamic_list.py)

if(CMAKE_NM)
  set(SANITIZER_NM "${CMAKE_NM}")
else()
  set(SANITIZER_NM nm)
endif()

# Create a target "<name>-<arch>-symbols" that would generate the list of
# symbols that need to be exported from sanitizer runtime "<name>". Function
# interceptors are exported automatically, user can also provide files with
# symbol names that should be exported as well.
#   add_sanitizer_rt_symbols(<name>
#                            ARCHS <architectures>
#                            PARENT_TARGET <convenience parent target>
#                            EXTRA <files with extra symbols to export>)
macro(add_sanitizer_rt_symbols name)
  cmake_parse_arguments(ARG
    ""
    "PARENT_TARGET"
    "ARCHS;EXTRA"
    ${ARGN})
  foreach(arch ${ARG_ARCHS})
    set(target_name ${name}-${arch})
    set(stamp ${CMAKE_CURRENT_BINARY_DIR}/${target_name}.syms-stamp)
    set(extra_args)
    foreach(arg ${ARG_EXTRA})
      list(APPEND extra_args "--extra" ${arg})
    endforeach()
    add_custom_command(OUTPUT ${stamp}
      COMMAND ${Python3_EXECUTABLE}
        ${SANITIZER_GEN_DYNAMIC_LIST} ${extra_args} $<TARGET_FILE:${target_name}>
        --nm-executable "${SANITIZER_NM}" -o $<TARGET_FILE:${target_name}>.syms
      COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
      DEPENDS ${target_name} ${SANITIZER_GEN_DYNAMIC_LIST} ${ARG_EXTRA}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating exported symbols for ${target_name}"
      VERBATIM)
    add_custom_target(${target_name}-symbols ALL
      DEPENDS ${stamp}
      SOURCES ${SANITIZER_GEN_DYNAMIC_LIST} ${ARG_EXTRA})
    get_compiler_rt_install_dir(${arch} install_dir)
    install(FILES $<TARGET_FILE:${target_name}>.syms
            DESTINATION ${install_dir})
    if(ARG_PARENT_TARGET)
      add_dependencies(${ARG_PARENT_TARGET} ${target_name}-symbols)
    endif()
  endforeach()
endmacro()

# This function is only used on Darwin, where undefined symbols must be specified
# in the linker invocation.
function(add_weak_symbols libname link_flags)
  set(weak_symbols_file "${COMPILER_RT_SOURCE_DIR}/lib/${libname}/weak_symbols.txt")
  file(STRINGS  "${weak_symbols_file}" WEAK_SYMBOLS)
  # Add this file as a configure-time dependency so that changes to this
  # file trigger a re-configure. This is necessary so that `${link_flags}`
  # is changed when appropriate.
  set_property(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${weak_symbols_file}")
  set(local_link_flags ${${link_flags}})
  foreach(SYMBOL ${WEAK_SYMBOLS})
    set(local_link_flags ${local_link_flags} -Wl,-U,${SYMBOL})
  endforeach()
  set(${link_flags} ${local_link_flags} PARENT_SCOPE)
endfunction()

macro(add_sanitizer_rt_version_list name)
  set(vers ${CMAKE_CURRENT_BINARY_DIR}/${name}.vers)
  cmake_parse_arguments(ARG "" "" "LIBS;EXTRA" ${ARGN})
  set(args)
  foreach(arg ${ARG_EXTRA})
    list(APPEND args "--extra" ${arg})
  endforeach()
  foreach(arg ${ARG_LIBS})
    list(APPEND args "$<TARGET_FILE:${arg}>")
  endforeach()
  add_custom_command(OUTPUT ${vers}
    COMMAND ${Python3_EXECUTABLE}
      ${SANITIZER_GEN_DYNAMIC_LIST} --version-list ${args}
      --nm-executable "${SANITIZER_NM}" -o ${vers}
    DEPENDS ${SANITIZER_GEN_DYNAMIC_LIST} ${ARG_EXTRA} ${ARG_LIBS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating version list for ${name}"
    VERBATIM)

  add_custom_target(${name}-version-list ALL
    DEPENDS ${vers})
endmacro()
