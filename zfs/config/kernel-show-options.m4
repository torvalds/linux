dnl #
dnl # Linux 3.3 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SHOW_OPTIONS], [
	AC_MSG_CHECKING([whether sops->show_options() wants dentry])

	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int show_options (struct seq_file * x, struct dentry * y) { return 0; };
		static struct super_operations sops __attribute__ ((unused)) = {
			.show_options = show_options,
		};
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_SHOW_OPTIONS_WITH_DENTRY, 1,
			[sops->show_options() with dentry])
	],[
		AC_MSG_RESULT([no])
	])
])
