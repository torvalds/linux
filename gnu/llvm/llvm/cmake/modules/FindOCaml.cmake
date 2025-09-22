# CMake find_package() module for the OCaml language.
# Assumes ocamlfind will be used for compilation.
# http://ocaml.org/
#
# Example usage:
#
# find_package(OCaml)
#
# If successful, the following variables will be defined:
# OCAMLFIND
# OCAML_VERSION
# OCAML_STDLIB_PATH
# HAVE_OCAMLOPT
#
# Also provides find_ocamlfind_package() macro.
#
# Example usage:
#
# find_ocamlfind_package(ctypes)
#
# In any case, the following variables are defined:
#
# HAVE_OCAML_${pkg}
#
# If successful, the following variables will be defined:
#
# OCAML_${pkg}_VERSION

include( FindPackageHandleStandardArgs )

find_program(OCAMLFIND
             NAMES ocamlfind)

if( OCAMLFIND )
  execute_process(
    COMMAND ${OCAMLFIND} ocamlc -version
    OUTPUT_VARIABLE OCAML_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  execute_process(
    COMMAND ${OCAMLFIND} ocamlc -where
    OUTPUT_VARIABLE OCAML_STDLIB_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  execute_process(
    COMMAND ${OCAMLFIND} ocamlc -version
    OUTPUT_QUIET
    RESULT_VARIABLE find_ocaml_result)
  if( find_ocaml_result EQUAL 0 )
    set(HAVE_OCAMLOPT TRUE)
  else()
    set(HAVE_OCAMLOPT FALSE)
  endif()
endif()

find_package_handle_standard_args( OCaml DEFAULT_MSG
  OCAMLFIND
  OCAML_VERSION
  OCAML_STDLIB_PATH)

mark_as_advanced(
  OCAMLFIND)

function(find_ocamlfind_package pkg)
  CMAKE_PARSE_ARGUMENTS(ARG "OPTIONAL" "VERSION" "" ${ARGN})

  execute_process(
    COMMAND "${OCAMLFIND}" "query" "${pkg}" "-format" "%v"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE version
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if( NOT result EQUAL 0 AND NOT ARG_OPTIONAL )
    message(FATAL_ERROR ${error})
  endif()

  if( result EQUAL 0 )
    set(found TRUE)
  else()
    set(found FALSE)
  endif()

  if( found AND ARG_VERSION )
    if( version VERSION_LESS ARG_VERSION AND ARG_OPTIONAL )
      # If it's optional and the constraint is not satisfied, pretend
      # it wasn't found.
      set(found FALSE)
    elseif( version VERSION_LESS ARG_VERSION )
      message(FATAL_ERROR
              "ocamlfind package ${pkg} should have version ${ARG_VERSION} or newer")
    endif()
  endif()

  string(TOUPPER ${pkg} pkg)

  set(HAVE_OCAML_${pkg} ${found}
      PARENT_SCOPE)

  set(OCAML_${pkg}_VERSION ${version}
      PARENT_SCOPE)
endfunction()
