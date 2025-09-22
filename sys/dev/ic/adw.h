/*	$OpenBSD: adw.h,v 1.11 2020/02/18 20:24:52 krw Exp $ */
/*      $NetBSD: adw.h,v 1.9 2000/05/26 15:13:43 dante Exp $        */

/*
 * Generic driver definitions and exported functions for the Advanced
 * Systems Inc. SCSI controllers
 *
 * Copyright (c) 1998, 1999, 2000 The NetBSD Foundation, Inc.
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

#ifndef _ADVANSYS_WIDE_H_
#define _ADVANSYS_WIDE_H_

/******************************************************************************/

typedef int (* ADW_ISR_CALLBACK) (ADW_SOFTC *, ADW_SCSI_REQ_Q *);
typedef void (* ADW_ASYNC_CALLBACK) (ADW_SOFTC *, u_int8_t);


/*
 * per request scatter-gather element limit
 * We could have up to 256 SG lists.
 */
#define ADW_MAX_SG_LIST		255

/*
 * Scatter-Gather Definitions per request.
 */

#define NO_OF_SG_PER_BLOCK	15

/* Number of SG blocks needed. */
#define ADW_NUM_SG_BLOCK \
	((ADW_MAX_SG_LIST + (NO_OF_SG_PER_BLOCK - 1))/NO_OF_SG_PER_BLOCK)


struct adw_ccb {
	ADW_SCSI_REQ_Q		scsiq;
	ADW_SG_BLOCK		sg_block[ADW_NUM_SG_BLOCK];

	struct scsi_sense_data  scsi_sense;

	TAILQ_ENTRY(adw_ccb)	chain;
	struct adw_ccb		*nexthash;
	u_int32_t		hashkey;

	struct scsi_xfer	*xs;	/* the scsi_xfer for this cmd */
	int			flags;	/* see below */

	int			timeout;

	/*
	 * This DMA map maps the buffer involved in the transfer.
	 */
	bus_dmamap_t		dmamap_xfer;
};

typedef struct adw_ccb ADW_CCB;

/* flags for ADW_CCB */
#define CCB_ALLOC	0x01
#define CCB_ABORTING	0x02
#define CCB_ABORTED	0x04


#define ADW_MAX_CCB	63	/* Max. number commands per device (63) */

struct adw_control {
	ADW_CCB		ccbs[ADW_MAX_CCB];	/* all our control blocks */
	ADW_CARRIER	*carriers;		/* all our carriers */
};

/*
 * Offset of a CCB from the beginning of the control DMA mapping.
 */
#define	ADW_CCB_OFF(c)	(offsetof(struct adw_control, ccbs[0]) +	\
		    (((u_long)(c)) - ((u_long)&sc->sc_control->ccbs[0])))

/******************************************************************************/

int adw_init(ADW_SOFTC *sc);
void adw_attach(ADW_SOFTC *sc);
int adw_intr(void *arg);
ADW_CCB *adw_ccb_phys_kv(ADW_SOFTC *, u_int32_t);

/******************************************************************************/

#endif /* _ADVANSYS_ADW_H_ */
