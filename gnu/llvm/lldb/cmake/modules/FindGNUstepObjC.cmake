#.rst:
# FindGNUstepObjC
# ---------------
#
# Find the GNUstep libobjc2 shared library.

set(gnustep_install_dir "")

if (UNIX)
  set(gnustep_lib lib/libobjc.so)
  set(gnustep_header include/objc/runtime.h)
  if (GNUstepObjC_DIR)
    if (EXISTS "${GNUstepObjC_DIR}/${gnustep_lib}" AND
        EXISTS "${GNUstepObjC_DIR}/${gnustep_header}")
      set(gnustep_install_dir ${GNUstepObjC_DIR})
    endif()
  else()
    set(gnustep_install_dir)
    find_path(gnustep_install_dir NAMES lib/libobjc.so include/objc/runtime.h)
  endif()
  if (gnustep_install_dir)
    set(GNUstepObjC_FOUND TRUE)
  endif()
elseif (WIN32)
  set(gnustep_lib lib/objc.dll)
  set(gnustep_header include/objc/runtime.h)
  if (GNUstepObjC_DIR)
    set(gnustep_install_dir ${GNUstepObjC_DIR})
  else()
    set(gnustep_install_dir "C:/Program Files (x86)/libobjc")
  endif()
  if (EXISTS "${gnustep_install_dir}/${gnustep_lib}" AND
      EXISTS "${gnustep_install_dir}/${gnustep_header}")
    set(GNUstepObjC_FOUND TRUE)
  endif()
endif()

if (GNUstepObjC_FOUND)
  set(GNUstepObjC_DIR ${gnustep_install_dir})
  message(STATUS "Found GNUstep ObjC runtime: ${GNUstepObjC_DIR}/${gnustep_lib}")
endif()
