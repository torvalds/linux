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

struct __aio_sigset;
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
struct user_msghdr;
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
struct sched_attr;
struct sel_arg_struct;
struct semaphore;
struct sembuf;
struct shmid_ds;
struct sockaddr;
struct stat;
struct stat64;
struct statfs;
struct statfs64;
struct statx;
struct __sysctl_args;
struct sysinfo;
struct timespec;
struct timeval;
struct __kernel_timex;
struct timezone;
struct tms;
struct utimbuf;
struct mq_attr;
struct compat_stat;
struct old_timeval32;
struct robust_list_head;
struct getcpu_cache;
struct old_linux_dirent;
struct perf_event_attr;
struct file_handle;
struct sigaltstack;
struct rseq;
union bpf_attr;
struct io_uring_params;

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
#include <linux/personality.h>
#include <trace/syscall.h>

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
/*
 * It may be useful for an architecture to override the definitions of the
 * SYSCALL_DEFINE0() and __SYSCALL_DEFINEx() macros, in particular to use a
 * different calling convention for syscalls. To allow for that, the prototypes
 * for the sys_*() functions below will *not* be included if
 * CONFIG_ARCH_HAS_SYSCALL_WRAPPER is enabled.
 */
#include <asm/syscall_wrapper.h>
#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */

/*
 * __MAP - apply a macro to syscall arguments
 * __MAP(n, m, t1, a1, t2, a2, ..., tn, an) will expand to
 *    m(t1, a1), m(t2, a2), ..., m(tn, an)
 * The first argument must be equal to the amount of type/name
 * pairs given.  Note that this list of pairs (i.e. the arguments
 * of __MAP starting at the third one) is in the same format as
 * for SYSCALL_DEFINE<n>/COMPAT_SYSCALL_DEFINE<n>
 */
#define __MAP0(m,...)
#define __MAP1(m,t,a,...) m(t,a)
#define __MAP2(m,t,a,...) m(t,a), __MAP1(m,__VA_ARGS__)
#define __MAP3(m,t,a,...) m(t,a), __MAP2(m,__VA_ARGS__)
#define __MAP4(m,t,a,...) m(t,a), __MAP3(m,__VA_ARGS__)
#define __MAP5(m,t,a,...) m(t,a), __MAP4(m,__VA_ARGS__)
#define __MAP6(m,t,a,...) m(t,a), __MAP5(m,__VA_ARGS__)
#define __MAP(n,...) __MAP##n(__VA_ARGS__)

#define __SC_DECL(t, a)	t a
#define __TYPE_AS(t, v)	__same_type((__force t)0, v)
#define __TYPE_IS_L(t)	(__TYPE_AS(t, 0L))
#define __TYPE_IS_UL(t)	(__TYPE_AS(t, 0UL))
#define __TYPE_IS_LL(t) (__TYPE_AS(t, 0LL) || __TYPE_AS(t, 0ULL))
#define __SC_LONG(t, a) __typeof(__builtin_choose_expr(__TYPE_IS_LL(t), 0LL, 0L)) a
#define __SC_CAST(t, a)	(__force t) a
#define __SC_ARGS(t, a)	a
#define __SC_TEST(t, a) (void)BUILD_BUG_ON_ZERO(!__TYPE_IS_LL(t) && sizeof(t) > sizeof(long))

#ifdef CONFIG_FTRACE_SYSCALLS
#define __SC_STR_ADECL(t, a)	#a
#define __SC_STR_TDECL(t, a)	#t

extern struct trace_event_class event_class_syscall_enter;
extern struct trace_event_class event_class_syscall_exit;
extern struct trace_event_functions enter_syscall_print_funcs;
extern struct trace_event_functions exit_syscall_print_funcs;

#define SYSCALL_TRACE_ENTER_EVENT(sname)				\
	static struct syscall_metadata __syscall_meta_##sname;		\
	static struct trace_event_call __used				\
	  event_enter_##sname = {					\
		.class			= &event_class_syscall_enter,	\
		{							\
			.name                   = "sys_enter"#sname,	\
		},							\
		.event.funcs            = &enter_syscall_print_funcs,	\
		.data			= (void *)&__syscall_meta_##sname,\
		.flags                  = TRACE_EVENT_FL_CAP_ANY,	\
	};								\
	static struct trace_event_call __used				\
	  __attribute__((section("_ftrace_events")))			\
	 *__event_enter_##sname = &event_enter_##sname;

#define SYSCALL_TRACE_EXIT_EVENT(sname)					\
	static struct syscall_metadata __syscall_meta_##sname;		\
	static struct trace_event_call __used				\
	  event_exit_##sname = {					\
		.class			= &event_class_syscall_exit,	\
		{							\
			.name                   = "sys_exit"#sname,	\
		},							\
		.event.funcs		= &exit_syscall_print_funcs,	\
		.data			= (void *)&__syscall_meta_##sname,\
		.flags                  = TRACE_EVENT_FL_CAP_ANY,	\
	};								\
	static struct trace_event_call __used				\
	  __attribute__((section("_ftrace_events")))			\
	*__event_exit_##sname = &event_exit_##sname;

#define SYSCALL_METADATA(sname, nb, ...)			\
	static const char *types_##sname[] = {			\
		__MAP(nb,__SC_STR_TDECL,__VA_ARGS__)		\
	};							\
	static const char *args_##sname[] = {			\
		__MAP(nb,__SC_STR_ADECL,__VA_ARGS__)		\
	};							\
	SYSCALL_TRACE_ENTER_EVENT(sname);			\
	SYSCALL_TRACE_EXIT_EVENT(sname);			\
	static struct syscall_metadata __used			\
	  __syscall_meta_##sname = {				\
		.name 		= "sys"#sname,			\
		.syscall_nr	= -1,	/* Filled in at boot */	\
		.nb_args 	= nb,				\
		.types		= nb ? types_##sname : NULL,	\
		.args		= nb ? args_##sname : NULL,	\
		.enter_event	= &event_enter_##sname,		\
		.exit_event	= &event_exit_##sname,		\
		.enter_fields	= LIST_HEAD_INIT(__syscall_meta_##sname.enter_fields), \
	};							\
	static struct syscall_metadata __used			\
	  __attribute__((section("__syscalls_metadata")))	\
	 *__p_syscall_meta_##sname = &__syscall_meta_##sname;

static inline int is_syscall_trace_event(struct trace_event_call *tp_event)
{
	return tp_event->class == &event_class_syscall_enter ||
	       tp_event->class == &event_class_syscall_exit;
}

#else
#define SYSCALL_METADATA(sname, nb, ...)

static inline int is_syscall_trace_event(struct trace_event_call *tp_event)
{
	return 0;
}
#endif

