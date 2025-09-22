include(${CMAKE_CURRENT_LIST_DIR}/Apple-lldb-base.cmake)
set(LLVM_ENABLE_EXPORTED_SYMBOLS_IN_EXECUTABLES ON CACHE BOOL "" FORCE)

set(LLVM_DISTRIBUTION_COMPONENTS
  lldb
  liblldb
  lldb-argdumper
  lldb-dap
  lldb-server
  lldb-python-scripts
  CACHE STRING "")
