/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Syscall definitions for NOLIBC (those in man(2))
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_SYS_H
#define _NOLIBC_SYS_H

#include "std.h"

/* system includes */
#include <linux/unistd.h>
#include <linux/signal.h>  /* for SIGCHLD */
#include <linux/termios.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <linux/time.h>
#include <linux/auxvec.h>
#include <linux/fcntl.h> /* for O_* and AT_* */
#include <linux/sched.h> /* for clone_args */
#include <linux/stat.h>  /* for statx() */

#include "errno.h"
#include "stdarg.h"
#include "types.h"


/* Syscall return helper: takes the syscall value in argument and checks for an
 * error in it. This may only be used with signed returns (int or long), but
 * not with pointers. An error is any value < 0. When an error is encountered,
 * -ret is set into errno and -1 is returned. Otherwise the returned value is
 * passed as-is with its type preserved.
 */

#define __sysret(arg)							\
({									\
	__typeof__(arg) __sysret_arg = (arg);				\
	(__sysret_arg < 0)                              /* error ? */	\
		? (({ SET_ERRNO(-__sysret_arg); }), -1) /* ret -1 with errno = -arg */ \
		: __sysret_arg;                         /* return original value */ \
})

/* Syscall ENOSYS helper: Avoids unused-parameter warnings and provides a
 * debugging hook.
 */

static __inline__ int __nolibc_enosys(const char *syscall, ...)
{
	(void)syscall;
	return -ENOSYS;
}


/* Functions in this file only describe syscalls. They're declared static so
 * that the compiler usually decides to inline them while still being allowed
 * to pass a pointer to one of their instances. Each syscall exists in two
 * versions:
 *   - the "internal" ones, which matches the raw syscall interface at the
 *     kernel level, which may sometimes slightly differ from the documented
 *     libc-level ones. For example most of them return either a valid value
 *     or -errno. All of these are prefixed with "sys_". They may be called
 *     by non-portable applications if desired.
 *
 *   - the "exported" ones, whose interface must closely match the one
 *     documented in man(2), that applications are supposed to expect. These
 *     ones rely on the internal ones, and set errno.
 *
 * Each syscall will be defined with the two functions, sorted in alphabetical
 * order applied to the exported names.
 *
 * In case of doubt about the relevance of a function here, only those which
 * set errno should be defined here. Wrappers like those appearing in man(3)
 * should not be placed here.
 */


/*
 * int brk(void *addr);
 * void *sbrk(intptr_t inc)
 */

static __attribute__((unused))
void *sys_brk(void *addr)
{
	return (void *)my_syscall1(__NR_brk, addr);
}

static __attribute__((unused))
int brk(void *addr)
{
	void *ret = sys_brk(addr);

	if (!ret) {
		SET_ERRNO(ENOMEM);
		return -1;
	}
	return 0;
}

static __attribute__((unused))
void *sbrk(intptr_t inc)
{
	/* first call to find current end */
	void *ret = sys_brk(0);

	if (ret && sys_brk(ret + inc) == ret + inc)
		return ret + inc;

	SET_ERRNO(ENOMEM);
	return (void *)-1;
}


/*
 * int chdir(const char *path);
 */

static __attribute__((unused))
int sys_chdir(const char *path)
{
	return my_syscall1(__NR_chdir, path);
}

static __attribute__((unused))
int chdir(const char *path)
{
	return __sysret(sys_chdir(path));
}


/*
 * int chmod(const char *path, mode_t mode);
 */

static __attribute__((unused))
int sys_chmod(const char *path, mode_t mode)
{
#if defined(__NR_fchmodat)
	return my_syscall4(__NR_fchmodat, AT_FDCWD, path, mode, 0);
#else
	return my_syscall2(__NR_chmod, path, mode);
#endif
}

static __attribute__((unused))
int chmod(const char *path, mode_t mode)
{
	return __sysret(sys_chmod(path, mode));
}


/*
 * int chown(const char *path, uid_t owner, gid_t group);
 */

static __attribute__((unused))
int sys_chown(const char *path, uid_t owner, gid_t group)
{
#if defined(__NR_fchownat)
	return my_syscall5(__NR_fchownat, AT_FDCWD, path, owner, group, 0);
#else
	return my_syscall3(__NR_chown, path, owner, group);
#endif
}

