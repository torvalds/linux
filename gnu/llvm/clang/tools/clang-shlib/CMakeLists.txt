# Building libclang-cpp.so fails if LLVM_ENABLE_PIC=Off
if (NOT LLVM_ENABLE_PIC)
  return()
endif()

get_property(clang_libs GLOBAL PROPERTY CLANG_STATIC_LIBS)

foreach (lib ${clang_libs})
  if(XCODE)
    # Xcode doesn't support object libraries, so we have to trick it into
    # linking the static libraries instead.
    list(APPEND _DEPS "-force_load" ${lib})
  else()
    list(APPEND _OBJECTS $<TARGET_OBJECTS:obj.${lib}>)
  endif()
  if (BUILD_SHARED_LIBS)
    # If we are building static libraries, then we don't need to add the static
    # libraries as a dependency, because we are already linking against the
    # individual object files.
    list(APPEND _DEPS $<TARGET_PROPERTY:${lib},INTERFACE_LINK_LIBRARIES>)
  endif()

  # clang libraries are redundant since we are linking all the individual
  # object files into libclang-cpp.so, so filter them out from _DEPS.
  # This avoids problems with LLVM global data when building with
  # BUILD_SHARED_LIBS=ON
  # FIXME: We could use list(FILTER) with cmake >= 3.6
  # FIXME: With cmake >= 3.15 we could use the generator expression
  # $<FILTER:list,INCLUDE|EXCLUDE,regex>
  get_target_property(interface ${lib} LINK_LIBRARIES)
  if (interface)
    foreach(lib ${interface})
      if (NOT ${lib} MATCHES "^clang")
        list(APPEND _DEPS ${lib})
      endif()
    endforeach()
  endif()
endforeach ()

if (CLANG_LINK_CLANG_DYLIB)
  set(INSTALL_WITH_TOOLCHAIN INSTALL_WITH_TOOLCHAIN)
endif()

add_clang_library(clang-cpp
                  SHARED
                  ${INSTALL_WITH_TOOLCHAIN}
                  clang-shlib.cpp
                  ${_OBJECTS}
                  LINK_LIBS
                  ${_DEPS})
# Optimize function calls for default visibility definitions to avoid PLT and
# reduce dynamic relocations.
if (NOT APPLE AND NOT MINGW AND NOT LLVM_LINKER_IS_SOLARISLD_ILLUMOS AND ${OPENBSD_LD_IS_LLD})
  target_link_options(clang-cpp PRIVATE LINKER:-Bsymbolic-functions)
endif()
if (MINGW OR CYGWIN)
  # The clang-cpp DLL is supposed to export all symbols (except for ones
  # that are explicitly hidden). Normally, this is what happens anyway, but
  # if there are symbols that are marked explicitly as dllexport, we'd only
  # export them and nothing else. Therefore, add --export-all-symbols to
  # make sure we export all symbols despite potential dllexports.
  target_link_options(clang-cpp PRIVATE LINKER:--export-all-symbols)
endif()
