dnl #
dnl # User namespaces, use kuid_t in place of uid_t
dnl # where available. Not strictly a user namespaces thing
dnl # but it should prevent surprises
dnl #
AC_DEFUN([SPL_AC_KUIDGID_T], [
	AC_MSG_CHECKING([whether kuid_t/kgid_t is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/uidgid.h>
	], [
		kuid_t userid = KUIDT_INIT(0);
		kgid_t groupid = KGIDT_INIT(0);
	],[
		SPL_LINUX_TRY_COMPILE([
			#include <linux/uidgid.h>
		], [
			kuid_t userid = 0;
			kgid_t groupid = 0;
		],[
			AC_MSG_RESULT(yes; optional)
		],[
			AC_MSG_RESULT(yes; mandatory)
			AC_DEFINE(HAVE_KUIDGID_T, 1, [kuid_t/kgid_t in use])
		])
	],[
		AC_MSG_RESULT(no)
	])
])