static __attribute__((unused))
int chown(const char *path, uid_t owner, gid_t group)
{
	return __sysret(sys_chown(path, owner, group));
}


/*
 * int chroot(const char *path);
 */

static __attribute__((unused))
int sys_chroot(const char *path)
{
	return my_syscall1(__NR_chroot, path);
}

static __attribute__((unused))
int chroot(const char *path)
{
	return __sysret(sys_chroot(path));
}


/*
 * int close(int fd);
 */

static __attribute__((unused))
int sys_close(int fd)
{
	return my_syscall1(__NR_close, fd);
}

static __attribute__((unused))
int close(int fd)
{
	return __sysret(sys_close(fd));
}


/*
 * int dup(int fd);
 */

static __attribute__((unused))
int sys_dup(int fd)
{
	return my_syscall1(__NR_dup, fd);
}

static __attribute__((unused))
int dup(int fd)
{
	return __sysret(sys_dup(fd));
}


/*
 * int dup2(int old, int new);
 */

static __attribute__((unused))
int sys_dup2(int old, int new)
{
#if defined(__NR_dup3)
	int ret, nr_fcntl;

#ifdef __NR_fcntl64
	nr_fcntl = __NR_fcntl64;
#else
	nr_fcntl = __NR_fcntl;
#endif

	if (old == new) {
		ret = my_syscall2(nr_fcntl, old, F_GETFD);
		return ret < 0 ? ret : old;
	}

	return my_syscall3(__NR_dup3, old, new, 0);
#else
	return my_syscall2(__NR_dup2, old, new);
#endif
}

static __attribute__((unused))
int dup2(int old, int new)
{
	return __sysret(sys_dup2(old, new));
}


/*
 * int dup3(int old, int new, int flags);
 */

#if defined(__NR_dup3)
static __attribute__((unused))
int sys_dup3(int old, int new, int flags)
{
	return my_syscall3(__NR_dup3, old, new, flags);
}

static __attribute__((unused))
int dup3(int old, int new, int flags)
{
	return __sysret(sys_dup3(old, new, flags));
}
#endif


/*
 * int execve(const char *filename, char *const argv[], char *const envp[]);
 */

static __attribute__((unused))
int sys_execve(const char *filename, char *const argv[], char *const envp[])
{
	return my_syscall3(__NR_execve, filename, argv, envp);
}

static __attribute__((unused))
int execve(const char *filename, char *const argv[], char *const envp[])
{
	return __sysret(sys_execve(filename, argv, envp));
}


/*
 * void exit(int status);
 */

static __attribute__((noreturn,unused))
void sys_exit(int status)
{
	my_syscall1(__NR_exit, status & 255);
	while(1); /* shut the "noreturn" warnings. */
}

static __attribute__((noreturn,unused))
void _exit(int status)
{
	sys_exit(status);
}

static __attribute__((noreturn,unused))
void exit(int status)
{
	_exit(status);
}


/*
 * pid_t fork(void);
 */

#ifndef sys_fork
static __attribute__((unused))
pid_t sys_fork(void)
{
#if defined(__NR_clone)
	/* note: some archs only have clone() and not fork(). Different archs
	 * have a different API, but most archs have the flags on first arg and
	 * will not use the rest with no other flag.
	 */
	return my_syscall5(__NR_clone, SIGCHLD, 0, 0, 0, 0);
#else
	return my_syscall0(__NR_fork);
#endif
}
#endif

static __attribute__((unused))
pid_t fork(void)
{
	return __sysret(sys_fork());
}

#ifndef sys_vfork
static __attribute__((unused))
pid_t sys_vfork(void)
{
#if defined(__NR_vfork)
	return my_syscall0(__NR_vfork);
#else
	/*
	 * clone() could be used but has different argument orders per
	 * architecture.
	 */
	struct clone_args args = {
		.flags		= CLONE_VM | CLONE_VFORK,
		.exit_signal	= SIGCHLD,
	};

	return my_syscall2(__NR_clone3, &args, sizeof(args));
#endif
}
#endif

static __attribute__((unused))
pid_t vfork(void)
{
	return __sysret(sys_vfork());
}

