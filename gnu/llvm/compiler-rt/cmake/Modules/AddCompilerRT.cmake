include(ExternalProject)
include(CompilerRTUtils)
include(HandleCompilerRT)

function(set_target_output_directories target output_dir)
  # For RUNTIME_OUTPUT_DIRECTORY variable, Multi-configuration generators
  # append a per-configuration subdirectory to the specified directory.
  # To avoid the appended folder, the configuration specific variable must be
  # set 'RUNTIME_OUTPUT_DIRECTORY_${CONF}':
  # RUNTIME_OUTPUT_DIRECTORY_DEBUG, RUNTIME_OUTPUT_DIRECTORY_RELEASE, ...
  if(CMAKE_CONFIGURATION_TYPES)
    foreach(build_mode ${CMAKE_CONFIGURATION_TYPES})
      string(TOUPPER "${build_mode}" CONFIG_SUFFIX)
      set_target_properties("${target}" PROPERTIES
          "ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${output_dir}
          "LIBRARY_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${output_dir}
          "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${output_dir})
    endforeach()
  else()
    set_target_properties("${target}" PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${output_dir}
        LIBRARY_OUTPUT_DIRECTORY ${output_dir}
        RUNTIME_OUTPUT_DIRECTORY ${output_dir})
  endif()
endfunction()

# Tries to add an "object library" target for a given list of OSs and/or
# architectures with name "<name>.<arch>" for non-Darwin platforms if
# architecture can be targeted, and "<name>.<os>" for Darwin platforms.
# add_compiler_rt_object_libraries(<name>
#                                  OS <os names>
#                                  ARCHS <architectures>
#                                  SOURCES <source files>
#                                  CFLAGS <compile flags>
#                                  DEFS <compile definitions>
#                                  DEPS <dependencies>
#                                  ADDITIONAL_HEADERS <header files>)
function(add_compiler_rt_object_libraries name)
  cmake_parse_arguments(LIB "" "" "OS;ARCHS;SOURCES;CFLAGS;DEFS;DEPS;ADDITIONAL_HEADERS"
    ${ARGN})
  set(libnames)
  if(APPLE)
    foreach(os ${LIB_OS})
      set(libname "${name}.${os}")
      set(libnames ${libnames} ${libname})
      set(extra_cflags_${libname} ${DARWIN_${os}_CFLAGS})
      list_intersect(LIB_ARCHS_${libname} DARWIN_${os}_ARCHS LIB_ARCHS)
    endforeach()
  else()
    foreach(arch ${LIB_ARCHS})
      set(libname "${name}.${arch}")
      set(libnames ${libnames} ${libname})
      set(extra_cflags_${libname} ${TARGET_${arch}_CFLAGS})
      if(NOT CAN_TARGET_${arch})
        message(FATAL_ERROR "Architecture ${arch} can't be targeted")
        return()
      endif()
    endforeach()
  endif()

  # Add headers to LIB_SOURCES for IDEs
  compiler_rt_process_sources(LIB_SOURCES
    ${LIB_SOURCES}
    ADDITIONAL_HEADERS
      ${LIB_ADDITIONAL_HEADERS}
  )

  foreach(libname ${libnames})
    add_library(${libname} OBJECT ${LIB_SOURCES})
    if(LIB_DEPS)
      add_dependencies(${libname} ${LIB_DEPS})
    endif()

    # Strip out -msse3 if this isn't macOS.
    set(target_flags ${LIB_CFLAGS})
    if(APPLE AND NOT "${libname}" MATCHES ".*\.osx.*")
      list(REMOVE_ITEM target_flags "-msse3")
    endif()

    # Build the macOS sanitizers with Mac Catalyst support.
    if (APPLE AND
        "${COMPILER_RT_ENABLE_MACCATALYST}" AND
        "${libname}" MATCHES ".*\.osx.*")
      foreach(arch ${LIB_ARCHS_${libname}})
        list(APPEND target_flags
          "SHELL:-target ${arch}-apple-macos${DARWIN_osx_MIN_VER} -darwin-target-variant ${arch}-apple-ios13.1-macabi")
      endforeach()
    endif()

    set_target_compile_flags(${libname}
      ${extra_cflags_${libname}} ${target_flags})
    set_property(TARGET ${libname} APPEND PROPERTY
      COMPILE_DEFINITIONS ${LIB_DEFS})
    set_target_properties(${libname} PROPERTIES FOLDER "Compiler-RT/Libraries")
    if(APPLE)
      set_target_properties(${libname} PROPERTIES
        OSX_ARCHITECTURES "${LIB_ARCHS_${libname}}")
    endif()
  endforeach()
endfunction()

# Takes a list of object library targets, and a suffix and appends the proper
# TARGET_OBJECTS string to the output variable.
# format_object_libs(<output> <suffix> ...)
macro(format_object_libs output suffix)
  foreach(lib ${ARGN})
    list(APPEND ${output} $<TARGET_OBJECTS:${lib}.${suffix}>)
  endforeach()
endmacro()

function(add_compiler_rt_component name)
  add_custom_target(${name})
  set_target_properties(${name} PROPERTIES FOLDER "Compiler-RT/Components")
  if(COMMAND runtime_register_component)
    runtime_register_component(${name})
  endif()
  add_dependencies(compiler-rt ${name})
endfunction()

macro(set_output_name output name arch)
  if(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR)
    set(${output} ${name})
  else()
    if(ANDROID AND ${arch} STREQUAL "i386")
      set(${output} "${name}-i686${COMPILER_RT_OS_SUFFIX}")
    elseif("${arch}" MATCHES "^arm")
      if(COMPILER_RT_DEFAULT_TARGET_ONLY)
        set(triple "${COMPILER_RT_DEFAULT_TARGET_TRIPLE}")
      else()
        set(triple "${LLVM_TARGET_TRIPLE}")
      endif()
      # Except for baremetal, when using arch-suffixed runtime library names,
      # clang only looks for libraries named "arm" or "armhf", see
      # getArchNameForCompilerRTLib in clang. Therefore, try to inspect both
      # the arch name and the triple if it seems like we're building an armhf
      # target.
      if (COMPILER_RT_BAREMETAL_BUILD)
        set(${output} "${name}-${arch}${COMPILER_RT_OS_SUFFIX}")
      elseif ("${arch}" MATCHES "hf$" OR "${triple}" MATCHES "hf$")
        set(${output} "${name}-armhf${COMPILER_RT_OS_SUFFIX}")
      else()
        set(${output} "${name}-arm${COMPILER_RT_OS_SUFFIX}")
      endif()
    else()
      set(${output} "${name}-${arch}${COMPILER_RT_OS_SUFFIX}")
    endif()
  endif()
endmacro()

# Adds static or shared runtime for a list of architectures and operating
# systems and puts it in the proper directory in the build and install trees.
# add_compiler_rt_runtime(<name>
#                         {OBJECT|STATIC|SHARED|MODULE}
#                         ARCHS <architectures>
#                         OS <os list>
#                         SOURCES <source files>
#                         CFLAGS <compile flags>
#                         LINK_FLAGS <linker flags>
#                         DEFS <compile definitions>
#                         DEPS <dependencies>
#                         LINK_LIBS <linked libraries> (only for shared library)
#                         OBJECT_LIBS <object libraries to use as sources>
#                         PARENT_TARGET <convenience parent target>
#                         ADDITIONAL_HEADERS <header files>)
function(add_compiler_rt_runtime name type)
  if(NOT type MATCHES "^(OBJECT|STATIC|SHARED|MODULE)$")
    message(FATAL_ERROR
            "type argument must be OBJECT, STATIC, SHARED or MODULE")
    return()
  endif()
  cmake_parse_arguments(LIB
    ""
    "PARENT_TARGET"
    "OS;ARCHS;SOURCES;CFLAGS;LINK_FLAGS;DEFS;DEPS;LINK_LIBS;OBJECT_LIBS;ADDITIONAL_HEADERS"
    ${ARGN})
  set(libnames)
  # Until we support this some other way, build compiler-rt runtime without LTO
  # to allow non-LTO projects to link with it. GPU targets can currently only be
  # distributed as LLVM-IR and ignore this.
  if(COMPILER_RT_HAS_FNO_LTO_FLAG AND NOT COMPILER_RT_GPU_BUILD)
    set(NO_LTO_FLAGS "-fno-lto")
  else()
    set(NO_LTO_FLAGS "")
  endif()

  # By default do not instrument or use profdata for compiler-rt.
  set(NO_PGO_FLAGS "")
  if(NOT COMPILER_RT_ENABLE_PGO)
    if(LLVM_PROFDATA_FILE AND COMPILER_RT_HAS_FNO_PROFILE_INSTR_USE_FLAG)
      list(APPEND NO_PGO_FLAGS "-fno-profile-instr-use")
    endif()
    if(LLVM_BUILD_INSTRUMENTED MATCHES IR AND COMPILER_RT_HAS_FNO_PROFILE_GENERATE_FLAG)
      list(APPEND NO_PGO_FLAGS "-fno-profile-generate")
    elseif((LLVM_BUILD_INSTRUMENTED OR LLVM_BUILD_INSTRUMENTED_COVERAGE) AND COMPILER_RT_HAS_FNO_PROFILE_INSTR_GENERATE_FLAG)
      list(APPEND NO_PGO_FLAGS "-fno-profile-instr-generate")
      if(LLVM_BUILD_INSTRUMENTED_COVERAGE AND COMPILER_RT_HAS_FNO_COVERAGE_MAPPING_FLAG)
        list(APPEND NO_PGO_FLAGS "-fno-coverage-mapping")
      endif()
    endif()
  endif()

  list(LENGTH LIB_SOURCES LIB_SOURCES_LENGTH)
  if (${LIB_SOURCES_LENGTH} GREATER 0)
    # Add headers to LIB_SOURCES for IDEs. It doesn't make sense to
    # do this for a runtime library that only consists of OBJECT
    # libraries, so only add the headers when source files are present.
    compiler_rt_process_sources(LIB_SOURCES
      ${LIB_SOURCES}
      ADDITIONAL_HEADERS
        ${LIB_ADDITIONAL_HEADERS}
    )
  endif()

  if(APPLE)
    foreach(os ${LIB_OS})
      # Strip out -msse3 if this isn't macOS.
      list(LENGTH LIB_CFLAGS HAS_EXTRA_CFLAGS)
      if(HAS_EXTRA_CFLAGS AND NOT "${os}" MATCHES "^(osx)$")
        list(REMOVE_ITEM LIB_CFLAGS "-msse3")
      endif()
      if(type STREQUAL "STATIC")
        set(libname "${name}_${os}")
      else()
        set(libname "${name}_${os}_dynamic")
        set(extra_link_flags_${libname} ${DARWIN_${os}_LINK_FLAGS} ${LIB_LINK_FLAGS})
      endif()
      list_intersect(LIB_ARCHS_${libname} DARWIN_${os}_ARCHS LIB_ARCHS)
      if(LIB_ARCHS_${libname})
        list(APPEND libnames ${libname})
        set(extra_cflags_${libname} ${DARWIN_${os}_CFLAGS} ${NO_LTO_FLAGS} ${NO_PGO_FLAGS} ${LIB_CFLAGS})
        set(output_name_${libname} ${libname}${COMPILER_RT_OS_SUFFIX})
        set(sources_${libname} ${LIB_SOURCES})
        format_object_libs(sources_${libname} ${os} ${LIB_OBJECT_LIBS})
        get_compiler_rt_output_dir(${COMPILER_RT_DEFAULT_TARGET_ARCH} output_dir_${libname})
        get_compiler_rt_install_dir(${COMPILER_RT_DEFAULT_TARGET_ARCH} install_dir_${libname})
      endif()

      # Build the macOS sanitizers with Mac Catalyst support.
      if ("${COMPILER_RT_ENABLE_MACCATALYST}" AND
          "${os}" MATCHES "^(osx)$")
        foreach(arch ${LIB_ARCHS_${libname}})
          list(APPEND extra_cflags_${libname}
            "SHELL:-target ${arch}-apple-macos${DARWIN_osx_MIN_VER} -darwin-target-variant ${arch}-apple-ios13.1-macabi")
          list(APPEND extra_link_flags_${libname}
            "SHELL:-target ${arch}-apple-macos${DARWIN_osx_MIN_VER} -darwin-target-variant ${arch}-apple-ios13.1-macabi")
        endforeach()
      endif()
    endforeach()
  else()
    foreach(arch ${LIB_ARCHS})
      if(NOT CAN_TARGET_${arch})
        message(FATAL_ERROR "Architecture ${arch} can't be targeted")
        return()
      endif()
      if(type STREQUAL "OBJECT")
        set(libname "${name}-${arch}")
        set_output_name(output_name_${libname} ${name}${COMPILER_RT_OS_SUFFIX} ${arch})
      elseif(type STREQUAL "STATIC")
        set(libname "${name}-${arch}")
        set_output_name(output_name_${libname} ${name} ${arch})
      else()
        set(libname "${name}-dynamic-${arch}")
        set(extra_cflags_${libname} ${TARGET_${arch}_CFLAGS} ${LIB_CFLAGS})
        set(extra_link_flags_${libname} ${TARGET_${arch}_LINK_FLAGS} ${LIB_LINK_FLAGS})
        if(WIN32)
          set_output_name(output_name_${libname} ${name}_dynamic ${arch})
        else()
          set_output_name(output_name_${libname} ${name} ${arch})
        endif()
      endif()
      if(COMPILER_RT_USE_BUILTINS_LIBRARY AND NOT type STREQUAL "OBJECT" AND
         NOT name STREQUAL "clang_rt.builtins")
        get_compiler_rt_target(${arch} target)
        find_compiler_rt_library(builtins builtins_${libname} TARGET ${target})
        if(builtins_${libname} STREQUAL "NOTFOUND")
          message(FATAL_ERROR "Cannot find builtins library for the target architecture")
        endif()
      endif()
      set(sources_${libname} ${LIB_SOURCES})
      format_object_libs(sources_${libname} ${arch} ${LIB_OBJECT_LIBS})
      set(libnames ${libnames} ${libname})
      set(extra_cflags_${libname} ${TARGET_${arch}_CFLAGS} ${NO_LTO_FLAGS} ${NO_PGO_FLAGS} ${LIB_CFLAGS})
      get_compiler_rt_output_dir(${arch} output_dir_${libname})
      get_compiler_rt_install_dir(${arch} install_dir_${libname})
    endforeach()
  endif()

  if(NOT libnames)
    return()
  endif()

  if(LIB_PARENT_TARGET)
    # If the parent targets aren't created we should create them
    if(NOT TARGET ${LIB_PARENT_TARGET})
      add_custom_target(${LIB_PARENT_TARGET})
      set_target_properties(${LIB_PARENT_TARGET} PROPERTIES
                            FOLDER "Compiler-RT/Runtimes")
    endif()
  endif()

  foreach(libname ${libnames})
    # If you are using a multi-configuration generator we don't generate
    # per-library install rules, so we fall back to the parent target COMPONENT
    if(CMAKE_CONFIGURATION_TYPES AND LIB_PARENT_TARGET)
      set(COMPONENT_OPTION COMPONENT ${LIB_PARENT_TARGET})
    else()
      set(COMPONENT_OPTION COMPONENT ${libname})
    endif()

    if(type STREQUAL "SHARED")
      list(APPEND LIB_DEFS COMPILER_RT_SHARED_LIB)
    endif()

    if(type STREQUAL "OBJECT")
      if(CMAKE_C_COMPILER_ID MATCHES Clang AND CMAKE_C_COMPILER_TARGET)
        list(APPEND extra_cflags_${libname} "--target=${CMAKE_C_COMPILER_TARGET}")
      endif()
      if(CMAKE_SYSROOT)
        list(APPEND extra_cflags_${libname} "--sysroot=${CMAKE_SYSROOT}")
      endif()
      string(REPLACE ";" " " extra_cflags_${libname} "${extra_cflags_${libname}}")
      string(REGEX MATCHALL "<[A-Za-z0-9_]*>" substitutions
             ${CMAKE_C_COMPILE_OBJECT})
      set(compile_command_${libname} "${CMAKE_C_COMPILE_OBJECT}")

      set(output_file_${libname} ${output_name_${libname}}${CMAKE_C_OUTPUT_EXTENSION})
      foreach(substitution ${substitutions})
        if(substitution STREQUAL "<CMAKE_C_COMPILER>")
          string(REPLACE "<CMAKE_C_COMPILER>" "${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}"
                 compile_command_${libname} ${compile_command_${libname}})
        elseif(substitution STREQUAL "<OBJECT>")
          string(REPLACE "<OBJECT>" "${output_dir_${libname}}/${output_file_${libname}}"
                 compile_command_${libname} ${compile_command_${libname}})
        elseif(substitution STREQUAL "<SOURCE>")
          string(REPLACE "<SOURCE>" "${sources_${libname}}"
                 compile_command_${libname} ${compile_command_${libname}})
        elseif(substitution STREQUAL "<FLAGS>")
          string(REPLACE "<FLAGS>" "${CMAKE_C_FLAGS} ${extra_cflags_${libname}}"
                 compile_command_${libname} ${compile_command_${libname}})
        else()
          string(REPLACE "${substitution}" "" compile_command_${libname}
                 ${compile_command_${libname}})
        endif()
      endforeach()
      separate_arguments(compile_command_${libname})
      add_custom_command(
          OUTPUT ${output_dir_${libname}}/${output_file_${libname}}
          COMMAND ${compile_command_${libname}}
          DEPENDS ${sources_${libname}}
          COMMENT "Building C object ${output_file_${libname}}")
      add_custom_target(${libname} DEPENDS ${output_dir_${libname}}/${output_file_${libname}})
      set_target_properties(${libname} PROPERTIES FOLDER "Compiler-RT/Codegenning")
      install(FILES ${output_dir_${libname}}/${output_file_${libname}}
        DESTINATION ${install_dir_${libname}}
        ${COMPONENT_OPTION})
    else()
      add_library(${libname} ${type} ${sources_${libname}})
      set_target_compile_flags(${libname} ${extra_cflags_${libname}})
      set_target_link_flags(${libname} ${extra_link_flags_${libname}})
      set_property(TARGET ${libname} APPEND PROPERTY
                   COMPILE_DEFINITIONS ${LIB_DEFS})
      set_target_output_directories(${libname} ${output_dir_${libname}})
      install(TARGETS ${libname}
        ARCHIVE DESTINATION ${install_dir_${libname}}
                ${COMPONENT_OPTION}
        LIBRARY DESTINATION ${install_dir_${libname}}
                ${COMPONENT_OPTION}
        RUNTIME DESTINATION ${install_dir_${libname}}
                ${COMPONENT_OPTION})
    endif()
    if(LIB_DEPS)
      add_dependencies(${libname} ${LIB_DEPS})
    endif()
    set_target_properties(${libname} PROPERTIES
        OUTPUT_NAME ${output_name_${libname}}
        FOLDER "Compiler-RT/Runtimes")
    if(LIB_LINK_LIBS)
      target_link_libraries(${libname} PRIVATE ${LIB_LINK_LIBS})
    endif()
    if(builtins_${libname})
      target_link_libraries(${libname} PRIVATE ${builtins_${libname}})
    endif()
    if(${type} STREQUAL "SHARED")
      if(APPLE OR WIN32)
        set_property(TARGET ${libname} PROPERTY BUILD_WITH_INSTALL_RPATH ON)
      endif()
      if(WIN32 AND NOT CYGWIN AND NOT MINGW)
        set_target_properties(${libname} PROPERTIES IMPORT_PREFIX "")
        set_target_properties(${libname} PROPERTIES IMPORT_SUFFIX ".lib")
      endif()
      if (APPLE AND NOT CMAKE_LINKER MATCHES ".*lld.*")
        # Apple's linker signs the resulting dylib with an ad-hoc code signature in
        # most situations, except:
        # 1. Versions of ld64 prior to ld64-609 in Xcode 12 predate this behavior.
        # 2. Apple's new linker does not when building with `-darwin-target-variant`
        #    to support macOS Catalyst.
        #
        # Explicitly re-signing the dylib works around both of these issues. The
        # signature is marked as `linker-signed` when that is supported so that it
        # behaves as expected when processed by subsequent tooling.
        #
        # Detect whether `codesign` supports `-o linker-signed` by passing it as an
        # argument and looking for `invalid argument "linker-signed"` in its output.
        # FIXME: Remove this once all supported toolchains support `-o linker-signed`.
        execute_process(
          COMMAND sh -c "codesign -f -s - -o linker-signed this-does-not-exist 2>&1 | grep -q linker-signed"
          RESULT_VARIABLE CODESIGN_SUPPORTS_LINKER_SIGNED
        )

        set(EXTRA_CODESIGN_ARGUMENTS)
        if (CODESIGN_SUPPORTS_LINKER_SIGNED)
          list(APPEND EXTRA_CODESIGN_ARGUMENTS -o linker-signed)
        endif()

        add_custom_command(TARGET ${libname}
          POST_BUILD
          COMMAND codesign --sign - ${EXTRA_CODESIGN_ARGUMENTS} $<TARGET_FILE:${libname}>
          WORKING_DIRECTORY ${COMPILER_RT_OUTPUT_LIBRARY_DIR}
          COMMAND_EXPAND_LISTS
        )
      endif()
    endif()

    set(parent_target_arg)
    if(LIB_PARENT_TARGET)
      set(parent_target_arg PARENT_TARGET ${LIB_PARENT_TARGET})
    endif()
    add_compiler_rt_install_targets(${libname} ${parent_target_arg})

    if(APPLE)
      set_target_properties(${libname} PROPERTIES
      OSX_ARCHITECTURES "${LIB_ARCHS_${libname}}")
    endif()

    if(type STREQUAL "SHARED")
      rt_externalize_debuginfo(${libname})
    endif()
  endforeach()
  if(LIB_PARENT_TARGET)
    add_dependencies(${LIB_PARENT_TARGET} ${libnames})
  endif()
endfunction()

# Compile and register compiler-rt tests.
# generate_compiler_rt_tests(<output object files> <test_suite> <test_name>
#                           <test architecture>
#                           KIND <custom prefix>
#                           SUBDIR <subdirectory for testing binary>
#                           SOURCES <sources to compile>
#                           RUNTIME <tests runtime to link in>
#                           CFLAGS <compile-time flags>
#                           COMPILE_DEPS <compile-time dependencies>
#                           DEPS <dependencies>
#                           LINK_FLAGS <flags to use during linking>
# )
function(generate_compiler_rt_tests test_objects test_suite testname arch)
  cmake_parse_arguments(TEST "" "KIND;RUNTIME;SUBDIR"
    "SOURCES;COMPILE_DEPS;DEPS;CFLAGS;LINK_FLAGS" ${ARGN})

  foreach(source ${TEST_SOURCES})
    sanitizer_test_compile(
      "${test_objects}" "${source}" "${arch}"
      KIND ${TEST_KIND}
      COMPILE_DEPS ${TEST_COMPILE_DEPS}
      DEPS ${TEST_DEPS}
      CFLAGS ${TEST_CFLAGS}
      )
  endforeach()

  set(TEST_DEPS ${${test_objects}})

  if(NOT "${TEST_RUNTIME}" STREQUAL "")
    list(APPEND TEST_DEPS ${TEST_RUNTIME})
    list(APPEND "${test_objects}" $<TARGET_FILE:${TEST_RUNTIME}>)
  endif()

  add_compiler_rt_test(${test_suite} "${testname}" "${arch}"
    SUBDIR ${TEST_SUBDIR}
    OBJECTS ${${test_objects}}
    DEPS ${TEST_DEPS}
    LINK_FLAGS ${TEST_LINK_FLAGS}
    )
  set("${test_objects}" "${${test_objects}}" PARENT_SCOPE)
endfunction()

# Link objects into a single executable with COMPILER_RT_TEST_COMPILER,
# using specified link flags. Make executable a part of provided
# test_suite.
# add_compiler_rt_test(<test_suite> <test_name> <arch>
#                      SUBDIR <subdirectory for binary>
#                      OBJECTS <object files>
#                      DEPS <deps (e.g. runtime libs)>
#                      LINK_FLAGS <link flags>)
function(add_compiler_rt_test test_suite test_name arch)
  cmake_parse_arguments(TEST "" "SUBDIR" "OBJECTS;DEPS;LINK_FLAGS" "" ${ARGN})
  set(output_dir ${CMAKE_CURRENT_BINARY_DIR})
  if(TEST_SUBDIR)
    set(output_dir "${output_dir}/${TEST_SUBDIR}")
  endif()
  set(output_dir "${output_dir}/${CMAKE_CFG_INTDIR}")
  file(MAKE_DIRECTORY "${output_dir}")
  set(output_bin "${output_dir}/${test_name}")
  if(WIN32)
    set(output_bin "${output_bin}.exe")
  endif()

  # Use host compiler in a standalone build, and just-built Clang otherwise.
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND TEST_DEPS clang)
  endif()

  get_target_flags_for_arch(${arch} TARGET_LINK_FLAGS)
  list(APPEND TEST_LINK_FLAGS ${TARGET_LINK_FLAGS})

  # If we're not on MSVC, include the linker flags from CMAKE but override them
  # with the provided link flags. This ensures that flags which are required to
  # link programs at all are included, but the changes needed for the test
  # trump. With MSVC we can't do that because CMake is set up to run link.exe
  # when linking, not the compiler. Here, we hack it to use the compiler
  # because we want to use -fsanitize flags.

  # Only add CMAKE_EXE_LINKER_FLAGS when in a standalone bulid.
  # Or else CMAKE_EXE_LINKER_FLAGS contains flags for build compiler of Clang/llvm.
  # This might not be the same as what the COMPILER_RT_TEST_COMPILER supports.
  # eg: the build compiler use lld linker and we build clang with default ld linker
  # then to be tested clang will complain about lld options like --color-diagnostics.
  if(NOT MSVC AND COMPILER_RT_STANDALONE_BUILD)
    set(TEST_LINK_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TEST_LINK_FLAGS}")
    separate_arguments(TEST_LINK_FLAGS)
  endif()
  if(NOT COMPILER_RT_STANDALONE_BUILD AND COMPILER_RT_HAS_LLD AND "lld" IN_LIST LLVM_ENABLE_PROJECTS)
    # CMAKE_EXE_LINKER_FLAGS may contain -fuse=lld
    # FIXME: -DLLVM_ENABLE_LLD=ON and -DLLVM_ENABLE_PROJECTS without lld case.
    list(APPEND TEST_DEPS lld)
  endif()
  add_custom_command(
    OUTPUT "${output_bin}"
    COMMAND ${COMPILER_RT_TEST_CXX_COMPILER} ${TEST_OBJECTS} -o "${output_bin}"
            ${TEST_LINK_FLAGS}
    DEPENDS ${TEST_DEPS}
    )
  add_custom_target(T${test_name} DEPENDS "${output_bin}")
  set_target_properties(T${test_name} PROPERTIES FOLDER "Compiler-RT/Tests")

  # Make the test suite depend on the binary.
  add_dependencies(${test_suite} T${test_name})
