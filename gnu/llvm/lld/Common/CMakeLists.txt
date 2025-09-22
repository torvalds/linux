find_first_existing_vc_file("${LLVM_MAIN_SRC_DIR}" llvm_vc)
find_first_existing_vc_file("${LLD_SOURCE_DIR}" lld_vc)

set(version_inc "${CMAKE_CURRENT_BINARY_DIR}/VCSVersion.inc")
set(generate_vcs_version_script "${LLVM_CMAKE_DIR}/GenerateVersionFromVCS.cmake")

if(lld_vc AND LLVM_APPEND_VC_REV)
  set(lld_source_dir ${LLD_SOURCE_DIR})
endif()

add_custom_command(OUTPUT "${version_inc}"
  DEPENDS "${lld_vc}" "${generate_vcs_version_script}"
  COMMAND ${CMAKE_COMMAND} "-DNAMES=LLD"
  "-DLLD_SOURCE_DIR=${lld_source_dir}"
  "-DHEADER_FILE=${version_inc}"
  "-DLLVM_FORCE_VC_REVISION=${LLVM_FORCE_VC_REVISION}"
  "-DLLVM_FORCE_VC_REPOSITORY=${LLVM_FORCE_VC_REPOSITORY}"
  -P "${generate_vcs_version_script}")

# Mark the generated header as being generated.
set_source_files_properties("${version_inc}"
  PROPERTIES GENERATED TRUE
  HEADER_FILE_ONLY TRUE)

add_lld_library(lldCommon
  Args.cpp
  CommonLinkerContext.cpp
  DriverDispatcher.cpp
  DWARF.cpp
  ErrorHandler.cpp
  Filesystem.cpp
  Memory.cpp
  Reproduce.cpp
  Strings.cpp
  TargetOptionsCommandFlags.cpp
  Timer.cpp
  VCSVersion.inc
  Version.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLD_INCLUDE_DIR}/lld/Common

  LINK_COMPONENTS
  Codegen
  Core
  DebugInfoDWARF
  Demangle
  MC
  Option
  Support
  Target
  TargetParser

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}
  ${LLVM_ATOMIC_LIB}

  DEPENDS
  intrinsics_gen
  )
