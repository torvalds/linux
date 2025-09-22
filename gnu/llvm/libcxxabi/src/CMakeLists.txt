# Get sources
set(LIBCXXABI_SOURCES
  # C++ABI files
  cxa_aux_runtime.cpp
  cxa_default_handlers.cpp
  cxa_demangle.cpp
  cxa_exception_storage.cpp
  cxa_guard.cpp
  cxa_handlers.cpp
  cxa_vector.cpp
  cxa_virtual.cpp
  # C++ STL files
  stdlib_exception.cpp
  stdlib_stdexcept.cpp
  stdlib_typeinfo.cpp
  # Internal files
  abort_message.cpp
  fallback_malloc.cpp
  private_typeinfo.cpp
)

if (LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS)
  list(APPEND LIBCXXABI_SOURCES
    stdlib_new_delete.cpp
  )
endif()

if (LIBCXXABI_ENABLE_EXCEPTIONS)
  list(APPEND LIBCXXABI_SOURCES
    cxa_exception.cpp
    cxa_personality.cpp
  )
else()
  list(APPEND LIBCXXABI_SOURCES
    cxa_noexception.cpp
  )
endif()

if (LIBCXXABI_ENABLE_THREADS AND (UNIX OR FUCHSIA) AND NOT (APPLE OR CYGWIN)
    AND NOT (${CMAKE_SYSTEM_NAME} MATCHES "AIX"))
  list(APPEND LIBCXXABI_SOURCES
    cxa_thread_atexit.cpp
  )
endif()

set(LIBCXXABI_HEADERS
  ../include/cxxabi.h
)

# Add all the headers to the project for IDEs.
if (MSVC_IDE OR XCODE)
  # Force them all into the headers dir on MSVC, otherwise they end up at
  # project scope because they don't have extensions.
  if (MSVC_IDE)
    source_group("Header Files" FILES ${LIBCXXABI_HEADERS})
  endif()
endif()

# Some files depend on libc++ internals.
include_directories("${LIBCXXABI_LIBCXX_PATH}/src")

if (LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL)
  add_definitions(-DHAVE___CXA_THREAD_ATEXIT_IMPL)
endif()

if (LIBCXXABI_ENABLE_FORGIVING_DYNAMIC_CAST)
  add_definitions(-D_LIBCXXABI_FORGIVING_DYNAMIC_CAST)
endif()

if (NOT APPLE) # On Apple platforms, we always use -nostdlib++ so we don't need to re-add other libraries
  if (LIBCXXABI_ENABLE_THREADS)
    add_library_flags_if(LIBCXXABI_HAS_PTHREAD_LIB pthread)
  endif()

  add_library_flags_if(LIBCXXABI_HAS_C_LIB c)
endif()

if (NOT LIBCXXABI_USE_COMPILER_RT)
  add_library_flags_if(LIBCXXABI_HAS_GCC_LIB gcc)
endif()
if (NOT LIBCXXABI_USE_LLVM_UNWINDER)
  add_library_flags_if(LIBCXXABI_HAS_GCC_S_LIB gcc_s)
endif()

if (MINGW)
  # MINGW_LIBRARIES is defined in config-ix.cmake
  list(APPEND LIBCXXABI_LIBRARIES ${MINGW_LIBRARIES})
endif()
if (ANDROID AND ANDROID_PLATFORM_LEVEL LESS 21)
  list(APPEND LIBCXXABI_LIBRARIES android_support)
endif()

# Setup flags.
if (CXX_SUPPORTS_NOSTDLIBXX_FLAG)
  add_link_flags_if_supported(-nostdlib++)
else()
  add_link_flags_if_supported(-nodefaultlibs)
endif()

if (CXX_SUPPORTS_UNWINDLIB_EQ_NONE_FLAG AND LIBCXXABI_USE_LLVM_UNWINDER)
  # If we're linking directly against the libunwind that we're building
  # in the same invocation, don't try to link in the toolchain's
  # default libunwind (which may be missing still).
  add_link_flags_if_supported(--unwindlib=none)
endif()