endfunction()

macro(add_compiler_rt_resource_file target_name file_name component)
  set(src_file "${CMAKE_CURRENT_SOURCE_DIR}/${file_name}")
  set(dst_file "${COMPILER_RT_OUTPUT_DIR}/share/${file_name}")
  add_custom_command(OUTPUT ${dst_file}
    DEPENDS ${src_file}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src_file} ${dst_file}
    COMMENT "Copying ${file_name}...")
  add_custom_target(${target_name} DEPENDS ${dst_file})
  # Install in Clang resource directory.
  install(FILES ${file_name}
    DESTINATION ${COMPILER_RT_INSTALL_DATA_DIR}
    COMPONENT ${component})
  add_dependencies(${component} ${target_name})

  set_target_properties(${target_name} PROPERTIES FOLDER "Compiler-RT/Resources")
endmacro()

macro(add_compiler_rt_script name)
  set(dst ${COMPILER_RT_EXEC_OUTPUT_DIR}/${name})
  set(src ${CMAKE_CURRENT_SOURCE_DIR}/${name})
  add_custom_command(OUTPUT ${dst}
    DEPENDS ${src}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
    COMMENT "Copying ${name}...")
  add_custom_target(${name} DEPENDS ${dst})
  install(FILES ${dst}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    DESTINATION ${COMPILER_RT_INSTALL_BINARY_DIR})