#ifndef SYSCALL_DEFINE0
#define SYSCALL_DEFINE0(sname)					\
	SYSCALL_METADATA(_##sname, 0);				\
	asmlinkage long sys_##sname(void);			\
	ALLOW_ERROR_INJECTION(sys_##sname, ERRNO);		\
	asmlinkage long sys_##sname(void)
#endif /* SYSCALL_DEFINE0 */

#define SYSCALL_DEFINE1(name, ...) SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE2(name, ...) SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE3(name, ...) SYSCALL_DEFINEx(3, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE4(name, ...) SYSCALL_DEFINEx(4, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE5(name, ...) SYSCALL_DEFINEx(5, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE6(name, ...) SYSCALL_DEFINEx(6, _##name, __VA_ARGS__)

#define SYSCALL_DEFINE_MAXARGS	6

#define SYSCALL_DEFINEx(x, sname, ...)				\
	SYSCALL_METADATA(sname, x, __VA_ARGS__)			\
	__SYSCALL_DEFINEx(x, sname, __VA_ARGS__)

#define __PROTECT(...) asmlinkage_protect(__VA_ARGS__)

/*
 * The asmlinkage stub is aliased to a function named __se_sys_*() which
 * sign-extends 32-bit ints to longs whenever needed. The actual work is
 * done within __do_sys_*().
 */
#ifndef __SYSCALL_DEFINEx
#define __SYSCALL_DEFINEx(x, name, ...)					\
	__diag_push();							\
	__diag_ignore(GCC, 8, "-Wattribute-alias",			\
		      "Type aliasing is used to sanitize syscall arguments");\
	asmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))	\
		__attribute__((alias(__stringify(__se_sys##name))));	\
	ALLOW_ERROR_INJECTION(sys##name, ERRNO);			\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));\
	asmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));	\
	asmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))	\
	{								\
		long ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));\
		__MAP(x,__SC_TEST,__VA_ARGS__);				\
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));	\
		return ret;						\
	}								\
	__diag_pop();							\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))
#endif /* __SYSCALL_DEFINEx */

/*
 * Called before coming back to user-mode. Returning to user-mode with an
 * address limit different than USER_DS can allow to overwrite kernel memory.
 */
static inline void addr_limit_user_check(void)
{
#ifdef TIF_FSCHECK
	if (!test_thread_flag(TIF_FSCHECK))
		return;
#endif

	if (CHECK_DATA_CORRUPTION(!segment_eq(get_fs(), USER_DS),
				  "Invalid address limit on user-mode return"))
		force_sig(SIGKILL, current);

#ifdef TIF_FSCHECK
	clear_thread_flag(TIF_FSCHECK);
#endif
}

/*
 * These syscall function prototypes are kept in the same order as
 * include/uapi/asm-generic/unistd.h. Architecture specific entries go below,
 * followed by deprecated or obsolete system calls.
 *
 * Please note that these prototypes here are only provided for information
 * purposes, for static analysis, and for linking from the syscall table.
 * These functions should not be called elsewhere from kernel code.
 *
 * As the syscall calling convention may be different from the default
 * for architectures overriding the syscall calling convention, do not
 * include the prototypes if CONFIG_ARCH_HAS_SYSCALL_WRAPPER is enabled.
 */
#ifndef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
asmlinkage long sys_io_setup(unsigned nr_reqs, aio_context_t __user *ctx);
asmlinkage long sys_io_destroy(aio_context_t ctx);
asmlinkage long sys_io_submit(aio_context_t, long,
			struct iocb __user * __user *);
asmlinkage long sys_io_cancel(aio_context_t ctx_id, struct iocb __user *iocb,
			      struct io_event __user *result);
asmlinkage long sys_io_getevents(aio_context_t ctx_id,
				long min_nr,
				long nr,
				struct io_event __user *events,
				struct __kernel_timespec __user *timeout);
asmlinkage long sys_io_getevents_time32(__u32 ctx_id,
				__s32 min_nr,
				__s32 nr,
				struct io_event __user *events,
				struct old_timespec32 __user *timeout);
asmlinkage long sys_io_pgetevents(aio_context_t ctx_id,
				long min_nr,
				long nr,
				struct io_event __user *events,
				struct __kernel_timespec __user *timeout,
				const struct __aio_sigset *sig);
asmlinkage long sys_io_pgetevents_time32(aio_context_t ctx_id,
				long min_nr,
				long nr,
				struct io_event __user *events,
				struct old_timespec32 __user *timeout,
				const struct __aio_sigset *sig);
asmlinkage long sys_io_uring_setup(u32 entries,
				struct io_uring_params __user *p);
asmlinkage long sys_io_uring_enter(unsigned int fd, u32 to_submit,
				u32 min_complete, u32 flags,
				const sigset_t __user *sig, size_t sigsz);
asmlinkage long sys_io_uring_register(unsigned int fd, unsigned int op,
				void __user *arg, unsigned int nr_args);

/* fs/xattr.c */
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

/* fs/dcache.c */
asmlinkage long sys_getcwd(char __user *buf, unsigned long size);

/* fs/cookies.c */
asmlinkage long sys_lookup_dcookie(u64 cookie64, char __user *buf, size_t len);

/* fs/eventfd.c */
asmlinkage long sys_eventfd2(unsigned int count, int flags);

/* fs/eventpoll.c */
asmlinkage long sys_epoll_create1(int flags);
asmlinkage long sys_epoll_ctl(int epfd, int op, int fd,
				struct epoll_event __user *event);
asmlinkage long sys_epoll_pwait(int epfd, struct epoll_event __user *events,
				int maxevents, int timeout,
				const sigset_t __user *sigmask,
				size_t sigsetsize);

/* fs/fcntl.c */
asmlinkage long sys_dup(unsigned int fildes);
asmlinkage long sys_dup3(unsigned int oldfd, unsigned int newfd, int flags);
asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);
#if BITS_PER_LONG == 32
asmlinkage long sys_fcntl64(unsigned int fd,
				unsigned int cmd, unsigned long arg);
#endif

/* fs/inotify_user.c */
asmlinkage long sys_inotify_init1(int flags);
asmlinkage long sys_inotify_add_watch(int fd, const char __user *path,
					u32 mask);
asmlinkage long sys_inotify_rm_watch(int fd, __s32 wd);

/* fs/ioctl.c */
asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd,
				unsigned long arg);

/* fs/ioprio.c */
asmlinkage long sys_ioprio_set(int which, int who, int ioprio);
asmlinkage long sys_ioprio_get(int which, int who);

/* fs/locks.c */
asmlinkage long sys_flock(unsigned int fd, unsigned int cmd);

/* fs/namei.c */
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

/* fs/namespace.c */
asmlinkage long sys_umount(char __user *name, int flags);
asmlinkage long sys_mount(char __user *dev_name, char __user *dir_name,
				char __user *type, unsigned long flags,
				void __user *data);
asmlinkage long sys_pivot_root(const char __user *new_root,
				const char __user *put_old);

/* fs/nfsctl.c */

