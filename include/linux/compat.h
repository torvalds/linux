#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
/*
 * These are the type definitions for the architecture specific
 * syscall compatibility layer.
 */

#ifdef CONFIG_COMPAT

#include <linux/stat.h>
#include <linux/param.h>	/* for HZ */
#include <linux/sem.h>
#include <linux/socket.h>
#include <linux/if.h>
#include <linux/fs.h>
#include <linux/aio_abi.h>	/* for aio_context_t */

#include <asm/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>

#ifndef COMPAT_USE_64BIT_TIME
#define COMPAT_USE_64BIT_TIME 0
#endif

#ifndef __SC_DELOUSE
#define __SC_DELOUSE(t,v) ((t)(unsigned long)(v))
#endif

#define __SC_CCAST1(t1, a1)      __SC_DELOUSE(t1,a1)
#define __SC_CCAST2(t2, a2, ...) __SC_DELOUSE(t2,a2), __SC_CCAST1(__VA_ARGS__)
#define __SC_CCAST3(t3, a3, ...) __SC_DELOUSE(t3,a3), __SC_CCAST2(__VA_ARGS__)
#define __SC_CCAST4(t4, a4, ...) __SC_DELOUSE(t4,a4), __SC_CCAST3(__VA_ARGS__)
#define __SC_CCAST5(t5, a5, ...) __SC_DELOUSE(t5,a5), __SC_CCAST4(__VA_ARGS__)
#define __SC_CCAST6(t6, a6, ...) __SC_DELOUSE(t6,a6), __SC_CCAST5(__VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE1(name, ...) \
        COMPAT_SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE2(name, ...) \
	COMPAT_SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE3(name, ...) \
	COMPAT_SYSCALL_DEFINEx(3, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE4(name, ...) \
	COMPAT_SYSCALL_DEFINEx(4, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE5(name, ...) \
	COMPAT_SYSCALL_DEFINEx(5, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE6(name, ...) \
	COMPAT_SYSCALL_DEFINEx(6, _##name, __VA_ARGS__)

#ifdef CONFIG_HAVE_SYSCALL_WRAPPERS

#define COMPAT_SYSCALL_DEFINEx(x, name, ...)				\
	asmlinkage long compat_sys##name(__SC_DECL##x(__VA_ARGS__));	\
	static inline long C_SYSC##name(__SC_DECL##x(__VA_ARGS__));	\
	asmlinkage long compat_SyS##name(__SC_LONG##x(__VA_ARGS__))	\
	{								\
		return (long) C_SYSC##name(__SC_CCAST##x(__VA_ARGS__));	\
	}								\
	SYSCALL_ALIAS(compat_sys##name, compat_SyS##name);		\
	static inline long C_SYSC##name(__SC_DECL##x(__VA_ARGS__))

#else /* CONFIG_HAVE_SYSCALL_WRAPPERS */

#define COMPAT_SYSCALL_DEFINEx(x, name, ...)				\
	asmlinkage long compat_sys##name(__SC_DECL##x(__VA_ARGS__))

#endif /* CONFIG_HAVE_SYSCALL_WRAPPERS */

#ifndef compat_user_stack_pointer
#define compat_user_stack_pointer() current_user_stack_pointer()
#endif
#ifdef CONFIG_GENERIC_SIGALTSTACK
#ifndef compat_sigaltstack	/* we'll need that for MIPS */
typedef struct compat_sigaltstack {
	compat_uptr_t			ss_sp;
	int				ss_flags;
	compat_size_t			ss_size;
} compat_stack_t;
#endif
#endif

#define compat_jiffies_to_clock_t(x)	\
		(((unsigned long)(x) * COMPAT_USER_HZ) / HZ)

typedef __compat_uid32_t	compat_uid_t;
typedef __compat_gid32_t	compat_gid_t;

struct compat_sel_arg_struct;
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
	compat_int_t tai;

	compat_int_t:32; compat_int_t:32; compat_int_t:32; compat_int_t:32;
	compat_int_t:32; compat_int_t:32; compat_int_t:32; compat_int_t:32;
	compat_int_t:32; compat_int_t:32; compat_int_t:32;
};

#define _COMPAT_NSIG_WORDS	(_COMPAT_NSIG / _COMPAT_NSIG_BPW)

typedef struct {
	compat_sigset_word	sig[_COMPAT_NSIG_WORDS];
} compat_sigset_t;

/*
 * These functions operate strictly on struct compat_time*
 */
extern int get_compat_timespec(struct timespec *,
			       const struct compat_timespec __user *);
extern int put_compat_timespec(const struct timespec *,
			       struct compat_timespec __user *);
extern int get_compat_timeval(struct timeval *,
			      const struct compat_timeval __user *);
extern int put_compat_timeval(const struct timeval *,
			      struct compat_timeval __user *);
/*
 * These functions operate on 32- or 64-bit specs depending on
 * COMPAT_USE_64BIT_TIME, hence the void user pointer arguments and the
 * naming as compat_get/put_ rather than get/put_compat_.
 */
extern int compat_get_timespec(struct timespec *, const void __user *);
extern int compat_put_timespec(const struct timespec *, void __user *);
extern int compat_get_timeval(struct timeval *, const void __user *);
extern int compat_put_timeval(const struct timeval *, void __user *);

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

extern int put_compat_rusage(const struct rusage *,
			     struct compat_rusage __user *);

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

struct compat_ustat {
	compat_daddr_t		f_tfree;
	compat_ino_t		f_tinode;
	char			f_fname[6];
	char			f_fpack[6];
};

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

struct compat_ifmap {
	compat_ulong_t mem_start;
	compat_ulong_t mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
};

struct compat_if_settings {
	unsigned int type;	/* Type of physical device or protocol */
	unsigned int size;	/* Size of the data allocated by the caller */
	compat_uptr_t ifs_ifsu;	/* union of pointers */
};

struct compat_ifreq {
	union {
		char	ifrn_name[IFNAMSIZ];    /* if name, e.g. "en0" */
	} ifr_ifrn;
	union {
		struct	sockaddr ifru_addr;
		struct	sockaddr ifru_dstaddr;
		struct	sockaddr ifru_broadaddr;
		struct	sockaddr ifru_netmask;
		struct	sockaddr ifru_hwaddr;
		short	ifru_flags;
		compat_int_t	ifru_ivalue;
		compat_int_t	ifru_mtu;
		struct	compat_ifmap ifru_map;
		char	ifru_slave[IFNAMSIZ];   /* Just fits the size */
		char	ifru_newname[IFNAMSIZ];
		compat_caddr_t	ifru_data;
		struct	compat_if_settings ifru_settings;
	} ifr_ifru;
};

struct compat_ifconf {
	compat_int_t	ifc_len;                /* size of buffer */
	compat_caddr_t  ifcbuf;
};

struct compat_robust_list {
	compat_uptr_t			next;
};

struct compat_robust_list_head {
	struct compat_robust_list	list;
	compat_long_t			futex_offset;
	compat_uptr_t			list_op_pending;
};

struct compat_statfs;
struct compat_statfs64;
struct compat_old_linux_dirent;
struct compat_linux_dirent;
struct linux_dirent64;
struct compat_msghdr;
struct compat_mmsghdr;
struct compat_sysinfo;
struct compat_sysctl_args;
struct compat_kexec_segment;
struct compat_mq_attr;
struct compat_msgbuf;

extern void compat_exit_robust_list(struct task_struct *curr);

asmlinkage long
compat_sys_set_robust_list(struct compat_robust_list_head __user *head,
			   compat_size_t len);
asmlinkage long
compat_sys_get_robust_list(int pid, compat_uptr_t __user *head_ptr,
			   compat_size_t __user *len_ptr);

#ifdef CONFIG_ARCH_WANT_OLD_COMPAT_IPC
long compat_sys_semctl(int first, int second, int third, void __user *uptr);
long compat_sys_msgsnd(int first, int second, int third, void __user *uptr);
long compat_sys_msgrcv(int first, int second, int msgtyp, int third,
		int version, void __user *uptr);
long compat_sys_shmat(int first, int second, compat_uptr_t third, int version,
		void __user *uptr);
#else
long compat_sys_semctl(int semid, int semnum, int cmd, int arg);
long compat_sys_msgsnd(int msqid, struct compat_msgbuf __user *msgp,
		compat_ssize_t msgsz, int msgflg);
long compat_sys_msgrcv(int msqid, struct compat_msgbuf __user *msgp,
		compat_ssize_t msgsz, long msgtyp, int msgflg);
long compat_sys_shmat(int shmid, compat_uptr_t shmaddr, int shmflg);
#endif
long compat_sys_msgctl(int first, int second, void __user *uptr);
long compat_sys_shmctl(int first, int second, void __user *uptr);
long compat_sys_semtimedop(int semid, struct sembuf __user *tsems,
		unsigned nsems, const struct compat_timespec __user *timeout);
asmlinkage long compat_sys_keyctl(u32 option,
			      u32 arg2, u32 arg3, u32 arg4, u32 arg5);
asmlinkage long compat_sys_ustat(unsigned dev, struct compat_ustat __user *u32);

asmlinkage ssize_t compat_sys_readv(unsigned long fd,
		const struct compat_iovec __user *vec, unsigned long vlen);
asmlinkage ssize_t compat_sys_writev(unsigned long fd,
		const struct compat_iovec __user *vec, unsigned long vlen);
asmlinkage ssize_t compat_sys_preadv(unsigned long fd,
		const struct compat_iovec __user *vec,
		unsigned long vlen, u32 pos_low, u32 pos_high);
asmlinkage ssize_t compat_sys_pwritev(unsigned long fd,
		const struct compat_iovec __user *vec,
		unsigned long vlen, u32 pos_low, u32 pos_high);

asmlinkage long compat_sys_execve(const char __user *filename, const compat_uptr_t __user *argv,
		     const compat_uptr_t __user *envp);

asmlinkage long compat_sys_select(int n, compat_ulong_t __user *inp,
		compat_ulong_t __user *outp, compat_ulong_t __user *exp,
		struct compat_timeval __user *tvp);

asmlinkage long compat_sys_old_select(struct compat_sel_arg_struct __user *arg);

asmlinkage long compat_sys_wait4(compat_pid_t pid,
				 compat_uint_t __user *stat_addr, int options,
				 struct compat_rusage __user *ru);

#define BITS_PER_COMPAT_LONG    (8*sizeof(compat_long_t))

#define BITS_TO_COMPAT_LONGS(bits) \
	(((bits)+BITS_PER_COMPAT_LONG-1)/BITS_PER_COMPAT_LONG)

long compat_get_bitmap(unsigned long *mask, const compat_ulong_t __user *umask,
		       unsigned long bitmap_size);
long compat_put_bitmap(compat_ulong_t __user *umask, unsigned long *mask,
		       unsigned long bitmap_size);
int copy_siginfo_from_user32(siginfo_t *to, struct compat_siginfo __user *from);
int copy_siginfo_to_user32(struct compat_siginfo __user *to, siginfo_t *from);
int get_compat_sigevent(struct sigevent *event,
		const struct compat_sigevent __user *u_event);
long compat_sys_rt_tgsigqueueinfo(compat_pid_t tgid, compat_pid_t pid, int sig,
				  struct compat_siginfo __user *uinfo);

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

extern int get_compat_itimerspec(struct itimerspec *dst,
				 const struct compat_itimerspec __user *src);
extern int put_compat_itimerspec(struct compat_itimerspec __user *dst,
				 const struct itimerspec *src);

asmlinkage long compat_sys_gettimeofday(struct compat_timeval __user *tv,
		struct timezone __user *tz);
asmlinkage long compat_sys_settimeofday(struct compat_timeval __user *tv,
		struct timezone __user *tz);

asmlinkage long compat_sys_adjtimex(struct compat_timex __user *utp);

extern int compat_printk(const char *fmt, ...);
extern void sigset_from_compat(sigset_t *set, const compat_sigset_t *compat);
extern void sigset_to_compat(compat_sigset_t *compat, const sigset_t *set);

asmlinkage long compat_sys_migrate_pages(compat_pid_t pid,
		compat_ulong_t maxnode, const compat_ulong_t __user *old_nodes,
		const compat_ulong_t __user *new_nodes);

extern int compat_ptrace_request(struct task_struct *child,
				 compat_long_t request,
				 compat_ulong_t addr, compat_ulong_t data);

extern long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			       compat_ulong_t addr, compat_ulong_t data);
asmlinkage long compat_sys_ptrace(compat_long_t request, compat_long_t pid,
				  compat_long_t addr, compat_long_t data);

/*
 * epoll (fs/eventpoll.c) compat bits follow ...
 */
struct epoll_event;
#define compat_epoll_event	epoll_event
asmlinkage long compat_sys_epoll_pwait(int epfd,
			struct compat_epoll_event __user *events,
			int maxevents, int timeout,
			const compat_sigset_t __user *sigmask,
			compat_size_t sigsetsize);

asmlinkage long compat_sys_utime(const char __user *filename,
				 struct compat_utimbuf __user *t);
asmlinkage long compat_sys_utimensat(unsigned int dfd,
				     const char __user *filename,
				     struct compat_timespec __user *t,
				     int flags);

asmlinkage long compat_sys_time(compat_time_t __user *tloc);
asmlinkage long compat_sys_stime(compat_time_t __user *tptr);
asmlinkage long compat_sys_signalfd(int ufd,
				    const compat_sigset_t __user *sigmask,
				    compat_size_t sigsetsize);
asmlinkage long compat_sys_timerfd_settime(int ufd, int flags,
				   const struct compat_itimerspec __user *utmr,
				   struct compat_itimerspec __user *otmr);
asmlinkage long compat_sys_timerfd_gettime(int ufd,
				   struct compat_itimerspec __user *otmr);

asmlinkage long compat_sys_move_pages(pid_t pid, unsigned long nr_page,
				      __u32 __user *pages,
				      const int __user *nodes,
				      int __user *status,
				      int flags);
asmlinkage long compat_sys_futimesat(unsigned int dfd,
				     const char __user *filename,
				     struct compat_timeval __user *t);
asmlinkage long compat_sys_utimes(const char __user *filename,
				  struct compat_timeval __user *t);
asmlinkage long compat_sys_newstat(const char __user *filename,
				   struct compat_stat __user *statbuf);
asmlinkage long compat_sys_newlstat(const char __user *filename,
				    struct compat_stat __user *statbuf);
asmlinkage long compat_sys_newfstatat(unsigned int dfd,
				      const char __user *filename,
				      struct compat_stat __user *statbuf,
				      int flag);
asmlinkage long compat_sys_newfstat(unsigned int fd,
				    struct compat_stat __user *statbuf);
asmlinkage long compat_sys_statfs(const char __user *pathname,
				  struct compat_statfs __user *buf);
asmlinkage long compat_sys_fstatfs(unsigned int fd,
				   struct compat_statfs __user *buf);
asmlinkage long compat_sys_statfs64(const char __user *pathname,
				    compat_size_t sz,
				    struct compat_statfs64 __user *buf);
asmlinkage long compat_sys_fstatfs64(unsigned int fd, compat_size_t sz,
				     struct compat_statfs64 __user *buf);
asmlinkage long compat_sys_fcntl64(unsigned int fd, unsigned int cmd,
				   unsigned long arg);
asmlinkage long compat_sys_fcntl(unsigned int fd, unsigned int cmd,
				 unsigned long arg);
asmlinkage long compat_sys_io_setup(unsigned nr_reqs, u32 __user *ctx32p);
asmlinkage long compat_sys_io_getevents(aio_context_t ctx_id,
					unsigned long min_nr,
					unsigned long nr,
					struct io_event __user *events,
					struct compat_timespec __user *timeout);
asmlinkage long compat_sys_io_submit(aio_context_t ctx_id, int nr,
				     u32 __user *iocb);
asmlinkage long compat_sys_mount(const char __user *dev_name,
				 const char __user *dir_name,
				 const char __user *type, unsigned long flags,
				 const void __user *data);
asmlinkage long compat_sys_old_readdir(unsigned int fd,
				       struct compat_old_linux_dirent __user *,
				       unsigned int count);
asmlinkage long compat_sys_getdents(unsigned int fd,
				    struct compat_linux_dirent __user *dirent,
				    unsigned int count);
asmlinkage long compat_sys_getdents64(unsigned int fd,
				      struct linux_dirent64 __user *dirent,
				      unsigned int count);
asmlinkage long compat_sys_vmsplice(int fd, const struct compat_iovec __user *,
				    unsigned int nr_segs, unsigned int flags);
asmlinkage long compat_sys_open(const char __user *filename, int flags,
				umode_t mode);
asmlinkage long compat_sys_openat(unsigned int dfd, const char __user *filename,
				  int flags, umode_t mode);
asmlinkage long compat_sys_open_by_handle_at(int mountdirfd,
					     struct file_handle __user *handle,
					     int flags);
asmlinkage long compat_sys_pselect6(int n, compat_ulong_t __user *inp,
				    compat_ulong_t __user *outp,
				    compat_ulong_t __user *exp,
				    struct compat_timespec __user *tsp,
				    void __user *sig);
asmlinkage long compat_sys_ppoll(struct pollfd __user *ufds,
				 unsigned int nfds,
				 struct compat_timespec __user *tsp,
				 const compat_sigset_t __user *sigmask,
				 compat_size_t sigsetsize);
asmlinkage long compat_sys_signalfd4(int ufd,
				     const compat_sigset_t __user *sigmask,
				     compat_size_t sigsetsize, int flags);
asmlinkage long compat_sys_get_mempolicy(int __user *policy,
					 compat_ulong_t __user *nmask,
					 compat_ulong_t maxnode,
					 compat_ulong_t addr,
					 compat_ulong_t flags);
asmlinkage long compat_sys_set_mempolicy(int mode, compat_ulong_t __user *nmask,
					 compat_ulong_t maxnode);
asmlinkage long compat_sys_mbind(compat_ulong_t start, compat_ulong_t len,
				 compat_ulong_t mode,
				 compat_ulong_t __user *nmask,
				 compat_ulong_t maxnode, compat_ulong_t flags);

asmlinkage long compat_sys_setsockopt(int fd, int level, int optname,
				      char __user *optval, unsigned int optlen);
asmlinkage long compat_sys_sendmsg(int fd, struct compat_msghdr __user *msg,
				   unsigned flags);
asmlinkage long compat_sys_sendmmsg(int fd, struct compat_mmsghdr __user *mmsg,
				    unsigned vlen, unsigned int flags);
asmlinkage long compat_sys_recvmsg(int fd, struct compat_msghdr __user *msg,
				   unsigned int flags);
asmlinkage long compat_sys_recv(int fd, void __user *buf, size_t len,
				unsigned flags);
asmlinkage long compat_sys_recvfrom(int fd, void __user *buf, size_t len,
			    unsigned flags, struct sockaddr __user *addr,
			    int __user *addrlen);
asmlinkage long compat_sys_recvmmsg(int fd, struct compat_mmsghdr __user *mmsg,
				    unsigned vlen, unsigned int flags,
				    struct compat_timespec __user *timeout);
asmlinkage long compat_sys_nanosleep(struct compat_timespec __user *rqtp,
				     struct compat_timespec __user *rmtp);
asmlinkage long compat_sys_getitimer(int which,
				     struct compat_itimerval __user *it);
asmlinkage long compat_sys_setitimer(int which,
				     struct compat_itimerval __user *in,
				     struct compat_itimerval __user *out);
asmlinkage long compat_sys_times(struct compat_tms __user *tbuf);
asmlinkage long compat_sys_setrlimit(unsigned int resource,
				     struct compat_rlimit __user *rlim);
asmlinkage long compat_sys_getrlimit(unsigned int resource,
				     struct compat_rlimit __user *rlim);
asmlinkage long compat_sys_getrusage(int who, struct compat_rusage __user *ru);
asmlinkage long compat_sys_sched_setaffinity(compat_pid_t pid,
				     unsigned int len,
				     compat_ulong_t __user *user_mask_ptr);
asmlinkage long compat_sys_sched_getaffinity(compat_pid_t pid,
				     unsigned int len,
				     compat_ulong_t __user *user_mask_ptr);
asmlinkage long compat_sys_timer_create(clockid_t which_clock,
			struct compat_sigevent __user *timer_event_spec,
			timer_t __user *created_timer_id);
asmlinkage long compat_sys_timer_settime(timer_t timer_id, int flags,
					 struct compat_itimerspec __user *new,
					 struct compat_itimerspec __user *old);
asmlinkage long compat_sys_timer_gettime(timer_t timer_id,
				 struct compat_itimerspec __user *setting);
asmlinkage long compat_sys_clock_settime(clockid_t which_clock,
					 struct compat_timespec __user *tp);
asmlinkage long compat_sys_clock_gettime(clockid_t which_clock,
					 struct compat_timespec __user *tp);
asmlinkage long compat_sys_clock_adjtime(clockid_t which_clock,
					 struct compat_timex __user *tp);
asmlinkage long compat_sys_clock_getres(clockid_t which_clock,
					struct compat_timespec __user *tp);
asmlinkage long compat_sys_clock_nanosleep(clockid_t which_clock, int flags,
					   struct compat_timespec __user *rqtp,
					   struct compat_timespec __user *rmtp);
asmlinkage long compat_sys_rt_sigtimedwait(compat_sigset_t __user *uthese,
		struct compat_siginfo __user *uinfo,
		struct compat_timespec __user *uts, compat_size_t sigsetsize);
asmlinkage long compat_sys_rt_sigsuspend(compat_sigset_t __user *unewset,
					 compat_size_t sigsetsize);
#ifdef CONFIG_GENERIC_COMPAT_RT_SIGPROCMASK
asmlinkage long compat_sys_rt_sigprocmask(int how, compat_sigset_t __user *set,
					  compat_sigset_t __user *oset,
					  compat_size_t sigsetsize);
#endif
#ifdef CONFIG_GENERIC_COMPAT_RT_SIGPENDING
asmlinkage long compat_sys_rt_sigpending(compat_sigset_t __user *uset,
					 compat_size_t sigsetsize);
#endif
#ifdef CONFIG_GENERIC_COMPAT_RT_SIGQUEUEINFO
asmlinkage long compat_sys_rt_sigqueueinfo(compat_pid_t pid, int sig,
				struct compat_siginfo __user *uinfo);
#endif
asmlinkage long compat_sys_sysinfo(struct compat_sysinfo __user *info);
asmlinkage long compat_sys_ioctl(unsigned int fd, unsigned int cmd,
				 unsigned long arg);
asmlinkage long compat_sys_futex(u32 __user *uaddr, int op, u32 val,
		struct compat_timespec __user *utime, u32 __user *uaddr2,
		u32 val3);
asmlinkage long compat_sys_getsockopt(int fd, int level, int optname,
				      char __user *optval, int __user *optlen);
asmlinkage long compat_sys_kexec_load(unsigned long entry,
				      unsigned long nr_segments,
				      struct compat_kexec_segment __user *,
				      unsigned long flags);
asmlinkage long compat_sys_mq_getsetattr(mqd_t mqdes,
			const struct compat_mq_attr __user *u_mqstat,
			struct compat_mq_attr __user *u_omqstat);
asmlinkage long compat_sys_mq_notify(mqd_t mqdes,
			const struct compat_sigevent __user *u_notification);
asmlinkage long compat_sys_mq_open(const char __user *u_name,
			int oflag, compat_mode_t mode,
			struct compat_mq_attr __user *u_attr);
asmlinkage long compat_sys_mq_timedsend(mqd_t mqdes,
			const char __user *u_msg_ptr,
			size_t msg_len, unsigned int msg_prio,
			const struct compat_timespec __user *u_abs_timeout);
asmlinkage ssize_t compat_sys_mq_timedreceive(mqd_t mqdes,
			char __user *u_msg_ptr,
			size_t msg_len, unsigned int __user *u_msg_prio,
			const struct compat_timespec __user *u_abs_timeout);
asmlinkage long compat_sys_socketcall(int call, u32 __user *args);
asmlinkage long compat_sys_sysctl(struct compat_sysctl_args __user *args);

extern ssize_t compat_rw_copy_check_uvector(int type,
		const struct compat_iovec __user *uvector,
		unsigned long nr_segs,
		unsigned long fast_segs, struct iovec *fast_pointer,
		struct iovec **ret_pointer);

extern void __user *compat_alloc_user_space(unsigned long len);

asmlinkage ssize_t compat_sys_process_vm_readv(compat_pid_t pid,
		const struct compat_iovec __user *lvec,
		unsigned long liovcnt, const struct compat_iovec __user *rvec,
		unsigned long riovcnt, unsigned long flags);
asmlinkage ssize_t compat_sys_process_vm_writev(compat_pid_t pid,
		const struct compat_iovec __user *lvec,
		unsigned long liovcnt, const struct compat_iovec __user *rvec,
		unsigned long riovcnt, unsigned long flags);

asmlinkage long compat_sys_sendfile(int out_fd, int in_fd,
				    compat_off_t __user *offset, compat_size_t count);
#ifdef CONFIG_GENERIC_SIGALTSTACK
asmlinkage long compat_sys_sigaltstack(const compat_stack_t __user *uss_ptr,
				       compat_stack_t __user *uoss_ptr);

int compat_restore_altstack(const compat_stack_t __user *uss);
int __compat_save_altstack(compat_stack_t __user *, unsigned long);
#endif

asmlinkage long compat_sys_sched_rr_get_interval(compat_pid_t pid,
						 struct compat_timespec __user *interval);

#else

#define is_compat_task() (0)

#endif /* CONFIG_COMPAT */
#endif /* _LINUX_COMPAT_H */
