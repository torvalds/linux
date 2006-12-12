#ifndef _ASM_COMPAT_H
#define _ASM_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <asm/page.h>
#include <asm/ptrace.h>

#define COMPAT_USER_HZ	100

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;
typedef s32		compat_suseconds_t;

typedef s32		compat_pid_t;
typedef s32		__compat_uid_t;
typedef s32		__compat_gid_t;
typedef __compat_uid_t	__compat_uid32_t;
typedef __compat_gid_t	__compat_gid32_t;
typedef u32		compat_mode_t;
typedef u32		compat_ino_t;
typedef u32		compat_dev_t;
typedef s32		compat_off_t;
typedef s64		compat_loff_t;
typedef u32		compat_nlink_t;
typedef s32		compat_ipc_pid_t;
typedef s32		compat_daddr_t;
typedef s32		compat_caddr_t;
typedef struct {
	s32	val[2];
} compat_fsid_t;
typedef s32		compat_timer_t;
typedef s32		compat_key_t;

typedef s32		compat_int_t;
typedef s32		compat_long_t;
typedef u32		compat_uint_t;
typedef u32		compat_ulong_t;

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

struct compat_stat {
	compat_dev_t	st_dev;
	s32		st_pad1[3];
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	__compat_uid_t	st_uid;
	__compat_gid_t	st_gid;
	compat_dev_t	st_rdev;
	s32		st_pad2[2];
	compat_off_t	st_size;
	s32		st_pad3;
	compat_time_t	st_atime;
	s32		st_atime_nsec;
	compat_time_t	st_mtime;
	s32		st_mtime_nsec;
	compat_time_t	st_ctime;
	s32		st_ctime_nsec;
	s32		st_blksize;
	s32		st_blocks;
	s32		st_pad4[14];
};

struct compat_flock {
	short		l_type;
	short		l_whence;
	compat_off_t	l_start;
	compat_off_t	l_len;
	s32		l_sysid;
	compat_pid_t	l_pid;
	short		__unused;
	s32		pad[4];
};

#define F_GETLK64	33
#define F_SETLK64	34
#define F_SETLKW64	35

struct compat_flock64 {
	short		l_type;
	short		l_whence;
	compat_loff_t	l_start;
	compat_loff_t	l_len;
	compat_pid_t	l_pid;
};

struct compat_statfs {
	int		f_type;
	int		f_bsize;
	int		f_frsize;
	int		f_blocks;
	int		f_bfree;
	int		f_files;
	int		f_ffree;
	int		f_bavail;
	compat_fsid_t	f_fsid;
	int		f_namelen;
	int		f_spare[6];
};

#define COMPAT_RLIM_INFINITY	0x7fffffffUL

typedef u32		compat_old_sigset_t;	/* at least 32 bits */

#define _COMPAT_NSIG		128		/* Don't ask !$@#% ...  */
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
typedef u32		compat_uptr_t;

static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(long)uptr;
}

static inline compat_uptr_t ptr_to_compat(void __user *uptr)
{
	return (u32)(unsigned long)uptr;
}

static inline void __user *compat_alloc_user_space(long len)
{
	struct pt_regs *regs = (struct pt_regs *)
		((unsigned long) current_thread_info() + THREAD_SIZE - 32) - 1;

	return (void __user *) (regs->regs[29] - len);
}

struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid32_t uid;
	__compat_gid32_t gid;
	__compat_uid32_t cuid;
	__compat_gid32_t cgid;
	compat_mode_t mode;
	unsigned short seq;
	unsigned short __pad2;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	compat_time_t	sem_otime;
	compat_time_t	sem_ctime;
	compat_ulong_t	sem_nsems;
	compat_ulong_t	__unused1;
	compat_ulong_t	__unused2;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused1;
#endif
	compat_time_t	msg_stime;
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused1;
#endif
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused2;
#endif
	compat_time_t	msg_rtime;
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused2;
#endif
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused3;
#endif
	compat_time_t	msg_ctime;
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused3;
#endif
	compat_ulong_t	msg_cbytes;
	compat_ulong_t	msg_qnum;
	compat_ulong_t	msg_qbytes;
	compat_pid_t	msg_lspid;
	compat_pid_t	msg_lrpid;
	compat_ulong_t	__unused4;
	compat_ulong_t	__unused5;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	compat_size_t	shm_segsz;
	compat_time_t	shm_atime;
	compat_time_t	shm_dtime;
	compat_time_t	shm_ctime;
	compat_pid_t	shm_cpid;
	compat_pid_t	shm_lpid;
	compat_ulong_t	shm_nattch;
	compat_ulong_t	__unused1;
	compat_ulong_t	__unused2;
};

#endif /* _ASM_COMPAT_H */
