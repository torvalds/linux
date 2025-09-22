dnl
dnl Check whether _Unwind_GetIPInfo is available.
dnl
AC_DEFUN([GCC_CHECK_UNWIND_GETIPINFO], [
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -fno-exceptions"
  AC_MSG_CHECKING([for _Unwind_GetIPInfo])
  AC_CACHE_VAL(gcc_cv_getipinfo, [
  AC_TRY_LINK([extern "C" { extern void _Unwind_GetIPInfo(); }],
      [_Unwind_GetIPInfo();],
      [gcc_cv_getipinfo=yes],
      [gcc_cv_getipinfo=no])
  ])
  if test $gcc_cv_getipinfo = yes; then
    AC_DEFINE(HAVE_GETIPINFO, 1, [Define if _Unwind_GetIPInfo is available.])
  fi
  AC_MSG_RESULT($gcc_cv_getipinfo)
  CXXFLAGS="$ac_save_CXXFLAGS"
  AC_LANG_RESTORE
])
