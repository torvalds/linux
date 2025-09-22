#.rst:
# FindLibEdit
# -----------
#
# Find libedit library and headers
#
# The module defines the following variables:
#
# ::
#
#   LibEdit_FOUND          - true if libedit was found
#   LibEdit_INCLUDE_DIRS   - include search path
#   LibEdit_LIBRARIES      - libraries to link
#   LibEdit_VERSION_STRING - version number

find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBEDIT QUIET libedit)

find_path(LibEdit_INCLUDE_DIRS NAMES histedit.h HINTS ${PC_LIBEDIT_INCLUDE_DIRS})
find_library(LibEdit_LIBRARIES NAMES edit HINTS ${PC_LIBEDIT_LIBRARY_DIRS})

include(CheckIncludeFile)
if(LibEdit_INCLUDE_DIRS AND EXISTS "${LibEdit_INCLUDE_DIRS}/histedit.h")
  include(CMakePushCheckState)
  cmake_push_check_state()
  list(APPEND CMAKE_REQUIRED_INCLUDES ${LibEdit_INCLUDE_DIRS})
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${LibEdit_LIBRARIES})
  check_include_file(histedit.h HAVE_HISTEDIT_H)
  cmake_pop_check_state()
  if (HAVE_HISTEDIT_H)
    file(STRINGS "${LibEdit_INCLUDE_DIRS}/histedit.h"
          libedit_major_version_str
          REGEX "^#define[ \t]+LIBEDIT_MAJOR[ \t]+[0-9]+")
    string(REGEX REPLACE "^#define[ \t]+LIBEDIT_MAJOR[ \t]+([0-9]+)" "\\1"
            libedit_major_version "${libedit_major_version_str}")

    file(STRINGS "${LibEdit_INCLUDE_DIRS}/histedit.h"
          libedit_minor_version_str
          REGEX "^#define[ \t]+LIBEDIT_MINOR[ \t]+[0-9]+")
    string(REGEX REPLACE "^#define[ \t]+LIBEDIT_MINOR[ \t]+([0-9]+)" "\\1"
            libedit_minor_version "${libedit_minor_version_str}")

    set(LibEdit_VERSION_STRING "${libedit_major_version}.${libedit_minor_version}")
  else()
    set(LibEdit_INCLUDE_DIRS "")
    set(LibEdit_LIBRARIES "")
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibEdit
                                  FOUND_VAR
                                    LibEdit_FOUND
                                  REQUIRED_VARS
                                    LibEdit_INCLUDE_DIRS
                                    LibEdit_LIBRARIES
                                  VERSION_VAR
                                    LibEdit_VERSION_STRING)
mark_as_advanced(LibEdit_INCLUDE_DIRS LibEdit_LIBRARIES)

if (LibEdit_FOUND AND NOT TARGET LibEdit::LibEdit)
  add_library(LibEdit::LibEdit INTERFACE IMPORTED)
  target_link_libraries(LibEdit::LibEdit INTERFACE ${LibEdit_LIBRARIES})
  target_include_directories(LibEdit::LibEdit INTERFACE ${LibEdit_INCLUDE_DIRS})
endif()
