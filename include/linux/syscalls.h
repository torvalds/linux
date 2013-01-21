/*
 * syscalls.h - Linux syscall interfaces (non-arch-specific)
 *
 * Copyright (c) 2004 Randy Dunlap
 * Copyright (c) 2004 Open Source Development Labs
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#ifndef _LINUX_SYSCALLS_H
#define _LINUX_SYSCALLS_H

struct epoll_event;
struct iattr;
struct inode;
struct iocb;
struct io_event;
struct iovec;
struct itimerspec;
struct itimerval;
struct kexec_segment;
struct linux_dirent;
struct linux_dirent64;
struct list_head;
struct mmap_arg_struct;
struct msgbuf;
struct msghdr;
struct mmsghdr;
struct msqid_ds;
struct new_utsname;
struct nfsctl_arg;
struct __old_kernel_stat;
struct oldold_utsname;
struct old_utsname;
struct pollfd;
struct rlimit;
struct rlimit64;
struct rusage;
struct sched_param;
struct sel_arg_struct;
struct semaphore;
struct sembuf;
struct shmid_ds;
struct sockaddr;
struct stat;
struct stat64;
struct statfs;
struct statfs64;
struct __sysctl_args;
struct sysinfo;
struct timespec;
struct timeval;
struct timex;
struct timezone;
struct tms;
struct utimbuf;
struct mq_attr;
struct compat_stat;
struct compat_timeval;
struct robust_list_head;
struct getcpu_cache;
struct old_linux_dirent;
struct perf_event_attr;
struct file_handle;
struct sigaltstack;

#include <linux/types.h>
#include <linux/aio_abi.h>
#include <linux/capability.h>
#include <linux/signal.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/sem.h>
#include <asm/siginfo.h>
#include <linux/unistd.h>
#include <linux/quota.h>
#include <linux/key.h>
#include <trace/syscall.h>

/*
 * __MAP - apply a macro to syscall arguments
 * __MAP(n, m, t1, a1, t2, a2, ..., tn, an) will expand to
 *    m(t1, a1), m(t2, a2), ..., m(tn, an)
 * The first argument must be equal to the amount of type/name
 * pairs given.  Note that this list of pairs (i.e. the arguments
 * of __MAP starting at the third one) is in the same format as
 * for SYSCALL_DEFINE<n>/COMPAT_SYSCALL_DEFINE<n>
 */
#define __MAP1(m,t,a) m(t,a)
#define __MAP2(m,t,a,...) m(t,a), __MAP1(m,__VA_ARGS__)
#define __MAP3(m,t,a,...) m(t,a), __MAP2(m,__VA_ARGS__)
#define __MAP4(m,t,a,...) m(t,a), __MAP3(m,__VA_ARGS__)
#define __MAP5(m,t,a,...) m(t,a), __MAP4(m,__VA_ARGS__)
#define __MAP6(m,t,a,...) m(t,a), __MAP5(m,__VA_ARGS__)
#define __MAP(n,...) __MAP##n(__VA_ARGS__)

#define __SC_DECL(t, a)	t a
#define __TYPE_IS_LL(t) (__same_type((t)0, 0LL) || __same_type((t)0, 0ULL))
#define __SC_LONG(t, a) __typeof(__builtin_choose_expr(__TYPE_IS_LL(t), 0LL, 0L)) a
#define __SC_CAST(t, a)	(t) a
#define __SC_TEST(t, a) (void)BUILD_BUG_ON_ZERO(!__TYPE_IS_LL(t) && sizeof(t) > sizeof(long))

#ifdef CONFIG_FTRACE_SYSCALLS
#define __SC_STR_ADECL(t, a)	#a
#define __SC_STR_TDECL(t, a)	#t

extern struct ftrace_event_class event_class_syscall_enter;
extern struct ftrace_event_class event_class_syscall_exit;
extern struct trace_event_functions enter_syscall_print_funcs;
extern struct trace_event_functions exit_syscall_print_funcs;

#define SYSCALL_TRACE_ENTER_EVENT(sname)				\
	static struct syscall_metadata __syscall_meta_##sname;		\
	static struct ftrace_event_call __used				\
	  event_enter_##sname = {					\
		.name                   = "sys_enter"#sname,		\
		.class			= &event_class_syscall_enter,	\
		.event.funcs            = &enter_syscall_print_funcs,	\
		.data			= (void *)&__syscall_meta_##sname,\
		.flags			= TRACE_EVENT_FL_CAP_ANY,	\
	};								\
	static struct ftrace_event_call __used				\
	  __attribute__((section("_ftrace_events")))			\
	 *__event_enter_##sname = &event_enter_##sname;

#define SYSCALL_TRACE_EXIT_EVENT(sname)					\
	static struct syscall_metadata __syscall_meta_##sname;		\
	static struct ftrace_event_call __used				\
	  event_exit_##sname = {					\
		.name                   = "sys_exit"#sname,		\
		.class			= &event_class_syscall_exit,	\
		.event.funcs		= &exit_syscall_print_funcs,	\
		.data			= (void *)&__syscall_meta_##sname,\
		.flags			= TRACE_EVENT_FL_CAP_ANY,	\
	};								\
	static struct ftrace_event_call __used				\
	  __attribute__((section("_ftrace_events")))			\
	*__event_exit_##sname = &event_exit_##sname;

