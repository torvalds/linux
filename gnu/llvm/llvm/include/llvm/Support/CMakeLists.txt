find_first_existing_vc_file("${LLVM_MAIN_SRC_DIR}" llvm_vc)

# The VC revision include that we want to generate.
set(version_inc "${CMAKE_CURRENT_BINARY_DIR}/VCSRevision.h")

set(generate_vcs_version_script "${LLVM_CMAKE_DIR}/GenerateVersionFromVCS.cmake")

if(LLVM_APPEND_VC_REV)
  set(llvm_source_dir ${LLVM_MAIN_SRC_DIR})

  # A fake version file and is not expected to exist. It is being used to
  # force regeneration of VCSRevision.h for source directory with no write
  # permission available.
  if (llvm_vc STREQUAL "")
    set(fake_version_inc "${CMAKE_CURRENT_BINARY_DIR}/__FakeVCSRevision.h")
  endif()
endif()

set(generated_files "${version_inc}")
if (fake_version_inc)
  list(APPEND generated_files "${fake_version_inc}")
endif()

# Create custom target to generate the VC revision include.
if (fake_version_inc)
  add_custom_command(OUTPUT "${version_inc}" "${fake_version_inc}"
    DEPENDS "${llvm_vc}" "${generate_vcs_version_script}"
    COMMAND ${CMAKE_COMMAND} "-DNAMES=LLVM"
                             "-DLLVM_SOURCE_DIR=${llvm_source_dir}"
                             "-DHEADER_FILE=${version_inc}"
                             "-DLLVM_FORCE_VC_REVISION=${LLVM_FORCE_VC_REVISION}"
                             "-DLLVM_FORCE_VC_REPOSITORY=${LLVM_FORCE_VC_REPOSITORY}"
                             -P "${generate_vcs_version_script}")
else()
  add_custom_command(OUTPUT "${version_inc}"
    DEPENDS "${llvm_vc}" "${generate_vcs_version_script}"
    COMMAND ${CMAKE_COMMAND} "-DNAMES=LLVM"
                             "-DLLVM_SOURCE_DIR=${llvm_source_dir}"
                             "-DHEADER_FILE=${version_inc}"
                             "-DLLVM_FORCE_VC_REVISION=${LLVM_FORCE_VC_REVISION}"
                             "-DLLVM_FORCE_VC_REPOSITORY=${LLVM_FORCE_VC_REPOSITORY}"
                             -P "${generate_vcs_version_script}")
endif()

# Mark the generated header as being generated.
set_source_files_properties("${version_inc}"
  PROPERTIES GENERATED TRUE
             HEADER_FILE_ONLY TRUE)

add_custom_target(llvm_vcsrevision_h ALL DEPENDS "${generated_files}")
set_target_properties(llvm_vcsrevision_h PROPERTIES FOLDER "LLVM/Resources")
