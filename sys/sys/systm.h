/*	$OpenBSD: systm.h,v 1.177 2025/07/28 05:08:35 dlg Exp $	*/
/*	$NetBSD: systm.h,v 1.50 1996/06/09 04:55:09 briggs Exp $	*/

/*-
 * Copyright (c) 1982, 1988, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 *	@(#)systm.h	8.4 (Berkeley) 2/23/94
 */

#ifndef __SYSTM_H__
#define __SYSTM_H__

#include <sys/queue.h>
#include <sys/stdarg.h>

/*
 * The `securelevel' variable controls the security level of the system.
 * It can only be decreased by process 1 (/sbin/init).
 *
 * Security levels are as follows:
 *   -1	permanently insecure mode - always run system in level 0 mode.
 *    0	insecure mode - immutable and append-only flags may be turned off.
 *	All devices may be read or written subject to permission modes.
 *    1	secure mode - immutable and append-only flags may not be changed;
 *	raw disks of mounted filesystems, /dev/mem, and /dev/kmem are
 *	read-only.
 *    2	highly secure mode - same as (1) plus raw disks are always
 *	read-only whether mounted or not. This level precludes tampering
 *	with filesystems by unmounting them, but also inhibits running
 *	newfs while the system is secured.
 *
 * In normal operation, the system runs in level 0 mode while single user
 * and in level 1 mode while multiuser. If level 2 mode is desired while
 * running multiuser, it can be set in the multiuser startup script
 * (/etc/rc.local) using sysctl(1). If it is desired to run the system
 * in level 0 mode while multiuser, initialize the variable securelevel
 * in /sys/kern/kern_sysctl.c to -1. Note that it is NOT initialized to
 * zero as that would allow the vmunix binary to be patched to -1.
 * Without initialization, securelevel loads in the BSS area which only
 * comes into existence when the kernel is loaded and hence cannot be
 * patched by a stalking hacker.
 */
extern int securelevel;		/* system security level */
extern const char *panicstr;	/* panic message */
extern const char version[];		/* system version */
extern const char copyright[];	/* system copyright */
extern const char ostype[];
extern const char osversion[];
extern const char osrelease[];
extern int cold;		/* cold start flag initialized in locore */
extern int db_active;		/* running currently inside ddb(4) */

extern char *hw_vendor;		/* sysctl hw.vendor */
extern char *hw_prod;		/* sysctl hw.product */
extern char *hw_uuid;		/* sysctl hw.uuid */
extern char *hw_serial;		/* sysctl hw.serialno */
extern char *hw_ver;		/* sysctl hw.version */

extern int ncpus;		/* number of CPUs used */
extern int ncpusfound;		/* number of CPUs found */
extern int nblkdev;		/* number of entries in bdevsw */
extern int nchrdev;		/* number of entries in cdevsw */

extern int physmem;		/* physical memory */

extern dev_t dumpdev;		/* dump device */
extern long dumplo;		/* offset into dumpdev */

extern dev_t rootdev;		/* root device */
extern u_char bootduid[8];	/* boot device disklabel uid */
extern u_char rootduid[8];	/* root device disklabel uid */
extern struct vnode *rootvp;	/* vnode equivalent to above */
extern struct device *rootdv;	/* device equivalent to above */

extern dev_t swapdev;		/* swapping device */
extern struct vnode *swapdev_vp;/* vnode equivalent to above */

extern int nowake;		/* dead wakeup(9) channel */

#ifdef MP_LOCKDEBUG
extern long __mp_lock_spinout;
#endif

struct proc;
struct process;
#define curproc curcpu()->ci_curproc

typedef int	sy_call_t(struct proc *, void *, register_t *);

extern const struct sysent {	/* system call table */
	short	sy_narg;	/* number of args */
	short	sy_argsize;	/* total size of arguments */
	int	sy_flags;
	sy_call_t *sy_call;	/* implementing function */
} sysent[];

#define SY_NOLOCK		0x01

