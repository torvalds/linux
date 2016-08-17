dnl #
dnl # Linux 4.8 API,
dnl #
dnl # The bio_op() helper was introduced as a replacement for explicitly
dnl # checking the bio->bi_rw flags.  The following checks are used to
dnl # detect if a specific operation is supported.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_REQ_OP_DISCARD], [
	AC_MSG_CHECKING([whether REQ_OP_DISCARD is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blk_types.h>
	],[
		int op __attribute__ ((unused)) = REQ_OP_DISCARD;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_DISCARD, 1,
		    [REQ_OP_DISCARD is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_REQ_OP_SECURE_ERASE], [
	AC_MSG_CHECKING([whether REQ_OP_SECURE_ERASE is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blk_types.h>
	],[
		int op __attribute__ ((unused)) = REQ_OP_SECURE_ERASE;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_SECURE_ERASE, 1,
		    [REQ_OP_SECURE_ERASE is defined])
	],[
		AC_MSG_RESULT(no)
	])
])


AC_DEFUN([ZFS_AC_KERNEL_REQ_OP_FLUSH], [
	AC_MSG_CHECKING([whether REQ_OP_FLUSH is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blk_types.h>
	],[
		int op __attribute__ ((unused)) = REQ_OP_FLUSH;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_FLUSH, 1,
		    [REQ_OP_FLUSH is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_BI_OPF], [
	AC_MSG_CHECKING([whether bio->bi_opf is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		struct bio bio __attribute__ ((unused));
		bio.bi_opf = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_BI_OPF, 1, [bio->bi_opf is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_HAVE_BIO_SET_OP_ATTRS], [
	AC_MSG_CHECKING([whether bio_set_op_attrs is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blk_types.h>
	],[
		struct bio *bio __attribute__ ((unused)) = NULL;

		bio_set_op_attrs(bio, 0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_OP_ATTRS, 1,
		    [bio_set_op_attrs is available])
	],[
		AC_MSG_RESULT(no)
	])
])
