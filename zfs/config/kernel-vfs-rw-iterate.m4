dnl #
dnl # Linux 3.16 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_RW_ITERATE],
	[AC_MSG_CHECKING([whether fops->read/write_iter() are available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_read(struct kiocb *kiocb, struct iov_iter *to)
		    { return 0; }
		ssize_t test_write(struct kiocb *kiocb, struct iov_iter *from)
		    { return 0; }

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
		    .read_iter = test_read,
		    .write_iter = test_write,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFS_RW_ITERATE, 1,
			[fops->read/write_iter() are available])

		ZFS_AC_KERNEL_NEW_SYNC_READ
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 4.1 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_NEW_SYNC_READ],
	[AC_MSG_CHECKING([whether new_sync_read/write() are available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		        ssize_t ret __attribute__ ((unused));
			struct file *filp = NULL;
			char __user *rbuf = NULL;
			const char __user *wbuf = NULL;
			size_t len = 0;
			loff_t ppos;

			ret = new_sync_read(filp, rbuf, len, &ppos);
			ret = new_sync_write(filp, wbuf, len, &ppos);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_NEW_SYNC_READ, 1,
			[new_sync_read()/new_sync_write() are available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 4.1.x API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GENERIC_WRITE_CHECKS],
	[AC_MSG_CHECKING([whether generic_write_checks() takes kiocb])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

	],[
		struct kiocb *iocb = NULL;
		struct iov_iter *iov = NULL;
		generic_write_checks(iocb, iov);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_WRITE_CHECKS_KIOCB, 1,
			[generic_write_checks() takes kiocb])
	],[
		AC_MSG_RESULT(no)
	])
])