#if	_BYTE_ORDER == _BIG_ENDIAN
#define SCARG(p, k)	((p)->k.be.datum)	/* get arg from args pointer */
#elif	_BYTE_ORDER == _LITTLE_ENDIAN
#define SCARG(p, k)	((p)->k.le.datum)	/* get arg from args pointer */
#else
#error	"what byte order is this machine?"
#endif

#if defined(_KERNEL) && defined(SYSCALL_DEBUG)
void scdebug_call(struct proc *p, register_t code, const register_t retval[]);
void scdebug_ret(struct proc *p, register_t code, int error,
    const register_t retval[]);
#endif /* _KERNEL && SYSCALL_DEBUG */

extern int boothowto;		/* reboot flags, from console subsystem */

extern void (*v_putc)(int); /* Virtual console putc routine */

/*
 * General function declarations.
 */
int	nullop(void *);
int	enodev(void);
int	enosys(void);
int	enoioctl(void);
int	enxio(void);
int	eopnotsupp(void *);

void	*hashinit(int, int, int, u_long *);
void	 hashfree(void *, int, int);
int	sys_nosys(struct proc *, void *, register_t *);

void	panic(const char *, ...)
    __attribute__((__noreturn__,__format__(__kprintf__,1,2)));
void	__assert(const char *, const char *, int, const char *)
    __attribute__((__noreturn__));
int	printf(const char *, ...)
    __attribute__((__format__(__kprintf__,1,2)));
void	uprintf(const char *, ...)
    __attribute__((__format__(__kprintf__,1,2)));
int	vprintf(const char *, va_list)
    __attribute__((__format__(__kprintf__,1,0)));
int	vsnprintf(char *, size_t, const char *, va_list)
    __attribute__((__format__(__kprintf__,3,0)));
int	snprintf(char *buf, size_t, const char *, ...)
    __attribute__((__format__(__kprintf__,3,4)));
struct tty;
void	ttyprintf(struct tty *, const char *, ...)
    __attribute__((__format__(__kprintf__,2,3)));

void	splassert_fail(int, int, const char *);
extern	int splassert_ctl;

void	assertwaitok(void);

void	tablefull(const char *);

int	kcopy(const void *, void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)))
		__attribute__ ((__bounded__(__buffer__,2,3)));

void	bcopy(const void *, void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)))
		__attribute__ ((__bounded__(__buffer__,2,3)));
void	bzero(void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,2)));
void	explicit_bzero(void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,2)));
int	bcmp(const void *, const void *, size_t);
void	*memcpy(void *, const void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)))
		__attribute__ ((__bounded__(__buffer__,2,3)));
void	*memmove(void *, const void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)))
		__attribute__ ((__bounded__(__buffer__,2,3)));
void	*memset(void *, int, size_t)
		__attribute__ ((__bounded__(__buffer__,1,3)));

int	copyinstr(const void *, void *, size_t, size_t *)
		__attribute__ ((__bounded__(__string__,2,3)));
int	_copyinstr(const void *, void *, size_t, size_t *)
		__attribute__ ((__bounded__(__string__,2,3)));
int	copyoutstr(const void *, void *, size_t, size_t *);
int	copyin(const void *, void *, size_t)
		__attribute__ ((__bounded__(__buffer__,2,3)));
int	_copyin(const void *, void *, size_t)
		__attribute__ ((__bounded__(__buffer__,2,3)));
int	copyout(const void *, void *, size_t);
int	copyin32(const uint32_t *, uint32_t *);

void	random_start(int);
void	enqueue_randomness(unsigned int);
void	suspend_randomness(void);
void	resume_randomness(char *, size_t);

struct arc4random_ctx;
void	arc4random_buf(void *, size_t)
		__attribute__ ((__bounded__(__buffer__,1,2)));
struct arc4random_ctx	*arc4random_ctx_new(void);
void	arc4random_ctx_free(struct arc4random_ctx *);
void	arc4random_ctx_buf(struct arc4random_ctx *, void *, size_t);
u_int32_t arc4random(void);
u_int32_t arc4random_uniform(u_int32_t);

