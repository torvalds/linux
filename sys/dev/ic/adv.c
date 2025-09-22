/*	$OpenBSD: adv.c,v 1.54 2024/09/20 02:00:46 jsg Exp $	*/
/*	$NetBSD: adv.c,v 1.6 1998/10/28 20:39:45 dante Exp $	*/

/*
 * Generic driver for the Advanced Systems Inc. Narrow SCSI controllers
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/buf.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/adv.h>
#include <dev/ic/advlib.h>

/* #define ASC_DEBUG */

/******************************************************************************/


static int adv_alloc_ccbs(ASC_SOFTC *);
static int adv_create_ccbs(ASC_SOFTC *, ADV_CCB *, int);
void adv_ccb_free(void *, void *);
static void adv_reset_ccb(ADV_CCB *);
static int adv_init_ccb(ASC_SOFTC *, ADV_CCB *);
void *adv_ccb_alloc(void *);
static void adv_queue_ccb(ASC_SOFTC *, ADV_CCB *);
static void adv_start_ccbs(ASC_SOFTC *);

static u_int8_t *adv_alloc_overrunbuf(char *dvname, bus_dma_tag_t);

static void adv_scsi_cmd(struct scsi_xfer *);
static void adv_narrow_isr_callback(ASC_SOFTC *, ASC_QDONE_INFO *);

static int adv_poll(ASC_SOFTC *, struct scsi_xfer *, int);
static void adv_timeout(void *);
static void adv_watchdog(void *);


/******************************************************************************/


struct cfdriver adv_cd = {
	NULL, "adv", DV_DULL
};


const struct scsi_adapter adv_switch = {
	adv_scsi_cmd, NULL, NULL, NULL, NULL
};


#define ADV_ABORT_TIMEOUT       2000	/* time to wait for abort (mSec) */
#define ADV_WATCH_TIMEOUT       1000	/* time to wait for watchdog (mSec) */


/******************************************************************************/
/*                             Control Blocks routines                        */
/******************************************************************************/