/*
 * int fsync(int fd);
 */

static __attribute__((unused))
int sys_fsync(int fd)
{
	return my_syscall1(__NR_fsync, fd);
}

static __attribute__((unused))
int fsync(int fd)
{
	return __sysret(sys_fsync(fd));
}


/*
 * int getdents64(int fd, struct linux_dirent64 *dirp, int count);
 */

static __attribute__((unused))
int sys_getdents64(int fd, struct linux_dirent64 *dirp, int count)
{
	return my_syscall3(__NR_getdents64, fd, dirp, count);
}

static __attribute__((unused))
int getdents64(int fd, struct linux_dirent64 *dirp, int count)
{
	return __sysret(sys_getdents64(fd, dirp, count));
}


/*
 * uid_t geteuid(void);
 */

static __attribute__((unused))
uid_t sys_geteuid(void)
{
#if defined(__NR_geteuid32)
	return my_syscall0(__NR_geteuid32);
#else
	return my_syscall0(__NR_geteuid);
#endif
}

static __attribute__((unused))
uid_t geteuid(void)
{
	return sys_geteuid();
}


/*
 * pid_t getpgid(pid_t pid);
 */

static __attribute__((unused))
pid_t sys_getpgid(pid_t pid)
{
	return my_syscall1(__NR_getpgid, pid);
}

static __attribute__((unused))
pid_t getpgid(pid_t pid)
{
	return __sysret(sys_getpgid(pid));
}


/*
 * pid_t getpgrp(void);
 */

static __attribute__((unused))
pid_t sys_getpgrp(void)
{
	return sys_getpgid(0);
}

static __attribute__((unused))
pid_t getpgrp(void)
{
	return sys_getpgrp();
}


/*
 * pid_t getpid(void);
 */

static __attribute__((unused))
pid_t sys_getpid(void)
{
	return my_syscall0(__NR_getpid);
}

static __attribute__((unused))
pid_t getpid(void)
{
	return sys_getpid();
}


/*
 * pid_t getppid(void);
 */

static __attribute__((unused))
pid_t sys_getppid(void)
{
	return my_syscall0(__NR_getppid);
}

static __attribute__((unused))
pid_t getppid(void)
{
	return sys_getppid();
}


/*
 * pid_t gettid(void);
 */

static __attribute__((unused))
pid_t sys_gettid(void)
{
	return my_syscall0(__NR_gettid);
}

static __attribute__((unused))
pid_t gettid(void)
{
	return sys_gettid();
}

static unsigned long getauxval(unsigned long key);

/*
 * int getpagesize(void);
 */

static __attribute__((unused))
int getpagesize(void)
{
	return __sysret((int)getauxval(AT_PAGESZ) ?: -ENOENT);
}


/*
 * uid_t getuid(void);
 */

static __attribute__((unused))
uid_t sys_getuid(void)
{
#if defined(__NR_getuid32)
	return my_syscall0(__NR_getuid32);
#else
	return my_syscall0(__NR_getuid);
#endif
}

static __attribute__((unused))
uid_t getuid(void)
{
	return sys_getuid();
}


/*
 * int kill(pid_t pid, int signal);
 */

static __attribute__((unused))
int sys_kill(pid_t pid, int signal)
{
	return my_syscall2(__NR_kill, pid, signal);
}

static __attribute__((unused))
int kill(pid_t pid, int signal)
{
	return __sysret(sys_kill(pid, signal));
}


/*
 * int link(const char *old, const char *new);
 */

static __attribute__((unused))
int sys_link(const char *old, const char *new)
{
#if defined(__NR_linkat)
	return my_syscall5(__NR_linkat, AT_FDCWD, old, AT_FDCWD, new, 0);
#else
	return my_syscall2(__NR_link, old, new);
#endif
}

static __attribute__((unused))
int link(const char *old, const char *new)
{
	return __sysret(sys_link(old, new));
}


/*
 * off_t lseek(int fd, off_t offset, int whence);
 */

