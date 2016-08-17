dnl #
dnl # 4.8 API change
dnl # The rw argument has been removed from submit_bio/submit_bio_wait.
dnl # Callers are now expected to set bio->bi_rw instead of passing it in.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SUBMIT_BIO], [
	AC_MSG_CHECKING([whether submit_bio() wants 1 arg])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		blk_qc_t blk_qc;
		struct bio *bio = NULL;
		blk_qc = submit_bio(bio);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_1ARG_SUBMIT_BIO, 1, [submit_bio() wants 1 arg])
	],[
		AC_MSG_RESULT(no)
	])
])
