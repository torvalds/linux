dnl #
dnl # 4.16 kernel: check if struct posix_acl acl.a_refcount is a refcount_t.
dnl # It's an atomic_t on older kernels.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_ACL_HAS_REFCOUNT], [
	AC_MSG_CHECKING([whether posix_acl has refcount_t])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/backing-dev.h>
		#include <linux/refcount.h>
		#include <linux/posix_acl.h>
	],[
		struct posix_acl acl;
		refcount_t *r __attribute__ ((unused)) = &acl.a_refcount;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ACL_REFCOUNT, 1, [posix_acl has refcount_t])
	],[
		AC_MSG_RESULT(no)
	])
])
