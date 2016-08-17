dnl #
dnl # zlib inflate compat,
dnl # Verify the kernel has CONFIG_ZLIB_INFLATE support enabled.
dnl #
AC_DEFUN([SPL_AC_CONFIG_ZLIB_INFLATE], [
	AC_MSG_CHECKING([whether CONFIG_ZLIB_INFLATE is defined])
	SPL_LINUX_TRY_COMPILE([
		#if !defined(CONFIG_ZLIB_INFLATE) && \
		    !defined(CONFIG_ZLIB_INFLATE_MODULE)
		#error CONFIG_ZLIB_INFLATE not defined
		#endif
	],[ ],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel does not include the required zlib inflate support.
	*** Rebuild the kernel with CONFIG_ZLIB_INFLATE=y|m set.])
	])
])

dnl #
dnl # zlib deflate compat,
dnl # Verify the kernel has CONFIG_ZLIB_DEFLATE support enabled.
dnl #
AC_DEFUN([SPL_AC_CONFIG_ZLIB_DEFLATE], [
	AC_MSG_CHECKING([whether CONFIG_ZLIB_DEFLATE is defined])
	SPL_LINUX_TRY_COMPILE([
		#if !defined(CONFIG_ZLIB_DEFLATE) && \
		    !defined(CONFIG_ZLIB_DEFLATE_MODULE)
		#error CONFIG_ZLIB_DEFLATE not defined
		#endif
	],[ ],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel does not include the required zlib deflate support.
	*** Rebuild the kernel with CONFIG_ZLIB_DEFLATE=y|m set.])
	])
])

dnl #
dnl # 2.6.39 API compat,
dnl # The function zlib_deflate_workspacesize() now take 2 arguments.
dnl # This was done to avoid always having to allocate the maximum size
dnl # workspace (268K).  The caller can now specific the windowBits and
dnl # memLevel compression parameters to get a smaller workspace.
dnl #
AC_DEFUN([SPL_AC_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE],
	[AC_MSG_CHECKING([whether zlib_deflate_workspacesize() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/zlib.h>
	],[
		return zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE, 1,
		          [zlib_deflate_workspacesize() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])
