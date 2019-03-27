/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 John Birrell <jb@cimlogic.com.au>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Private definitions for libc, libc_r and libpthread.
 *
 */

#ifndef _LIBC_PRIVATE_H_
#define _LIBC_PRIVATE_H_
#include <sys/_types.h>
#include <sys/_pthreadtypes.h>

/*
 * This global flag is non-zero when a process has created one
 * or more threads. It is used to avoid calling locking functions
 * when they are not required.
 */
#ifndef __LIBC_ISTHREADED_DECLARED
#define __LIBC_ISTHREADED_DECLARED
extern int	__isthreaded;
#endif

/*
 * Elf_Auxinfo *__elf_aux_vector, the pointer to the ELF aux vector
 * provided by kernel. Either set for us by rtld, or found at runtime
 * on stack for static binaries.
 *
 * Type is void to avoid polluting whole libc with ELF types.
 */
extern void	*__elf_aux_vector;

/*
 * libc should use libc_dlopen internally, which respects a global
 * flag where loading of new shared objects can be restricted.
 */
void *libc_dlopen(const char *, int);

/*
 * For dynamic linker.
 */
void _rtld_error(const char *fmt, ...);

/*
 * File lock contention is difficult to diagnose without knowing
 * where locks were set. Allow a debug library to be built which
 * records the source file and line number of each lock call.
 */
#ifdef	_FLOCK_DEBUG
#define _FLOCKFILE(x)	_flockfile_debug(x, __FILE__, __LINE__)
#else
#define _FLOCKFILE(x)	_flockfile(x)
#endif

/*
 * Macros for locking and unlocking FILEs. These test if the
 * process is threaded to avoid locking when not required.
 */
#define	FLOCKFILE(fp)		if (__isthreaded) _FLOCKFILE(fp)
#define	FUNLOCKFILE(fp)		if (__isthreaded) _funlockfile(fp)

struct _spinlock;
extern struct _spinlock __stdio_thread_lock __hidden;
#define STDIO_THREAD_LOCK()				\
do {							\
	if (__isthreaded)				\
		_SPINLOCK(&__stdio_thread_lock);	\
} while (0)
#define STDIO_THREAD_UNLOCK()				\
do {							\
	if (__isthreaded)				\
		_SPINUNLOCK(&__stdio_thread_lock);	\
} while (0)

void		__libc_spinlock_stub(struct _spinlock *);
void		__libc_spinunlock_stub(struct _spinlock *);

/*
 * Indexes into the pthread jump table.
 *
 * Warning! If you change this type, you must also change the threads
 * libraries that reference it (libc_r, libpthread).
 */
typedef enum {
	PJT_ATFORK,
	PJT_ATTR_DESTROY,
	PJT_ATTR_GETDETACHSTATE,
	PJT_ATTR_GETGUARDSIZE,
	PJT_ATTR_GETINHERITSCHED,
	PJT_ATTR_GETSCHEDPARAM,
	PJT_ATTR_GETSCHEDPOLICY,
	PJT_ATTR_GETSCOPE,
	PJT_ATTR_GETSTACKADDR,
	PJT_ATTR_GETSTACKSIZE,
	PJT_ATTR_INIT,
	PJT_ATTR_SETDETACHSTATE,
	PJT_ATTR_SETGUARDSIZE,
	PJT_ATTR_SETINHERITSCHED,
	PJT_ATTR_SETSCHEDPARAM,
	PJT_ATTR_SETSCHEDPOLICY,
	PJT_ATTR_SETSCOPE,
	PJT_ATTR_SETSTACKADDR,
	PJT_ATTR_SETSTACKSIZE,
	PJT_CANCEL,
	PJT_CLEANUP_POP,
	PJT_CLEANUP_PUSH,
	PJT_COND_BROADCAST,
	PJT_COND_DESTROY,
	PJT_COND_INIT,
	PJT_COND_SIGNAL,
	PJT_COND_TIMEDWAIT,
	PJT_COND_WAIT,
	PJT_DETACH,
	PJT_EQUAL,
	PJT_EXIT,
	PJT_GETSPECIFIC,
	PJT_JOIN,
	PJT_KEY_CREATE,
	PJT_KEY_DELETE,
	PJT_KILL,
	PJT_MAIN_NP,
	PJT_MUTEXATTR_DESTROY,
	PJT_MUTEXATTR_INIT,
	PJT_MUTEXATTR_SETTYPE,
	PJT_MUTEX_DESTROY,
	PJT_MUTEX_INIT,
	PJT_MUTEX_LOCK,
	PJT_MUTEX_TRYLOCK,
	PJT_MUTEX_UNLOCK,
	PJT_ONCE,
	PJT_RWLOCK_DESTROY,
	PJT_RWLOCK_INIT,
	PJT_RWLOCK_RDLOCK,
	PJT_RWLOCK_TRYRDLOCK,
	PJT_RWLOCK_TRYWRLOCK,
	PJT_RWLOCK_UNLOCK,
	PJT_RWLOCK_WRLOCK,
	PJT_SELF,
	PJT_SETCANCELSTATE,
	PJT_SETCANCELTYPE,
	PJT_SETSPECIFIC,
	PJT_SIGMASK,
	PJT_TESTCANCEL,
	PJT_CLEANUP_POP_IMP,
	PJT_CLEANUP_PUSH_IMP,
	PJT_CANCEL_ENTER,
	PJT_CANCEL_LEAVE,
	PJT_MUTEX_CONSISTENT,
	PJT_MUTEXATTR_GETROBUST,
	PJT_MUTEXATTR_SETROBUST,
	PJT_MAX
} pjt_index_t;