endmacro(add_compiler_rt_script src name)

# Builds custom version of libc++ and installs it in <prefix>.
# Can be used to build sanitized versions of libc++ for running unit tests.
# add_custom_libcxx(<name> <prefix>
#                   DEPS <list of build deps>
#                   CFLAGS <list of compile flags>
#                   USE_TOOLCHAIN)
macro(add_custom_libcxx name prefix)
  if(NOT COMPILER_RT_LIBCXX_PATH)
    message(FATAL_ERROR "libcxx not found!")
  endif()
  if(NOT COMPILER_RT_LIBCXXABI_PATH)
    message(FATAL_ERROR "libcxxabi not found!")
  endif()

  cmake_parse_arguments(LIBCXX "USE_TOOLCHAIN" "" "DEPS;CFLAGS;CMAKE_ARGS" ${ARGN})

  if(LIBCXX_USE_TOOLCHAIN)
    set(compiler_args -DCMAKE_C_COMPILER=${COMPILER_RT_TEST_COMPILER}
                      -DCMAKE_CXX_COMPILER=${COMPILER_RT_TEST_CXX_COMPILER})
    if(NOT COMPILER_RT_STANDALONE_BUILD AND NOT LLVM_RUNTIMES_BUILD)
      set(toolchain_deps $<TARGET_FILE:clang>)
      set(force_deps DEPENDS $<TARGET_FILE:clang>)
    endif()
  else()
    set(compiler_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
  endif()

  add_custom_target(${name}-clear
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${prefix}
    COMMENT "Clobbering ${name} build directories"
    USES_TERMINAL
    )
  set_target_properties(${name}-clear PROPERTIES FOLDER "Compiler-RT/Metatargets")

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp
    DEPENDS ${LIBCXX_DEPS} ${toolchain_deps}
    COMMAND ${CMAKE_COMMAND} -E touch ${prefix}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp
    COMMENT "Clobbering bootstrap build directories"
    )

  add_custom_target(${name}-clobber
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp)
  set_target_properties(${name}-clobber PROPERTIES FOLDER "Compiler-RT/Metatargets")

  set(PASSTHROUGH_VARIABLES
    ANDROID
    ANDROID_NATIVE_API_LEVEL
    CMAKE_C_COMPILER_TARGET
    CMAKE_CXX_COMPILER_TARGET
    CMAKE_SHARED_LINKER_FLAGS
    CMAKE_MODULE_LINKER_FLAGS
    CMAKE_EXE_LINKER_FLAGS
    CMAKE_INSTALL_PREFIX
    CMAKE_MAKE_PROGRAM
    CMAKE_LINKER
    CMAKE_AR
    CMAKE_RANLIB
    CMAKE_NM
    CMAKE_OBJCOPY
    CMAKE_OBJDUMP
    CMAKE_STRIP
    CMAKE_READELF
    CMAKE_SYSROOT
    CMAKE_TOOLCHAIN_FILE
    LIBCXX_HAS_MUSL_LIBC
    LIBCXX_HAS_GCC_S_LIB
    LIBCXX_HAS_PTHREAD_LIB
    LIBCXX_HAS_RT_LIB
    LIBCXX_USE_COMPILER_RT
    LIBCXXABI_HAS_PTHREAD_LIB
    PYTHON_EXECUTABLE
    Python3_EXECUTABLE
    Python2_EXECUTABLE
    CMAKE_SYSTEM_NAME)
  foreach(variable ${PASSTHROUGH_VARIABLES})
    get_property(is_value_set CACHE ${variable} PROPERTY VALUE SET)
    if(${is_value_set})
      get_property(value CACHE ${variable} PROPERTY VALUE)
      list(APPEND CMAKE_PASSTHROUGH_VARIABLES -D${variable}=${value})
    endif()
  endforeach()

  string(REPLACE ";" " " LIBCXX_C_FLAGS "${LIBCXX_CFLAGS}")
  get_property(C_FLAGS CACHE CMAKE_C_FLAGS PROPERTY VALUE)
  set(LIBCXX_C_FLAGS "${LIBCXX_C_FLAGS} ${C_FLAGS}")

  string(REPLACE ";" " " LIBCXX_CXX_FLAGS "${LIBCXX_CFLAGS}")
  get_property(CXX_FLAGS CACHE CMAKE_CXX_FLAGS PROPERTY VALUE)
  set(LIBCXX_CXX_FLAGS "${LIBCXX_CXX_FLAGS} ${CXX_FLAGS}")

  if(CMAKE_VERBOSE_MAKEFILE)
    set(verbose -DCMAKE_VERBOSE_MAKEFILE=ON)
  endif()

  ExternalProject_Add(${name}
    DEPENDS ${name}-clobber ${LIBCXX_DEPS}
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/${name}
    SOURCE_DIR ${LLVM_MAIN_SRC_DIR}/../runtimes
    BINARY_DIR ${prefix}
    CMAKE_ARGS ${CMAKE_PASSTHROUGH_VARIABLES}
               ${compiler_args}
               ${verbose}
               -DCMAKE_C_FLAGS=${LIBCXX_C_FLAGS}
               -DCMAKE_CXX_FLAGS=${LIBCXX_CXX_FLAGS}
               -DCMAKE_BUILD_TYPE=Release
               -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
               -DLLVM_PATH=${LLVM_MAIN_SRC_DIR}
               -DLLVM_ENABLE_RUNTIMES=libcxx|libcxxabi
               -DLIBCXXABI_USE_LLVM_UNWINDER=OFF
               -DLIBCXXABI_ENABLE_SHARED=OFF
               -DLIBCXXABI_HERMETIC_STATIC_LIBRARY=ON
               -DLIBCXXABI_INCLUDE_TESTS=OFF
               -DLIBCXX_CXX_ABI=libcxxabi
               -DLIBCXX_ENABLE_SHARED=OFF
               -DLIBCXX_HERMETIC_STATIC_LIBRARY=ON
               -DLIBCXX_INCLUDE_BENCHMARKS=OFF
               -DLIBCXX_INCLUDE_TESTS=OFF
               -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON
               ${LIBCXX_CMAKE_ARGS}
    INSTALL_COMMAND ""
    STEP_TARGETS configure build
    BUILD_ALWAYS 1
    USES_TERMINAL_CONFIGURE 1
    USES_TERMINAL_BUILD 1
    USES_TERMINAL_INSTALL 1
    LIST_SEPARATOR |
    EXCLUDE_FROM_ALL TRUE
    BUILD_BYPRODUCTS "${prefix}/lib/libc++.a" "${prefix}/lib/libc++abi.a"
    )

  if (CMAKE_GENERATOR MATCHES "Make")
    set(run_clean "$(MAKE)" "-C" "${prefix}" "clean")
  else()
    set(run_clean ${CMAKE_COMMAND} --build ${prefix} --target clean
                                   --config "$<CONFIG>")
  endif()

  ExternalProject_Add_Step(${name} clean
    COMMAND ${run_clean}
    COMMENT "Cleaning ${name}..."
    DEPENDEES configure
    ${force_deps}
    WORKING_DIRECTORY ${prefix}
    EXCLUDE_FROM_MAIN 1
    USES_TERMINAL 1
    )
  ExternalProject_Add_StepTargets(${name} clean)

  if(LIBCXX_USE_TOOLCHAIN)
    add_dependencies(${name}-clean ${name}-clobber)
    set_target_properties(${name}-clean PROPERTIES
      SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp)
  endif()
