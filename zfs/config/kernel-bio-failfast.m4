dnl #
dnl # Preferred interface for setting FAILFAST on a bio:
dnl #   2.6.28-2.6.35: BIO_RW_FAILFAST_{DEV|TRANSPORT|DRIVER}
dnl #       >= 2.6.36: REQ_FAILFAST_{DEV|TRANSPORT|DRIVER}
dnl #

AC_DEFUN([ZFS_AC_KERNEL_BIO_FAILFAST_DTD], [
	AC_MSG_CHECKING([whether BIO_RW_FAILFAST_* are defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = ((1 << BIO_RW_FAILFAST_DEV) |
			 (1 << BIO_RW_FAILFAST_TRANSPORT) |
			 (1 << BIO_RW_FAILFAST_DRIVER));
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_RW_FAILFAST_DTD, 1,
		          [BIO_RW_FAILFAST_* are defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_REQ_FAILFAST_MASK], [
	AC_MSG_CHECKING([whether REQ_FAILFAST_MASK is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = REQ_FAILFAST_MASK;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_FAILFAST_MASK, 1,
		          [REQ_FAILFAST_MASK is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
