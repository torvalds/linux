include(CMakePushCheckState)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(LLVMCheckCompilerLinkerFlag)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckCSourceCompiles)

# The compiler driver may be implicitly trying to link against libunwind.
# This is normally ok (libcxx relies on an unwinder), but if libunwind is
# built in the same cmake invocation as libcxx and we've got
# LIBCXXABI_USE_LLVM_UNWINDER set, we'd be linking against the just-built
# libunwind (and the compiler implicit -lunwind wouldn't succeed as the newly
# built libunwind isn't installed yet). For those cases, it'd be good to
# link with --uwnindlib=none. Check if that option works.
llvm_check_compiler_linker_flag(C "--unwindlib=none" CXX_SUPPORTS_UNWINDLIB_EQ_NONE_FLAG)

if (NOT LIBCXX_USE_COMPILER_RT)
  if(WIN32 AND NOT MINGW)
    set(LIBCXX_HAS_GCC_S_LIB NO)
  else()
    if(ANDROID)
      check_library_exists(gcc __gcc_personality_v0 "" LIBCXX_HAS_GCC_LIB)
    else()
      check_library_exists(gcc_s __gcc_personality_v0 "" LIBCXX_HAS_GCC_S_LIB)
    endif()
  endif()
endif()

check_cxx_compiler_flag(-nostdlibinc CXX_SUPPORTS_NOSTDLIBINC_FLAG)
check_cxx_compiler_flag(-nolibc CXX_SUPPORTS_NOLIBC_FLAG)

# libc++ is using -nostdlib++ at the link step when available,
# otherwise -nodefaultlibs is used. We want all our checks to also
# use one of these options, otherwise we may end up with an inconsistency between
# the flags we think we require during configuration (if the checks are
# performed without one of those options) and the flags that are actually
# required during compilation (which has the -nostdlib++ or -nodefaultlibs). libc is
# required for the link to go through. We remove sanitizers from the
# configuration checks to avoid spurious link errors.

check_cxx_compiler_flag(-nostdlib++ CXX_SUPPORTS_NOSTDLIBXX_FLAG)
if (CXX_SUPPORTS_NOSTDLIBXX_FLAG)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -nostdlib++")
else()
  check_c_compiler_flag(-nodefaultlibs C_SUPPORTS_NODEFAULTLIBS_FLAG)
  if (C_SUPPORTS_NODEFAULTLIBS_FLAG)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -nodefaultlibs")
  endif()
endif()

# Only link against compiler-rt manually if we use -nodefaultlibs, since
# otherwise the compiler will do the right thing on its own.
if (NOT CXX_SUPPORTS_NOSTDLIBXX_FLAG AND C_SUPPORTS_NODEFAULTLIBS_FLAG)
  if (LIBCXX_USE_COMPILER_RT)
    include(HandleCompilerRT)
    find_compiler_rt_library(builtins LIBCXX_BUILTINS_LIBRARY
                             FLAGS ${LIBCXX_COMPILE_FLAGS})
    if (LIBCXX_BUILTINS_LIBRARY)
      list(APPEND CMAKE_REQUIRED_LIBRARIES "${LIBCXX_BUILTINS_LIBRARY}")
    else()
      message(WARNING "Could not find builtins library from libc++")
    endif()
  elseif (LIBCXX_HAS_GCC_LIB)
    list(APPEND CMAKE_REQUIRED_LIBRARIES gcc)
  elseif (LIBCXX_HAS_GCC_S_LIB)
    list(APPEND CMAKE_REQUIRED_LIBRARIES gcc_s)
  endif ()
  if (MINGW)
    # Mingw64 requires quite a few "C" runtime libraries in order for basic
    # programs to link successfully with -nodefaultlibs.
    if (LIBCXX_USE_COMPILER_RT)
      set(MINGW_RUNTIME ${LIBCXX_BUILTINS_LIBRARY})
    else ()
      set(MINGW_RUNTIME gcc_s gcc)
    endif()
    set(MINGW_LIBRARIES mingw32 ${MINGW_RUNTIME} moldname mingwex msvcrt advapi32
                        shell32 user32 kernel32 mingw32 ${MINGW_RUNTIME}
                        moldname mingwex msvcrt)
    list(APPEND CMAKE_REQUIRED_LIBRARIES ${MINGW_LIBRARIES})
  endif()
endif()

if (CXX_SUPPORTS_NOSTDLIBXX_FLAG OR C_SUPPORTS_NODEFAULTLIBS_FLAG)
  if (CMAKE_C_FLAGS MATCHES -fsanitize OR CMAKE_CXX_FLAGS MATCHES -fsanitize)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fno-sanitize=all")
  endif ()
  if (CMAKE_C_FLAGS MATCHES -fsanitize-coverage OR CMAKE_CXX_FLAGS MATCHES -fsanitize-coverage)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize-coverage=0")
  endif ()
endif ()

# Check compiler pragmas
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  cmake_push_check_state()
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-pragmas")
  check_c_source_compiles("
#pragma comment(lib, \"c\")
int main(void) { return 0; }
" C_SUPPORTS_COMMENT_LIB_PRAGMA)
  cmake_pop_check_state()
endif()

check_symbol_exists(__PICOLIBC__ "string.h" PICOLIBC)

# Check libraries
if(WIN32 AND NOT MINGW)
  # TODO(compnerd) do we want to support an emulation layer that allows for the
  # use of pthread-win32 or similar libraries to emulate pthreads on Windows?
  set(LIBCXX_HAS_PTHREAD_LIB NO)
  set(LIBCXX_HAS_RT_LIB NO)
  set(LIBCXX_HAS_ATOMIC_LIB NO)
elseif(APPLE)
  set(LIBCXX_HAS_PTHREAD_LIB NO)
  set(LIBCXX_HAS_RT_LIB NO)
  set(LIBCXX_HAS_ATOMIC_LIB NO)
elseif(FUCHSIA)
  set(LIBCXX_HAS_PTHREAD_LIB NO)
  set(LIBCXX_HAS_RT_LIB NO)
  check_library_exists(atomic __atomic_fetch_add_8 "" LIBCXX_HAS_ATOMIC_LIB)
elseif(ANDROID)
  set(LIBCXX_HAS_PTHREAD_LIB NO)
  set(LIBCXX_HAS_RT_LIB NO)
  set(LIBCXX_HAS_ATOMIC_LIB NO)
elseif(PICOLIBC)
  set(LIBCXX_HAS_PTHREAD_LIB NO)
  set(LIBCXX_HAS_RT_LIB NO)
  set(LIBCXX_HAS_ATOMIC_LIB NO)
else()
  check_library_exists(pthread pthread_create "" LIBCXX_HAS_PTHREAD_LIB)
  check_library_exists(rt clock_gettime "" LIBCXX_HAS_RT_LIB)
  check_library_exists(atomic __atomic_fetch_add_8 "" LIBCXX_HAS_ATOMIC_LIB)
endif()
