dnl #
dnl # Linux 4.14 API,
dnl #
dnl # The bio_set_dev() helper was introduced as part of the transition
dnl # to have struct gendisk in struct bio. 
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_SET_DEV], [
	AC_MSG_CHECKING([whether bio_set_dev() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
		#include <linux/fs.h>
	],[
		struct block_device *bdev = NULL;
		struct bio *bio = NULL;
		bio_set_dev(bio, bdev);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_DEV, 1, [bio_set_dev() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
