/*	$OpenBSD: adw.c,v 1.71 2024/09/20 02:00:46 jsg Exp $ */
/* $NetBSD: adw.c,v 1.23 2000/05/27 18:24:50 dante Exp $	 */

/*
 * Generic driver for the Advanced Systems Inc. SCSI controllers
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/adwlib.h>
#include <dev/microcode/adw/adwmcode.h>
#include <dev/ic/adw.h>

/******************************************************************************/


int adw_alloc_controls(ADW_SOFTC *);
int adw_alloc_carriers(ADW_SOFTC *);
int adw_create_ccbs(ADW_SOFTC *, ADW_CCB *, int);
void adw_ccb_free(void *, void *);
void adw_reset_ccb(ADW_CCB *);
int adw_init_ccb(ADW_SOFTC *, ADW_CCB *);
void *adw_ccb_alloc(void *);
int adw_queue_ccb(ADW_SOFTC *, ADW_CCB *, int);

void adw_scsi_cmd(struct scsi_xfer *);
int adw_build_req(struct scsi_xfer *, ADW_CCB *, int);
void adw_build_sglist(ADW_CCB *, ADW_SCSI_REQ_Q *, ADW_SG_BLOCK *);
void adw_isr_callback(ADW_SOFTC *, ADW_SCSI_REQ_Q *);
void adw_async_callback(ADW_SOFTC *, u_int8_t);

void adw_print_info(ADW_SOFTC *, int);

int adw_poll(ADW_SOFTC *, struct scsi_xfer *, int);
void adw_timeout(void *);
void adw_reset_bus(ADW_SOFTC *);


/******************************************************************************/


struct cfdriver adw_cd = {
	NULL, "adw", DV_DULL
};

const struct scsi_adapter adw_switch = {
	adw_scsi_cmd, NULL, NULL, NULL, NULL
};

/******************************************************************************/
/*                       DMA Mapping for Control Blocks                       */
/******************************************************************************/


