/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_COMPAT_H
#define __ASM_GENERIC_COMPAT_H

#ifndef COMPAT_USER_HZ
#define COMPAT_USER_HZ		100
#endif

#ifndef COMPAT_RLIM_INFINITY
#define COMPAT_RLIM_INFINITY	0xffffffff
#endif

#ifndef COMPAT_OFF_T_MAX
#define COMPAT_OFF_T_MAX	0x7fffffff
#endif

#ifndef compat_arg_u64
#ifndef CONFIG_CPU_BIG_ENDIAN
#define compat_arg_u64(name)		u32  name##_lo, u32  name##_hi
#define compat_arg_u64_dual(name)	u32, name##_lo, u32, name##_hi
#else
#define compat_arg_u64(name)		u32  name##_hi, u32  name##_lo
#define compat_arg_u64_dual(name)	u32, name##_hi, u32, name##_lo
#endif
#define compat_arg_u64_glue(name)	(((u64)name##_lo & 0xffffffffUL) | \
					 ((u64)name##_hi << 32))
#endif /* compat_arg_u64 */

/* These types are common across all compat ABIs */
typedef u32 compat_size_t;
typedef s32 compat_ssize_t;
typedef s32 compat_clock_t;
typedef s32 compat_pid_t;
typedef u32 compat_ino_t;
typedef s32 compat_off_t;
typedef s64 compat_loff_t;
typedef s32 compat_daddr_t;
typedef s32 compat_timer_t;
typedef s32 compat_key_t;
typedef s16 compat_short_t;
typedef s32 compat_int_t;
typedef s32 compat_long_t;
typedef u16 compat_ushort_t;
typedef u32 compat_uint_t;
typedef u32 compat_ulong_t;
typedef u32 compat_uptr_t;
typedef u32 compat_caddr_t;
typedef u32 compat_aio_context_t;
typedef u32 compat_old_sigset_t;

#ifndef __compat_uid_t
typedef u32 __compat_uid_t;
typedef u32 __compat_gid_t;
#endif

#ifndef __compat_uid32_t
typedef u32 __compat_uid32_t;
typedef u32 __compat_gid32_t;
#endif

#ifndef compat_mode_t
typedef u32 compat_mode_t;
#endif

#ifdef CONFIG_COMPAT_FOR_U64_ALIGNMENT
typedef s64 __attribute__((aligned(4))) compat_s64;
typedef u64 __attribute__((aligned(4))) compat_u64;
#else
typedef s64 compat_s64;
typedef u64 compat_u64;
#endif

#ifndef _COMPAT_NSIG
typedef u32 compat_sigset_word;
#define _COMPAT_NSIG _NSIG
#define _COMPAT_NSIG_BPW 32
#endif

#ifndef compat_dev_t
typedef u32 compat_dev_t;
#endif

#ifndef compat_ipc_pid_t
typedef s32 compat_ipc_pid_t;
#endif

#ifndef compat_fsid_t
typedef __kernel_fsid_t	compat_fsid_t;
#endif

#ifndef compat_statfs
struct compat_statfs {
	compat_int_t	f_type;
	compat_int_t	f_bsize;
	compat_int_t	f_blocks;
	compat_int_t	f_bfree;
	compat_int_t	f_bavail;
	compat_int_t	f_files;
	compat_int_t	f_ffree;
	compat_fsid_t	f_fsid;
	compat_int_t	f_namelen;
	compat_int_t	f_frsize;
	compat_int_t	f_flags;
	compat_int_t	f_spare[4];
};
#endif

#ifndef compat_ipc64_perm
struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid32_t uid;
	__compat_gid32_t gid;
	__compat_uid32_t cuid;
	__compat_gid32_t cgid;
	compat_mode_t	mode;
	unsigned char	__pad1[4 - sizeof(compat_mode_t)];
	compat_ushort_t	seq;
	compat_ushort_t	__pad2;
	compat_ulong_t	unused1;
	compat_ulong_t	unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	compat_ulong_t sem_otime;
	compat_ulong_t sem_otime_high;
	compat_ulong_t sem_ctime;
	compat_ulong_t sem_ctime_high;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused3;
	compat_ulong_t __unused4;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
	compat_ulong_t msg_stime;
	compat_ulong_t msg_stime_high;
	compat_ulong_t msg_rtime;
	compat_ulong_t msg_rtime_high;
	compat_ulong_t msg_ctime;
	compat_ulong_t msg_ctime_high;
	compat_ulong_t msg_cbytes;
	compat_ulong_t msg_qnum;
	compat_ulong_t msg_qbytes;
	compat_pid_t   msg_lspid;
	compat_pid_t   msg_lrpid;
	compat_ulong_t __unused4;
	compat_ulong_t __unused5;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	compat_size_t  shm_segsz;
	compat_ulong_t shm_atime;
	compat_ulong_t shm_atime_high;
	compat_ulong_t shm_dtime;
	compat_ulong_t shm_dtime_high;
	compat_ulong_t shm_ctime;
	compat_ulong_t shm_ctime_high;
	compat_pid_t   shm_cpid;
	compat_pid_t   shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused4;
	compat_ulong_t __unused5;
};
#endif

#endif
