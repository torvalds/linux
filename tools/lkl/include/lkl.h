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

#endif
