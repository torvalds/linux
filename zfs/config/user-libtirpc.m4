dnl #
dnl # Check for libtirpc - may be needed for xdr functionality
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBTIRPC], [
	AC_ARG_WITH([tirpc],
	    [AS_HELP_STRING([--with-tirpc],
	        [use tirpc for xdr encoding @<:@default=check@:>@])],
	    [],
	    [with_tirpc=check])

	LIBTIRPC=
	LIBTIRPC_CFLAGS=

	AS_IF([test "x$with_tirpc" != xno],
	    [AC_CHECK_LIB([tirpc], [xdrmem_create],
		[AC_SUBST([LIBTIRPC], [-ltirpc])
		 AC_SUBST([LIBTIRPC_CFLAGS], [-I/usr/include/tirpc])
		 AC_DEFINE([HAVE_LIBTIRPC], [1], [Define if you have libtirpc])
		],
		[if test "x$with_tirpc" != xcheck; then
		    AC_MSG_FAILURE(
			[--with-tirpc was given, but test for tirpc failed])
		 fi
		AC_SEARCH_LIBS([xdrmem_create], [tirpc], [], [
		    AC_MSG_FAILURE([xdrmem_create() requires tirpc or libc])])
		])],
	    [AC_SEARCH_LIBS([xdrmem_create], [tirpc], [], [
		AC_MSG_FAILURE([xdrmem_create() requires libc])])
	    ])
])
