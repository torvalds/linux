cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW)

function(get_system_libs return_var)
  message(AUTHOR_WARNING "get_system_libs no longer needed")
  set(${return_var} "" PARENT_SCOPE)
endfunction()


function(link_system_libs target)
  message(AUTHOR_WARNING "link_system_libs no longer needed")
endfunction()

# is_llvm_target_library(
#   library
#     Name of the LLVM library to check
#   return_var
#     Output variable name
#   ALL_TARGETS;INCLUDED_TARGETS;OMITTED_TARGETS
#     ALL_TARGETS - default looks at the full list of known targets
#     INCLUDED_TARGETS - looks only at targets being configured
#     OMITTED_TARGETS - looks only at targets that are not being configured
# )
function(is_llvm_target_library library return_var)
  cmake_parse_arguments(ARG "ALL_TARGETS;INCLUDED_TARGETS;OMITTED_TARGETS" "" "" ${ARGN})
  # Sets variable `return_var' to ON if `library' corresponds to a
  # LLVM supported target. To OFF if it doesn't.
  set(${return_var} OFF PARENT_SCOPE)
  string(TOUPPER "${library}" capitalized_lib)
  if(ARG_INCLUDED_TARGETS)
    string(TOUPPER "${LLVM_TARGETS_TO_BUILD}" targets)
  elseif(ARG_OMITTED_TARGETS)
    set(omitted_targets ${LLVM_ALL_TARGETS})
    if (LLVM_TARGETS_TO_BUILD)
      list(REMOVE_ITEM omitted_targets ${LLVM_TARGETS_TO_BUILD})
    endif()
    string(TOUPPER "${omitted_targets}" targets)
  else()
    string(TOUPPER "${LLVM_ALL_TARGETS}" targets)
  endif()
  foreach(t ${targets})
    if( capitalized_lib STREQUAL t OR
        capitalized_lib STREQUAL "${t}" OR
        capitalized_lib STREQUAL "${t}DESC" OR
        capitalized_lib STREQUAL "${t}CODEGEN" OR
        capitalized_lib STREQUAL "${t}ASMPARSER" OR
        capitalized_lib STREQUAL "${t}ASMPRINTER" OR
        capitalized_lib STREQUAL "${t}DISASSEMBLER" OR
        capitalized_lib STREQUAL "${t}INFO" OR
        capitalized_lib STREQUAL "${t}UTILS" )
      set(${return_var} ON PARENT_SCOPE)
      break()
    endif()
  endforeach()
endfunction(is_llvm_target_library)

function(is_llvm_target_specifier library return_var)
  is_llvm_target_library(${library} ${return_var} ${ARGN})
  string(TOUPPER "${library}" capitalized_lib)
  if(NOT ${return_var})
    if( capitalized_lib STREQUAL "ALLTARGETSASMPARSERS" OR
        capitalized_lib STREQUAL "ALLTARGETSDESCS" OR
        capitalized_lib STREQUAL "ALLTARGETSDISASSEMBLERS" OR
        capitalized_lib STREQUAL "ALLTARGETSINFOS" OR
        capitalized_lib STREQUAL "NATIVE" OR
        capitalized_lib STREQUAL "NATIVECODEGEN" )
      set(${return_var} ON PARENT_SCOPE)
    endif()
  endif()
endfunction()

macro(llvm_config executable)
  cmake_parse_arguments(ARG "USE_SHARED" "" "" ${ARGN})
  set(link_components ${ARG_UNPARSED_ARGUMENTS})

  if(ARG_USE_SHARED)
    # If USE_SHARED is specified, then we link against libLLVM,
    # but also against the component libraries below. This is
    # done in case libLLVM does not contain all of the components
    # the target requires.
    #
    # Strip LLVM_DYLIB_COMPONENTS out of link_components.
    # To do this, we need special handling for "all", since that
    # may imply linking to libraries that are not included in
    # libLLVM.

    if (DEFINED link_components AND DEFINED LLVM_DYLIB_COMPONENTS)
      if("${LLVM_DYLIB_COMPONENTS}" STREQUAL "all")
        set(link_components "")
      else()
        list(REMOVE_ITEM link_components ${LLVM_DYLIB_COMPONENTS})
      endif()
    endif()

    target_link_libraries(${executable} PRIVATE LLVM)
  endif()

  explicit_llvm_config(${executable} ${link_components})
