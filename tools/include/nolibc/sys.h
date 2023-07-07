/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Syscall definitions for NOLIBC (those in man(2))
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_SYS_H
#define _NOLIBC_SYS_H

#include <stdarg.h>
#include "std.h"

/* system includes */
#include <asm/unistd.h>
#include <asm/signal.h>  /* for SIGCHLD */
#include <asm/ioctls.h>
#include <asm/mman.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <linux/time.h>
#include <linux/auxvec.h>
#include <linux/fcntl.h> /* for O_* and AT_* */
#include <linux/stat.h>  /* for statx() */
#include <linux/reboot.h> /* for LINUX_REBOOT_* */
#include <linux/prctl.h>

#include "arch.h"
#include "errno.h"
#include "types.h"


/* Syscall return helper for library routines, set errno as -ret when ret is in
 * range of [-MAX_ERRNO, -1]
 *
 * Note, No official reference states the errno range here aligns with musl
 * (src/internal/syscall_ret.c) and glibc (sysdeps/unix/sysv/linux/sysdep.h)
 */

static __inline__ __attribute__((unused, always_inline))
long __sysret(unsigned long ret)
{
	if (ret >= (unsigned long)-MAX_ERRNO) {
		SET_ERRNO(-(long)ret);
		return -1;
	}
	return ret;
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
	return __sysret(sys_brk(addr) ? 0 : -ENOMEM);
}

static __attribute__((unused))
void *sbrk(intptr_t inc)
{
	void *ret;

	/* first call to find current end */
	if ((ret = sys_brk(0)) && (sys_brk(ret + inc) == ret + inc))
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
#ifdef __NR_fchmodat
	return my_syscall4(__NR_fchmodat, AT_FDCWD, path, mode, 0);
#elif defined(__NR_chmod)
	return my_syscall2(__NR_chmod, path, mode);
#else
	return -ENOSYS;
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
#ifdef __NR_fchownat
	return my_syscall5(__NR_fchownat, AT_FDCWD, path, owner, group, 0);
#elif defined(__NR_chown)
	return my_syscall3(__NR_chown, path, owner, group);
#else
	return -ENOSYS;
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
#ifdef __NR_dup3
	return my_syscall3(__NR_dup3, old, new, 0);
#elif defined(__NR_dup2)
	return my_syscall2(__NR_dup2, old, new);
#else
	return -ENOSYS;
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

#ifdef __NR_dup3
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
void exit(int status)
{
	sys_exit(status);
}


/*
 * pid_t fork(void);
 */

#ifndef sys_fork
static __attribute__((unused))
pid_t sys_fork(void)
{
#ifdef __NR_clone
	/* note: some archs only have clone() and not fork(). Different archs
	 * have a different API, but most archs have the flags on first arg and
	 * will not use the rest with no other flag.
	 */
	return my_syscall5(__NR_clone, SIGCHLD, 0, 0, 0, 0);
#elif defined(__NR_fork)
	return my_syscall0(__NR_fork);
#else
	return -ENOSYS;
#endif
}
#endif

static __attribute__((unused))
pid_t fork(void)
{
	return __sysret(sys_fork());
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
#ifdef __NR_geteuid32
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
 * long getpagesize(void);
 */

static __attribute__((unused))
long getpagesize(void)
{
	return __sysret(getauxval(AT_PAGESZ) ?: -ENOENT);
}


/*
 * int gettimeofday(struct timeval *tv, struct timezone *tz);
 */

static __attribute__((unused))
int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
#ifdef __NR_gettimeofday
	return my_syscall2(__NR_gettimeofday, tv, tz);
#else
	return -ENOSYS;
#endif
}

static __attribute__((unused))
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return __sysret(sys_gettimeofday(tv, tz));
}


/*
 * uid_t getuid(void);
 */

