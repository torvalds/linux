dnl Check whether the target supports TLS.
AC_DEFUN([GCC_CHECK_TLS], [
  GCC_ENABLE(tls, yes, [], [Use thread-local storage])
  AC_CACHE_CHECK([whether the target supports thread-local storage],
		 have_tls, [
    AC_RUN_IFELSE([__thread int a; int b; int main() { return a = b; }],
      [dnl If the test case passed with dynamic linking, try again with
       dnl static linking, but only if static linking is supported (not
       dnl on Solaris 10).  This fails with some older Red Hat releases.
      save_LDFLAGS="$LDFLAGS"
      LDFLAGS="-static $LDFLAGS"
      AC_LINK_IFELSE([int main() { return 0; }],
	AC_RUN_IFELSE([__thread int a; int b; int main() { return a = b; }],
		      [have_tls=yes], [have_tls=no],[]),
	[have_tls=yes])
      LDFLAGS="$save_LDFLAGS"],
      [have_tls=no],
      [AC_COMPILE_IFELSE([__thread int foo;], [have_tls=yes], [have_tls=no])]
    )])
  if test "$enable_tls $have_tls" = "yes yes"; then
    AC_DEFINE(HAVE_TLS, 1,
	      [Define to 1 if the target supports thread-local storage.])
  fi])
