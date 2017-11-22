dnl #
dnl # Check if posix_acl_release can be used from a ZFS_META_LICENSED
dnl # module.  The is_owner_or_cap macro was replaced by
dnl # inode_owner_or_capable
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_RELEASE], [
	AC_MSG_CHECKING([whether posix_acl_release() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		struct posix_acl* tmp = posix_acl_alloc(1, 0);
		posix_acl_release(tmp);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_RELEASE, 1,
		    [posix_acl_release() is available])

		AC_MSG_CHECKING([whether posix_acl_release() is GPL-only])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/module.h>
			#include <linux/cred.h>
			#include <linux/fs.h>
			#include <linux/posix_acl.h>

			MODULE_LICENSE("$ZFS_META_LICENSE");
		],[
			struct posix_acl* tmp = posix_acl_alloc(1, 0);
			posix_acl_release(tmp);
		],[
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_POSIX_ACL_RELEASE_GPL_ONLY, 1,
			    [posix_acl_release() is GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.14 API change,
dnl # set_cached_acl() and forget_cached_acl() changed from inline to
dnl # EXPORT_SYMBOL. In the former case, they may not be usable because of
dnl # posix_acl_release. In the latter case, we can always use them.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SET_CACHED_ACL_USABLE], [
	AC_MSG_CHECKING([whether set_cached_acl() is usable])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
		#include <linux/cred.h>
		#include <linux/fs.h>
		#include <linux/posix_acl.h>

		MODULE_LICENSE("$ZFS_META_LICENSE");
	],[
		struct inode *ip = NULL;
		struct posix_acl *acl = posix_acl_alloc(1, 0);
		set_cached_acl(ip, ACL_TYPE_ACCESS, acl);
		forget_cached_acl(ip, ACL_TYPE_ACCESS);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_CACHED_ACL_USABLE, 1,
		    [posix_acl_release() is usable])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API change,
dnl # posix_acl_chmod_masq() is not exported anymore and posix_acl_chmod()
dnl # was introduced to replace it.
dnl #
dnl # 3.14 API change,
dnl # posix_acl_chmod() is changed to __posix_acl_chmod()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_CHMOD], [
	AC_MSG_CHECKING([whether posix_acl_chmod exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		posix_acl_chmod(NULL, 0, 0)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_CHMOD, 1, [posix_acl_chmod() exists])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether __posix_acl_chmod exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		__posix_acl_chmod(NULL, 0, 0)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE___POSIX_ACL_CHMOD, 1, [__posix_acl_chmod() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API change,
dnl # posix_acl_equiv_mode now wants an umode_t* instead of a mode_t*
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T], [
	AC_MSG_CHECKING([whether posix_acl_equiv_mode() wants umode_t])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		umode_t tmp;
		posix_acl_equiv_mode(NULL,&tmp);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_EQUIV_MODE_UMODE_T, 1,
		    [ posix_acl_equiv_mode wants umode_t*])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.8 API change,
dnl # The function posix_acl_valid now must be passed a namespace.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_VALID_WITH_NS], [
	AC_MSG_CHECKING([whether posix_acl_valid() wants user namespace])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		struct user_namespace *user_ns = NULL;
		const struct posix_acl *acl = NULL;
		int error;

		error = posix_acl_valid(user_ns, acl);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_VALID_WITH_NS, 1,
		    [posix_acl_valid() wants user namespace])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.27 API change,
dnl # Check if inode_operations contains the function permission
dnl # and expects the nameidata structure to have been removed.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION], [
	AC_MSG_CHECKING([whether iops->permission() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int permission_fn(struct inode *inode, int mask) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.permission = permission_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PERMISSION, 1, [iops->permission() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.26 API change,
dnl # Check if inode_operations contains the function permission
dnl # and expects the nameidata structure to be passed.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION_WITH_NAMEIDATA], [
	AC_MSG_CHECKING([whether iops->permission() wants nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int permission_fn(struct inode *inode, int mask,
		    struct nameidata *nd) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.permission = permission_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PERMISSION, 1, [iops->permission() exists])
		AC_DEFINE(HAVE_PERMISSION_WITH_NAMEIDATA, 1,
		    [iops->permission() with nameidata exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.32 API change,
dnl # Check if inode_operations contains the function check_acl
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL], [
	AC_MSG_CHECKING([whether iops->check_acl() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int check_acl_fn(struct inode *inode, int mask) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.check_acl = check_acl_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CHECK_ACL, 1, [iops->check_acl() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.38 API change,
dnl # The function check_acl gained a new parameter: flags
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL_WITH_FLAGS], [
	AC_MSG_CHECKING([whether iops->check_acl() wants flags])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int check_acl_fn(struct inode *inode, int mask,
		    unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.check_acl = check_acl_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CHECK_ACL, 1, [iops->check_acl() exists])
		AC_DEFINE(HAVE_CHECK_ACL_WITH_FLAGS, 1,
		    [iops->check_acl() wants flags])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API change,
dnl # Check if inode_operations contains the function get_acl
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_GET_ACL], [
	AC_MSG_CHECKING([whether iops->get_acl() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		struct posix_acl *get_acl_fn(struct inode *inode, int type)
		    { return NULL; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.get_acl = get_acl_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_ACL, 1, [iops->get_acl() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.14 API change,
dnl # Check if inode_operations contains the function set_acl
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_SET_ACL], [
	AC_MSG_CHECKING([whether iops->set_acl() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int set_acl_fn(struct inode *inode, struct posix_acl *acl, int type)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.set_acl = set_acl_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_ACL, 1, [iops->set_acl() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.7 API change,
dnl # The kernel get_acl will now check cache before calling i_op->get_acl and
dnl # do set_cached_acl after that, so i_op->get_acl don't need to do that
dnl # anymore.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GET_ACL_HANDLE_CACHE], [
	AC_MSG_CHECKING([whether uncached_acl_sentinel() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		void *sentinel __attribute__ ((unused)) = uncached_acl_sentinel(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_GET_ACL_HANDLE_CACHE, 1, [uncached_acl_sentinel() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