typedef int (*pthread_func_t)(void);
typedef pthread_func_t pthread_func_entry_t[2];

extern pthread_func_entry_t __thr_jtable[];

void	__set_error_selector(int *(*arg)(void));
int	_pthread_mutex_init_calloc_cb_stub(pthread_mutex_t *mutex,
	    void *(calloc_cb)(__size_t, __size_t));

typedef int (*interpos_func_t)(void);
interpos_func_t *__libc_interposing_slot(int interposno);
extern interpos_func_t __libc_interposing[] __hidden;

enum {
	INTERPOS_accept,
	INTERPOS_accept4,
	INTERPOS_aio_suspend,
	INTERPOS_close,
	INTERPOS_connect,
	INTERPOS_fcntl,
	INTERPOS_fsync,
	INTERPOS_fork,
	INTERPOS_msync,
	INTERPOS_nanosleep,
	INTERPOS_openat,
	INTERPOS_poll,
	INTERPOS_pselect,
	INTERPOS_recvfrom,
	INTERPOS_recvmsg,
	INTERPOS_select,
	INTERPOS_sendmsg,
	INTERPOS_sendto,
	INTERPOS_setcontext,
	INTERPOS_sigaction,
	INTERPOS_sigprocmask,
	INTERPOS_sigsuspend,
	INTERPOS_sigwait,
	INTERPOS_sigtimedwait,
	INTERPOS_sigwaitinfo,
	INTERPOS_swapcontext,
	INTERPOS_system,
	INTERPOS_tcdrain,
	INTERPOS_read,
	INTERPOS_readv,
	INTERPOS_wait4,
	INTERPOS_write,
	INTERPOS_writev,
	INTERPOS__pthread_mutex_init_calloc_cb,
	INTERPOS_spinlock,
	INTERPOS_spinunlock,
	INTERPOS_kevent,
	INTERPOS_wait6,
	INTERPOS_ppoll,
	INTERPOS_map_stacks_exec,
	INTERPOS_fdatasync,
	INTERPOS_clock_nanosleep,
	INTERPOS_MAX
};

/*
 * yplib internal interfaces
 */
#ifdef YP
int _yp_check(char **);
#endif

/*
 * Initialise TLS for static programs
 */
void _init_tls(void);

/*
 * Provides pthread_once()-like functionality for both single-threaded
 * and multi-threaded applications.
 */
int _once(pthread_once_t *, void (*)(void));

/*
 * Set the TLS thread pointer
 */
void _set_tp(void *tp);

/*
 * This is a pointer in the C run-time startup code. It is used
 * by getprogname() and setprogname().
 */
extern const char *__progname;

/*
 * This function is used by the threading libraries to notify malloc that a
 * thread is exiting.
 */
void _malloc_thread_cleanup(void);

/*
 * This function is used by the threading libraries to notify libc that a
 * thread is exiting, so its thread-local dtors should be called.
 */
void __cxa_thread_call_dtors(void);
int __cxa_thread_atexit_hidden(void (*dtor_func)(void *), void *obj,
    void *dso_symbol) __hidden;

/*
 * These functions are used by the threading libraries in order to protect
 * malloc across fork().
 */
void _malloc_prefork(void);
void _malloc_postfork(void);

void _malloc_first_thread(void);

/*
 * Function to clean up streams, called from abort() and exit().
 */
extern void (*__cleanup)(void) __hidden;

/*
 * Get kern.osreldate to detect ABI revisions.  Explicitly
 * ignores value of $OSVERSION and caches result.
 */
int __getosreldate(void);
#include <sys/_types.h>
#include <sys/_sigset.h>

struct aiocb;
struct fd_set;
struct iovec;
struct kevent;
struct msghdr;
struct pollfd;
struct rusage;
struct sigaction;
struct sockaddr;
struct stat;
struct statfs;
struct timespec;
struct timeval;
struct timezone;
struct __siginfo;
struct __ucontext;
struct __wrusage;
enum idtype;
int		__sys_aio_suspend(const struct aiocb * const[], int,
		    const struct timespec *);
int		__sys_accept(int, struct sockaddr *, __socklen_t *);
int		__sys_accept4(int, struct sockaddr *, __socklen_t *, int);
int		__sys_clock_gettime(__clockid_t, struct timespec *ts);
int		__sys_clock_nanosleep(__clockid_t, int,
		    const struct timespec *, struct timespec *);
