dnl #
dnl # 4.14 API change
dnl # kernel_write() which was introduced in 3.9 was updated to take
dnl # the offset as a pointer which is needed by vn_rdwr().
dnl #
AC_DEFUN([SPL_AC_KERNEL_WRITE], [
	AC_MSG_CHECKING([whether kernel_write() takes loff_t pointer])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct file *file = NULL;
		const void *buf = NULL;
		size_t count = 0;
		loff_t *pos = NULL;
		ssize_t ret;

		ret = kernel_write(file, buf, count, pos);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_WRITE_PPOS, 1,
		    [kernel_write() take loff_t pointer])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 4.14 API change
dnl # kernel_read() which has existed for forever was updated to take
dnl # the offset as a pointer which is needed by vn_rdwr().
dnl #
AC_DEFUN([SPL_AC_KERNEL_READ], [
	AC_MSG_CHECKING([whether kernel_read() takes loff_t pointer])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct file *file = NULL;
		void *buf = NULL;
		size_t count = 0;
		loff_t *pos = NULL;
		ssize_t ret;

		ret = kernel_read(file, buf, count, pos);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_READ_PPOS, 1,
		    [kernel_read() take loff_t pointer])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
