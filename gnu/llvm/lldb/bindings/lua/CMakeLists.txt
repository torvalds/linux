add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/LLDBWrapLua.cpp
  DEPENDS ${SWIG_SOURCES}
  DEPENDS ${SWIG_INTERFACES}
  DEPENDS ${SWIG_HEADERS}
  DEPENDS lldb-sbapi-dwarf-enums
  COMMAND ${SWIG_EXECUTABLE}
      ${SWIG_COMMON_FLAGS}
      -I${CMAKE_CURRENT_SOURCE_DIR}
      -lua
      -w503
      -outdir ${CMAKE_CURRENT_BINARY_DIR}
      -o ${CMAKE_CURRENT_BINARY_DIR}/LLDBWrapLua.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/lua.swig
  VERBATIM
  COMMENT "Building LLDB Lua wrapper")

add_custom_target(swig_wrapper_lua ALL DEPENDS
  ${CMAKE_CURRENT_BINARY_DIR}/LLDBWrapLua.cpp
)

function(create_lua_package swig_target working_dir pkg_dir)
  cmake_parse_arguments(ARG "NOINIT" "" "FILES" ${ARGN})
  add_custom_command(TARGET ${swig_target} POST_BUILD VERBATIM
    COMMAND ${CMAKE_COMMAND} -E make_directory ${pkg_dir}
    WORKING_DIRECTORY ${working_dir})
endfunction()

function(finish_swig_lua swig_target lldb_lua_bindings_dir lldb_lua_target_dir)
  add_custom_target(${swig_target} ALL VERBATIM
    COMMAND ${CMAKE_COMMAND} -E make_directory ${lldb_lua_target_dir}
    DEPENDS swig_wrapper_lua liblldb
    COMMENT "LLDB Lua API")
  if(LLDB_BUILD_FRAMEWORK)
    set(LIBLLDB_SYMLINK_DEST "${LLDB_FRAMEWORK_ABSOLUTE_BUILD_DIR}/LLDB.framework/LLDB")
  else()
    set(LIBLLDB_SYMLINK_DEST "${LLVM_SHLIB_OUTPUT_INTDIR}/liblldb${CMAKE_SHARED_LIBRARY_SUFFIX}")
  endif()
  if(WIN32)
    set(LIBLLDB_SYMLINK_OUTPUT_FILE "lldb.dll")
  else()
    set(LIBLLDB_SYMLINK_OUTPUT_FILE "lldb.so")
  endif()
  create_relative_symlink(${swig_target} ${LIBLLDB_SYMLINK_DEST}
                          ${lldb_lua_target_dir} ${LIBLLDB_SYMLINK_OUTPUT_FILE})
  set(lldb_lua_library_target "${swig_target}-library")
  add_custom_target(${lldb_lua_library_target})
  add_dependencies(${lldb_lua_library_target} ${swig_target})

  # Ensure we do the Lua post-build step when building lldb.
  add_dependencies(lldb ${swig_target})

  if(LLDB_BUILD_FRAMEWORK)
    set(LLDB_LUA_INSTALL_PATH ${LLDB_FRAMEWORK_INSTALL_DIR}/LLDB.framework/Resources/Lua)
  else()
    set(LLDB_LUA_INSTALL_PATH ${LLDB_LUA_RELATIVE_PATH})
  endif()
  install(DIRECTORY ${lldb_lua_target_dir}/
          DESTINATION ${LLDB_LUA_INSTALL_PATH}
          COMPONENT ${lldb_lua_library_target})

  set(lldb_lua_library_install_target "install-${lldb_lua_library_target}")
  if (NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(${lldb_lua_library_install_target}
                            COMPONENT ${lldb_lua_library_target}
                            DEPENDS ${lldb_lua_library_target})
  endif()
endfunction()