/* fs/open.c */
asmlinkage long sys_statfs(const char __user * path,
				struct statfs __user *buf);
asmlinkage long sys_statfs64(const char __user *path, size_t sz,
				struct statfs64 __user *buf);
asmlinkage long sys_fstatfs(unsigned int fd, struct statfs __user *buf);
asmlinkage long sys_fstatfs64(unsigned int fd, size_t sz,
				struct statfs64 __user *buf);
asmlinkage long sys_truncate(const char __user *path, long length);
asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length);
#if BITS_PER_LONG == 32
asmlinkage long sys_truncate64(const char __user *path, loff_t length);
asmlinkage long sys_ftruncate64(unsigned int fd, loff_t length);
#endif
asmlinkage long sys_fallocate(int fd, int mode, loff_t offset, loff_t len);
asmlinkage long sys_faccessat(int dfd, const char __user *filename, int mode);
asmlinkage long sys_chdir(const char __user *filename);
asmlinkage long sys_fchdir(unsigned int fd);
asmlinkage long sys_chroot(const char __user *filename);
asmlinkage long sys_fchmod(unsigned int fd, umode_t mode);
asmlinkage long sys_fchmodat(int dfd, const char __user * filename,
			     umode_t mode);
asmlinkage long sys_fchownat(int dfd, const char __user *filename, uid_t user,
			     gid_t group, int flag);
asmlinkage long sys_fchown(unsigned int fd, uid_t user, gid_t group);
asmlinkage long sys_openat(int dfd, const char __user *filename, int flags,
			   umode_t mode);
asmlinkage long sys_close(unsigned int fd);
asmlinkage long sys_vhangup(void);

/* fs/pipe.c */
asmlinkage long sys_pipe2(int __user *fildes, int flags);

/* fs/quota.c */
asmlinkage long sys_quotactl(unsigned int cmd, const char __user *special,
				qid_t id, void __user *addr);

/* fs/readdir.c */
asmlinkage long sys_getdents64(unsigned int fd,
				struct linux_dirent64 __user *dirent,
				unsigned int count);

/* fs/read_write.c */
asmlinkage long sys_llseek(unsigned int fd, unsigned long offset_high,
			unsigned long offset_low, loff_t __user *result,
			unsigned int whence);
asmlinkage long sys_lseek(unsigned int fd, off_t offset,
			  unsigned int whence);
asmlinkage long sys_read(unsigned int fd, char __user *buf, size_t count);
asmlinkage long sys_write(unsigned int fd, const char __user *buf,
			  size_t count);
asmlinkage long sys_readv(unsigned long fd,
			  const struct iovec __user *vec,
			  unsigned long vlen);
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

/* fs/sendfile.c */
asmlinkage long sys_sendfile64(int out_fd, int in_fd,
			       loff_t __user *offset, size_t count);

/* fs/select.c */
asmlinkage long sys_pselect6(int, fd_set __user *, fd_set __user *,
			     fd_set __user *, struct __kernel_timespec __user *,
			     void __user *);
asmlinkage long sys_pselect6_time32(int, fd_set __user *, fd_set __user *,
			     fd_set __user *, struct old_timespec32 __user *,
			     void __user *);
asmlinkage long sys_ppoll(struct pollfd __user *, unsigned int,
			  struct __kernel_timespec __user *, const sigset_t __user *,
			  size_t);
asmlinkage long sys_ppoll_time32(struct pollfd __user *, unsigned int,
			  struct old_timespec32 __user *, const sigset_t __user *,
			  size_t);

/* fs/signalfd.c */
asmlinkage long sys_signalfd4(int ufd, sigset_t __user *user_mask, size_t sizemask, int flags);

/* fs/splice.c */
asmlinkage long sys_vmsplice(int fd, const struct iovec __user *iov,
			     unsigned long nr_segs, unsigned int flags);
asmlinkage long sys_splice(int fd_in, loff_t __user *off_in,
			   int fd_out, loff_t __user *off_out,
			   size_t len, unsigned int flags);
asmlinkage long sys_tee(int fdin, int fdout, size_t len, unsigned int flags);

/* fs/stat.c */
asmlinkage long sys_readlinkat(int dfd, const char __user *path, char __user *buf,
			       int bufsiz);
asmlinkage long sys_newfstatat(int dfd, const char __user *filename,
			       struct stat __user *statbuf, int flag);
asmlinkage long sys_newfstat(unsigned int fd, struct stat __user *statbuf);
#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)
asmlinkage long sys_fstat64(unsigned long fd, struct stat64 __user *statbuf);
asmlinkage long sys_fstatat64(int dfd, const char __user *filename,
			       struct stat64 __user *statbuf, int flag);
#endif

/* fs/sync.c */
asmlinkage long sys_sync(void);
asmlinkage long sys_fsync(unsigned int fd);
asmlinkage long sys_fdatasync(unsigned int fd);
asmlinkage long sys_sync_file_range2(int fd, unsigned int flags,
				     loff_t offset, loff_t nbytes);
asmlinkage long sys_sync_file_range(int fd, loff_t offset, loff_t nbytes,
					unsigned int flags);

/* fs/timerfd.c */
asmlinkage long sys_timerfd_create(int clockid, int flags);
asmlinkage long sys_timerfd_settime(int ufd, int flags,
				    const struct __kernel_itimerspec __user *utmr,
				    struct __kernel_itimerspec __user *otmr);
asmlinkage long sys_timerfd_gettime(int ufd, struct __kernel_itimerspec __user *otmr);
asmlinkage long sys_timerfd_gettime32(int ufd,
				   struct old_itimerspec32 __user *otmr);
asmlinkage long sys_timerfd_settime32(int ufd, int flags,
				   const struct old_itimerspec32 __user *utmr,
				   struct old_itimerspec32 __user *otmr);

/* fs/utimes.c */
asmlinkage long sys_utimensat(int dfd, const char __user *filename,
				struct __kernel_timespec __user *utimes,
				int flags);
asmlinkage long sys_utimensat_time32(unsigned int dfd,
				const char __user *filename,
				struct old_timespec32 __user *t, int flags);

/* kernel/acct.c */
asmlinkage long sys_acct(const char __user *name);

/* kernel/capability.c */
asmlinkage long sys_capget(cap_user_header_t header,
				cap_user_data_t dataptr);
asmlinkage long sys_capset(cap_user_header_t header,
				const cap_user_data_t data);

/* kernel/exec_domain.c */
asmlinkage long sys_personality(unsigned int personality);

/* kernel/exit.c */
asmlinkage long sys_exit(int error_code);
asmlinkage long sys_exit_group(int error_code);
asmlinkage long sys_waitid(int which, pid_t pid,
			   struct siginfo __user *infop,
			   int options, struct rusage __user *ru);

/* kernel/fork.c */
asmlinkage long sys_set_tid_address(int __user *tidptr);
asmlinkage long sys_unshare(unsigned long unshare_flags);

