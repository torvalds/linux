dnl #
dnl # 4.9 API change
dnl # group_info changed from 2d array via >blocks to 1d array via ->gid
dnl #
AC_DEFUN([SPL_AC_GROUP_INFO_GID], [
	AC_MSG_CHECKING([whether group_info->gid exists])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
	],[
		struct group_info *gi = groups_alloc(1);
		gi->gid[0] = KGIDT_INIT(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GROUP_INFO_GID, 1, [group_info->gid exists])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
