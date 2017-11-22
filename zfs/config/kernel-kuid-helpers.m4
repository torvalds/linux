dnl #
dnl # 3.5 API change,
dnl # Since usernamespaces were introduced in kernel version 3.5, it
dnl # became necessary to go through one more level of indirection
dnl # when dealing with uid/gid - namely the kuid type.
dnl #
dnl #
AC_DEFUN([ZFS_AC_KERNEL_KUID_HELPERS], [
	AC_MSG_CHECKING([whether i_(uid|gid)_(read|write) exist])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct inode *ip = NULL;
		(void) i_uid_read(ip);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KUID_HELPERS, 1,
		    [i_(uid|gid)_(read|write) exist])
	],[
		AC_MSG_RESULT(no)
	])
])