/* kernel/futex.c */
asmlinkage long sys_futex(u32 __user *uaddr, int op, u32 val,
			struct __kernel_timespec __user *utime, u32 __user *uaddr2,
			u32 val3);
asmlinkage long sys_futex_time32(u32 __user *uaddr, int op, u32 val,
			struct old_timespec32 __user *utime, u32 __user *uaddr2,
			u32 val3);
asmlinkage long sys_get_robust_list(int pid,
				    struct robust_list_head __user * __user *head_ptr,
				    size_t __user *len_ptr);
asmlinkage long sys_set_robust_list(struct robust_list_head __user *head,
				    size_t len);

/* kernel/hrtimer.c */
asmlinkage long sys_nanosleep(struct __kernel_timespec __user *rqtp,
			      struct __kernel_timespec __user *rmtp);
asmlinkage long sys_nanosleep_time32(struct old_timespec32 __user *rqtp,
				     struct old_timespec32 __user *rmtp);

/* kernel/itimer.c */
asmlinkage long sys_getitimer(int which, struct itimerval __user *value);
asmlinkage long sys_setitimer(int which,
				struct itimerval __user *value,
				struct itimerval __user *ovalue);

/* kernel/kexec.c */
asmlinkage long sys_kexec_load(unsigned long entry, unsigned long nr_segments,
				struct kexec_segment __user *segments,
				unsigned long flags);

/* kernel/module.c */
asmlinkage long sys_init_module(void __user *umod, unsigned long len,
				const char __user *uargs);
asmlinkage long sys_delete_module(const char __user *name_user,
				unsigned int flags);

/* kernel/posix-timers.c */
asmlinkage long sys_timer_create(clockid_t which_clock,
				 struct sigevent __user *timer_event_spec,
				 timer_t __user * created_timer_id);
asmlinkage long sys_timer_gettime(timer_t timer_id,
				struct __kernel_itimerspec __user *setting);
asmlinkage long sys_timer_getoverrun(timer_t timer_id);
asmlinkage long sys_timer_settime(timer_t timer_id, int flags,
				const struct __kernel_itimerspec __user *new_setting,
				struct __kernel_itimerspec __user *old_setting);
asmlinkage long sys_timer_delete(timer_t timer_id);
asmlinkage long sys_clock_settime(clockid_t which_clock,
				const struct __kernel_timespec __user *tp);
asmlinkage long sys_clock_gettime(clockid_t which_clock,
				struct __kernel_timespec __user *tp);
asmlinkage long sys_clock_getres(clockid_t which_clock,
				struct __kernel_timespec __user *tp);
asmlinkage long sys_clock_nanosleep(clockid_t which_clock, int flags,
				const struct __kernel_timespec __user *rqtp,
				struct __kernel_timespec __user *rmtp);
asmlinkage long sys_timer_gettime32(timer_t timer_id,
				 struct old_itimerspec32 __user *setting);
asmlinkage long sys_timer_settime32(timer_t timer_id, int flags,
					 struct old_itimerspec32 __user *new,
					 struct old_itimerspec32 __user *old);
asmlinkage long sys_clock_settime32(clockid_t which_clock,
				struct old_timespec32 __user *tp);
asmlinkage long sys_clock_gettime32(clockid_t which_clock,
				struct old_timespec32 __user *tp);
asmlinkage long sys_clock_getres_time32(clockid_t which_clock,
				struct old_timespec32 __user *tp);
asmlinkage long sys_clock_nanosleep_time32(clockid_t which_clock, int flags,
				struct old_timespec32 __user *rqtp,
				struct old_timespec32 __user *rmtp);

/* kernel/printk.c */
asmlinkage long sys_syslog(int type, char __user *buf, int len);

/* kernel/ptrace.c */
asmlinkage long sys_ptrace(long request, long pid, unsigned long addr,
			   unsigned long data);
/* kernel/sched/core.c */

asmlinkage long sys_sched_setparam(pid_t pid,
					struct sched_param __user *param);
asmlinkage long sys_sched_setscheduler(pid_t pid, int policy,
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
				struct __kernel_timespec __user *interval);
asmlinkage long sys_sched_rr_get_interval_time32(pid_t pid,
						 struct old_timespec32 __user *interval);

/* kernel/signal.c */
asmlinkage long sys_restart_syscall(void);
asmlinkage long sys_kill(pid_t pid, int sig);
asmlinkage long sys_tkill(pid_t pid, int sig);
asmlinkage long sys_tgkill(pid_t tgid, pid_t pid, int sig);
asmlinkage long sys_sigaltstack(const struct sigaltstack __user *uss,
				struct sigaltstack __user *uoss);
asmlinkage long sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize);
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
				const struct __kernel_timespec __user *uts,
				size_t sigsetsize);
asmlinkage long sys_rt_sigtimedwait_time32(const sigset_t __user *uthese,
				siginfo_t __user *uinfo,
				const struct old_timespec32 __user *uts,
				size_t sigsetsize);
asmlinkage long sys_rt_sigqueueinfo(pid_t pid, int sig, siginfo_t __user *uinfo);

/* kernel/sys.c */
asmlinkage long sys_setpriority(int which, int who, int niceval);
asmlinkage long sys_getpriority(int which, int who);
asmlinkage long sys_reboot(int magic1, int magic2, unsigned int cmd,
				void __user *arg);
asmlinkage long sys_setregid(gid_t rgid, gid_t egid);
asmlinkage long sys_setgid(gid_t gid);
asmlinkage long sys_setreuid(uid_t ruid, uid_t euid);
asmlinkage long sys_setuid(uid_t uid);
asmlinkage long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
asmlinkage long sys_getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid);
asmlinkage long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
asmlinkage long sys_getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid);
asmlinkage long sys_setfsuid(uid_t uid);
asmlinkage long sys_setfsgid(gid_t gid);
asmlinkage long sys_times(struct tms __user *tbuf);
asmlinkage long sys_setpgid(pid_t pid, pid_t pgid);
asmlinkage long sys_getpgid(pid_t pid);
asmlinkage long sys_getsid(pid_t pid);
asmlinkage long sys_setsid(void);
asmlinkage long sys_getgroups(int gidsetsize, gid_t __user *grouplist);
asmlinkage long sys_setgroups(int gidsetsize, gid_t __user *grouplist);
asmlinkage long sys_newuname(struct new_utsname __user *name);
asmlinkage long sys_sethostname(char __user *name, int len);
asmlinkage long sys_setdomainname(char __user *name, int len);
asmlinkage long sys_getrlimit(unsigned int resource,
				struct rlimit __user *rlim);
asmlinkage long sys_setrlimit(unsigned int resource,
				struct rlimit __user *rlim);
asmlinkage long sys_getrusage(int who, struct rusage __user *ru);
asmlinkage long sys_umask(int mask);
asmlinkage long sys_prctl(int option, unsigned long arg2, unsigned long arg3,
			unsigned long arg4, unsigned long arg5);
