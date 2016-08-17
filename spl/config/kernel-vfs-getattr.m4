dnl #
dnl # 4.11 API, a528d35e@torvalds/linux
dnl # vfs_getattr(const struct path *p, struct kstat *s, u32 m, unsigned int f)
dnl #
AC_DEFUN([SPL_AC_4ARGS_VFS_GETATTR], [
	AC_MSG_CHECKING([whether vfs_getattr() wants 4 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_getattr((const struct path *)NULL,
			(struct kstat *)NULL,
			(u32)0,
			(unsigned int)0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_4ARGS_VFS_GETATTR, 1,
		  [vfs_getattr wants 4 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.9 API
dnl # vfs_getattr(struct path *p, struct kstat *s)
dnl #
AC_DEFUN([SPL_AC_2ARGS_VFS_GETATTR], [
	AC_MSG_CHECKING([whether vfs_getattr() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_getattr((struct path *) NULL,
			(struct kstat *)NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VFS_GETATTR, 1,
			  [vfs_getattr wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # <3.9 API
dnl # vfs_getattr(struct vfsmount *v, struct dentry *d, struct kstat *k)
dnl #
AC_DEFUN([SPL_AC_3ARGS_VFS_GETATTR], [
	AC_MSG_CHECKING([whether vfs_getattr() wants 3 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_getattr((struct vfsmount *)NULL,
			(struct dentry *)NULL,
			(struct kstat *)NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARGS_VFS_GETATTR, 1,
		  [vfs_getattr wants 3 args])
	],[
		AC_MSG_RESULT(no)
	])
])
