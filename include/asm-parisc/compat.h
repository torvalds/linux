#ifndef _ASM_PARISC_COMPAT_H
#define _ASM_PARISC_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>

#define COMPAT_USER_HZ 100

typedef u32	compat_size_t;
typedef s32	compat_ssize_t;
typedef s32	compat_time_t;
typedef s32	compat_clock_t;
typedef s32	compat_pid_t;
typedef u32	__compat_uid_t;
typedef u32	__compat_gid_t;
typedef u32	__compat_uid32_t;
typedef u32	__compat_gid32_t;
typedef u16	compat_mode_t;
typedef u32	compat_ino_t;
typedef u32	compat_dev_t;
typedef s32	compat_off_t;
typedef s64	compat_loff_t;
typedef u16	compat_nlink_t;
typedef u16	compat_ipc_pid_t;
typedef s32	compat_daddr_t;
typedef u32	compat_caddr_t;
typedef s32	compat_timer_t;

typedef s32	compat_int_t;
typedef s32	compat_long_t;
typedef u32	compat_uint_t;
typedef u32	compat_ulong_t;

struct compat_timespec {
	compat_time_t		tv_sec;
	s32			tv_nsec;
};

struct compat_timeval {
	compat_time_t		tv_sec;
	s32			tv_usec;
};

struct compat_stat {
	compat_dev_t		st_dev;	/* dev_t is 32 bits on parisc */
	compat_ino_t		st_ino;	/* 32 bits */
	compat_mode_t		st_mode;	/* 16 bits */
	compat_nlink_t  	st_nlink;	/* 16 bits */
	u16			st_reserved1;	/* old st_uid */
	u16			st_reserved2;	/* old st_gid */
	compat_dev_t		st_rdev;
	compat_off_t		st_size;
	compat_time_t		st_atime;
	u32			st_atime_nsec;
	compat_time_t		st_mtime;
	u32			st_mtime_nsec;
	compat_time_t		st_ctime;
	u32			st_ctime_nsec;
	s32			st_blksize;
	s32			st_blocks;
	u32			__unused1;	/* ACL stuff */
	compat_dev_t		__unused2;	/* network */
	compat_ino_t		__unused3;	/* network */
	u32			__unused4;	/* cnodes */
	u16			__unused5;	/* netsite */
	short			st_fstype;
	compat_dev_t		st_realdev;
	u16			st_basemode;
	u16			st_spareshort;
	__compat_uid32_t	st_uid;
	__compat_gid32_t	st_gid;
	u32			st_spare4[3];
};

struct compat_flock {
	short			l_type;
	short			l_whence;
	compat_off_t		l_start;
	compat_off_t		l_len;
	compat_pid_t		l_pid;
};

struct compat_flock64 {
	short			l_type;
	short			l_whence;
	compat_loff_t		l_start;
	compat_loff_t		l_len;
	compat_pid_t		l_pid;
};

struct compat_statfs {
	s32		f_type;
	s32		f_bsize;
	s32		f_blocks;
	s32		f_bfree;
	s32		f_bavail;
	s32		f_files;
	s32		f_ffree;
	__kernel_fsid_t	f_fsid;
	s32		f_namelen;
	s32		f_frsize;
	s32		f_spare[5];
};

struct compat_sigcontext {
	compat_int_t sc_flags;
	compat_int_t sc_gr[32]; /* PSW in sc_gr[0] */
	u64 sc_fr[32];
	compat_int_t sc_iasq[2];
	compat_int_t sc_iaoq[2];
	compat_int_t sc_sar; /* cr11 */
};

#define COMPAT_RLIM_INFINITY 0xffffffff

typedef u32		compat_old_sigset_t;	/* at least 32 bits */

#define _COMPAT_NSIG		64
#define _COMPAT_NSIG_BPW	32

typedef u32		compat_sigset_word;

#define COMPAT_OFF_T_MAX	0x7fffffff
#define COMPAT_LOFF_T_MAX	0x7fffffffffffffffL

/*
 * A pointer passed in from user mode. This should not
 * be used for syscall parameters, just declare them
 * as pointers because the syscall entry code will have
 * appropriately comverted them already.
 */
typedef	u32		compat_uptr_t;

static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)uptr;
}

static __inline__ void __user *compat_alloc_user_space(long len)
{
	struct pt_regs *regs = &current->thread.regs;
	return (void __user *)regs->gr[30];
}

#endif /* _ASM_PARISC_COMPAT_H */