#define SYSCALL_METADATA(sname, nb)				\
	SYSCALL_TRACE_ENTER_EVENT(sname);			\
	SYSCALL_TRACE_EXIT_EVENT(sname);			\
	static struct syscall_metadata __used			\
	  __syscall_meta_##sname = {				\
		.name 		= "sys"#sname,			\
		.syscall_nr	= -1,	/* Filled in at boot */	\
		.nb_args 	= nb,				\
		.types		= types_##sname,		\
		.args		= args_##sname,			\
		.enter_event	= &event_enter_##sname,		\
		.exit_event	= &event_exit_##sname,		\
		.enter_fields	= LIST_HEAD_INIT(__syscall_meta_##sname.enter_fields), \
	};							\
	static struct syscall_metadata __used			\
	  __attribute__((section("__syscalls_metadata")))	\
	 *__p_syscall_meta_##sname = &__syscall_meta_##sname;

#define SYSCALL_DEFINE0(sname)					\
	SYSCALL_TRACE_ENTER_EVENT(_##sname);			\
	SYSCALL_TRACE_EXIT_EVENT(_##sname);			\
	static struct syscall_metadata __used			\
	  __syscall_meta__##sname = {				\
		.name 		= "sys_"#sname,			\
		.syscall_nr	= -1,	/* Filled in at boot */	\
		.nb_args 	= 0,				\
		.enter_event	= &event_enter__##sname,	\
		.exit_event	= &event_exit__##sname,		\
		.enter_fields	= LIST_HEAD_INIT(__syscall_meta__##sname.enter_fields), \
	};							\
	static struct syscall_metadata __used			\
	  __attribute__((section("__syscalls_metadata")))	\
	 *__p_syscall_meta_##sname = &__syscall_meta__##sname;	\
	asmlinkage long sys_##sname(void)
#else
#define SYSCALL_DEFINE0(name)	   asmlinkage long sys_##name(void)
#endif

#define SYSCALL_DEFINE1(name, ...) SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE2(name, ...) SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE3(name, ...) SYSCALL_DEFINEx(3, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE4(name, ...) SYSCALL_DEFINEx(4, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE5(name, ...) SYSCALL_DEFINEx(5, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE6(name, ...) SYSCALL_DEFINEx(6, _##name, __VA_ARGS__)

#ifdef CONFIG_PPC64
#define SYSCALL_ALIAS(alias, name)					\
	asm ("\t.globl " #alias "\n\t.set " #alias ", " #name "\n"	\
	     "\t.globl ." #alias "\n\t.set ." #alias ", ." #name)
#else
#if defined(CONFIG_ALPHA) || defined(CONFIG_MIPS)
#define SYSCALL_ALIAS(alias, name)					\
	asm ( #alias " = " #name "\n\t.globl " #alias)
#else
#define SYSCALL_ALIAS(alias, name)					\
	asm ("\t.globl " #alias "\n\t.set " #alias ", " #name)
#endif
#endif

#ifdef CONFIG_FTRACE_SYSCALLS
#define SYSCALL_DEFINEx(x, sname, ...)				\
	static const char *types_##sname[] = {			\
		__MAP(x,__SC_STR_TDECL,__VA_ARGS__)		\
	};							\
	static const char *args_##sname[] = {			\
		__MAP(x,__SC_STR_ADECL,__VA_ARGS__)		\
	};							\
	SYSCALL_METADATA(sname, x);				\
	__SYSCALL_DEFINEx(x, sname, __VA_ARGS__)
#else
#define SYSCALL_DEFINEx(x, sname, ...)				\
	__SYSCALL_DEFINEx(x, sname, __VA_ARGS__)
#endif

#ifdef CONFIG_HAVE_SYSCALL_WRAPPERS

#define SYSCALL_DEFINE(name) static inline long SYSC_##name

#define __SYSCALL_DEFINEx(x, name, ...)					\
	asmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	static inline long SYSC##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	asmlinkage long SyS##name(__MAP(x,__SC_LONG,__VA_ARGS__))	\
	{								\
		__MAP(x,__SC_TEST,__VA_ARGS__);				\
		return SYSC##name(__MAP(x,__SC_CAST,__VA_ARGS__));	\
	}								\
	SYSCALL_ALIAS(sys##name, SyS##name);				\
	static inline long SYSC##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#else /* CONFIG_HAVE_SYSCALL_WRAPPERS */

#define SYSCALL_DEFINE(name) asmlinkage long sys_##name
#define __SYSCALL_DEFINEx(x, name, ...)					\
	asmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#endif /* CONFIG_HAVE_SYSCALL_WRAPPERS */

asmlinkage long sys_time(time_t __user *tloc);
asmlinkage long sys_stime(time_t __user *tptr);
asmlinkage long sys_gettimeofday(struct timeval __user *tv,
				struct timezone __user *tz);
asmlinkage long sys_settimeofday(struct timeval __user *tv,
				struct timezone __user *tz);
asmlinkage long sys_adjtimex(struct timex __user *txc_p);

asmlinkage long sys_times(struct tms __user *tbuf);

asmlinkage long sys_gettid(void);
asmlinkage long sys_nanosleep(struct timespec __user *rqtp, struct timespec __user *rmtp);
asmlinkage long sys_alarm(unsigned int seconds);
asmlinkage long sys_getpid(void);
asmlinkage long sys_getppid(void);
asmlinkage long sys_getuid(void);
asmlinkage long sys_geteuid(void);
asmlinkage long sys_getgid(void);
asmlinkage long sys_getegid(void);
asmlinkage long sys_getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid);
asmlinkage long sys_getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid);
asmlinkage long sys_getpgid(pid_t pid);
asmlinkage long sys_getpgrp(void);
asmlinkage long sys_getsid(pid_t pid);
asmlinkage long sys_getgroups(int gidsetsize, gid_t __user *grouplist);

asmlinkage long sys_setregid(gid_t rgid, gid_t egid);
asmlinkage long sys_setgid(gid_t gid);
asmlinkage long sys_setreuid(uid_t ruid, uid_t euid);
asmlinkage long sys_setuid(uid_t uid);
asmlinkage long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
asmlinkage long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
asmlinkage long sys_setfsuid(uid_t uid);
asmlinkage long sys_setfsgid(gid_t gid);
asmlinkage long sys_setpgid(pid_t pid, pid_t pgid);
asmlinkage long sys_setsid(void);
asmlinkage long sys_setgroups(int gidsetsize, gid_t __user *grouplist);

asmlinkage long sys_acct(const char __user *name);
asmlinkage long sys_capget(cap_user_header_t header,
				cap_user_data_t dataptr);
asmlinkage long sys_capset(cap_user_header_t header,
				const cap_user_data_t data);
asmlinkage long sys_personality(unsigned int personality);

asmlinkage long sys_sigpending(old_sigset_t __user *set);
asmlinkage long sys_sigprocmask(int how, old_sigset_t __user *set,
				old_sigset_t __user *oset);
asmlinkage long sys_sigaltstack(const struct sigaltstack __user *uss,
				struct sigaltstack __user *uoss);

asmlinkage long sys_getitimer(int which, struct itimerval __user *value);
asmlinkage long sys_setitimer(int which,
				struct itimerval __user *value,
				struct itimerval __user *ovalue);
asmlinkage long sys_timer_create(clockid_t which_clock,
				 struct sigevent __user *timer_event_spec,
				 timer_t __user * created_timer_id);
asmlinkage long sys_timer_gettime(timer_t timer_id,
				struct itimerspec __user *setting);
asmlinkage long sys_timer_getoverrun(timer_t timer_id);
asmlinkage long sys_timer_settime(timer_t timer_id, int flags,
				const struct itimerspec __user *new_setting,
				struct itimerspec __user *old_setting);
asmlinkage long sys_timer_delete(timer_t timer_id);
asmlinkage long sys_clock_settime(clockid_t which_clock,
				const struct timespec __user *tp);
asmlinkage long sys_clock_gettime(clockid_t which_clock,
				struct timespec __user *tp);
asmlinkage long sys_clock_adjtime(clockid_t which_clock,
				struct timex __user *tx);
asmlinkage long sys_clock_getres(clockid_t which_clock,
				struct timespec __user *tp);
asmlinkage long sys_clock_nanosleep(clockid_t which_clock, int flags,
				const struct timespec __user *rqtp,
				struct timespec __user *rmtp);

asmlinkage long sys_nice(int increment);
asmlinkage long sys_sched_setscheduler(pid_t pid, int policy,
					struct sched_param __user *param);
asmlinkage long sys_sched_setparam(pid_t pid,
					struct sched_param __user *param);
asmlinkage long sys_sched_getscheduler(pid_t pid);
asmlinkage long sys_sched_getparam(pid_t pid,
					struct sched_param __user *param);
asmlinkage long sys_sched_setaffinity(pid_t pid, unsigned int len,
					unsigned long __user *user_mask_ptr);
asmlinkage long sys_sched_getaffinity(pid_t pid, unsigned int len,
					unsigned long __user *user_mask_ptr);
asmlinkage long sys_sched_yield(void);
asmlinkage long sys_sched_get_priority_max(int policy);
asmlinkage long sys_sched_get_priority_min(int policy);
asmlinkage long sys_sched_rr_get_interval(pid_t pid,
					struct timespec __user *interval);
asmlinkage long sys_setpriority(int which, int who, int niceval);
asmlinkage long sys_getpriority(int which, int who);

asmlinkage long sys_shutdown(int, int);
asmlinkage long sys_reboot(int magic1, int magic2, unsigned int cmd,
				void __user *arg);
asmlinkage long sys_restart_syscall(void);
asmlinkage long sys_kexec_load(unsigned long entry, unsigned long nr_segments,
				struct kexec_segment __user *segments,
				unsigned long flags);

asmlinkage long sys_exit(int error_code);
asmlinkage long sys_exit_group(int error_code);
asmlinkage long sys_wait4(pid_t pid, int __user *stat_addr,
				int options, struct rusage __user *ru);
asmlinkage long sys_waitid(int which, pid_t pid,
			   struct siginfo __user *infop,
			   int options, struct rusage __user *ru);
asmlinkage long sys_waitpid(pid_t pid, int __user *stat_addr, int options);
asmlinkage long sys_set_tid_address(int __user *tidptr);
asmlinkage long sys_futex(u32 __user *uaddr, int op, u32 val,
			struct timespec __user *utime, u32 __user *uaddr2,
			u32 val3);

asmlinkage long sys_init_module(void __user *umod, unsigned long len,
				const char __user *uargs);
asmlinkage long sys_delete_module(const char __user *name_user,
				unsigned int flags);

#ifdef CONFIG_OLD_SIGSUSPEND
asmlinkage long sys_sigsuspend(old_sigset_t mask);
#endif

#ifdef CONFIG_OLD_SIGSUSPEND3
asmlinkage long sys_sigsuspend(int unused1, int unused2, old_sigset_t mask);
#endif

asmlinkage long sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize);

#ifdef CONFIG_OLD_SIGACTION
asmlinkage long sys_sigaction(int, const struct old_sigaction __user *,
				struct old_sigaction __user *);
#endif

#ifndef CONFIG_ODD_RT_SIGACTION
asmlinkage long sys_rt_sigaction(int,
				 const struct sigaction __user *,
				 struct sigaction __user *,
				 size_t);
#endif
asmlinkage long sys_rt_sigprocmask(int how, sigset_t __user *set,
				sigset_t __user *oset, size_t sigsetsize);
asmlinkage long sys_rt_sigpending(sigset_t __user *set, size_t sigsetsize);
asmlinkage long sys_rt_sigtimedwait(const sigset_t __user *uthese,
				siginfo_t __user *uinfo,
				const struct timespec __user *uts,
				size_t sigsetsize);
asmlinkage long sys_rt_tgsigqueueinfo(pid_t tgid, pid_t  pid, int sig,
		siginfo_t __user *uinfo);
asmlinkage long sys_kill(int pid, int sig);
asmlinkage long sys_tgkill(int tgid, int pid, int sig);
asmlinkage long sys_tkill(int pid, int sig);
asmlinkage long sys_rt_sigqueueinfo(int pid, int sig, siginfo_t __user *uinfo);
asmlinkage long sys_sgetmask(void);
asmlinkage long sys_ssetmask(int newmask);
asmlinkage long sys_signal(int sig, __sighandler_t handler);
asmlinkage long sys_pause(void);

asmlinkage long sys_sync(void);
asmlinkage long sys_fsync(unsigned int fd);
asmlinkage long sys_fdatasync(unsigned int fd);
asmlinkage long sys_bdflush(int func, long data);
asmlinkage long sys_mount(char __user *dev_name, char __user *dir_name,
				char __user *type, unsigned long flags,
				void __user *data);
asmlinkage long sys_umount(char __user *name, int flags);
asmlinkage long sys_oldumount(char __user *name);
asmlinkage long sys_truncate(const char __user *path, long length);
asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length);
asmlinkage long sys_stat(const char __user *filename,
			struct __old_kernel_stat __user *statbuf);
