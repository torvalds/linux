/*	$OpenBSD: iha.c,v 1.52 2020/09/22 19:32:52 krw Exp $ */
/*-------------------------------------------------------------------------
 *
 * Device driver for the INI-9XXXU/UW or INIC-940/950  PCI SCSI Controller.
 *
 * Written for 386bsd and FreeBSD by
 *	Winston Hung		<winstonh@initio.com>
 *
 * Copyright (c) 1997-1999 Initio Corp
 * Copyright (c) 2000-2002 Ken Westerback
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 *-------------------------------------------------------------------------
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_message.h>

#include <dev/ic/iha.h>

/* #define IHA_DEBUG_STATE */

/*
 * SCSI Rate Table, indexed by FLAG_SCSI_RATE field of
 * TCS_Flags.
 */
static const u_int8_t iha_rate_tbl[] = {
	/* fast 20		  */
	/* nanosecond divide by 4 */
	12,	/* 50ns,  20M	  */
	18,	/* 75ns,  13.3M	  */
	25,	/* 100ns, 10M	  */
	31,	/* 125ns, 8M	  */
	37,	/* 150ns, 6.6M	  */
	43,	/* 175ns, 5.7M	  */
	50,	/* 200ns, 5M	  */
	62	/* 250ns, 4M	  */
};

int iha_setup_sg_list(struct iha_softc *, struct iha_scb *);
u_int8_t iha_data_over_run(struct iha_scb *);
int iha_push_sense_request(struct iha_softc *, struct iha_scb *);
void iha_timeout(void *);
int  iha_alloc_scbs(struct iha_softc *);
void iha_read_eeprom(bus_space_tag_t, bus_space_handle_t,
			     struct iha_nvram *);
void iha_se2_instr(bus_space_tag_t, bus_space_handle_t, u_int8_t);
u_int16_t iha_se2_rd(bus_space_tag_t, bus_space_handle_t, u_int8_t);
void iha_reset_scsi_bus(struct iha_softc *);
void iha_reset_chip(struct iha_softc *,
			    bus_space_tag_t, bus_space_handle_t);
void iha_reset_dma(bus_space_tag_t, bus_space_handle_t);
void iha_reset_tcs(struct tcs *, u_int8_t);
void iha_print_info(struct iha_softc *, int);
void iha_done_scb(struct iha_softc *, struct iha_scb *);
void iha_exec_scb(struct iha_softc *, struct iha_scb *);
void iha_main(struct iha_softc *, bus_space_tag_t, bus_space_handle_t);
void iha_scsi(struct iha_softc *, bus_space_tag_t, bus_space_handle_t);
int  iha_wait(struct iha_softc *, bus_space_tag_t, bus_space_handle_t,
		      u_int8_t);
void iha_mark_busy_scb(struct iha_scb *);
void *iha_scb_alloc(void *);
void iha_scb_free(void *, void *);
void iha_append_done_scb(struct iha_softc *, struct iha_scb *,
				 u_int8_t);
struct iha_scb *iha_pop_done_scb(struct iha_softc *);
void iha_append_pend_scb(struct iha_softc *, struct iha_scb *);
void iha_push_pend_scb(struct iha_softc *, struct iha_scb *);
struct iha_scb *iha_find_pend_scb(struct iha_softc *);
void iha_sync_done(struct iha_softc *,
			   bus_space_tag_t, bus_space_handle_t);
void iha_wide_done(struct iha_softc *,
			   bus_space_tag_t, bus_space_handle_t);
void iha_bad_seq(struct iha_softc *);
int  iha_next_state(struct iha_softc *,
			    bus_space_tag_t, bus_space_handle_t);
