include(AddFileDependencies)
include(CMakeParseArguments)

function(llvm_replace_compiler_option var old new)
  # Replaces a compiler option or switch `old' in `var' by `new'.
  # If `old' is not in `var', appends `new' to `var'.
  # Example: llvm_replace_compiler_option(CMAKE_CXX_FLAGS_RELEASE "-O3" "-O2")
  # If the option already is on the variable, don't add it:
  if( "${${var}}" MATCHES "(^| )${new}($| )" )
    set(n "")
  else()
    set(n "${new}")
  endif()
  if( "${${var}}" MATCHES "(^| )${old}($| )" )
    string( REGEX REPLACE "(^| )${old}($| )" " ${n} " ${var} "${${var}}" )
  else()
    set( ${var} "${${var}} ${n}" )
  endif()
  set( ${var} "${${var}}" PARENT_SCOPE )
endfunction(llvm_replace_compiler_option)

macro(add_td_sources srcs)
  file(GLOB tds *.td)
  if( tds )
    source_group("TableGen descriptions" FILES ${tds})
    set_source_files_properties(${tds} PROPERTIES HEADER_FILE_ONLY ON)
    list(APPEND ${srcs} ${tds})
  endif()
endmacro(add_td_sources)

function(add_header_files_for_glob hdrs_out glob)
  file(GLOB hds ${glob})
  set(filtered)
  foreach(file ${hds})
    # Explicit existence check is necessary to filter dangling symlinks
    # out.  See https://bugs.gentoo.org/674662.
    if(EXISTS ${file})
      list(APPEND filtered ${file})
    endif()
  endforeach()
  set(${hdrs_out} ${filtered} PARENT_SCOPE)
endfunction(add_header_files_for_glob)

function(find_all_header_files hdrs_out additional_headerdirs)
  add_header_files_for_glob(hds *.h)
  list(APPEND all_headers ${hds})

  foreach(additional_dir ${additional_headerdirs})
    add_header_files_for_glob(hds "${additional_dir}/*.h")
    list(APPEND all_headers ${hds})
    add_header_files_for_glob(hds "${additional_dir}/*.inc")
    list(APPEND all_headers ${hds})
  endforeach(additional_dir)

  set( ${hdrs_out} ${all_headers} PARENT_SCOPE )
endfunction(find_all_header_files)


function(llvm_process_sources OUT_VAR)
  cmake_parse_arguments(ARG "PARTIAL_SOURCES_INTENDED" "" "ADDITIONAL_HEADERS;ADDITIONAL_HEADER_DIRS" ${ARGN})
  set(sources ${ARG_UNPARSED_ARGUMENTS})
  llvm_check_source_file_list(${sources})

  # This adds .td and .h files to the Visual Studio solution:
  add_td_sources(sources)
  find_all_header_files(hdrs "${ARG_ADDITIONAL_HEADER_DIRS}")
  if (hdrs)
    set_source_files_properties(${hdrs} PROPERTIES HEADER_FILE_ONLY ON)
  endif()
  set_source_files_properties(${ARG_ADDITIONAL_HEADERS} PROPERTIES HEADER_FILE_ONLY ON)
  list(APPEND sources ${ARG_ADDITIONAL_HEADERS} ${hdrs})

  set( ${OUT_VAR} ${sources} PARENT_SCOPE )
endfunction(llvm_process_sources)


function(llvm_check_source_file_list)
  cmake_parse_arguments(ARG "" "SOURCE_DIR" "" ${ARGN})
  foreach(l ${ARG_UNPARSED_ARGUMENTS})
      get_filename_component(fp ${l} REALPATH)
      list(APPEND listed ${fp})
  endforeach()

  if(ARG_SOURCE_DIR)
    file(GLOB globbed
         "${ARG_SOURCE_DIR}/*.c" "${ARG_SOURCE_DIR}/*.cpp")
  else()
    file(GLOB globbed *.c *.cpp)
  endif()

  set_property(DIRECTORY APPEND PROPERTY LLVM_SOURCE_FILES ${listed})
  if (ARG_PARTIAL_SOURCES_INTENDED) # llvm_process_source's scope
    return()
  endif()
  get_directory_property(listed LLVM_SOURCE_FILES)

  foreach(g ${globbed})
    get_filename_component(fn ${g} NAME)
    if(ARG_SOURCE_DIR)
      set(entry "${g}")
    else()
      set(entry "${fn}")
    endif()
    get_filename_component(gp ${g} REALPATH)

    # Don't reject hidden files. Some editors create backups in the
    # same directory as the file.
    if (NOT "${fn}" MATCHES "^\\.")
      if(NOT ${entry} IN_LIST LLVM_OPTIONAL_SOURCES)
        if(NOT ${gp} IN_LIST listed)
          if(ARG_SOURCE_DIR)
              set(fn_relative "${ARG_SOURCE_DIR}/${fn}")
          else()
              set(fn_relative "${fn}")
          endif()
          message(SEND_ERROR "Found erroneous configuration for source file ${fn_relative}
LLVM's build system enforces that all source files are added to a build target, \
that exactly one build target exists in each directory, \
and that this target lists all files in that directory. \
If you want multiple targets in the same directory, add \
PARTIAL_SOURCES_INTENDED to the target specification, though it is discouraged.
Please update ${CMAKE_CURRENT_LIST_FILE}\n")
        endif()
      endif()
    endif()
  endforeach()
endfunction(llvm_check_source_file_list)
