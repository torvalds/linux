set(SWIG_EXTRA_FLAGS -c++ -threads -python)

if ("${SWIG_VERSION}" VERSION_LESS "4.1.0")
  set(SWIG_EXTRA_FLAGS ${SWIG_EXTRA_FLAGS} -py3)
  message(STATUS "SWIG version ${SWIG_VERSION} uses `-py3` flag.")
endif()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/LLDBWrapPython.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lldb.py
  DEPENDS ${SWIG_SOURCES}
  DEPENDS ${SWIG_INTERFACES}
  DEPENDS ${SWIG_HEADERS}
  DEPENDS lldb-sbapi-dwarf-enums
  COMMAND ${SWIG_EXECUTABLE}
      ${SWIG_COMMON_FLAGS}
      -I${CMAKE_CURRENT_SOURCE_DIR}
      ${SWIG_EXTRA_FLAGS}
      -outdir ${CMAKE_CURRENT_BINARY_DIR}
      -o ${CMAKE_CURRENT_BINARY_DIR}/LLDBWrapPython.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/python.swig
  VERBATIM
  COMMENT "Building LLDB Python wrapper")

add_custom_target(swig_wrapper_python ALL DEPENDS
  ${CMAKE_CURRENT_BINARY_DIR}/LLDBWrapPython.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/lldb.py
)

