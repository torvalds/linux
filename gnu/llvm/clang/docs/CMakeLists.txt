
if (DOXYGEN_FOUND)
if (LLVM_ENABLE_DOXYGEN)
  set(abs_srcdir ${CMAKE_CURRENT_SOURCE_DIR})
  set(abs_builddir ${CMAKE_CURRENT_BINARY_DIR})

  if (HAVE_DOT)
    set(DOT ${LLVM_PATH_DOT})
  endif()

  if (LLVM_DOXYGEN_EXTERNAL_SEARCH)
    set(enable_searchengine "YES")
    set(searchengine_url "${LLVM_DOXYGEN_SEARCHENGINE_URL}")
    set(enable_server_based_search "YES")
    set(enable_external_search "YES")
    set(extra_search_mappings "${LLVM_DOXYGEN_SEARCH_MAPPINGS}")
  else()
    set(enable_searchengine "NO")
    set(searchengine_url "")
    set(enable_server_based_search "NO")
    set(enable_external_search "NO")
    set(extra_search_mappings "")
  endif()

  # If asked, configure doxygen for the creation of a Qt Compressed Help file.
  if (LLVM_ENABLE_DOXYGEN_QT_HELP)
    set(CLANG_DOXYGEN_QCH_FILENAME "org.llvm.clang.qch" CACHE STRING
      "Filename of the Qt Compressed help file")
    set(CLANG_DOXYGEN_QHP_NAMESPACE "org.llvm.clang" CACHE STRING
      "Namespace under which the intermediate Qt Help Project file lives")
    set(CLANG_DOXYGEN_QHP_CUST_FILTER_NAME "Clang ${CLANG_VERSION}" CACHE STRING
      "See http://qt-project.org/doc/qt-4.8/qthelpproject.html#custom-filters")
    set(CLANG_DOXYGEN_QHP_CUST_FILTER_ATTRS "Clang,${CLANG_VERSION}" CACHE STRING
      "See http://qt-project.org/doc/qt-4.8/qthelpproject.html#filter-attributes")
    set(clang_doxygen_generate_qhp "YES")
    set(clang_doxygen_qch_filename "${CLANG_DOXYGEN_QCH_FILENAME}")
    set(clang_doxygen_qhp_namespace "${CLANG_DOXYGEN_QHP_NAMESPACE}")
    set(clang_doxygen_qhelpgenerator_path "${LLVM_DOXYGEN_QHELPGENERATOR_PATH}")
    set(clang_doxygen_qhp_cust_filter_name "${CLANG_DOXYGEN_QHP_CUST_FILTER_NAME}")
    set(clang_doxygen_qhp_cust_filter_attrs "${CLANG_DOXYGEN_QHP_CUST_FILTER_ATTRS}")
  else()
    set(clang_doxygen_generate_qhp "NO")
    set(clang_doxygen_qch_filename "")
    set(clang_doxygen_qhp_namespace "")
    set(clang_doxygen_qhelpgenerator_path "")
    set(clang_doxygen_qhp_cust_filter_name "")
    set(clang_doxygen_qhp_cust_filter_attrs "")
  endif()

  option(LLVM_DOXYGEN_SVG
    "Use svg instead of png files for doxygen graphs." OFF)
  if (LLVM_DOXYGEN_SVG)
    set(DOT_IMAGE_FORMAT "svg")
  else()
    set(DOT_IMAGE_FORMAT "png")
  endif()

  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doxygen.cfg.in
    ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg @ONLY)

  set(abs_top_srcdir)
  set(abs_top_builddir)
  set(DOT)
  set(enable_searchengine)
  set(searchengine_url)
  set(enable_server_based_search)
  set(enable_external_search)
  set(extra_search_mappings)
  set(clang_doxygen_generate_qhp)
  set(clang_doxygen_qch_filename)
  set(clang_doxygen_qhp_namespace)
  set(clang_doxygen_qhelpgenerator_path)
  set(clang_doxygen_qhp_cust_filter_name)
  set(clang_doxygen_qhp_cust_filter_attrs)
  set(DOT_IMAGE_FORMAT)

  add_custom_target(doxygen-clang
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating clang doxygen documentation." VERBATIM)
  set_target_properties(doxygen-clang PROPERTIES FOLDER "Clang/Docs")

  if (LLVM_BUILD_DOCS)
    add_dependencies(doxygen doxygen-clang)
  endif()

  if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doxygen/html
      DESTINATION docs/html)
  endif()
endif()
endif()

function (gen_rst_file_from_td output_file td_option source docs_targets)
  if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    message(FATAL_ERROR "Cannot find source file: ${source} in ${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  get_filename_component(TABLEGEN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${source}" DIRECTORY)
  list(APPEND LLVM_TABLEGEN_FLAGS "-I${TABLEGEN_INCLUDE_DIR}")
  clang_tablegen(${output_file} ${td_option} SOURCE ${source} TARGET "gen-${output_file}")
  foreach(target ${docs_targets})
    add_dependencies(${target} gen-${output_file})
  endforeach()
endfunction()

if (LLVM_ENABLE_SPHINX)
  include(AddSphinxTarget)
  if (SPHINX_FOUND AND (${SPHINX_OUTPUT_HTML} OR ${SPHINX_OUTPUT_MAN}))
    # Copy rst files to build directory before generating the html
    # documentation.  Some of the rst files are generated, so they
    # only exist in the build directory.  Sphinx needs all files in
    # the same directory in order to generate the html, so we need to
    # copy all the non-gnerated rst files from the source to the build
    # directory before we run sphinx.
    add_custom_target(copy-clang-rst-docs
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"

      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/../CodeOwners.rst"
      "${CMAKE_CURRENT_BINARY_DIR}"
    )

    set(docs_targets "")

    if (${SPHINX_OUTPUT_HTML})
      add_sphinx_target(html clang SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}")

      add_custom_command(TARGET docs-clang-html POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/LibASTMatchersReference.html"
        "${CMAKE_CURRENT_BINARY_DIR}/html/LibASTMatchersReference.html")

      list(APPEND docs_targets "docs-clang-html")
    endif()
    if (${SPHINX_OUTPUT_MAN})
      add_sphinx_target(man clang SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
      list(APPEND docs_targets "docs-clang-man")
    endif()

    # Generated files
    gen_rst_file_from_td(AttributeReference.rst -gen-attr-docs ../include/clang/Basic/Attr.td "${docs_targets}")
    gen_rst_file_from_td(DiagnosticsReference.rst -gen-diag-docs ../include/clang/Basic/Diagnostic.td "${docs_targets}")
    gen_rst_file_from_td(ClangCommandLineReference.rst -gen-opt-docs ../include/clang/Driver/ClangOptionDocs.td "${docs_targets}")

    foreach(target ${docs_targets})
      add_dependencies(${target} copy-clang-rst-docs)
    endforeach()
  endif()
endif()