asmlinkage long sys_getcpu(unsigned __user *cpu, unsigned __user *node, struct getcpu_cache __user *cache);

/* kernel/time.c */
asmlinkage long sys_gettimeofday(struct timeval __user *tv,
				struct timezone __user *tz);
asmlinkage long sys_settimeofday(struct timeval __user *tv,
				struct timezone __user *tz);
asmlinkage long sys_adjtimex(struct __kernel_timex __user *txc_p);
asmlinkage long sys_adjtimex_time32(struct old_timex32 __user *txc_p);

/* kernel/timer.c */
asmlinkage long sys_getpid(void);
asmlinkage long sys_getppid(void);
asmlinkage long sys_getuid(void);
asmlinkage long sys_geteuid(void);
asmlinkage long sys_getgid(void);
asmlinkage long sys_getegid(void);
asmlinkage long sys_gettid(void);
asmlinkage long sys_sysinfo(struct sysinfo __user *info);

/* ipc/mqueue.c */
asmlinkage long sys_mq_open(const char __user *name, int oflag, umode_t mode, struct mq_attr __user *attr);
asmlinkage long sys_mq_unlink(const char __user *name);
asmlinkage long sys_mq_timedsend(mqd_t mqdes, const char __user *msg_ptr, size_t msg_len, unsigned int msg_prio, const struct __kernel_timespec __user *abs_timeout);
asmlinkage long sys_mq_timedreceive(mqd_t mqdes, char __user *msg_ptr, size_t msg_len, unsigned int __user *msg_prio, const struct __kernel_timespec __user *abs_timeout);
asmlinkage long sys_mq_notify(mqd_t mqdes, const struct sigevent __user *notification);
asmlinkage long sys_mq_getsetattr(mqd_t mqdes, const struct mq_attr __user *mqstat, struct mq_attr __user *omqstat);
asmlinkage long sys_mq_timedreceive_time32(mqd_t mqdes,
			char __user *u_msg_ptr,
			unsigned int msg_len, unsigned int __user *u_msg_prio,
			const struct old_timespec32 __user *u_abs_timeout);
asmlinkage long sys_mq_timedsend_time32(mqd_t mqdes,
			const char __user *u_msg_ptr,
			unsigned int msg_len, unsigned int msg_prio,
			const struct old_timespec32 __user *u_abs_timeout);

/* ipc/msg.c */
asmlinkage long sys_msgget(key_t key, int msgflg);
asmlinkage long sys_old_msgctl(int msqid, int cmd, struct msqid_ds __user *buf);
asmlinkage long sys_msgctl(int msqid, int cmd, struct msqid_ds __user *buf);
asmlinkage long sys_msgrcv(int msqid, struct msgbuf __user *msgp,
				size_t msgsz, long msgtyp, int msgflg);
asmlinkage long sys_msgsnd(int msqid, struct msgbuf __user *msgp,
				size_t msgsz, int msgflg);

/* ipc/sem.c */
asmlinkage long sys_semget(key_t key, int nsems, int semflg);
asmlinkage long sys_semctl(int semid, int semnum, int cmd, unsigned long arg);
asmlinkage long sys_old_semctl(int semid, int semnum, int cmd, unsigned long arg);
asmlinkage long sys_semtimedop(int semid, struct sembuf __user *sops,
				unsigned nsops,
				const struct __kernel_timespec __user *timeout);
asmlinkage long sys_semtimedop_time32(int semid, struct sembuf __user *sops,
				unsigned nsops,
				const struct old_timespec32 __user *timeout);
asmlinkage long sys_semop(int semid, struct sembuf __user *sops,
				unsigned nsops);

/* ipc/shm.c */
asmlinkage long sys_shmget(key_t key, size_t size, int flag);
asmlinkage long sys_old_shmctl(int shmid, int cmd, struct shmid_ds __user *buf);
asmlinkage long sys_shmctl(int shmid, int cmd, struct shmid_ds __user *buf);
asmlinkage long sys_shmat(int shmid, char __user *shmaddr, int shmflg);
asmlinkage long sys_shmdt(char __user *shmaddr);

/* net/socket.c */
asmlinkage long sys_socket(int, int, int);
asmlinkage long sys_socketpair(int, int, int, int __user *);
asmlinkage long sys_bind(int, struct sockaddr __user *, int);
asmlinkage long sys_listen(int, int);
asmlinkage long sys_accept(int, struct sockaddr __user *, int __user *);
asmlinkage long sys_connect(int, struct sockaddr __user *, int);
asmlinkage long sys_getsockname(int, struct sockaddr __user *, int __user *);
asmlinkage long sys_getpeername(int, struct sockaddr __user *, int __user *);
asmlinkage long sys_sendto(int, void __user *, size_t, unsigned,
				struct sockaddr __user *, int);
asmlinkage long sys_recvfrom(int, void __user *, size_t, unsigned,
				struct sockaddr __user *, int __user *);
asmlinkage long sys_setsockopt(int fd, int level, int optname,
				char __user *optval, int optlen);
asmlinkage long sys_getsockopt(int fd, int level, int optname,
				char __user *optval, int __user *optlen);
asmlinkage long sys_shutdown(int, int);
asmlinkage long sys_sendmsg(int fd, struct user_msghdr __user *msg, unsigned flags);
asmlinkage long sys_recvmsg(int fd, struct user_msghdr __user *msg, unsigned flags);

/* mm/filemap.c */
asmlinkage long sys_readahead(int fd, loff_t offset, size_t count);

/* mm/nommu.c, also with MMU */
asmlinkage long sys_brk(unsigned long brk);
asmlinkage long sys_munmap(unsigned long addr, size_t len);
asmlinkage long sys_mremap(unsigned long addr,
			   unsigned long old_len, unsigned long new_len,
			   unsigned long flags, unsigned long new_addr);

/* security/keys/keyctl.c */
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

/* arch/example/kernel/sys_example.c */
#ifdef CONFIG_CLONE_BACKWARDS
asmlinkage long sys_clone(unsigned long, unsigned long, int __user *, unsigned long,
	       int __user *);
#else
#ifdef CONFIG_CLONE_BACKWARDS3
asmlinkage long sys_clone(unsigned long, unsigned long, int, int __user *,
			  int __user *, unsigned long);
#else
asmlinkage long sys_clone(unsigned long, unsigned long, int __user *,
	       int __user *, unsigned long);
#endif
#endif
asmlinkage long sys_execve(const char __user *filename,
		const char __user *const __user *argv,
		const char __user *const __user *envp);

/* mm/fadvise.c */
asmlinkage long sys_fadvise64_64(int fd, loff_t offset, loff_t len, int advice);

/* mm/, CONFIG_MMU only */
asmlinkage long sys_swapon(const char __user *specialfile, int swap_flags);
asmlinkage long sys_swapoff(const char __user *specialfile);
asmlinkage long sys_mprotect(unsigned long start, size_t len,
				unsigned long prot);
