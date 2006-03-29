#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
/*
 * These are the type definitions for the architecture specific
 * syscall compatibility layer.
 */
#include <linux/config.h>

#ifdef CONFIG_COMPAT

#include <linux/stat.h>
#include <linux/param.h>	/* for HZ */
#include <linux/sem.h>

#include <asm/compat.h>
#include <asm/siginfo.h>

#define compat_jiffies_to_clock_t(x)	\
		(((unsigned long)(x) * COMPAT_USER_HZ) / HZ)

typedef __compat_uid32_t	compat_uid_t;
typedef __compat_gid32_t	compat_gid_t;

struct rusage;

struct compat_itimerspec { 
	struct compat_timespec it_interval;
	struct compat_timespec it_value;
};

struct compat_utimbuf {
	compat_time_t		actime;
	compat_time_t		modtime;
};

struct compat_itimerval {
	struct compat_timeval	it_interval;
	struct compat_timeval	it_value;
};

struct compat_tms {
	compat_clock_t		tms_utime;
	compat_clock_t		tms_stime;
	compat_clock_t		tms_cutime;
	compat_clock_t		tms_cstime;
};

struct compat_timex {
	compat_uint_t modes;
	compat_long_t offset;
	compat_long_t freq;
	compat_long_t maxerror;
	compat_long_t esterror;
	compat_int_t status;
	compat_long_t constant;
	compat_long_t precision;
	compat_long_t tolerance;
	struct compat_timeval time;
	compat_long_t tick;
	compat_long_t ppsfreq;
	compat_long_t jitter;
	compat_int_t shift;
	compat_long_t stabil;
	compat_long_t jitcnt;
	compat_long_t calcnt;
	compat_long_t errcnt;
	compat_long_t stbcnt;

	compat_int_t :32; compat_int_t :32; compat_int_t :32; compat_int_t :32;
	compat_int_t :32; compat_int_t :32; compat_int_t :32; compat_int_t :32;
	compat_int_t :32; compat_int_t :32; compat_int_t :32; compat_int_t :32;
};

#define _COMPAT_NSIG_WORDS	(_COMPAT_NSIG / _COMPAT_NSIG_BPW)

typedef struct {
	compat_sigset_word	sig[_COMPAT_NSIG_WORDS];
} compat_sigset_t;

extern int cp_compat_stat(struct kstat *, struct compat_stat __user *);
extern int get_compat_timespec(struct timespec *, const struct compat_timespec __user *);
extern int put_compat_timespec(const struct timespec *, struct compat_timespec __user *);

struct compat_iovec {
	compat_uptr_t	iov_base;
	compat_size_t	iov_len;
};

struct compat_rlimit {
	compat_ulong_t	rlim_cur;
	compat_ulong_t	rlim_max;
};

struct compat_rusage {
	struct compat_timeval ru_utime;
	struct compat_timeval ru_stime;
	compat_long_t	ru_maxrss;
	compat_long_t	ru_ixrss;
	compat_long_t	ru_idrss;
	compat_long_t	ru_isrss;
	compat_long_t	ru_minflt;
	compat_long_t	ru_majflt;
	compat_long_t	ru_nswap;
	compat_long_t	ru_inblock;
	compat_long_t	ru_oublock;
	compat_long_t	ru_msgsnd;
	compat_long_t	ru_msgrcv;
	compat_long_t	ru_nsignals;
	compat_long_t	ru_nvcsw;
	compat_long_t	ru_nivcsw;
};

extern int put_compat_rusage(const struct rusage *, struct compat_rusage __user *);

struct compat_siginfo;

extern asmlinkage long compat_sys_waitid(int, compat_pid_t,
		struct compat_siginfo __user *, int,
		struct compat_rusage __user *);

struct compat_dirent {
	u32		d_ino;
	compat_off_t	d_off;
	u16		d_reclen;
	char		d_name[256];
};

typedef union compat_sigval {
	compat_int_t	sival_int;
	compat_uptr_t	sival_ptr;
} compat_sigval_t;

#define COMPAT_SIGEV_PAD_SIZE	((SIGEV_MAX_SIZE/sizeof(int)) - 3)

