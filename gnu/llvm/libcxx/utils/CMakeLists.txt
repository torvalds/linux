add_custom_target(libcxx-generate-feature-test-macros
    COMMAND "${Python3_EXECUTABLE}" "${LIBCXX_SOURCE_DIR}/utils/generate_feature_test_macro_components.py"
    COMMENT "Generate the <version> header and tests for feature test macros.")

add_custom_target(libcxx-generate-std-clang-module-header
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_std_clang_module_header.py"
  COMMENT "Generate the <__std_clang_module> header")

add_custom_target(libcxx-generate-std-cppm-in-file
  COMMAND
        "${Python3_EXECUTABLE}"
		"${LIBCXX_SOURCE_DIR}/utils/generate_libcxx_cppm_in.py"
		"std"
  COMMENT "Generate the std.cppm.in file")

add_custom_target(libcxx-generate-std-compat-cppm-in-file
  COMMAND
        "${Python3_EXECUTABLE}"
		"${LIBCXX_SOURCE_DIR}/utils/generate_libcxx_cppm_in.py"
		"std.compat"
  COMMENT "Generate the std.compat.cppm.in file")

add_custom_target(libcxx-generate-extended-grapheme-cluster-tables
    COMMAND
        "${Python3_EXECUTABLE}"
        "${LIBCXX_SOURCE_DIR}/utils/generate_extended_grapheme_cluster_table.py"
        "${LIBCXX_SOURCE_DIR}/include/__format/extended_grapheme_cluster_table.h"
    COMMENT "Generate the extended grapheme cluster header.")

add_custom_target(libcxx-generate-extended-grapheme-cluster-tests
    COMMAND
        "${Python3_EXECUTABLE}"
        "${LIBCXX_SOURCE_DIR}/utils/generate_extended_grapheme_cluster_test.py"
         "${LIBCXX_SOURCE_DIR}/test/libcxx/utilities/format/format.string/format.string.std/extended_grapheme_cluster.h"
    COMMENT "Generate the extended grapheme cluster header.")

add_custom_target(libcxx-generate-escaped-output-table
    COMMAND
        "${Python3_EXECUTABLE}"
        "${LIBCXX_SOURCE_DIR}/utils/generate_escaped_output_table.py"
        "${LIBCXX_SOURCE_DIR}/include/__format/escaped_output_table.h"
    COMMENT "Generate the escaped output header")

add_custom_target(libcxx-generate-width-estimation-table
    COMMAND
        "${Python3_EXECUTABLE}"
        "${LIBCXX_SOURCE_DIR}/utils/generate_width_estimation_table.py"
        "${LIBCXX_SOURCE_DIR}/include/__format/width_estimation_table.h"
    COMMENT "Generate the width estimation header")

add_custom_target(libcxx-indic-conjunct-break-table
    COMMAND
        "${Python3_EXECUTABLE}"
        "${LIBCXX_SOURCE_DIR}/utils/generate_indic_conjunct_break_table.py"
        "${LIBCXX_SOURCE_DIR}/include/__format/indic_conjunct_break_table.h"
    COMMENT "Generate the Indic Conjunct Break header")

add_custom_target(libcxx-generate-files
    DEPENDS libcxx-generate-feature-test-macros
            libcxx-generate-std-clang-module-header
            libcxx-generate-std-cppm-in-file
            libcxx-generate-std-compat-cppm-in-file
            libcxx-generate-extended-grapheme-cluster-tables
            libcxx-generate-extended-grapheme-cluster-tests
            libcxx-generate-escaped-output-table
            libcxx-generate-width-estimation-table
            libcxx-indic-conjunct-break-table
    COMMENT "Create all the auto-generated files in libc++ and its tests.")