static int
adv_alloc_ccbs(ASC_SOFTC *sc)
{
	bus_dma_segment_t seg;
	int             error, rseg;

	/*
	 * Allocate the control blocks.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat, sizeof(struct adv_control),
			   NBPG, 0, &seg, 1, &rseg,
			   BUS_DMA_NOWAIT | BUS_DMA_ZERO)) != 0) {
		printf("%s: unable to allocate control structures,"
		       " error = %d\n", sc->sc_dev.dv_xname, error);
		return (error);
	}
	if ((error = bus_dmamem_map(sc->sc_dmat, &seg, rseg,
		   sizeof(struct adv_control), (caddr_t *) & sc->sc_control,
				 BUS_DMA_NOWAIT | BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map control structures, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}
	/*
	 * Create and load the DMA map used for the control blocks.
	 */
	if ((error = bus_dmamap_create(sc->sc_dmat, sizeof(struct adv_control),
			   1, sizeof(struct adv_control), 0, BUS_DMA_NOWAIT,
				       &sc->sc_dmamap_control)) != 0) {
		printf("%s: unable to create control DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}
	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_dmamap_control,
			   sc->sc_control, sizeof(struct adv_control), NULL,
				     BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load control DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}
	return (0);
}


/*
 * Create a set of ccbs and add them to the free list.  Called once
 * by adv_init().  We return the number of CCBs successfully created.
 * CCB data is already zeroed on allocation.
 */
static int
adv_create_ccbs(ASC_SOFTC *sc, ADV_CCB *ccbstore, int count)
{
	ADV_CCB        *ccb;
	int             i, error;

	for (i = 0; i < count; i++) {
		ccb = &ccbstore[i];
		if ((error = adv_init_ccb(sc, ccb)) != 0) {
			printf("%s: unable to initialize ccb, error = %d\n",
			       sc->sc_dev.dv_xname, error);
			return (i);
		}
		TAILQ_INSERT_TAIL(&sc->sc_free_ccb, ccb, chain);
	}

	return (i);
}


/*
 * A ccb is put onto the free list.
 */
void
adv_ccb_free(void *xsc, void *xccb)
{
	ASC_SOFTC *sc = xsc;
	ADV_CCB *ccb = xccb;

	adv_reset_ccb(ccb);

	mtx_enter(&sc->sc_ccb_mtx);
	TAILQ_INSERT_HEAD(&sc->sc_free_ccb, ccb, chain);
	mtx_leave(&sc->sc_ccb_mtx);
}


static void
adv_reset_ccb(ADV_CCB *ccb)
{

	ccb->flags = 0;
}


static int
adv_init_ccb(ASC_SOFTC *sc, ADV_CCB *ccb)
{
	int             error;

	/*
	 * Create the DMA map for this CCB.
	 */
	error = bus_dmamap_create(sc->sc_dmat,
				  (ASC_MAX_SG_LIST - 1) * PAGE_SIZE,
			 ASC_MAX_SG_LIST, (ASC_MAX_SG_LIST - 1) * PAGE_SIZE,
		   0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ccb->dmamap_xfer);
	if (error) {
		printf("%s: unable to create DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}
	adv_reset_ccb(ccb);
	return (0);
}


/*
 * Get a free ccb
 */
void *
adv_ccb_alloc(void *xsc)
{
	ASC_SOFTC *sc = xsc;
	ADV_CCB *ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = TAILQ_FIRST(&sc->sc_free_ccb);
	if (ccb) {
		TAILQ_REMOVE(&sc->sc_free_ccb, ccb, chain);
		ccb->flags |= CCB_ALLOC;
	}
	mtx_leave(&sc->sc_ccb_mtx);

	return (ccb);
}

/*
 * Queue a CCB to be sent to the controller, and send it if possible.
 */
static void
adv_queue_ccb(ASC_SOFTC *sc, ADV_CCB *ccb)
{

	timeout_set(&ccb->xs->stimeout, adv_timeout, ccb);
	TAILQ_INSERT_TAIL(&sc->sc_waiting_ccb, ccb, chain);

	adv_start_ccbs(sc);
}


static void
adv_start_ccbs(ASC_SOFTC *sc)
{
	ADV_CCB        *ccb;
	struct scsi_xfer *xs;

	while ((ccb = TAILQ_FIRST(&sc->sc_waiting_ccb)) != NULL) {

		xs = ccb->xs;
		if (ccb->flags & CCB_WATCHDOG)
			timeout_del(&xs->stimeout);

		if (AscExeScsiQueue(sc, &ccb->scsiq) == ASC_BUSY) {
			ccb->flags |= CCB_WATCHDOG;
			timeout_set(&xs->stimeout, adv_watchdog, ccb);
			timeout_add_msec(&xs->stimeout, ADV_WATCH_TIMEOUT);
			break;
		}
		TAILQ_REMOVE(&sc->sc_waiting_ccb, ccb, chain);

		if ((ccb->xs->flags & SCSI_POLL) == 0) {
			timeout_set(&xs->stimeout, adv_timeout, ccb);
			timeout_add_msec(&xs->stimeout, ccb->timeout);
		}
	}
}


/******************************************************************************/
/*                      DMA able memory allocation routines                   */
/******************************************************************************/


/*
 * Allocate a DMA able memory for overrun_buffer.
 * This memory can be safely shared among all the AdvanSys boards.
 */
u_int8_t *
adv_alloc_overrunbuf(char *dvname, bus_dma_tag_t dmat)
{
	static u_int8_t *overrunbuf = NULL;

	bus_dmamap_t    ovrbuf_dmamap;
	bus_dma_segment_t seg;
	int             rseg, error;


	/*
	 * if an overrun buffer has been already allocated don't allocate it
	 * again. Instead return the address of the allocated buffer.
	 */
	if (overrunbuf)
		return (overrunbuf);


	if ((error = bus_dmamem_alloc(dmat, ASC_OVERRUN_BSIZE,
			   NBPG, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to allocate overrun buffer, error = %d\n",
		       dvname, error);
		return (0);
	}
	if ((error = bus_dmamem_map(dmat, &seg, rseg, ASC_OVERRUN_BSIZE,
	(caddr_t *) & overrunbuf, BUS_DMA_NOWAIT | BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map overrun buffer, error = %d\n",
		       dvname, error);

		bus_dmamem_free(dmat, &seg, 1);
		return (0);
	}
	if ((error = bus_dmamap_create(dmat, ASC_OVERRUN_BSIZE, 1,
	      ASC_OVERRUN_BSIZE, 0, BUS_DMA_NOWAIT, &ovrbuf_dmamap)) != 0) {
		printf("%s: unable to create overrun buffer DMA map,"
		       " error = %d\n", dvname, error);

		bus_dmamem_unmap(dmat, overrunbuf, ASC_OVERRUN_BSIZE);
		bus_dmamem_free(dmat, &seg, 1);
		return (0);
	}
	if ((error = bus_dmamap_load(dmat, ovrbuf_dmamap, overrunbuf,
			   ASC_OVERRUN_BSIZE, NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load overrun buffer DMA map,"
		       " error = %d\n", dvname, error);

		bus_dmamap_destroy(dmat, ovrbuf_dmamap);
		bus_dmamem_unmap(dmat, overrunbuf, ASC_OVERRUN_BSIZE);
		bus_dmamem_free(dmat, &seg, 1);
		return (0);
	}
	return (overrunbuf);
}


/******************************************************************************/
/*                         SCSI layer interfacing routines                    */
/******************************************************************************/


int
adv_init(ASC_SOFTC *sc)
{
	int             warn;

	if (!AscFindSignature(sc->sc_iot, sc->sc_ioh))
		panic("adv_init: adv_find_signature failed");

	/*
	 * Read the board configuration
	 */
	AscInitASC_SOFTC(sc);
	warn = AscInitFromEEP(sc);
	if (warn) {
		printf("%s -get: ", sc->sc_dev.dv_xname);
		switch (warn) {
		case -1:
			printf("Chip is not halted\n");
			break;

		case -2:
			printf("Couldn't get MicroCode Start"
			       " address\n");
			break;

		case ASC_WARN_IO_PORT_ROTATE:
			printf("I/O port address modified\n");
			break;

		case ASC_WARN_AUTO_CONFIG:
			printf("I/O port increment switch enabled\n");
			break;

		case ASC_WARN_EEPROM_CHKSUM:
			printf("EEPROM checksum error\n");
			break;

		case ASC_WARN_IRQ_MODIFIED:
			printf("IRQ modified\n");
			break;

		case ASC_WARN_CMD_QNG_CONFLICT:
			printf("tag queuing enabled w/o disconnects\n");
			break;

		default:
			printf("unknown warning %d\n", warn);
		}
	}
	if (sc->scsi_reset_wait > ASC_MAX_SCSI_RESET_WAIT)
		sc->scsi_reset_wait = ASC_MAX_SCSI_RESET_WAIT;

	/*
	 * Modify the board configuration
	 */
	warn = AscInitFromASC_SOFTC(sc);
	if (warn) {
		printf("%s -set: ", sc->sc_dev.dv_xname);
		switch (warn) {
		case ASC_WARN_CMD_QNG_CONFLICT:
			printf("tag queuing enabled w/o disconnects\n");
			break;

		case ASC_WARN_AUTO_CONFIG:
			printf("I/O port increment switch enabled\n");
			break;

		default:
			printf("unknown warning %d\n", warn);
		}
	}
	sc->isr_callback = (ulong) adv_narrow_isr_callback;

	if (!(sc->overrun_buf = adv_alloc_overrunbuf(sc->sc_dev.dv_xname,
						     sc->sc_dmat))) {
		return (1);
	}

	return (0);
}


void
adv_attach(ASC_SOFTC *sc)
{
	struct scsibus_attach_args	saa;
	int				i, error;

	/*
	 * Initialize board RISC chip and enable interrupts.
	 */
	switch (AscInitDriver(sc)) {
	case 0:
		/* AllOK */
		break;

	case 1:
		panic("%s: bad signature", sc->sc_dev.dv_xname);
		break;

	case 2:
		panic("%s: unable to load MicroCode",
		      sc->sc_dev.dv_xname);
		break;

	case 3:
		panic("%s: unable to initialize MicroCode",
		      sc->sc_dev.dv_xname);
		break;

	default:
		panic("%s: unable to initialize board RISC chip",
		      sc->sc_dev.dv_xname);
	}

	TAILQ_INIT(&sc->sc_free_ccb);
	TAILQ_INIT(&sc->sc_waiting_ccb);

	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, adv_ccb_alloc, adv_ccb_free);

	/*
	 * Allocate the Control Blocks.
	 */
	error = adv_alloc_ccbs(sc);
	if (error)
		return; /* (error) */

	/*
	 * Create and initialize the Control Blocks.
	 */
	i = adv_create_ccbs(sc, sc->sc_control->ccbs, ADV_MAX_CCB);
	if (i == 0) {
		printf("%s: unable to create control blocks\n",
		       sc->sc_dev.dv_xname);
		return; /* (ENOMEM) */ ;
	} else if (i != ADV_MAX_CCB) {
		printf("%s: WARNING: only %d of %d control blocks created\n",
		       sc->sc_dev.dv_xname, i, ADV_MAX_CCB);
	}

	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = sc->chip_scsi_id;
	saa.saa_adapter = &adv_switch;
	saa.saa_adapter_buswidth = 7;
	saa.saa_luns = 8;
	saa.saa_openings = 4;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_wwpn = saa.saa_wwnn = 0;
	saa.saa_quirks = saa.saa_flags = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);
}


/*
 * start a scsi operation given the command and the data address.  Also needs
 * the unit, target and lu.
 */
static void
adv_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *sc_link = xs->sc_link;
	ASC_SOFTC      *sc = sc_link->bus->sb_adapter_softc;
	bus_dma_tag_t   dmat = sc->sc_dmat;
	ADV_CCB        *ccb;
	int             flags, error, nsegs;

	/*
	 * get a ccb to use. If the transfer
	 * is from a buf (possibly from interrupt time)
	 * then we can't allow it to sleep
	 */

	flags = xs->flags;
	ccb = xs->io;

	ccb->xs = xs;
	ccb->timeout = xs->timeout;

	/*
	 * Build up the request
	 */
	memset(&ccb->scsiq, 0, sizeof(ASC_SCSI_Q));

	ccb->scsiq.q2.ccb_ptr = (ulong) ccb;

	ccb->scsiq.cdbptr = (u_int8_t *)&xs->cmd;
	ccb->scsiq.q2.cdb_len = xs->cmdlen;
	ccb->scsiq.q1.target_id = ASC_TID_TO_TARGET_ID(sc_link->target);
	ccb->scsiq.q1.target_lun = sc_link->lun;
	ccb->scsiq.q2.target_ix = ASC_TIDLUN_TO_IX(sc_link->target,
						   sc_link->lun);
	ccb->scsiq.q1.sense_addr = sc->sc_dmamap_control->dm_segs[0].ds_addr +
		ADV_CCB_OFF(ccb) + offsetof(struct adv_ccb, scsi_sense);
	ccb->scsiq.q1.sense_len = sizeof(struct scsi_sense_data);

	/*
	 * If  there  are  any  outstanding  requests  for  the  current target,
	 * then  every  255th request  send an  ORDERED request.  This heuristic
	 * tries  to  retain  the  benefit  of request  sorting while preventing
	 * request starvation. 255 is the max number of tags or pending commands
	 * a device may have outstanding.
	 */
	sc->reqcnt[sc_link->target]++;
	if ((sc->reqcnt[sc_link->target] > 0) &&
	    (sc->reqcnt[sc_link->target] % 255) == 0) {
		ccb->scsiq.q2.tag_code = M2_QTAG_MSG_ORDERED;
	} else {
		ccb->scsiq.q2.tag_code = M2_QTAG_MSG_SIMPLE;
	}


	if (xs->datalen) {
		/*
		 * Map the DMA transfer.
		 */
		error = bus_dmamap_load(dmat,
		      ccb->dmamap_xfer, xs->data, xs->datalen, NULL,
					(flags & SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);

		if (error) {
			if (error == EFBIG) {
				printf("%s: adv_scsi_cmd, more than %d dma"
				       " segments\n",
				       sc->sc_dev.dv_xname, ASC_MAX_SG_LIST);
			} else {
				printf("%s: adv_scsi_cmd, error %d loading"
				       " dma map\n",
				       sc->sc_dev.dv_xname, error);
			}

			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}
		bus_dmamap_sync(dmat, ccb->dmamap_xfer,
		    0, ccb->dmamap_xfer->dm_mapsize,
		    ((flags & SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD :
			BUS_DMASYNC_PREWRITE));


		memset(&ccb->sghead, 0, sizeof(ASC_SG_HEAD));

		for (nsegs = 0; nsegs < ccb->dmamap_xfer->dm_nsegs; nsegs++) {

			ccb->sghead.sg_list[nsegs].addr =
				ccb->dmamap_xfer->dm_segs[nsegs].ds_addr;
			ccb->sghead.sg_list[nsegs].bytes =
				ccb->dmamap_xfer->dm_segs[nsegs].ds_len;
		}

		ccb->sghead.entry_cnt = ccb->scsiq.q1.sg_queue_cnt =
			ccb->dmamap_xfer->dm_nsegs;

		ccb->scsiq.q1.cntl |= ASC_QC_SG_HEAD;
		ccb->scsiq.sg_head = &ccb->sghead;
		ccb->scsiq.q1.data_addr = 0;
		ccb->scsiq.q1.data_cnt = 0;
	} else {
		/*
		 * No data xfer, use non S/G values.
		 */
		ccb->scsiq.q1.data_addr = 0;
		ccb->scsiq.q1.data_cnt = 0;
	}

#ifdef ASC_DEBUG
	printf("id = %d, lun = %d, cmd = %d, ccb = 0x%lX \n",
			sc_link->target,
			sc_link->lun, xs->cmd.opcode,
			(unsigned long)ccb);
#endif
	/*
	 * Usually return SUCCESSFULLY QUEUED
	 */
	if ((flags & SCSI_POLL) == 0)
		return;

	/*
	 * If we can't use interrupts, poll on completion
	 */
	if (adv_poll(sc, xs, ccb->timeout)) {
		adv_timeout(ccb);
		if (adv_poll(sc, xs, ccb->timeout))
			adv_timeout(ccb);
	}
}


int
adv_intr(void *arg)
{
	ASC_SOFTC      *sc = arg;

#ifdef ASC_DEBUG
	int int_pend = FALSE;

	if(ASC_IS_INT_PENDING(sc->sc_iot, sc->sc_ioh))
	{
		int_pend = TRUE;
		printf("ISR - ");
	}
#endif
	AscISR(sc);
#ifdef ASC_DEBUG
	if(int_pend)
		printf("\n");
#endif

	return (1);
}


/*
 * Poll a particular unit, looking for a particular xs
 */
static int
adv_poll(ASC_SOFTC *sc, struct scsi_xfer *xs, int count)
{
	int s;

	/* timeouts are in msec, so we loop in 1000 usec cycles */
	while (count) {
		s = splbio();
		adv_intr(sc);
		splx(s);
		if (xs->flags & ITSDONE)
			return (0);
		delay(1000);	/* only happens in boot so ok */
		count--;
	}
	return (1);
}


static void
adv_timeout(void *arg)
{
	ADV_CCB        *ccb = arg;
	struct scsi_xfer *xs = ccb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	ASC_SOFTC      *sc = sc_link->bus->sb_adapter_softc;
	int             s;

	sc_print_addr(sc_link);
	printf("timed out");

	s = splbio();

	/*
	 * If it has been through before, then a previous abort has failed,
	 * don't try abort again, reset the bus instead.
	 */
	if (ccb->flags & CCB_ABORT) {
		/* abort timed out */
		printf(" AGAIN. Resetting Bus\n");
		/* Lets try resetting the bus! */
		if (AscResetBus(sc) == ASC_ERROR) {
			ccb->timeout = sc->scsi_reset_wait;
			adv_queue_ccb(sc, ccb);
		}
	} else {
		/* abort the operation that has timed out */
		printf("\n");
		AscAbortCCB(sc, (u_int32_t) ccb);
		ccb->xs->error = XS_TIMEOUT;
		ccb->timeout = ADV_ABORT_TIMEOUT;
		ccb->flags |= CCB_ABORT;
		adv_queue_ccb(sc, ccb);
	}

	splx(s);
}


static void
adv_watchdog(void *arg)
{
	ADV_CCB        *ccb = arg;
	struct scsi_xfer *xs = ccb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	ASC_SOFTC      *sc = sc_link->bus->sb_adapter_softc;
	int             s;

	s = splbio();

	ccb->flags &= ~CCB_WATCHDOG;
	adv_start_ccbs(sc);

	splx(s);
}


/******************************************************************************/
/*                  NARROW and WIDE boards Interrupt callbacks                */
/******************************************************************************/


/*
 * adv_narrow_isr_callback() - Second Level Interrupt Handler called by AscISR()
 *
 * Interrupt callback function for the Narrow SCSI Asc Library.
 */
static void
adv_narrow_isr_callback(ASC_SOFTC *sc, ASC_QDONE_INFO *qdonep)
{
	bus_dma_tag_t   dmat = sc->sc_dmat;
	ADV_CCB        *ccb = (ADV_CCB *) qdonep->d2.ccb_ptr;
	struct scsi_xfer *xs = ccb->xs;
	struct scsi_sense_data *s1, *s2;


#ifdef ASC_DEBUG
	printf(" - ccb=0x%lx, id=%d, lun=%d, cmd=%d, ",
			(unsigned long)ccb,
			xs->sc_link->target,
			xs->sc_link->lun, xs->cmd.opcode);
#endif
	timeout_del(&xs->stimeout);

	/*
	 * If we were a data transfer, unload the map that described
	 * the data buffer.
	 */
	if (xs->datalen) {
		bus_dmamap_sync(dmat, ccb->dmamap_xfer,
		    0, ccb->dmamap_xfer->dm_mapsize,
		    ((xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
			BUS_DMASYNC_POSTWRITE));
		bus_dmamap_unload(dmat, ccb->dmamap_xfer);
	}
	if ((ccb->flags & CCB_ALLOC) == 0) {
		panic("%s: exiting ccb not allocated!", sc->sc_dev.dv_xname);
		return;
	}
	/*
	 * 'qdonep' contains the command's ending status.
	 */
#ifdef ASC_DEBUG
	printf("d_s=%d, h_s=%d", qdonep->d3.done_stat, qdonep->d3.host_stat);
#endif
	switch (qdonep->d3.done_stat) {
	case ASC_QD_NO_ERROR:
		switch (qdonep->d3.host_stat) {
		case ASC_QHSTA_NO_ERROR:
			xs->error = XS_NOERROR;
			xs->resid = 0;
			break;

		default:
			/* QHSTA error occurred */
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}

		/*
		 * If an INQUIRY command completed successfully, then call
		 * the AscInquiryHandling() function to patch bugged boards.
		 */
		if ((xs->cmd.opcode == SCSICMD_Inquiry) &&
		    (xs->sc_link->lun == 0) &&
		    (xs->datalen - qdonep->remain_bytes) >= 8) {
			AscInquiryHandling(sc,
				      xs->sc_link->target & 0x7,
					   (ASC_SCSI_INQUIRY *) xs->data);
		}
		break;

	case ASC_QD_WITH_ERROR:
		switch (qdonep->d3.host_stat) {
		case ASC_QHSTA_NO_ERROR:
			if (qdonep->d3.scsi_stat == SS_CHK_CONDITION) {
				s1 = &ccb->scsi_sense;
				s2 = &xs->sense;
				*s2 = *s1;
				xs->error = XS_SENSE;
			} else {
				xs->error = XS_DRIVER_STUFFUP;
			}
			break;

		default:
			/* QHSTA error occurred */
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		break;

	case ASC_QD_ABORTED_BY_HOST:
	default:
		xs->error = XS_DRIVER_STUFFUP;
		break;
	}

	scsi_done(xs);
}
