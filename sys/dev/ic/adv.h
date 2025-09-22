/*	$OpenBSD: adv.h,v 1.8 2024/10/22 21:50:02 jsg Exp $	*/
/*      $NetBSD: adv.h,v 1.3 1998/09/26 16:02:56 dante Exp $        */

/*
 * Generic driver definitions and exported functions for the Advanced
 * Systems Inc. Narrow SCSI controllers
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Author: Baldassare Dante Profeta <dante@mclink.it>
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ADVANSYS_NARROW_H_
#define _ADVANSYS_NARROW_H_

#include <dev/ic/advlib.h>

/******************************************************************************/

struct adv_ccb {
	ASC_SG_HEAD	sghead;
	ASC_SCSI_Q	scsiq;

	struct scsi_sense_data scsi_sense;

	TAILQ_ENTRY(adv_ccb) chain;
	struct scsi_xfer	*xs;	/* the scsi_xfer for this cmd */
	int			flags;	/* see below */

	int			timeout;
	/*
	 * This DMA map maps the buffer involved in the transfer.
	 */
	bus_dmamap_t		dmamap_xfer;
};

typedef struct adv_ccb ADV_CCB;

/* flags for ADV_CCB */
#define CCB_ALLOC       0x01
#define CCB_ABORT       0x02
#define	CCB_WATCHDOG	0x10


#define ADV_MAX_CCB	32

struct adv_control {
	ADV_CCB	ccbs[ADV_MAX_CCB];	/* all our control blocks */
};

/*
 * Offset of a CCB from the beginning of the control DMA mapping.
 */
#define	ADV_CCB_OFF(c)	(offsetof(struct adv_control, ccbs[0]) +	\
		    (((u_long)(c)) - ((u_long)&sc->sc_control->ccbs[0])))

/******************************************************************************/

int adv_init(ASC_SOFTC *sc);
void adv_attach(ASC_SOFTC *sc);
int adv_intr(void *arg);

/******************************************************************************/

#endif /* _ADVANSYS_NARROW_H_ */
