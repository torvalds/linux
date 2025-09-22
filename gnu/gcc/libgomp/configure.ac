# Process this file with autoconf to produce a configure script, like so:
# aclocal -I ../config && autoconf && autoheader && automake

AC_PREREQ(2.59)
AC_INIT([GNU OpenMP Runtime Library], 1.0,,[libgomp])
AC_CONFIG_HEADER(config.h)

# -------
# Options
# -------

AC_MSG_CHECKING([for --enable-version-specific-runtime-libs])
LIBGOMP_ENABLE(version-specific-runtime-libs, no, ,
   [Specify that runtime libraries should be installed in a compiler-specific directory],
   permit yes|no)
AC_MSG_RESULT($enable_version_specific_runtime_libs)

AC_MSG_CHECKING([for --enable-linux-futex])
LIBGOMP_ENABLE(linux-futex, default, ,
  [Use the Linux futex system call],
  permit yes|no|default)
AC_MSG_RESULT($enable_linux_futex)

# We would like our source tree to be readonly. However when releases or
# pre-releases are generated, the flex/bison generated files as well as the
# various formats of manuals need to be included along with the rest of the
# sources.  Therefore we have --enable-generated-files-in-srcdir to do 
# just that.
AC_MSG_CHECKING([for --enable-generated-files-in-srcdir])
LIBGOMP_ENABLE(generated-files-in-srcdir, no, ,
   [put copies of generated files in source dir intended for creating source 
    tarballs for users without texinfo bison or flex.],
   permit yes|no)
AC_MSG_RESULT($enable_generated_files_in_srcdir)
AM_CONDITIONAL(GENINSRC, test "$enable_generated_files_in_srcdir" = yes)


# -------
# -------

# Gets build, host, target, *_vendor, *_cpu, *_os, etc.
#
# You will slowly go insane if you do not grok the following fact:  when
# building this library, the top-level /target/ becomes the library's /host/.
#
# configure then causes --target to default to --host, exactly like any
# other package using autoconf.  Therefore, 'target' and 'host' will
# always be the same.  This makes sense both for native and cross compilers
# just think about it for a little while.  :-)
#
# Also, if this library is being configured as part of a cross compiler, the
# top-level configure script will pass the "real" host as $with_cross_host.
#
# Do not delete or change the following two lines.  For why, see
# http://gcc.gnu.org/ml/libstdc++/2003-07/msg00451.html
AC_CANONICAL_SYSTEM
target_alias=${target_alias-$host_alias}

# Sets up automake.  Must come after AC_CANONICAL_SYSTEM.  Each of the
# following is magically included in AUTOMAKE_OPTIONS in each Makefile.am.
#  1.9.0:  minimum required version
#  no-define:  PACKAGE and VERSION will not be #define'd in config.h (a bunch
#              of other PACKAGE_* variables will, however, and there's nothing
#              we can do about that; they come from AC_INIT).
#  foreign:  we don't follow the normal rules for GNU packages (no COPYING
#            file in the top srcdir, etc, etc), so stop complaining.
#  no-dependencies:  turns off auto dependency generation (just for now)
#  -Wall:  turns on all automake warnings...
#  -Wno-portability:  ...except this one, since GNU make is required.
#  -Wno-override: ... and this one, since we do want this in testsuite.
AM_INIT_AUTOMAKE([1.9.0 foreign -Wall -Wno-portability -Wno-override])
AM_ENABLE_MULTILIB(, ..)

# Calculate toolexeclibdir
# Also toolexecdir, though it's only used in toolexeclibdir
case ${enable_version_specific_runtime_libs} in
  yes)
    # Need the gcc compiler version to know where to install libraries
    # and header files if --enable-version-specific-runtime-libs option
    # is selected.
    toolexecdir='$(libdir)/gcc/$(target_alias)'
    toolexeclibdir='$(toolexecdir)/$(gcc_version)$(MULTISUBDIR)'
    ;;
  no)
    if test -n "$with_cross_host" &&
       test x"$with_cross_host" != x"no"; then
      # Install a library built with a cross compiler in tooldir, not libdir.
      toolexecdir='$(exec_prefix)/$(target_alias)'
      toolexeclibdir='$(toolexecdir)/lib'
    else
      toolexecdir='$(libdir)/gcc-lib/$(target_alias)'
      toolexeclibdir='$(libdir)'
    fi
    multi_os_directory=`$CC -print-multi-os-directory`
    case $multi_os_directory in
      .) ;; # Avoid trailing /.
      *) toolexeclibdir=$toolexeclibdir/$multi_os_directory ;;
    esac
    ;;
esac
AC_SUBST(toolexecdir)
AC_SUBST(toolexeclibdir)

# Check the compiler.
# The same as in boehm-gc and libstdc++. Have to borrow it from there.
# We must force CC to /not/ be precious variables; otherwise
# the wrong, non-multilib-adjusted value will be used in multilibs.
# As a side effect, we have to subst CFLAGS ourselves.

