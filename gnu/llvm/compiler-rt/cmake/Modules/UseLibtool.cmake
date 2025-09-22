# if CMAKE_LIBTOOL is not set, try and find it with xcrun or find_program
if(NOT CMAKE_LIBTOOL)
  if(NOT CMAKE_XCRUN)
    find_program(CMAKE_XCRUN NAMES xcrun)
  endif()
  if(CMAKE_XCRUN)
    execute_process(COMMAND ${CMAKE_XCRUN} -find libtool
      OUTPUT_VARIABLE CMAKE_LIBTOOL
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()

  if(NOT CMAKE_LIBTOOL OR NOT EXISTS CMAKE_LIBTOOL)
    find_program(CMAKE_LIBTOOL NAMES libtool)
  endif()
endif()

get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
if(CMAKE_LIBTOOL)
  set(CMAKE_LIBTOOL ${CMAKE_LIBTOOL} CACHE PATH "libtool executable")
  message(STATUS "Found libtool - ${CMAKE_LIBTOOL}")

  execute_process(COMMAND ${CMAKE_LIBTOOL} -V
    OUTPUT_VARIABLE LIBTOOL_V_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if("${LIBTOOL_V_OUTPUT}" MATCHES ".*cctools-([0-9.]+).*")
    string(REGEX REPLACE ".*cctools-([0-9.]+).*" "\\1" LIBTOOL_VERSION
      ${LIBTOOL_V_OUTPUT})
    if(NOT LIBTOOL_VERSION VERSION_LESS "862")
      set(LIBTOOL_NO_WARNING_FLAG "-no_warning_for_no_symbols")
    endif()
  endif()

  foreach(lang ${languages})
    set(CMAKE_${lang}_CREATE_STATIC_LIBRARY
      "\"${CMAKE_LIBTOOL}\" -static ${LIBTOOL_NO_WARNING_FLAG} -o <TARGET> <LINK_FLAGS> <OBJECTS>")
  endforeach()

  # By default, CMake invokes ranlib on a static library after installing it.
  # libtool will have produced the table of contents for us already, and ranlib
  # does not understanding universal binaries, so skip this step. It's important
  # to set it to empty instead of unsetting it to shadow the cache variable, and
  # we don't want to unset the cache variable to not affect anything outside
  # this scope.
  set(CMAKE_RANLIB "")
endif()

# If DYLD_LIBRARY_PATH is set we need to set it on archiver commands
if(DYLD_LIBRARY_PATH)
  set(dyld_envar "DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}")
  foreach(lang ${languages})
    foreach(cmd ${CMAKE_${lang}_CREATE_STATIC_LIBRARY})
      list(APPEND CMAKE_${lang}_CREATE_STATIC_LIBRARY_NEW
           "${dyld_envar} ${cmd}")
    endforeach()
    set(CMAKE_${lang}_CREATE_STATIC_LIBRARY
      ${CMAKE_${lang}_CREATE_STATIC_LIBRARY_NEW})
  endforeach()
endif()
