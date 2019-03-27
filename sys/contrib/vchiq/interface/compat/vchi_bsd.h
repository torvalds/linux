/*-
 * Copyright (c) 2010 Max Khon <fjoe@freebsd.org>
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@bluezbox.com>
 * Copyright (c) 2013 Jared D. McNeill <jmcneill@invisible.ca>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef __VCHI_BSD_H__
#define __VCHI_BSD_H__

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/ioccom.h>

/*
 * Copy from/to user API
 */
#define copy_from_user(to, from, n)	copyin((from), (to), (n))
#define copy_to_user(to, from, n)	copyout((from), (to), (n))

/*
 * Bit API
 */

static __inline int
test_and_set_bit(int nr, volatile void *addr)
{
	int val;

	do {
		val = *(volatile int *) addr;
	} while (atomic_cmpset_int(addr, val, val | (1 << nr)) == 0);
	return (val & (1 << nr));
}

static __inline__
int test_and_clear_bit(int nr, volatile void *addr)
{
	int val;

	do {
		val = *(volatile int *) addr;
	} while (atomic_cmpset_int(addr, val, val & ~(1 << nr)) == 0);
	return (val & (1 << nr));
}

/*
 * Atomic API
 */
typedef volatile unsigned atomic_t;

#define atomic_set(p, v)	(*(p) = (v))
#define atomic_read(p)		(*(p))
#define atomic_inc(p)		atomic_add_int(p, 1)
#define atomic_dec(p)		atomic_subtract_int(p, 1)
#define atomic_dec_and_test(p)	(atomic_fetchadd_int(p, -1) == 1)
#define	atomic_inc_return(v)	atomic_add_return(1, (v))
#define	atomic_dec_return(v)	atomic_sub_return(1, (v))
#define atomic_add(v, p)	atomic_add_int(p, v)
#define atomic_sub(v, p)	atomic_subtract_int(p, v)

#define ATOMIC_INIT(v)		(v)

static inline int
atomic_add_return(int i, atomic_t *v)
{
	return i + atomic_fetchadd_int(v, i);
}

static inline int
atomic_sub_return(int i, atomic_t *v)
{
	return atomic_fetchadd_int(v, -i) - i;
}

static inline int
atomic_cmpxchg(atomic_t *v, int oldv, int newv)
{
	if (atomic_cmpset_rel_int(v, oldv, newv))
		return newv;
	else
		return *v;
}

static inline int
atomic_xchg(atomic_t *v, int newv)
{
	int oldv;
	if (newv == 0)
		return atomic_readandclear_int(v);
	else {
		do {
			oldv = atomic_load_acq_int(v);
		} while (!atomic_cmpset_rel_int(v, oldv, newv));
	}

	return (oldv);
}

/*
 * Spinlock API
 */
typedef struct mtx spinlock_t;

#define DEFINE_SPINLOCK(name)				\
	struct mtx name
#define spin_lock_init(lock)	mtx_init(lock, "VCHI spinlock " # lock, NULL, MTX_DEF)
#define spin_lock_destroy(lock)	mtx_destroy(lock)
#define spin_lock(lock)		mtx_lock(lock)
#define spin_unlock(lock)	mtx_unlock(lock)
#define spin_lock_bh(lock)	spin_lock(lock)
#define spin_unlock_bh(lock)	spin_unlock(lock)

/*
 * Mutex API
 */
struct mutex {
	struct sx	mtx;
};

#define	lmutex_init(lock)	sx_init(&(lock)->mtx, #lock)
#define lmutex_lock(lock)	sx_xlock(&(lock)->mtx)
#define	lmutex_unlock(lock)	sx_unlock(&(lock)->mtx)
#define	lmutex_destroy(lock)	sx_destroy(&(lock)->mtx)

#define lmutex_lock_interruptible(lock)	sx_xlock_sig(&(lock)->mtx)

/*
 * Rwlock API
 */
typedef struct rwlock rwlock_t;

