set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  FrontendOpenMP
  )

find_first_existing_vc_file("${LLVM_MAIN_SRC_DIR}" llvm_vc)
find_first_existing_vc_file("${CLANG_SOURCE_DIR}" clang_vc)

# The VC revision include that we want to generate.
set(version_inc "${CMAKE_CURRENT_BINARY_DIR}/VCSVersion.inc")

set(generate_vcs_version_script "${LLVM_CMAKE_DIR}/GenerateVersionFromVCS.cmake")

if(llvm_vc AND LLVM_APPEND_VC_REV)
  set(llvm_source_dir ${LLVM_MAIN_SRC_DIR})
endif()
if (LLVM_VC_REPOSITORY AND LLVM_VC_REVISION)
  set(llvm_source_dir ${LLVM_SOURCE_DIR})
  set(llvm_vc_repository ${LLVM_VC_REPOSITORY})
  set(llvm_vc_revision ${LLVM_VC_REVISION})
endif()
if(clang_vc AND LLVM_APPEND_VC_REV)
  set(clang_source_dir ${CLANG_SOURCE_DIR})
endif()
if (CLANG_VC_REPOSITORY AND CLANG_VC_REVISION)
  set(clang_source_dir ${CLANG_SOURCE_DIR})
  set(clang_vc_repository ${CLANG_VC_REPOSITORY})
  set(clang_vc_revision ${CLANG_VC_REVISION})
endif()

# Create custom target to generate the VC revision include.
add_custom_command(OUTPUT "${version_inc}"
  DEPENDS "${llvm_vc}" "${clang_vc}" "${generate_vcs_version_script}"
  COMMAND ${CMAKE_COMMAND} "-DNAMES=\"LLVM;CLANG\""
                           "-DLLVM_SOURCE_DIR=${llvm_source_dir}"
                           "-DCLANG_SOURCE_DIR=${clang_source_dir}"
                           "-DCLANG_VC_REPOSITORY=${clang_vc_repository}"
                           "-DCLANG_VC_REVISION=${clang_vc_revision}"
                           "-DHEADER_FILE=${version_inc}"
                           "-DLLVM_VC_REPOSITORY=${llvm_vc_repository}"
                           "-DLLVM_VC_REVISION=${llvm_vc_revision}"
                           "-DLLVM_FORCE_VC_REVISION=${LLVM_FORCE_VC_REVISION}"
                           "-DLLVM_FORCE_VC_REPOSITORY=${LLVM_FORCE_VC_REPOSITORY}"
                           -P "${generate_vcs_version_script}")

# Mark the generated header as being generated.
set_source_files_properties("${version_inc}"
  PROPERTIES GENERATED TRUE
             HEADER_FILE_ONLY TRUE)

if(CLANG_VENDOR)
  set_source_files_properties(Version.cpp
    PROPERTIES COMPILE_DEFINITIONS "CLANG_VENDOR=\"${CLANG_VENDOR} \"")
endif()

add_clang_library(clangBasic
  ASTSourceDescriptor.cpp
  Attributes.cpp
  Builtins.cpp
  CLWarnings.cpp
  CharInfo.cpp
  CodeGenOptions.cpp
  Cuda.cpp
  DarwinSDKInfo.cpp
  Diagnostic.cpp
  DiagnosticIDs.cpp
  DiagnosticOptions.cpp
  ExpressionTraits.cpp
  FileEntry.cpp
  FileManager.cpp
  FileSystemStatCache.cpp
  IdentifierTable.cpp
  LangOptions.cpp
  LangStandards.cpp
  MakeSupport.cpp
  Module.cpp
  ObjCRuntime.cpp
  OpenCLOptions.cpp
  OpenMPKinds.cpp
  OperatorPrecedence.cpp
  ParsedAttrInfo.cpp
  ProfileList.cpp
  NoSanitizeList.cpp
  SanitizerSpecialCaseList.cpp
  Sanitizers.cpp
  Sarif.cpp
  SourceLocation.cpp
  SourceManager.cpp
  SourceMgrAdapter.cpp
  Stack.cpp
  TargetID.cpp
  TargetInfo.cpp
  Targets.cpp
  Targets/AArch64.cpp
  Targets/AMDGPU.cpp
  Targets/ARC.cpp
  Targets/ARM.cpp
  Targets/AVR.cpp
  Targets/BPF.cpp
  Targets/CSKY.cpp
  Targets/DirectX.cpp
  Targets/Hexagon.cpp
  Targets/Lanai.cpp
  Targets/Le64.cpp
  Targets/LoongArch.cpp
  Targets/M68k.cpp
  Targets/MSP430.cpp
  Targets/Mips.cpp
  Targets/NVPTX.cpp
  Targets/OSTargets.cpp
  Targets/PNaCl.cpp
  Targets/PPC.cpp
  Targets/RISCV.cpp
  Targets/SPIR.cpp
  Targets/Sparc.cpp
  Targets/SystemZ.cpp
  Targets/TCE.cpp
  Targets/VE.cpp
  Targets/WebAssembly.cpp
  Targets/X86.cpp
  Targets/XCore.cpp
  TokenKinds.cpp
  TypeTraits.cpp
  Version.cpp
  Warnings.cpp
  XRayInstr.cpp
  XRayLists.cpp
  ${version_inc}

  DEPENDS
  omp_gen
  ClangDriverOptions
  # These generated headers are included transitively.
  ARMTargetParserTableGen
  AArch64TargetParserTableGen
  )

target_link_libraries(clangBasic
  PRIVATE
  ${LLVM_ATOMIC_LIB}
)
