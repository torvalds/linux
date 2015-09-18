#ifndef _LKL_H
#define _LKL_H

#include <lkl/asm/unistd.h>

/**
 * lkl_sys_lseek - wrapper for lkl_sys_llseek
 */
static inline long lkl_sys_lseek(unsigned int fd, __lkl__kernel_loff_t off,
				 __lkl__kernel_loff_t *res, unsigned int whence)
{
	return lkl_sys_llseek(fd, off >> 32, off & 0xffffffff, res, whence);
}

/**
 * lkl_strerror - returns a string describing the given error code
 *
 * @err - error code
 * @returns - string for the given error code
 */
const char *lkl_strerror(int err);

/**
 * lkl_disk_backstore - host dependend disk backstore
 *
 * @fd - an open file descriptor that can be used by preadv/pwritev; used by
 * POSIX hosts
 */
union lkl_disk_backstore {
	int fd;
};

/**
 * lkl_disk_add - add a new disk
 *
 * Must be called before calling lkl_start_kernel.
 *
 * @backstore - the disk backstore
 * @returns a disk id (0 is valid) or a strictly negative value in case of error
 */
int lkl_disk_add(union lkl_disk_backstore backstore);

#endif