if (NOT WIN32)
add_custom_command(
  OUTPUT  ${LLVM_RUNTIME_OUTPUT_INTDIR}/lldb-python
  VERBATIM
  COMMAND ${CMAKE_COMMAND} -E copy lldb-python ${LLVM_RUNTIME_OUTPUT_INTDIR}/lldb-python
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_custom_target(lldb-python-wrapper ALL DEPENDS
  ${LLVM_RUNTIME_OUTPUT_INTDIR}/lldb-python
)
endif()

function(create_python_package swig_target working_dir pkg_dir)
  cmake_parse_arguments(ARG "NOINIT" "" "FILES" ${ARGN})
  if(ARG_FILES)
    set(copy_cmd COMMAND ${CMAKE_COMMAND} -E copy ${ARG_FILES} ${pkg_dir})
  endif()
  if(NOT ARG_NOINIT)
    set(init_cmd COMMAND ${Python3_EXECUTABLE}
        "${LLDB_SOURCE_DIR}/bindings/python/createPythonInit.py"
        "${pkg_dir}" ${ARG_FILES})
  endif()
  add_custom_command(TARGET ${swig_target} POST_BUILD VERBATIM
    COMMAND ${CMAKE_COMMAND} -E make_directory ${pkg_dir}
    ${copy_cmd}
    ${init_cmd}
    WORKING_DIRECTORY ${working_dir})
endfunction()

function(finish_swig_python swig_target lldb_python_bindings_dir lldb_python_target_dir)
  # Add a Post-Build Event to copy over Python files and create the symlink to
  # liblldb.so for the Python API(hardlink on Windows).
  add_custom_target(${swig_target} ALL VERBATIM
    COMMAND ${CMAKE_COMMAND} -E make_directory ${lldb_python_target_dir}
    DEPENDS ${lldb_python_bindings_dir}/lldb.py
    COMMENT "Python script sym-linking LLDB Python API")

  add_custom_command(TARGET ${swig_target} POST_BUILD VERBATIM
    COMMAND ${CMAKE_COMMAND} -E copy
      "${lldb_python_bindings_dir}/lldb.py"
      "${lldb_python_target_dir}/__init__.py")

  add_custom_command(TARGET ${swig_target} POST_BUILD VERBATIM
    COMMAND ${CMAKE_COMMAND} -E copy
      "${LLDB_SOURCE_DIR}/source/Interpreter/embedded_interpreter.py"
      "${lldb_python_target_dir}")

  # Distribute the examples as python packages.
  create_python_package(
    ${swig_target}
    ${lldb_python_target_dir}
    "formatters/cpp"
    FILES "${LLDB_SOURCE_DIR}/examples/synthetic/gnu_libstdcpp.py"
          "${LLDB_SOURCE_DIR}/examples/synthetic/libcxx.py")

  create_python_package(
    ${swig_target}
    ${lldb_python_target_dir}
    "formatters"
    FILES "${LLDB_SOURCE_DIR}/examples/summaries/cocoa/cache.py"
          "${LLDB_SOURCE_DIR}/examples/summaries/synth.py"
          "${LLDB_SOURCE_DIR}/examples/summaries/cocoa/metrics.py"
          "${LLDB_SOURCE_DIR}/examples/summaries/cocoa/attrib_fromdict.py"
          "${LLDB_SOURCE_DIR}/examples/summaries/cocoa/Logger.py")

  create_python_package(
    ${swig_target}
    ${lldb_python_target_dir}
    "utils"
    FILES "${LLDB_SOURCE_DIR}/examples/python/in_call_stack.py"
          "${LLDB_SOURCE_DIR}/examples/python/symbolication.py"
  )

  create_python_package(
    ${swig_target}
    ${lldb_python_target_dir}
    "plugins"
    FILES
    "${LLDB_SOURCE_DIR}/examples/python/templates/parsed_cmd.py"
    "${LLDB_SOURCE_DIR}/examples/python/templates/scripted_process.py"
    "${LLDB_SOURCE_DIR}/examples/python/templates/scripted_platform.py"
    "${LLDB_SOURCE_DIR}/examples/python/templates/operating_system.py"
    "${LLDB_SOURCE_DIR}/examples/python/templates/scripted_thread_plan.py"
    )

  if(APPLE)
    create_python_package(
      ${swig_target}
      ${lldb_python_target_dir} "macosx"
      FILES "${LLDB_SOURCE_DIR}/examples/python/crashlog.py"
            "${LLDB_SOURCE_DIR}/examples/python/crashlog_scripted_process.py"
            "${LLDB_SOURCE_DIR}/examples/darwin/heap_find/heap.py")

    create_python_package(
      ${swig_target}
      ${lldb_python_target_dir} "macosx/heap"
      FILES "${LLDB_SOURCE_DIR}/examples/darwin/heap_find/heap/heap_find.cpp"
            "${LLDB_SOURCE_DIR}/examples/darwin/heap_find/heap/Makefile"
            NOINIT)

    create_python_package(
      ${swig_target}
      ${lldb_python_target_dir} "diagnose"
      FILES "${LLDB_SOURCE_DIR}/examples/python/diagnose_unwind.py"
            "${LLDB_SOURCE_DIR}/examples/python/diagnose_nsstring.py")
  endif()

  if(LLDB_BUILD_FRAMEWORK)
    set(LIBLLDB_SYMLINK_DEST "${LLDB_FRAMEWORK_ABSOLUTE_BUILD_DIR}/LLDB.framework/LLDB")
  else()
    set(LIBLLDB_SYMLINK_DEST "${LLVM_SHLIB_OUTPUT_INTDIR}/liblldb${CMAKE_SHARED_LIBRARY_SUFFIX}")
  endif()
  set(LIBLLDB_SYMLINK_OUTPUT_FILE "_lldb${LLDB_PYTHON_EXT_SUFFIX}")
  create_relative_symlink(${swig_target} ${LIBLLDB_SYMLINK_DEST}
                          ${lldb_python_target_dir} ${LIBLLDB_SYMLINK_OUTPUT_FILE})


  if (NOT WIN32)
  add_dependencies(${swig_target} lldb-python-wrapper)
  endif()

  if(NOT LLDB_BUILD_FRAMEWORK)
    set(LLDB_ARGDUMPER_FILENAME "lldb-argdumper${CMAKE_EXECUTABLE_SUFFIX}")
    create_relative_symlink(${swig_target} "${LLVM_RUNTIME_OUTPUT_INTDIR}/${LLDB_ARGDUMPER_FILENAME}"
                            ${lldb_python_target_dir} ${LLDB_ARGDUMPER_FILENAME})
  endif()

  add_dependencies(${swig_target} swig_wrapper_python liblldb lldb-argdumper)
  set_target_properties(${swig_target} swig_wrapper_python PROPERTIES FOLDER "lldb misc")

  # Ensure we do the python post-build step when building lldb.
  add_dependencies(lldb ${swig_target})

  # Install the LLDB python module
  if(LLDB_BUILD_FRAMEWORK)
    set(LLDB_PYTHON_INSTALL_PATH ${LLDB_FRAMEWORK_INSTALL_DIR}/LLDB.framework/Versions/${LLDB_FRAMEWORK_VERSION}/Resources/Python)
  else()
    set(LLDB_PYTHON_INSTALL_PATH ${LLDB_PYTHON_RELATIVE_PATH})
  endif()
  if (NOT CMAKE_CFG_INTDIR STREQUAL  ".")
    string(REPLACE ${CMAKE_CFG_INTDIR} "\$\{CMAKE_INSTALL_CONFIG_NAME\}" LLDB_PYTHON_INSTALL_PATH ${LLDB_PYTHON_INSTALL_PATH})
    string(REPLACE ${CMAKE_CFG_INTDIR} "\$\{CMAKE_INSTALL_CONFIG_NAME\}" lldb_python_target_dir ${lldb_python_target_dir})
  endif()
  set(python_scripts_target "${swig_target}-scripts")
  set(python_scripts_install_target "install-${python_scripts_target}")
  add_custom_target(${python_scripts_target})
  add_dependencies(${python_scripts_target} ${swig_target})
  install(DIRECTORY ${lldb_python_target_dir}/../
          DESTINATION ${LLDB_PYTHON_INSTALL_PATH}
          COMPONENT ${python_scripts_target})
  if (NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(${python_scripts_install_target}
                             COMPONENT ${python_scripts_target}
                             DEPENDS ${python_scripts_target})
  endif()

  # Add a Post-Build Event to copy the custom Python DLL to the lldb binaries dir so that Windows can find it when launching
  # lldb.exe or any other executables that were linked with liblldb.
  if (WIN32 AND NOT "${PYTHON_DLL}" STREQUAL "")
    # When using the Visual Studio CMake generator the lldb binaries end up in Release/bin, Debug/bin etc.
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin" LLDB_BIN_DIR)
    file(TO_NATIVE_PATH "${PYTHON_DLL}" PYTHON_DLL_NATIVE_PATH)
    add_custom_command(
      TARGET ${swig_target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${PYTHON_DLL_NATIVE_PATH} ${LLDB_BIN_DIR} VERBATIM
      COMMENT "Copying Python DLL to LLDB binaries directory.")
  endif()
endfunction()
