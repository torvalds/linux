dnl #
dnl # 3.5.0 API change
dnl # torvalds/linux@dbd5768f87ff6fb0a4fe09c4d7b6c4a24de99430 and
dnl # torvalds/linux@7994e6f7254354e03028a11f98a27bd67dace9f1 reworked
dnl # where inode_sync_wait() is called.
dnl #
dnl # Prior to these changes it would occur in end_writeback() but due
dnl # to various issues (described in the above commits) it has been
dnl # moved to evict().   This changes the ordering is which sync occurs
dnl # but otherwise doesn't impact the zpl implementation.
dnl #
dnl # The major impact here is the renaming of end_writeback() to
dnl # clear_inode().  However, care must be taken when detecting this
dnl # API change because as recently as 2.6.35 there was a clear_inode()
dnl # function.  However, it was made obsolete by the evict_inode() API
dnl # change at the same time.
dnl #
dnl # Therefore, to ensure we have the correct API we only allow the
dnl # clear_inode() compatibility code to be defined iff the evict_inode()
dnl # functionality is also detected.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CLEAR_INODE],
	[AC_MSG_CHECKING([whether clear_inode() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		clear_inode(NULL);
	], [clear_inode], [fs/inode.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CLEAR_INODE, 1, [clear_inode() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