asmlinkage long sys_msync(unsigned long start, size_t len, int flags);
asmlinkage long sys_mlock(unsigned long start, size_t len);
asmlinkage long sys_munlock(unsigned long start, size_t len);
asmlinkage long sys_mlockall(int flags);
asmlinkage long sys_munlockall(void);
asmlinkage long sys_mincore(unsigned long start, size_t len,
				unsigned char __user * vec);
asmlinkage long sys_madvise(unsigned long start, size_t len, int behavior);
asmlinkage long sys_remap_file_pages(unsigned long start, unsigned long size,
			unsigned long prot, unsigned long pgoff,
			unsigned long flags);
asmlinkage long sys_mbind(unsigned long start, unsigned long len,
				unsigned long mode,
				const unsigned long __user *nmask,
				unsigned long maxnode,
				unsigned flags);
asmlinkage long sys_get_mempolicy(int __user *policy,
				unsigned long __user *nmask,
				unsigned long maxnode,
				unsigned long addr, unsigned long flags);
asmlinkage long sys_set_mempolicy(int mode, const unsigned long __user *nmask,
				unsigned long maxnode);
asmlinkage long sys_migrate_pages(pid_t pid, unsigned long maxnode,
				const unsigned long __user *from,
				const unsigned long __user *to);
asmlinkage long sys_move_pages(pid_t pid, unsigned long nr_pages,
				const void __user * __user *pages,
				const int __user *nodes,
				int __user *status,
				int flags);

asmlinkage long sys_rt_tgsigqueueinfo(pid_t tgid, pid_t  pid, int sig,
		siginfo_t __user *uinfo);
asmlinkage long sys_perf_event_open(
		struct perf_event_attr __user *attr_uptr,
		pid_t pid, int cpu, int group_fd, unsigned long flags);
asmlinkage long sys_accept4(int, struct sockaddr __user *, int __user *, int);
asmlinkage long sys_recvmmsg(int fd, struct mmsghdr __user *msg,
			     unsigned int vlen, unsigned flags,
			     struct __kernel_timespec __user *timeout);
asmlinkage long sys_recvmmsg_time32(int fd, struct mmsghdr __user *msg,
			     unsigned int vlen, unsigned flags,
			     struct old_timespec32 __user *timeout);

asmlinkage long sys_wait4(pid_t pid, int __user *stat_addr,
				int options, struct rusage __user *ru);
asmlinkage long sys_prlimit64(pid_t pid, unsigned int resource,
				const struct rlimit64 __user *new_rlim,
				struct rlimit64 __user *old_rlim);
asmlinkage long sys_fanotify_init(unsigned int flags, unsigned int event_f_flags);
asmlinkage long sys_fanotify_mark(int fanotify_fd, unsigned int flags,
				  u64 mask, int fd,
				  const char  __user *pathname);
asmlinkage long sys_name_to_handle_at(int dfd, const char __user *name,
				      struct file_handle __user *handle,
				      int __user *mnt_id, int flag);
asmlinkage long sys_open_by_handle_at(int mountdirfd,
				      struct file_handle __user *handle,
				      int flags);
asmlinkage long sys_clock_adjtime(clockid_t which_clock,
				struct __kernel_timex __user *tx);
asmlinkage long sys_clock_adjtime32(clockid_t which_clock,
				struct old_timex32 __user *tx);
asmlinkage long sys_syncfs(int fd);
asmlinkage long sys_setns(int fd, int nstype);
asmlinkage long sys_sendmmsg(int fd, struct mmsghdr __user *msg,
			     unsigned int vlen, unsigned flags);
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
asmlinkage long sys_sched_setattr(pid_t pid,
					struct sched_attr __user *attr,
					unsigned int flags);
asmlinkage long sys_sched_getattr(pid_t pid,
					struct sched_attr __user *attr,
					unsigned int size,
					unsigned int flags);
asmlinkage long sys_renameat2(int olddfd, const char __user *oldname,
			      int newdfd, const char __user *newname,
			      unsigned int flags);
asmlinkage long sys_seccomp(unsigned int op, unsigned int flags,
			    void __user *uargs);
asmlinkage long sys_getrandom(char __user *buf, size_t count,
			      unsigned int flags);
asmlinkage long sys_memfd_create(const char __user *uname_ptr, unsigned int flags);
asmlinkage long sys_bpf(int cmd, union bpf_attr *attr, unsigned int size);
asmlinkage long sys_execveat(int dfd, const char __user *filename,
			const char __user *const __user *argv,
			const char __user *const __user *envp, int flags);
asmlinkage long sys_userfaultfd(int flags);
asmlinkage long sys_membarrier(int cmd, int flags);
asmlinkage long sys_mlock2(unsigned long start, size_t len, int flags);
asmlinkage long sys_copy_file_range(int fd_in, loff_t __user *off_in,
				    int fd_out, loff_t __user *off_out,
				    size_t len, unsigned int flags);
asmlinkage long sys_preadv2(unsigned long fd, const struct iovec __user *vec,
			    unsigned long vlen, unsigned long pos_l, unsigned long pos_h,
			    rwf_t flags);
asmlinkage long sys_pwritev2(unsigned long fd, const struct iovec __user *vec,
			    unsigned long vlen, unsigned long pos_l, unsigned long pos_h,
			    rwf_t flags);
asmlinkage long sys_pkey_mprotect(unsigned long start, size_t len,
				  unsigned long prot, int pkey);
asmlinkage long sys_pkey_alloc(unsigned long flags, unsigned long init_val);
asmlinkage long sys_pkey_free(int pkey);
asmlinkage long sys_statx(int dfd, const char __user *path, unsigned flags,
			  unsigned mask, struct statx __user *buffer);
asmlinkage long sys_rseq(struct rseq __user *rseq, uint32_t rseq_len,
			 int flags, uint32_t sig);

/*
 * Architecture-specific system calls
 */

/* arch/x86/kernel/ioport.c */
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int on);

/* pciconfig: alpha, arm, arm64, ia64, sparc */
asmlinkage long sys_pciconfig_read(unsigned long bus, unsigned long dfn,
				unsigned long off, unsigned long len,
				void __user *buf);
asmlinkage long sys_pciconfig_write(unsigned long bus, unsigned long dfn,
				unsigned long off, unsigned long len,
				void __user *buf);
asmlinkage long sys_pciconfig_iobase(long which, unsigned long bus, unsigned long devfn);

/* powerpc */
asmlinkage long sys_spu_run(int fd, __u32 __user *unpc,
				 __u32 __user *ustatus);
asmlinkage long sys_spu_create(const char __user *name,
		unsigned int flags, umode_t mode, int fd);


/*
 * Deprecated system calls which are still defined in
 * include/uapi/asm-generic/unistd.h and wanted by >= 1 arch
 */