struct timeval;
struct timespec;
int	tvtohz(const struct timeval *);
int	tstohz(const struct timespec *);
void	realitexpire(void *);

extern uint64_t hardclock_period;
extern uint64_t statclock_avg;
extern int statclock_is_randomized;

struct clockframe;
void	hardclock(struct clockframe *);

struct clockrequest;
void	statclock(struct clockrequest *, void *, void *);

void	initclocks(void);
void	inittodr(time_t);
void	resettodr(void);
void	cpu_initclocks(void);
void	cpu_startclock(void);

void	startprofclock(struct process *);
void	stopprofclock(struct process *);
void	setstatclockrate(int);
void	prof_fork(struct process *);
void	prof_exec(struct process *);
void	prof_write(struct proc *);

void	start_periodic_resettodr(void);
void	stop_periodic_resettodr(void);

void	sleep_setup(const volatile void *, int, const char *);
int	sleep_finish(uint64_t, int);
void	sleep_queue_init(void);

struct cond;
void	cond_init(struct cond *);
void	cond_wait(struct cond *, const char *);
void	cond_signal_handler(void *);

static inline void
cond_signal(struct cond *c)
{
	cond_signal_handler(c);
}

#define	INFSLP	UINT64_MAX
#define	MAXTSLP	(UINT64_MAX - 1)

struct mutex;
struct rwlock;
void    wakeup_n(const volatile void *, int);
void    wakeup(const volatile void *);
#define wakeup_one(c) wakeup_n((c), 1)
int	tsleep(const volatile void *, int, const char *, int);
int	tsleep_nsec(const volatile void *, int, const char *, uint64_t);
int	msleep(const volatile void *, struct mutex *, int,  const char*, int);
int	msleep_nsec(const volatile void *, struct mutex *, int,  const char*,
	    uint64_t);
int	rwsleep(const volatile void *, struct rwlock *, int, const char *, int);
int	rwsleep_nsec(const volatile void *, struct rwlock *, int, const char *,
	    uint64_t);
void	yield(void);

void	wdog_register(int (*)(void *, int), void *);
void	wdog_shutdown(void *);

/*
 * Startup hooks are functions running after the scheduler has started
 * but before any threads have been created or root has been mounted.
 */

struct hook_desc {
	TAILQ_ENTRY(hook_desc) hd_list;
	void	(*hd_fn)(void *);
	void	*hd_arg;
};
TAILQ_HEAD(hook_desc_head, hook_desc);

extern struct hook_desc_head startuphook_list;

void	*hook_establish(struct hook_desc_head *, int, void (*)(void *), void *);
void	hook_disestablish(struct hook_desc_head *, void *);
void	dohooks(struct hook_desc_head *, int);

#define HOOK_REMOVE	0x01
#define HOOK_FREE	0x02

#define startuphook_establish(fn, arg) \
	hook_establish(&startuphook_list, 1, (fn), (arg))
#define startuphook_disestablish(vhook) \
	hook_disestablish(&startuphook_list, (vhook))
#define dostartuphooks() dohooks(&startuphook_list, HOOK_REMOVE|HOOK_FREE)

struct uio;
int	uiomove(void *, size_t, struct uio *);

#if defined(_KERNEL)

#include <sys/rwlock.h>

extern struct rwlock netlock;

/*
 * Network stack data structures are, unless stated otherwise, protected
 * by the NET_LOCK().  It's a single non-recursive lock for the whole
 * subsystem.
 */
#define	NET_LOCK()	do { rw_enter_write(&netlock); } while (0)
#define	NET_UNLOCK()	do { rw_exit_write(&netlock); } while (0)

/*
 * Reader version of NET_LOCK().
 * The "softnet" thread should be the only thread processing packets
 * without holding an exclusive lock.  This is done to allow read-only
 * ioctl(2) to not block.
 * Shared lock can be grabbed instead of the exclusive version if no field
 * protected by the NET_LOCK() is modified by the ioctl/sysctl.
 * Socket system call can use shared netlock if it has additional locks
 * to protect socket and pcb data structures.
 */