m4_rename([_AC_ARG_VAR_PRECIOUS],[real_PRECIOUS])
m4_define([_AC_ARG_VAR_PRECIOUS],[])
AC_PROG_CC
m4_rename([real_PRECIOUS],[_AC_ARG_VAR_PRECIOUS])

AC_SUBST(CFLAGS)

# In order to override CFLAGS_FOR_TARGET, all of our special flags go
# in XCFLAGS.  But we need them in CFLAGS during configury.  So put them
# in both places for now and restore CFLAGS at the end of config.
save_CFLAGS="$CFLAGS"

# Add -Wall -Werror if we are using GCC.
if test "x$GCC" = "xyes"; then
  XCFLAGS="$XCFLAGS -Wall -Werror"
fi

# Find other programs we need.
AC_CHECK_TOOL(AR, ar)
AC_CHECK_TOOL(RANLIB, ranlib, ranlib-not-found-in-path-error)
AC_PATH_PROG(PERL, perl, perl-not-found-in-path-error)
AC_PROG_MAKE_SET
AC_PROG_INSTALL

# See if makeinfo has been installed and is modern enough
# that we can use it.
ACX_CHECK_PROG_VER([MAKEINFO], [makeinfo], [--version],
                   [GNU texinfo.* \([0-9][0-9.]*\)],
                   [4.[4-9]*])
AM_CONDITIONAL(BUILD_INFO, test $gcc_cv_prog_makeinfo_modern = "yes")


# Configure libtool
AM_PROG_LIBTOOL
AC_SUBST(enable_shared)
AC_SUBST(enable_static)

AM_MAINTAINER_MODE

# We need gfortran to compile parts of the library
# We can't use AC_PROG_FC because it expects a fully working gfortran.
#AC_PROG_FC(gfortran)
FC="$GFORTRAN"
AC_PROG_FC(gfortran)
FCFLAGS="$FCFLAGS -Wall"

# For libtool versioning info, format is CURRENT:REVISION:AGE
libtool_VERSION=1:0:0
AC_SUBST(libtool_VERSION)

# Check header files.
AC_STDC_HEADERS
AC_HEADER_TIME
AC_CHECK_HEADERS(unistd.h semaphore.h sys/loadavg.h sys/time.h)

GCC_HEADER_STDINT(gstdint.h)

# Check to see if -pthread or -lpthread is needed.  Prefer the former.
# In case the pthread.h system header is not found, this test will fail.
XPCFLAGS=""
CFLAGS="$CFLAGS -pthread"
AC_LINK_IFELSE(
 [AC_LANG_PROGRAM(
  [#include <pthread.h>
   void *g(void *d) { return NULL; }],
  [pthread_t t; pthread_create(&t,NULL,g,NULL);])],
 [XPCFLAGS=" -Wc,-pthread"],
 [CFLAGS="$save_CFLAGS" LIBS="-lpthread $LIBS"
  AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
    [#include <pthread.h>
     void *g(void *d) { return NULL; }],
    [pthread_t t; pthread_create(&t,NULL,g,NULL);])],
   [],
   [AC_MSG_ERROR([Pthreads are required to build libgomp])])])

# Check for functions needed.
AC_CHECK_FUNCS(getloadavg clock_gettime)

# Check for broken semaphore implementation on darwin.
# sem_init returns: sem_init error: Function not implemented.
case "$host" in
  *-darwin*)
    AC_DEFINE(HAVE_BROKEN_POSIX_SEMAPHORES, 1,
	Define if the POSIX Semaphores do not work on your system.)
    ;;
  *-linux*)
    case "$enable_linux_futex" in
      default)
	# If headers don't have gettid/futex syscalls definition, then
	# default to no, otherwise there will be compile time failures.
	# Otherwise, default to yes.  If we don't detect we are
	# compiled/linked against NPTL and not cross-compiling, check
	# if programs are run by default against NPTL and if not, issue
	# a warning.
	enable_linux_futex=no
	AC_LINK_IFELSE(
	 [AC_LANG_PROGRAM(
	  [#include <sys/syscall.h>
	   int lk;],
	  [syscall (SYS_gettid); syscall (SYS_futex, &lk, 0, 0, 0);])],
	  [AC_LINK_IFELSE(
	   [AC_LANG_PROGRAM(
	    [#ifndef _GNU_SOURCE
	     #define _GNU_SOURCE 1
	     #endif
	     #include <pthread.h>
	     pthread_t th; void *status;],
	    [pthread_tryjoin_np (th, &status);])],[enable_linux_futex=yes],
	    [if test x$cross_compiling = xno; then
	       if getconf GNU_LIBPTHREAD_VERSION 2>/dev/null \
		  | LC_ALL=C grep -i NPTL > /dev/null 2>/dev/null; then
		 AC_MSG_WARN([The kernel might not support futex or gettid syscalls.
If so, please configure with --disable-linux-futex])
	       fi
	     fi
	     enable_linux_futex=yes])])
	;;
      yes)
	AC_LINK_IFELSE(
	 [AC_LANG_PROGRAM(
	  [#include <sys/syscall.h>
	   int lk;],
	  [syscall (SYS_gettid); syscall (SYS_futex, &lk, 0, 0, 0);])],[],
	  [AC_MSG_ERROR([SYS_gettid and SYS_futex required for --enable-linux-futex])])
	;;
    esac
    ;;