int  iha_state_1(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_state_2(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_state_3(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_state_4(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_state_5(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_state_6(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_state_8(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
void iha_set_ssig(bus_space_tag_t,
			  bus_space_handle_t, u_int8_t, u_int8_t);
int  iha_xpad_in(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_xpad_out(struct iha_softc *,
			  bus_space_tag_t, bus_space_handle_t);
int  iha_xfer_data(struct iha_scb *,
			   bus_space_tag_t, bus_space_handle_t,
			   int direction);
int  iha_status_msg(struct iha_softc *,
			    bus_space_tag_t, bus_space_handle_t);
int  iha_msgin(struct iha_softc *, bus_space_tag_t, bus_space_handle_t);
int  iha_msgin_sdtr(struct iha_softc *);
int  iha_msgin_extended(struct iha_softc *,
				bus_space_tag_t, bus_space_handle_t);
int  iha_msgin_ignore_wid_resid(struct iha_softc *,
					bus_space_tag_t, bus_space_handle_t);
int  iha_msgout(struct iha_softc *,
			bus_space_tag_t, bus_space_handle_t, u_int8_t);
int  iha_msgout_extended(struct iha_softc *,
				 bus_space_tag_t, bus_space_handle_t);
void iha_msgout_abort(struct iha_softc *,
			      bus_space_tag_t, bus_space_handle_t,  u_int8_t);
int  iha_msgout_reject(struct iha_softc *,
			       bus_space_tag_t, bus_space_handle_t);
int  iha_msgout_sdtr(struct iha_softc *,
			     bus_space_tag_t, bus_space_handle_t);
int  iha_msgout_wdtr(struct iha_softc *,
			     bus_space_tag_t, bus_space_handle_t);
void iha_select(struct iha_softc *,
			bus_space_tag_t, bus_space_handle_t,
			struct iha_scb *, u_int8_t);
void iha_busfree(struct iha_softc *,
			 bus_space_tag_t, bus_space_handle_t);
int  iha_resel(struct iha_softc *, bus_space_tag_t, bus_space_handle_t);
void iha_abort_xs(struct iha_softc *, struct scsi_xfer *, u_int8_t);

/*
 * iha_intr - the interrupt service routine for the iha driver
 */
int
iha_intr(void *arg)
{
	bus_space_handle_t ioh;
	struct iha_softc *sc;
	bus_space_tag_t iot;
	int s;

	sc  = (struct iha_softc *)arg;
	iot = sc->sc_iot;
	ioh = sc->sc_ioh;

	if ((bus_space_read_1(iot, ioh, TUL_STAT0) & INTPD) == 0)
		return (0);

	s = splbio(); /* XXX - Or are interrupts off when ISR's are called? */

	if (sc->HCS_Semaph != SEMAPH_IN_MAIN) {
		/* XXX - need these inside a splbio()/splx()? */
		bus_space_write_1(iot, ioh, TUL_IMSK, MASK_ALL);
		sc->HCS_Semaph = SEMAPH_IN_MAIN;

		iha_main(sc, iot, ioh);

		sc->HCS_Semaph = ~SEMAPH_IN_MAIN;
		bus_space_write_1(iot, ioh, TUL_IMSK, (MASK_ALL & ~MSCMP));
	}

	splx(s);

	return (1);
}

/*
 * iha_setup_sg_list -	initialize scatter gather list of pScb from
 *			pScb->SCB_DataDma.
 */
int
iha_setup_sg_list(struct iha_softc *sc, struct iha_scb *pScb)
{
	bus_dma_segment_t *segs = pScb->SCB_DataDma->dm_segs;
	int i, error, nseg = pScb->SCB_DataDma->dm_nsegs;

	if (nseg > 1) {
		error = bus_dmamap_load(sc->sc_dmat, pScb->SCB_SGDma,
				pScb->SCB_SGList, sizeof(pScb->SCB_SGList), NULL,
				(pScb->SCB_Flags & SCSI_NOSLEEP) ?
					BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
		if (error) {
			sc_print_addr(pScb->SCB_Xs->sc_link);
			printf("error %d loading SG list dma map\n", error);
			return (error);
		}

		/*
		 * Only set FLAG_SG when SCB_SGDma is loaded so iha_scsi_done
		 * will not unload an unloaded map.
		 */
		pScb->SCB_Flags	|= FLAG_SG;
		bzero(pScb->SCB_SGList, sizeof(pScb->SCB_SGList));

		pScb->SCB_SGIdx	  = 0;
		pScb->SCB_SGCount = nseg;

		for (i=0; i < nseg; i++) {
			pScb->SCB_SGList[i].SG_Len  = segs[i].ds_len;
			pScb->SCB_SGList[i].SG_Addr = segs[i].ds_addr;
		}

		bus_dmamap_sync(sc->sc_dmat, pScb->SCB_SGDma,
		    0, sizeof(pScb->SCB_SGList), BUS_DMASYNC_PREWRITE);
	}

	return (0);
}

/*
 * iha_scsi_cmd - start execution of a SCSI command. This is called
 *		  from the generic SCSI driver via the field
 *		  sc_adapter.scsi_cmd of iha_softc.
 */
void
iha_scsi_cmd(struct scsi_xfer *xs)
{
	struct iha_scb *pScb;
	struct scsi_link *sc_link = xs->sc_link;
	struct iha_softc *sc = sc_link->bus->sb_adapter_softc;
	int error;

	if ((xs->cmdlen > 12) || (sc_link->target >= IHA_MAX_TARGETS)) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	pScb = xs->io;

	pScb->SCB_Target = sc_link->target;
	pScb->SCB_Lun	 = sc_link->lun;
	pScb->SCB_Tcs	 = &sc->HCS_Tcs[pScb->SCB_Target];
	pScb->SCB_Flags	 = xs->flags;
	pScb->SCB_Ident  = MSG_IDENTIFYFLAG |
		(pScb->SCB_Lun & MSG_IDENTIFY_LUNMASK);

	if ((xs->cmd.opcode != REQUEST_SENSE)
	    && ((pScb->SCB_Flags & SCSI_POLL) == 0))
		pScb->SCB_Ident |= MSG_IDENTIFY_DISCFLAG;

	pScb->SCB_Xs	 = xs;
	pScb->SCB_CDBLen = xs->cmdlen;
	bcopy(&xs->cmd, &pScb->SCB_CDB, xs->cmdlen);

	pScb->SCB_BufCharsLeft = pScb->SCB_BufChars = xs->datalen;

	if ((pScb->SCB_Flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) != 0) {
		error = bus_dmamap_load(sc->sc_dmat, pScb->SCB_DataDma,
		    xs->data, pScb->SCB_BufChars, NULL,
		    (pScb->SCB_Flags & SCSI_NOSLEEP) ?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK);

		if (error) {
			sc_print_addr(xs->sc_link);
			if (error == EFBIG)
				printf("buffer needs >%d dma segments\n",
				    IHA_MAX_SG_ENTRIES);
			else
				printf("error %d loading buffer dma map\n",
				    error);

			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}
		bus_dmamap_sync(sc->sc_dmat, pScb->SCB_DataDma,
		    0, pScb->SCB_BufChars,
		    (pScb->SCB_Flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

		error = iha_setup_sg_list(sc, pScb);
		if (error) {
			bus_dmamap_unload(sc->sc_dmat, pScb->SCB_DataDma);
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}

	}

	/*
	 * Always initialize the stimeout structure as it may
	 * contain garbage that confuses timeout_del() later on.
	 * But, timeout_add() ONLY if we are not polling.
	 */
	timeout_set(&xs->stimeout, iha_timeout, pScb);

	iha_exec_scb(sc, pScb);
}

/*
 * iha_init_tulip - initialize the inic-940/950 card and the rest of the
 *		    iha_softc structure supplied
 */
int
iha_init_tulip(struct iha_softc *sc)
{
	struct iha_scb *pScb;
	struct iha_nvram_scsi *pScsi;
	bus_space_handle_t ioh;
	struct iha_nvram iha_nvram;
	bus_space_tag_t iot;
	int i, error;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;

	iha_read_eeprom(iot, ioh, &iha_nvram);

	pScsi = &iha_nvram.NVM_Scsi[0];

	TAILQ_INIT(&sc->HCS_FreeScb);
	TAILQ_INIT(&sc->HCS_PendScb);
	TAILQ_INIT(&sc->HCS_DoneScb);

	mtx_init(&sc->sc_scb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, iha_scb_alloc, iha_scb_free);

	sc->HCS_Semaph	  = ~SEMAPH_IN_MAIN;
	sc->HCS_JSStatus0 = 0;
	sc->HCS_ActScb	  = NULL;
	sc->sc_id	  = pScsi->NVM_SCSI_Id;
	sc->sc_maxtargets = pScsi->NVM_SCSI_Targets;

	error = iha_alloc_scbs(sc);
	if (error != 0)
		return (error);

	for (i = 0, pScb = sc->HCS_Scb; i < IHA_MAX_SCB; i++, pScb++) {
		pScb->SCB_TagId = i;

		error = bus_dmamap_create(sc->sc_dmat,
		    (IHA_MAX_SG_ENTRIES-1) * PAGE_SIZE, IHA_MAX_SG_ENTRIES,
		    (IHA_MAX_SG_ENTRIES-1) * PAGE_SIZE, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &pScb->SCB_DataDma);

		if (error != 0) {
			printf("%s: couldn't create SCB data DMA map, error = %d\n",
			    sc->sc_dev.dv_xname, error);
			return (error);
		}

		error = bus_dmamap_create(sc->sc_dmat,
				sizeof(pScb->SCB_SGList), 1,
				sizeof(pScb->SCB_SGList), 0,
				BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
				&pScb->SCB_SGDma);
		if (error != 0) {
			printf("%s: couldn't create SCB SG DMA map, error = %d\n",
			    sc->sc_dev.dv_xname, error);
			return (error);
		}

		TAILQ_INSERT_TAIL(&sc->HCS_FreeScb, pScb, SCB_ScbList);
	}

	/* Mask all the interrupts */
	bus_space_write_1(iot, ioh, TUL_IMSK, MASK_ALL);

	/* Stop any I/O and reset the scsi module */
	iha_reset_dma(iot, ioh);
	bus_space_write_1(iot, ioh, TUL_SCTRL0, RSMOD);

	/* Program HBA's SCSI ID */
	bus_space_write_1(iot, ioh, TUL_SID, sc->sc_id << 4);

	/*
	 * Configure the channel as requested by the NVRAM settings read
	 * into iha_nvram by iha_read_eeprom() above.
	 */

	if ((pScsi->NVM_SCSI_Cfg & CFG_EN_PAR) != 0)
		sc->HCS_SConf1 = (SCONFIG0DEFAULT | SPCHK);
	else
		sc->HCS_SConf1 = (SCONFIG0DEFAULT);
	bus_space_write_1(iot, ioh, TUL_SCONFIG0, sc->HCS_SConf1);

	/* selection time out in units of 1.6385 millisecond = 250 ms */
	bus_space_write_1(iot, ioh, TUL_STIMO, 153);

	/* Enable desired SCSI termination configuration read from eeprom */
	bus_space_write_1(iot, ioh, TUL_DCTRL0,
	    (pScsi->NVM_SCSI_Cfg & (CFG_ACT_TERM1 | CFG_ACT_TERM2)));

	bus_space_write_1(iot, ioh, TUL_GCTRL1,
	    ((pScsi->NVM_SCSI_Cfg & CFG_AUTO_TERM) >> 4)
	    | (bus_space_read_1(iot, ioh, TUL_GCTRL1) & (~ATDEN)));

	for (i = 0; i < IHA_MAX_TARGETS; i++) {
		sc->HCS_Tcs[i].TCS_Flags = pScsi->NVM_SCSI_TargetFlags[i];
		iha_reset_tcs(&sc->HCS_Tcs[i], sc->HCS_SConf1);
	}

	iha_reset_chip(sc, iot, ioh);
	bus_space_write_1(iot, ioh, TUL_SIEN, ALL_INTERRUPTS);

	return (0);
}

/*
 * iha_reset_dma - abort any active DMA xfer, reset tulip FIFO.
 */
void
iha_reset_dma(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	if ((bus_space_read_1(iot, ioh, TUL_ISTUS1) & XPEND) != 0) {
		/* if DMA xfer is pending, abort DMA xfer */
		bus_space_write_1(iot, ioh, TUL_DCMD, ABTXFR);
		/* wait Abort DMA xfer done */
		while ((bus_space_read_1(iot, ioh, TUL_ISTUS0) & DABT) == 0)
			;
	}

	bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
}

/*
 * iha_scb_alloc - return the first free SCB, or NULL if there are none.
 */
void *
iha_scb_alloc(void *xsc)
{
	struct iha_softc *sc = xsc;
	struct iha_scb *pScb;

	mtx_enter(&sc->sc_scb_mtx);
	pScb = TAILQ_FIRST(&sc->HCS_FreeScb);
	if (pScb != NULL) {
		pScb->SCB_Status = STATUS_RENT;
		TAILQ_REMOVE(&sc->HCS_FreeScb, pScb, SCB_ScbList);
	}
	mtx_leave(&sc->sc_scb_mtx);

	return (pScb);
}

/*
 * iha_scb_free - append the supplied SCB to the tail of the
 *                HCS_FreeScb queue after clearing and resetting
 *		  everything possible.
 */
void
iha_scb_free(void *xsc, void *xscb)
{
	struct iha_softc *sc = xsc;
	struct iha_scb *pScb = xscb;
	int s;

	s = splbio();
	if (pScb == sc->HCS_ActScb)
		sc->HCS_ActScb = NULL;
	splx(s);

	pScb->SCB_Status = STATUS_QUEUED;
	pScb->SCB_HaStat = HOST_OK;
	pScb->SCB_TaStat = SCSI_OK;

	pScb->SCB_NxtStat  = 0;
	pScb->SCB_Flags	   = 0;
	pScb->SCB_Target   = 0;
	pScb->SCB_Lun	   = 0;
	pScb->SCB_CDBLen   = 0;
	pScb->SCB_Ident	   = 0;
	pScb->SCB_TagMsg   = 0;

	pScb->SCB_BufChars     = 0;
	pScb->SCB_BufCharsLeft = 0;

	pScb->SCB_Xs  = NULL;
	pScb->SCB_Tcs = NULL;

	bzero(pScb->SCB_CDB, sizeof(pScb->SCB_CDB));

	/*
	 * SCB_TagId is set at initialization and never changes
	 */

	mtx_enter(&sc->sc_scb_mtx);
	TAILQ_INSERT_TAIL(&sc->HCS_FreeScb, pScb, SCB_ScbList);
	mtx_leave(&sc->sc_scb_mtx);
}

void
iha_append_pend_scb(struct iha_softc *sc, struct iha_scb *pScb)
{
	/* ASSUMPTION: only called within a splbio()/splx() pair */

	if (pScb == sc->HCS_ActScb)
		sc->HCS_ActScb = NULL;

	pScb->SCB_Status = STATUS_QUEUED;

	TAILQ_INSERT_TAIL(&sc->HCS_PendScb, pScb, SCB_ScbList);
}

void
iha_push_pend_scb(struct iha_softc *sc, struct iha_scb *pScb)
{
	int s;

	s = splbio();

	if (pScb == sc->HCS_ActScb)
		sc->HCS_ActScb = NULL;

	pScb->SCB_Status = STATUS_QUEUED;

	TAILQ_INSERT_HEAD(&sc->HCS_PendScb, pScb, SCB_ScbList);

	splx(s);
}

/*
 * iha_find_pend_scb - scan the pending queue for a SCB that can be
 *		       processed immediately. Return NULL if none found
 *		       and a pointer to the SCB if one is found. If there
 *		       is an active SCB, return NULL!
 */
struct iha_scb *
iha_find_pend_scb(struct iha_softc *sc)
{
	struct iha_scb *pScb;
	struct tcs *pTcs;
	int s;

	s = splbio();

	if (sc->HCS_ActScb != NULL)
		pScb = NULL;

	else
		TAILQ_FOREACH(pScb, &sc->HCS_PendScb, SCB_ScbList) {
			if ((pScb->SCB_Flags & SCSI_RESET) != 0)
				/* ALWAYS willing to reset a device */
				break;

			pTcs = pScb->SCB_Tcs;

			if ((pScb->SCB_TagMsg) != 0) {
				/*
				 * A Tagged I/O. OK to start If no
				 * non-tagged I/O is active on the same
				 * target
				 */
				if (pTcs->TCS_NonTagScb == NULL)
					break;

			} else	if (pScb->SCB_CDB[0] == REQUEST_SENSE) {
				/*
				 * OK to do a non-tagged request sense
				 * even if a non-tagged I/O has been
				 * started, because we don't allow any
				 * disconnect during a request sense op
				 */
				break;

			} else	if (pTcs->TCS_TagCnt == 0) {
				/*
				 * No tagged I/O active on this target,
				 * ok to start a non-tagged one if one
				 * is not already active
				 */
				if (pTcs->TCS_NonTagScb == NULL)
					break;
			}
		}

	splx(s);

	return (pScb);
}

void
iha_mark_busy_scb(struct iha_scb *pScb)
{
	int  s;

	s = splbio();

	pScb->SCB_Status = STATUS_BUSY;

	if (pScb->SCB_TagMsg == 0)
		pScb->SCB_Tcs->TCS_NonTagScb = pScb;
	else
		pScb->SCB_Tcs->TCS_TagCnt++;

	splx(s);
}

void
iha_append_done_scb(struct iha_softc *sc, struct iha_scb *pScb, u_int8_t hastat)
{
	struct tcs *pTcs;
	int s;

	s = splbio();

	if (pScb->SCB_Xs != NULL)
		timeout_del(&pScb->SCB_Xs->stimeout);

	if (pScb == sc->HCS_ActScb)
		sc->HCS_ActScb = NULL;

	pTcs = pScb->SCB_Tcs;

	if (pScb->SCB_TagMsg != 0) {
		if (pTcs->TCS_TagCnt)
			pTcs->TCS_TagCnt--;
	} else if (pTcs->TCS_NonTagScb == pScb)
		pTcs->TCS_NonTagScb = NULL;

	pScb->SCB_Status = STATUS_QUEUED;
	pScb->SCB_HaStat = hastat;

	TAILQ_INSERT_TAIL(&sc->HCS_DoneScb, pScb, SCB_ScbList);

	splx(s);
}

struct iha_scb *
iha_pop_done_scb(struct iha_softc *sc)
{
	struct iha_scb *pScb;
	int s;

	s = splbio();

	pScb = TAILQ_FIRST(&sc->HCS_DoneScb);

	if (pScb != NULL) {
		pScb->SCB_Status = STATUS_RENT;
		TAILQ_REMOVE(&sc->HCS_DoneScb, pScb, SCB_ScbList);
	}

	splx(s);

	return (pScb);
}

/*
 * iha_abort_xs - find the SCB associated with the supplied xs and
 *                stop all processing on it, moving it to the done
 *                queue with the supplied host status value.
 */
void
iha_abort_xs(struct iha_softc *sc, struct scsi_xfer *xs, u_int8_t hastat)
{
	struct iha_scb *pScb, *next;
	int i, s;

	s = splbio();

	/* Check the pending queue for the SCB pointing to xs */

	for (pScb = TAILQ_FIRST(&sc->HCS_PendScb); pScb != NULL; pScb = next) {
		next = TAILQ_NEXT(pScb, SCB_ScbList);
		if (pScb->SCB_Xs == xs) {
			TAILQ_REMOVE(&sc->HCS_PendScb, pScb, SCB_ScbList);
			iha_append_done_scb(sc, pScb, hastat);
			splx(s);
			return;
		}
	}

	/*
	 * If that didn't work, check all BUSY/SELECTING SCB's for one
	 * pointing to xs
	 */

	for (i = 0, pScb = sc->HCS_Scb; i < IHA_MAX_SCB; i++, pScb++)
		switch (pScb->SCB_Status) {
		case STATUS_BUSY:
		case STATUS_SELECT:
			if (pScb->SCB_Xs == xs) {
				iha_append_done_scb(sc, pScb, hastat);
				splx(s);
				return;
			}
			break;
		default:
			break;
		}

	splx(s);
}

/*
 * iha_bad_seq - a SCSI bus phase was encountered out of the
 *               correct/expected sequence. Reset the SCSI bus.
 */
void
iha_bad_seq(struct iha_softc *sc)
{
	struct iha_scb *pScb = sc->HCS_ActScb;

	if (pScb != NULL)
		iha_append_done_scb(sc, pScb, HOST_BAD_PHAS);

	iha_reset_scsi_bus(sc);
	iha_reset_chip(sc, sc->sc_iot, sc->sc_ioh);
}

/*
 * iha_push_sense_request - obtain auto sense data by pushing the
 *                          SCB needing it back onto the pending
 *			    queue with a REQUEST_SENSE CDB.
 */
int
iha_push_sense_request(struct iha_softc *sc, struct iha_scb *pScb)
{
	struct scsi_sense *sensecmd;
	int error;

	/* First sync & unload any existing DataDma and SGDma maps */
	if ((pScb->SCB_Flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) != 0) {
		bus_dmamap_sync(sc->sc_dmat, pScb->SCB_DataDma,
			0, pScb->SCB_BufChars,
			((pScb->SCB_Flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE));
		bus_dmamap_unload(sc->sc_dmat, pScb->SCB_DataDma);
		/* Don't unload this map again until it is reloaded */
		pScb->SCB_Flags &= ~(SCSI_DATA_IN | SCSI_DATA_OUT);
	}
	if ((pScb->SCB_Flags & FLAG_SG) != 0) {
		bus_dmamap_sync(sc->sc_dmat, pScb->SCB_SGDma,
			0, sizeof(pScb->SCB_SGList),
			BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, pScb->SCB_SGDma);
		/* Don't unload this map again until it is reloaded */
		pScb->SCB_Flags &= ~FLAG_SG;
	}

	pScb->SCB_BufChars     = sizeof(pScb->SCB_ScsiSenseData);
	pScb->SCB_BufCharsLeft = sizeof(pScb->SCB_ScsiSenseData);
	bzero(&pScb->SCB_ScsiSenseData, sizeof(pScb->SCB_ScsiSenseData));

	error = bus_dmamap_load(sc->sc_dmat, pScb->SCB_DataDma,
			&pScb->SCB_ScsiSenseData,
			sizeof(pScb->SCB_ScsiSenseData), NULL,
			(pScb->SCB_Flags & SCSI_NOSLEEP) ?
				BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		sc_print_addr(pScb->SCB_Xs->sc_link);
		printf("error %d loading request sense buffer dma map\n",
			error);
		return (error);
	}
	bus_dmamap_sync(sc->sc_dmat, pScb->SCB_DataDma,
	    0, pScb->SCB_BufChars, BUS_DMASYNC_PREREAD);

	/* Save _POLL and _NOSLEEP flags. */
	pScb->SCB_Flags &= SCSI_POLL | SCSI_NOSLEEP;
	pScb->SCB_Flags |= FLAG_RSENS | SCSI_DATA_IN;

	error = iha_setup_sg_list(sc, pScb);
	if (error)
		return (error);

	pScb->SCB_Ident &= ~MSG_IDENTIFY_DISCFLAG;

	pScb->SCB_TagMsg = 0;
	pScb->SCB_TaStat = SCSI_OK;

	bzero(pScb->SCB_CDB, sizeof(pScb->SCB_CDB));

	sensecmd = (struct scsi_sense *)pScb->SCB_CDB;
	pScb->SCB_CDBLen = sizeof(*sensecmd);
	sensecmd->opcode = REQUEST_SENSE;
	sensecmd->byte2  = pScb->SCB_Xs->sc_link->lun << 5;
	sensecmd->length = sizeof(pScb->SCB_ScsiSenseData);

	if ((pScb->SCB_Flags & SCSI_POLL) == 0)
		timeout_add_msec(&pScb->SCB_Xs->stimeout,
		    pScb->SCB_Xs->timeout);

	iha_push_pend_scb(sc, pScb);

	return (0);
}

/*
 * iha_main - process the active SCB, taking one off pending and making it
 *            active if necessary, and any done SCB's created as
 *            a result until there are no interrupts pending and no pending
 *            SCB's that can be started.
 */
void
iha_main(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb;

	for (;;) {
iha_scsi_label:
		iha_scsi(sc, iot, ioh);

		while ((pScb = iha_pop_done_scb(sc)) != NULL) {

			switch (pScb->SCB_TaStat) {
			case SCSI_TERMINATED:
			case SCSI_ACA_ACTIVE:
			case SCSI_CHECK:
				pScb->SCB_Tcs->TCS_Flags &=
				    ~(FLAG_SYNC_DONE | FLAG_WIDE_DONE);

				if ((pScb->SCB_Flags & FLAG_RSENS) != 0)
					/* Check condition on check condition*/
					pScb->SCB_HaStat = HOST_BAD_PHAS;
				else if (iha_push_sense_request(sc, pScb) != 0)
					/* Could not push sense request */
					pScb->SCB_HaStat = HOST_BAD_PHAS;
				else
					/* REQUEST SENSE ready to process */
					goto iha_scsi_label;
				break;

			default:
				if ((pScb->SCB_Flags & FLAG_RSENS) != 0)
					/*
					 * Return the original SCSI_CHECK, not
					 * the status of the request sense
					 * command!
					 */
					pScb->SCB_TaStat = SCSI_CHECK;
				break;
			}

			iha_done_scb(sc, pScb);
		}

		/*
		 * If there are no interrupts pending, or we can't start
		 * a pending sc, break out of the for(;;). Otherwise
		 * continue the good work with another call to
		 * iha_scsi().
		 */
		if (((bus_space_read_1(iot, ioh, TUL_STAT0) & INTPD) == 0)
		    && (iha_find_pend_scb(sc) == NULL))
			break;
	}
}

/*
 * iha_scsi - service any outstanding interrupts. If there are none, try to
 *            start another SCB currently in the pending queue.
 */
void
iha_scsi(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb;
	struct tcs *pTcs;
	u_int8_t stat;
	int i;

	/* service pending interrupts asap */

	stat = bus_space_read_1(iot, ioh, TUL_STAT0);
	if ((stat & INTPD) != 0) {
		sc->HCS_JSStatus0 = stat;
		sc->HCS_JSStatus1 = bus_space_read_1(iot, ioh, TUL_STAT1);
		sc->HCS_JSInt     = bus_space_read_1(iot, ioh, TUL_SISTAT);

		sc->HCS_Phase = sc->HCS_JSStatus0 & PH_MASK;

		if ((sc->HCS_JSInt & SRSTD) != 0) {
			iha_reset_scsi_bus(sc);
			return;
		}

		if ((sc->HCS_JSInt & RSELED) != 0) {
			iha_resel(sc, iot, ioh);
			return;
		}

		if ((sc->HCS_JSInt & (STIMEO | DISCD)) != 0) {
			iha_busfree(sc, iot, ioh);
			return;
		}

		if ((sc->HCS_JSInt & (SCMDN | SBSRV)) != 0) {
			iha_next_state(sc, iot, ioh);
			return;
		}

		if ((sc->HCS_JSInt & SELED) != 0)
			iha_set_ssig(iot, ioh, 0, 0);
	}

	/*
	 * There were no interrupts pending which required action elsewhere, so
	 * see if it is possible to start the selection phase on a pending SCB
	 */
	if ((pScb = iha_find_pend_scb(sc)) == NULL)
		return;

	pTcs = pScb->SCB_Tcs;

	/* program HBA's SCSI ID & target SCSI ID */
	bus_space_write_1(iot, ioh, TUL_SID,
	    (sc->sc_id << 4) | pScb->SCB_Target);

	if ((pScb->SCB_Flags & SCSI_RESET) == 0) {
		bus_space_write_1(iot, ioh, TUL_SYNCM, pTcs->TCS_JS_Period);

		if (((pTcs->TCS_Flags & FLAG_NO_NEG_WIDE) == 0)
		    ||
		    ((pTcs->TCS_Flags & FLAG_NO_NEG_SYNC) == 0))
			iha_select(sc, iot, ioh, pScb, SELATNSTOP);

		else if (pScb->SCB_TagMsg != 0)
			iha_select(sc, iot, ioh, pScb, SEL_ATN3);

		else
			iha_select(sc, iot, ioh, pScb, SEL_ATN);

	} else {
		iha_select(sc, iot, ioh, pScb, SELATNSTOP);
		pScb->SCB_NxtStat = 8;
	}

	if ((pScb->SCB_Flags & SCSI_POLL) != 0) {
		for (i = pScb->SCB_Xs->timeout; i > 0; i--) {
			if (iha_wait(sc, iot, ioh, NO_OP) == -1)
				break;
			if (iha_next_state(sc, iot, ioh) == -1)
				break;
			delay(1000); /* Only happens in boot, so it's ok */
		}

		/*
		 * Since done queue processing not done until AFTER this
		 * function returns, pScb is on the done queue, not
		 * the free queue at this point and still has valid data
		 *
		 * Conversely, xs->error has not been set yet
		 */
		if (i == 0)
			iha_timeout(pScb);

		else if ((pScb->SCB_CDB[0] == INQUIRY)
		    && (pScb->SCB_Lun == 0)
		    && (pScb->SCB_HaStat == HOST_OK)
		    && (pScb->SCB_TaStat == SCSI_OK))
			iha_print_info(sc, pScb->SCB_Target);
	}
}

/*
 * iha_data_over_run - return HOST_OK for all SCSI opcodes where BufCharsLeft
 *                     is an 'Allocation Length'. All other SCSI opcodes
 *		       get HOST_DO_DU as they SHOULD have xferred all the
 *		       data requested.
 *
 *		       The list of opcodes using 'Allocation Length' was
 * 		       found by scanning all the SCSI-3 T10 drafts. See
 *		       www.t10.org for the curious with a .pdf reader.
 */
u_int8_t
iha_data_over_run(struct iha_scb *pScb)
{
	switch (pScb->SCB_CDB[0]) {
	case 0x03: /* Request Sense                   SPC-2 */
	case 0x12: /* Inquiry                         SPC-2 */
	case 0x1a: /* Mode Sense (6 byte version)     SPC-2 */
	case 0x1c: /* Receive Diagnostic Results      SPC-2 */
	case 0x23: /* Read Format Capacities          MMC-2 */
	case 0x29: /* Read Generation                 SBC   */
	case 0x34: /* Read Position                   SSC-2 */
	case 0x37: /* Read Defect Data                SBC   */
	case 0x3c: /* Read Buffer                     SPC-2 */
	case 0x42: /* Read Sub Channel                MMC-2 */
	case 0x43: /* Read TOC/PMA/ATIP               MMC   */

	/* XXX - 2 with same opcode of 0x44? */
	case 0x44: /* Read Header/Read Density Suprt  MMC/SSC*/

	case 0x46: /* Get Configuration               MMC-2 */
	case 0x4a: /* Get Event/Status Notification   MMC-2 */
	case 0x4d: /* Log Sense                       SPC-2 */
	case 0x51: /* Read Disc Information           MMC   */
	case 0x52: /* Read Track Information          MMC   */
	case 0x59: /* Read Master CUE                 MMC   */
	case 0x5a: /* Mode Sense (10 byte version)    SPC-2 */
	case 0x5c: /* Read Buffer Capacity            MMC   */
	case 0x5e: /* Persistent Reserve In           SPC-2 */
	case 0x84: /* Receive Copy Results            SPC-2 */
	case 0xa0: /* Report LUNs                     SPC-2 */
	case 0xa3: /* Various Report requests         SBC-2/SCC-2*/
	case 0xa4: /* Report Key                      MMC-2 */
	case 0xad: /* Read DVD Structure              MMC-2 */
	case 0xb4: /* Read Element Status (Attached)  SMC   */
	case 0xb5: /* Request Volume Element Address  SMC   */
	case 0xb7: /* Read Defect Data (12 byte ver.) SBC   */
	case 0xb8: /* Read Element Status (Independ.) SMC   */
	case 0xba: /* Report Redundancy               SCC-2 */
	case 0xbd: /* Mechanism Status                MMC   */
	case 0xbe: /* Report Basic Redundancy         SCC-2 */

		return (HOST_OK);
		break;

	default:
		return (HOST_DO_DU);
		break;
	}
}

/*
 * iha_next_state - process the current SCB as requested in its
 *                  SCB_NxtStat member.
 */
int
iha_next_state(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	if (sc->HCS_ActScb == NULL)
		return (-1);

	switch (sc->HCS_ActScb->SCB_NxtStat) {
	case 1:
		if (iha_state_1(sc, iot, ioh) == 3)
			goto state_3;
		break;

	case 2:
		switch (iha_state_2(sc, iot, ioh)) {
		case 3:	 goto state_3;
		case 4:	 goto state_4;
		default: break;
		}
		break;

	case 3:
	state_3:
		if (iha_state_3(sc, iot, ioh) == 4)
			goto state_4;
		break;

	case 4:
	state_4:
		switch (iha_state_4(sc, iot, ioh)) {
		case 0:	 return (0);
		case 6:	 goto state_6;
		default: break;
		}
		break;

	case 5:
		switch (iha_state_5(sc, iot, ioh)) {
		case 4:	 goto state_4;
		case 6:	 goto state_6;
		default: break;
		}
		break;

	case 6:
	state_6:
		iha_state_6(sc, iot, ioh);
		break;

	case 8:
		iha_state_8(sc, iot, ioh);
		break;

	default:
#ifdef IHA_DEBUG_STATE
		sc_print_addr(sc->HCS_ActScb->SCB_Xs->sc_link);
		printf("[debug] -unknown state: %i-\n",
		    sc->HCS_ActScb->SCB_NxtStat);
#endif
		iha_bad_seq(sc);
		break;
	}

	return (-1);
}

/*
 * iha_state_1 - selection is complete after a SELATNSTOP. If the target
 *               has put the bus into MSG_OUT phase start wide/sync
 *               negotiation. Otherwise clear the FIFO and go to state 3,
 *	    	 which will send the SCSI CDB to the target.
 */
int
iha_state_1(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb = sc->HCS_ActScb;
	struct tcs *pTcs;
	u_int16_t flags;

	iha_mark_busy_scb(pScb);

	pTcs = pScb->SCB_Tcs;

	bus_space_write_1(iot, ioh, TUL_SCONFIG0, pTcs->TCS_SConfig0);

	/*
	 * If we are in PHASE_MSG_OUT, send
	 *     a) IDENT message (with tags if appropriate)
	 *     b) WDTR if the target is configured to negotiate wide xfers
	 *     ** OR **
	 *     c) SDTR if the target is configured to negotiate sync xfers
	 *	  but not wide ones
	 *
	 * If we are NOT, then the target is not asking for anything but
	 * the data/command, so go straight to state 3.
	 */
	if (sc->HCS_Phase == PHASE_MSG_OUT) {
		bus_space_write_1(iot, ioh, TUL_SCTRL1, (ESBUSIN | EHRSL));
		bus_space_write_1(iot, ioh, TUL_SFIFO,	pScb->SCB_Ident);

		if (pScb->SCB_TagMsg != 0) {
			bus_space_write_1(iot, ioh, TUL_SFIFO,
			    pScb->SCB_TagMsg);
			bus_space_write_1(iot, ioh, TUL_SFIFO,
			    pScb->SCB_TagId);
		}

		flags = pTcs->TCS_Flags;
		if ((flags & FLAG_NO_NEG_WIDE) == 0) {
			if (iha_msgout_wdtr(sc, iot, ioh) == -1)
				return (-1);
		} else if ((flags & FLAG_NO_NEG_SYNC) == 0) {
			if (iha_msgout_sdtr(sc, iot, ioh) == -1)
				return (-1);
		}

	} else {
		bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
		iha_set_ssig(iot, ioh, REQ | BSY | SEL | ATN, 0);
	}

	return (3);
}

/*
 * iha_state_2 - selection is complete after a SEL_ATN or SEL_ATN3. If the SCSI
 *		 CDB has already been send, go to state 4 to start the data
 *               xfer. Otherwise reset the FIFO and go to state 3, sending
 *		 the SCSI CDB.
 */
int
iha_state_2(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb = sc->HCS_ActScb;

	iha_mark_busy_scb(pScb);

	bus_space_write_1(iot, ioh, TUL_SCONFIG0, pScb->SCB_Tcs->TCS_SConfig0);

	if ((sc->HCS_JSStatus1 & CPDNE) != 0)
		return (4);

	bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);

	iha_set_ssig(iot, ioh, REQ | BSY | SEL | ATN, 0);

	return (3);
}

/*
 * iha_state_3 - send the SCSI CDB to the target, processing any status
 *		 or other messages received until that is done or
 *               abandoned.
 */
int
iha_state_3(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb = sc->HCS_ActScb;
	u_int16_t flags;

	for (;;)
		switch (sc->HCS_Phase) {
		case PHASE_CMD_OUT:
			bus_space_write_multi_1(iot, ioh, TUL_SFIFO,
			    pScb->SCB_CDB, pScb->SCB_CDBLen);
			if (iha_wait(sc, iot, ioh, XF_FIFO_OUT) == -1)
				return (-1);
			else if (sc->HCS_Phase == PHASE_CMD_OUT) {
				iha_bad_seq(sc);
				return (-1);
			} else
				return (4);

		case PHASE_MSG_IN:
			pScb->SCB_NxtStat = 3;
			if (iha_msgin(sc, iot, ioh) == -1)
				return (-1);
			break;

		case PHASE_STATUS_IN:
			if (iha_status_msg(sc, iot, ioh) == -1)
				return (-1);
			break;

		case PHASE_MSG_OUT:
			flags = pScb->SCB_Tcs->TCS_Flags;
			if ((flags & FLAG_NO_NEG_SYNC) != 0) {
				if (iha_msgout(sc, iot, ioh, MSG_NOOP) == -1)
					return (-1);
			} else if (iha_msgout_sdtr(sc, iot, ioh) == -1)
				return (-1);
			break;

		default:
#ifdef IHA_DEBUG_STATE
			sc_print_addr(pScb->SCB_Xs->sc_link);
			printf("[debug] -s3- bad phase = %d\n", sc->HCS_Phase);
#endif
			iha_bad_seq(sc);
			return (-1);
		}
}

/*
 * iha_state_4 - start a data xfer. Handle any bus state
 *               transitions until PHASE_DATA_IN/_OUT
 *               or the attempt is abandoned. If there is
 *               no data to xfer, go to state 6 and finish
 *               processing the current SCB.
 */
int
iha_state_4(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb = sc->HCS_ActScb;

	if ((pScb->SCB_Flags & FLAG_DIR) == FLAG_DIR)
		return (6); /* Both dir flags set => NO xfer was requested */

	for (;;) {
		if (pScb->SCB_BufCharsLeft == 0)
			return (6);

		switch (sc->HCS_Phase) {
		case PHASE_STATUS_IN:
			if ((pScb->SCB_Flags & FLAG_DIR) != 0)
				pScb->SCB_HaStat = iha_data_over_run(pScb);
			if ((iha_status_msg(sc, iot, ioh)) == -1)
				return (-1);
			break;

		case PHASE_MSG_IN:
			pScb->SCB_NxtStat = 4;
			if (iha_msgin(sc, iot, ioh) == -1)
				return (-1);
			break;

		case PHASE_MSG_OUT:
			if ((sc->HCS_JSStatus0 & SPERR) != 0) {
				pScb->SCB_BufCharsLeft = 0;
				pScb->SCB_HaStat = HOST_SPERR;
				if (iha_msgout(sc, iot, ioh,
					MSG_INITIATOR_DET_ERR) == -1)
					return (-1);
				else
					return (6);
			} else {
				if (iha_msgout(sc, iot, ioh, MSG_NOOP) == -1)
					return (-1);
			}
			break;

		case PHASE_DATA_IN:
			return (iha_xfer_data(pScb, iot, ioh, SCSI_DATA_IN));

		case PHASE_DATA_OUT:
			return (iha_xfer_data(pScb, iot, ioh, SCSI_DATA_OUT));

		default:
			iha_bad_seq(sc);
			return (-1);
		}
	}
}

/*
 * iha_state_5 - handle the partial or final completion of the current
 *               data xfer. If DMA is still active stop it. If there is
 *		 more data to xfer, go to state 4 and start the xfer.
 *               If not go to state 6 and finish the SCB.
 */
int
iha_state_5(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb = sc->HCS_ActScb;
	struct iha_sg_element *pSg;
	u_int32_t cnt;
	u_int16_t period;
	u_int8_t stat;
	long xcnt;  /* cannot use unsigned!! see code: if (xcnt < 0) */

	cnt = bus_space_read_4(iot, ioh, TUL_STCNT0) & TCNT;

	/*
	 * Stop any pending DMA activity and check for parity error.
	 */

	if ((bus_space_read_1(iot, ioh, TUL_DCMD) & XDIR) != 0) {
		/* Input Operation */
		if ((sc->HCS_JSStatus0 & SPERR) != 0)
			pScb->SCB_HaStat = HOST_SPERR;

		if ((bus_space_read_1(iot, ioh, TUL_ISTUS1) & XPEND) != 0) {
			bus_space_write_1(iot, ioh, TUL_DCTRL0,
			    bus_space_read_1(iot, ioh, TUL_DCTRL0) | SXSTP);
			while (bus_space_read_1(iot, ioh, TUL_ISTUS1) & XPEND)
				;
		}

	} else {
		/* Output Operation */
		if ((sc->HCS_JSStatus1 & SXCMP) == 0) {
			period = pScb->SCB_Tcs->TCS_JS_Period;
			if ((period & PERIOD_WIDE_SCSI) != 0)
				cnt += (bus_space_read_1(iot, ioh,
					    TUL_SFIFOCNT) & FIFOC) << 1;
			else
				cnt += (bus_space_read_1(iot, ioh,
					    TUL_SFIFOCNT) & FIFOC);
		}

		if ((bus_space_read_1(iot, ioh, TUL_ISTUS1) & XPEND) != 0) {
			bus_space_write_1(iot, ioh, TUL_DCMD, ABTXFR);
			do
				stat = bus_space_read_1(iot, ioh, TUL_ISTUS0);
			while ((stat & DABT) == 0);
		}

		if ((cnt == 1) && (sc->HCS_Phase == PHASE_DATA_OUT)) {
			if (iha_wait(sc, iot, ioh, XF_FIFO_OUT) == -1)
				return (-1);
			cnt = 0;

		} else if ((sc->HCS_JSStatus1 & SXCMP) == 0)
			bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
	}

	if (cnt == 0) {
		pScb->SCB_BufCharsLeft = 0;
		return (6);
	}

	/* Update active data pointer and restart the I/O at the new point */

	xcnt = pScb->SCB_BufCharsLeft - cnt;	/* xcnt == bytes xferred */
	pScb->SCB_BufCharsLeft = cnt;		/* cnt  == bytes left    */

	bus_dmamap_sync(sc->sc_dmat, pScb->SCB_SGDma,
	    0, sizeof(pScb->SCB_SGList), BUS_DMASYNC_POSTWRITE);

	if ((pScb->SCB_Flags & FLAG_SG) != 0) {
		pSg = &pScb->SCB_SGList[pScb->SCB_SGIdx];
		for (; pScb->SCB_SGIdx < pScb->SCB_SGCount; pSg++, pScb->SCB_SGIdx++) {
			xcnt -= pSg->SG_Len;
			if (xcnt < 0) {
				xcnt += pSg->SG_Len;

				pSg->SG_Addr += xcnt;
				pSg->SG_Len -= xcnt;

				bus_dmamap_sync(sc->sc_dmat, pScb->SCB_SGDma,
				    0, sizeof(pScb->SCB_SGList),
					BUS_DMASYNC_PREWRITE);

				return (4);
			}
		}
		return (6);

	}

	return (4);
}

/*
 * iha_state_6 - finish off the active scb (may require several
 *               iterations if PHASE_MSG_IN) and return -1 to indicate
 *		 the bus is free.
 */
int
iha_state_6(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	for (;;)
		switch (sc->HCS_Phase) {
		case PHASE_STATUS_IN:
			if (iha_status_msg(sc, iot, ioh) == -1)
				return (-1);
			break;

		case PHASE_MSG_IN:
			sc->HCS_ActScb->SCB_NxtStat = 6;
			if ((iha_msgin(sc, iot, ioh)) == -1)
				return (-1);
			break;

		case PHASE_MSG_OUT:
			if ((iha_msgout(sc, iot, ioh, MSG_NOOP)) == -1)
				return (-1);
			break;

		case PHASE_DATA_IN:
			if (iha_xpad_in(sc, iot, ioh) == -1)
				return (-1);
			break;

		case PHASE_DATA_OUT:
			if (iha_xpad_out(sc, iot, ioh) == -1)
				return (-1);
			break;

		default:
			iha_bad_seq(sc);
			return (-1);
		}
}

/*
 * iha_state_8 - reset the active device and all busy SCBs using it
 */
int
iha_state_8(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb;
	u_int32_t i;
	u_int8_t tar;

	if (sc->HCS_Phase == PHASE_MSG_OUT) {
		bus_space_write_1(iot, ioh, TUL_SFIFO, MSG_BUS_DEV_RESET);

		pScb = sc->HCS_ActScb;

		/* This SCB finished correctly -- resetting the device */
		iha_append_done_scb(sc, pScb, HOST_OK);

		iha_reset_tcs(pScb->SCB_Tcs, sc->HCS_SConf1);

		tar = pScb->SCB_Target;
		for (i = 0, pScb = sc->HCS_Scb; i < IHA_MAX_SCB; i++, pScb++)
			if (pScb->SCB_Target == tar)
				switch (pScb->SCB_Status) {
				case STATUS_BUSY:
					iha_append_done_scb(sc,
					    pScb, HOST_DEV_RST);
					break;

				case STATUS_SELECT:
					iha_push_pend_scb(sc, pScb);
					break;

				default:
					break;
				}

		sc->HCS_Flags |= FLAG_EXPECT_DISC;

		if (iha_wait(sc, iot, ioh, XF_FIFO_OUT) == -1)
			return (-1);
	}

	iha_bad_seq(sc);
	return (-1);
}

/*
 * iha_xfer_data - initiate the DMA xfer of the data
 */
int
iha_xfer_data(struct iha_scb *pScb, bus_space_tag_t iot, bus_space_handle_t ioh,
    int direction)
{
	u_int32_t xferaddr, xferlen;
	u_int8_t xfertype;

	if ((pScb->SCB_Flags & FLAG_DIR) != direction)
		return (6); /* wrong direction, abandon I/O */

	bus_space_write_4(iot, ioh, TUL_STCNT0, pScb->SCB_BufCharsLeft);

	if ((pScb->SCB_Flags & FLAG_SG) == 0) {
		xferaddr = pScb->SCB_DataDma->dm_segs[0].ds_addr
				+ (pScb->SCB_BufChars - pScb->SCB_BufCharsLeft);
		xferlen  = pScb->SCB_BufCharsLeft;
		xfertype = (direction == SCSI_DATA_IN) ? ST_X_IN : ST_X_OUT;

	} else {
		xferaddr = pScb->SCB_SGDma->dm_segs[0].ds_addr
				+ (pScb->SCB_SGIdx * sizeof(struct iha_sg_element));
		xferlen  = (pScb->SCB_SGCount - pScb->SCB_SGIdx)
				* sizeof(struct iha_sg_element);
		xfertype = (direction == SCSI_DATA_IN) ? ST_SG_IN : ST_SG_OUT;
	}

	bus_space_write_4(iot, ioh, TUL_DXC,  xferlen);
	bus_space_write_4(iot, ioh, TUL_DXPA, xferaddr);
	bus_space_write_1(iot, ioh, TUL_DCMD, xfertype);

	bus_space_write_1(iot, ioh, TUL_SCMD,
	    (direction == SCSI_DATA_IN) ? XF_DMA_IN : XF_DMA_OUT);

	pScb->SCB_NxtStat = 5;

	return (0);
}

int
iha_xpad_in(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb = sc->HCS_ActScb;

	if ((pScb->SCB_Flags & FLAG_DIR) != 0)
		pScb->SCB_HaStat = HOST_DO_DU;

	for (;;) {
		if ((pScb->SCB_Tcs->TCS_JS_Period & PERIOD_WIDE_SCSI) != 0)
			bus_space_write_4(iot, ioh, TUL_STCNT0, 2);
		else
			bus_space_write_4(iot, ioh, TUL_STCNT0, 1);

		switch (iha_wait(sc, iot, ioh, XF_FIFO_IN)) {
		case -1:
			return (-1);

		case PHASE_DATA_IN:
			bus_space_read_1(iot, ioh, TUL_SFIFO);
			break;

		default:
			bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
			return (6);
		}
	}
}

int
iha_xpad_out(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb = sc->HCS_ActScb;

	if ((pScb->SCB_Flags & FLAG_DIR) != 0)
		pScb->SCB_HaStat = HOST_DO_DU;

	for (;;) {
		if ((pScb->SCB_Tcs->TCS_JS_Period & PERIOD_WIDE_SCSI) != 0)
			bus_space_write_4(iot, ioh, TUL_STCNT0, 2);
		else
			bus_space_write_4(iot, ioh, TUL_STCNT0, 1);

		bus_space_write_1(iot, ioh, TUL_SFIFO, 0);

		switch (iha_wait(sc, iot, ioh, XF_FIFO_OUT)) {
		case -1:
			return (-1);

		case PHASE_DATA_OUT:
			break;

		default:
			/* Disable wide CPU to allow read 16 bits */
			bus_space_write_1(iot, ioh, TUL_SCTRL1, EHRSL);
			bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
			return (6);
		}
	}
}

int
iha_status_msg(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	struct iha_scb *pScb;
	u_int8_t msg;
	int phase;

	if ((phase = iha_wait(sc, iot, ioh, CMD_COMP)) == -1)
		return (-1);

	pScb = sc->HCS_ActScb;

	pScb->SCB_TaStat = bus_space_read_1(iot, ioh, TUL_SFIFO);

	if (phase == PHASE_MSG_OUT) {
		if ((sc->HCS_JSStatus0 & SPERR) == 0)
			bus_space_write_1(iot, ioh, TUL_SFIFO,
			    MSG_NOOP);
		else
			bus_space_write_1(iot, ioh, TUL_SFIFO,
			    MSG_PARITY_ERROR);

		return (iha_wait(sc, iot, ioh, XF_FIFO_OUT));

	} else if (phase == PHASE_MSG_IN) {
		msg = bus_space_read_1(iot, ioh, TUL_SFIFO);

		if ((sc->HCS_JSStatus0 & SPERR) != 0)
			switch (iha_wait(sc, iot, ioh, MSG_ACCEPT)) {
			case -1:
				return (-1);
			case PHASE_MSG_OUT:
				bus_space_write_1(iot, ioh, TUL_SFIFO,
				    MSG_PARITY_ERROR);
				return (iha_wait(sc, iot, ioh, XF_FIFO_OUT));
			default:
				iha_bad_seq(sc);
				return (-1);
			}

		if (msg == MSG_CMDCOMPLETE) {
			if ((pScb->SCB_TaStat
			    & (SCSI_INTERM | SCSI_BUSY)) == SCSI_INTERM) {
				iha_bad_seq(sc);
				return (-1);
			}
			sc->HCS_Flags |= FLAG_EXPECT_DONE_DISC;
			bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
			return (iha_wait(sc, iot, ioh, MSG_ACCEPT));
		}

		if ((msg == MSG_LINK_CMD_COMPLETE)
		    || (msg == MSG_LINK_CMD_COMPLETEF)) {
			if ((pScb->SCB_TaStat
			    & (SCSI_INTERM | SCSI_BUSY)) == SCSI_INTERM)
				return (iha_wait(sc, iot, ioh, MSG_ACCEPT));
		}
	}

	iha_bad_seq(sc);
	return (-1);
}

/*
 * iha_busfree - SCSI bus free detected as a result of a TIMEOUT or
 *		 DISCONNECT interrupt. Reset the tulip FIFO and
 *		 SCONFIG0 and enable hardware reselect. Move any active
 *		 SCB to HCS_DoneScb list. Return an appropriate host status
 *		 if an I/O was active.
 */
void
iha_busfree(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb;

	bus_space_write_1(iot, ioh, TUL_SCTRL0,	  RSFIFO);
	bus_space_write_1(iot, ioh, TUL_SCONFIG0, SCONFIG0DEFAULT);
	bus_space_write_1(iot, ioh, TUL_SCTRL1,	  EHRSL);

	pScb = sc->HCS_ActScb;

	if (pScb != NULL) {
		if (pScb->SCB_Status == STATUS_SELECT)
			/* selection timeout   */
			iha_append_done_scb(sc, pScb, HOST_SEL_TOUT);
		else
			/* Unexpected bus free */
			iha_append_done_scb(sc, pScb, HOST_BAD_PHAS);

	}
}

void
iha_reset_scsi_bus(struct iha_softc *sc)
{
	struct iha_scb *pScb;
	struct tcs *pTcs;
	int i, s;

	s = splbio();

	iha_reset_dma(sc->sc_iot, sc->sc_ioh);

	for (i = 0, pScb = sc->HCS_Scb; i < IHA_MAX_SCB; i++, pScb++)
		switch (pScb->SCB_Status) {
		case STATUS_BUSY:
			iha_append_done_scb(sc, pScb, HOST_SCSI_RST);
			break;

		case STATUS_SELECT:
			iha_push_pend_scb(sc, pScb);
			break;

		default:
			break;
		}

	for (i = 0, pTcs = sc->HCS_Tcs; i < IHA_MAX_TARGETS; i++, pTcs++)
		iha_reset_tcs(pTcs, sc->HCS_SConf1);

	splx(s);
}

/*
 * iha_resel - handle a detected SCSI bus reselection request.
 */
int
iha_resel(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct iha_scb *pScb;
	struct tcs *pTcs;
	u_int8_t tag, target, lun, msg, abortmsg;

	if (sc->HCS_ActScb != NULL) {
		if (sc->HCS_ActScb->SCB_Status == STATUS_SELECT)
			iha_push_pend_scb(sc, sc->HCS_ActScb);
		sc->HCS_ActScb = NULL;
	}

	target = bus_space_read_1(iot, ioh, TUL_SBID);
	lun    = bus_space_read_1(iot, ioh, TUL_SALVC) & MSG_IDENTIFY_LUNMASK;

	pTcs = &sc->HCS_Tcs[target];

	bus_space_write_1(iot, ioh, TUL_SCONFIG0, pTcs->TCS_SConfig0);
	bus_space_write_1(iot, ioh, TUL_SYNCM,	  pTcs->TCS_JS_Period);

	abortmsg = MSG_ABORT; /* until a valid tag has been obtained */

	if (pTcs->TCS_NonTagScb != NULL)
		/* There is a non-tagged I/O active on the target */
		pScb = pTcs->TCS_NonTagScb;

	else {
		/*
		 * Since there is no active non-tagged operation
		 * read the tag type, the tag itself, and find
		 * the appropriate pScb by indexing HCS_Scb with
		 * the tag.
		 */

		switch (iha_wait(sc, iot, ioh, MSG_ACCEPT)) {
		case -1:
			return (-1);
		case PHASE_MSG_IN:
			bus_space_write_4(iot, ioh, TUL_STCNT0, 1);
			if ((iha_wait(sc, iot, ioh, XF_FIFO_IN)) == -1)
				return (-1);
			break;
		default:
			goto abort;
		}

		msg = bus_space_read_1(iot, ioh, TUL_SFIFO); /* Read Tag Msg */

		if ((msg < MSG_SIMPLE_Q_TAG) || (msg > MSG_ORDERED_Q_TAG))
			goto abort;

		switch (iha_wait(sc, iot, ioh, MSG_ACCEPT)) {
		case -1:
			return (-1);
		case PHASE_MSG_IN:
			bus_space_write_4(iot, ioh, TUL_STCNT0, 1);
			if ((iha_wait(sc, iot, ioh, XF_FIFO_IN)) == -1)
				return (-1);
			break;
		default:
			goto abort;
		}

		tag  = bus_space_read_1(iot, ioh, TUL_SFIFO); /* Read Tag ID */
		pScb = &sc->HCS_Scb[tag];

		abortmsg = MSG_ABORT_TAG; /* Now that we have valdid tag! */
	}

	if ((pScb->SCB_Target != target)
	    || (pScb->SCB_Lun != lun)
	    || (pScb->SCB_Status != STATUS_BUSY)) {
abort:
		iha_msgout_abort(sc, iot, ioh, abortmsg);
		return (-1);
	}

	sc->HCS_ActScb = pScb;

	if (iha_wait(sc, iot, ioh, MSG_ACCEPT) == -1)
		return (-1);

	return(iha_next_state(sc, iot, ioh));
}

int
iha_msgin(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int16_t flags;
	u_int8_t msg;
	int phase;

	for (;;) {
		if ((bus_space_read_1(iot, ioh, TUL_SFIFOCNT) & FIFOC) > 0)
			bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);

		bus_space_write_4(iot, ioh, TUL_STCNT0, 1);

		phase = iha_wait(sc, iot, ioh, XF_FIFO_IN);
		msg   = bus_space_read_1(iot, ioh, TUL_SFIFO);

		switch (msg) {
		case MSG_DISCONNECT:
			sc->HCS_Flags |= FLAG_EXPECT_DISC;
			if (iha_wait(sc, iot, ioh, MSG_ACCEPT) != -1)
				iha_bad_seq(sc);
			phase = -1;
			break;
		case MSG_SAVEDATAPOINTER:
		case MSG_RESTOREPOINTERS:
		case MSG_NOOP:
			phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);
			break;
		case MSG_MESSAGE_REJECT:
			/* XXX - need to clear FIFO like other 'Clear ATN'?*/
			iha_set_ssig(iot, ioh, REQ | BSY | SEL | ATN, 0);
			flags = sc->HCS_ActScb->SCB_Tcs->TCS_Flags;
			if ((flags & FLAG_NO_NEG_SYNC) == 0)
				iha_set_ssig(iot, ioh, REQ | BSY | SEL, ATN);
			phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);
			break;
		case MSG_EXTENDED:
			phase = iha_msgin_extended(sc, iot, ioh);
			break;
		case MSG_IGN_WIDE_RESIDUE:
			phase = iha_msgin_ignore_wid_resid(sc, iot, ioh);
			break;
		case MSG_CMDCOMPLETE:
			sc->HCS_Flags |= FLAG_EXPECT_DONE_DISC;
			bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
			phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);
			if (phase != -1) {
				iha_bad_seq(sc);
				return (-1);
			}
			break;
		default:
#ifdef IHA_DEBUG_STATE
			printf("[debug] iha_msgin: bad msg type: %d\n", msg);
#endif
			phase = iha_msgout_reject(sc, iot, ioh);
			break;
		}

		if (phase != PHASE_MSG_IN)
			return (phase);
	}
	/* NOTREACHED */
}

int
iha_msgin_ignore_wid_resid(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	int phase;

	phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);

	if (phase == PHASE_MSG_IN) {
		phase = iha_wait(sc, iot, ioh, XF_FIFO_IN);

		if (phase != -1) {
			bus_space_write_1(iot, ioh, TUL_SFIFO, 0);
			bus_space_read_1 (iot, ioh, TUL_SFIFO);
			bus_space_read_1 (iot, ioh, TUL_SFIFO);

			phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);
		}
	}

	return (phase);
}

int
iha_msgin_extended(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	u_int16_t flags;
	int i, phase, msglen, msgcode;

	/* XXX - can we just stop reading and reject, or do we have to
	 *	 read all input, discarding the excess, and then reject
	 */
	for (i = 0; i < IHA_MAX_EXTENDED_MSG; i++) {
		phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);

		if (phase != PHASE_MSG_IN)
			return (phase);

		bus_space_write_4(iot, ioh, TUL_STCNT0, 1);

		if (iha_wait(sc, iot, ioh, XF_FIFO_IN) == -1)
			return (-1);

		sc->HCS_Msg[i] = bus_space_read_1(iot, ioh, TUL_SFIFO);

		if (sc->HCS_Msg[0] == i)
			break;
	}

	msglen	= sc->HCS_Msg[0];
	msgcode = sc->HCS_Msg[1];

	if ((msglen == MSG_EXT_SDTR_LEN) && (msgcode == MSG_EXT_SDTR)) {
		if (iha_msgin_sdtr(sc) == 0) {
			iha_sync_done(sc, iot, ioh);
			return (iha_wait(sc, iot, ioh, MSG_ACCEPT));
		}

		iha_set_ssig(iot, ioh, REQ | BSY | SEL, ATN);

		phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);
		if (phase != PHASE_MSG_OUT)
			return (phase);

		/* Clear FIFO for important message - final SYNC offer */
		bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);

		iha_sync_done(sc, iot, ioh); /* This is our final offer */

	} else if ((msglen == MSG_EXT_WDTR_LEN) && (msgcode == MSG_EXT_WDTR)) {

		flags = sc->HCS_ActScb->SCB_Tcs->TCS_Flags;

		if ((flags & FLAG_NO_WIDE) != 0)
			/* Offer 8 bit xfers only */
			sc->HCS_Msg[2] = MSG_EXT_WDTR_BUS_8_BIT;

		else if (sc->HCS_Msg[2] > MSG_EXT_WDTR_BUS_32_BIT)
			return (iha_msgout_reject(sc, iot, ioh));

		else if (sc->HCS_Msg[2] == MSG_EXT_WDTR_BUS_32_BIT)
			/* Offer 16 instead */
			sc->HCS_Msg[2] = MSG_EXT_WDTR_BUS_32_BIT;

		else {
			iha_wide_done(sc, iot, ioh);
			if ((flags & FLAG_NO_NEG_SYNC) == 0)
				iha_set_ssig(iot, ioh, REQ | BSY | SEL, ATN);
			return (iha_wait(sc, iot, ioh, MSG_ACCEPT));
		}

		iha_set_ssig(iot, ioh, REQ | BSY | SEL, ATN);

		phase = iha_wait(sc, iot, ioh, MSG_ACCEPT);
		if (phase != PHASE_MSG_OUT)
			return (phase);

	} else
		return (iha_msgout_reject(sc, iot, ioh));

	/* Send message built in sc->HCS_Msg[] */
	return (iha_msgout_extended(sc, iot, ioh));
}

/*
 * iha_msgin_sdtr - check SDTR msg in HCS_Msg. If the offer is
 *                  acceptable leave HCS_Msg as is and return 0.
 *                  If the negotiation must continue, modify HCS_Msg
 *                  as needed and return 1. Else return 0.
 */
int
iha_msgin_sdtr(struct iha_softc *sc)
{
	u_int16_t flags;
	u_int8_t default_period;
	int newoffer;

	flags = sc->HCS_ActScb->SCB_Tcs->TCS_Flags;

	default_period = iha_rate_tbl[flags & FLAG_SCSI_RATE];

	if (sc->HCS_Msg[3] == 0) /* target offered async only. Accept it. */
		return (0);

	newoffer = 0;

	if ((flags & FLAG_NO_SYNC) != 0) {
		sc->HCS_Msg[3] = 0;
		newoffer   = 1;
	}

	if (sc->HCS_Msg[3] > IHA_MAX_TARGETS-1) {
		sc->HCS_Msg[3] = IHA_MAX_TARGETS-1;
		newoffer   = 1;
	}

	if (sc->HCS_Msg[2] < default_period) {
		sc->HCS_Msg[2] = default_period;
		newoffer   = 1;
	}

	if (sc->HCS_Msg[2] >= 59) {
		sc->HCS_Msg[3] = 0;
		newoffer   = 1;
	}

	return (newoffer);
}

int
iha_msgout(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh, u_int8_t msg)
{
	bus_space_write_1(iot, ioh, TUL_SFIFO, msg);

	return (iha_wait(sc, iot, ioh, XF_FIFO_OUT));
}

void
iha_msgout_abort(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh, u_int8_t aborttype)
{
	iha_set_ssig(iot, ioh, REQ | BSY | SEL, ATN);

	switch (iha_wait(sc, iot, ioh, MSG_ACCEPT)) {
	case -1:
		break;

	case PHASE_MSG_OUT:
		sc->HCS_Flags |= FLAG_EXPECT_DISC;
		if (iha_msgout(sc, iot, ioh, aborttype) != -1)
			iha_bad_seq(sc);
		break;

	default:
		iha_bad_seq(sc);
		break;
	}
}

int
iha_msgout_reject(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	iha_set_ssig(iot, ioh, REQ | BSY | SEL, ATN);

	if (iha_wait(sc, iot, ioh, MSG_ACCEPT) == PHASE_MSG_OUT)
		return (iha_msgout(sc, iot, ioh, MSG_MESSAGE_REJECT));

	return (-1);
}

int
iha_msgout_extended(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	int phase;

	bus_space_write_1(iot, ioh, TUL_SFIFO, MSG_EXTENDED);

	bus_space_write_multi_1(iot, ioh, TUL_SFIFO,
	    sc->HCS_Msg, sc->HCS_Msg[0]+1);

	phase = iha_wait(sc, iot, ioh, XF_FIFO_OUT);

	bus_space_write_1(iot, ioh, TUL_SCTRL0, RSFIFO);
	iha_set_ssig(iot, ioh, REQ | BSY | SEL | ATN, 0);

	return (phase);
}

int
iha_msgout_wdtr(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	sc->HCS_ActScb->SCB_Tcs->TCS_Flags |= FLAG_WIDE_DONE;

	sc->HCS_Msg[0] = MSG_EXT_WDTR_LEN;
	sc->HCS_Msg[1] = MSG_EXT_WDTR;
	sc->HCS_Msg[2] = MSG_EXT_WDTR_BUS_16_BIT;

	return (iha_msgout_extended(sc, iot, ioh));
}

int
iha_msgout_sdtr(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	u_int16_t rateindex;
	u_int8_t sync_rate;

	rateindex = sc->HCS_ActScb->SCB_Tcs->TCS_Flags & FLAG_SCSI_RATE;

	sync_rate = iha_rate_tbl[rateindex];

	sc->HCS_Msg[0] = MSG_EXT_SDTR_LEN;
	sc->HCS_Msg[1] = MSG_EXT_SDTR;
	sc->HCS_Msg[2] = sync_rate;
	sc->HCS_Msg[3] = IHA_MAX_TARGETS-1; /* REQ/ACK */

	return (iha_msgout_extended(sc, iot, ioh));
}

void
iha_wide_done(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct tcs *pTcs = sc->HCS_ActScb->SCB_Tcs;

	pTcs->TCS_JS_Period = 0;

	if (sc->HCS_Msg[2] != 0)
		pTcs->TCS_JS_Period |= PERIOD_WIDE_SCSI;

	pTcs->TCS_SConfig0 &= ~ALTPD;
	pTcs->TCS_Flags	   &= ~FLAG_SYNC_DONE;
	pTcs->TCS_Flags	   |=  FLAG_WIDE_DONE;

	bus_space_write_1(iot, ioh, TUL_SCONFIG0, pTcs->TCS_SConfig0);
	bus_space_write_1(iot, ioh, TUL_SYNCM,	  pTcs->TCS_JS_Period);
}

void
iha_sync_done(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	struct tcs *pTcs = sc->HCS_ActScb->SCB_Tcs;
	int i;

	if ((pTcs->TCS_Flags & FLAG_SYNC_DONE) == 0) {
		if (sc->HCS_Msg[3] != 0) {
			pTcs->TCS_JS_Period |= sc->HCS_Msg[3];

			/* pick the highest possible rate */
			for (i = 0; i < sizeof(iha_rate_tbl); i++)
				if (iha_rate_tbl[i] >= sc->HCS_Msg[2])
					break;

			pTcs->TCS_JS_Period |= (i << 4);
			pTcs->TCS_SConfig0  |= ALTPD;
		}

		pTcs->TCS_Flags |= FLAG_SYNC_DONE;

		bus_space_write_1(iot, ioh, TUL_SCONFIG0, pTcs->TCS_SConfig0);
		bus_space_write_1(iot, ioh, TUL_SYNCM,	  pTcs->TCS_JS_Period);
	}
}

void
iha_reset_chip(struct iha_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	int i;

	/* reset tulip chip */

	bus_space_write_1(iot, ioh, TUL_SCTRL0, RSCSI);

	do
		sc->HCS_JSInt = bus_space_read_1(iot, ioh, TUL_SISTAT);
	while((sc->HCS_JSInt & SRSTD) == 0);

	iha_set_ssig(iot, ioh, 0, 0);

	/*
	 * Stall for 2 seconds, wait for target's firmware ready.
	 */
	for (i = 0; i < 2000; i++)
		DELAY (1000);

	bus_space_read_1(iot, ioh, TUL_SISTAT); /* Clear any active interrupt*/
}

void
iha_select(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh,
    struct iha_scb *pScb, u_int8_t select_type)
{
	int s;

	switch (select_type) {
	case SEL_ATN:
		bus_space_write_1(iot, ioh, TUL_SFIFO, pScb->SCB_Ident);
		bus_space_write_multi_1(iot, ioh, TUL_SFIFO,
		    pScb->SCB_CDB, pScb->SCB_CDBLen);

		pScb->SCB_NxtStat = 2;
		break;

	case SELATNSTOP:
		pScb->SCB_NxtStat = 1;
		break;

	case SEL_ATN3:
		bus_space_write_1(iot, ioh, TUL_SFIFO, pScb->SCB_Ident);
		bus_space_write_1(iot, ioh, TUL_SFIFO, pScb->SCB_TagMsg);
		bus_space_write_1(iot, ioh, TUL_SFIFO, pScb->SCB_TagId);

		bus_space_write_multi_1(iot, ioh, TUL_SFIFO, pScb->SCB_CDB,
		    pScb->SCB_CDBLen);

		pScb->SCB_NxtStat = 2;
		break;

	default:
#ifdef IHA_DEBUG_STATE
		sc_print_addr(pScb->SCB_Xs->sc_link);
		printf("[debug] iha_select() - unknown select type = 0x%02x\n",
		    select_type);
#endif
		return;
	}

	s = splbio();
	TAILQ_REMOVE(&sc->HCS_PendScb, pScb, SCB_ScbList);
	splx(s);

	pScb->SCB_Status = STATUS_SELECT;

	sc->HCS_ActScb = pScb;

	bus_space_write_1(iot, ioh, TUL_SCMD, select_type);
}

/*
 * iha_wait - wait for an interrupt to service or a SCSI bus phase change
 *            after writing the supplied command to the tulip chip. If
 *            the command is NO_OP, skip the command writing.
 */
int
iha_wait(struct iha_softc *sc, bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t cmd)
{
	if (cmd != NO_OP)
		bus_space_write_1(iot, ioh, TUL_SCMD, cmd);

	/*
	 * Have to do this here, in addition to in iha_isr, because
	 * interrupts might be turned off when we get here.
	 */
	do
		sc->HCS_JSStatus0 = bus_space_read_1(iot, ioh, TUL_STAT0);
	while ((sc->HCS_JSStatus0 & INTPD) == 0);

	sc->HCS_JSStatus1 = bus_space_read_1(iot, ioh, TUL_STAT1);
	sc->HCS_JSInt     = bus_space_read_1(iot, ioh, TUL_SISTAT);

	sc->HCS_Phase = sc->HCS_JSStatus0 & PH_MASK;

	if ((sc->HCS_JSInt & SRSTD) != 0) {
		/* SCSI bus reset interrupt */
		iha_reset_scsi_bus(sc);
		return (-1);
	}

	if ((sc->HCS_JSInt & RSELED) != 0)
		/* Reselection interrupt */
		return (iha_resel(sc, iot, ioh));

	if ((sc->HCS_JSInt & STIMEO) != 0) {
		/* selected/reselected timeout interrupt */
		iha_busfree(sc, iot, ioh);
		return (-1);
	}

	if ((sc->HCS_JSInt & DISCD) != 0) {
		/* BUS disconnection interrupt */
		if ((sc->HCS_Flags & FLAG_EXPECT_DONE_DISC) != 0) {
			bus_space_write_1(iot, ioh, TUL_SCTRL0,	  RSFIFO);
			bus_space_write_1(iot, ioh, TUL_SCONFIG0,
			    SCONFIG0DEFAULT);
			bus_space_write_1(iot, ioh, TUL_SCTRL1,	  EHRSL);
			iha_append_done_scb(sc, sc->HCS_ActScb, HOST_OK);
			sc->HCS_Flags &= ~FLAG_EXPECT_DONE_DISC;

		} else if ((sc->HCS_Flags & FLAG_EXPECT_DISC) != 0) {
			bus_space_write_1(iot, ioh, TUL_SCTRL0,	  RSFIFO);
			bus_space_write_1(iot, ioh, TUL_SCONFIG0,
			    SCONFIG0DEFAULT);
			bus_space_write_1(iot, ioh, TUL_SCTRL1,	  EHRSL);
			sc->HCS_ActScb = NULL;
			sc->HCS_Flags &= ~FLAG_EXPECT_DISC;

		} else
			iha_busfree(sc, iot, ioh);

		return (-1);
	}

	return (sc->HCS_Phase);
}

/*
 * iha_done_scb - We have a scb which has been processed by the
 *                adaptor, now we look to see how the operation went.
 */
void
iha_done_scb(struct iha_softc *sc, struct iha_scb *pScb)
{
	struct scsi_sense_data *s1, *s2;
	struct scsi_xfer *xs = pScb->SCB_Xs;

	if (xs != NULL) {
		timeout_del(&xs->stimeout);

		xs->status = pScb->SCB_TaStat;

		if ((pScb->SCB_Flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) != 0) {
			bus_dmamap_sync(sc->sc_dmat, pScb->SCB_DataDma,
				0, pScb->SCB_BufChars,
				((pScb->SCB_Flags & SCSI_DATA_IN) ?
			    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE));
			bus_dmamap_unload(sc->sc_dmat, pScb->SCB_DataDma);
		}
		if ((pScb->SCB_Flags & FLAG_SG) != 0) {
			bus_dmamap_sync(sc->sc_dmat, pScb->SCB_SGDma,
				0, sizeof(pScb->SCB_SGList),
				BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, pScb->SCB_SGDma);
		}

		switch (pScb->SCB_HaStat) {
		case HOST_OK:
			switch (pScb->SCB_TaStat) {
			case SCSI_OK:
			case SCSI_COND_MET:
			case SCSI_INTERM:
			case SCSI_INTERM_COND_MET:
				xs->resid = pScb->SCB_BufCharsLeft;
				xs->error = XS_NOERROR;
				break;

			case SCSI_RESV_CONFLICT:
			case SCSI_BUSY:
			case SCSI_QUEUE_FULL:
				xs->error = XS_BUSY;
				break;

			case SCSI_TERMINATED:
			case SCSI_ACA_ACTIVE:
			case SCSI_CHECK:
				s1 = &pScb->SCB_ScsiSenseData;
				s2 = &xs->sense;
				*s2 = *s1;

				xs->error = XS_SENSE;
				break;

			default:
				xs->error = XS_DRIVER_STUFFUP;
				break;
			}
			break;

		case HOST_SEL_TOUT:
			xs->error = XS_SELTIMEOUT;
			break;

		case HOST_SCSI_RST:
		case HOST_DEV_RST:
			xs->error = XS_RESET;
			break;

		case HOST_SPERR:
			sc_print_addr(xs->sc_link);
			printf("SCSI Parity error detected\n");
			xs->error = XS_DRIVER_STUFFUP;
			break;

		case HOST_TIMED_OUT:
			xs->error = XS_TIMEOUT;
			break;

		case HOST_DO_DU:
		case HOST_BAD_PHAS:
		default:
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		scsi_done(xs);
	}
}

void
iha_timeout(void *arg)
{
	struct iha_scb *pScb = (struct iha_scb *)arg;
	struct scsi_xfer *xs = pScb->SCB_Xs;

	if (xs != NULL) {
		sc_print_addr(xs->sc_link);
		printf("SCSI OpCode 0x%02x timed out\n", xs->cmd.opcode);
		iha_abort_xs(xs->sc_link->bus->sb_adapter_softc, xs, HOST_TIMED_OUT);
	}
}

void
iha_exec_scb(struct iha_softc *sc, struct iha_scb *pScb)
{
	struct scsi_xfer *xs = pScb->SCB_Xs;
	bus_space_handle_t ioh;
	bus_space_tag_t iot;
	int s;

	s = splbio();

	if ((pScb->SCB_Flags & SCSI_POLL) == 0)
		timeout_add_msec(&xs->stimeout, xs->timeout);

	if (((pScb->SCB_Flags & SCSI_RESET) != 0)
	    || (pScb->SCB_CDB[0] == REQUEST_SENSE))
		iha_push_pend_scb(sc, pScb);   /* Insert SCB at head of Pend */
	else
		iha_append_pend_scb(sc, pScb); /* Append SCB to tail of Pend */

	/*
	 * Run through iha_main() to ensure something is active, if
	 * only this new SCB.
	 */
	if (sc->HCS_Semaph != SEMAPH_IN_MAIN) {
		iot = sc->sc_iot;
		ioh = sc->sc_ioh;

		bus_space_write_1(iot, ioh, TUL_IMSK, MASK_ALL);
		sc->HCS_Semaph = SEMAPH_IN_MAIN;

		splx(s);
		iha_main(sc, iot, ioh);
		s = splbio();

		sc->HCS_Semaph = ~SEMAPH_IN_MAIN;
		bus_space_write_1(iot, ioh, TUL_IMSK, (MASK_ALL & ~MSCMP));
	}

	splx(s);
}


/*
 * iha_set_ssig - read the current scsi signal mask, then write a new
 *		  one which turns off/on the specified signals.
 */
void
iha_set_ssig(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t offsigs,
    u_int8_t onsigs)
{
	u_int8_t currsigs;

	currsigs = bus_space_read_1(iot, ioh, TUL_SSIGI);
	bus_space_write_1(iot, ioh, TUL_SSIGO, (currsigs & ~offsigs) | onsigs);
}

void
iha_print_info(struct iha_softc *sc, int target)
{
	u_int8_t period = sc->HCS_Tcs[target].TCS_JS_Period;
	u_int8_t config = sc->HCS_Tcs[target].TCS_SConfig0;
	int rate;

	printf("%s: target %d using %d bit ", sc->sc_dev.dv_xname, target,
		(period & PERIOD_WIDE_SCSI) ? 16 : 8);

	if ((period & PERIOD_SYOFS) == 0)
		printf("async ");
	else {
		rate = (period & PERIOD_SYXPD) >> 4;
		if ((config & ALTPD) == 0)
			rate = 100 + rate * 50;
		else
			rate =	50 + rate * 25;
		rate = 1000000000 / rate;
		printf("%d.%d MHz %d REQ/ACK offset ", rate / 1000000,
		    (rate % 1000000 + 99999) / 100000, period & PERIOD_SYOFS);
	}

	printf("xfers\n");
}


/*
 * iha_alloc_scbs - allocate and map the SCB's for the supplied iha_softc
 */
int
iha_alloc_scbs(struct iha_softc *sc)
{
	bus_dma_segment_t seg;
	int error, rseg;

	/*
	 * Allocate dma-safe memory for the SCB's
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat,
		 sizeof(struct iha_scb)*IHA_MAX_SCB,
		 NBPG, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO))
	    != 0) {
		printf("%s: unable to allocate SCBs,"
		       " error = %d\n", sc->sc_dev.dv_xname, error);
		return (error);
	}
	if ((error = bus_dmamem_map(sc->sc_dmat,
		 &seg, rseg, sizeof(struct iha_scb)*IHA_MAX_SCB,
		 (caddr_t *)&sc->HCS_Scb, BUS_DMA_NOWAIT | BUS_DMA_COHERENT))
	    != 0) {
		printf("%s: unable to map SCBs, error = %d\n",
		       sc->sc_dev.dv_xname, error);
		return (error);
	}

	return (0);
}

/*
 * iha_read_eeprom - read contents of serial EEPROM into iha_nvram pointed at
 *                                        by parameter nvram.
 */
void
iha_read_eeprom(bus_space_tag_t iot, bus_space_handle_t ioh,
    struct iha_nvram *nvram)
{
	u_int32_t chksum;
	u_int16_t *np;
	u_int8_t gctrl, addr;

	const int chksum_addr = offsetof(struct iha_nvram, NVM_CheckSum) / 2;

	/* Enable EEProm programming */
	gctrl = bus_space_read_1(iot, ioh, TUL_GCTRL0) | EEPRG;
	bus_space_write_1(iot, ioh, TUL_GCTRL0, gctrl);

	/* Read EEProm */
	np = (u_int16_t *)nvram;
	for (addr=0, chksum=0; addr < chksum_addr; addr++, np++) {
		*np = iha_se2_rd(iot, ioh, addr);
		chksum += *np;
	}

	chksum &= 0x0000ffff;
	nvram->NVM_CheckSum = iha_se2_rd(iot, ioh, chksum_addr);

	/* Disable EEProm programming */
	gctrl = bus_space_read_1(iot, ioh, TUL_GCTRL0) & ~EEPRG;
	bus_space_write_1(iot, ioh, TUL_GCTRL0, gctrl);

	if ((nvram->NVM_Signature != SIGNATURE)
	    ||
	    (nvram->NVM_CheckSum  != chksum))
		panic("iha: invalid EEPROM,  bad signature or checksum");
}

/*
 * iha_se2_rd - read & return the 16 bit value at the specified
 *		offset in the Serial E2PROM
 *
 */
u_int16_t
iha_se2_rd(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t addr)
{
	u_int16_t readWord;
	u_int8_t bit;
	int i;

	/* Send 'READ' instruction == address | READ bit */
	iha_se2_instr(iot, ioh, (addr | NVREAD));

	readWord = 0;
	for (i = 15; i >= 0; i--) {
		bus_space_write_1(iot, ioh, TUL_NVRAM, NVRCS | NVRCK);
		DELAY(5);

		bus_space_write_1(iot, ioh, TUL_NVRAM, NVRCS);
		DELAY(5);

		/* sample data after the following edge of clock     */
		bit = bus_space_read_1(iot, ioh, TUL_NVRAM) & NVRDI;
		DELAY(5);

		readWord += bit << i;
	}

	bus_space_write_1(iot, ioh, TUL_NVRAM, 0);
	DELAY(5);

	return (readWord);
}

/*
 * iha_se2_instr - write an octet to serial E2PROM one bit at a time
 */
void
iha_se2_instr(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t instr)
{
	u_int8_t b;
	int i;

	b = NVRCS | NVRDO; /* Write the start bit (== 1) */

	bus_space_write_1(iot, ioh, TUL_NVRAM, b);
	DELAY(5);
	bus_space_write_1(iot, ioh, TUL_NVRAM, b | NVRCK);
	DELAY(5);

	for (i = 0; i < 8; i++, instr <<= 1) {
		if (instr & 0x80)
			b = NVRCS | NVRDO; /* Write a 1 bit */
		else
			b = NVRCS;	   /* Write a 0 bit */

		bus_space_write_1(iot, ioh, TUL_NVRAM, b);
		DELAY(5);
		bus_space_write_1(iot, ioh, TUL_NVRAM, b | NVRCK);
		DELAY(5);
	}

	bus_space_write_1(iot, ioh, TUL_NVRAM, NVRCS);
	DELAY(5);

	return;
}

/*
 * iha_reset_tcs - reset the target control structure pointed
 *		   to by pTcs to default values. TCS_Flags
 *		   only has the negotiation done bits reset as
 *		   the other bits are fixed at initialization.
 */
void
iha_reset_tcs(struct tcs *pTcs, u_int8_t config0)
{
	pTcs->TCS_Flags	    &= ~(FLAG_SYNC_DONE | FLAG_WIDE_DONE);
	pTcs->TCS_JS_Period  = 0;
	pTcs->TCS_SConfig0   = config0;
	pTcs->TCS_TagCnt     = 0;
	pTcs->TCS_NonTagScb  = NULL;
}
