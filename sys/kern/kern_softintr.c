/*	$OpenBSD: kern_softintr.c,v 1.1 2025/04/23 15:07:00 visa Exp $	*/

/*
 * Copyright (c) 2021, 2025 Visa Hankala
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include <machine/intr.h>

#include <uvm/uvm_extern.h>

#ifdef __USE_MI_SOFTINTR

struct softintr_hand {
	TAILQ_ENTRY(softintr_hand) sih_q;
	void			(*sih_fn)(void *);
	void			*sih_arg;
	struct cpu_info		*sih_runner;
	int			sih_level;
	unsigned short		sih_flags;
	unsigned short		sih_state;
};

#define SIF_MPSAFE		0x0001

#define SIS_DYING		0x0001
#define SIS_PENDING		0x0002
#define SIS_RESTART		0x0004

TAILQ_HEAD(softintr_queue, softintr_hand);

struct softintr_queue softintr_queue[NSOFTINTR];
struct mutex softintr_lock = MUTEX_INITIALIZER(IPL_HIGH);

void
softintr_init(void)
{
	int i;

	for (i = 0; i < nitems(softintr_queue); i++)
		TAILQ_INIT(&softintr_queue[i]);
}

void
softintr_dispatch(int level)
{
	struct cpu_info *ci = curcpu();
	struct softintr_queue *queue = &softintr_queue[level];
	struct softintr_hand *sih;

	mtx_enter(&softintr_lock);
	while ((sih = TAILQ_FIRST(queue)) != NULL) {
		KASSERT((sih->sih_state & (SIS_PENDING | SIS_RESTART)) ==
		    SIS_PENDING);
		KASSERT(sih->sih_runner == NULL);

		sih->sih_state &= ~SIS_PENDING;
		TAILQ_REMOVE(queue, sih, sih_q);
		sih->sih_runner = ci;
		mtx_leave(&softintr_lock);

		if (sih->sih_flags & SIF_MPSAFE) {
			(*sih->sih_fn)(sih->sih_arg);
		} else {
			KERNEL_LOCK();
			(*sih->sih_fn)(sih->sih_arg);
			KERNEL_UNLOCK();
		}

		mtx_enter(&softintr_lock);
		KASSERT((sih->sih_state & SIS_PENDING) == 0);
		sih->sih_runner = NULL;
		if (sih->sih_state & SIS_RESTART) {
			TAILQ_INSERT_TAIL(queue, sih, sih_q);
			sih->sih_state |= SIS_PENDING;
			sih->sih_state &= ~SIS_RESTART;
		}

		uvmexp.softs++;
	}
	mtx_leave(&softintr_lock);
}

void *
softintr_establish(int ipl, void (*func)(void *), void *arg)
{
	struct softintr_hand *sih;
	int level;
	unsigned short flags = 0;

	if (ipl & IPL_MPSAFE)
		flags |= SIF_MPSAFE;
	ipl &= ~IPL_MPSAFE;

	switch (ipl) {
#ifdef IPL_SOFT
	case IPL_SOFT:
#endif
	case IPL_SOFTCLOCK:
		level = SOFTINTR_CLOCK;
		break;

	case IPL_SOFTNET:
		level = SOFTINTR_NET;
		break;

	case IPL_TTY:
	case IPL_SOFTTTY:
		level = SOFTINTR_TTY;
		break;

	default:
		panic("softintr_establish: unhandled ipl %d", ipl);
	}

	sih = malloc(sizeof(*sih), M_DEVBUF, M_NOWAIT);
	if (sih != NULL) {
		sih->sih_fn = func;
		sih->sih_arg = arg;
		sih->sih_runner = NULL;
		sih->sih_level = level;
		sih->sih_flags = flags;
		sih->sih_state = 0;
	}
	return (sih);
}

void
softintr_disestablish(void *arg)
{
	struct cpu_info *runner;
	struct softintr_hand *sih = arg;

	assertwaitok();

	mtx_enter(&softintr_lock);
	sih->sih_state |= SIS_DYING;
	sih->sih_state &= ~SIS_RESTART;
	if (sih->sih_state & SIS_PENDING) {
		sih->sih_state &= ~SIS_PENDING;
		TAILQ_REMOVE(&softintr_queue[sih->sih_level], sih, sih_q);
	}
	runner = sih->sih_runner;
	mtx_leave(&softintr_lock);

	if (runner != NULL)
		sched_barrier(runner);

	KASSERT((sih->sih_state & (SIS_PENDING | SIS_RESTART)) == 0);
	KASSERT(sih->sih_runner == NULL);

	free(sih, M_DEVBUF, sizeof(*sih));
}

void
softintr_schedule(void *arg)
{
	struct softintr_hand *sih = arg;
	struct softintr_queue *queue = &softintr_queue[sih->sih_level];

	mtx_enter(&softintr_lock);
	KASSERT((sih->sih_state & SIS_DYING) == 0);
	if (sih->sih_runner == NULL) {
		KASSERT((sih->sih_state & SIS_RESTART) == 0);
		if ((sih->sih_state & SIS_PENDING) == 0) {
			TAILQ_INSERT_TAIL(queue, sih, sih_q);
			sih->sih_state |= SIS_PENDING;
			/* Call softintr() while SPL is still at IPL_HIGH. */
			softintr(sih->sih_level);
		}
	} else {
		KASSERT((sih->sih_state & SIS_PENDING) == 0);
		sih->sih_state |= SIS_RESTART;
	}
	mtx_leave(&softintr_lock);
}

#endif /* __USE_MI_SOFTINTR */
