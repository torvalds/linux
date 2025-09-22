/* $OpenBSD: shared_intr.c,v 1.23 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: shared_intr.c,v 1.13 2000/03/19 01:46:18 thorpej Exp $ */

/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Common shared-interrupt-line functionality.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/queue.h>

#include <machine/intr.h>

static const char *intr_typename(int);

static const char *
intr_typename(int type)
{

	switch (type) {
	case IST_UNUSABLE:
		return ("disabled");
	case IST_NONE:
		return ("none");
	case IST_PULSE:
		return ("pulsed");
	case IST_EDGE:
		return ("edge-triggered");
	case IST_LEVEL:
		return ("level-triggered");
	}
	panic("intr_typename: unknown type %d", type);
}

struct alpha_shared_intr *
alpha_shared_intr_alloc(unsigned int n)
{
	struct alpha_shared_intr *intr;
	unsigned int i;

	intr = mallocarray(n, sizeof(struct alpha_shared_intr), M_DEVBUF,
	    cold ? M_NOWAIT : M_WAITOK);
	if (intr == NULL)
		panic("alpha_shared_intr_alloc: couldn't malloc intr");

	for (i = 0; i < n; i++) {
		TAILQ_INIT(&intr[i].intr_q);
		intr[i].intr_sharetype = IST_NONE;
		intr[i].intr_dfltsharetype = IST_NONE;
		intr[i].intr_nstrays = 0;
		intr[i].intr_maxstrays = 5;
		intr[i].intr_private = NULL;
	}

	return (intr);
}

int
alpha_shared_intr_dispatch(struct alpha_shared_intr *intr, unsigned int num)
{
	struct alpha_shared_intrhand *ih;
	int rv, handled;

	handled = 0;
	TAILQ_FOREACH(ih, &intr[num].intr_q, ih_q) {
#if defined(MULTIPROCESSOR)
		/* XXX Need to support IPL_MPSAFE eventually. */
		if (ih->ih_level < IPL_CLOCK)
			__mp_lock(&kernel_lock);
#endif
		/*
		 * The handler returns one of three values:
		 *   0:	This interrupt wasn't for me.
		 *   1: This interrupt was for me.
		 *  -1: This interrupt might have been for me, but I can't say
		 *      for sure.
		 */
		rv = (*ih->ih_fn)(ih->ih_arg);
		if (rv)
			ih->ih_count.ec_count++;
#if defined(MULTIPROCESSOR)
		if (ih->ih_level < IPL_CLOCK)
			__mp_unlock(&kernel_lock);
#endif
		handled = handled || (rv != 0);
		if (intr_shared_edge == 0 && rv == 1)
			break;
	}

	return (handled);
}

