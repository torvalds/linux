# LLVM_TARGET_DEFINITIONS must contain the name of the .td file to process,
# while LLVM_TARGET_DEPENDS may contain additional file dependencies.
# Extra parameters for `tblgen' may come after `ofn' parameter.
# Adds the name of the generated file to TABLEGEN_OUTPUT.
include(LLVMDistributionSupport)

# Clear out any pre-existing compile_commands file before processing. This
# allows for generating a clean compile_commands on each configure.
file(REMOVE ${CMAKE_BINARY_DIR}/tablegen_compile_commands.yml)

function(tablegen project ofn)
  cmake_parse_arguments(ARG "" "" "DEPENDS;EXTRA_INCLUDES" ${ARGN})

  # Override ${project} with ${project}_TABLEGEN_PROJECT
  if(NOT "${${project}_TABLEGEN_PROJECT}" STREQUAL "")
    set(project ${${project}_TABLEGEN_PROJECT})
  endif()

  # Validate calling context.
  if(NOT ${project}_TABLEGEN_EXE)
    message(FATAL_ERROR "${project}_TABLEGEN_EXE not set")
  endif()

  # Use depfile instead of globbing arbitrary *.td(s) for Ninja.
  if(CMAKE_GENERATOR MATCHES "Ninja")
    # Make output path relative to build.ninja, assuming located on
    # ${CMAKE_BINARY_DIR}.
    # CMake emits build targets as relative paths but Ninja doesn't identify
    # absolute path (in *.d) as relative path (in build.ninja)
    # Note that tblgen is executed on ${CMAKE_BINARY_DIR} as working directory.
    file(RELATIVE_PATH ofn_rel
      ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/${ofn})
    set(additional_cmdline
      -o ${ofn_rel}
      -d ${ofn_rel}.d
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPFILE ${CMAKE_CURRENT_BINARY_DIR}/${ofn}.d
      )
    set(local_tds)
    set(global_tds)
  else()
    file(GLOB local_tds "*.td")
    file(GLOB_RECURSE global_tds "${LLVM_MAIN_INCLUDE_DIR}/llvm/*.td")
    set(additional_cmdline
      -o ${CMAKE_CURRENT_BINARY_DIR}/${ofn}
      )
  endif()

  if (IS_ABSOLUTE ${LLVM_TARGET_DEFINITIONS})
    set(LLVM_TARGET_DEFINITIONS_ABSOLUTE ${LLVM_TARGET_DEFINITIONS})
  else()
    set(LLVM_TARGET_DEFINITIONS_ABSOLUTE
      ${CMAKE_CURRENT_SOURCE_DIR}/${LLVM_TARGET_DEFINITIONS})
  endif()
  if (LLVM_ENABLE_DAGISEL_COV AND "-gen-dag-isel" IN_LIST ARGN)
    list(APPEND LLVM_TABLEGEN_FLAGS "-instrument-coverage")
  endif()
  if (LLVM_ENABLE_GISEL_COV AND "-gen-global-isel" IN_LIST ARGN)
    list(APPEND LLVM_TABLEGEN_FLAGS "-instrument-gisel-coverage")
    list(APPEND LLVM_TABLEGEN_FLAGS "-gisel-coverage-file=${LLVM_GISEL_COV_PREFIX}all")
  endif()
  if (LLVM_OMIT_DAGISEL_COMMENTS AND "-gen-dag-isel" IN_LIST ARGN)
    list(APPEND LLVM_TABLEGEN_FLAGS "-omit-comments")
  endif()

  # MSVC can't support long string literals ("long" > 65534 bytes)[1], so if there's
  # a possibility of generated tables being consumed by MSVC, generate arrays of
  # char literals, instead. If we're cross-compiling, then conservatively assume
  # that the source might be consumed by MSVC.
  # [1] https://docs.microsoft.com/en-us/cpp/cpp/compiler-limits?view=vs-2017
  if (MSVC AND project STREQUAL LLVM)
    list(APPEND LLVM_TABLEGEN_FLAGS "--long-string-literals=0")
  endif()
  if (CMAKE_GENERATOR MATCHES "Visual Studio")
    # Visual Studio has problems with llvm-tblgen's native --write-if-changed
    # behavior. Since it doesn't do restat optimizations anyway, just don't
    # pass --write-if-changed there.
    set(tblgen_change_flag)
  else()
    set(tblgen_change_flag "--write-if-changed")
  endif()

  if (NOT LLVM_ENABLE_WARNINGS)
    list(APPEND LLVM_TABLEGEN_FLAGS "-no-warn-on-unused-template-args")
  endif()

  # We need both _TABLEGEN_TARGET and _TABLEGEN_EXE in the  DEPENDS list
  # (both the target and the file) to have .inc files rebuilt on
  # a tablegen change, as cmake does not propagate file-level dependencies
  # of custom targets. See the following ticket for more information:
  # https://cmake.org/Bug/view.php?id=15858
  # The dependency on both, the target and the file, produces the same
  # dependency twice in the result file when
  # ("${${project}_TABLEGEN_TARGET}" STREQUAL "${${project}_TABLEGEN_EXE}")
  # but lets us having smaller and cleaner code here.
  get_directory_property(tblgen_includes INCLUDE_DIRECTORIES)
  list(APPEND tblgen_includes ${ARG_EXTRA_INCLUDES})

  # Get the current set of include paths for this td file.
  cmake_parse_arguments(ARG "" "" "DEPENDS;EXTRA_INCLUDES" ${ARGN})
  get_directory_property(tblgen_includes INCLUDE_DIRECTORIES)
  list(APPEND tblgen_includes ${ARG_EXTRA_INCLUDES})
  # Filter out any empty include items.
  list(REMOVE_ITEM tblgen_includes "")

  # Build the absolute path for the current input file.
  if (IS_ABSOLUTE ${LLVM_TARGET_DEFINITIONS})
    set(LLVM_TARGET_DEFINITIONS_ABSOLUTE ${LLVM_TARGET_DEFINITIONS})
  else()
    set(LLVM_TARGET_DEFINITIONS_ABSOLUTE ${CMAKE_CURRENT_SOURCE_DIR}/${LLVM_TARGET_DEFINITIONS})
  endif()

  # Append this file and its includes to the compile commands file.
  # This file is used by the TableGen LSP Language Server (tblgen-lsp-server).
  file(APPEND ${CMAKE_BINARY_DIR}/tablegen_compile_commands.yml
      "--- !FileInfo:\n"
      "  filepath: \"${LLVM_TARGET_DEFINITIONS_ABSOLUTE}\"\n"
      "  includes: \"${CMAKE_CURRENT_SOURCE_DIR};${tblgen_includes}\"\n"
  )

  # Filter out empty items before prepending each entry with -I
  list(REMOVE_ITEM tblgen_includes "")
  list(TRANSFORM tblgen_includes PREPEND -I)

  set(tablegen_exe ${${project}_TABLEGEN_EXE})
  set(tablegen_depends ${${project}_TABLEGEN_TARGET} ${tablegen_exe})

  if(LLVM_PARALLEL_TABLEGEN_JOBS)
    set(LLVM_TABLEGEN_JOB_POOL JOB_POOL tablegen_job_pool)
  else()
    set(LLVM_TABLEGEN_JOB_POOL "")
  endif()

  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${ofn}
    COMMAND ${tablegen_exe} ${ARG_UNPARSED_ARGUMENTS} -I ${CMAKE_CURRENT_SOURCE_DIR}
    ${tblgen_includes}
    ${LLVM_TABLEGEN_FLAGS}
    ${LLVM_TARGET_DEFINITIONS_ABSOLUTE}
    ${tblgen_change_flag}
    ${additional_cmdline}
    # The file in LLVM_TARGET_DEFINITIONS may be not in the current
    # directory and local_tds may not contain it, so we must
    # explicitly list it here:
    DEPENDS ${ARG_DEPENDS} ${tablegen_depends}
      ${local_tds} ${global_tds}
    ${LLVM_TARGET_DEFINITIONS_ABSOLUTE}
    ${LLVM_TARGET_DEPENDS}
    ${LLVM_TABLEGEN_JOB_POOL}
    COMMENT "Building ${ofn}..."
    )

  # `make clean' must remove all those generated files:
  set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${ofn})

  set(TABLEGEN_OUTPUT ${TABLEGEN_OUTPUT} ${CMAKE_CURRENT_BINARY_DIR}/${ofn} PARENT_SCOPE)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${ofn} PROPERTIES
    GENERATED 1)
