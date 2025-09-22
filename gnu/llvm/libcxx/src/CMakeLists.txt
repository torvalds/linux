set(LIBCXX_LIB_CMAKEFILES_DIR "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}"  PARENT_SCOPE)

# Get sources
set(LIBCXX_SOURCES
  algorithm.cpp
  any.cpp
  bind.cpp
  call_once.cpp
  charconv.cpp
  chrono.cpp
  error_category.cpp
  exception.cpp
  expected.cpp
  filesystem/filesystem_clock.cpp
  filesystem/filesystem_error.cpp
  filesystem/path_parser.h
  filesystem/path.cpp
  functional.cpp
  hash.cpp
  include/apple_availability.h
  include/atomic_support.h
  include/config_elast.h
  include/refstring.h
  include/ryu/common.h
  include/ryu/d2fixed.h
  include/ryu/d2fixed_full_table.h
  include/ryu/d2s.h
  include/ryu/d2s_full_table.h
  include/ryu/d2s_intrinsics.h
  include/ryu/digit_table.h
  include/ryu/f2s.h
  include/ryu/ryu.h
  include/to_chars_floating_point.h
  legacy_pointer_safety.cpp
  memory.cpp
  memory_resource.cpp
  new_handler.cpp
  new_helpers.cpp
  optional.cpp
  print.cpp
  random_shuffle.cpp
  ryu/d2fixed.cpp
  ryu/d2s.cpp
  ryu/f2s.cpp
  stdexcept.cpp
  string.cpp
  support/runtime/exception_fallback.ipp
  support/runtime/exception_glibcxx.ipp
  support/runtime/exception_libcxxabi.ipp
  support/runtime/exception_libcxxrt.ipp
  support/runtime/exception_msvc.ipp
  support/runtime/exception_pointer_cxxabi.ipp
  support/runtime/exception_pointer_glibcxx.ipp
  support/runtime/exception_pointer_msvc.ipp
  support/runtime/exception_pointer_unimplemented.ipp
  support/runtime/stdexcept_default.ipp
  support/runtime/stdexcept_vcruntime.ipp
  system_error.cpp
  typeinfo.cpp
  valarray.cpp
  variant.cpp
  vector.cpp
  verbose_abort.cpp
  )

if (LIBCXX_ENABLE_THREADS)
  list(APPEND LIBCXX_SOURCES
    atomic.cpp
    barrier.cpp
    condition_variable_destructor.cpp
    condition_variable.cpp
    future.cpp
    mutex_destructor.cpp
    mutex.cpp
    shared_mutex.cpp
    thread.cpp
    )
endif()

if (LIBCXX_ENABLE_RANDOM_DEVICE)
  list(APPEND LIBCXX_SOURCES
    random.cpp
    )
endif()

if (LIBCXX_ENABLE_LOCALIZATION)
  list(APPEND LIBCXX_SOURCES
    fstream.cpp
    include/sso_allocator.h
    ios.cpp
    ios.instantiations.cpp
    iostream.cpp
    locale.cpp
    ostream.cpp
    regex.cpp
    strstream.cpp
    )
endif()

if(WIN32)
  list(APPEND LIBCXX_SOURCES
    support/win32/compiler_rt_shims.cpp
    support/win32/locale_win32.cpp
    support/win32/support.cpp
    )

  if (NOT LIBCXX_HAS_PTHREAD_API)
    list(APPEND LIBCXX_SOURCES
      support/win32/thread_win32.cpp
      )
  endif()
elseif(ZOS)
  list(APPEND LIBCXX_SOURCES
    support/ibm/mbsnrtowcs.cpp
    support/ibm/wcsnrtombs.cpp
    support/ibm/xlocale_zos.cpp
    )
endif()