static __attribute__((unused))
uid_t sys_getuid(void)
{
#ifdef __NR_getuid32
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
 * int ioctl(int fd, unsigned long req, void *value);
 */

static __attribute__((unused))
int sys_ioctl(int fd, unsigned long req, void *value)
{
	return my_syscall3(__NR_ioctl, fd, req, value);
}

static __attribute__((unused))
int ioctl(int fd, unsigned long req, void *value)
{
	return __sysret(sys_ioctl(fd, req, value));
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
#ifdef __NR_linkat
	return my_syscall5(__NR_linkat, AT_FDCWD, old, AT_FDCWD, new, 0);
#elif defined(__NR_link)
	return my_syscall2(__NR_link, old, new);
#else
	return -ENOSYS;
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
#ifdef __NR_lseek
	return my_syscall3(__NR_lseek, fd, offset, whence);
#else
	return -ENOSYS;
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
#ifdef __NR_mkdirat
	return my_syscall3(__NR_mkdirat, AT_FDCWD, path, mode);
#elif defined(__NR_mkdir)
	return my_syscall2(__NR_mkdir, path, mode);
#else
	return -ENOSYS;
#endif
}

static __attribute__((unused))
int mkdir(const char *path, mode_t mode)
{
	return __sysret(sys_mkdir(path, mode));
}


/*
 * int mknod(const char *path, mode_t mode, dev_t dev);
 */

static __attribute__((unused))
long sys_mknod(const char *path, mode_t mode, dev_t dev)
{
#ifdef __NR_mknodat
	return my_syscall4(__NR_mknodat, AT_FDCWD, path, mode, dev);
#elif defined(__NR_mknod)
	return my_syscall3(__NR_mknod, path, mode, dev);
#else
	return -ENOSYS;
#endif
}

static __attribute__((unused))
int mknod(const char *path, mode_t mode, dev_t dev)
{
	return __sysret(sys_mknod(path, mode, dev));
}

#ifndef sys_mmap
static __attribute__((unused))
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
	       off_t offset)
{
	int n;

#if defined(__NR_mmap2)
	n = __NR_mmap2;
	offset >>= 12;
#else
	n = __NR_mmap;
#endif

	return (void *)my_syscall6(n, addr, length, prot, flags, fd, offset);
}
#endif

/* Note that on Linux, MAP_FAILED is -1 so we can use the generic __sysret()
 * which returns -1 upon error and still satisfy user land that checks for
 * MAP_FAILED.
 */

static __attribute__((unused))
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return (void *)__sysret((unsigned long)sys_mmap(addr, length, prot, flags, fd, offset));
}

static __attribute__((unused))
int sys_munmap(void *addr, size_t length)
{
	return my_syscall2(__NR_munmap, addr, length);
}

static __attribute__((unused))
int munmap(void *addr, size_t length)
{
	return __sysret(sys_munmap(addr, length));
}

/*
 * int mount(const char *source, const char *target,
 *           const char *fstype, unsigned long flags,
 *           const void *data);
 */
static __attribute__((unused))
int sys_mount(const char *src, const char *tgt, const char *fst,
                     unsigned long flags, const void *data)
{
	return my_syscall5(__NR_mount, src, tgt, fst, flags, data);
}

static __attribute__((unused))
int mount(const char *src, const char *tgt,
          const char *fst, unsigned long flags,
          const void *data)
{
	return __sysret(sys_mount(src, tgt, fst, flags, data));
}


/*
 * int open(const char *path, int flags[, mode_t mode]);
 */

static __attribute__((unused))
int sys_open(const char *path, int flags, mode_t mode)
{
#ifdef __NR_openat
	return my_syscall4(__NR_openat, AT_FDCWD, path, flags, mode);
#elif defined(__NR_open)
	return my_syscall3(__NR_open, path, flags, mode);
#else
	return -ENOSYS;
#endif
}

static __attribute__((unused))
int open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	int ret;

	if (flags & O_CREAT) {
		va_list args;

		va_start(args, flags);
		mode = va_arg(args, int);
		va_end(args);
	}

	return __sysret(sys_open(path, flags, mode));
}


/*
 * int prctl(int option, unsigned long arg2, unsigned long arg3,
 *                       unsigned long arg4, unsigned long arg5);
 */