void *
alpha_shared_intr_establish(struct alpha_shared_intr *intr, unsigned int num,
    int type, int level, int (*fn)(void *), void *arg, const char *basename)
{
	struct alpha_shared_intrhand *ih;

	if (intr[num].intr_sharetype == IST_UNUSABLE) {
		printf("alpha_shared_intr_establish: %s %d: unusable\n",
		    basename, num);
		return NULL;
	}

	/* no point in sleeping unless someone can free memory. */
	ih = malloc(sizeof *ih, M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
	if (ih == NULL)
		panic("alpha_shared_intr_establish: can't malloc intrhand");

#ifdef DIAGNOSTIC
	if (type == IST_NONE)
		panic("alpha_shared_intr_establish: bogus type");
#endif

	switch (intr[num].intr_sharetype) {
	case IST_EDGE:
		intr_shared_edge = 1;
		/* FALLTHROUGH */
	case IST_LEVEL:
		if (type == intr[num].intr_sharetype)
			break;
	case IST_PULSE:
		if (type != IST_NONE) {
			if (TAILQ_EMPTY(&intr[num].intr_q)) {
				printf("alpha_shared_intr_establish: %s %d: warning: using %s on %s\n",
				    basename, num, intr_typename(type),
				    intr_typename(intr[num].intr_sharetype));
				type = intr[num].intr_sharetype;
			} else {
				panic("alpha_shared_intr_establish: %s %d: can't share %s with %s",
				    basename, num, intr_typename(type),
				    intr_typename(intr[num].intr_sharetype));
			}
		}
		break;

	case IST_NONE:
		/* not currently used; safe */
		break;
	}

	ih->ih_intrhead = intr;
	ih->ih_fn = fn;
	ih->ih_arg = arg;
	ih->ih_level = level;
	ih->ih_num = num;
	evcount_attach(&ih->ih_count, basename, &ih->ih_num);

	intr[num].intr_sharetype = type;
	TAILQ_INSERT_TAIL(&intr[num].intr_q, ih, ih_q);

	return (ih);
}

void
alpha_shared_intr_disestablish(struct alpha_shared_intr *intr, void *cookie)
{
	struct alpha_shared_intrhand *ih = cookie;
	unsigned int num = ih->ih_num;

	/*
	 * Just remove it from the list and free the entry.  We let
	 * the caller deal with resetting the share type, if appropriate.
	 */
	evcount_detach(&ih->ih_count);
	TAILQ_REMOVE(&intr[num].intr_q, ih, ih_q);
	free(ih, M_DEVBUF, sizeof *ih);
}

int
alpha_shared_intr_get_sharetype(struct alpha_shared_intr *intr,
    unsigned int num)
{

	return (intr[num].intr_sharetype);
}

int
alpha_shared_intr_isactive(struct alpha_shared_intr *intr, unsigned int num)
{

	return (!TAILQ_EMPTY(&intr[num].intr_q));
}

int
alpha_shared_intr_firstactive(struct alpha_shared_intr *intr, unsigned int num)
{

	return (!TAILQ_EMPTY(&intr[num].intr_q) &&
		TAILQ_NEXT(intr[num].intr_q.tqh_first, ih_q) == NULL);
}

void
alpha_shared_intr_set_dfltsharetype(struct alpha_shared_intr *intr,
    unsigned int num, int newdfltsharetype)
{

#ifdef DIAGNOSTIC
	if (alpha_shared_intr_isactive(intr, num))
		panic("alpha_shared_intr_set_dfltsharetype on active intr");
#endif

	intr[num].intr_dfltsharetype = newdfltsharetype;
	intr[num].intr_sharetype = intr[num].intr_dfltsharetype;
}

void
alpha_shared_intr_set_maxstrays(struct alpha_shared_intr *intr,
    unsigned int num, int newmaxstrays)
{
	int s = splhigh();
	intr[num].intr_maxstrays = newmaxstrays;
	intr[num].intr_nstrays = 0;
	splx(s);
}

void
alpha_shared_intr_reset_strays(struct alpha_shared_intr *intr, unsigned int num)
{

	/*
	 * Don't bother blocking interrupts; this doesn't have to be
	 * precise, but it does need to be fast.
	 */
	intr[num].intr_nstrays = 0;
}

void
alpha_shared_intr_stray(struct alpha_shared_intr *intr, unsigned int num,
    const char *basename)
{

	intr[num].intr_nstrays++;

	if (intr[num].intr_maxstrays == 0)
		return;

	if (intr[num].intr_nstrays <= intr[num].intr_maxstrays)
		log(LOG_ERR, "stray %s %d%s\n", basename, num,
		    intr[num].intr_nstrays >= intr[num].intr_maxstrays ?
		      "; stopped logging" : "");
}

void
alpha_shared_intr_set_private(struct alpha_shared_intr *intr, unsigned int num,
    void *v)
{

	intr[num].intr_private = v;
}

void *
alpha_shared_intr_get_private(struct alpha_shared_intr *intr, unsigned int num)
{

	return (intr[num].intr_private);
}
