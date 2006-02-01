#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#ifdef	__KERNEL__
#include <linux/config.h>

#define BITS_TO_LONGS(bits) \
	(((bits)+BITS_PER_LONG-1)/BITS_PER_LONG)
#define DECLARE_BITMAP(name,bits) \
	unsigned long name[BITS_TO_LONGS(bits)]

#define BITS_PER_BYTE 8
#endif

#include <linux/posix_types.h>
#include <asm/types.h>

#ifndef __KERNEL_STRICT_NAMES

typedef __u32 __kernel_dev_t;

typedef __kernel_fd_set		fd_set;
typedef __kernel_dev_t		dev_t;
typedef __kernel_ino_t		ino_t;
typedef __kernel_mode_t		mode_t;
typedef __kernel_nlink_t	nlink_t;
typedef __kernel_off_t		off_t;
typedef __kernel_pid_t		pid_t;
typedef __kernel_daddr_t	daddr_t;
typedef __kernel_key_t		key_t;
typedef __kernel_suseconds_t	suseconds_t;
typedef __kernel_timer_t	timer_t;
typedef __kernel_clockid_t	clockid_t;
typedef __kernel_mqd_t		mqd_t;

#ifdef __KERNEL__
typedef __kernel_uid32_t	uid_t;
typedef __kernel_gid32_t	gid_t;
typedef __kernel_uid16_t        uid16_t;
typedef __kernel_gid16_t        gid16_t;

#ifdef CONFIG_UID16
/* This is defined by include/asm-{arch}/posix_types.h */
typedef __kernel_old_uid_t	old_uid_t;
typedef __kernel_old_gid_t	old_gid_t;
#endif /* CONFIG_UID16 */

/* libc5 includes this file to define uid_t, thus uid_t can never change
 * when it is included by non-kernel code
 */
#else
typedef __kernel_uid_t		uid_t;
typedef __kernel_gid_t		gid_t;
#endif /* __KERNEL__ */

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __kernel_loff_t		loff_t;
#endif

/*
 * The following typedefs are also protected by individual ifdefs for
 * historical reasons:
 */
#ifndef _SIZE_T
#define _SIZE_T
typedef __kernel_size_t		size_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
typedef __kernel_ssize_t	ssize_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef __kernel_ptrdiff_t	ptrdiff_t;
#endif

#ifndef _TIME_T
#define _TIME_T
typedef __kernel_time_t		time_t;
#endif

#ifndef _CLOCK_T
#define _CLOCK_T
typedef __kernel_clock_t	clock_t;
#endif

#ifndef _CADDR_T
#define _CADDR_T
typedef __kernel_caddr_t	caddr_t;
#endif

/* bsd */
typedef unsigned char		u_char;
typedef unsigned short		u_short;
typedef unsigned int		u_int;
typedef unsigned long		u_long;

/* sysv */
typedef unsigned char		unchar;
typedef unsigned short		ushort;
typedef unsigned int		uint;
typedef unsigned long		ulong;

#ifndef __BIT_TYPES_DEFINED__
#define __BIT_TYPES_DEFINED__

typedef		__u8		u_int8_t;
typedef		__s8		int8_t;
typedef		__u16		u_int16_t;
typedef		__s16		int16_t;
typedef		__u32		u_int32_t;
typedef		__s32		int32_t;

#endif /* !(__BIT_TYPES_DEFINED__) */

typedef		__u8		uint8_t;
typedef		__u16		uint16_t;
typedef		__u32		uint32_t;

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef		__u64		uint64_t;
typedef		__u64		u_int64_t;
typedef		__s64		int64_t;
#endif

/* this is a special 64bit data type that is 8-byte aligned */
#define aligned_u64 unsigned long long __attribute__((aligned(8)))

/*
 * The type used for indexing onto a disc or disc partition.
 * If required, asm/types.h can override it and define
 * HAVE_SECTOR_T
 */
#ifndef HAVE_SECTOR_T
typedef unsigned long sector_t;
#endif

/*
 * The type of an index into the pagecache.  Use a #define so asm/types.h
 * can override it.
 */
#ifndef pgoff_t
#define pgoff_t unsigned long
#endif

#endif /* __KERNEL_STRICT_NAMES */

/*
 * Below are truly Linux-specific types that should never collide with
 * any application/library that wants linux/types.h.
 */

#ifdef __CHECKER__
#define __bitwise__ __attribute__((bitwise))
#else
#define __bitwise__
#endif
#ifdef __CHECK_ENDIAN__
#define __bitwise __bitwise__
#else
#define __bitwise
#endif

typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;
#endif

#ifdef __KERNEL__
typedef unsigned __bitwise__ gfp_t;
#endif

struct ustat {
	__kernel_daddr_t	f_tfree;
	__kernel_ino_t		f_tinode;
	char			f_fname[6];
	char			f_fpack[6];
};

#endif /* _LINUX_TYPES_H */
