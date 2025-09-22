#.rst:
# FindPythonAndSwig
# -----------
#
# Find the python interpreter and libraries as a whole.

macro(FindPython3)
  # Use PYTHON_HOME as a hint to find Python 3.
  set(Python3_ROOT_DIR "${PYTHON_HOME}")
  find_package(Python3 COMPONENTS Interpreter Development)
  if(Python3_FOUND AND Python3_Interpreter_FOUND)

    # The install name for the Python 3 framework in Xcode is relative to
    # the framework's location and not the dylib itself.
    #
    #   @rpath/Python3.framework/Versions/3.x/Python3
    #
    # This means that we need to compute the path to the Python3.framework
    # and use that as the RPATH instead of the usual dylib's directory.
    #
    # The check below shouldn't match Homebrew's Python framework as it is
    # called Python.framework instead of Python3.framework.
    if (APPLE AND Python3_LIBRARIES MATCHES "Python3.framework")
      string(FIND "${Python3_LIBRARIES}" "Python3.framework" python_framework_pos)
      string(SUBSTRING "${Python3_LIBRARIES}" "0" ${python_framework_pos} Python3_RPATH)
    endif()

    set(PYTHON3_FOUND TRUE)
    mark_as_advanced(
      Python3_LIBRARIES
      Python3_INCLUDE_DIRS
      Python3_EXECUTABLE
      Python3_RPATH)
  endif()
endmacro()

if(Python3_LIBRARIES AND Python3_INCLUDE_DIRS AND Python3_EXECUTABLE AND LLDB_ENABLE_SWIG)
  set(PYTHONANDSWIG_FOUND TRUE)
else()
  if (LLDB_ENABLE_SWIG)
    FindPython3()
  else()
    message(STATUS "SWIG 4 or later is required for Python support in LLDB but could not be found")
  endif()

  get_property(MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if ("${Python3_VERSION}" VERSION_GREATER_EQUAL "3.7" AND
      "${SWIG_VERSION}" VERSION_LESS "4.0" AND WIN32 AND (
      ${MULTI_CONFIG} OR (uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")))
    # Technically this can happen with non-Windows builds too, but we are not
    # able to detect whether Python was built with assertions, and only Windows
    # has the requirement that Debug LLDB must use Debug Python.
    message(WARNING "Debug builds of LLDB are likely to be unusable due to "
      "<https://github.com/swig/swig/issues/1321>. Please use SWIG >= 4.0.")
  endif()

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(PythonAndSwig
                                    FOUND_VAR
                                      PYTHONANDSWIG_FOUND
                                    REQUIRED_VARS
                                      Python3_LIBRARIES
                                      Python3_INCLUDE_DIRS
                                      Python3_EXECUTABLE
                                      LLDB_ENABLE_SWIG)
endif()