asmlinkage long sys_statfs(const char __user * path,
				struct statfs __user *buf);
asmlinkage long sys_statfs64(const char __user *path, size_t sz,
				struct statfs64 __user *buf);
asmlinkage long sys_fstatfs(unsigned int fd, struct statfs __user *buf);
asmlinkage long sys_fstatfs64(unsigned int fd, size_t sz,
				struct statfs64 __user *buf);
asmlinkage long sys_lstat(const char __user *filename,
			struct __old_kernel_stat __user *statbuf);
asmlinkage long sys_fstat(unsigned int fd,
			struct __old_kernel_stat __user *statbuf);
asmlinkage long sys_newstat(const char __user *filename,
				struct stat __user *statbuf);
asmlinkage long sys_newlstat(const char __user *filename,
				struct stat __user *statbuf);
asmlinkage long sys_newfstat(unsigned int fd, struct stat __user *statbuf);
asmlinkage long sys_ustat(unsigned dev, struct ustat __user *ubuf);
#if BITS_PER_LONG == 32
asmlinkage long sys_stat64(const char __user *filename,
				struct stat64 __user *statbuf);
asmlinkage long sys_fstat64(unsigned long fd, struct stat64 __user *statbuf);
asmlinkage long sys_lstat64(const char __user *filename,
				struct stat64 __user *statbuf);