if ( APPLE )
  if (LLVM_USE_SANITIZER)
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
      find_compiler_rt_library(builtins LIBCXXABI_BUILTINS_LIBRARY)
      get_filename_component(LIBDIR "${LIBCXXABI_BUILTINS_LIBRARY}" DIRECTORY)
      if (NOT IS_DIRECTORY "${LIBDIR}")
        message(FATAL_ERROR "Cannot find compiler-rt directory on OS X required for LLVM_USE_SANITIZER")
      endif()
      set(LIBCXXABI_SANITIZER_LIBRARY "${LIBDIR}/${LIBFILE}")
      set(LIBCXXABI_SANITIZER_LIBRARY "${LIBCXXABI_SANITIZER_LIBRARY}" PARENT_SCOPE)
      message(STATUS "Manually linking compiler-rt library: ${LIBCXXABI_SANITIZER_LIBRARY}")
      add_library_flags("${LIBCXXABI_SANITIZER_LIBRARY}")
      add_link_flags("-Wl,-rpath,${LIBDIR}")
    endif()
  endif()

  # Make sure we link in CrashReporterClient if we find it -- it's used by
  # abort() on Apple platforms when building the system dylib.
  find_library(CrashReporterClient NAMES libCrashReporterClient.a
                                   PATHS "${CMAKE_OSX_SYSROOT}/usr/local/lib")
  if (CrashReporterClient)
    message(STATUS "Linking with CrashReporterClient at ${CrashReporterClient}")
    add_library_flags("${CrashReporterClient}")
  else()
    message(STATUS "Could not find CrashReporterClient, not linking against it")
  endif()
endif()

split_list(LIBCXXABI_COMPILE_FLAGS)
split_list(LIBCXXABI_LINK_FLAGS)