static __attribute__((unused))
int sys_prctl(int option, unsigned long arg2, unsigned long arg3,
		          unsigned long arg4, unsigned long arg5)
{
	return my_syscall5(__NR_prctl, option, arg2, arg3, arg4, arg5);
}

static __attribute__((unused))
int prctl(int option, unsigned long arg2, unsigned long arg3,
		      unsigned long arg4, unsigned long arg5)
{
	return __sysret(sys_prctl(option, arg2, arg3, arg4, arg5));
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
 * int poll(struct pollfd *fds, int nfds, int timeout);
 */

static __attribute__((unused))
int sys_poll(struct pollfd *fds, int nfds, int timeout)
{
#if defined(__NR_ppoll)
	struct timespec t;

	if (timeout >= 0) {
		t.tv_sec  = timeout / 1000;
		t.tv_nsec = (timeout % 1000) * 1000000;
	}
	return my_syscall5(__NR_ppoll, fds, nfds, (timeout >= 0) ? &t : NULL, NULL, 0);
#elif defined(__NR_poll)
	return my_syscall3(__NR_poll, fds, nfds, timeout);
#else
	return -ENOSYS;
#endif
}

static __attribute__((unused))
int poll(struct pollfd *fds, int nfds, int timeout)
{
	return __sysret(sys_poll(fds, nfds, timeout));
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
 * int reboot(int cmd);
 * <cmd> is among LINUX_REBOOT_CMD_*
 */

static __attribute__((unused))
ssize_t sys_reboot(int magic1, int magic2, int cmd, void *arg)
{
	return my_syscall4(__NR_reboot, magic1, magic2, cmd, arg);
}

static __attribute__((unused))
int reboot(int cmd)
{
	return __sysret(sys_reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, 0));
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
#elif defined(__ARCH_WANT_SYS_PSELECT6) && defined(__NR_pselect6)
	struct timespec t;

	if (timeout) {
		t.tv_sec  = timeout->tv_sec;
		t.tv_nsec = timeout->tv_usec * 1000;
	}
	return my_syscall6(__NR_pselect6, nfds, rfds, wfds, efds, timeout ? &t : NULL, NULL);
#elif defined(__NR__newselect) || defined(__NR_select)
#ifndef __NR__newselect
#define __NR__newselect __NR_select
#endif
	return my_syscall5(__NR__newselect, nfds, rfds, wfds, efds, timeout);
#else
	return -ENOSYS;
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

#if defined(__NR_statx)
/*
 * int statx(int fd, const char *path, int flags, unsigned int mask, struct statx *buf);
 */

static __attribute__((unused))
int sys_statx(int fd, const char *path, int flags, unsigned int mask, struct statx *buf)
{
	return my_syscall5(__NR_statx, fd, path, flags, mask, buf);
}

static __attribute__((unused))
int statx(int fd, const char *path, int flags, unsigned int mask, struct statx *buf)
{
	return __sysret(sys_statx(fd, path, flags, mask, buf));
}
#endif

/*
 * int stat(const char *path, struct stat *buf);
 * Warning: the struct stat's layout is arch-dependent.
 */

#if defined(__NR_statx) && !defined(__NR_newfstatat) && !defined(__NR_stat)
/*
 * Maybe we can just use statx() when available for all architectures?
 */
static __attribute__((unused))
int sys_stat(const char *path, struct stat *buf)
{
	struct statx statx;
	long ret;

	ret = sys_statx(AT_FDCWD, path, AT_NO_AUTOMOUNT, STATX_BASIC_STATS, &statx);
	buf->st_dev          = ((statx.stx_dev_minor & 0xff)
			       | (statx.stx_dev_major << 8)
			       | ((statx.stx_dev_minor & ~0xff) << 12));
	buf->st_ino          = statx.stx_ino;
	buf->st_mode         = statx.stx_mode;
	buf->st_nlink        = statx.stx_nlink;
	buf->st_uid          = statx.stx_uid;
	buf->st_gid          = statx.stx_gid;
	buf->st_rdev         = ((statx.stx_rdev_minor & 0xff)
			       | (statx.stx_rdev_major << 8)
			       | ((statx.stx_rdev_minor & ~0xff) << 12));
	buf->st_size         = statx.stx_size;
	buf->st_blksize      = statx.stx_blksize;
	buf->st_blocks       = statx.stx_blocks;
	buf->st_atim.tv_sec  = statx.stx_atime.tv_sec;
	buf->st_atim.tv_nsec = statx.stx_atime.tv_nsec;
	buf->st_mtim.tv_sec  = statx.stx_mtime.tv_sec;
	buf->st_mtim.tv_nsec = statx.stx_mtime.tv_nsec;
	buf->st_ctim.tv_sec  = statx.stx_ctime.tv_sec;
	buf->st_ctim.tv_nsec = statx.stx_ctime.tv_nsec;
	return ret;
}
#else
static __attribute__((unused))
int sys_stat(const char *path, struct stat *buf)
{
	struct sys_stat_struct stat;
	long ret;

#ifdef __NR_newfstatat
	/* only solution for arm64 */
	ret = my_syscall4(__NR_newfstatat, AT_FDCWD, path, &stat, 0);
#elif defined(__NR_stat)
	ret = my_syscall2(__NR_stat, path, &stat);
#else
	return -ENOSYS;
#endif
	buf->st_dev          = stat.st_dev;
	buf->st_ino          = stat.st_ino;
	buf->st_mode         = stat.st_mode;
	buf->st_nlink        = stat.st_nlink;
	buf->st_uid          = stat.st_uid;
	buf->st_gid          = stat.st_gid;
	buf->st_rdev         = stat.st_rdev;
	buf->st_size         = stat.st_size;
	buf->st_blksize      = stat.st_blksize;
	buf->st_blocks       = stat.st_blocks;
	buf->st_atim.tv_sec  = stat.st_atime;
	buf->st_atim.tv_nsec = stat.st_atime_nsec;
	buf->st_mtim.tv_sec  = stat.st_mtime;
	buf->st_mtim.tv_nsec = stat.st_mtime_nsec;
	buf->st_ctim.tv_sec  = stat.st_ctime;
	buf->st_ctim.tv_nsec = stat.st_ctime_nsec;
	return ret;
}
#endif

static __attribute__((unused))
int stat(const char *path, struct stat *buf)
{
	return __sysret(sys_stat(path, buf));
}


/*
 * int symlink(const char *old, const char *new);
 */

static __attribute__((unused))
int sys_symlink(const char *old, const char *new)
{
#ifdef __NR_symlinkat
	return my_syscall3(__NR_symlinkat, old, AT_FDCWD, new);
#elif defined(__NR_symlink)
	return my_syscall2(__NR_symlink, old, new);
#else
	return -ENOSYS;
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
#ifdef __NR_unlinkat
	return my_syscall3(__NR_unlinkat, AT_FDCWD, path, 0);
#elif defined(__NR_unlink)
	return my_syscall1(__NR_unlink, path);
#else
	return -ENOSYS;
#endif
}

static __attribute__((unused))
int unlink(const char *path)
{
	return __sysret(sys_unlink(path));
}


/*
 * pid_t wait(int *status);
 * pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);
 * pid_t waitpid(pid_t pid, int *status, int options);
 */

static __attribute__((unused))
pid_t sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
#ifdef __NR_wait4
	return my_syscall4(__NR_wait4, pid, status, options, rusage);
#else
	return -ENOSYS;
#endif
}

static __attribute__((unused))
pid_t wait(int *status)
{
	return __sysret(sys_wait4(-1, status, 0, NULL));
}

static __attribute__((unused))
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
	return __sysret(sys_wait4(pid, status, options, rusage));
}


static __attribute__((unused))
pid_t waitpid(pid_t pid, int *status, int options)
{
	return __sysret(sys_wait4(pid, status, options, NULL));
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

/* make sure to include all global symbols */
#include "nolibc.h"

#endif /* _NOLIBC_SYS_H */
