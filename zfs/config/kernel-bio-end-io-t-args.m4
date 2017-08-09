dnl #
dnl # 4.3 API change
dnl # Error argument dropped from bio_endio in favor of newly introduced
dnl # bio->bi_error. This also replaces bio->bi_flags value BIO_UPTODATE.
dnl # Introduced by torvalds/linux@4246a0b63bd8f56a1469b12eafeb875b1041a451
dnl # ("block: add a bi_error field to struct bio").
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_END_IO_T_ARGS], [
	AC_MSG_CHECKING([whether bio_end_io_t wants 1 arg])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>

		void wanted_end_io(struct bio *bio) { return; }

		bio_end_io_t *end_io __attribute__ ((unused)) = wanted_end_io;
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_1ARG_BIO_END_IO_T, 1,
			  [bio_end_io_t wants 1 arg])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.13 API change
dnl # The bio->bi_error field was replaced with bio->bi_status which is an
dnl # enum which describes all possible error types.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_BI_STATUS], [
	AC_MSG_CHECKING([whether bio->bi_status exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		struct bio bio __attribute__ ((unused));
		blk_status_t status __attribute__ ((unused)) = BLK_STS_OK;

		bio.bi_status = status;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_BI_STATUS, 1, [bio->bi_status exists])
	],[
		AC_MSG_RESULT(no)
	])
])