esac

# At least for glibc, clock_gettime is in librt.  But don't pull that
# in if it still doesn't give us the function we want.
if test $ac_cv_func_clock_gettime = no; then
  AC_CHECK_LIB(rt, clock_gettime,
    [LIBS="-lrt $LIBS"
     AC_DEFINE(HAVE_CLOCK_GETTIME, 1,
	       [Define to 1 if you have the `clock_gettime' function.])])
fi

# See if we support thread-local storage.
GCC_CHECK_TLS

# See what sort of export controls are availible.
LIBGOMP_CHECK_ATTRIBUTE_VISIBILITY
LIBGOMP_CHECK_ATTRIBUTE_DLLEXPORT
LIBGOMP_CHECK_ATTRIBUTE_ALIAS
LIBGOMP_ENABLE_SYMVERS

# Get target configury.
. ${srcdir}/configure.tgt
CFLAGS="$save_CFLAGS $XCFLAGS"

# Check for __sync_val_compare_and_swap, but only after the target has
# had a chance to set XCFLAGS.
LIBGOMP_CHECK_SYNC_BUILTINS

XCFLAGS="$XCFLAGS$XPCFLAGS"

AC_SUBST(config_path)
AC_SUBST(XCFLAGS)
AC_SUBST(XLDFLAGS)

# Cleanup and exit.
CFLAGS="$save_CFLAGS"
AC_CACHE_SAVE

if test ${multilib} = yes; then
  multilib_arg="--enable-multilib"
else
  multilib_arg=
fi

# Set up the set of libraries that we need to link against for libgomp.
# Note that the GOMP_SELF_SPEC in gcc.c will force -pthread for -fopenmp,
# which will force linkage against -lpthread (or equivalent for the system).
# That's not 100% ideal, but about the best we can do easily.
if test $enable_shared = yes; then
  link_gomp="-lgomp %{static: $LIBS}"
else
  link_gomp="-lgomp $LIBS"
fi
AC_SUBST(link_gomp)

AM_CONDITIONAL([USE_FORTRAN], [test "$ac_cv_fc_compiler_gnu" = yes])

# ??? 2006-01-24: Paulo committed to asking autoconf folk to document
# and export AC_COMPUTE_INT.  If that happens, then we'll need to remove
# the underscore here and update the PREREQ.  If it doesn't, then we'll
# need to copy this macro to our acinclude.m4.
save_CFLAGS="$CFLAGS"
for i in $config_path; do
  if test -f $srcdir/config/$i/omp-lock.h; then
    CFLAGS="$CFLAGS -include $srcdir/config/$i/omp-lock.h"
    break
  fi
done

_AC_COMPUTE_INT([sizeof (omp_lock_t)], [OMP_LOCK_SIZE],,
  [AC_MSG_ERROR([unsupported system, cannot find sizeof (omp_lock_t)])])
_AC_COMPUTE_INT([__alignof (omp_lock_t)], [OMP_LOCK_ALIGN])
_AC_COMPUTE_INT([sizeof (omp_nest_lock_t)], [OMP_NEST_LOCK_SIZE])
_AC_COMPUTE_INT([__alignof (omp_nest_lock_t)], [OMP_NEST_LOCK_ALIGN])

# If the lock fits in an integer, then arrange for Fortran to use that
# integer.  If it doesn't, then arrange for Fortran to use a pointer.
# Except that we don't have a way at present to multi-lib the installed
# Fortran modules, so we assume 8 bytes for pointers, regardless of the
# actual target.
OMP_LOCK_KIND=$OMP_LOCK_SIZE
OMP_NEST_LOCK_KIND=$OMP_NEST_LOCK_SIZE
if test $OMP_LOCK_SIZE -gt 8 || test $OMP_LOCK_ALIGN -gt $OMP_LOCK_SIZE; then
  OMP_LOCK_KIND=8
fi
if test $OMP_NEST_LOCK_SIZE -gt 8 || test $OMP_NEST_LOCK_ALIGN -gt $OMP_NEST_LOCK_SIZE; then
  OMP_NEST_LOCK_KIND=8
fi

AC_SUBST(OMP_LOCK_SIZE)
AC_SUBST(OMP_LOCK_ALIGN)
AC_SUBST(OMP_NEST_LOCK_SIZE)
AC_SUBST(OMP_NEST_LOCK_ALIGN)
AC_SUBST(OMP_LOCK_KIND)
AC_SUBST(OMP_NEST_LOCK_KIND)
CFLAGS="$save_CFLAGS"

AC_CONFIG_FILES(omp.h omp_lib.h omp_lib.f90 libgomp_f.h)
AC_CONFIG_FILES(Makefile testsuite/Makefile libgomp.spec)
AC_OUTPUT