static __attribute__((unused))
off_t sys_lseek(int fd, off_t offset, int whence)
{
#if defined(__NR_lseek)
	return my_syscall3(__NR_lseek, fd, offset, whence);
#else
	__kernel_loff_t loff = 0;
	off_t result;
	int ret;

	/* Only exists on 32bit where nolibc off_t is also 32bit */
	ret = my_syscall5(__NR_llseek, fd, 0, offset, &loff, whence);
	if (ret < 0)
		result = ret;
	else if (loff != (off_t)loff)
		result = -EOVERFLOW;
	else
		result = loff;

	return result;
#endif
}

static __attribute__((unused))
off_t lseek(int fd, off_t offset, int whence)
{
	return __sysret(sys_lseek(fd, offset, whence));
}


/*
 * int mkdir(const char *path, mode_t mode);
 */

static __attribute__((unused))
int sys_mkdir(const char *path, mode_t mode)
{
#if defined(__NR_mkdirat)
	return my_syscall3(__NR_mkdirat, AT_FDCWD, path, mode);
#else
	return my_syscall2(__NR_mkdir, path, mode);
#endif
}

static __attribute__((unused))
int mkdir(const char *path, mode_t mode)
{
	return __sysret(sys_mkdir(path, mode));
}

/*
 * int rmdir(const char *path);
 */

static __attribute__((unused))
int sys_rmdir(const char *path)
{
#if defined(__NR_rmdir)
	return my_syscall1(__NR_rmdir, path);
#else
	return my_syscall3(__NR_unlinkat, AT_FDCWD, path, AT_REMOVEDIR);
#endif
}

static __attribute__((unused))
int rmdir(const char *path)
{
	return __sysret(sys_rmdir(path));
}


/*
 * int mknod(const char *path, mode_t mode, dev_t dev);
 */

static __attribute__((unused))
long sys_mknod(const char *path, mode_t mode, dev_t dev)
{
#if defined(__NR_mknodat)
	return my_syscall4(__NR_mknodat, AT_FDCWD, path, mode, dev);
#else
	return my_syscall3(__NR_mknod, path, mode, dev);
#endif
}

static __attribute__((unused))
int mknod(const char *path, mode_t mode, dev_t dev)
{
	return __sysret(sys_mknod(path, mode, dev));
}


/*
 * int pipe2(int pipefd[2], int flags);
 * int pipe(int pipefd[2]);
 */

static __attribute__((unused))
int sys_pipe2(int pipefd[2], int flags)
{
	return my_syscall2(__NR_pipe2, pipefd, flags);
}

static __attribute__((unused))
int pipe2(int pipefd[2], int flags)
{
	return __sysret(sys_pipe2(pipefd, flags));
}

static __attribute__((unused))
int pipe(int pipefd[2])
{
	return pipe2(pipefd, 0);
}


/*
 * int pivot_root(const char *new, const char *old);
 */

static __attribute__((unused))
int sys_pivot_root(const char *new, const char *old)
{
	return my_syscall2(__NR_pivot_root, new, old);
}

static __attribute__((unused))
int pivot_root(const char *new, const char *old)
{
	return __sysret(sys_pivot_root(new, old));
}


/*
 * ssize_t read(int fd, void *buf, size_t count);
 */

static __attribute__((unused))
ssize_t sys_read(int fd, void *buf, size_t count)
{
	return my_syscall3(__NR_read, fd, buf, count);
}

static __attribute__((unused))
ssize_t read(int fd, void *buf, size_t count)
{
	return __sysret(sys_read(fd, buf, count));
}


/*
 * int sched_yield(void);
 */

static __attribute__((unused))
int sys_sched_yield(void)
{
	return my_syscall0(__NR_sched_yield);
}

static __attribute__((unused))
int sched_yield(void)
{
	return __sysret(sys_sched_yield());
}


/*
 * int select(int nfds, fd_set *read_fds, fd_set *write_fds,
 *            fd_set *except_fds, struct timeval *timeout);
 */