asmlinkage long sys_truncate64(const char __user *path, loff_t length);
asmlinkage long sys_ftruncate64(unsigned int fd, loff_t length);
#endif

asmlinkage long sys_setxattr(const char __user *path, const char __user *name,
			     const void __user *value, size_t size, int flags);
asmlinkage long sys_lsetxattr(const char __user *path, const char __user *name,
			      const void __user *value, size_t size, int flags);
asmlinkage long sys_fsetxattr(int fd, const char __user *name,
			      const void __user *value, size_t size, int flags);
asmlinkage long sys_getxattr(const char __user *path, const char __user *name,
			     void __user *value, size_t size);
asmlinkage long sys_lgetxattr(const char __user *path, const char __user *name,
			      void __user *value, size_t size);
asmlinkage long sys_fgetxattr(int fd, const char __user *name,
			      void __user *value, size_t size);
asmlinkage long sys_listxattr(const char __user *path, char __user *list,
			      size_t size);
asmlinkage long sys_llistxattr(const char __user *path, char __user *list,
			       size_t size);
asmlinkage long sys_flistxattr(int fd, char __user *list, size_t size);
asmlinkage long sys_removexattr(const char __user *path,
				const char __user *name);
asmlinkage long sys_lremovexattr(const char __user *path,
				 const char __user *name);
asmlinkage long sys_fremovexattr(int fd, const char __user *name);

asmlinkage long sys_brk(unsigned long brk);
asmlinkage long sys_mprotect(unsigned long start, size_t len,
				unsigned long prot);
asmlinkage long sys_mremap(unsigned long addr,
			   unsigned long old_len, unsigned long new_len,
			   unsigned long flags, unsigned long new_addr);
asmlinkage long sys_remap_file_pages(unsigned long start, unsigned long size,
			unsigned long prot, unsigned long pgoff,
			unsigned long flags);
asmlinkage long sys_msync(unsigned long start, size_t len, int flags);
asmlinkage long sys_fadvise64(int fd, loff_t offset, size_t len, int advice);
asmlinkage long sys_fadvise64_64(int fd, loff_t offset, loff_t len, int advice);
asmlinkage long sys_munmap(unsigned long addr, size_t len);
asmlinkage long sys_mlock(unsigned long start, size_t len);
asmlinkage long sys_munlock(unsigned long start, size_t len);
asmlinkage long sys_mlockall(int flags);
asmlinkage long sys_munlockall(void);
asmlinkage long sys_madvise(unsigned long start, size_t len, int behavior);
asmlinkage long sys_mincore(unsigned long start, size_t len,
				unsigned char __user * vec);

