# The SOVERSION should be updated only if a change is made to the libclang
# ABI, and when it is updated, it should be updated to the current
# LLVM_VERSION_MAJOR.
# Please also see clang/tools/libclang/libclang.map

# This option defaults to CLANG_FORCE_MATCHING_LIBCLANG_SOVERSION
# to ON - which means that it by default matches CLANG_VERSION_MAJOR
#
# TODO: This should probably not be a option going forward but we
# we should commit to a way to do it. But due to getting this out
# in LLVM 15.x we opted for a option.
set(LIBCLANG_SOVERSION_ARG)
if(NOT CLANG_FORCE_MATCHING_LIBCLANG_SOVERSION)
  set(LIBCLANG_SOVERSION_ARG SOVERSION 13)
endif()

# TODO: harmonize usage of LIBCLANG_SOVERSION / LIBCLANG_LIBARY_VERSION
#       below; this was added under time-pressure to avoid reverting the
#       better default from LLVM 14 for LLVM 15.0.0-rc3, hence no time
#       to clean up previous inconsistencies.

set(SOURCES
  ARCMigrate.cpp
  BuildSystem.cpp
  CIndex.cpp
  CIndexCXX.cpp
  CIndexCodeCompletion.cpp
  CIndexDiagnostic.cpp
  CIndexHigh.cpp
  CIndexInclusionStack.cpp
  CIndexUSRs.cpp
  CIndexer.cpp
  CXComment.cpp
  CXCursor.cpp
  CXExtractAPI.cpp
  CXIndexDataConsumer.cpp
  CXCompilationDatabase.cpp
  CXLoadedDiagnostic.cpp
  CXSourceLocation.cpp
  CXStoredDiagnostic.cpp
  CXString.cpp
  CXType.cpp
  Indexing.cpp
  FatalErrorHandler.cpp
  Rewrite.cpp

  ADDITIONAL_HEADERS
  CIndexDiagnostic.h
  CIndexer.h
  CXCursor.h
  CXLoadedDiagnostic.h
  CXSourceLocation.h
  CXString.h
  CXTranslationUnit.h
  CXType.h
  Index_Internal.h
  ../../include/clang-c/Index.h
  )

set(LIBS
  clangAST
  clangBasic
  clangDriver
  clangExtractAPI
  clangFrontend
  clangIndex
  clangLex
  clangRewrite
  clangSema
  clangSerialization
  clangTooling
)

if (CLANG_ENABLE_ARCMT)
  list(APPEND LIBS clangARCMigrate)
endif ()

if (HAVE_LIBDL)
  list(APPEND LIBS ${CMAKE_DL_LIBS})
elseif (CLANG_BUILT_STANDALONE)
  find_library(DL_LIBRARY_PATH dl)
  if (DL_LIBRARY_PATH)
    list(APPEND LIBS dl)
  endif ()
endif ()

option(LIBCLANG_BUILD_STATIC
  "Build libclang as a static library (in addition to a shared one)" OFF)

set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_BINARY_DIR}/libclang-generic.exports)
set(LIBCLANG_VERSION_SCRIPT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/libclang.map)

if(MSVC)
  # Avoid LNK4197 by not specifying libclang.exports here.
  # Each functions is exported as "dllexport" in include/clang-c.
  # KB835326
  set(LLVM_EXPORTED_SYMBOL_FILE)
endif()