# FIXME: libc++abi.so will not link when modules are enabled because it depends
# on symbols defined in libc++.so which has not yet been built.
if (LLVM_ENABLE_MODULES)
  string(REPLACE "-Wl,-z,defs" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
endif()

include(WarningFlags)

# Build the shared library.
add_library(cxxabi_shared_objects OBJECT EXCLUDE_FROM_ALL ${LIBCXXABI_SOURCES} ${LIBCXXABI_HEADERS})
cxx_add_warning_flags(cxxabi_shared_objects ${LIBCXXABI_ENABLE_WERROR} ${LIBCXXABI_ENABLE_PEDANTIC})
if (LIBCXXABI_USE_LLVM_UNWINDER)
  if (LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY OR
      (DEFINED LIBUNWIND_ENABLE_SHARED AND NOT LIBUNWIND_ENABLE_SHARED))
    target_link_libraries(cxxabi_shared_objects PUBLIC unwind_shared_objects) # propagate usage requirements
    target_sources(cxxabi_shared_objects PUBLIC $<TARGET_OBJECTS:unwind_shared_objects>)
  else()
    target_link_libraries(cxxabi_shared_objects PUBLIC unwind_shared)
  endif()
endif()
target_link_libraries(cxxabi_shared_objects PRIVATE cxx-headers ${LIBCXXABI_LIBRARIES})
if (NOT CXX_SUPPORTS_NOSTDLIBXX_FLAG)
  target_link_libraries(cxxabi_shared_objects PRIVATE ${LIBCXXABI_BUILTINS_LIBRARY})
endif()
target_link_libraries(cxxabi_shared_objects PUBLIC cxxabi-headers)
set_target_properties(cxxabi_shared_objects
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED OFF # TODO: Make this REQUIRED once we don't need to accommodate the LLVM documentation builders using an ancient CMake
    COMPILE_FLAGS "${LIBCXXABI_COMPILE_FLAGS}"
    DEFINE_SYMBOL ""
)
if (CMAKE_POSITION_INDEPENDENT_CODE OR NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
  set_target_properties(cxxabi_shared_objects PROPERTIES POSITION_INDEPENDENT_CODE ON) # must set manually because it's an object library
endif()
target_compile_options(cxxabi_shared_objects PRIVATE "${LIBCXXABI_ADDITIONAL_COMPILE_FLAGS}")

if (LIBCXXABI_ENABLE_SHARED)
  add_library(cxxabi_shared SHARED)
  set_target_properties(cxxabi_shared
    PROPERTIES
      LINK_FLAGS "${LIBCXXABI_LINK_FLAGS}"
      OUTPUT_NAME "${LIBCXXABI_SHARED_OUTPUT_NAME}"
      SOVERSION "1"
      VERSION "${LIBCXXABI_LIBRARY_VERSION}"
  )

  if (ZOS)
    add_custom_command(TARGET cxxabi_shared POST_BUILD
      COMMAND
        ${LIBCXXABI_LIBCXX_PATH}/utils/zos_rename_dll_side_deck.sh
        $<TARGET_LINKER_FILE_NAME:cxxabi_shared> $<TARGET_FILE_NAME:cxxabi_shared> "${LIBCXXABI_DLL_NAME}"
      COMMENT "Rename dll name inside the side deck file"
      WORKING_DIRECTORY $<TARGET_FILE_DIR:cxxabi_shared>
    )
  endif ()

  target_link_libraries(cxxabi_shared
    PUBLIC cxxabi_shared_objects
    PRIVATE ${LIBCXXABI_LIBRARIES})

  list(APPEND LIBCXXABI_BUILD_TARGETS "cxxabi_shared")
  if (LIBCXXABI_INSTALL_SHARED_LIBRARY)
    list(APPEND LIBCXXABI_INSTALL_TARGETS "cxxabi_shared")
  endif()

  # TODO: Move this to libc++'s HandleLibCXXABI.cmake since this is effectively trying to control
  #       what libc++ re-exports.
  add_library(cxxabi-reexports INTERFACE)
  function(export_symbols file)
    # -exported_symbols_list is only available on Apple platforms
    if (APPLE)
      target_link_libraries(cxxabi_shared PRIVATE "-Wl,-exported_symbols_list,${file}")
    endif()
  endfunction()

  function(reexport_symbols file)
    export_symbols("${file}")
    # -reexported_symbols_list is only available on Apple platforms
    if (APPLE)
      target_link_libraries(cxxabi-reexports INTERFACE "-Wl,-reexported_symbols_list,${file}")
    endif()
  endfunction()

  export_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/symbols-not-reexported.exp")
  reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/cxxabiv1.exp")
  reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/fundamental-types.exp")
  reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/itanium-base.exp")
  reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/std-misc.exp")

  if (LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS)
    reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/new-delete.exp")
  endif()

  # Note that std:: exception types are always defined by the library regardless of
  # whether the exception runtime machinery is provided.
  reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/std-exceptions.exp")

  if (LIBCXXABI_ENABLE_EXCEPTIONS)
    reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/itanium-exceptions.exp")

    if ("${CMAKE_OSX_ARCHITECTURES}" MATCHES "^(armv6|armv7|armv7s)$")
      reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/personality-sjlj.exp")
    else()
      reexport_symbols("${CMAKE_CURRENT_SOURCE_DIR}/../lib/personality-v0.exp")
    endif()
  endif()
endif()

# Build the static library.
add_library(cxxabi_static_objects OBJECT EXCLUDE_FROM_ALL ${LIBCXXABI_SOURCES} ${LIBCXXABI_HEADERS})
cxx_add_warning_flags(cxxabi_static_objects ${LIBCXXABI_ENABLE_WERROR} ${LIBCXXABI_ENABLE_PEDANTIC})
if (LIBCXXABI_USE_LLVM_UNWINDER AND LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY)
  target_link_libraries(cxxabi_static_objects PUBLIC unwind_static_objects) # propagate usage requirements
  target_sources(cxxabi_static_objects PUBLIC $<TARGET_OBJECTS:unwind_static_objects>)
endif()
target_link_libraries(cxxabi_static_objects PRIVATE cxx-headers ${LIBCXXABI_STATIC_LIBRARIES} ${LIBCXXABI_LIBRARIES})
target_link_libraries(cxxabi_static_objects PUBLIC cxxabi-headers)
set_target_properties(cxxabi_static_objects
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED OFF # TODO: Make this REQUIRED once we don't need to accommodate the LLVM documentation builders using an ancient CMake
    COMPILE_FLAGS "${LIBCXXABI_COMPILE_FLAGS}"
)
target_compile_options(cxxabi_static_objects PRIVATE "${LIBCXXABI_ADDITIONAL_COMPILE_FLAGS}")

if(LIBCXXABI_HERMETIC_STATIC_LIBRARY)
  target_add_compile_flags_if_supported(cxxabi_static_objects PRIVATE -fvisibility=hidden)
  # If the hermetic library doesn't define the operator new/delete functions
  # then its code shouldn't declare them with hidden visibility.  They might
  # actually be provided by a shared library at link time.
  if (LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS)
    target_add_compile_flags_if_supported(cxxabi_static_objects PRIVATE -fvisibility-global-new-delete=force-hidden)
    if (NOT CXX_SUPPORTS_FVISIBILITY_GLOBAL_NEW_DELETE_EQ_FORCE_HIDDEN_FLAG)
      target_add_compile_flags_if_supported(cxxabi_static_objects PRIVATE -fvisibility-global-new-delete-hidden)
    endif()
  endif()
  # _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS can be defined in libcxx's
  # __config_site too. Define it in the same way here, to avoid redefinition
  # conflicts.
  target_compile_definitions(cxxabi_static_objects
    PRIVATE
      _LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS
      _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS=)
endif()

if (LIBCXXABI_ENABLE_STATIC)
  add_library(cxxabi_static STATIC)
  if (LIBCXXABI_USE_LLVM_UNWINDER AND NOT LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY)
    target_link_libraries(cxxabi_static PUBLIC unwind_static)
  endif()
  set_target_properties(cxxabi_static
    PROPERTIES
      LINK_FLAGS "${LIBCXXABI_LINK_FLAGS}"
      OUTPUT_NAME "${LIBCXXABI_STATIC_OUTPUT_NAME}"
    )
  target_link_libraries(cxxabi_static
    PUBLIC cxxabi_static_objects
    PRIVATE ${LIBCXXABI_STATIC_LIBRARIES} ${LIBCXXABI_LIBRARIES})

  list(APPEND LIBCXXABI_BUILD_TARGETS "cxxabi_static")
  if (LIBCXXABI_INSTALL_STATIC_LIBRARY)
    list(APPEND LIBCXXABI_INSTALL_TARGETS "cxxabi_static")
  endif()
endif()

# Add a meta-target for both libraries.
add_custom_target(cxxabi DEPENDS ${LIBCXXABI_BUILD_TARGETS})
add_dependencies(cxxabi-test-depends cxxabi cxx)

if (LIBCXXABI_INSTALL_LIBRARY)
  install(TARGETS ${LIBCXXABI_INSTALL_TARGETS}
    LIBRARY DESTINATION ${LIBCXXABI_INSTALL_LIBRARY_DIR} COMPONENT cxxabi
    ARCHIVE DESTINATION ${LIBCXXABI_INSTALL_LIBRARY_DIR} COMPONENT cxxabi
    RUNTIME DESTINATION ${LIBCXXABI_INSTALL_RUNTIME_DIR} COMPONENT cxxabi
    )
endif()

if (NOT CMAKE_CONFIGURATION_TYPES AND LIBCXXABI_INSTALL_LIBRARY)
  add_custom_target(install-cxxabi
    DEPENDS cxxabi install-cxxabi-headers
    COMMAND "${CMAKE_COMMAND}"
            -DCMAKE_INSTALL_COMPONENT=cxxabi
            -P "${LIBCXXABI_BINARY_DIR}/cmake_install.cmake")
  add_custom_target(install-cxxabi-stripped
    DEPENDS cxxabi
    COMMAND "${CMAKE_COMMAND}"
            -DCMAKE_INSTALL_COMPONENT=cxxabi
            -DCMAKE_INSTALL_DO_STRIP=1
            -P "${LIBCXXABI_BINARY_DIR}/cmake_install.cmake")
endif()