asmlinkage long sys_pivot_root(const char __user *new_root,
				const char __user *put_old);
asmlinkage long sys_chroot(const char __user *filename);
asmlinkage long sys_mknod(const char __user *filename, umode_t mode,
				unsigned dev);
asmlinkage long sys_link(const char __user *oldname,
				const char __user *newname);
asmlinkage long sys_symlink(const char __user *old, const char __user *new);
asmlinkage long sys_unlink(const char __user *pathname);
asmlinkage long sys_rename(const char __user *oldname,
				const char __user *newname);
asmlinkage long sys_chmod(const char __user *filename, umode_t mode);
asmlinkage long sys_fchmod(unsigned int fd, umode_t mode);

asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);
#if BITS_PER_LONG == 32
asmlinkage long sys_fcntl64(unsigned int fd,
				unsigned int cmd, unsigned long arg);
#endif
asmlinkage long sys_pipe(int __user *fildes);
asmlinkage long sys_pipe2(int __user *fildes, int flags);
asmlinkage long sys_dup(unsigned int fildes);
asmlinkage long sys_dup2(unsigned int oldfd, unsigned int newfd);
asmlinkage long sys_dup3(unsigned int oldfd, unsigned int newfd, int flags);
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int on);
asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd,
				unsigned long arg);
asmlinkage long sys_flock(unsigned int fd, unsigned int cmd);
asmlinkage long sys_io_setup(unsigned nr_reqs, aio_context_t __user *ctx);
asmlinkage long sys_io_destroy(aio_context_t ctx);
asmlinkage long sys_io_getevents(aio_context_t ctx_id,
				long min_nr,
				long nr,
				struct io_event __user *events,
				struct timespec __user *timeout);
asmlinkage long sys_io_submit(aio_context_t, long,
				struct iocb __user * __user *);
asmlinkage long sys_io_cancel(aio_context_t ctx_id, struct iocb __user *iocb,
			      struct io_event __user *result);
asmlinkage long sys_sendfile(int out_fd, int in_fd,
			     off_t __user *offset, size_t count);
asmlinkage long sys_sendfile64(int out_fd, int in_fd,
			       loff_t __user *offset, size_t count);
asmlinkage long sys_readlink(const char __user *path,
				char __user *buf, int bufsiz);
asmlinkage long sys_creat(const char __user *pathname, umode_t mode);
asmlinkage long sys_open(const char __user *filename,
				int flags, umode_t mode);
asmlinkage long sys_close(unsigned int fd);
asmlinkage long sys_access(const char __user *filename, int mode);
asmlinkage long sys_vhangup(void);
asmlinkage long sys_chown(const char __user *filename,
				uid_t user, gid_t group);
asmlinkage long sys_lchown(const char __user *filename,
				uid_t user, gid_t group);
asmlinkage long sys_fchown(unsigned int fd, uid_t user, gid_t group);
#ifdef CONFIG_UID16
asmlinkage long sys_chown16(const char __user *filename,
				old_uid_t user, old_gid_t group);
asmlinkage long sys_lchown16(const char __user *filename,
				old_uid_t user, old_gid_t group);
asmlinkage long sys_fchown16(unsigned int fd, old_uid_t user, old_gid_t group);
asmlinkage long sys_setregid16(old_gid_t rgid, old_gid_t egid);
asmlinkage long sys_setgid16(old_gid_t gid);
asmlinkage long sys_setreuid16(old_uid_t ruid, old_uid_t euid);
asmlinkage long sys_setuid16(old_uid_t uid);
asmlinkage long sys_setresuid16(old_uid_t ruid, old_uid_t euid, old_uid_t suid);
asmlinkage long sys_getresuid16(old_uid_t __user *ruid,
				old_uid_t __user *euid, old_uid_t __user *suid);
asmlinkage long sys_setresgid16(old_gid_t rgid, old_gid_t egid, old_gid_t sgid);
asmlinkage long sys_getresgid16(old_gid_t __user *rgid,
				old_gid_t __user *egid, old_gid_t __user *sgid);
asmlinkage long sys_setfsuid16(old_uid_t uid);
asmlinkage long sys_setfsgid16(old_gid_t gid);
asmlinkage long sys_getgroups16(int gidsetsize, old_gid_t __user *grouplist);
asmlinkage long sys_setgroups16(int gidsetsize, old_gid_t __user *grouplist);
asmlinkage long sys_getuid16(void);
asmlinkage long sys_geteuid16(void);
asmlinkage long sys_getgid16(void);
asmlinkage long sys_getegid16(void);
#endif

asmlinkage long sys_utime(char __user *filename,
				struct utimbuf __user *times);
asmlinkage long sys_utimes(char __user *filename,
				struct timeval __user *utimes);
asmlinkage long sys_lseek(unsigned int fd, off_t offset,
			  unsigned int whence);
asmlinkage long sys_llseek(unsigned int fd, unsigned long offset_high,
			unsigned long offset_low, loff_t __user *result,
			unsigned int whence);
asmlinkage long sys_read(unsigned int fd, char __user *buf, size_t count);
asmlinkage long sys_readahead(int fd, loff_t offset, size_t count);
asmlinkage long sys_readv(unsigned long fd,
			  const struct iovec __user *vec,
			  unsigned long vlen);