if (UNIX AND NOT APPLE AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  set(LLVM_EXPORTED_SYMBOL_FILE)
  set(USE_VERSION_SCRIPT ${LLVM_HAVE_LINK_VERSION_SCRIPT})
endif()

if (LLVM_EXPORTED_SYMBOL_FILE)
  add_custom_command(OUTPUT ${LLVM_EXPORTED_SYMBOL_FILE}
                     COMMAND "${Python3_EXECUTABLE}"
                       ARGS ${CMAKE_CURRENT_SOURCE_DIR}/linker-script-to-export-list.py
                            ${LIBCLANG_VERSION_SCRIPT_FILE}
                            ${LLVM_EXPORTED_SYMBOL_FILE}
                     DEPENDS ${LIBCLANG_VERSION_SCRIPT_FILE})
endif()

if(LLVM_ENABLE_PIC OR (WIN32 AND NOT LIBCLANG_BUILD_STATIC))
  set(ENABLE_SHARED SHARED)
endif()

if(NOT LLVM_ENABLE_PIC OR LIBCLANG_BUILD_STATIC)
  set(ENABLE_STATIC STATIC)
endif()

if (MSVC AND ENABLE_SHARED AND ENABLE_STATIC)
  unset(ENABLE_STATIC)
endif()

if(MSVC)
  set(output_name "libclang")
else()
  set(output_name "clang")
endif()

if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
    set(CMAKE_AIX_EXPORT_ALL_SYMBOLS OFF)
    # libclang requires headers which need _ALL_SOURCE to build on AIX
    remove_definitions("-D_XOPEN_SOURCE=700")
endif()

add_clang_library(libclang ${ENABLE_SHARED} ${ENABLE_STATIC} INSTALL_WITH_TOOLCHAIN
  OUTPUT_NAME ${output_name}
  ${SOURCES}

  DEPENDS
  ClangDriverOptions
  clang-resource-headers

  LINK_LIBS
  ${LIBS}

  LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Core
  Support
  TargetParser
  )

if(ENABLE_STATIC)
  foreach(name libclang obj.libclang libclang_static)
    if (TARGET ${name})
      target_compile_definitions(${name} PUBLIC CINDEX_NO_EXPORTS)
    endif()
  endforeach()
endif()

if(ENABLE_SHARED)
  if(WIN32)
    set_target_properties(libclang
      PROPERTIES
      VERSION ${LIBCLANG_LIBRARY_VERSION}
      DEFINE_SYMBOL _CINDEX_LIB_)
  elseif(APPLE)
    set(LIBCLANG_LINK_FLAGS " -Wl,-compatibility_version -Wl,1")
    set(LIBCLANG_LINK_FLAGS "${LIBCLANG_LINK_FLAGS} -Wl,-current_version -Wl,${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}")

    set_property(TARGET libclang APPEND_STRING PROPERTY
                 LINK_FLAGS ${LIBCLANG_LINK_FLAGS})
  else()
    set_target_properties(libclang
      PROPERTIES
      VERSION ${LIBCLANG_LIBRARY_VERSION}
      DEFINE_SYMBOL _CINDEX_LIB_)
    # FIXME: _CINDEX_LIB_ affects dllexport/dllimport on Win32.
    if(LLVM_ENABLE_MODULES AND NOT WIN32)
      target_compile_options(libclang PRIVATE
        "-fmodules-ignore-macro=_CINDEX_LIB_"
        )
    endif()
  endif()
  if (USE_VERSION_SCRIPT)
    if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
      include(CheckLinkerFlag)
      # The Solaris 11.4 linker supports a subset of GNU ld version scripts,
      # but requires a special option to enable it.
      llvm_check_linker_flag(CXX "-Wl,-z,gnu-version-script-compat"
                             LINKER_SUPPORTS_Z_GNU_VERSION_SCRIPT_COMPAT)
      # Older Solaris (and illumos) linker does not support GNU ld version scripts
      # and does not support GNU version script compat.
      if (LINKER_SUPPORTS_Z_GNU_VERSION_SCRIPT_COMPAT)
        target_link_options(libclang PRIVATE "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/libclang.map")
        target_link_options(libclang PRIVATE "-Wl,-z,gnu-version-script-compat")
      else()
        target_link_options(libclang PRIVATE "-Wl,-M,${CMAKE_CURRENT_SOURCE_DIR}/libclang.map")
      endif()
    else()
      target_link_options(libclang PRIVATE "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/libclang.map")
    endif()
    # Ensure that libclang.so gets rebuilt when the linker script changes.
    set_property(SOURCE ARCMigrate.cpp APPEND PROPERTY
                 OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libclang.map)

    set_target_properties(libclang PROPERTIES
                          VERSION ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}${LLVM_VERSION_SUFFIX}
                          ${LIBCLANG_SOVERSION_ARG})
  endif()
endif()

if(INTERNAL_INSTALL_PREFIX)
  set(LIBCLANG_HEADERS_INSTALL_DESTINATION "${INTERNAL_INSTALL_PREFIX}/include")
else()
  set(LIBCLANG_HEADERS_INSTALL_DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()

install(DIRECTORY ../../include/clang-c
  COMPONENT libclang-headers
  DESTINATION "${LIBCLANG_HEADERS_INSTALL_DESTINATION}"
  FILES_MATCHING
  PATTERN "*.h"
  )

# LLVM_DISTRIBUTION_COMPONENTS requires that each component have both a
# component and an install-component target, so add a dummy libclang-headers
# target to allow using it in LLVM_DISTRIBUTION_COMPONENTS.
add_custom_target(libclang-headers)
set_target_properties(libclang-headers PROPERTIES FOLDER "Clang/Resources")

if (NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-libclang-headers
                           COMPONENT libclang-headers)
endif()

# Create a target to install the python bindings to make them easier to
# distribute.  Since the bindings are over libclang, which is installed
# unbundled to the clang version, follow suit.
foreach(PythonVersion ${CLANG_PYTHON_BINDINGS_VERSIONS})
  install(DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}/../../bindings/python/clang
          COMPONENT
            libclang-python-bindings
          DESTINATION
            "lib${LLVM_LIBDIR_SUFFIX}/python${PythonVersion}/site-packages")
endforeach()
if(NOT LLVM_ENABLE_IDE)
  add_custom_target(libclang-python-bindings)
  add_llvm_install_targets(install-libclang-python-bindings
                           COMPONENT
                             libclang-python-bindings)
endif()

