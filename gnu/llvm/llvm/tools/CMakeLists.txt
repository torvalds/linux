# This file will recurse into all subdirectories that contain CMakeLists.txt
# Setting variables that match the pattern LLVM_TOOL_{NAME}_BUILD to Off will
# prevent traversing into a directory.
#
# The only tools that need to be explicitly added are ones that have explicit
# ordering requirements.

# Iterates all the subdirectories to create CMake options to enable/disable
# traversing each directory.
create_llvm_tool_options()

if(NOT LLVM_BUILD_LLVM_DYLIB AND NOT LLVM_BUILD_LLVM_C_DYLIB)
  set(LLVM_TOOL_LLVM_SHLIB_BUILD Off)
endif()

if(NOT LLVM_USE_INTEL_JITEVENTS )
  set(LLVM_TOOL_LLVM_JITLISTENER_BUILD Off)
endif()

if(CYGWIN OR NOT LLVM_ENABLE_PIC)
  set(LLVM_TOOL_LTO_BUILD Off)
endif()

if (LLVM_TOOL_LLVM_DRIVER_BUILD)
  add_llvm_tool(llvm-driver)
endif()

# Add LTO, llvm-ar, llvm-config, and llvm-profdata before clang, ExternalProject
# requires targets specified in DEPENDS to exist before the call to
# ExternalProject_Add.
add_llvm_tool_subdirectory(lto)
add_llvm_tool_subdirectory(gold)
add_llvm_tool_subdirectory(llvm-ar)
add_llvm_tool_subdirectory(llvm-config)
add_llvm_tool_subdirectory(llvm-lto)
add_llvm_tool_subdirectory(llvm-profdata)

# Projects supported via LLVM_EXTERNAL_*_SOURCE_DIR need to be explicitly
# specified.
add_llvm_external_project(lld)
add_llvm_external_project(mlir)
# ClangIR and Flang depend on mlir, lldb and Flang depend on clang, sort them
# accordingly so place them afterwards
add_llvm_external_project(clang)
add_llvm_external_project(flang)
add_llvm_external_project(lldb)
add_llvm_external_project(bolt)

# Automatically add remaining sub-directories containing a 'CMakeLists.txt'
# file as external projects.
add_llvm_implicit_projects()

add_llvm_external_project(polly)

# libclc depends on clang
add_llvm_external_project(libclc)

# Add subprojects specified using LLVM_EXTERNAL_PROJECTS
foreach(p ${LLVM_EXTERNAL_PROJECTS})
  add_llvm_external_project(${p})
endforeach(p)

set(LLVM_COMMON_DEPENDS ${LLVM_COMMON_DEPENDS} PARENT_SCOPE)

if (LLVM_TOOL_LLVM_DRIVER_BUILD)
  # This is explicitly added at the end _after_ all tool projects so that it can
  # scrape up tools from other projects into itself.
  add_subdirectory(llvm-driver)
  # This must be here otherwise CMake complains in add_llvm_tool_symlink that
  # it can't add_custom_command that happens after llvm-driver is built because
  # llvm-driver was not created in that directory.
  generate_driver_tool_targets()
endif()
