option(LLVM_INCLUDE_DXIL_TESTS "Include DXIL tests" Off)
mark_as_advanced(LLVM_INCLUDE_DXIL_TESTS)

if (NOT LLVM_INCLUDE_DXIL_TESTS)
  return()
endif ()

if (NOT "DirectX" IN_LIST LLVM_TARGETS_TO_BUILD)
  message(FATAL_ERROR "Building dxil-dis tests is unsupported without the DirectX target")
endif ()

if (CMAKE_HOST_UNIX)
  set(LLVM_LINK_OR_COPY create_symlink)
else ()
  set(LLVM_LINK_OR_COPY copy)
endif ()

if (DXIL_DIS)
  add_custom_target(dxil-dis
    COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${DXIL_DIS}" "${LLVM_RUNTIME_OUTPUT_INTDIR}/dxil-dis${CMAKE_EXECUTABLE_SUFFIX}")
  return()
endif ()

include(ExternalProject)

set(SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/DXC-src)
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/DXC-bins)
set(GIT_SETTINGS GIT_REPOSITORY https://github.com/microsoft/DirectXShaderCompiler.git)

if (DXC_SOURCE_DIR)
  set(SOURCE_DIR ${DXC_SOURCE_DIR})
  unset(GIT_SETTINGS)
endif ()

ExternalProject_Add(DXC
                    ${GIT_SETTINGS}
                    SOURCE_DIR ${SOURCE_DIR}
                    BINARY_DIR ${BINARY_DIR}
                    CMAKE_ARGS -C ${SOURCE_DIR}/cmake/caches/PredefinedParams.cmake -DLLVM_INCLUDE_TESTS=On
                    BUILD_COMMAND ${CMAKE_COMMAND} --build ${BINARY_DIR} --target llvm-dis
                    BUILD_BYPRODUCTS ${BINARY_DIR}/bin/llvm-dis
                    INSTALL_COMMAND ""
                    )

add_custom_target(dxil-dis
                  COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${BINARY_DIR}/bin/llvm-dis${CMAKE_EXECUTABLE_SUFFIX}" "${LLVM_RUNTIME_OUTPUT_INTDIR}/dxil-dis${CMAKE_EXECUTABLE_SUFFIX}"
                  DEPENDS DXC
                  )
