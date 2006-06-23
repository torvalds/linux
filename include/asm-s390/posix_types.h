/*
 *  include/asm-s390/posix_types.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/posix_types.h"
 */

#ifndef __ARCH_S390_POSIX_TYPES_H
#define __ARCH_S390_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef long            __kernel_off_t;
typedef int             __kernel_pid_t;
typedef unsigned long   __kernel_size_t;
typedef long            __kernel_time_t;
typedef long            __kernel_suseconds_t;
typedef long            __kernel_clock_t;
typedef int		__kernel_timer_t;
typedef int		__kernel_clockid_t;
typedef int             __kernel_daddr_t;
typedef char *          __kernel_caddr_t;
typedef unsigned short	__kernel_uid16_t;
typedef unsigned short	__kernel_gid16_t;

#ifdef __GNUC__
typedef long long       __kernel_loff_t;
#endif

#ifndef __s390x__

typedef unsigned long   __kernel_ino_t;
typedef unsigned short  __kernel_mode_t;
typedef unsigned short  __kernel_nlink_t;
typedef unsigned short  __kernel_ipc_pid_t;
typedef unsigned short  __kernel_uid_t;
typedef unsigned short  __kernel_gid_t;
typedef int             __kernel_ssize_t;
typedef int             __kernel_ptrdiff_t;
typedef unsigned int	__kernel_uid32_t;
typedef unsigned int	__kernel_gid32_t;
typedef unsigned short	__kernel_old_uid_t;
typedef unsigned short	__kernel_old_gid_t;
typedef unsigned short	__kernel_old_dev_t;

#else /* __s390x__ */

typedef unsigned int    __kernel_ino_t;
typedef unsigned int    __kernel_mode_t;
typedef unsigned int    __kernel_nlink_t;
typedef int             __kernel_ipc_pid_t;
typedef unsigned int    __kernel_uid_t;
typedef unsigned int    __kernel_gid_t;
typedef long            __kernel_ssize_t;
typedef long            __kernel_ptrdiff_t;
typedef unsigned long   __kernel_sigset_t;      /* at least 32 bits */
typedef __kernel_uid_t __kernel_old_uid_t;
typedef __kernel_gid_t __kernel_old_gid_t;
typedef __kernel_uid_t __kernel_uid32_t;
typedef __kernel_gid_t __kernel_gid32_t;
typedef unsigned short __kernel_old_dev_t;

#endif /* __s390x__ */

typedef struct {
#if defined(__KERNEL__) || defined(__USE_ALL)
        int     val[2];
#else                        /* !defined(__KERNEL__) && !defined(__USE_ALL)*/
        int     __val[2];
#endif                       /* !defined(__KERNEL__) && !defined(__USE_ALL)*/
} __kernel_fsid_t;


#ifdef __KERNEL__

#undef __FD_SET
static inline void __FD_SET(unsigned long fd, __kernel_fd_set *fdsetp)
{
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	fdsetp->fds_bits[_tmp] |= (1UL<<_rem);
}

#undef __FD_CLR
static inline void __FD_CLR(unsigned long fd, __kernel_fd_set *fdsetp)
{
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	fdsetp->fds_bits[_tmp] &= ~(1UL<<_rem);
}

#undef __FD_ISSET
static inline int __FD_ISSET(unsigned long fd, const __kernel_fd_set *fdsetp)
{
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	return (fdsetp->fds_bits[_tmp] & (1UL<<_rem)) != 0;
}

#undef  __FD_ZERO
#define __FD_ZERO(fdsetp) \
	((void) memset ((__ptr_t) (fdsetp), 0, sizeof (__kernel_fd_set)))

#endif     /* __KERNEL__ */

#endif
