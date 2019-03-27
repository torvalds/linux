/*-
 * Copyright (c) 2010 Max Khon <fjoe@freebsd.org>
 * All rights reserved.
 *
 * This software was developed by Max Khon under sponsorship from
 * the FreeBSD Foundation and Ethon Technologies GmbH.
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
 *
 * $Id: bsd-compat.c 9253 2010-09-02 10:12:09Z fjoe $
 */

#include <sys/types.h>
#include <sys/limits.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/firmware.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <machine/stdarg.h>

#include "mbox_if.h"

#include <interface/compat/vchi_bsd.h>

MALLOC_DEFINE(M_VCHI, "VCHI", "VCHI");

/*
 * Timer API
 */
static void
run_timer(void *arg)
{
	struct timer_list *t = (struct timer_list *) arg;
	void (*function)(unsigned long);

	mtx_lock_spin(&t->mtx);
	if (callout_pending(&t->callout)) {
		/* callout was reset */
		mtx_unlock_spin(&t->mtx);
		return;
	}
	if (!callout_active(&t->callout)) {
		/* callout was stopped */
		mtx_unlock_spin(&t->mtx);
		return;
	}
	callout_deactivate(&t->callout);

	function = t->function;
	mtx_unlock_spin(&t->mtx);

	function(t->data);
}

void
init_timer(struct timer_list *t)
{
	mtx_init(&t->mtx, "dahdi timer lock", NULL, MTX_SPIN);
	callout_init(&t->callout, 1);
	t->expires = 0;
	/*
	 * function and data are not initialized intentionally:
	 * they are not initialized by Linux implementation too
	 */
}

void
setup_timer(struct timer_list *t, void (*function)(unsigned long), unsigned long data)
{
	t->function = function;
	t->data = data;
	init_timer(t);
}

void
mod_timer(struct timer_list *t, unsigned long expires)
{
	mtx_lock_spin(&t->mtx);
	callout_reset(&t->callout, expires - jiffies, run_timer, t);
	mtx_unlock_spin(&t->mtx);
}

void
add_timer(struct timer_list *t)
{
	mod_timer(t, t->expires);
}

int
del_timer_sync(struct timer_list *t)
{
	mtx_lock_spin(&t->mtx);
	callout_stop(&t->callout);
	mtx_unlock_spin(&t->mtx);

	mtx_destroy(&t->mtx);
	return 0;
}

int
del_timer(struct timer_list *t)
{
	del_timer_sync(t);
	return 0;
}

/*
 * Completion API
 */
void
init_completion(struct completion *c)
{
	cv_init(&c->cv, "VCHI completion cv");
	mtx_init(&c->lock, "VCHI completion lock", "condvar", MTX_DEF);
	c->done = 0;
}

void
destroy_completion(struct completion *c)
{
	cv_destroy(&c->cv);
	mtx_destroy(&c->lock);
}

void
complete(struct completion *c)
{
	mtx_lock(&c->lock);

	if (c->done >= 0) {
		KASSERT(c->done < INT_MAX, ("c->done overflow")); /* XXX check */
		c->done++;
		cv_signal(&c->cv);
	} else {
		KASSERT(c->done == -1, ("Invalid value of c->done: %d", c->done));
	}

	mtx_unlock(&c->lock);
}

void
complete_all(struct completion *c)
{
	mtx_lock(&c->lock);

	if (c->done >= 0) {
		KASSERT(c->done < INT_MAX, ("c->done overflow")); /* XXX check */
		c->done = -1;
		cv_broadcast(&c->cv);
	} else {
		KASSERT(c->done == -1, ("Invalid value of c->done: %d", c->done));
	}

	mtx_unlock(&c->lock);
}

void
INIT_COMPLETION_locked(struct completion *c)
{
	mtx_lock(&c->lock);

	c->done = 0;

	mtx_unlock(&c->lock);
}

static void
_completion_claim(struct completion *c)
{

	KASSERT(mtx_owned(&c->lock),
	    ("_completion_claim should be called with acquired lock"));
	KASSERT(c->done != 0, ("_completion_claim on non-waited completion"));
	if (c->done > 0)
		c->done--;
	else
		KASSERT(c->done == -1, ("Invalid value of c->done: %d", c->done));
}

void
wait_for_completion(struct completion *c)
{
	mtx_lock(&c->lock);
	if (!c->done)
		cv_wait(&c->cv, &c->lock);
	c->done--;
	mtx_unlock(&c->lock);
}

int
try_wait_for_completion(struct completion *c)
{
	int res = 0;

	mtx_lock(&c->lock);
	if (!c->done)
		res = 1;
	else
		c->done--;
	mtx_unlock(&c->lock);
	return res == 0;
}

int
wait_for_completion_interruptible_timeout(struct completion *c, unsigned long timeout)
{
	int res = 0;
	unsigned long start, now;
	start = jiffies;

	mtx_lock(&c->lock);
	while (c->done == 0) {
		res = cv_timedwait_sig(&c->cv, &c->lock, timeout);
		if (res)
			goto out;
		now = jiffies;
		if (timeout < (now - start)) {
			res = EWOULDBLOCK;
			goto out;
		}

		timeout -= (now - start);
		start = now;
	}

	_completion_claim(c);
	res = 0;

out:
	mtx_unlock(&c->lock);

	if (res == EWOULDBLOCK) {
		return 0;
	} else if ((res == EINTR) || (res == ERESTART)) {
		return -ERESTART;
	} else {
		KASSERT((res == 0), ("res = %d", res));
		return timeout;
	}
}