#define DEFINE_RWLOCK(name)				\
	struct rwlock name;					\
	SX_SYSINIT(name, &name, #name)
#define rwlock_init(rwlock)	rw_init(rwlock, "VCHI rwlock")
#define read_lock(rwlock)	rw_rlock(rwlock)
#define read_unlock(rwlock)	rw_unlock(rwlock)

#define write_lock(rwlock)	rw_wlock(rwlock)
#define write_unlock(rwlock)	rw_unlock(rwlock)
#define write_lock_irqsave(rwlock, flags)		\
	do {						\
		rw_wlock(rwlock);			\
		(void) &(flags);			\
	} while (0)
#define write_unlock_irqrestore(rwlock, flags)		\
	rw_unlock(rwlock)

#define read_lock_bh(rwlock)	rw_rlock(rwlock)
#define read_unlock_bh(rwlock)	rw_unlock(rwlock)
#define write_lock_bh(rwlock)	rw_wlock(rwlock)
#define write_unlock_bh(rwlock)	rw_unlock(rwlock)

/*
 * Timer API
 */
struct timer_list {
	struct mtx mtx;
	struct callout callout;

	unsigned long expires;
	void (*function)(unsigned long);
	unsigned long data;
};

void init_timer(struct timer_list *t);
void setup_timer(struct timer_list *t, void (*function)(unsigned long), unsigned long data);
void mod_timer(struct timer_list *t, unsigned long expires);
void add_timer(struct timer_list *t);
int del_timer(struct timer_list *t);
int del_timer_sync(struct timer_list *t);

/*
 * Completion API
 */
struct completion {
	struct cv cv;
	struct mtx lock;
	int done;
};

void init_completion(struct completion *c);
void destroy_completion(struct completion *c);
int try_wait_for_completion(struct completion *);
int wait_for_completion_interruptible(struct completion *);
int wait_for_completion_interruptible_timeout(struct completion *, unsigned long ticks);
int wait_for_completion_killable(struct completion *);
void wait_for_completion(struct completion *c);
void complete(struct completion *c);
void complete_all(struct completion *c);
void INIT_COMPLETION_locked(struct completion *c);

#define	INIT_COMPLETION(x)	INIT_COMPLETION_locked(&(x))

/*
 * Semaphore API
 */
struct semaphore {
	struct mtx	mtx;
	struct cv	cv;
	int		value;
	int		waiters;
};

#define	DEFINE_SEMAPHORE(name)		\
	struct semaphore name;		\
	SYSINIT(name##_sema_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    sema_sysinit, &name);					\
	SYSUNINIT(name##_sema_sysuninit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    _sema_destroy, __DEVOLATILE(void *, &(name)))

void sema_sysinit(void *arg);
void _sema_init(struct semaphore *s, int value);
void _sema_destroy(struct semaphore *s);
void down(struct semaphore *s);
int down_interruptible(struct semaphore *s);
int down_trylock(struct semaphore *s);
void up(struct semaphore *s);

/*
 * Logging and assertions API
 */
void rlprintf(int pps, const char *fmt, ...)
	__printflike(2, 3);

void
device_rlprintf(int pps, device_t dev, const char *fmt, ...)
	__printflike(3, 4);

#define might_sleep()

#define WARN(condition, msg)				\
({							\
	int __ret_warn_on = !!(condition);		\
	if (unlikely(__ret_warn_on))			\
		printf((msg));				\
	unlikely(__ret_warn_on);			\
})



#define WARN_ON(condition)				\
({							\
	int __ret_warn_on = !!(condition);		\
	if (unlikely(__ret_warn_on))			\
		printf("WARN_ON: " #condition "\n");	\
	unlikely(__ret_warn_on);			\
})

#define WARN_ON_ONCE(condition) ({			\
	static int __warned;				\
	int __ret_warn_once = !!(condition);		\
							\
	if (unlikely(__ret_warn_once))			\
		if (WARN_ON(!__warned))			\
			__warned = 1;			\
	unlikely(__ret_warn_once);			\
})

#define BUG_ON(cond)					\
	do {						\
		if (cond)				\
			panic("BUG_ON: " #cond);	\
	} while (0)

#define BUG()						\
	do {						\
		panic("BUG: %s:%d", __FILE__, __LINE__);	\
	} while (0)

#define vchiq_static_assert(cond) CTASSERT(cond)

#define KERN_EMERG	"<0>"	/* system is unusable			*/
#define KERN_ALERT	"<1>"	/* action must be taken immediately	*/
#define KERN_CRIT	"<2>"	/* critical conditions			*/
#define KERN_ERR	"<3>"	/* error conditions			*/
#define KERN_WARNING	"<4>"	/* warning conditions			*/
#define KERN_NOTICE	"<5>"	/* normal but significant condition	*/
#define KERN_INFO	"<6>"	/* informational			*/
#define KERN_DEBUG	"<7>"	/* debug-level messages			*/
#define KERN_CONT	""

#define printk(fmt, args...)		printf(fmt, ##args)
#define vprintk(fmt, args)		vprintf(fmt, args)

/*
 * Malloc API
 */
#define GFP_KERNEL	0
#define GFP_ATOMIC	0

MALLOC_DECLARE(M_VCHI);

#define kmalloc(size, flags)	malloc((size), M_VCHI, M_NOWAIT | M_ZERO)
#define kcalloc(n, size, flags)	mallocarray((n), (size), M_VCHI, \
				    M_NOWAIT | M_ZERO)
#define kzalloc(a, b)		kcalloc(1, (a), (b))
#define kfree(p)		free(p, M_VCHI)

/*
 * Kernel module API
 */
#define __init
#define __exit
#define __devinit
#define __devexit
#define __devinitdata

/*
 * Time API
 */
#if 1
/* emulate jiffies */
static inline unsigned long
_jiffies(void)
{
	struct timeval tv;

	microuptime(&tv);
	return tvtohz(&tv);
}

static inline unsigned long
msecs_to_jiffies(unsigned long msecs)
{
	struct timeval tv;

	tv.tv_sec = msecs / 1000000UL;
	tv.tv_usec = msecs % 1000000UL;
	return tvtohz(&tv);
}

#define jiffies			_jiffies()
#else
#define jiffies			ticks
#endif
#define HZ			hz

#define udelay(usec)		DELAY(usec)
#define mdelay(msec)		DELAY((msec) * 1000)

#define schedule_timeout(jiff)	pause("dhdslp", jiff)

#if defined(msleep)
#undef msleep
#endif
#define msleep(msec)		mdelay(msec)

#define time_after(a, b)	((a) > (b))
#define time_after_eq(a, b)	((a) >= (b))
#define time_before(a, b)	time_after((b), (a))

/*
 * kthread API (we use proc)
 */
typedef struct proc * VCHIQ_THREAD_T;

VCHIQ_THREAD_T vchiq_thread_create(int (*threadfn)(void *data),
                                   void *data,
                                   const char namefmt[], ...);
void set_user_nice(VCHIQ_THREAD_T p, int nice);
void wake_up_process(VCHIQ_THREAD_T p);

/*
 * Proc APIs
 */
void flush_signals(VCHIQ_THREAD_T);
int fatal_signal_pending(VCHIQ_THREAD_T);

/*
 * mbox API
 */
void bcm_mbox_write(int channel, uint32_t data);

/*
 * Misc API
 */

#define ENODATA EINVAL

#define __user

#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)
#define	current			curproc
#define EXPORT_SYMBOL(x) 
#define PAGE_ALIGN(addr)	round_page(addr)

typedef	void	irqreturn_t;
typedef	off_t	loff_t;

#define BCM2835_MBOX_CHAN_VCHIQ	3

#define smp_mb	wmb
#define smp_rmb	rmb
#define smp_wmb	wmb

#define device_print_prettyname(dev)	device_printf((dev), "")

#endif /* __VCHI_BSD_H__ */
