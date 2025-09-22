# Process this file with autoconf to produce a configure script, like so:
# 
# aclocal -I .. -I ../config && autoconf && autoheader && automake

AC_PREREQ(2.59)
AC_INIT(libssp, 1.0)
AC_CONFIG_SRCDIR(ssp.c)
AC_CANONICAL_SYSTEM
ACX_NONCANONICAL_TARGET

AM_INIT_AUTOMAKE

AC_MSG_CHECKING([for --enable-version-specific-runtime-libs])
AC_ARG_ENABLE(version-specific-runtime-libs,
[  --enable-version-specific-runtime-libs    Specify that runtime libraries should be installed in a compiler-specific directory ],
[case "$enableval" in
 yes) version_specific_libs=yes ;;
 no)  version_specific_libs=no ;;
 *)   AC_MSG_ERROR([Unknown argument to enable/disable version-specific libs]);;
 esac],
[version_specific_libs=no])
AC_MSG_RESULT($version_specific_libs)

AM_MAINTAINER_MODE

GCC_NO_EXECUTABLES

AM_ENABLE_MULTILIB(, ..)

target_alias=${target_alias-$host_alias}
AC_SUBST(target_alias)

AC_CONFIG_HEADERS(config.h)

AC_LANG_C
# The same as in boehm-gc and libstdc++. Have to borrow it from there.
# We must force CC to /not/ be precious variables; otherwise
# the wrong, non-multilib-adjusted value will be used in multilibs.
# As a side effect, we have to subst CFLAGS ourselves.

m4_rename([_AC_ARG_VAR_PRECIOUS],[real_PRECIOUS])
m4_define([_AC_ARG_VAR_PRECIOUS],[])
AC_PROG_CC
m4_rename([real_PRECIOUS],[_AC_ARG_VAR_PRECIOUS])

AC_SUBST(CFLAGS)

if test "x$GCC" != "xyes"; then
  AC_MSG_ERROR([libssp must be built with GCC])
fi
AC_PROG_CPP

AC_MSG_CHECKING([whether -fstack-protector works])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -fstack-protector -Werror"
AC_TRY_COMPILE([
void __attribute__((noinline)) bar (char *x)
{
  __builtin_memset (x, 0, 64);
}],[char buf[64]; bar (buf);],
[AC_MSG_RESULT(yes)],
[AC_MSG_RESULT(no)])
CFLAGS="$save_CFLAGS"

AC_MSG_CHECKING([whether hidden visibility is supported])
AC_TRY_COMPILE([
void __attribute__((visibility ("hidden"))) bar (void) {}],,
[ssp_hidden=yes],[ssp_hidden=no])
AC_MSG_RESULT($ssp_hidden)
if test x$ssp_hidden = xyes; then
  AC_DEFINE([HAVE_HIDDEN_VISIBILITY],[1],[__attribute__((visibility ("hidden"))) supported])
fi

AC_MSG_CHECKING([whether symbol versioning is supported])
cat > conftest.map <<EOF
FOO_1.0 {
  global: *foo*; bar; local: *;
};
EOF
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS -fPIC -shared -Wl,--version-script,./conftest.map"
if test x$gcc_no_link = xyes; then
  # If we cannot link, we cannot build shared libraries, so do not use
  # symbol versioning.
  ssp_use_symver=no
else
  AC_TRY_LINK([int foo;],[],[ssp_use_symver=yes],[ssp_use_symver=no])
fi
LDFLAGS="$save_LDFLAGS"
AC_MSG_RESULT($ssp_use_symver)
AM_CONDITIONAL(LIBSSP_USE_SYMVER, [test "x$ssp_use_symver" = xyes])

AC_CHECK_HEADERS(alloca.h paths.h syslog.h string.h unistd.h fcntl.h stdio.h limits.h)

if test x$gcc_no_link = xyes; then
  # Presume the ISO C functions are available; add target-specific
  # configuration here if required.
  AC_DEFINE(HAVE_STRNCPY)
  AC_DEFINE(HAVE_STRNCAT)
else
  AC_CHECK_FUNCS(mempcpy strncpy strncat)
fi

AC_MSG_CHECKING([whether vsnprintf is usable])
AC_RUN_IFELSE(AC_LANG_PROGRAM([
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
int foo (char *buf, size_t n, const char *fmt, ...)
{
  va_list ap;
  int ret;
  va_start (ap, fmt);
  ret = vsnprintf (buf, n, fmt, ap);
  va_end (ap);
  return ret;
}],
[char buf@<:@8@:>@; memset (buf, 'A', sizeof (buf));
 if (foo (buf, 4, ".%s.", "CDEFG") != 7)
   return 1;
 return memcmp (buf, ".CD\0AAAA", sizeof (buf)) != 0;]),
[ssp_have_usable_vsnprintf=define],
[ssp_have_usable_vsnprintf=undef],
[ssp_have_usable_vsnprintf=undef])
if test "x$ssp_have_usable_vsnprintf" = xdefine; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([HAVE_USABLE_VSNPRINTF],[1],[vsnprintf is present and works])
else
  AC_MSG_RESULT(no)
fi
AC_SUBST(ssp_have_usable_vsnprintf)

AM_PROG_LIBTOOL
AC_SUBST(enable_shared)
AC_SUBST(enable_static)

# Calculate toolexeclibdir
# Also toolexecdir, though it's only used in toolexeclibdir
case ${version_specific_libs} in
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

if test ${multilib} = yes; then
  multilib_arg="--enable-multilib"
else
  multilib_arg=
fi

AC_CONFIG_FILES([Makefile ssp/ssp.h])
AC_OUTPUT