asmlinkage long sys_write(unsigned int fd, const char __user *buf,
			  size_t count);
asmlinkage long sys_writev(unsigned long fd,
			   const struct iovec __user *vec,
			   unsigned long vlen);
asmlinkage long sys_pread64(unsigned int fd, char __user *buf,
			    size_t count, loff_t pos);
asmlinkage long sys_pwrite64(unsigned int fd, const char __user *buf,
			     size_t count, loff_t pos);
asmlinkage long sys_preadv(unsigned long fd, const struct iovec __user *vec,
			   unsigned long vlen, unsigned long pos_l, unsigned long pos_h);
asmlinkage long sys_pwritev(unsigned long fd, const struct iovec __user *vec,
			    unsigned long vlen, unsigned long pos_l, unsigned long pos_h);
asmlinkage long sys_getcwd(char __user *buf, unsigned long size);
asmlinkage long sys_mkdir(const char __user *pathname, umode_t mode);
asmlinkage long sys_chdir(const char __user *filename);
asmlinkage long sys_fchdir(unsigned int fd);
asmlinkage long sys_rmdir(const char __user *pathname);
asmlinkage long sys_lookup_dcookie(u64 cookie64, char __user *buf, size_t len);
asmlinkage long sys_quotactl(unsigned int cmd, const char __user *special,
				qid_t id, void __user *addr);
asmlinkage long sys_getdents(unsigned int fd,
				struct linux_dirent __user *dirent,
				unsigned int count);
asmlinkage long sys_getdents64(unsigned int fd,
				struct linux_dirent64 __user *dirent,
				unsigned int count);

asmlinkage long sys_setsockopt(int fd, int level, int optname,
				char __user *optval, int optlen);
asmlinkage long sys_getsockopt(int fd, int level, int optname,
				char __user *optval, int __user *optlen);
asmlinkage long sys_bind(int, struct sockaddr __user *, int);
asmlinkage long sys_connect(int, struct sockaddr __user *, int);
asmlinkage long sys_accept(int, struct sockaddr __user *, int __user *);
asmlinkage long sys_accept4(int, struct sockaddr __user *, int __user *, int);
asmlinkage long sys_getsockname(int, struct sockaddr __user *, int __user *);
asmlinkage long sys_getpeername(int, struct sockaddr __user *, int __user *);
asmlinkage long sys_send(int, void __user *, size_t, unsigned);
asmlinkage long sys_sendto(int, void __user *, size_t, unsigned,
				struct sockaddr __user *, int);
asmlinkage long sys_sendmsg(int fd, struct msghdr __user *msg, unsigned flags);
asmlinkage long sys_sendmmsg(int fd, struct mmsghdr __user *msg,
			     unsigned int vlen, unsigned flags);
asmlinkage long sys_recv(int, void __user *, size_t, unsigned);
asmlinkage long sys_recvfrom(int, void __user *, size_t, unsigned,
				struct sockaddr __user *, int __user *);
asmlinkage long sys_recvmsg(int fd, struct msghdr __user *msg, unsigned flags);
asmlinkage long sys_recvmmsg(int fd, struct mmsghdr __user *msg,
			     unsigned int vlen, unsigned flags,
			     struct timespec __user *timeout);
asmlinkage long sys_socket(int, int, int);
asmlinkage long sys_socketpair(int, int, int, int __user *);
asmlinkage long sys_socketcall(int call, unsigned long __user *args);
asmlinkage long sys_listen(int, int);
asmlinkage long sys_poll(struct pollfd __user *ufds, unsigned int nfds,
				int timeout);
asmlinkage long sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			fd_set __user *exp, struct timeval __user *tvp);
asmlinkage long sys_old_select(struct sel_arg_struct __user *arg);
asmlinkage long sys_epoll_create(int size);
asmlinkage long sys_epoll_create1(int flags);
asmlinkage long sys_epoll_ctl(int epfd, int op, int fd,
				struct epoll_event __user *event);
asmlinkage long sys_epoll_wait(int epfd, struct epoll_event __user *events,
				int maxevents, int timeout);
asmlinkage long sys_epoll_pwait(int epfd, struct epoll_event __user *events,
				int maxevents, int timeout,
				const sigset_t __user *sigmask,
				size_t sigsetsize);
asmlinkage long sys_gethostname(char __user *name, int len);
asmlinkage long sys_sethostname(char __user *name, int len);
asmlinkage long sys_setdomainname(char __user *name, int len);
asmlinkage long sys_newuname(struct new_utsname __user *name);
asmlinkage long sys_uname(struct old_utsname __user *);
asmlinkage long sys_olduname(struct oldold_utsname __user *);

asmlinkage long sys_getrlimit(unsigned int resource,
				struct rlimit __user *rlim);
#if defined(COMPAT_RLIM_OLD_INFINITY) || !(defined(CONFIG_IA64))
asmlinkage long sys_old_getrlimit(unsigned int resource, struct rlimit __user *rlim);
#endif
asmlinkage long sys_setrlimit(unsigned int resource,
				struct rlimit __user *rlim);
asmlinkage long sys_prlimit64(pid_t pid, unsigned int resource,
				const struct rlimit64 __user *new_rlim,
				struct rlimit64 __user *old_rlim);
asmlinkage long sys_getrusage(int who, struct rusage __user *ru);
asmlinkage long sys_umask(int mask);

asmlinkage long sys_msgget(key_t key, int msgflg);
asmlinkage long sys_msgsnd(int msqid, struct msgbuf __user *msgp,
				size_t msgsz, int msgflg);
