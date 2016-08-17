dnl #
dnl # Supported get_link() interfaces checked newest to oldest.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FOLLOW_LINK], [
	dnl #
	dnl # 4.2 API change
	dnl # - This kernel retired the nameidata structure.
	dnl #
	AC_MSG_CHECKING([whether iops->follow_link() passes cookie])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		const char *follow_link(struct dentry *de,
		    void **cookie) { return "symlink"; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.follow_link = follow_link,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FOLLOW_LINK_COOKIE, 1,
		    [iops->follow_link() cookie])
	],[
		dnl #
		dnl # 2.6.32 API
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING(
		   [whether iops->follow_link() passes nameidata])
		ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
			void *follow_link(struct dentry *de, struct
			    nameidata *nd) { return (void *)NULL; }
			static struct inode_operations
			    iops __attribute__ ((unused)) = {
				.follow_link = follow_link,
			};
		],[
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_FOLLOW_LINK_NAMEIDATA, 1,
			          [iops->follow_link() nameidata])
		],[
                        AC_MSG_ERROR(no; please file a bug report)
		])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GET_LINK], [
	dnl #
	dnl # 4.5 API change
	dnl # The get_link interface has added a delayed done call and
	dnl # used it to retire the put_link() interface.
	dnl #
	AC_MSG_CHECKING([whether iops->get_link() passes delayed])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		const char *get_link(struct dentry *de, struct inode *ip,
		    struct delayed_call *done) { return "symlink"; }
		static struct inode_operations
		     iops __attribute__ ((unused)) = {
			.get_link = get_link,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_LINK_DELAYED, 1,
		    [iops->get_link() delayed])
	],[
		dnl #
		dnl # 4.5 API change
		dnl # The follow_link() interface has been replaced by
		dnl # get_link() which behaves the same as before except:
		dnl # - An inode is passed as a separate argument
		dnl # - When called in RCU mode a NULL dentry is passed.
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether iops->get_link() passes cookie])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
			const char *get_link(struct dentry *de, struct
			    inode *ip, void **cookie) { return "symlink"; }
			static struct inode_operations
			     iops __attribute__ ((unused)) = {
				.get_link = get_link,
			};
		],[
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_GET_LINK_COOKIE, 1,
			    [iops->get_link() cookie])
		],[
			dnl #
			dnl # Check for the follow_link APIs.
			dnl #
			AC_MSG_RESULT(no)
			ZFS_AC_KERNEL_FOLLOW_LINK
		])
	])
])