int		__sys_close(int);
int		__sys_connect(int, const struct sockaddr *, __socklen_t);
int		__sys_fcntl(int, int, ...);
int		__sys_fdatasync(int);
int		__sys_fstat(int fd, struct stat *);
int		__sys_fstatfs(int fd, struct statfs *);
int		__sys_fstatat(int, const char *, struct stat *, int);
int		__sys_fsync(int);
__pid_t		__sys_fork(void);
int		__sys_ftruncate(int, __off_t);
__ssize_t	__sys_getdirentries(int, char *, __size_t, __off_t *);
int		__sys_getfsstat(struct statfs *, long, int);
int		__sys_gettimeofday(struct timeval *, struct timezone *);
int		__sys_kevent(int, const struct kevent *, int, struct kevent *,
		    int, const struct timespec *);
__off_t		__sys_lseek(int, __off_t, int);
void	       *__sys_mmap(void *, __size_t, int, int, int, __off_t);
int		__sys_msync(void *, __size_t, int);
int		__sys_nanosleep(const struct timespec *, struct timespec *);
int		__sys_open(const char *, int, ...);
int		__sys_openat(int, const char *, int, ...);
int		__sys_pselect(int, struct fd_set *, struct fd_set *,
		    struct fd_set *, const struct timespec *,
		    const __sigset_t *);
int		__sys_ptrace(int, __pid_t, char *, int);
int		__sys_poll(struct pollfd *, unsigned, int);
int		__sys_ppoll(struct pollfd *, unsigned, const struct timespec *,
		    const __sigset_t *);
__ssize_t	__sys_pread(int, void *, __size_t, __off_t);
__ssize_t	__sys_pwrite(int, const void *, __size_t, __off_t);
__ssize_t	__sys_read(int, void *, __size_t);
__ssize_t	__sys_readv(int, const struct iovec *, int);
__ssize_t	__sys_recv(int, void *, __size_t, int);
__ssize_t	__sys_recvfrom(int, void *, __size_t, int, struct sockaddr *,
		    __socklen_t *);
__ssize_t	__sys_recvmsg(int, struct msghdr *, int);
int		__sys_select(int, struct fd_set *, struct fd_set *,
		    struct fd_set *, struct timeval *);
__ssize_t	__sys_sendmsg(int, const struct msghdr *, int);
__ssize_t	__sys_sendto(int, const void *, __size_t, int,
		    const struct sockaddr *, __socklen_t);
int		__sys_setcontext(const struct __ucontext *);
int		__sys_sigaction(int, const struct sigaction *,
		    struct sigaction *);
int		__sys_sigprocmask(int, const __sigset_t *, __sigset_t *);
int		__sys_sigsuspend(const __sigset_t *);
int		__sys_sigtimedwait(const __sigset_t *, struct __siginfo *,
		    const struct timespec *);
int		__sys_sigwait(const __sigset_t *, int *);
int		__sys_sigwaitinfo(const __sigset_t *, struct __siginfo *);
int		__sys_statfs(const char *, struct statfs *);
int		__sys_swapcontext(struct __ucontext *,
		    const struct __ucontext *);
int		__sys_thr_kill(long, int);
int		__sys_thr_self(long *);
int		__sys_truncate(const char *, __off_t);
__pid_t		__sys_wait4(__pid_t, int *, int, struct rusage *);
__pid_t		__sys_wait6(enum idtype, __id_t, int *, int,
		    struct __wrusage *, struct __siginfo *);
__ssize_t	__sys_write(int, const void *, __size_t);
__ssize_t	__sys_writev(int, const struct iovec *, int);

int		__libc_sigaction(int, const struct sigaction *,
		    struct sigaction *) __hidden;
int		__libc_sigprocmask(int, const __sigset_t *, __sigset_t *)
		    __hidden;
int		__libc_sigsuspend(const __sigset_t *) __hidden;
int		__libc_sigwait(const __sigset_t * __restrict,
		    int * restrict sig);
int		__libc_system(const char *);
int		__libc_tcdrain(int);
int		__fcntl_compat(int fd, int cmd, ...);

int		__sys_futimens(int fd, const struct timespec *times) __hidden;
int		__sys_utimensat(int fd, const char *path,
		    const struct timespec *times, int flag) __hidden;

/* execve() with PATH processing to implement posix_spawnp() */
int _execvpe(const char *, char * const *, char * const *);

int _elf_aux_info(int aux, void *buf, int buflen);
struct dl_phdr_info;
int __elf_phdr_match_addr(struct dl_phdr_info *, void *);
void __init_elf_aux_vector(void);
void __libc_map_stacks_exec(void);

void	_pthread_cancel_enter(int);
void	_pthread_cancel_leave(int);

struct _pthread_cleanup_info;
void	___pthread_cleanup_push_imp(void (*)(void *), void *,
	    struct _pthread_cleanup_info *);
void	___pthread_cleanup_pop_imp(int);

void __throw_constraint_handler_s(const char * restrict msg, int error);

#endif /* _LIBC_PRIVATE_H_ */
