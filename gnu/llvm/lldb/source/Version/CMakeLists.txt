if(LLDB_VERSION_STRING)
  set(LLDB_FULL_VERSION_STRING ${LLDB_VERSION_STRING})
endif()

# Configure the VCSVersion.inc file.
set(vcs_version_inc "${CMAKE_CURRENT_BINARY_DIR}/VCSVersion.inc")
set(generate_vcs_version_script "${LLVM_CMAKE_DIR}/GenerateVersionFromVCS.cmake")

find_first_existing_vc_file("${LLDB_SOURCE_DIR}" lldb_vc)

if(lldb_vc AND LLVM_APPEND_VC_REV)
  set(lldb_source_dir ${LLDB_SOURCE_DIR})
endif()

add_custom_command(OUTPUT "${vcs_version_inc}"
  DEPENDS "${lldb_vc}" "${generate_vcs_version_script}"
  COMMAND ${CMAKE_COMMAND} "-DNAMES=LLDB"
                           "-DLLDB_SOURCE_DIR=${lldb_source_dir}"
                           "-DHEADER_FILE=${vcs_version_inc}"
                           "-DLLVM_FORCE_VC_REVISION=${LLVM_FORCE_VC_REVISION}"
                           "-DLLVM_FORCE_VC_REPOSITORY=${LLVM_FORCE_VC_REPOSITORY}"
                           -P "${generate_vcs_version_script}")

set_source_files_properties("${vcs_version_inc}"
  PROPERTIES GENERATED TRUE
             HEADER_FILE_ONLY TRUE)

# Configure the Version.inc file.
set(version_inc "${LLDB_BINARY_DIR}/include/lldb/Version/Version.inc")

configure_file(
  ${LLDB_SOURCE_DIR}/include/lldb/Version/Version.inc.in
  ${version_inc})

set_source_files_properties("${version_inc}"
  PROPERTIES GENERATED TRUE
             HEADER_FILE_ONLY TRUE)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_lldb_library(lldbVersion NO_PLUGIN_DEPENDENCIES
  Version.cpp
  ${vcs_version_inc}
  ${version_inc})