endfunction()

# Creates a target for publicly exporting tablegen dependencies.
function(add_public_tablegen_target target)
  if(NOT TABLEGEN_OUTPUT)
    message(FATAL_ERROR "Requires tablegen() definitions as TABLEGEN_OUTPUT.")
  endif()
  add_custom_target(${target}
    DEPENDS ${TABLEGEN_OUTPUT})
  if(LLVM_COMMON_DEPENDS)
    add_dependencies(${target} ${LLVM_COMMON_DEPENDS})
  endif()
  get_subproject_title(subproject_title)
  set_target_properties(${target} PROPERTIES FOLDER "${subproject_title}/Tablegenning")
  set(LLVM_COMMON_DEPENDS ${LLVM_COMMON_DEPENDS} ${target} PARENT_SCOPE)
endfunction()

macro(add_tablegen target project)
  cmake_parse_arguments(ADD_TABLEGEN "" "DESTINATION;EXPORT" "" ${ARGN})

  set(${target}_OLD_LLVM_LINK_COMPONENTS ${LLVM_LINK_COMPONENTS})
  set(LLVM_LINK_COMPONENTS ${LLVM_LINK_COMPONENTS} TableGen)

  add_llvm_executable(${target} DISABLE_LLVM_LINK_LLVM_DYLIB
    ${ADD_TABLEGEN_UNPARSED_ARGUMENTS})
  set(LLVM_LINK_COMPONENTS ${${target}_OLD_LLVM_LINK_COMPONENTS})

  set(${project}_TABLEGEN_DEFAULT "${target}")
  if (LLVM_NATIVE_TOOL_DIR)
    if (EXISTS "${LLVM_NATIVE_TOOL_DIR}/${target}${LLVM_HOST_EXECUTABLE_SUFFIX}")
      set(${project}_TABLEGEN_DEFAULT "${LLVM_NATIVE_TOOL_DIR}/${target}${LLVM_HOST_EXECUTABLE_SUFFIX}")
    endif()
  endif()

  # FIXME: Quick fix to reflect LLVM_TABLEGEN to llvm-min-tblgen
  if("${target}" STREQUAL "llvm-min-tblgen"
      AND NOT "${LLVM_TABLEGEN}" STREQUAL ""
      AND NOT "${LLVM_TABLEGEN}" STREQUAL "llvm-tblgen")
    set(${project}_TABLEGEN_DEFAULT "${LLVM_TABLEGEN}")
  endif()

  if(ADD_TABLEGEN_EXPORT)
    set(${project}_TABLEGEN "${${project}_TABLEGEN_DEFAULT}" CACHE
      STRING "Native TableGen executable. Saves building one when cross-compiling.")
  else()
    # Internal tablegen
    set(${project}_TABLEGEN "${${project}_TABLEGEN_DEFAULT}")
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL ON)
  endif()

  # Effective tblgen executable to be used:
  set(${project}_TABLEGEN_EXE ${${project}_TABLEGEN} PARENT_SCOPE)
  set(${project}_TABLEGEN_TARGET ${${project}_TABLEGEN} PARENT_SCOPE)

  if(LLVM_USE_HOST_TOOLS)
    if( ${${project}_TABLEGEN} STREQUAL "${target}" )
      # The NATIVE tablegen executable *must* depend on the current target one
      # otherwise the native one won't get rebuilt when the tablgen sources
      # change, and we end up with incorrect builds.
      build_native_tool(${target} ${project}_TABLEGEN_EXE DEPENDS ${target})
      set(${project}_TABLEGEN_EXE ${${project}_TABLEGEN_EXE} PARENT_SCOPE)

      add_custom_target(${target}-host DEPENDS ${${project}_TABLEGEN_EXE})
      get_subproject_title(subproject_title)
      set_target_properties(${target}-host PROPERTIES FOLDER "${subproject_title}/Native")
      set(${project}_TABLEGEN_TARGET ${target}-host PARENT_SCOPE)

      # If we're using the host tablegen, and utils were not requested, we have no
      # need to build this tablegen.
      if ( NOT LLVM_BUILD_UTILS )
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL ON)
      endif()
    endif()
  endif()

  if (ADD_TABLEGEN_DESTINATION AND NOT LLVM_INSTALL_TOOLCHAIN_ONLY AND
      (LLVM_BUILD_UTILS OR ${target} IN_LIST LLVM_DISTRIBUTION_COMPONENTS))
    set(export_arg)
    if(ADD_TABLEGEN_EXPORT)
      get_target_export_arg(${target} ${ADD_TABLEGEN_EXPORT} export_arg)
    endif()
    install(TARGETS ${target}
            ${export_arg}
            COMPONENT ${target}
            RUNTIME DESTINATION "${ADD_TABLEGEN_DESTINATION}")
    if(NOT LLVM_ENABLE_IDE)
      add_llvm_install_targets("install-${target}"
                               DEPENDS ${target}
                               COMPONENT ${target})
    endif()
  endif()
  if(ADD_TABLEGEN_EXPORT)
    string(TOUPPER ${ADD_TABLEGEN_EXPORT} export_upper)
    set_property(GLOBAL APPEND PROPERTY ${export_upper}_EXPORTS ${target})
  endif()
endmacro()
