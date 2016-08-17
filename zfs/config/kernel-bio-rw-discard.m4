dnl #
dnl # Interface for issuing a discard bio:
dnl # 2.6.28-2.6.35: BIO_RW_DISCARD
dnl # 2.6.36-3.x:    REQ_DISCARD
dnl #

dnl # Since REQ_DISCARD is a preprocessor definition, there is no need for an
dnl # autotools check for it. Also, REQ_DISCARD existed in the request layer
dnl # until torvalds/linux@7b6d91daee5cac6402186ff224c3af39d79f4a0e unified the
dnl # request layer and bio layer flags, so it would be wrong to assume that
dnl # the APIs are mutually exclusive contrary to the typical case.
AC_DEFUN([ZFS_AC_KERNEL_BIO_RW_DISCARD], [
	AC_MSG_CHECKING([whether BIO_RW_DISCARD is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = BIO_RW_DISCARD;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_RW_DISCARD, 1, [BIO_RW_DISCARD is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
