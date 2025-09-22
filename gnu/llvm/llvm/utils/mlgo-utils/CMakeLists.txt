configure_lit_site_cfg(
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/lit.site.cfg.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg"
)

add_lit_testsuite(check-mlgo-utils "Running mlgo-utils tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS "FileCheck" "not" "count" "split-file" "yaml2obj" "llvm-objcopy"
)