endmacro(llvm_config)


function(explicit_llvm_config executable)
  set( link_components ${ARGN} )

  llvm_map_components_to_libnames(LIBRARIES ${link_components})
  get_target_property(t ${executable} TYPE)
  if(t STREQUAL "STATIC_LIBRARY")
    target_link_libraries(${executable} INTERFACE ${LIBRARIES})
  elseif(t STREQUAL "EXECUTABLE" OR t STREQUAL "SHARED_LIBRARY" OR t STREQUAL "MODULE_LIBRARY")
    target_link_libraries(${executable} PRIVATE ${LIBRARIES})
  else()
    # Use plain form for legacy user.
    target_link_libraries(${executable} ${LIBRARIES})
  endif()
endfunction(explicit_llvm_config)


# This is Deprecated
function(llvm_map_components_to_libraries OUT_VAR)
  message(AUTHOR_WARNING "Using llvm_map_components_to_libraries() is deprecated. Use llvm_map_components_to_libnames() instead")
  explicit_map_components_to_libraries(result ${ARGN})
  set( ${OUT_VAR} ${result} ${sys_result} PARENT_SCOPE )
endfunction(llvm_map_components_to_libraries)

# Expand pseudo-components into real components.
# Does not cover 'native', 'backend', or 'engine' as these require special
# handling. Also does not cover 'all' as we only have a list of the libnames
# available and not a list of the components.
function(llvm_expand_pseudo_components out_components)
  set( link_components ${ARGN} )
  if(NOT LLVM_AVAILABLE_LIBS)
    # Inside LLVM itself available libs are in a global property.
    get_property(LLVM_AVAILABLE_LIBS GLOBAL PROPERTY LLVM_LIBS)
  endif()
  foreach(c ${link_components})
    # add codegen, asmprinter, asmparser, disassembler
    if("${c}" IN_LIST LLVM_TARGETS_TO_BUILD)
      if(LLVM${c}CodeGen IN_LIST LLVM_AVAILABLE_LIBS)
        list(APPEND expanded_components "${c}CodeGen")
      else()
        if(LLVM${c} IN_LIST LLVM_AVAILABLE_LIBS)
          list(APPEND expanded_components "${c}")
        else()
          message(FATAL_ERROR "Target ${c} is not in the set of libraries.")
        endif()
      endif()
      foreach(subcomponent IN ITEMS AsmPrinter AsmParser Desc Disassembler Info Utils)
        if(LLVM${c}${subcomponent} IN_LIST LLVM_AVAILABLE_LIBS)
          list(APPEND expanded_components "${c}${subcomponent}")
        endif()
      endforeach()
    elseif("${c}" STREQUAL "nativecodegen" )
      foreach(subcomponent IN ITEMS CodeGen Desc Info)
        if(LLVM${LLVM_NATIVE_ARCH}${subcomponent} IN_LIST LLVM_AVAILABLE_LIBS)
          list(APPEND expanded_components "${LLVM_NATIVE_ARCH}${subcomponent}")
        endif()
      endforeach()
    elseif("${c}" STREQUAL "AllTargetsCodeGens" )
      # Link all the codegens from all the targets
      foreach(t ${LLVM_TARGETS_TO_BUILD})
        if( TARGET LLVM${t}CodeGen)
          list(APPEND expanded_components "${t}CodeGen")
        endif()
      endforeach(t)
    elseif("${c}" STREQUAL "AllTargetsAsmParsers" )
      # Link all the asm parsers from all the targets
      foreach(t ${LLVM_TARGETS_TO_BUILD})
        if(LLVM${t}AsmParser IN_LIST LLVM_AVAILABLE_LIBS)
          list(APPEND expanded_components "${t}AsmParser")
        endif()
      endforeach(t)
    elseif( "${c}" STREQUAL "AllTargetsDescs" )
      # Link all the descs from all the targets
      foreach(t ${LLVM_TARGETS_TO_BUILD})
        if(LLVM${t}Desc IN_LIST LLVM_AVAILABLE_LIBS)
          list(APPEND expanded_components "${t}Desc")
        endif()
      endforeach(t)
    elseif("${c}" STREQUAL "AllTargetsDisassemblers" )
      # Link all the disassemblers from all the targets
      foreach(t ${LLVM_TARGETS_TO_BUILD})
        if(LLVM${t}Disassembler IN_LIST LLVM_AVAILABLE_LIBS)
          list(APPEND expanded_components "${t}Disassembler")
        endif()
      endforeach(t)
    elseif("${c}" STREQUAL "AllTargetsInfos" )
      # Link all the infos from all the targets
      foreach(t ${LLVM_TARGETS_TO_BUILD})
        if(LLVM${t}Info IN_LIST LLVM_AVAILABLE_LIBS)
          list(APPEND expanded_components "${t}Info")
        endif()
      endforeach(t)
    elseif("${c}" STREQUAL "AllTargetsMCAs" )
      # Link all the TargetMCAs from all the targets
      foreach(t ${LLVM_TARGETS_TO_BUILD})
        if( TARGET LLVM${t}TargetMCA )
          list(APPEND expanded_components "${t}TargetMCA")
        endif()
      endforeach(t)
    else()
      list(APPEND expanded_components "${c}")
    endif()
  endforeach()
  set(${out_components} ${expanded_components} PARENT_SCOPE)
