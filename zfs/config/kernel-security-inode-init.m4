dnl #
dnl # 2.6.39 API change
dnl # The security_inode_init_security() function now takes an additional
dnl # qstr argument which must be passed in from the dentry if available.
dnl # Passing a NULL is safe when no qstr is available the relevant
dnl # security checks will just be skipped.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_6ARGS_SECURITY_INODE_INIT_SECURITY], [
	AC_MSG_CHECKING([whether security_inode_init_security wants 6 args])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/security.h>
	],[
		struct inode *ip __attribute__ ((unused)) = NULL;
		struct inode *dip __attribute__ ((unused)) = NULL;
		const struct qstr *str __attribute__ ((unused)) = NULL;
		char *name __attribute__ ((unused)) = NULL;
		void *value __attribute__ ((unused)) = NULL;
		size_t len __attribute__ ((unused)) = 0;

		security_inode_init_security(ip, dip, str, &name, &value, &len);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_6ARGS_SECURITY_INODE_INIT_SECURITY, 1,
		          [security_inode_init_security wants 6 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.2 API change
dnl # The security_inode_init_security() API has been changed to include
dnl # a filesystem specific callback to write security extended attributes.
dnl # This was done to support the initialization of multiple LSM xattrs
dnl # and the EVM xattr.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CALLBACK_SECURITY_INODE_INIT_SECURITY], [
	AC_MSG_CHECKING([whether security_inode_init_security wants callback])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/security.h>
	],[
		struct inode *ip __attribute__ ((unused)) = NULL;
		struct inode *dip __attribute__ ((unused)) = NULL;
		const struct qstr *str __attribute__ ((unused)) = NULL;
		initxattrs func __attribute__ ((unused)) = NULL;

		security_inode_init_security(ip, dip, str, func, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CALLBACK_SECURITY_INODE_INIT_SECURITY, 1,
		          [security_inode_init_security wants callback])
	],[
		AC_MSG_RESULT(no)
	])
])