/* __ARCH_WANT_SYSCALL_NO_AT */
asmlinkage long sys_open(const char __user *filename,
				int flags, umode_t mode);
asmlinkage long sys_link(const char __user *oldname,
				const char __user *newname);
asmlinkage long sys_unlink(const char __user *pathname);
asmlinkage long sys_mknod(const char __user *filename, umode_t mode,
				unsigned dev);
asmlinkage long sys_chmod(const char __user *filename, umode_t mode);
asmlinkage long sys_chown(const char __user *filename,
				uid_t user, gid_t group);
asmlinkage long sys_mkdir(const char __user *pathname, umode_t mode);
asmlinkage long sys_rmdir(const char __user *pathname);
asmlinkage long sys_lchown(const char __user *filename,
				uid_t user, gid_t group);
asmlinkage long sys_access(const char __user *filename, int mode);
asmlinkage long sys_rename(const char __user *oldname,
				const char __user *newname);
asmlinkage long sys_symlink(const char __user *old, const char __user *new);
#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)
asmlinkage long sys_stat64(const char __user *filename,
				struct stat64 __user *statbuf);
asmlinkage long sys_lstat64(const char __user *filename,
				struct stat64 __user *statbuf);
#endif

/* __ARCH_WANT_SYSCALL_NO_FLAGS */
asmlinkage long sys_pipe(int __user *fildes);
asmlinkage long sys_dup2(unsigned int oldfd, unsigned int newfd);
asmlinkage long sys_epoll_create(int size);
asmlinkage long sys_inotify_init(void);
asmlinkage long sys_eventfd(unsigned int count);
asmlinkage long sys_signalfd(int ufd, sigset_t __user *user_mask, size_t sizemask);

/* __ARCH_WANT_SYSCALL_OFF_T */
asmlinkage long sys_sendfile(int out_fd, int in_fd,
			     off_t __user *offset, size_t count);
asmlinkage long sys_newstat(const char __user *filename,
				struct stat __user *statbuf);
asmlinkage long sys_newlstat(const char __user *filename,
				struct stat __user *statbuf);
asmlinkage long sys_fadvise64(int fd, loff_t offset, size_t len, int advice);

/* __ARCH_WANT_SYSCALL_DEPRECATED */
asmlinkage long sys_alarm(unsigned int seconds);
asmlinkage long sys_getpgrp(void);
asmlinkage long sys_pause(void);
asmlinkage long sys_time(time_t __user *tloc);
asmlinkage long sys_time32(old_time32_t __user *tloc);
#ifdef __ARCH_WANT_SYS_UTIME
asmlinkage long sys_utime(char __user *filename,
				struct utimbuf __user *times);
asmlinkage long sys_utimes(char __user *filename,
				struct timeval __user *utimes);
asmlinkage long sys_futimesat(int dfd, const char __user *filename,
			      struct timeval __user *utimes);
#endif
asmlinkage long sys_futimesat_time32(unsigned int dfd,
				     const char __user *filename,
				     struct old_timeval32 __user *t);
asmlinkage long sys_utime32(const char __user *filename,
				 struct old_utimbuf32 __user *t);
asmlinkage long sys_utimes_time32(const char __user *filename,
				  struct old_timeval32 __user *t);
asmlinkage long sys_creat(const char __user *pathname, umode_t mode);
asmlinkage long sys_getdents(unsigned int fd,
				struct linux_dirent __user *dirent,
				unsigned int count);
asmlinkage long sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			fd_set __user *exp, struct timeval __user *tvp);
asmlinkage long sys_poll(struct pollfd __user *ufds, unsigned int nfds,
				int timeout);
asmlinkage long sys_epoll_wait(int epfd, struct epoll_event __user *events,
				int maxevents, int timeout);
asmlinkage long sys_ustat(unsigned dev, struct ustat __user *ubuf);
asmlinkage long sys_vfork(void);
asmlinkage long sys_recv(int, void __user *, size_t, unsigned);
asmlinkage long sys_send(int, void __user *, size_t, unsigned);
asmlinkage long sys_bdflush(int func, long data);
asmlinkage long sys_oldumount(char __user *name);
asmlinkage long sys_uselib(const char __user *library);
asmlinkage long sys_sysctl(struct __sysctl_args __user *args);
asmlinkage long sys_sysfs(int option,
				unsigned long arg1, unsigned long arg2);
asmlinkage long sys_fork(void);

/* obsolete: kernel/time/time.c */
asmlinkage long sys_stime(time_t __user *tptr);
asmlinkage long sys_stime32(old_time32_t __user *tptr);

/* obsolete: kernel/signal.c */
asmlinkage long sys_sigpending(old_sigset_t __user *uset);
asmlinkage long sys_sigprocmask(int how, old_sigset_t __user *set,
				old_sigset_t __user *oset);
#ifdef CONFIG_OLD_SIGSUSPEND
asmlinkage long sys_sigsuspend(old_sigset_t mask);
#endif

#ifdef CONFIG_OLD_SIGSUSPEND3
asmlinkage long sys_sigsuspend(int unused1, int unused2, old_sigset_t mask);
#endif

#ifdef CONFIG_OLD_SIGACTION
asmlinkage long sys_sigaction(int, const struct old_sigaction __user *,
				struct old_sigaction __user *);
#endif
asmlinkage long sys_sgetmask(void);
asmlinkage long sys_ssetmask(int newmask);
asmlinkage long sys_signal(int sig, __sighandler_t handler);

/* obsolete: kernel/sched/core.c */
asmlinkage long sys_nice(int increment);

/* obsolete: kernel/kexec_file.c */
asmlinkage long sys_kexec_file_load(int kernel_fd, int initrd_fd,
				    unsigned long cmdline_len,
				    const char __user *cmdline_ptr,
				    unsigned long flags);

/* obsolete: kernel/exit.c */
asmlinkage long sys_waitpid(pid_t pid, int __user *stat_addr, int options);

/* obsolete: kernel/uid16.c */
#ifdef CONFIG_HAVE_UID16
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

/* obsolete: net/socket.c */
asmlinkage long sys_socketcall(int call, unsigned long __user *args);

/* obsolete: fs/stat.c */
asmlinkage long sys_stat(const char __user *filename,
			struct __old_kernel_stat __user *statbuf);
asmlinkage long sys_lstat(const char __user *filename,
			struct __old_kernel_stat __user *statbuf);
asmlinkage long sys_fstat(unsigned int fd,
			struct __old_kernel_stat __user *statbuf);
asmlinkage long sys_readlink(const char __user *path,
				char __user *buf, int bufsiz);

/* obsolete: fs/select.c */
asmlinkage long sys_old_select(struct sel_arg_struct __user *arg);

/* obsolete: fs/readdir.c */
asmlinkage long sys_old_readdir(unsigned int, struct old_linux_dirent __user *, unsigned int);

