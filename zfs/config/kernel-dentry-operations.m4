dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_D_REVALIDATE_NAMEIDATA], [
	AC_MSG_CHECKING([whether dops->d_revalidate() takes struct nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/dcache.h>
		#include <linux/sched.h>

		int revalidate (struct dentry *dentry,
		    struct nameidata *nidata) { return 0; }

		static const struct dentry_operations
		    dops __attribute__ ((unused)) = {
			.d_revalidate	= revalidate,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_REVALIDATE_NAMEIDATA, 1,
		          [dops->d_revalidate() operation takes nameidata])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.30 API change
dnl # The 'struct dentry_operations' was constified in the dentry structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONST_DENTRY_OPERATIONS], [
	AC_MSG_CHECKING([whether dentry uses const struct dentry_operations])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/dcache.h>

		const struct dentry_operations test_d_op = {
			.d_revalidate = NULL,
		};
	],[
		struct dentry d __attribute__ ((unused));

		d.d_op = &test_d_op;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CONST_DENTRY_OPERATIONS, 1,
		          [dentry uses const struct dentry_operations])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.38 API change
dnl # Added d_set_d_op() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_D_SET_D_OP],
	[AC_MSG_CHECKING([whether d_set_d_op() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/dcache.h>
	], [
		d_set_d_op(NULL, NULL);
	], [d_set_d_op], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_SET_D_OP, 1,
		          [d_set_d_op() is available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.38 API chage
dnl # Added sb->s_d_op default dentry_operations member
dnl #
AC_DEFUN([ZFS_AC_KERNEL_S_D_OP],
	[AC_MSG_CHECKING([whether super_block has s_d_op])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct super_block sb __attribute__ ((unused));
		sb.s_d_op = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_S_D_OP, 1, [struct super_block has s_d_op])
	], [
		AC_MSG_RESULT(no)
	])
])
