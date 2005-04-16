/*
 *  linux/include/asm-arm/posix_types.h
 *
 *  Copyright (C) 1996-1998 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ARCH_ARM_POSIX_TYPES_H
#define __ARCH_ARM_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned long		__kernel_ino_t;
typedef unsigned short		__kernel_mode_t;
typedef unsigned short		__kernel_nlink_t;
typedef long			__kernel_off_t;
typedef int			__kernel_pid_t;
typedef unsigned short		__kernel_ipc_pid_t;
typedef unsigned short		__kernel_uid_t;
typedef unsigned short		__kernel_gid_t;
typedef unsigned int		__kernel_size_t;
typedef int			__kernel_ssize_t;
typedef int			__kernel_ptrdiff_t;
typedef long			__kernel_time_t;
typedef long			__kernel_suseconds_t;
typedef long			__kernel_clock_t;
typedef int			__kernel_timer_t;
typedef int			__kernel_clockid_t;
typedef int			__kernel_daddr_t;
typedef char *			__kernel_caddr_t;
typedef unsigned short		__kernel_uid16_t;
typedef unsigned short		__kernel_gid16_t;
typedef unsigned int		__kernel_uid32_t;
typedef unsigned int		__kernel_gid32_t;

typedef unsigned short		__kernel_old_uid_t;
typedef unsigned short		__kernel_old_gid_t;
typedef unsigned short		__kernel_old_dev_t;

#ifdef __GNUC__
typedef long long		__kernel_loff_t;
#endif

typedef struct {
#if defined(__KERNEL__) || defined(__USE_ALL)
	int	val[2];
#else /* !defined(__KERNEL__) && !defined(__USE_ALL) */
	int	__val[2];
#endif /* !defined(__KERNEL__) && !defined(__USE_ALL) */
} __kernel_fsid_t;

#if defined(__KERNEL__) || !defined(__GLIBC__) || (__GLIBC__ < 2)

#undef	__FD_SET
#define __FD_SET(fd, fdsetp) \
		(((fd_set *)(fdsetp))->fds_bits[(fd) >> 5] |= (1<<((fd) & 31)))

#undef	__FD_CLR
#define __FD_CLR(fd, fdsetp) \
		(((fd_set *)(fdsetp))->fds_bits[(fd) >> 5] &= ~(1<<((fd) & 31)))

#undef	__FD_ISSET
#define __FD_ISSET(fd, fdsetp) \
		((((fd_set *)(fdsetp))->fds_bits[(fd) >> 5] & (1<<((fd) & 31))) != 0)

#undef	__FD_ZERO
#define __FD_ZERO(fdsetp) \
		(memset (fdsetp, 0, sizeof (*(fd_set *)(fdsetp))))

#endif

#endif