/* obsolete: kernel/sys.c */
asmlinkage long sys_gethostname(char __user *name, int len);
asmlinkage long sys_uname(struct old_utsname __user *);
asmlinkage long sys_olduname(struct oldold_utsname __user *);
#ifdef __ARCH_WANT_SYS_OLD_GETRLIMIT
asmlinkage long sys_old_getrlimit(unsigned int resource, struct rlimit __user *rlim);
#endif

/* obsolete: ipc */
asmlinkage long sys_ipc(unsigned int call, int first, unsigned long second,
		unsigned long third, void __user *ptr, long fifth);

/* obsolete: mm/ */
asmlinkage long sys_mmap_pgoff(unsigned long addr, unsigned long len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff);
asmlinkage long sys_old_mmap(struct mmap_arg_struct __user *arg);


/*
 * Not a real system call, but a placeholder for syscalls which are
 * not implemented -- see kernel/sys_ni.c
 */
asmlinkage long sys_ni_syscall(void);

#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */


/*
 * Kernel code should not call syscalls (i.e., sys_xyzyyz()) directly.
 * Instead, use one of the functions which work equivalently, such as
 * the ksys_xyzyyz() functions prototyped below.
 */

int ksys_mount(char __user *dev_name, char __user *dir_name, char __user *type,
	       unsigned long flags, void __user *data);
int ksys_umount(char __user *name, int flags);
int ksys_dup(unsigned int fildes);
int ksys_chroot(const char __user *filename);
ssize_t ksys_write(unsigned int fd, const char __user *buf, size_t count);
int ksys_chdir(const char __user *filename);
int ksys_fchmod(unsigned int fd, umode_t mode);
int ksys_fchown(unsigned int fd, uid_t user, gid_t group);
int ksys_getdents64(unsigned int fd, struct linux_dirent64 __user *dirent,
		    unsigned int count);
int ksys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);
off_t ksys_lseek(unsigned int fd, off_t offset, unsigned int whence);
ssize_t ksys_read(unsigned int fd, char __user *buf, size_t count);
void ksys_sync(void);
int ksys_unshare(unsigned long unshare_flags);
int ksys_setsid(void);
int ksys_sync_file_range(int fd, loff_t offset, loff_t nbytes,
			 unsigned int flags);
ssize_t ksys_pread64(unsigned int fd, char __user *buf, size_t count,
		     loff_t pos);
ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf,
		      size_t count, loff_t pos);
int ksys_fallocate(int fd, int mode, loff_t offset, loff_t len);
#ifdef CONFIG_ADVISE_SYSCALLS
int ksys_fadvise64_64(int fd, loff_t offset, loff_t len, int advice);
#else
static inline int ksys_fadvise64_64(int fd, loff_t offset, loff_t len,
				    int advice)
{
	return -EINVAL;
}
#endif
unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff);
ssize_t ksys_readahead(int fd, loff_t offset, size_t count);
int ksys_ipc(unsigned int call, int first, unsigned long second,
	unsigned long third, void __user * ptr, long fifth);
int compat_ksys_ipc(u32 call, int first, int second,
	u32 third, u32 ptr, u32 fifth);

/*
 * The following kernel syscall equivalents are just wrappers to fs-internal
 * functions. Therefore, provide stubs to be inlined at the callsites.
 */
extern long do_unlinkat(int dfd, struct filename *name);

static inline long ksys_unlink(const char __user *pathname)
{
	return do_unlinkat(AT_FDCWD, getname(pathname));
}

extern long do_rmdir(int dfd, const char __user *pathname);

static inline long ksys_rmdir(const char __user *pathname)
{
	return do_rmdir(AT_FDCWD, pathname);
}

extern long do_mkdirat(int dfd, const char __user *pathname, umode_t mode);

static inline long ksys_mkdir(const char __user *pathname, umode_t mode)
{
	return do_mkdirat(AT_FDCWD, pathname, mode);
}

extern long do_symlinkat(const char __user *oldname, int newdfd,
			 const char __user *newname);

static inline long ksys_symlink(const char __user *oldname,
				const char __user *newname)
{
	return do_symlinkat(oldname, AT_FDCWD, newname);
}

extern long do_mknodat(int dfd, const char __user *filename, umode_t mode,
		       unsigned int dev);

static inline long ksys_mknod(const char __user *filename, umode_t mode,
			      unsigned int dev)
{
	return do_mknodat(AT_FDCWD, filename, mode, dev);
}

extern int do_linkat(int olddfd, const char __user *oldname, int newdfd,
		     const char __user *newname, int flags);

static inline long ksys_link(const char __user *oldname,
			     const char __user *newname)
{
	return do_linkat(AT_FDCWD, oldname, AT_FDCWD, newname, 0);
}

extern int do_fchmodat(int dfd, const char __user *filename, umode_t mode);

static inline int ksys_chmod(const char __user *filename, umode_t mode)
{
	return do_fchmodat(AT_FDCWD, filename, mode);
}

extern long do_faccessat(int dfd, const char __user *filename, int mode);

static inline long ksys_access(const char __user *filename, int mode)
{
	return do_faccessat(AT_FDCWD, filename, mode);
}

extern int do_fchownat(int dfd, const char __user *filename, uid_t user,
		       gid_t group, int flag);

static inline long ksys_chown(const char __user *filename, uid_t user,
			      gid_t group)
{
	return do_fchownat(AT_FDCWD, filename, user, group, 0);
}

static inline long ksys_lchown(const char __user *filename, uid_t user,
			       gid_t group)
{
	return do_fchownat(AT_FDCWD, filename, user, group,
			     AT_SYMLINK_NOFOLLOW);
}

extern long do_sys_ftruncate(unsigned int fd, loff_t length, int small);

static inline long ksys_ftruncate(unsigned int fd, unsigned long length)
{
	return do_sys_ftruncate(fd, length, 1);
}

extern int __close_fd(struct files_struct *files, unsigned int fd);

/*
 * In contrast to sys_close(), this stub does not check whether the syscall
 * should or should not be restarted, but returns the raw error codes from
 * __close_fd().
 */
static inline int ksys_close(unsigned int fd)
{
	return __close_fd(current->files, fd);
}

extern long do_sys_open(int dfd, const char __user *filename, int flags,
			umode_t mode);

static inline long ksys_open(const char __user *filename, int flags,
			     umode_t mode)
{
	if (force_o_largefile())
		flags |= O_LARGEFILE;
	return do_sys_open(AT_FDCWD, filename, flags, mode);
}

extern long do_sys_truncate(const char __user *pathname, loff_t length);

static inline long ksys_truncate(const char __user *pathname, loff_t length)
{
	return do_sys_truncate(pathname, length);
}

static inline unsigned int ksys_personality(unsigned int personality)
{
	unsigned int old = current->personality;

	if (personality != 0xffffffff)
		set_personality(personality);

	return old;
}

#endif
