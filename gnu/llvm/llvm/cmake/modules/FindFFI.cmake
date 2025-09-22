# Attempts to discover ffi library with a linkable ffi_call function.
#
# Example usage:
#
# find_package(FFI)
#
# FFI_REQUIRE_INCLUDE may be set to consider ffi found if the includes
# are present in addition to the library. This is useful to keep off
# for the imported package on platforms that install the library but
# not the headers.
#
# FFI_LIBRARY_DIR may be set to define search paths for the ffi library.
#
# If successful, the following variables will be defined:
# FFI_FOUND
# FFI_INCLUDE_DIRS
# FFI_LIBRARIES
# FFI_STATIC_LIBRARIES
# HAVE_FFI_CALL
#
# HAVE_FFI_H or HAVE_FFI_FFI_H is defined depending on the ffi.h include path.
#
# Additionally, the following import target will be defined:
# FFI::ffi

find_path(FFI_INCLUDE_DIRS ffi.h PATHS ${FFI_INCLUDE_DIR})
if( EXISTS "${FFI_INCLUDE_DIRS}/ffi.h" )
  set(FFI_HEADER ffi.h CACHE INTERNAL "")
  set(HAVE_FFI_H 1 CACHE INTERNAL "")
else()
  find_path(FFI_INCLUDE_DIRS ffi/ffi.h PATHS ${FFI_INCLUDE_DIR})
  if( EXISTS "${FFI_INCLUDE_DIRS}/ffi/ffi.h" )
    set(FFI_HEADER ffi/ffi.h CACHE INTERNAL "")
    set(HAVE_FFI_FFI_H 1 CACHE INTERNAL "")
  endif()
endif()

find_library(FFI_LIBRARIES NAMES ffi PATHS ${FFI_LIBRARY_DIR})
find_library(FFI_STATIC_LIBRARIES NAMES libffi.a PATHS ${FFI_LIBRARY_DIR})

if(FFI_LIBRARIES)
  include(CMakePushCheckState)
  cmake_push_check_state()
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${FFI_LIBRARIES})
  set(HAVE_FFI_CALL_SRC [=[
    #ifdef __cplusplus
    extern "C" {
    #endif
    struct ffi_cif;
    typedef struct ffi_cif ffi_cif;
    void ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue);
    #ifdef __cplusplus
    }
    #endif
    int main(void) { ffi_call(0, 0, 0, 0); }
    ]=])
  if(DEFINED CMAKE_C_COMPILER)
    include(CheckCSourceCompiles)
    check_c_source_compiles("${HAVE_FFI_CALL_SRC}" HAVE_FFI_CALL)
  else()
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("${HAVE_FFI_CALL_SRC}" HAVE_FFI_CALL)
  endif()
  cmake_pop_check_state()
endif()

unset(required_includes)
if(FFI_REQUIRE_INCLUDE)
  set(required_includes FFI_INCLUDE_DIRS)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFI
                                  FOUND_VAR
                                    FFI_FOUND
                                  REQUIRED_VARS
                                    FFI_LIBRARIES
                                    ${required_includes}
                                    HAVE_FFI_CALL)
mark_as_advanced(FFI_LIBRARIES
                 FFI_STATIC_LIBRARIES
                 FFI_INCLUDE_DIRS
                 HAVE_FFI_CALL
                 FFI_HEADER
                 HAVE_FFI_H
                 HAVE_FFI_FFI_H)

if(FFI_FOUND)
  if(NOT TARGET FFI::ffi AND FFI_LIBRARIES)
    add_library(FFI::ffi UNKNOWN IMPORTED)
    set_target_properties(FFI::ffi PROPERTIES IMPORTED_LOCATION "${FFI_LIBRARIES}")
    if(FFI_INCLUDE_DIRS)
      set_target_properties(FFI::ffi PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${FFI_INCLUDE_DIRS}")
    endif()
  endif()
  if(NOT TARGET FFI::ffi_static AND FFI_STATIC_LIBRARIES)
    add_library(FFI::ffi_static UNKNOWN IMPORTED)
    set_target_properties(FFI::ffi_static PROPERTIES IMPORTED_LOCATION "${FFI_STATIC_LIBRARIES}")
    if(FFI_INCLUDE_DIRS)
      set_target_properties(FFI::ffi_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${FFI_INCLUDE_DIRS}")
    endif()
  endif()
endif()
