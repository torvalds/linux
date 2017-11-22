dnl #
dnl # 2.6.32 - 2.6.33, bdi_setup_and_register() is not exported.
dnl # 2.6.34 - 3.19, bdi_setup_and_register() takes 3 arguments.
dnl # 4.0 - 4.11, bdi_setup_and_register() takes 2 arguments.
dnl # 4.12 - x.y, super_setup_bdi_name() new interface.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDI], [
	AC_MSG_CHECKING([whether super_setup_bdi_name() exists])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
		struct super_block sb;
	], [
		char *name = "bdi";
		atomic_long_t zfs_bdi_seq;
		int error __attribute__((unused)) =
		    super_setup_bdi_name(&sb, "%.28s-%ld", name, atomic_long_inc_return(&zfs_bdi_seq));
	], [super_setup_bdi_name], [fs/super.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SUPER_SETUP_BDI_NAME, 1,
                    [super_setup_bdi_name() exits])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING(
		    [whether bdi_setup_and_register() wants 2 args])
		ZFS_LINUX_TRY_COMPILE_SYMBOL([
			#include <linux/backing-dev.h>
			struct backing_dev_info bdi;
		], [
			char *name = "bdi";
			int error __attribute__((unused)) =
			    bdi_setup_and_register(&bdi, name);
		], [bdi_setup_and_register], [mm/backing-dev.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_2ARGS_BDI_SETUP_AND_REGISTER, 1,
			    [bdi_setup_and_register() wants 2 args])
		], [
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING(
			    [whether bdi_setup_and_register() wants 3 args])
			ZFS_LINUX_TRY_COMPILE_SYMBOL([
				#include <linux/backing-dev.h>
				struct backing_dev_info bdi;
			], [
				char *name = "bdi";
				unsigned int cap = BDI_CAP_MAP_COPY;
				int error __attribute__((unused)) =
				    bdi_setup_and_register(&bdi, name, cap);
			], [bdi_setup_and_register], [mm/backing-dev.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_3ARGS_BDI_SETUP_AND_REGISTER, 1,
				    [bdi_setup_and_register() wants 3 args])
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])
