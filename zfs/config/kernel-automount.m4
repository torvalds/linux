dnl #
dnl # 2.6.37 API change
dnl # The dops->d_automount() dentry operation was added as a clean
dnl # solution to handling automounts.  Prior to this cifs/nfs clients
dnl # which required automount support would abuse the follow_link()
dnl # operation on directories for this purpose.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_AUTOMOUNT], [
	AC_MSG_CHECKING([whether dops->d_automount() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/dcache.h>
		struct vfsmount *d_automount(struct path *p) { return NULL; }
		struct dentry_operations dops __attribute__ ((unused)) = {
			.d_automount = d_automount,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_AUTOMOUNT, 1, [dops->automount() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