int
adw_alloc_controls(ADW_SOFTC *sc)
{
	bus_dma_segment_t seg;
	int             error, rseg;

	/*
	 * Allocate the control structure.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat, sizeof(struct adw_control),
	    NBPG, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) != 0) {
		printf("%s: unable to allocate control structures,"
		       " error = %d\n", sc->sc_dev.dv_xname, error);
		return (error);
	}
	if ((error = bus_dmamem_map(sc->sc_dmat, &seg, rseg,
		   sizeof(struct adw_control), (caddr_t *) & sc->sc_control,
				 BUS_DMA_NOWAIT | BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map control structures, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}

	/*
	 * Create and load the DMA map used for the control blocks.
	 */
	if ((error = bus_dmamap_create(sc->sc_dmat, sizeof(struct adw_control),
			   1, sizeof(struct adw_control), 0, BUS_DMA_NOWAIT,
				       &sc->sc_dmamap_control)) != 0) {
		printf("%s: unable to create control DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}
	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_dmamap_control,
			   sc->sc_control, sizeof(struct adw_control), NULL,
				     BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load control DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}

	return (0);
}


int
adw_alloc_carriers(ADW_SOFTC *sc)
{
	bus_dma_segment_t seg;
	int             error, rseg;

	/*
	 * Allocate the control structure.
	 */
	sc->sc_control->carriers =
	    malloc(ADW_MAX_CARRIER * sizeof(ADW_CARRIER), M_DEVBUF,
		M_NOWAIT);
	if (sc->sc_control->carriers == NULL)
		return (ENOMEM);


	if ((error = bus_dmamem_alloc(sc->sc_dmat,
			sizeof(ADW_CARRIER) * ADW_MAX_CARRIER,
			0x10, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to allocate carrier structures,"
		       " error = %d\n", sc->sc_dev.dv_xname, error);
		return (error);
	}
	if ((error = bus_dmamem_map(sc->sc_dmat, &seg, rseg,
			sizeof(ADW_CARRIER) * ADW_MAX_CARRIER,
			(caddr_t *) &sc->sc_control->carriers,
			BUS_DMA_NOWAIT | BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map carrier structures,"
			" error = %d\n", sc->sc_dev.dv_xname, error);
		return (error);
	}

	/*
	 * Create and load the DMA map used for the control blocks.
	 */
	if ((error = bus_dmamap_create(sc->sc_dmat,
			sizeof(ADW_CARRIER) * ADW_MAX_CARRIER, 1,
			sizeof(ADW_CARRIER) * ADW_MAX_CARRIER, 0,BUS_DMA_NOWAIT,
			&sc->sc_dmamap_carrier)) != 0) {
		printf("%s: unable to create carriers DMA map,"
			" error = %d\n", sc->sc_dev.dv_xname, error);
		return (error);
	}
	if ((error = bus_dmamap_load(sc->sc_dmat,
			sc->sc_dmamap_carrier, sc->sc_control->carriers,
			sizeof(ADW_CARRIER) * ADW_MAX_CARRIER, NULL,
			BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load carriers DMA map,"
			" error = %d\n", sc->sc_dev.dv_xname, error);
		return (error);
	}

	return (0);
}


/******************************************************************************/
/*                           Control Blocks routines                          */
/******************************************************************************/


/*
 * Create a set of ccbs and add them to the free list.  Called once
 * by adw_init().  We return the number of CCBs successfully created.
 */
int
adw_create_ccbs(ADW_SOFTC *sc, ADW_CCB *ccbstore, int count)
{
	ADW_CCB        *ccb;
	int             i, error;

	for (i = 0; i < count; i++) {
		ccb = &ccbstore[i];
		if ((error = adw_init_ccb(sc, ccb)) != 0) {
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
adw_ccb_free(void *xsc, void *xccb)
{
	ADW_SOFTC *sc = xsc;
	ADW_CCB *ccb = xccb;

	adw_reset_ccb(ccb);

	mtx_enter(&sc->sc_ccb_mtx);
	TAILQ_INSERT_HEAD(&sc->sc_free_ccb, ccb, chain);
	mtx_leave(&sc->sc_ccb_mtx);
}


void
adw_reset_ccb(ADW_CCB *ccb)
{

	ccb->flags = 0;
}


int
adw_init_ccb(ADW_SOFTC *sc, ADW_CCB *ccb)
{
	int	hashnum, error;

	/*
	 * Create the DMA map for this CCB.
	 */
	error = bus_dmamap_create(sc->sc_dmat,
				  (ADW_MAX_SG_LIST - 1) * PAGE_SIZE,
			 ADW_MAX_SG_LIST, (ADW_MAX_SG_LIST - 1) * PAGE_SIZE,
		   0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ccb->dmamap_xfer);
	if (error) {
		printf("%s: unable to create CCB DMA map, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}

	/*
	 * put in the phystokv hash table
	 * Never gets taken out.
	 */
	ccb->hashkey = sc->sc_dmamap_control->dm_segs[0].ds_addr +
	    ADW_CCB_OFF(ccb);
	hashnum = CCB_HASH(ccb->hashkey);
	ccb->nexthash = sc->sc_ccbhash[hashnum];
	sc->sc_ccbhash[hashnum] = ccb;
	adw_reset_ccb(ccb);
	return (0);
}


/*
 * Get a free ccb
 *
 * If there are none, see if we can allocate a new one
 */
void *
adw_ccb_alloc(void *xsc)
{
	ADW_SOFTC *sc = xsc;
	ADW_CCB *ccb;

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
 * Given a physical address, find the ccb that it corresponds to.
 */
ADW_CCB *
adw_ccb_phys_kv(ADW_SOFTC *sc, u_int32_t ccb_phys)
{
	int hashnum = CCB_HASH(ccb_phys);
	ADW_CCB *ccb = sc->sc_ccbhash[hashnum];

	while (ccb) {
		if (ccb->hashkey == ccb_phys)
			break;
		ccb = ccb->nexthash;
	}
	return (ccb);
}


/*
 * Queue a CCB to be sent to the controller, and send it if possible.
 */
int
adw_queue_ccb(ADW_SOFTC *sc, ADW_CCB *ccb, int retry)
{
	int		errcode = ADW_SUCCESS;

	if(!retry) {
		TAILQ_INSERT_TAIL(&sc->sc_waiting_ccb, ccb, chain);
	}

	while ((ccb = TAILQ_FIRST(&sc->sc_waiting_ccb)) != NULL) {

		errcode = AdwExeScsiQueue(sc, &ccb->scsiq);
		switch(errcode) {
		case ADW_SUCCESS:
			break;

		case ADW_BUSY:
			printf("ADW_BUSY\n");
			return(ADW_BUSY);

		case ADW_ERROR:
			printf("ADW_ERROR\n");
			TAILQ_REMOVE(&sc->sc_waiting_ccb, ccb, chain);
			return(ADW_ERROR);
		}

		TAILQ_REMOVE(&sc->sc_waiting_ccb, ccb, chain);
		TAILQ_INSERT_TAIL(&sc->sc_pending_ccb, ccb, chain);

		/* ALWAYS initialize stimeout, lest it contain garbage! */
		timeout_set(&ccb->xs->stimeout, adw_timeout, ccb);
		if ((ccb->xs->flags & SCSI_POLL) == 0)
			timeout_add_msec(&ccb->xs->stimeout, ccb->timeout);
	}

	return(errcode);
}


/******************************************************************************/
/*                       SCSI layer interfacing routines                      */
/******************************************************************************/


int
adw_init(ADW_SOFTC *sc)
{
	u_int16_t       warn_code;


	sc->cfg.lib_version = (ADW_LIB_VERSION_MAJOR << 8) |
		ADW_LIB_VERSION_MINOR;
	sc->cfg.chip_version =
		ADW_GET_CHIP_VERSION(sc->sc_iot, sc->sc_ioh, sc->bus_type);

	/*
	 * Reset the chip to start and allow register writes.
	 */
	if (ADW_FIND_SIGNATURE(sc->sc_iot, sc->sc_ioh) == 0) {
		panic("adw_init: adw_find_signature failed");
	} else {
		AdwResetChip(sc->sc_iot, sc->sc_ioh);

		warn_code = AdwInitFromEEPROM(sc);

		if (warn_code & ADW_WARN_EEPROM_CHKSUM)
			printf("%s: Bad checksum found. "
			       "Setting default values\n",
			       sc->sc_dev.dv_xname);
		if (warn_code & ADW_WARN_EEPROM_TERMINATION)
			printf("%s: Bad bus termination setting."
			       "Using automatic termination.\n",
			       sc->sc_dev.dv_xname);
	}

	sc->isr_callback = (ADW_CALLBACK) adw_isr_callback;
	sc->async_callback = (ADW_CALLBACK) adw_async_callback;

	return 0;
}


void
adw_attach(ADW_SOFTC *sc)
{
	struct scsibus_attach_args	saa;
	int				i, error;


	TAILQ_INIT(&sc->sc_free_ccb);
	TAILQ_INIT(&sc->sc_waiting_ccb);
	TAILQ_INIT(&sc->sc_pending_ccb);

	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, adw_ccb_alloc, adw_ccb_free);

	/*
	 * Allocate the Control Blocks.
	 */
	error = adw_alloc_controls(sc);
	if (error)
		return; /* (error) */

	/*
	 * Create and initialize the Control Blocks.
	 */
	i = adw_create_ccbs(sc, sc->sc_control->ccbs, ADW_MAX_CCB);
	if (i == 0) {
		printf("%s: unable to create Control Blocks\n",
		       sc->sc_dev.dv_xname);
		return; /* (ENOMEM) */ ;
	} else if (i != ADW_MAX_CCB) {
		printf("%s: WARNING: only %d of %d Control Blocks"
		       " created\n",
		       sc->sc_dev.dv_xname, i, ADW_MAX_CCB);
	}

	/*
	 * Create and initialize the Carriers.
	 */
	error = adw_alloc_carriers(sc);
	if (error)
		return; /* (error) */

	/*
	 * Zero's the freeze_device status
	 */
	bzero(sc->sc_freeze_dev, sizeof(sc->sc_freeze_dev));

	/*
	 * Initialize the adapter
	 */
	switch (AdwInitDriver(sc)) {
	case ADW_IERR_BIST_PRE_TEST:
		panic("%s: BIST pre-test error",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_IERR_BIST_RAM_TEST:
		panic("%s: BIST RAM test error",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_IERR_MCODE_CHKSUM:
		panic("%s: Microcode checksum error",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_IERR_ILLEGAL_CONNECTION:
		panic("%s: All three connectors are in use",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_IERR_REVERSED_CABLE:
		panic("%s: Cable is reversed",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_IERR_HVD_DEVICE:
		panic("%s: HVD attached to LVD connector",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_IERR_SINGLE_END_DEVICE:
		panic("%s: single-ended device is attached to"
		      " one of the connectors",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_IERR_NO_CARRIER:
		panic("%s: unable to create Carriers",
		      sc->sc_dev.dv_xname);
		break;

	case ADW_WARN_BUSRESET_ERROR:
		printf("%s: WARNING: Bus Reset Error\n",
		      sc->sc_dev.dv_xname);
		break;
	}

	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = sc->chip_scsi_id;
	saa.saa_adapter = &adw_switch;
	saa.saa_adapter_buswidth = ADW_MAX_TID+1;
	saa.saa_luns = 8;
	saa.saa_openings = 4;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);
}


/*
 * start a scsi operation given the command and the data address.
 * Also needs the unit, target and lu.
 */
void
adw_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *sc_link = xs->sc_link;
	ADW_SOFTC      *sc = sc_link->bus->sb_adapter_softc;
	ADW_CCB        *ccb;
	int             s, retry = 0;

	/*
	 * get a ccb to use. If the transfer
	 * is from a buf (possibly from interrupt time)
	 * then we can't allow it to sleep
	 */

	ccb = xs->io;

	ccb->xs = xs;
	ccb->timeout = xs->timeout;

	if (adw_build_req(xs, ccb, xs->flags)) {
retryagain:
		s = splbio();
		retry = adw_queue_ccb(sc, ccb, retry);
		splx(s);

		switch(retry) {
		case ADW_BUSY:
			goto retryagain;

		case ADW_ERROR:
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}

		if ((xs->flags & SCSI_POLL) == 0)
			return;

		/*
		 * If we can't use interrupts, poll on completion
		 */
		if (adw_poll(sc, xs, ccb->timeout)) {
			adw_timeout(ccb);
			if (adw_poll(sc, xs, ccb->timeout))
				adw_timeout(ccb);
		}
	} else {
		/* adw_build_req() has set xs->error already */
		scsi_done(xs);
	}
}


/*
 * Build a request structure for the Wide Boards.
 */
int
adw_build_req(struct scsi_xfer *xs, ADW_CCB *ccb, int flags)
{
	struct scsi_link *sc_link = xs->sc_link;
	ADW_SOFTC      *sc = sc_link->bus->sb_adapter_softc;
	bus_dma_tag_t   dmat = sc->sc_dmat;
	ADW_SCSI_REQ_Q *scsiqp;
	int             error;

	scsiqp = &ccb->scsiq;
	bzero(scsiqp, sizeof(ADW_SCSI_REQ_Q));

	/*
	 * Set the ADW_SCSI_REQ_Q 'ccb_ptr' to point to the
	 * physical CCB structure.
	 */
	scsiqp->ccb_ptr = ccb->hashkey;

	/*
	 * Build the ADW_SCSI_REQ_Q request.
	 */

	/*
	 * Set CDB length and copy it to the request structure.
	 * For wide  boards a CDB length maximum of 16 bytes
	 * is supported.
	 */
	scsiqp->cdb_len = xs->cmdlen;
	bcopy(&xs->cmd, &scsiqp->cdb, 12);
	bcopy((caddr_t)&xs->cmd + 12, &scsiqp->cdb16, 4);

	scsiqp->target_id = sc_link->target;
	scsiqp->target_lun = sc_link->lun;

	scsiqp->vsense_addr = &ccb->scsi_sense;
	scsiqp->sense_addr = sc->sc_dmamap_control->dm_segs[0].ds_addr +
			ADW_CCB_OFF(ccb) + offsetof(struct adw_ccb, scsi_sense);
	scsiqp->sense_len = sizeof(struct scsi_sense_data);

	/*
	 * Build ADW_SCSI_REQ_Q for a scatter-gather buffer command.
	 */
	if (xs->datalen) {
		/*
		 * Map the DMA transfer.
		 */
		error = bus_dmamap_load(dmat,
		      ccb->dmamap_xfer, xs->data, xs->datalen, NULL,
			(flags & SCSI_NOSLEEP) ?
			BUS_DMA_NOWAIT : BUS_DMA_WAITOK);

		if (error) {
			if (error == EFBIG) {
				printf("%s: adw_scsi_cmd, more than %d dma"
				       " segments\n",
				       sc->sc_dev.dv_xname, ADW_MAX_SG_LIST);
			} else {
				printf("%s: adw_scsi_cmd, error %d loading"
				       " dma map\n",
				       sc->sc_dev.dv_xname, error);
			}

			xs->error = XS_DRIVER_STUFFUP;
			return (0);
		}
		bus_dmamap_sync(dmat, ccb->dmamap_xfer,
		    0, ccb->dmamap_xfer->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

		/*
		 * Build scatter-gather list.
		 */
		scsiqp->data_cnt = xs->datalen;
		scsiqp->vdata_addr = xs->data;
		scsiqp->data_addr = ccb->dmamap_xfer->dm_segs[0].ds_addr;
		bzero(ccb->sg_block, sizeof(ADW_SG_BLOCK) * ADW_NUM_SG_BLOCK);
		adw_build_sglist(ccb, scsiqp, ccb->sg_block);
	} else {
		/*
		 * No data xfer, use non S/G values.
		 */
		scsiqp->data_cnt = 0;
		scsiqp->vdata_addr = 0;
		scsiqp->data_addr = 0;
	}

	return (1);
}


/*
 * Build scatter-gather list for Wide Boards.
 */
void
adw_build_sglist(ADW_CCB *ccb, ADW_SCSI_REQ_Q *scsiqp, ADW_SG_BLOCK *sg_block)
{
	u_long          sg_block_next_addr;	/* block and its next */
	u_int32_t       sg_block_physical_addr;
	int             i;	/* how many SG entries */
	bus_dma_segment_t *sg_list = &ccb->dmamap_xfer->dm_segs[0];
	int             sg_elem_cnt = ccb->dmamap_xfer->dm_nsegs;


	sg_block_next_addr = (u_long) sg_block;	/* allow math operation */
	sg_block_physical_addr = ccb->hashkey +
	    offsetof(struct adw_ccb, sg_block[0]);
	scsiqp->sg_real_addr = sg_block_physical_addr;

	/*
	 * If there are more than NO_OF_SG_PER_BLOCK dma segments (hw sg-list)
	 * then split the request into multiple sg-list blocks.
	 */

	do {
		for (i = 0; i < NO_OF_SG_PER_BLOCK; i++) {
			sg_block->sg_list[i].sg_addr = sg_list->ds_addr;
			sg_block->sg_list[i].sg_count = sg_list->ds_len;

			if (--sg_elem_cnt == 0) {
				/* last entry, get out */
				sg_block->sg_cnt = i + 1;
				sg_block->sg_ptr = 0; /* next link = NULL */
				return;
			}
			sg_list++;
		}
		sg_block_next_addr += sizeof(ADW_SG_BLOCK);
		sg_block_physical_addr += sizeof(ADW_SG_BLOCK);

		sg_block->sg_cnt = NO_OF_SG_PER_BLOCK;
		sg_block->sg_ptr = sg_block_physical_addr;
		sg_block = (ADW_SG_BLOCK *) sg_block_next_addr;	/* virt. addr */
	} while (1);
}


/******************************************************************************/
/*                       Interrupts and TimeOut routines                      */
/******************************************************************************/


int
adw_intr(void *arg)
{
	ADW_SOFTC      *sc = arg;


	if(AdwISR(sc) != ADW_FALSE) {
		return (1);
	}

	return (0);
}


/*
 * Poll a particular unit, looking for a particular xs
 */
int
adw_poll(ADW_SOFTC *sc, struct scsi_xfer *xs, int count)
{
	int s;

	/* timeouts are in msec, so we loop in 1000 usec cycles */
	while (count > 0) {
		s = splbio();
		adw_intr(sc);
		splx(s);
		if (xs->flags & ITSDONE) {
			if ((xs->cmd.opcode == INQUIRY)
			    && (xs->sc_link->lun == 0)
			    && (xs->error == XS_NOERROR))
				adw_print_info(sc, xs->sc_link->target);
			return (0);
		}
		delay(1000);	/* only happens in boot so ok */
		count--;
	}
	return (1);
}


void
adw_timeout(void *arg)
{
	ADW_CCB        *ccb = arg;
	struct scsi_xfer *xs = ccb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	ADW_SOFTC      *sc = sc_link->bus->sb_adapter_softc;
	int             s;

	sc_print_addr(sc_link);
	printf("timed out");

	s = splbio();

	if (ccb->flags & CCB_ABORTED) {
	/*
	 * Abort Timed Out
	 *
	 * No more opportunities. Lets try resetting the bus and
	 * reinitialize the host adapter.
	 */
		timeout_del(&xs->stimeout);
		printf(" AGAIN. Resetting SCSI Bus\n");
		adw_reset_bus(sc);
		splx(s);
		return;
	} else if (ccb->flags & CCB_ABORTING) {
	/*
	 * Abort the operation that has timed out.
	 *
	 * Second opportunity.
	 */
		printf("\n");
		xs->error = XS_TIMEOUT;
		ccb->flags |= CCB_ABORTED;
#if 0
		/*
		 * - XXX - 3.3a microcode is BROKEN!!!
		 *
		 * We cannot abort a CCB, so we can only hope the command
		 * get completed before the next timeout, otherwise a
		 * Bus Reset will arrive inexorably.
		 */
		/*
		 * ADW_ABORT_CCB() makes the board to generate an interrupt
		 *
		 * - XXX - The above assertion MUST be verified (and this
		 *         code changed as well [callout_*()]), when the
		 *         ADW_ABORT_CCB will be working again
		 */
		ADW_ABORT_CCB(sc, ccb);
#endif
		/*
		 * waiting for multishot callout_reset() let's restart it
		 * by hand so the next time a timeout event will occur
		 * we will reset the bus.
		 */
		timeout_add_msec(&xs->stimeout, ccb->timeout);
	} else {
	/*
	 * Abort the operation that has timed out.
	 *
	 * First opportunity.
	 */
		printf("\n");
		xs->error = XS_TIMEOUT;
		ccb->flags |= CCB_ABORTING;
#if 0
		/*
		 * - XXX - 3.3a microcode is BROKEN!!!
		 *
		 * We cannot abort a CCB, so we can only hope the command
		 * get completed before the next 2 timeout, otherwise a
		 * Bus Reset will arrive inexorably.
		 */
		/*
		 * ADW_ABORT_CCB() makes the board to generate an interrupt
		 *
		 * - XXX - The above assertion MUST be verified (and this
		 *         code changed as well [callout_*()]), when the
		 *         ADW_ABORT_CCB will be working again
		 */
		ADW_ABORT_CCB(sc, ccb);
#endif
		/*
		 * waiting for multishot callout_reset() let's restart it
		 * by hand so to give a second opportunity to the command
		 * which timed-out.
		 */
		timeout_add_msec(&xs->stimeout, ccb->timeout);
	}

	splx(s);
}


void
adw_reset_bus(ADW_SOFTC *sc)
{
	ADW_CCB	*ccb;
	int	 s;

	s = splbio();
	AdwResetSCSIBus(sc); /* XXX - should check return value? */
	while((ccb = TAILQ_LAST(&sc->sc_pending_ccb,
			adw_pending_ccb)) != NULL) {
		timeout_del(&ccb->xs->stimeout);
		TAILQ_REMOVE(&sc->sc_pending_ccb, ccb, chain);
		TAILQ_INSERT_HEAD(&sc->sc_waiting_ccb, ccb, chain);
	}

	bzero(sc->sc_freeze_dev, sizeof(sc->sc_freeze_dev));
	adw_queue_ccb(sc, TAILQ_FIRST(&sc->sc_waiting_ccb), 1);

	splx(s);
}


/******************************************************************************/
/*              Host Adapter and Peripherals Information Routines             */
/******************************************************************************/


void
adw_print_info(ADW_SOFTC *sc, int tid)
{
	bus_space_handle_t ioh = sc->sc_ioh;
	bus_space_tag_t iot = sc->sc_iot;
	u_int16_t hshk_cfg, able_mask, period = 0;

	/* hshk/HSHK means 'handskake' */

	ADW_READ_WORD_LRAM(iot, ioh,
	    ADW_MC_DEVICE_HSHK_CFG_TABLE + (2 * tid), hshk_cfg);

	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_WDTR_ABLE, able_mask);
	if ((able_mask & ADW_TID_TO_TIDMASK(tid)) == 0)
		hshk_cfg &= ~HSHK_CFG_WIDE_XFR;

	ADW_READ_WORD_LRAM(iot, ioh, ADW_MC_SDTR_ABLE, able_mask);
	if ((able_mask & ADW_TID_TO_TIDMASK(tid)) == 0)
		hshk_cfg &= ~HSHK_CFG_OFFSET;

	printf("%s: target %d using %d bit ", sc->sc_dev.dv_xname, tid,
	    (hshk_cfg & HSHK_CFG_WIDE_XFR) ? 16 : 8);

	if ((hshk_cfg & HSHK_CFG_OFFSET) == 0)
		printf("async ");
	else {
		period = (hshk_cfg & 0x1f00) >> 8;
		switch (period) {
		case 0x11:
			printf("80.0 ");
			break;
		case 0x10:
			printf("40.0 ");
			break;
		default:
			period = (period * 25) + 50;
			printf("%d.%d ", 1000/period, ADW_TENTHS(1000, period));
			break;
		}
		printf("MHz %d REQ/ACK offset ", hshk_cfg & HSHK_CFG_OFFSET);
	}

	printf("xfers\n");
}


/******************************************************************************/
/*                        WIDE boards Interrupt callbacks                     */
/******************************************************************************/


/*
 * adw_isr_callback() - Second Level Interrupt Handler called by AdwISR()
 *
 * Interrupt callback function for the Wide SCSI Adw Library.
 *
 * Notice:
 * Interrupts are disabled by the caller (AdwISR() function), and will be
 * enabled at the end of the caller.
 */
void
adw_isr_callback(ADW_SOFTC *sc, ADW_SCSI_REQ_Q *scsiq)
{
	bus_dma_tag_t   dmat;
	ADW_CCB        *ccb;
	struct scsi_xfer *xs;
	struct scsi_sense_data *s1, *s2;


	ccb = adw_ccb_phys_kv(sc, scsiq->ccb_ptr);
	TAILQ_REMOVE(&sc->sc_pending_ccb, ccb, chain);

	if ((ccb->flags & CCB_ALLOC) == 0) {
		panic("%s: unallocated ccb found on pending list!",
		    sc->sc_dev.dv_xname);
		return;
	}

	xs = ccb->xs;
	timeout_del(&xs->stimeout);

	/*
	 * If we were a data transfer, unload the map that described
	 * the data buffer.
	 */
	dmat = sc->sc_dmat;
	if (xs->datalen) {
		bus_dmamap_sync(dmat, ccb->dmamap_xfer,
		    0, ccb->dmamap_xfer->dm_mapsize,
		    ((xs->flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE));
		bus_dmamap_unload(dmat, ccb->dmamap_xfer);
	}

	/*
	 * 'done_status' contains the command's ending status.
	 * 'host_status' contains the host adapter status.
	 * 'scsi_status' contains the scsi peripheral status.
	 */

	sc->sc_freeze_dev[scsiq->target_id] = 0;
	xs->status = scsiq->scsi_status;

	switch (scsiq->done_status) {
	case QD_NO_ERROR: /* (scsi_status == 0) && (host_status == 0) */
NO_ERROR:
		xs->resid = scsiq->data_cnt;
		xs->error = XS_NOERROR;
		break;

	case QD_WITH_ERROR:
		switch (scsiq->host_status) {
		case QHSTA_NO_ERROR:
			switch (scsiq->scsi_status) {
			case SCSI_COND_MET:
			case SCSI_INTERM:
			case SCSI_INTERM_COND_MET:
				/*
				 * These non-zero status values are
				 * not really error conditions.
				 *
				 * XXX - would it be too paranoid to
				 *       add SCSI_OK here in
				 *       case the docs are wrong re
				 *       QD_NO_ERROR?
				 */
				goto NO_ERROR;

			case SCSI_CHECK:
			case SCSI_TERMINATED:
			case SCSI_ACA_ACTIVE:
				s1 = &ccb->scsi_sense;
				s2 = &xs->sense;
				*s2 = *s1;
				xs->error = XS_SENSE;
				break;

			case SCSI_BUSY:
			case SCSI_QUEUE_FULL:
			case SCSI_RESV_CONFLICT:
				sc->sc_freeze_dev[scsiq->target_id] = 1;
				xs->error = XS_BUSY;
				break;

			default: /* scsiq->scsi_status value */
				printf("%s: bad scsi_status: 0x%02x.\n"
				    ,sc->sc_dev.dv_xname
				    ,scsiq->scsi_status);
				xs->error = XS_DRIVER_STUFFUP;
				break;
			}
			break;

		case QHSTA_M_SEL_TIMEOUT:
			xs->error = XS_SELTIMEOUT;
			break;

		case QHSTA_M_DIRECTION_ERR:
		case QHSTA_M_SXFR_OFF_UFLW:
		case QHSTA_M_SXFR_OFF_OFLW:
		case QHSTA_M_SXFR_XFR_OFLW:
		case QHSTA_M_QUEUE_ABORTED:
		case QHSTA_M_INVALID_DEVICE:
		case QHSTA_M_SGBACKUP_ERROR:
		case QHSTA_M_SXFR_DESELECTED:
		case QHSTA_M_SXFR_XFR_PH_ERR:
		case QHSTA_M_BUS_DEVICE_RESET:
		case QHSTA_M_NO_AUTO_REQ_SENSE:
		case QHSTA_M_BAD_CMPL_STATUS_IN:
		case QHSTA_M_SXFR_UNKNOWN_ERROR:
		case QHSTA_M_AUTO_REQ_SENSE_FAIL:
		case QHSTA_M_UNEXPECTED_BUS_FREE:
			printf("%s: host adapter error 0x%02x."
			       " See adw(4).\n"
			    ,sc->sc_dev.dv_xname, scsiq->host_status);
			xs->error = XS_DRIVER_STUFFUP;
			break;

		case QHSTA_M_RDMA_PERR:
		case QHSTA_M_SXFR_WD_TMO:
		case QHSTA_M_WTM_TIMEOUT:
		case QHSTA_M_FROZEN_TIDQ:
		case QHSTA_M_SXFR_SDMA_ERR:
		case QHSTA_M_SXFR_SXFR_PERR:
		case QHSTA_M_SCSI_BUS_RESET:
		case QHSTA_M_DIRECTION_ERR_HUNG:
		case QHSTA_M_SCSI_BUS_RESET_UNSOL:
			/*
			 * XXX - are all these cases really asking
			 *       for a card reset? _BUS_RESET and
			 *       _BUS_RESET_UNSOL added just to make
			 *       sure the pending queue is cleared out
			 *       in case card has lost track of them.
			 */
			printf("%s: host adapter error 0x%02x,"
			       " resetting bus. See adw(4).\n"
			    ,sc->sc_dev.dv_xname, scsiq->host_status);
			adw_reset_bus(sc);
			xs->error = XS_RESET;
			break;

		default: /* scsiq->host_status value */
			/*
			 * XXX - is a panic really appropriate here? If
			 *       not, would it be better to make the
			 *       XS_DRIVER_STUFFUP case above the
			 *       default behaviour? Or XS_RESET?
			 */
			panic("%s: bad host_status: 0x%02x"
			    ,sc->sc_dev.dv_xname, scsiq->host_status);
			break;
		}
		break;

	case QD_ABORTED_BY_HOST:
		xs->error = XS_DRIVER_STUFFUP;
		break;

	default: /* scsiq->done_status value */
		/*
		 * XXX - would QD_NO_STATUS really mean the I/O is not
		 *       done? and would that mean it should somehow be
		 *       put back as a pending I/O?
		 */
		printf("%s: bad done_status: 0x%02x"
		       " (host_status: 0x%02x, scsi_status: 0x%02x)\n"
		    ,sc->sc_dev.dv_xname
		    ,scsiq->done_status
		    ,scsiq->host_status
		    ,scsiq->scsi_status);
		xs->error = XS_DRIVER_STUFFUP;
		break;
	}

	scsi_done(xs);
}


/*
 * adw_async_callback() - Adw Library asynchronous event callback function.
 */
void
adw_async_callback(ADW_SOFTC *sc, u_int8_t code)
{
	switch (code) {
	case ADW_ASYNC_SCSI_BUS_RESET_DET:
		/* The firmware detected a SCSI Bus reset. */
		printf("%s: SCSI Bus reset detected\n", sc->sc_dev.dv_xname);
		break;

	case ADW_ASYNC_RDMA_FAILURE:
		/*
		 * Handle RDMA failure by resetting the SCSI Bus and
		 * possibly the chip if it is unresponsive.
		 */
		printf("%s: RDMA failure. Resetting the SCSI Bus and"
				" the adapter\n", sc->sc_dev.dv_xname);
		adw_reset_bus(sc);
		break;

	case ADW_HOST_SCSI_BUS_RESET:
		/* Host generated SCSI bus reset occurred. */
		printf("%s: Host generated SCSI bus reset occurred\n",
				sc->sc_dev.dv_xname);
		break;


	case ADW_ASYNC_CARRIER_READY_FAILURE:
		/*
		 * Carrier Ready failure.
		 *
		 * A warning only - RISC too busy to realize it's been
		 * tickled. Occurs in normal operation under heavy
		 * load, so a message is printed only when ADW_DEBUG'ing
		 */
#ifdef ADW_DEBUG
		printf("%s: Carrier Ready failure!\n", sc->sc_dev.dv_xname);
#endif
		break;

	default:
		printf("%s: Unknown Async callback code (ignored): 0x%02x\n",
		    sc->sc_dev.dv_xname, code);
		break;
	}
}
