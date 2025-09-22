include(${CMAKE_CURRENT_LIST_DIR}/Apple-lldb-base.cmake)

set(LLDB_BUILD_FRAMEWORK ON CACHE BOOL "")
set(LLDB_NO_INSTALL_DEFAULT_RPATH ON CACHE BOOL "")
set(LLDB_SKIP_STRIP ON CACHE BOOL "")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.11 CACHE STRING "")

# Default install location on the enduser machine. On the build machine, use the
# DESTDIR environment variable in order to relocate the whole installation, e.g.:
# `DESTDIR=/tmp ninja install-distribution`
set(CMAKE_INSTALL_PREFIX /Applications/Xcode.app/Contents/Developer/usr CACHE STRING "")

# Install location for LLDB.framework on the enduser machine.
# DESTDIR will be an extra prefix.
set(LLDB_FRAMEWORK_INSTALL_DIR /Applications/Xcode.app/Contents/SharedFrameworks CACHE STRING "")

# Install location for externalized debug-info on the build machine.
# DESTDIR will be an extra prefix.
set(LLDB_DEBUGINFO_INSTALL_PREFIX /debuginfo CACHE STRING "")

set(LLVM_DISTRIBUTION_COMPONENTS
  lldb
  liblldb
  lldb-argdumper
  lldb-dap
  darwin-debug
  debugserver
  CACHE STRING "")
