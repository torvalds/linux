dnl #
dnl # config trim unused symbols,
dnl # Verify the kernel has CONFIG_TRIM_UNUSED_KSYMS DISABLED.
dnl #
AC_DEFUN([SPL_AC_CONFIG_TRIM_UNUSED_KSYMS], [
	AC_MSG_CHECKING([whether CONFIG_TRIM_UNUSED_KSYM is disabled])
	SPL_LINUX_TRY_COMPILE([
		#if defined(CONFIG_TRIM_UNUSED_KSYMS)
		#error CONFIG_TRIM_UNUSED_KSYMS not defined
		#endif
	],[ ],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel has unused symbols trimming enabled, please disable.
	*** Rebuild the kernel with CONFIG_TRIM_UNUSED_KSYMS=n set.])
	])
])
