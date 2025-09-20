/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * mm definition for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_MMAN_H
#define _NOLIBC_SYS_MMAN_H

#include "../arch.h"
#include "../sys.h"

#ifndef sys_mmap
static __attribute__((unused))
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
	       off_t offset)
{
	int n;

#if defined(__NR_mmap2)
	n = __NR_mmap2;
	offset >>= 12;
#else
	n = __NR_mmap;
#endif

	return (void *)my_syscall6(n, addr, length, prot, flags, fd, offset);
}
#endif

/* Note that on Linux, MAP_FAILED is -1 so we can use the generic __sysret()
 * which returns -1 upon error and still satisfy user land that checks for
 * MAP_FAILED.
 */

static __attribute__((unused))
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	void *ret = sys_mmap(addr, length, prot, flags, fd, offset);

	if ((unsigned long)ret >= -4095UL) {
		SET_ERRNO(-(long)ret);
		ret = MAP_FAILED;
	}
	return ret;
}

static __attribute__((unused))
void *sys_mremap(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address)
{
	return (void *)my_syscall5(__NR_mremap, old_address, old_size,
				   new_size, flags, new_address);
}

static __attribute__((unused))
void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address)
{
	void *ret = sys_mremap(old_address, old_size, new_size, flags, new_address);

	if ((unsigned long)ret >= -4095UL) {
		SET_ERRNO(-(long)ret);
		ret = MAP_FAILED;
	}
	return ret;
}

static __attribute__((unused))
int sys_munmap(void *addr, size_t length)
{
	return my_syscall2(__NR_munmap, addr, length);
}

static __attribute__((unused))
int munmap(void *addr, size_t length)
{
	return __sysret(sys_munmap(addr, length));
}

#endif /* _NOLIBC_SYS_MMAN_H */
