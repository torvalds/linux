/*
 *  linux/kernel/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002-2003 Stephen Rothwell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/sched.h>	/* for MAX_SCHEDULE_TIMEOUT */
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/security.h>
#include <linux/timex.h>
#include <linux/export.h>
#include <linux/migrate.h>
#include <linux/posix-timers.h>
#include <linux/times.h>
#include <linux/ptrace.h>
#include <linux/gfp.h>

#include <linux/uaccess.h>

int compat_get_timex(struct timex *txc, const struct compat_timex __user *utp)
{
	struct compat_timex tx32;

	if (copy_from_user(&tx32, utp, sizeof(struct compat_timex)))
		return -EFAULT;

	txc->modes = tx32.modes;
	txc->offset = tx32.offset;
	txc->freq = tx32.freq;
	txc->maxerror = tx32.maxerror;
	txc->esterror = tx32.esterror;
	txc->status = tx32.status;
	txc->constant = tx32.constant;
	txc->precision = tx32.precision;
	txc->tolerance = tx32.tolerance;
	txc->time.tv_sec = tx32.time.tv_sec;
	txc->time.tv_usec = tx32.time.tv_usec;
	txc->tick = tx32.tick;
	txc->ppsfreq = tx32.ppsfreq;
	txc->jitter = tx32.jitter;
	txc->shift = tx32.shift;
	txc->stabil = tx32.stabil;
	txc->jitcnt = tx32.jitcnt;
	txc->calcnt = tx32.calcnt;
	txc->errcnt = tx32.errcnt;
	txc->stbcnt = tx32.stbcnt;

	return 0;
}

int compat_put_timex(struct compat_timex __user *utp, const struct timex *txc)
{
	struct compat_timex tx32;

	memset(&tx32, 0, sizeof(struct compat_timex));
	tx32.modes = txc->modes;
	tx32.offset = txc->offset;
	tx32.freq = txc->freq;
	tx32.maxerror = txc->maxerror;
	tx32.esterror = txc->esterror;
	tx32.status = txc->status;
	tx32.constant = txc->constant;
	tx32.precision = txc->precision;
	tx32.tolerance = txc->tolerance;
	tx32.time.tv_sec = txc->time.tv_sec;
	tx32.time.tv_usec = txc->time.tv_usec;
	tx32.tick = txc->tick;
	tx32.ppsfreq = txc->ppsfreq;
	tx32.jitter = txc->jitter;
	tx32.shift = txc->shift;
	tx32.stabil = txc->stabil;
	tx32.jitcnt = txc->jitcnt;
	tx32.calcnt = txc->calcnt;
	tx32.errcnt = txc->errcnt;
	tx32.stbcnt = txc->stbcnt;
	tx32.tai = txc->tai;
	if (copy_to_user(utp, &tx32, sizeof(struct compat_timex)))
		return -EFAULT;
	return 0;
}