endfunction(llvm_expand_pseudo_components out_components)

# This is a variant intended for the final user:
# Map LINK_COMPONENTS to actual libnames.
function(llvm_map_components_to_libnames out_libs)
  set( link_components ${ARGN} )
  if(NOT LLVM_AVAILABLE_LIBS)
    # Inside LLVM itself available libs are in a global property.
    get_property(LLVM_AVAILABLE_LIBS GLOBAL PROPERTY LLVM_LIBS)
  endif()
  string(TOUPPER "${LLVM_AVAILABLE_LIBS}" capitalized_libs)

  get_property(LLVM_TARGETS_CONFIGURED GLOBAL PROPERTY LLVM_TARGETS_CONFIGURED)

  # Generally in our build system we avoid order-dependence. Unfortunately since
  # not all targets create the same set of libraries we actually need to ensure
  # that all build targets associated with a target are added before we can
  # process target dependencies.
  if(NOT LLVM_TARGETS_CONFIGURED)
    foreach(c ${link_components})
      is_llvm_target_specifier("${c}" iltl_result ALL_TARGETS)
      if(iltl_result)
        message(FATAL_ERROR "Specified target library before target registration is complete.")
      endif()
    endforeach()
  endif()

  # Expand some keywords:
  if(engine IN_LIST link_components)
    if(${LLVM_NATIVE_ARCH} IN_LIST LLVM_TARGETS_TO_BUILD AND
       ${LLVM_NATIVE_ARCH} IN_LIST LLVM_TARGETS_WITH_JIT)
      list(APPEND link_components "jit")
      list(APPEND link_components "native")
    else()
      list(APPEND link_components "interpreter")
    endif()
  endif()
  if(native IN_LIST link_components AND "${LLVM_NATIVE_ARCH}" IN_LIST LLVM_TARGETS_TO_BUILD)
    list(APPEND link_components ${LLVM_NATIVE_ARCH})
  endif()

  # Translate symbolic component names to real libraries:
  llvm_expand_pseudo_components(link_components ${link_components})
  foreach(c ${link_components})
    get_property(c_rename GLOBAL PROPERTY LLVM_COMPONENT_NAME_${c})
    if(c_rename)
        set(c ${c_rename})
    endif()
    if("${c}" STREQUAL "native" )
      # already processed
    elseif("${c}" STREQUAL "backend" )
      # same case as in `native'.
    elseif("${c}" STREQUAL "engine" )
      # already processed
    elseif("${c}" STREQUAL "all" )
      get_property(all_components GLOBAL PROPERTY LLVM_COMPONENT_LIBS)
      list(APPEND expanded_components ${all_components})
    else()
      # Canonize the component name:
      string(TOUPPER "${c}" capitalized)
      list(FIND capitalized_libs LLVM${capitalized} lib_idx)
      if( lib_idx LESS 0 )
        # The component is unknown. Maybe is an omitted target?
        is_llvm_target_library("${c}" iltl_result OMITTED_TARGETS)
        if(iltl_result)
          # A missing library to a directly referenced omitted target would be bad.
          message(FATAL_ERROR "Library '${c}' is a direct reference to a target library for an omitted target.")
        else()
          # If it is not an omitted target we should assume it is a component
          # that hasn't yet been processed by CMake. Missing components will
          # cause errors later in the configuration, so we can safely assume
          # that this is valid here.
          list(APPEND expanded_components LLVM${c})
        endif()
      else( lib_idx LESS 0 )
        list(GET LLVM_AVAILABLE_LIBS ${lib_idx} canonical_lib)
        list(APPEND expanded_components ${canonical_lib})
      endif( lib_idx LESS 0 )
    endif("${c}" STREQUAL "native" )
  endforeach(c)

  set(${out_libs} ${expanded_components} PARENT_SCOPE)