int
wait_for_completion_interruptible(struct completion *c)
{
	int res = 0;

	mtx_lock(&c->lock);
	while (c->done == 0) {
		res = cv_wait_sig(&c->cv, &c->lock);
		if (res)
			goto out;
	}

	_completion_claim(c);

out:
	mtx_unlock(&c->lock);

	if ((res == EINTR) || (res == ERESTART))
		res = -ERESTART;
	return res;
}

int
wait_for_completion_killable(struct completion *c)
{

	return wait_for_completion_interruptible(c);
}

/*
 * Semaphore API
 */

void sema_sysinit(void *arg)
{
	struct semaphore *s = arg;

	_sema_init(s, 1);
}

void
_sema_init(struct semaphore *s, int value)
{
	bzero(s, sizeof(*s));
	mtx_init(&s->mtx, "sema lock", "VCHIQ sepmaphore backing lock",
		MTX_DEF | MTX_NOWITNESS | MTX_QUIET);
	cv_init(&s->cv, "sema cv");
	s->value = value;
}

void
_sema_destroy(struct semaphore *s)
{
	mtx_destroy(&s->mtx);
	cv_destroy(&s->cv);
}

void
down(struct semaphore *s)
{

	mtx_lock(&s->mtx);
	while (s->value == 0) {
		s->waiters++;
		cv_wait(&s->cv, &s->mtx);
		s->waiters--;
	}

	s->value--;
	mtx_unlock(&s->mtx);
}

int
down_interruptible(struct semaphore *s)
{
	int ret ;

	ret = 0;

	mtx_lock(&s->mtx);

	while (s->value == 0) {
		s->waiters++;
		ret = cv_wait_sig(&s->cv, &s->mtx);
		s->waiters--;

		if (ret == EINTR) {
			mtx_unlock(&s->mtx);
			return (-EINTR);
		}

		if (ret == ERESTART)
			continue;
	}

	s->value--;
	mtx_unlock(&s->mtx);

	return (0);
}

int
down_trylock(struct semaphore *s)
{
	int ret;

	ret = 0;

	mtx_lock(&s->mtx);

	if (s->value > 0) {
		/* Success. */
		s->value--;
		ret = 0;
	} else {
		ret = -EAGAIN;
	}

	mtx_unlock(&s->mtx);

	return (ret);
}

void
up(struct semaphore *s)
{
	mtx_lock(&s->mtx);
	s->value++;
	if (s->waiters && s->value > 0)
		cv_signal(&s->cv);

	mtx_unlock(&s->mtx);
}

/*
 * Logging API
 */
void
rlprintf(int pps, const char *fmt, ...)
{
	va_list ap;
	static struct timeval last_printf;
	static int count;

	if (ppsratecheck(&last_printf, &count, pps)) {
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}
}

void
device_rlprintf(int pps, device_t dev, const char *fmt, ...)
{
	va_list ap;
	static struct timeval last_printf;
	static int count;

	if (ppsratecheck(&last_printf, &count, pps)) {
		va_start(ap, fmt);
		device_print_prettyname(dev);
		vprintf(fmt, ap);
		va_end(ap);
	}
}

/*
 * Signals API
 */

void
flush_signals(VCHIQ_THREAD_T thr)
{
	printf("Implement ME: %s\n", __func__);
}

int
fatal_signal_pending(VCHIQ_THREAD_T thr)
{
	printf("Implement ME: %s\n", __func__);
	return (0);
}

/*
 * kthread API
 */

/*
 *  This is a hack to avoid memory leak
 */
#define MAX_THREAD_DATA_SLOTS	32
static int thread_data_slot = 0;

struct thread_data {
	void *data;
	int (*threadfn)(void *);
};

static struct thread_data thread_slots[MAX_THREAD_DATA_SLOTS];

static void
kthread_wrapper(void *data)
{
	struct thread_data *slot;

	slot = data;
	slot->threadfn(slot->data);
}

VCHIQ_THREAD_T
vchiq_thread_create(int (*threadfn)(void *data),
	void *data,
	const char namefmt[], ...)
{
	VCHIQ_THREAD_T newp;
	va_list ap;
	char name[MAXCOMLEN+1];
	struct thread_data *slot;

	if (thread_data_slot >= MAX_THREAD_DATA_SLOTS) {
		printf("kthread_create: out of thread data slots\n");
		return (NULL);
	}

	slot = &thread_slots[thread_data_slot];
	slot->data = data;
	slot->threadfn = threadfn;

	va_start(ap, namefmt);
	vsnprintf(name, sizeof(name), namefmt, ap);
	va_end(ap);
	
	newp = NULL;
	if (kproc_create(kthread_wrapper, (void*)slot, &newp, 0, 0,
	    "%s", name) != 0) {
		/* Just to be sure */
		newp = NULL;
	}
	else
		thread_data_slot++;

	return newp;
}

void
set_user_nice(VCHIQ_THREAD_T thr, int nice)
{
	/* NOOP */
}

void
wake_up_process(VCHIQ_THREAD_T thr)
{
	/* NOOP */
}

void
bcm_mbox_write(int channel, uint32_t data)
{
	device_t mbox;

        mbox = devclass_get_device(devclass_find("mbox"), 0);

        if (mbox)
                MBOX_WRITE(mbox, channel, data);
}
