# Needed for lit support in standalone builds.
include(AddLLVM)

configure_compiler_rt_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.common.unit.configured.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.common.unit.configured)
