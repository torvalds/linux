AC_DEFUN([ZFS_AC_KERNEL_VFS_ITERATE], [
	dnl #
	dnl # 4.7 API change
	dnl #
	AC_MSG_CHECKING([whether fops->iterate_shared() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		int iterate(struct file *filp, struct dir_context * context)
		    { return 0; }

		static const struct file_operations fops
		    __attribute__ ((unused)) = {
			.iterate_shared	 = iterate,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFS_ITERATE_SHARED, 1,
		          [fops->iterate_shared() is available])
	],[
		AC_MSG_RESULT(no)

		dnl #
		dnl # 3.11 API change
		dnl #
		dnl # RHEL 7.5 compatibility; the fops.iterate() method was
		dnl # added to the file_operations structure but in order to
		dnl # maintain KABI compatibility all callers must set
		dnl # FMODE_KABI_ITERATE which is checked in iterate_dir().
		dnl # When detected ignore this interface and fallback to
		dnl # to using fops.readdir() to retain KABI compatibility.
		dnl #
		AC_MSG_CHECKING([whether fops->iterate() is available])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
			int iterate(struct file *filp,
			    struct dir_context *context) { return 0; }

			static const struct file_operations fops
			    __attribute__ ((unused)) = {
				.iterate	 = iterate,
			};

			#if defined(FMODE_KABI_ITERATE)
			#error "RHEL 7.5, FMODE_KABI_ITERATE interface"
			#endif
		],[
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_VFS_ITERATE, 1,
				  [fops->iterate() is available])
		],[
			AC_MSG_RESULT(no)

			AC_MSG_CHECKING([whether fops->readdir() is available])
			ZFS_LINUX_TRY_COMPILE([
				#include <linux/fs.h>
				int readdir(struct file *filp, void *entry,
				    filldir_t func) { return 0; }

				static const struct file_operations fops
				    __attribute__ ((unused)) = {
					.readdir = readdir,
				};
			],[
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_VFS_READDIR, 1,
					  [fops->readdir() is available])
			],[
				AC_MSG_ERROR(no; file a bug report with ZoL)
			])
		])
	])
])
