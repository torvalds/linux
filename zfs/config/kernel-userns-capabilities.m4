dnl #
dnl # 2.6.38 API change
dnl # ns_capable() was introduced
dnl #
AC_DEFUN([ZFS_AC_KERNEL_NS_CAPABLE], [
	AC_MSG_CHECKING([whether ns_capable exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/capability.h>
	],[
		ns_capable((struct user_namespace *)NULL, CAP_SYS_ADMIN);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_NS_CAPABLE, 1,
		    [ns_capable exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.39 API change
dnl # struct user_namespace was added to struct cred_t as
dnl # cred->user_ns member
dnl # Note that current_user_ns() was added in 2.6.28.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CRED_USER_NS], [
	AC_MSG_CHECKING([whether cred_t->user_ns exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
	],[
		struct cred cr;
		cr.user_ns = (struct user_namespace *)NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CRED_USER_NS, 1,
		    [cred_t->user_ns exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.4 API change
dnl # kuid_has_mapping() and kgid_has_mapping() were added to distinguish
dnl # between internal kernel uids/gids and user namespace uids/gids.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_KUID_HAS_MAPPING], [
	AC_MSG_CHECKING([whether kuid_has_mapping/kgid_has_mapping exist])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/uidgid.h>
	],[
		kuid_has_mapping((struct user_namespace *)NULL, KUIDT_INIT(0));
		kgid_has_mapping((struct user_namespace *)NULL, KGIDT_INIT(0));
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KUID_HAS_MAPPING, 1,
		    [kuid_has_mapping/kgid_has_mapping exist])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_USERNS_CAPABILITIES], [
	ZFS_AC_KERNEL_NS_CAPABLE
	ZFS_AC_KERNEL_CRED_USER_NS
	ZFS_AC_KERNEL_KUID_HAS_MAPPING
])