asmlinkage long sys_msgrcv(int msqid, struct msgbuf __user *msgp,
				size_t msgsz, long msgtyp, int msgflg);
asmlinkage long sys_msgctl(int msqid, int cmd, struct msqid_ds __user *buf);

asmlinkage long sys_semget(key_t key, int nsems, int semflg);
asmlinkage long sys_semop(int semid, struct sembuf __user *sops,
				unsigned nsops);
asmlinkage long sys_semctl(int semid, int semnum, int cmd, union semun arg);
asmlinkage long sys_semtimedop(int semid, struct sembuf __user *sops,
				unsigned nsops,
				const struct timespec __user *timeout);
asmlinkage long sys_shmat(int shmid, char __user *shmaddr, int shmflg);
asmlinkage long sys_shmget(key_t key, size_t size, int flag);
asmlinkage long sys_shmdt(char __user *shmaddr);
asmlinkage long sys_shmctl(int shmid, int cmd, struct shmid_ds __user *buf);
asmlinkage long sys_ipc(unsigned int call, int first, unsigned long second,
		unsigned long third, void __user *ptr, long fifth);

asmlinkage long sys_mq_open(const char __user *name, int oflag, umode_t mode, struct mq_attr __user *attr);
asmlinkage long sys_mq_unlink(const char __user *name);
asmlinkage long sys_mq_timedsend(mqd_t mqdes, const char __user *msg_ptr, size_t msg_len, unsigned int msg_prio, const struct timespec __user *abs_timeout);
asmlinkage long sys_mq_timedreceive(mqd_t mqdes, char __user *msg_ptr, size_t msg_len, unsigned int __user *msg_prio, const struct timespec __user *abs_timeout);
asmlinkage long sys_mq_notify(mqd_t mqdes, const struct sigevent __user *notification);
asmlinkage long sys_mq_getsetattr(mqd_t mqdes, const struct mq_attr __user *mqstat, struct mq_attr __user *omqstat);

asmlinkage long sys_pciconfig_iobase(long which, unsigned long bus, unsigned long devfn);
asmlinkage long sys_pciconfig_read(unsigned long bus, unsigned long dfn,
				unsigned long off, unsigned long len,
				void __user *buf);
asmlinkage long sys_pciconfig_write(unsigned long bus, unsigned long dfn,
				unsigned long off, unsigned long len,
				void __user *buf);

asmlinkage long sys_prctl(int option, unsigned long arg2, unsigned long arg3,
			unsigned long arg4, unsigned long arg5);
asmlinkage long sys_swapon(const char __user *specialfile, int swap_flags);
asmlinkage long sys_swapoff(const char __user *specialfile);
asmlinkage long sys_sysctl(struct __sysctl_args __user *args);
asmlinkage long sys_sysinfo(struct sysinfo __user *info);
asmlinkage long sys_sysfs(int option,
				unsigned long arg1, unsigned long arg2);
asmlinkage long sys_syslog(int type, char __user *buf, int len);
asmlinkage long sys_uselib(const char __user *library);
asmlinkage long sys_ni_syscall(void);
asmlinkage long sys_ptrace(long request, long pid, unsigned long addr,
			   unsigned long data);

asmlinkage long sys_add_key(const char __user *_type,
			    const char __user *_description,
			    const void __user *_payload,
			    size_t plen,
			    key_serial_t destringid);

asmlinkage long sys_request_key(const char __user *_type,
				const char __user *_description,
				const char __user *_callout_info,
				key_serial_t destringid);

asmlinkage long sys_keyctl(int cmd, unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, unsigned long arg5);

asmlinkage long sys_ioprio_set(int which, int who, int ioprio);
asmlinkage long sys_ioprio_get(int which, int who);
asmlinkage long sys_set_mempolicy(int mode, unsigned long __user *nmask,
				unsigned long maxnode);
asmlinkage long sys_migrate_pages(pid_t pid, unsigned long maxnode,
				const unsigned long __user *from,
				const unsigned long __user *to);
asmlinkage long sys_move_pages(pid_t pid, unsigned long nr_pages,
				const void __user * __user *pages,
				const int __user *nodes,
				int __user *status,
				int flags);
asmlinkage long sys_mbind(unsigned long start, unsigned long len,
				unsigned long mode,
				unsigned long __user *nmask,
				unsigned long maxnode,
				unsigned flags);
asmlinkage long sys_get_mempolicy(int __user *policy,
				unsigned long __user *nmask,
				unsigned long maxnode,
				unsigned long addr, unsigned long flags);

asmlinkage long sys_inotify_init(void);
asmlinkage long sys_inotify_init1(int flags);
asmlinkage long sys_inotify_add_watch(int fd, const char __user *path,
					u32 mask);
asmlinkage long sys_inotify_rm_watch(int fd, __s32 wd);

asmlinkage long sys_spu_run(int fd, __u32 __user *unpc,
				 __u32 __user *ustatus);
asmlinkage long sys_spu_create(const char __user *name,
		unsigned int flags, umode_t mode, int fd);

asmlinkage long sys_mknodat(int dfd, const char __user * filename, umode_t mode,
			    unsigned dev);
asmlinkage long sys_mkdirat(int dfd, const char __user * pathname, umode_t mode);
asmlinkage long sys_unlinkat(int dfd, const char __user * pathname, int flag);
asmlinkage long sys_symlinkat(const char __user * oldname,
			      int newdfd, const char __user * newname);
asmlinkage long sys_linkat(int olddfd, const char __user *oldname,
			   int newdfd, const char __user *newname, int flags);