static int __compat_get_timeval(struct timeval *tv, const struct compat_timeval __user *ctv)
{
	return (!access_ok(VERIFY_READ, ctv, sizeof(*ctv)) ||
			__get_user(tv->tv_sec, &ctv->tv_sec) ||
			__get_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}

static int __compat_put_timeval(const struct timeval *tv, struct compat_timeval __user *ctv)
{
	return (!access_ok(VERIFY_WRITE, ctv, sizeof(*ctv)) ||
			__put_user(tv->tv_sec, &ctv->tv_sec) ||
			__put_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}

static int __compat_get_timespec(struct timespec *ts, const struct compat_timespec __user *cts)
{
	return (!access_ok(VERIFY_READ, cts, sizeof(*cts)) ||
			__get_user(ts->tv_sec, &cts->tv_sec) ||
			__get_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

static int __compat_put_timespec(const struct timespec *ts, struct compat_timespec __user *cts)
{
	return (!access_ok(VERIFY_WRITE, cts, sizeof(*cts)) ||
			__put_user(ts->tv_sec, &cts->tv_sec) ||
			__put_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

static int __compat_get_timespec64(struct timespec64 *ts64,
				   const struct compat_timespec __user *cts)
{
	struct compat_timespec ts;
	int ret;

	ret = copy_from_user(&ts, cts, sizeof(ts));
	if (ret)
		return -EFAULT;

	ts64->tv_sec = ts.tv_sec;
	ts64->tv_nsec = ts.tv_nsec;

	return 0;
}

static int __compat_put_timespec64(const struct timespec64 *ts64,
				   struct compat_timespec __user *cts)
{
	struct compat_timespec ts = {
		.tv_sec = ts64->tv_sec,
		.tv_nsec = ts64->tv_nsec
	};
	return copy_to_user(cts, &ts, sizeof(ts)) ? -EFAULT : 0;
}

int compat_get_timespec64(struct timespec64 *ts, const void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_from_user(ts, uts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __compat_get_timespec64(ts, uts);
}
EXPORT_SYMBOL_GPL(compat_get_timespec64);

int compat_put_timespec64(const struct timespec64 *ts, void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_to_user(uts, ts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __compat_put_timespec64(ts, uts);
}
EXPORT_SYMBOL_GPL(compat_put_timespec64);

int compat_get_timeval(struct timeval *tv, const void __user *utv)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_from_user(tv, utv, sizeof(*tv)) ? -EFAULT : 0;
	else
		return __compat_get_timeval(tv, utv);
}
EXPORT_SYMBOL_GPL(compat_get_timeval);

int compat_put_timeval(const struct timeval *tv, void __user *utv)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_to_user(utv, tv, sizeof(*tv)) ? -EFAULT : 0;
	else
		return __compat_put_timeval(tv, utv);
}
EXPORT_SYMBOL_GPL(compat_put_timeval);

int compat_get_timespec(struct timespec *ts, const void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_from_user(ts, uts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __compat_get_timespec(ts, uts);
}
EXPORT_SYMBOL_GPL(compat_get_timespec);

int compat_put_timespec(const struct timespec *ts, void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_to_user(uts, ts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __compat_put_timespec(ts, uts);
}
EXPORT_SYMBOL_GPL(compat_put_timespec);

int compat_convert_timespec(struct timespec __user **kts,
			    const void __user *cts)
{
	struct timespec ts;
	struct timespec __user *uts;

	if (!cts || COMPAT_USE_64BIT_TIME) {
		*kts = (struct timespec __user *)cts;
		return 0;
	}

	uts = compat_alloc_user_space(sizeof(ts));
	if (!uts)
		return -EFAULT;
	if (compat_get_timespec(&ts, cts))
		return -EFAULT;
	if (copy_to_user(uts, &ts, sizeof(ts)))
		return -EFAULT;

	*kts = uts;
	return 0;
}

int get_compat_itimerval(struct itimerval *o, const struct compat_itimerval __user *i)
{
	struct compat_itimerval v32;

	if (copy_from_user(&v32, i, sizeof(struct compat_itimerval)))
		return -EFAULT;
	o->it_interval.tv_sec = v32.it_interval.tv_sec;
	o->it_interval.tv_usec = v32.it_interval.tv_usec;
	o->it_value.tv_sec = v32.it_value.tv_sec;
	o->it_value.tv_usec = v32.it_value.tv_usec;
	return 0;
}

int put_compat_itimerval(struct compat_itimerval __user *o, const struct itimerval *i)
{
	struct compat_itimerval v32;

	v32.it_interval.tv_sec = i->it_interval.tv_sec;
	v32.it_interval.tv_usec = i->it_interval.tv_usec;
	v32.it_value.tv_sec = i->it_value.tv_sec;
	v32.it_value.tv_usec = i->it_value.tv_usec;
	return copy_to_user(o, &v32, sizeof(struct compat_itimerval)) ? -EFAULT : 0;
}

#ifdef __ARCH_WANT_SYS_SIGPROCMASK

/*
 * sys_sigprocmask SIG_SETMASK sets the first (compat) word of the
 * blocked set of signals to the supplied signal set
 */
static inline void compat_sig_setmask(sigset_t *blocked, compat_sigset_word set)
{
	memcpy(blocked->sig, &set, sizeof(set));
}

COMPAT_SYSCALL_DEFINE3(sigprocmask, int, how,
		       compat_old_sigset_t __user *, nset,
		       compat_old_sigset_t __user *, oset)
{
	old_sigset_t old_set, new_set;
	sigset_t new_blocked;

	old_set = current->blocked.sig[0];

	if (nset) {
		if (get_user(new_set, nset))
			return -EFAULT;
		new_set &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));

		new_blocked = current->blocked;

		switch (how) {
		case SIG_BLOCK:
			sigaddsetmask(&new_blocked, new_set);
			break;
		case SIG_UNBLOCK:
			sigdelsetmask(&new_blocked, new_set);
			break;
		case SIG_SETMASK:
			compat_sig_setmask(&new_blocked, new_set);
			break;
		default:
			return -EINVAL;
		}

		set_current_blocked(&new_blocked);
	}

	if (oset) {
		if (put_user(old_set, oset))
			return -EFAULT;
	}

	return 0;
}

#endif

int put_compat_rusage(const struct rusage *r, struct compat_rusage __user *ru)
{
	struct compat_rusage r32;
	memset(&r32, 0, sizeof(r32));
	r32.ru_utime.tv_sec = r->ru_utime.tv_sec;
	r32.ru_utime.tv_usec = r->ru_utime.tv_usec;
	r32.ru_stime.tv_sec = r->ru_stime.tv_sec;
	r32.ru_stime.tv_usec = r->ru_stime.tv_usec;
	r32.ru_maxrss = r->ru_maxrss;
	r32.ru_ixrss = r->ru_ixrss;
	r32.ru_idrss = r->ru_idrss;
	r32.ru_isrss = r->ru_isrss;
	r32.ru_minflt = r->ru_minflt;
	r32.ru_majflt = r->ru_majflt;
	r32.ru_nswap = r->ru_nswap;
	r32.ru_inblock = r->ru_inblock;
	r32.ru_oublock = r->ru_oublock;
	r32.ru_msgsnd = r->ru_msgsnd;
	r32.ru_msgrcv = r->ru_msgrcv;
	r32.ru_nsignals = r->ru_nsignals;
	r32.ru_nvcsw = r->ru_nvcsw;
	r32.ru_nivcsw = r->ru_nivcsw;
	if (copy_to_user(ru, &r32, sizeof(r32)))
		return -EFAULT;
	return 0;
}

static int compat_get_user_cpu_mask(compat_ulong_t __user *user_mask_ptr,
				    unsigned len, struct cpumask *new_mask)
{
	unsigned long *k;

	if (len < cpumask_size())
		memset(new_mask, 0, cpumask_size());
	else if (len > cpumask_size())
		len = cpumask_size();

	k = cpumask_bits(new_mask);
	return compat_get_bitmap(k, user_mask_ptr, len * 8);
}

COMPAT_SYSCALL_DEFINE3(sched_setaffinity, compat_pid_t, pid,
		       unsigned int, len,
		       compat_ulong_t __user *, user_mask_ptr)
{
	cpumask_var_t new_mask;
	int retval;

	if (!alloc_cpumask_var(&new_mask, GFP_KERNEL))
		return -ENOMEM;

	retval = compat_get_user_cpu_mask(user_mask_ptr, len, new_mask);
	if (retval)
		goto out;

	retval = sched_setaffinity(pid, new_mask);
out:
	free_cpumask_var(new_mask);
	return retval;
}

COMPAT_SYSCALL_DEFINE3(sched_getaffinity, compat_pid_t,  pid, unsigned int, len,
		       compat_ulong_t __user *, user_mask_ptr)
{
	int ret;
	cpumask_var_t mask;

	if ((len * BITS_PER_BYTE) < nr_cpu_ids)
		return -EINVAL;
	if (len & (sizeof(compat_ulong_t)-1))
		return -EINVAL;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	ret = sched_getaffinity(pid, mask);
	if (ret == 0) {
		size_t retlen = min_t(size_t, len, cpumask_size());

		if (compat_put_bitmap(user_mask_ptr, cpumask_bits(mask), retlen * 8))
			ret = -EFAULT;
		else
			ret = retlen;
	}
	free_cpumask_var(mask);

	return ret;
}

int get_compat_itimerspec(struct itimerspec *dst,
			  const struct compat_itimerspec __user *src)
{
	if (__compat_get_timespec(&dst->it_interval, &src->it_interval) ||
	    __compat_get_timespec(&dst->it_value, &src->it_value))
		return -EFAULT;
	return 0;
}

int put_compat_itimerspec(struct compat_itimerspec __user *dst,
			  const struct itimerspec *src)
{
	if (__compat_put_timespec(&src->it_interval, &dst->it_interval) ||
	    __compat_put_timespec(&src->it_value, &dst->it_value))
		return -EFAULT;
	return 0;
}

int get_compat_itimerspec64(struct itimerspec64 *its,
			const struct compat_itimerspec __user *uits)
{

	if (__compat_get_timespec64(&its->it_interval, &uits->it_interval) ||
	    __compat_get_timespec64(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(get_compat_itimerspec64);

int put_compat_itimerspec64(const struct itimerspec64 *its,
			struct compat_itimerspec __user *uits)
{
	if (__compat_put_timespec64(&its->it_interval, &uits->it_interval) ||
	    __compat_put_timespec64(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(put_compat_itimerspec64);

/*
 * We currently only need the following fields from the sigevent
 * structure: sigev_value, sigev_signo, sig_notify and (sometimes
 * sigev_notify_thread_id).  The others are handled in user mode.
 * We also assume that copying sigev_value.sival_int is sufficient
 * to keep all the bits of sigev_value.sival_ptr intact.
 */
int get_compat_sigevent(struct sigevent *event,
		const struct compat_sigevent __user *u_event)
{
	memset(event, 0, sizeof(*event));
	return (!access_ok(VERIFY_READ, u_event, sizeof(*u_event)) ||
		__get_user(event->sigev_value.sival_int,
			&u_event->sigev_value.sival_int) ||
		__get_user(event->sigev_signo, &u_event->sigev_signo) ||
		__get_user(event->sigev_notify, &u_event->sigev_notify) ||
		__get_user(event->sigev_notify_thread_id,
			&u_event->sigev_notify_thread_id))
		? -EFAULT : 0;
}

long compat_get_bitmap(unsigned long *mask, const compat_ulong_t __user *umask,
		       unsigned long bitmap_size)
{
	unsigned long nr_compat_longs;

	/* align bitmap up to nearest compat_long_t boundary */
	bitmap_size = ALIGN(bitmap_size, BITS_PER_COMPAT_LONG);
	nr_compat_longs = BITS_TO_COMPAT_LONGS(bitmap_size);

	if (!access_ok(VERIFY_READ, umask, bitmap_size / 8))
		return -EFAULT;

	user_access_begin();
	while (nr_compat_longs > 1) {
		compat_ulong_t l1, l2;
		unsafe_get_user(l1, umask++, Efault);
		unsafe_get_user(l2, umask++, Efault);
		*mask++ = ((unsigned long)l2 << BITS_PER_COMPAT_LONG) | l1;
		nr_compat_longs -= 2;
	}
	if (nr_compat_longs)
		unsafe_get_user(*mask, umask++, Efault);
	user_access_end();
	return 0;

Efault:
	user_access_end();
	return -EFAULT;
}

long compat_put_bitmap(compat_ulong_t __user *umask, unsigned long *mask,
		       unsigned long bitmap_size)
{
	unsigned long nr_compat_longs;

	/* align bitmap up to nearest compat_long_t boundary */
	bitmap_size = ALIGN(bitmap_size, BITS_PER_COMPAT_LONG);
	nr_compat_longs = BITS_TO_COMPAT_LONGS(bitmap_size);

	if (!access_ok(VERIFY_WRITE, umask, bitmap_size / 8))
		return -EFAULT;

	user_access_begin();
	while (nr_compat_longs > 1) {
		unsigned long m = *mask++;
		unsafe_put_user((compat_ulong_t)m, umask++, Efault);
		unsafe_put_user(m >> BITS_PER_COMPAT_LONG, umask++, Efault);
		nr_compat_longs -= 2;
	}
	if (nr_compat_longs)
		unsafe_put_user((compat_ulong_t)*mask, umask++, Efault);
	user_access_end();
	return 0;
Efault:
	user_access_end();
	return -EFAULT;
}

void
sigset_from_compat(sigset_t *set, const compat_sigset_t *compat)
{
	switch (_NSIG_WORDS) {
	case 4: set->sig[3] = compat->sig[6] | (((long)compat->sig[7]) << 32 );
	case 3: set->sig[2] = compat->sig[4] | (((long)compat->sig[5]) << 32 );
	case 2: set->sig[1] = compat->sig[2] | (((long)compat->sig[3]) << 32 );
	case 1: set->sig[0] = compat->sig[0] | (((long)compat->sig[1]) << 32 );
	}
}
EXPORT_SYMBOL_GPL(sigset_from_compat);

void
sigset_to_compat(compat_sigset_t *compat, const sigset_t *set)
{
	switch (_NSIG_WORDS) {
	case 4: compat->sig[7] = (set->sig[3] >> 32); compat->sig[6] = set->sig[3];
	case 3: compat->sig[5] = (set->sig[2] >> 32); compat->sig[4] = set->sig[2];
	case 2: compat->sig[3] = (set->sig[1] >> 32); compat->sig[2] = set->sig[1];
	case 1: compat->sig[1] = (set->sig[0] >> 32); compat->sig[0] = set->sig[0];
	}
}

#ifdef CONFIG_NUMA
COMPAT_SYSCALL_DEFINE6(move_pages, pid_t, pid, compat_ulong_t, nr_pages,
		       compat_uptr_t __user *, pages32,
		       const int __user *, nodes,
		       int __user *, status,
		       int, flags)
{
	const void __user * __user *pages;
	int i;

	pages = compat_alloc_user_space(nr_pages * sizeof(void *));
	for (i = 0; i < nr_pages; i++) {
		compat_uptr_t p;

		if (get_user(p, pages32 + i) ||
			put_user(compat_ptr(p), pages + i))
			return -EFAULT;
	}
	return sys_move_pages(pid, nr_pages, pages, nodes, status, flags);
}

COMPAT_SYSCALL_DEFINE4(migrate_pages, compat_pid_t, pid,
		       compat_ulong_t, maxnode,
		       const compat_ulong_t __user *, old_nodes,
		       const compat_ulong_t __user *, new_nodes)
{
	unsigned long __user *old = NULL;
	unsigned long __user *new = NULL;
	nodemask_t tmp_mask;
	unsigned long nr_bits;
	unsigned long size;

	nr_bits = min_t(unsigned long, maxnode - 1, MAX_NUMNODES);
	size = ALIGN(nr_bits, BITS_PER_LONG) / 8;
	if (old_nodes) {
		if (compat_get_bitmap(nodes_addr(tmp_mask), old_nodes, nr_bits))
			return -EFAULT;
		old = compat_alloc_user_space(new_nodes ? size * 2 : size);
		if (new_nodes)
			new = old + size / sizeof(unsigned long);
		if (copy_to_user(old, nodes_addr(tmp_mask), size))
			return -EFAULT;
	}
	if (new_nodes) {
		if (compat_get_bitmap(nodes_addr(tmp_mask), new_nodes, nr_bits))
			return -EFAULT;
		if (new == NULL)
			new = compat_alloc_user_space(size);
		if (copy_to_user(new, nodes_addr(tmp_mask), size))
			return -EFAULT;
	}
	return sys_migrate_pages(pid, nr_bits + 1, old, new);
}
#endif

COMPAT_SYSCALL_DEFINE2(sched_rr_get_interval,
		       compat_pid_t, pid,
		       struct compat_timespec __user *, interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, (struct timespec __user *)&t);
	set_fs(old_fs);
	if (compat_put_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

/*
 * Allocate user-space memory for the duration of a single system call,
 * in order to marshall parameters inside a compat thunk.
 */
void __user *compat_alloc_user_space(unsigned long len)
{
	void __user *ptr;

	/* If len would occupy more than half of the entire compat space... */
	if (unlikely(len > (((compat_uptr_t)~0) >> 1)))
		return NULL;

	ptr = arch_compat_alloc_user_space(len);

	if (unlikely(!access_ok(VERIFY_WRITE, ptr, len)))
		return NULL;

	return ptr;
}
EXPORT_SYMBOL_GPL(compat_alloc_user_space);