static __attribute__((unused))
int sys_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *timeout)
{
#if defined(__ARCH_WANT_SYS_OLD_SELECT) && !defined(__NR__newselect)
	struct sel_arg_struct {
		unsigned long n;
		fd_set *r, *w, *e;
		struct timeval *t;
	} arg = { .n = nfds, .r = rfds, .w = wfds, .e = efds, .t = timeout };
	return my_syscall1(__NR_select, &arg);
#elif defined(__NR__newselect)
	return my_syscall5(__NR__newselect, nfds, rfds, wfds, efds, timeout);
#elif defined(__NR_select)
	return my_syscall5(__NR_select, nfds, rfds, wfds, efds, timeout);
#elif defined(__NR_pselect6)
	struct timespec t;

	if (timeout) {
		t.tv_sec  = timeout->tv_sec;
		t.tv_nsec = timeout->tv_usec * 1000;
	}
	return my_syscall6(__NR_pselect6, nfds, rfds, wfds, efds, timeout ? &t : NULL, NULL);
#else
	struct __kernel_timespec t;

	if (timeout) {
		t.tv_sec  = timeout->tv_sec;
		t.tv_nsec = timeout->tv_usec * 1000;
	}
	return my_syscall6(__NR_pselect6_time64, nfds, rfds, wfds, efds, timeout ? &t : NULL, NULL);
#endif
}

static __attribute__((unused))
int select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *timeout)
{
	return __sysret(sys_select(nfds, rfds, wfds, efds, timeout));
}


/*
 * int setpgid(pid_t pid, pid_t pgid);
 */

static __attribute__((unused))
int sys_setpgid(pid_t pid, pid_t pgid)
{
	return my_syscall2(__NR_setpgid, pid, pgid);
}

static __attribute__((unused))
int setpgid(pid_t pid, pid_t pgid)
{
	return __sysret(sys_setpgid(pid, pgid));
}

/*
 * pid_t setpgrp(void)
 */

static __attribute__((unused))
pid_t setpgrp(void)
{
	return setpgid(0, 0);
}


/*
 * pid_t setsid(void);
 */

static __attribute__((unused))
pid_t sys_setsid(void)
{
	return my_syscall0(__NR_setsid);
}

static __attribute__((unused))
pid_t setsid(void)
{
	return __sysret(sys_setsid());
}


/*
 * int symlink(const char *old, const char *new);
 */

static __attribute__((unused))
int sys_symlink(const char *old, const char *new)
{
#if defined(__NR_symlinkat)
	return my_syscall3(__NR_symlinkat, old, AT_FDCWD, new);
#else
	return my_syscall2(__NR_symlink, old, new);
#endif
}

static __attribute__((unused))
int symlink(const char *old, const char *new)
{
	return __sysret(sys_symlink(old, new));
}


/*
 * mode_t umask(mode_t mode);
 */

static __attribute__((unused))
mode_t sys_umask(mode_t mode)
{
	return my_syscall1(__NR_umask, mode);
}

static __attribute__((unused))
mode_t umask(mode_t mode)
{
	return sys_umask(mode);
}


/*
 * int umount2(const char *path, int flags);
 */

static __attribute__((unused))
int sys_umount2(const char *path, int flags)
{
	return my_syscall2(__NR_umount2, path, flags);
}

static __attribute__((unused))
int umount2(const char *path, int flags)
{
	return __sysret(sys_umount2(path, flags));
}


/*
 * int unlink(const char *path);
 */

static __attribute__((unused))
int sys_unlink(const char *path)
{
#if defined(__NR_unlinkat)
	return my_syscall3(__NR_unlinkat, AT_FDCWD, path, 0);
#else
	return my_syscall1(__NR_unlink, path);
#endif
}

static __attribute__((unused))
int unlink(const char *path)
{
	return __sysret(sys_unlink(path));
}


/*
 * ssize_t write(int fd, const void *buf, size_t count);
 */

static __attribute__((unused))
ssize_t sys_write(int fd, const void *buf, size_t count)
{
	return my_syscall3(__NR_write, fd, buf, count);
}

static __attribute__((unused))
ssize_t write(int fd, const void *buf, size_t count)
{
	return __sysret(sys_write(fd, buf, count));
}


/*
 * int memfd_create(const char *name, unsigned int flags);
 */

static __attribute__((unused))
int sys_memfd_create(const char *name, unsigned int flags)
{
	return my_syscall2(__NR_memfd_create, name, flags);
}

static __attribute__((unused))
int memfd_create(const char *name, unsigned int flags)
{
	return __sysret(sys_memfd_create(name, flags));
}

#endif /* _NOLIBC_SYS_H */
