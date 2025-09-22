get_property(LLVM_LIT_CONFIG_FILES GLOBAL PROPERTY LLVM_LIT_CONFIG_FILES)
list(LENGTH LLVM_LIT_CONFIG_FILES file_len)
math(EXPR file_last "${file_len} - 1")

get_llvm_lit_path(LIT_BASE_DIR LIT_FILE_NAME)

set(LLVM_SOURCE_DIR ${LLVM_MAIN_SRC_DIR})

# LLVM_LIT_CONFIG_FILES contains interleaved main config (in the source tree)
# and site config (in the build tree) pairs. Make them relative to
# llvm-lit and then convert them to map_config() calls.
if("${CMAKE_CFG_INTDIR}" STREQUAL ".")
  make_paths_relative(
    LLVM_LIT_CONFIG_FILES "${LIT_BASE_DIR}" "${LLVM_LIT_CONFIG_FILES}")
  make_paths_relative(
    LLVM_SOURCE_DIR "${LIT_BASE_DIR}" "${LLVM_SOURCE_DIR}")
endif()

set(LLVM_LIT_CONFIG_MAP "${LLVM_LIT_PATH_FUNCTION}\n")
if (${file_last} GREATER -1)
  foreach(i RANGE 0 ${file_last} 2)
    list(GET LLVM_LIT_CONFIG_FILES ${i} main_config)
    math(EXPR i1 "${i} + 1")
    list(GET LLVM_LIT_CONFIG_FILES ${i1} site_out)
    set(map "map_config(path(r'${main_config}'), path(r'${site_out}'))")
    set(LLVM_LIT_CONFIG_MAP "${LLVM_LIT_CONFIG_MAP}\n${map}")
  endforeach()
endif()

if(NOT "${CMAKE_CFG_INTDIR}" STREQUAL ".")
  foreach(BUILD_MODE ${CMAKE_CONFIGURATION_TYPES})
    string(REPLACE ${CMAKE_CFG_INTDIR} ${BUILD_MODE} bi ${LIT_BASE_DIR})
    set(bi "${bi}/${LIT_FILE_NAME}")
    configure_file(
      llvm-lit.in
      ${bi}
      )
  endforeach()
else()
  set(BUILD_MODE .)
  configure_file(
    llvm-lit.in
    ${LIT_BASE_DIR}/${LIT_FILE_NAME}
    )
endif()