typedef struct compat_sigevent {
	compat_sigval_t sigev_value;
	compat_int_t sigev_signo;
	compat_int_t sigev_notify;
	union {
		compat_int_t _pad[COMPAT_SIGEV_PAD_SIZE];
		compat_int_t _tid;

		struct {
			compat_uptr_t _function;
			compat_uptr_t _attribute;
		} _sigev_thread;
	} _sigev_un;
} compat_sigevent_t;

struct compat_robust_list {
	compat_uptr_t			next;
};

struct compat_robust_list_head {
	struct compat_robust_list	list;
	compat_long_t			futex_offset;
	compat_uptr_t			list_op_pending;
};

extern void compat_exit_robust_list(struct task_struct *curr);

asmlinkage long
compat_sys_set_robust_list(struct compat_robust_list_head __user *head,
			   compat_size_t len);
asmlinkage long
compat_sys_get_robust_list(int pid, compat_uptr_t *head_ptr,
			   compat_size_t __user *len_ptr);

long compat_sys_semctl(int first, int second, int third, void __user *uptr);
long compat_sys_msgsnd(int first, int second, int third, void __user *uptr);
long compat_sys_msgrcv(int first, int second, int msgtyp, int third,
		int version, void __user *uptr);
long compat_sys_msgctl(int first, int second, void __user *uptr);
long compat_sys_shmat(int first, int second, compat_uptr_t third, int version,
		void __user *uptr);
long compat_sys_shmctl(int first, int second, void __user *uptr);
long compat_sys_semtimedop(int semid, struct sembuf __user *tsems,
		unsigned nsems, const struct compat_timespec __user *timeout);
asmlinkage long compat_sys_keyctl(u32 option,
			      u32 arg2, u32 arg3, u32 arg4, u32 arg5);

asmlinkage ssize_t compat_sys_readv(unsigned long fd,
		const struct compat_iovec __user *vec, unsigned long vlen);
asmlinkage ssize_t compat_sys_writev(unsigned long fd,
		const struct compat_iovec __user *vec, unsigned long vlen);

int compat_do_execve(char * filename, compat_uptr_t __user *argv,
	        compat_uptr_t __user *envp, struct pt_regs * regs);

asmlinkage long compat_sys_select(int n, compat_ulong_t __user *inp,
		compat_ulong_t __user *outp, compat_ulong_t __user *exp,
		struct compat_timeval __user *tvp);

#define BITS_PER_COMPAT_LONG    (8*sizeof(compat_long_t))

#define BITS_TO_COMPAT_LONGS(bits) \
	(((bits)+BITS_PER_COMPAT_LONG-1)/BITS_PER_COMPAT_LONG)

long compat_get_bitmap(unsigned long *mask, compat_ulong_t __user *umask,
		       unsigned long bitmap_size);
long compat_put_bitmap(compat_ulong_t __user *umask, unsigned long *mask,
		       unsigned long bitmap_size);
int copy_siginfo_from_user32(siginfo_t *to, struct compat_siginfo __user *from);
int copy_siginfo_to_user32(struct compat_siginfo __user *to, siginfo_t *from);
int get_compat_sigevent(struct sigevent *event,
		const struct compat_sigevent __user *u_event);

static inline int compat_timeval_compare(struct compat_timeval *lhs,
					struct compat_timeval *rhs)
{
	if (lhs->tv_sec < rhs->tv_sec)
		return -1;
	if (lhs->tv_sec > rhs->tv_sec)
		return 1;
	return lhs->tv_usec - rhs->tv_usec;
}

static inline int compat_timespec_compare(struct compat_timespec *lhs,
					struct compat_timespec *rhs)
{
	if (lhs->tv_sec < rhs->tv_sec)
		return -1;
	if (lhs->tv_sec > rhs->tv_sec)
		return 1;
	return lhs->tv_nsec - rhs->tv_nsec;
}

asmlinkage long compat_sys_adjtimex(struct compat_timex __user *utp);

#endif /* CONFIG_COMPAT */
#endif /* _LINUX_COMPAT_H */
