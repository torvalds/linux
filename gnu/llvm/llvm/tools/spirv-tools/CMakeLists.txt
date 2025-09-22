option(LLVM_INCLUDE_SPIRV_TOOLS_TESTS "Include tests that use SPIRV-Tools" Off)
mark_as_advanced(LLVM_INCLUDE_SPIRV_TOOLS_TESTS)

if (NOT LLVM_INCLUDE_SPIRV_TOOLS_TESTS)
  return()
endif ()

if (NOT "SPIRV" IN_LIST LLVM_TARGETS_TO_BUILD)
  message(FATAL_ERROR "Building SPIRV-Tools tests is unsupported without the SPIR-V target")
endif ()

# SPIRV_DIS and SPIRV_VAL variables can be used to provide paths to existing
# spirv-dis and spirv-val binaries, respectively. Otherwise, build them from
# SPIRV-Tools source.
if (NOT SPIRV_DIS OR NOT SPIRV_VAL)
  include(ExternalProject)

  set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/SPIRVTools-bin)

  ExternalProject_Add(SPIRVTools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG main
    BINARY_DIR ${BINARY_DIR}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${BINARY_DIR} --target spirv-dis spirv-val
    BUILD_BYPRODUCTS ${BINARY_DIR}/tools/spirv-dis ${BINARY_DIR}/tools/spirv-val
    DOWNLOAD_COMMAND git clone https://github.com/KhronosGroup/SPIRV-Tools.git SPIRVTools &&
      cd SPIRVTools &&
      ${Python3_EXECUTABLE} utils/git-sync-deps
    UPDATE_COMMAND git pull origin main &&
      ${Python3_EXECUTABLE} utils/git-sync-deps
    # Don't auto-update on every build.
    UPDATE_DISCONNECTED 1
    # Allow manual updating with an explicit SPIRVTools-update target.
    STEP_TARGETS update
    # Install handled below.
    INSTALL_COMMAND ""
    )
endif ()

if (CMAKE_HOST_UNIX)
  set(LLVM_LINK_OR_COPY create_symlink)
else ()
  set(LLVM_LINK_OR_COPY copy)
endif ()

# Link the provided or just built spirv-dis and spirv-val binaries.
if (SPIRV_DIS)
  add_custom_target(spirv-dis
    COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${SPIRV_DIS}" "${LLVM_RUNTIME_OUTPUT_INTDIR}/spirv-dis")
else ()
  add_custom_target(spirv-dis
    COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${BINARY_DIR}/tools/spirv-dis${CMAKE_EXECUTABLE_SUFFIX}" "${LLVM_RUNTIME_OUTPUT_INTDIR}/spirv-dis${CMAKE_EXECUTABLE_SUFFIX}"
    DEPENDS SPIRVTools
    )
endif ()

if (SPIRV_VAL)
  add_custom_target(spirv-val
    COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${SPIRV_VAL}" "${LLVM_RUNTIME_OUTPUT_INTDIR}/spirv-val")
else ()
  add_custom_target(spirv-val
    COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${BINARY_DIR}/tools/spirv-val${CMAKE_EXECUTABLE_SUFFIX}" "${LLVM_RUNTIME_OUTPUT_INTDIR}/spirv-val${CMAKE_EXECUTABLE_SUFFIX}"
    DEPENDS SPIRVTools
    )
endif ()