endfunction()

# Perform a post-order traversal of the dependency graph.
# This duplicates the algorithm used by llvm-config, originally
# in tools/llvm-config/llvm-config.cpp, function ComputeLibsForComponents.
function(expand_topologically name required_libs visited_libs)
  if(NOT ${name} IN_LIST visited_libs)
    list(APPEND visited_libs ${name})
    set(visited_libs ${visited_libs} PARENT_SCOPE)

    #
    get_property(libname GLOBAL PROPERTY LLVM_COMPONENT_NAME_${name})
    if(libname)
      set(cname LLVM${libname})
    elseif(TARGET ${name})
      set(cname ${name})
    elseif(TARGET LLVM${name})
      set(cname LLVM${name})
    else()
      message(FATAL_ERROR "unknown component ${name}")
    endif()

    get_property(lib_deps TARGET ${cname} PROPERTY LLVM_LINK_COMPONENTS)
    foreach( lib_dep ${lib_deps} )
      expand_topologically(${lib_dep} "${required_libs}" "${visited_libs}")
      set(required_libs ${required_libs} PARENT_SCOPE)
      set(visited_libs ${visited_libs} PARENT_SCOPE)
    endforeach()

    list(APPEND required_libs ${cname})
    set(required_libs ${required_libs} PARENT_SCOPE)
  endif()
endfunction()

# Expand dependencies while topologically sorting the list of libraries:
function(llvm_expand_dependencies out_libs)
  set(expanded_components ${ARGN})

  set(required_libs)
  set(visited_libs)
  foreach( lib ${expanded_components} )
    expand_topologically(${lib} "${required_libs}" "${visited_libs}")
  endforeach()

  if(required_libs)
    list(REVERSE required_libs)
  endif()
  set(${out_libs} ${required_libs} PARENT_SCOPE)
endfunction()

function(explicit_map_components_to_libraries out_libs)
  llvm_map_components_to_libnames(link_libs ${ARGN})
  llvm_expand_dependencies(expanded_components ${link_libs})
  # Return just the libraries included in this build:
  set(result)
  foreach(c ${expanded_components})
    if( TARGET ${c} )
      set(result ${result} ${c})
    endif()
  endforeach(c)
  set(${out_libs} ${result} PARENT_SCOPE)
endfunction(explicit_map_components_to_libraries)

cmake_policy(POP)
