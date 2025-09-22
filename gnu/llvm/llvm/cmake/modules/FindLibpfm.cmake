# CMake module for finding libpfm4.
#
# If successful, the following variables will be defined:
# HAVE_LIBPFM
#
# Libpfm can be disabled by setting LLVM_ENABLE_LIBPFM to 0.

include(CheckIncludeFile)
include(CheckLibraryExists)
include(CheckCXXSourceCompiles)

if (LLVM_ENABLE_LIBPFM)
  check_library_exists(pfm pfm_initialize "" HAVE_LIBPFM_INITIALIZE)
  if(HAVE_LIBPFM_INITIALIZE)
    check_include_file(perfmon/perf_event.h HAVE_PERFMON_PERF_EVENT_H)
    check_include_file(perfmon/pfmlib.h HAVE_PERFMON_PFMLIB_H)
    check_include_file(perfmon/pfmlib_perf_event.h HAVE_PERFMON_PFMLIB_PERF_EVENT_H)
    if(HAVE_PERFMON_PERF_EVENT_H AND HAVE_PERFMON_PFMLIB_H AND HAVE_PERFMON_PFMLIB_PERF_EVENT_H)
      set(HAVE_LIBPFM 1)
      # Check to see if perf_branch_entry has the field 'cycles'.
      # We couldn't use CheckStructHasMember here because 'cycles' is a bit field which is not
      # supported by CheckStructHasMember.
      CHECK_CXX_SOURCE_COMPILES("
        #include <perfmon/perf_event.h>
        int main() {
          perf_branch_entry entry;
          entry.cycles = 2;
          return 0;
        }" COMPILE_WITH_CYCLES)
      if(COMPILE_WITH_CYCLES)
        set(LIBPFM_HAS_FIELD_CYCLES 1)
      endif()
    endif()
  endif()
endif()


