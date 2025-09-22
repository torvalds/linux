# Generate C code in the file provided as OUTPUT that describes the properties
# of all components. This C code is suitable for inclusion in `llvm-config`
function(LLVMBuildGenerateCFragment)
  cmake_parse_arguments(ARG "" "OUTPUT" "" ${ARGN})

  # Write C header
  #################
  get_property(llvmbuild_components GLOBAL PROPERTY LLVM_COMPONENT_LIBS)
  foreach(llvmbuild_component ${llvmbuild_components})
    string(REGEX REPLACE "^LLVM" "" component_name ${llvmbuild_component})
    list(APPEND all_component_libdeps ${component_name})
  endforeach()
  list(APPEND llvmbuild_components all)
  foreach(llvm_component all-targets Engine Native NativeCodeGen ${LLVM_TARGETS_TO_BUILD})
    list(APPEND llvmbuild_components ${llvm_component})
    list(APPEND all_component_libdeps ${llvm_component})
  endforeach()

  list(LENGTH llvmbuild_components llvmbuild_components_size)
  file(WRITE ${ARG_OUTPUT}
  "
  struct AvailableComponent {
    /// The name of the component.
    const char *Name;

    /// The name of the library for this component (or NULL).
    const char *Library;

    /// Whether the component is installed.
    bool IsInstalled;

    /// The list of libraries required when linking this component.
    const char *RequiredLibraries[${llvmbuild_components_size}];
  } AvailableComponents[${llvmbuild_components_size}] = {
  ")

  foreach(llvmbuild_component ${llvmbuild_components})
    if(llvmbuild_component STREQUAL "all")
      unset(llvmbuild_libname)
      set(llvmbuild_libdeps ${all_component_libdeps})
    else()
      get_property(llvmbuild_libname TARGET ${llvmbuild_component} PROPERTY LLVM_COMPONENT_NAME)
      get_property(llvmbuild_libdeps TARGET ${llvmbuild_component} PROPERTY LLVM_LINK_COMPONENTS)
    endif()
    string(TOLOWER ${llvmbuild_component} llvmbuild_componentname)

    if(NOT llvmbuild_libname)
      set(llvmbuild_llvmlibname nullptr)
      string(TOLOWER ${llvmbuild_component} llvmbuild_libname)
    else()
      set(llvmbuild_llvmlibname "\"LLVM${llvmbuild_libname}\"")
      string(TOLOWER ${llvmbuild_libname} llvmbuild_libname)
    endif()

    set(llvmbuild_clibdeps "")
    foreach(llvmbuild_libdep ${llvmbuild_libdeps})
      get_property(llvmbuild_libdepname GLOBAL PROPERTY LLVM_COMPONENT_NAME_${llvmbuild_libdep})
      if(NOT llvmbuild_libdepname)
        string(TOLOWER ${llvmbuild_libdep} llvmbuild_clibdep)
      else()
        string(TOLOWER ${llvmbuild_libdepname} llvmbuild_clibdep)
      endif()
      list(APPEND llvmbuild_clibdeps ${llvmbuild_clibdep})
    endforeach()

    list(TRANSFORM llvmbuild_clibdeps PREPEND "\"")
    list(TRANSFORM llvmbuild_clibdeps APPEND "\"")
    list(JOIN llvmbuild_clibdeps ", " llvmbuild_clibdeps_joint)
    list(APPEND llvmbuild_centries "{ \"${llvmbuild_libname}\", ${llvmbuild_llvmlibname}, true, {${llvmbuild_clibdeps_joint}} },\n")
  endforeach()

  list(SORT llvmbuild_centries)
  foreach(llvmbuild_centry ${llvmbuild_centries})
    file(APPEND ${ARG_OUTPUT} "${llvmbuild_centry}")
  endforeach()
  file(APPEND ${ARG_OUTPUT} "};")
endfunction()

# Resolve cross-component dependencies, for each available component.
function(LLVMBuildResolveComponentsLink)

  # the native target may not be enabled when cross compiling
  if(TARGET ${LLVM_NATIVE_ARCH})
    get_property(llvm_has_jit_native TARGET ${LLVM_NATIVE_ARCH} PROPERTY LLVM_HAS_JIT)
  else()
    set(llvm_has_jit_native OFF)
  endif()

  if(llvm_has_jit_native)
    set_property(TARGET Engine APPEND PROPERTY LLVM_LINK_COMPONENTS "MCJIT" "Native")
  else()
    set_property(TARGET Engine APPEND PROPERTY LLVM_LINK_COMPONENTS "Interpreter")
  endif()

  get_property(llvm_components GLOBAL PROPERTY LLVM_COMPONENT_LIBS)
  foreach(llvm_component ${llvm_components})
    get_property(link_components TARGET ${llvm_component} PROPERTY LLVM_LINK_COMPONENTS)
    llvm_map_components_to_libnames(llvm_libs ${link_components})
    if(llvm_libs)
      get_property(libtype TARGET ${llvm_component} PROPERTY LLVM_LIBTYPE)
      target_link_libraries(${llvm_component} ${libtype} ${llvm_libs})
    endif()
  endforeach()
endfunction()
