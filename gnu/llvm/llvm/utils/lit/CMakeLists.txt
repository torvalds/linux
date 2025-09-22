# The configured file is not placed in the correct location
# until the tests are run as we need to copy it into
# a copy of the tests folder
configure_lit_site_cfg(
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/lit.site.cfg.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg"
  )

# Lit's test suite creates output files next to the sources which makes the
# source tree dirty. This is undesirable because we do out of source builds.
# To work around this the tests and the configuration file are copied into the
# build directory just before running them. The tests are not copied over at
# configure time (i.e. `file(COPY ...)`) because this could lead to stale
# tests being run.
add_custom_target(prepare-check-lit
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/tests"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${CMAKE_CURRENT_BINARY_DIR}/tests"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg" "${CMAKE_CURRENT_BINARY_DIR}/tests"
  COMMENT "Preparing lit tests"
)

# Add rules for lit's own test suite
add_lit_testsuite(check-lit "Running lit's tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS "FileCheck" "not" "prepare-check-lit"
)

# For IDEs
set_target_properties(check-lit PROPERTIES FOLDER "LLVM/Tests")
set_target_properties(prepare-check-lit PROPERTIES FOLDER "LLVM/Tests")
