dnl #
dnl # 3.19 API addition
dnl #
dnl # torvalds/linux@394ffa503bc40e32d7f54a9b817264e81ce131b4 allows us to
dnl # increment iostat counters without generic_make_request().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GENERIC_IO_ACCT_3ARG], [
	AC_MSG_CHECKING([whether 3 arg generic IO accounting symbols are available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/bio.h>

		void (*generic_start_io_acct_f)(int, unsigned long,
		    struct hd_struct *) = &generic_start_io_acct;
		void (*generic_end_io_acct_f)(int, struct hd_struct *,
		    unsigned long) = &generic_end_io_acct;
	], [
		generic_start_io_acct(0, 0, NULL);
		generic_end_io_acct(0, NULL, 0);
	], [generic_start_io_acct], [block/bio.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_IO_ACCT_3ARG, 1,
		    [generic_start_io_acct()/generic_end_io_acct() available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 4.14 API,
dnl #
dnl # generic_start_io_acct/generic_end_io_acct now require request_queue to be
dnl # provided. No functional changes, but preparation for inflight accounting 
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GENERIC_IO_ACCT_4ARG], [
	AC_MSG_CHECKING([whether 4 arg generic IO accounting symbols are available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/bio.h>

		void (*generic_start_io_acct_f)(struct request_queue *, int,
		    unsigned long, struct hd_struct *) = &generic_start_io_acct;
		void (*generic_end_io_acct_f)(struct request_queue *, int,
		    struct hd_struct *, unsigned long) = &generic_end_io_acct;
	], [
		generic_start_io_acct(NULL, 0, 0, NULL);
		generic_end_io_acct(NULL, 0, NULL, 0);
	], [generic_start_io_acct], [block/bio.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_IO_ACCT_4ARG, 1,
		    [generic_start_io_acct()/generic_end_io_acct() 4 arg available])
	], [
		AC_MSG_RESULT(no)
	])
])
