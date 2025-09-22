# GCC_NO_EXECUTABLES
# -----------------
# FIXME: The GCC team has specific needs which the current Autoconf
# framework cannot solve elegantly.  This macro implements a dirty
# hack until Autoconf is able to provide the services its users
# need.
#
# Several of the support libraries that are often built with GCC can't
# assume the tool-chain is already capable of linking a program: the
# compiler often expects to be able to link with some of such
# libraries.
#
# In several of these libraries, workarounds have been introduced to
# avoid the AC_PROG_CC_WORKS test, that would just abort their
# configuration.  The introduction of AC_EXEEXT, enabled either by
# libtool or by CVS autoconf, have just made matters worse.
#
# Unlike the previous AC_NO_EXECUTABLES, this test does not
# disable link tests at autoconf time, but at configure time.
# This allows AC_NO_EXECUTABLES to be invoked conditionally.
AC_DEFUN_ONCE([GCC_NO_EXECUTABLES],
[m4_divert_push([KILL])

AC_BEFORE([$0], [_AC_COMPILER_EXEEXT])
AC_BEFORE([$0], [AC_LINK_IFELSE])

m4_define([_AC_COMPILER_EXEEXT],
AC_LANG_CONFTEST([AC_LANG_PROGRAM()])
# FIXME: Cleanup?
AS_IF([AC_TRY_EVAL(ac_link)], [gcc_no_link=no], [gcc_no_link=yes])
if test x$gcc_no_link = xyes; then
  # Setting cross_compile will disable run tests; it will
  # also disable AC_CHECK_FILE but that's generally
  # correct if we can't link.
  cross_compiling=yes
  EXEEXT=
else
  m4_defn([_AC_COMPILER_EXEEXT])dnl
fi
)

m4_define([AC_LINK_IFELSE],
if test x$gcc_no_link = xyes; then
  AC_MSG_ERROR([Link tests are not allowed after [[$0]].])
fi
m4_defn([AC_LINK_IFELSE]))

dnl This is a shame.  We have to provide a default for some link tests,
dnl similar to the default for run tests.
m4_define([AC_FUNC_MMAP],
if test x$gcc_no_link = xyes; then
  if test "x${ac_cv_func_mmap_fixed_mapped+set}" != xset; then
    ac_cv_func_mmap_fixed_mapped=no
  fi
fi
if test "x${ac_cv_func_mmap_fixed_mapped}" != xno; then
  m4_defn([AC_FUNC_MMAP])
fi)

m4_divert_pop()dnl
])# GCC_NO_EXECUTABLES