if (LIBCXX_ENABLE_FILESYSTEM)
  list(APPEND LIBCXX_SOURCES
    filesystem/directory_entry.cpp
    filesystem/directory_iterator.cpp
    filesystem/file_descriptor.h
    filesystem/operations.cpp
    filesystem/posix_compat.h
    filesystem/time_utils.h
    )
  # Filesystem uses __int128_t, which requires a definition of __muloi4 when
  # compiled with UBSAN. This definition is not provided by libgcc_s, but is
  # provided by compiler-rt. So we need to disable it to avoid having multiple
  # definitions. See filesystem/int128_builtins.cpp.
  if (NOT LIBCXX_USE_COMPILER_RT)
    list(APPEND LIBCXX_SOURCES
      filesystem/int128_builtins.cpp
      )
  endif()
endif()

if (LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS)
  list(APPEND LIBCXX_SOURCES
    new.cpp
    )
endif()

# Add all the headers to the project for IDEs.
if (LIBCXX_CONFIGURE_IDE)
  file(GLOB_RECURSE LIBCXX_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../include/*)
  if(WIN32)
    file( GLOB LIBCXX_WIN32_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../include/__support/win32/*.h)
    list(APPEND LIBCXX_HEADERS ${LIBCXX_WIN32_HEADERS})
  endif()
  # Force them all into the headers dir on MSVC, otherwise they end up at
  # project scope because they don't have extensions.
  if (MSVC_IDE)
    source_group("Header Files" FILES ${LIBCXX_HEADERS})
  endif()
endif()

if(NOT LIBCXX_INSTALL_LIBRARY)
  set(exclude_from_all EXCLUDE_FROM_ALL)
endif()

if (LIBCXX_GENERATE_COVERAGE AND NOT LIBCXX_COVERAGE_LIBRARY)
  find_compiler_rt_library(profile LIBCXX_COVERAGE_LIBRARY)
endif()
add_library_flags_if(LIBCXX_COVERAGE_LIBRARY "${LIBCXX_COVERAGE_LIBRARY}")

if (APPLE AND LLVM_USE_SANITIZER)
  if (("${LLVM_USE_SANITIZER}" STREQUAL "Address") OR
      ("${LLVM_USE_SANITIZER}" STREQUAL "Address;Undefined") OR
      ("${LLVM_USE_SANITIZER}" STREQUAL "Undefined;Address"))
    set(LIBFILE "libclang_rt.asan_osx_dynamic.dylib")
  elseif("${LLVM_USE_SANITIZER}" STREQUAL "Undefined")
    set(LIBFILE "libclang_rt.ubsan_osx_dynamic.dylib")
  elseif("${LLVM_USE_SANITIZER}" STREQUAL "Thread")
    set(LIBFILE "libclang_rt.tsan_osx_dynamic.dylib")
  else()
    message(WARNING "LLVM_USE_SANITIZER=${LLVM_USE_SANITIZER} is not supported on OS X")
  endif()
  if (LIBFILE)
    find_compiler_rt_library(builtins LIBCXX_BUILTINS_LIBRARY)
    get_filename_component(LIBDIR "${LIBCXX_BUILTINS_LIBRARY}" DIRECTORY)
    if (NOT IS_DIRECTORY "${LIBDIR}")
      message(FATAL_ERROR "Cannot find compiler-rt directory on OS X required for LLVM_USE_SANITIZER")
    endif()
    set(LIBCXX_SANITIZER_LIBRARY "${LIBDIR}/${LIBFILE}")
    set(LIBCXX_SANITIZER_LIBRARY "${LIBCXX_SANITIZER_LIBRARY}" PARENT_SCOPE)
    message(STATUS "Manually linking compiler-rt library: ${LIBCXX_SANITIZER_LIBRARY}")
    add_library_flags("${LIBCXX_SANITIZER_LIBRARY}")
    add_link_flags("-Wl,-rpath,${LIBDIR}")
  endif()
endif()

split_list(LIBCXX_COMPILE_FLAGS)
split_list(LIBCXX_LINK_FLAGS)

# Build the shared library.
if (LIBCXX_ENABLE_SHARED)
  add_library(cxx_shared SHARED ${exclude_from_all} ${LIBCXX_SOURCES} ${LIBCXX_HEADERS})
  target_include_directories(cxx_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(cxx_shared PUBLIC cxx-headers libcxx-libc-shared
                                   PRIVATE ${LIBCXX_LIBRARIES})
  set_target_properties(cxx_shared
    PROPERTIES
      COMPILE_FLAGS "${LIBCXX_COMPILE_FLAGS}"
      LINK_FLAGS    "${LIBCXX_LINK_FLAGS}"
      OUTPUT_NAME   "${LIBCXX_SHARED_OUTPUT_NAME}"
      VERSION       "${LIBCXX_LIBRARY_VERSION}"
      SOVERSION     "${LIBCXX_ABI_VERSION}"
      DEFINE_SYMBOL ""
  )
  cxx_add_common_build_flags(cxx_shared)

  if(ZOS)
    add_custom_command(TARGET cxx_shared POST_BUILD
      COMMAND
        ${LIBCXX_SOURCE_DIR}/utils/zos_rename_dll_side_deck.sh
        $<TARGET_LINKER_FILE_NAME:cxx_shared> $<TARGET_FILE_NAME:cxx_shared> "${LIBCXX_DLL_NAME}"
      COMMENT "Rename dll name inside the side deck file"
      WORKING_DIRECTORY $<TARGET_FILE_DIR:cxx_shared>
    )
  endif()

  # Link against libc++abi
  if (LIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY)
    target_link_libraries(cxx_shared PRIVATE libcxx-abi-shared-objects)
  else()
    target_link_libraries(cxx_shared PUBLIC libcxx-abi-shared)
  endif()

  # Maybe force some symbols to be weak, not weak or not exported.
  # TODO: This shouldn't depend on the platform, and ideally it should be done in the sources.
  if (APPLE AND LIBCXX_CXX_ABI MATCHES "libcxxabi$"
            AND NOT LIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY)
    target_link_libraries(cxx_shared PRIVATE
      "-Wl,-unexported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/../lib/libc++unexp.exp"
      "-Wl,-force_symbols_not_weak_list,${CMAKE_CURRENT_SOURCE_DIR}/../lib/notweak.exp"
      "-Wl,-force_symbols_weak_list,${CMAKE_CURRENT_SOURCE_DIR}/../lib/weak.exp")
  endif()

  # Generate a linker script in place of a libc++.so symlink.
  if (LIBCXX_ENABLE_ABI_LINKER_SCRIPT)
    set(link_libraries)

    set(imported_libname "$<TARGET_PROPERTY:libcxx-abi-shared,IMPORTED_LIBNAME>")
    set(output_name "$<TARGET_PROPERTY:libcxx-abi-shared,OUTPUT_NAME>")
    string(APPEND link_libraries "${CMAKE_LINK_LIBRARY_FLAG}$<IF:$<BOOL:${imported_libname}>,${imported_libname},${output_name}>")

    # TODO: Move to the same approach as above for the unwind library
    if (LIBCXXABI_USE_LLVM_UNWINDER)
      if (LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY)
        # libunwind is already included in libc++abi
      elseif (TARGET unwind_shared OR HAVE_LIBUNWIND)
        string(APPEND link_libraries " ${CMAKE_LINK_LIBRARY_FLAG}$<TARGET_PROPERTY:unwind_shared,OUTPUT_NAME>")
      else()
        string(APPEND link_libraries " ${CMAKE_LINK_LIBRARY_FLAG}unwind")
      endif()
    endif()

    set(linker_script "INPUT($<TARGET_SONAME_FILE_NAME:cxx_shared> ${link_libraries})")
    add_custom_command(TARGET cxx_shared POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E remove "$<TARGET_LINKER_FILE:cxx_shared>"
      COMMAND "${CMAKE_COMMAND}" -E echo "${linker_script}" > "$<TARGET_LINKER_FILE:cxx_shared>"
      COMMENT "Generating linker script: '${linker_script}' as file $<TARGET_LINKER_FILE:cxx_shared>"
      VERBATIM
    )
  endif()

  list(APPEND LIBCXX_BUILD_TARGETS "cxx_shared")
  if(WIN32 AND NOT MINGW AND NOT "${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
    # Since we most likely do not have a mt.exe replacement, disable the
    # manifest bundling.  This allows a normal cmake invocation to pass which
    # will attempt to use the manifest tool to generate the bundled manifest
    if (${CMAKE_CXX_COMPILER_FRONTEND_VARIANT} STREQUAL "MSVC")
      set_target_properties(cxx_shared PROPERTIES
                            APPEND_STRING PROPERTY LINK_FLAGS " /MANIFEST:NO")
    else()
      set_target_properties(cxx_shared PROPERTIES
                            APPEND_STRING PROPERTY LINK_FLAGS " -Xlinker /MANIFEST:NO")
    endif()
  endif()
endif()

set(CMAKE_STATIC_LIBRARY_PREFIX "lib")

# Build the static library.
if (LIBCXX_ENABLE_STATIC)
  add_library(cxx_static STATIC ${exclude_from_all} ${LIBCXX_SOURCES} ${LIBCXX_HEADERS})
  target_include_directories(cxx_static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(cxx_static PUBLIC cxx-headers libcxx-libc-static
                                   PRIVATE ${LIBCXX_LIBRARIES}
                                   PRIVATE libcxx-abi-static)
  set_target_properties(cxx_static
    PROPERTIES
      COMPILE_FLAGS "${LIBCXX_COMPILE_FLAGS}"
      LINK_FLAGS    "${LIBCXX_LINK_FLAGS}"
      OUTPUT_NAME   "${LIBCXX_STATIC_OUTPUT_NAME}"
  )
  cxx_add_common_build_flags(cxx_static)

  if (LIBCXX_HERMETIC_STATIC_LIBRARY)
    # If the hermetic library doesn't define the operator new/delete functions
    # then its code shouldn't declare them with hidden visibility.  They might
    # actually be provided by a shared library at link time.
    if (LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS)
      append_flags_if_supported(CXX_STATIC_LIBRARY_FLAGS -fvisibility-global-new-delete=force-hidden)
      if (NOT CXX_SUPPORTS_FVISIBILITY_GLOBAL_NEW_DELETE_EQ_FORCE_HIDDEN_FLAG)
        append_flags_if_supported(CXX_STATIC_LIBRARY_FLAGS -fvisibility-global-new-delete-hidden)
      endif()
    endif()
    target_compile_options(cxx_static PRIVATE ${CXX_STATIC_LIBRARY_FLAGS})
    # _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS can be defined in __config_site
    # too. Define it in the same way here, to avoid redefinition conflicts.
    target_compile_definitions(cxx_static PRIVATE _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS=)
  endif()

  list(APPEND LIBCXX_BUILD_TARGETS "cxx_static")
  # Attempt to merge the libc++.a archive and the ABI library archive into one.
  if (LIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY)
    target_link_libraries(cxx_static PRIVATE libcxx-abi-static-objects)
  endif()
endif()

# Add a meta-target for both libraries.
add_custom_target(cxx DEPENDS ${LIBCXX_BUILD_TARGETS})
add_dependencies(cxx-test-depends cxx)

set(LIBCXX_EXPERIMENTAL_SOURCES
  experimental/keep.cpp
  )

if (LIBCXX_PSTL_BACKEND STREQUAL "libdispatch")
  list(APPEND LIBCXX_EXPERIMENTAL_SOURCES
    pstl/libdispatch.cpp
    )
endif()

if (LIBCXX_ENABLE_LOCALIZATION AND LIBCXX_ENABLE_FILESYSTEM AND LIBCXX_ENABLE_TIME_ZONE_DATABASE)
  list(APPEND LIBCXX_EXPERIMENTAL_SOURCES
    experimental/include/tzdb/time_zone_private.h
    experimental/include/tzdb/types_private.h
    experimental/include/tzdb/tzdb_list_private.h
    experimental/include/tzdb/tzdb_private.h
    # TODO TZDB The exception could be moved in chrono once the TZDB library
    # is no longer experimental.
    experimental/chrono_exception.cpp
    experimental/time_zone.cpp
    experimental/tzdb.cpp
    experimental/tzdb_list.cpp
    )
endif()

add_library(cxx_experimental STATIC ${LIBCXX_EXPERIMENTAL_SOURCES})
target_link_libraries(cxx_experimental PUBLIC cxx-headers)
if (LIBCXX_ENABLE_SHARED)
  target_link_libraries(cxx_experimental PRIVATE cxx_shared)
else()
  target_link_libraries(cxx_experimental PRIVATE cxx_static)
endif()

if (LIBCXX_HERMETIC_STATIC_LIBRARY)
  # _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS can be defined in __config_site
  # too. Define it in the same way here, to avoid redefinition conflicts.
  target_compile_definitions(cxx_experimental PRIVATE _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS=)
endif()

set_target_properties(cxx_experimental
  PROPERTIES
    COMPILE_FLAGS "${LIBCXX_COMPILE_FLAGS}"
    OUTPUT_NAME   "c++experimental"
)
cxx_add_common_build_flags(cxx_experimental)
target_compile_options(cxx_experimental PUBLIC -D_LIBCPP_ENABLE_EXPERIMENTAL)
add_dependencies(cxx-test-depends cxx_experimental)

if (LIBCXX_INSTALL_SHARED_LIBRARY)
  install(TARGETS cxx_shared
    ARCHIVE DESTINATION ${LIBCXX_INSTALL_LIBRARY_DIR} COMPONENT cxx
    LIBRARY DESTINATION ${LIBCXX_INSTALL_LIBRARY_DIR} COMPONENT cxx
    RUNTIME DESTINATION ${LIBCXX_INSTALL_RUNTIME_DIR} COMPONENT cxx)
endif()

if (LIBCXX_INSTALL_STATIC_LIBRARY)
  install(TARGETS cxx_static
    ARCHIVE DESTINATION ${LIBCXX_INSTALL_LIBRARY_DIR} COMPONENT cxx
    LIBRARY DESTINATION ${LIBCXX_INSTALL_LIBRARY_DIR} COMPONENT cxx
    RUNTIME DESTINATION ${LIBCXX_INSTALL_RUNTIME_DIR} COMPONENT cxx)
endif()

if (LIBCXX_INSTALL_LIBRARY)
  install(TARGETS cxx_experimental
    LIBRARY DESTINATION ${LIBCXX_INSTALL_LIBRARY_DIR} COMPONENT cxx
    ARCHIVE DESTINATION ${LIBCXX_INSTALL_LIBRARY_DIR} COMPONENT cxx
    RUNTIME DESTINATION ${LIBCXX_INSTALL_RUNTIME_DIR} COMPONENT cxx)
endif()

# NOTE: This install command must go after the cxx install command otherwise
# it will not be executed after the library symlinks are installed.
if (LIBCXX_ENABLE_SHARED AND LIBCXX_ENABLE_ABI_LINKER_SCRIPT)
  install(FILES "$<TARGET_LINKER_FILE:cxx_shared>"
    DESTINATION ${LIBCXX_INSTALL_LIBRARY_DIR}
    COMPONENT libcxx)
endif()

if (NOT CMAKE_CONFIGURATION_TYPES)
    if(LIBCXX_INSTALL_LIBRARY)
      set(lib_install_target "cxx;cxx_experimental")
    endif()
    if(LIBCXX_INSTALL_HEADERS)
      set(header_install_target install-cxx-headers)
    endif()
    if(LIBCXX_INSTALL_MODULES)
      set(module_install_target install-cxx-modules)
    endif()
    add_custom_target(install-cxx
                      DEPENDS ${lib_install_target}
                              cxx_experimental
                              ${header_install_target}
                              ${module_install_target}
                      COMMAND "${CMAKE_COMMAND}"
                      -DCMAKE_INSTALL_COMPONENT=cxx
                      -P "${LIBCXX_BINARY_DIR}/cmake_install.cmake")
    add_custom_target(install-cxx-stripped
                      DEPENDS ${lib_install_target}
                              cxx_experimental
                              ${header_install_target}
                              ${module_install_target}
                      COMMAND "${CMAKE_COMMAND}"
                      -DCMAKE_INSTALL_COMPONENT=cxx
                      -DCMAKE_INSTALL_DO_STRIP=1
                      -P "${LIBCXX_BINARY_DIR}/cmake_install.cmake")
endif()