endmacro()

function(rt_externalize_debuginfo name)
  if(NOT COMPILER_RT_EXTERNALIZE_DEBUGINFO)
    return()
  endif()

  if(NOT COMPILER_RT_EXTERNALIZE_DEBUGINFO_SKIP_STRIP)
    set(strip_command COMMAND xcrun strip -Sl $<TARGET_FILE:${name}>)
  endif()

  if(APPLE)
    if(CMAKE_CXX_FLAGS MATCHES "-flto"
      OR CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE} MATCHES "-flto")

      set(lto_object ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${name}-lto.o)
      set_property(TARGET ${name} APPEND_STRING PROPERTY
        LINK_FLAGS " -Wl,-object_path_lto -Wl,${lto_object}")
    endif()
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND xcrun dsymutil $<TARGET_FILE:${name}>
      ${strip_command})
  else()
    message(FATAL_ERROR "COMPILER_RT_EXTERNALIZE_DEBUGINFO isn't implemented for non-darwin platforms!")
  endif()
endfunction()


# Configure lit configuration files, including compiler-rt specific variables.
function(configure_compiler_rt_lit_site_cfg input output)
  set_llvm_build_mode()

  get_compiler_rt_output_dir(${COMPILER_RT_DEFAULT_TARGET_ARCH} output_dir)

  string(REPLACE ${CMAKE_CFG_INTDIR} ${LLVM_BUILD_MODE} COMPILER_RT_RESOLVED_TEST_COMPILER ${COMPILER_RT_TEST_COMPILER})
  string(REPLACE ${CMAKE_CFG_INTDIR} ${LLVM_BUILD_MODE} COMPILER_RT_RESOLVED_OUTPUT_DIR ${COMPILER_RT_OUTPUT_DIR})
  string(REPLACE ${CMAKE_CFG_INTDIR} ${LLVM_BUILD_MODE} COMPILER_RT_RESOLVED_LIBRARY_OUTPUT_DIR ${output_dir})

  configure_lit_site_cfg(${input} ${output})
endfunction()