#define	NET_LOCK_SHARED()	do { rw_enter_read(&netlock); } while (0)
#define	NET_UNLOCK_SHARED()	do { rw_exit_read(&netlock); } while (0)

#ifdef DIAGNOSTIC

#define	NET_ASSERT_UNLOCKED()						\
do {									\
	int _s = rw_status(&netlock);					\
	if ((splassert_ctl > 0) && (_s == RW_WRITE))			\
		splassert_fail(0, RW_WRITE, __func__);			\
} while (0)

#define	NET_ASSERT_LOCKED()						\
do {									\
	int _s = rw_status(&netlock);					\
	if ((splassert_ctl > 0) && (_s != RW_WRITE && _s != RW_READ))	\
		splassert_fail(RW_READ, _s, __func__);			\
} while (0)

#define	NET_ASSERT_LOCKED_EXCLUSIVE()					\
do {									\
	int _s = rw_status(&netlock);					\
	if ((splassert_ctl > 0) && (_s != RW_WRITE))			\
		splassert_fail(RW_WRITE, _s, __func__);			\
} while (0)

#else /* DIAGNOSTIC */
#define	NET_ASSERT_UNLOCKED()		do {} while (0)
#define	NET_ASSERT_LOCKED()		do {} while (0)
#define	NET_ASSERT_LOCKED_EXCLUSIVE()	do {} while (0)
#endif /* !DIAGNOSTIC */

__returns_twice int	setjmp(label_t *);
__dead void	longjmp(label_t *);
#endif

void	consinit(void);

void	cpu_startup(void);
void	cpu_configure(void);
void	diskconf(void);

void	powerbutton_event(void);

int nfs_mountroot(void);
int dk_mountroot(void);
extern int (*mountroot)(void);

#include <lib/libkern/libkern.h>

#define bzero(b, n)		__builtin_bzero((b), (n))
#define memcmp(b1, b2, n)	__builtin_memcmp((b1), (b2), (n))
#define memcpy(d, s, n)		__builtin_memcpy((d), (s), (n))
#define memset(b, c, n)		__builtin_memset((b), (c), (n))
#if (defined(__GNUC__) && __GNUC__ >= 4)
#define memmove(d, s, n)	__builtin_memmove((d), (s), (n))
#endif
#if !defined(__clang__) && (defined(__GNUC__) && __GNUC__ >= 4)
#define bcmp(b1, b2, n)		__builtin_bcmp((b1), (b2), (n))
#define bcopy(s, d, n)		__builtin_bcopy((s), (d), (n))
#endif

#if defined(DDB)
/* debugger entry points */
void	db_enter(void);	/* in DDB only */
int	db_rint(int);
#endif

#ifdef BOOT_CONFIG
void	user_config(void);
#endif

#if defined(MULTIPROCESSOR)
void	_kernel_lock_init(void);
void	_kernel_lock(void);
void	_kernel_unlock(void);
int	_kernel_lock_held(void);

#define	KERNEL_LOCK_INIT()		_kernel_lock_init()
#define	KERNEL_LOCK()			_kernel_lock()
#define	KERNEL_UNLOCK()			_kernel_unlock()
#define	KERNEL_ASSERT_LOCKED()		KASSERT(_kernel_lock_held())
#define	KERNEL_ASSERT_UNLOCKED()	KASSERT(panicstr || db_active || !_kernel_lock_held())

#else /* ! MULTIPROCESSOR */

#define	KERNEL_LOCK_INIT()		/* nothing */
#define	KERNEL_LOCK()			/* nothing */
#define	KERNEL_UNLOCK()			/* nothing */
#define	KERNEL_ASSERT_LOCKED()		/* nothing */
#define	KERNEL_ASSERT_UNLOCKED()	/* nothing */

#endif /* MULTIPROCESSOR */

#endif /* __SYSTM_H__ */
