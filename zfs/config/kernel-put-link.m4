dnl #
dnl # Supported symlink APIs
dnl #
AC_DEFUN([ZFS_AC_KERNEL_PUT_LINK], [
	dnl #
	dnl # 4.5 API change
	dnl # get_link() uses delayed done, there is no put_link() interface.
	dnl #
	ZFS_LINUX_TRY_COMPILE([
		#if !defined(HAVE_GET_LINK_DELAYED)
		#error "Expecting get_link() delayed done"
		#endif
	],[
	],[
		AC_DEFINE(HAVE_PUT_LINK_DELAYED, 1, [iops->put_link() delayed])
	],[
		dnl #
		dnl # 4.2 API change
		dnl # This kernel retired the nameidata structure.
		dnl #
		AC_MSG_CHECKING([whether iops->put_link() passes cookie])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
			void put_link(struct inode *ip, void *cookie)
			    { return; }
			static struct inode_operations
			    iops __attribute__ ((unused)) = {
				.put_link = put_link,
			};
		],[
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_PUT_LINK_COOKIE, 1,
			    [iops->put_link() cookie])
		],[
			dnl #
			dnl # 2.6.32 API
			dnl #
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING(
			    [whether iops->put_link() passes nameidata])
			ZFS_LINUX_TRY_COMPILE([
				#include <linux/fs.h>
				void put_link(struct dentry *de, struct
				    nameidata *nd, void *ptr) { return; }
				static struct inode_operations
				    iops __attribute__ ((unused)) = {
					.put_link = put_link,
				};
			],[
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_PUT_LINK_NAMEIDATA, 1,
				    [iops->put_link() nameidata])
			],[
				AC_MSG_ERROR(no; please file a bug report)
			])
		])
	])
])