asmlinkage long sys_renameat(int olddfd, const char __user * oldname,
			     int newdfd, const char __user * newname);
asmlinkage long sys_futimesat(int dfd, const char __user *filename,
			      struct timeval __user *utimes);
asmlinkage long sys_faccessat(int dfd, const char __user *filename, int mode);
asmlinkage long sys_fchmodat(int dfd, const char __user * filename,
			     umode_t mode);
asmlinkage long sys_fchownat(int dfd, const char __user *filename, uid_t user,
			     gid_t group, int flag);
asmlinkage long sys_openat(int dfd, const char __user *filename, int flags,
			   umode_t mode);
asmlinkage long sys_newfstatat(int dfd, const char __user *filename,
			       struct stat __user *statbuf, int flag);
asmlinkage long sys_fstatat64(int dfd, const char __user *filename,
			       struct stat64 __user *statbuf, int flag);
asmlinkage long sys_readlinkat(int dfd, const char __user *path, char __user *buf,
			       int bufsiz);
asmlinkage long sys_utimensat(int dfd, const char __user *filename,
				struct timespec __user *utimes, int flags);
asmlinkage long sys_unshare(unsigned long unshare_flags);

asmlinkage long sys_splice(int fd_in, loff_t __user *off_in,
			   int fd_out, loff_t __user *off_out,
			   size_t len, unsigned int flags);

asmlinkage long sys_vmsplice(int fd, const struct iovec __user *iov,
			     unsigned long nr_segs, unsigned int flags);

asmlinkage long sys_tee(int fdin, int fdout, size_t len, unsigned int flags);

asmlinkage long sys_sync_file_range(int fd, loff_t offset, loff_t nbytes,
					unsigned int flags);
asmlinkage long sys_sync_file_range2(int fd, unsigned int flags,
				     loff_t offset, loff_t nbytes);
asmlinkage long sys_get_robust_list(int pid,
				    struct robust_list_head __user * __user *head_ptr,
				    size_t __user *len_ptr);
asmlinkage long sys_set_robust_list(struct robust_list_head __user *head,
				    size_t len);
asmlinkage long sys_getcpu(unsigned __user *cpu, unsigned __user *node, struct getcpu_cache __user *cache);
asmlinkage long sys_signalfd(int ufd, sigset_t __user *user_mask, size_t sizemask);
asmlinkage long sys_signalfd4(int ufd, sigset_t __user *user_mask, size_t sizemask, int flags);
asmlinkage long sys_timerfd_create(int clockid, int flags);
asmlinkage long sys_timerfd_settime(int ufd, int flags,
				    const struct itimerspec __user *utmr,
				    struct itimerspec __user *otmr);
asmlinkage long sys_timerfd_gettime(int ufd, struct itimerspec __user *otmr);
asmlinkage long sys_eventfd(unsigned int count);
asmlinkage long sys_eventfd2(unsigned int count, int flags);
asmlinkage long sys_fallocate(int fd, int mode, loff_t offset, loff_t len);
asmlinkage long sys_old_readdir(unsigned int, struct old_linux_dirent __user *, unsigned int);
asmlinkage long sys_pselect6(int, fd_set __user *, fd_set __user *,
			     fd_set __user *, struct timespec __user *,
			     void __user *);
asmlinkage long sys_ppoll(struct pollfd __user *, unsigned int,
			  struct timespec __user *, const sigset_t __user *,
			  size_t);
asmlinkage long sys_fanotify_init(unsigned int flags, unsigned int event_f_flags);
asmlinkage long sys_fanotify_mark(int fanotify_fd, unsigned int flags,
				  u64 mask, int fd,
				  const char  __user *pathname);
asmlinkage long sys_syncfs(int fd);

asmlinkage long sys_fork(void);
asmlinkage long sys_vfork(void);
#ifdef CONFIG_CLONE_BACKWARDS
asmlinkage long sys_clone(unsigned long, unsigned long, int __user *, int,
	       int __user *);
#else
asmlinkage long sys_clone(unsigned long, unsigned long, int __user *,
	       int __user *, int);
#endif

asmlinkage long sys_execve(const char __user *filename,
		const char __user *const __user *argv,
		const char __user *const __user *envp);

asmlinkage long sys_perf_event_open(
		struct perf_event_attr __user *attr_uptr,
		pid_t pid, int cpu, int group_fd, unsigned long flags);

asmlinkage long sys_mmap_pgoff(unsigned long addr, unsigned long len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff);
asmlinkage long sys_old_mmap(struct mmap_arg_struct __user *arg);
asmlinkage long sys_name_to_handle_at(int dfd, const char __user *name,
				      struct file_handle __user *handle,
				      int __user *mnt_id, int flag);
asmlinkage long sys_open_by_handle_at(int mountdirfd,
				      struct file_handle __user *handle,
				      int flags);
asmlinkage long sys_setns(int fd, int nstype);
asmlinkage long sys_process_vm_readv(pid_t pid,
				     const struct iovec __user *lvec,
				     unsigned long liovcnt,
				     const struct iovec __user *rvec,
				     unsigned long riovcnt,
				     unsigned long flags);
asmlinkage long sys_process_vm_writev(pid_t pid,
				      const struct iovec __user *lvec,
				      unsigned long liovcnt,
				      const struct iovec __user *rvec,
				      unsigned long riovcnt,
				      unsigned long flags);

asmlinkage long sys_kcmp(pid_t pid1, pid_t pid2, int type,
			 unsigned long idx1, unsigned long idx2);
asmlinkage long sys_finit_module(int fd, const char __user *uargs, int flags);
#endif
