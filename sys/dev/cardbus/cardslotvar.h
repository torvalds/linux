/*	$OpenBSD: cardslotvar.h,v 1.6 2013/10/30 08:47:20 mpi Exp $	*/
/*	$NetBSD: cardslotvar.h,v 1.5 2000/03/13 23:52:38 soren Exp $	*/

/*
 * Copyright (c) 1999
 *       HAYAKAWA Koichi.  All rights reserved.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_CARDBUS_CARDSLOTVAR_H_
#define _DEV_CARDBUS_CARDSLOTVAR_H_

/* require sys/device.h */
/* require sys/queue.h */

struct cardslot_event;

/*
 * The data structure cardslot_attach_args is the attach argument for
 * PCMCIA (including CardBus and 16-bit card) slot.
 */
struct cardslot_attach_args {
	char *caa_busname;

	int caa_slot;

	/* for cardbus... */
	struct cbslot_attach_args *caa_cb_attach;

	/* for 16-bit pcmcia */
	struct pcmciabus_attach_args *caa_16_attach;

	/* XXX: for 16-bit pcmcia.  dirty!
	 * This should be removed to achieve MI.
	 */
	struct pcic_handle *caa_ph;
};


/*
 * The data structure cardslot_attach_args is the attach argument for
 * PCMCIA (including CardBus and 16-bit card) slot.
 */
struct cardslot_softc {
	struct device sc_dev;

	int sc_slot;			/* slot number */
	int sc_status;			/* the status of slot */

	struct cardbus_softc *sc_cb_softc;
	struct pcmcia_softc *sc_16_softc;

	/* An event queue for the thread which processes slot state events. */
	SIMPLEQ_HEAD(, cardslot_event) sc_events;
	struct task sc_event_task;
};

#define CARDSLOT_STATUS_CARD_MASK     0x07
#define CARDSLOT_STATUS_CARD_NONE     0x00  /* NO card inserted */
#define CARDSLOT_STATUS_CARD_16	      0x01  /* 16-bit pcmcia card inserted */
#define CARDSLOT_STATUS_CARD_CB	      0x02  /* CardBus pcmcia card inserted */
#define CARDSLOT_STATUS_UNKNOWN	      0x07  /* Unknown card inserted */

#define CARDSLOT_CARDTYPE(x) ((x) & CARDSLOT_STATUS_CARD_MASK)
#define CARDSLOT_SET_CARDTYPE(x, type) \
	do {(x) &= ~CARDSLOT_STATUS_CARD_MASK;\
	    (x) |= (CARDSLOT_STATUS_CARD_MASK & (type));} while(0)

#define CARDSLOT_STATUS_WORK_MASK     0x08
#define CARDSLOT_STATUS_WORKING	      0x08  /* at least one function works */
#define CARDSLOT_STATUS_NOTWORK	      0x00  /* no functions are working */

#define CARDSLOT_WORK(x) ((x) & CARDSLOT_STATUS_WORK_MASK)
#define CARDSLOT_SET_WORK(x, type) \
	do {(x) &= ~CARDSLOT_STATUS_WORK_MASK;\
	    (x) |= (CARDSLOT_STATUS_WORK_MASK & (type));} while(0)


struct cardslot_event {
	SIMPLEQ_ENTRY(cardslot_event) ce_q;

	int ce_type;
};

typedef struct cardslot_softc *cardslot_t;

/* ce_type */
#define	CARDSLOT_EVENT_INSERTION_16	0
#define	CARDSLOT_EVENT_REMOVAL_16	1

#define	CARDSLOT_EVENT_INSERTION_CB	2
#define	CARDSLOT_EVENT_REMOVAL_CB	3

#define IS_CARDSLOT_INSERT_REMOVE_EV(x) (0 <= (x) && (x) <= 3)

void	cardslot_event_throw(cardslot_t, int);

#endif /* !_DEV_CARDBUS_CARDSLOTVAR_H_ */
