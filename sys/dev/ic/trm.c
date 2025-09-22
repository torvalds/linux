/*	$OpenBSD: trm.c,v 1.47 2024/09/04 07:54:52 mglocker Exp $
 * ------------------------------------------------------------
 *   O.S       : OpenBSD
 *   File Name : trm.c
 *   Device Driver for Tekram DC395U/UW/F,DC315/U
 *   PCI SCSI Bus Master Host Adapter
 *   (SCSI chip set used Tekram ASIC TRM-S1040)
 *
 * (C)Copyright 1995-1999 Tekram Technology Co., Ltd.
 * (C)Copyright 2001-2002 Ashley R. Martens and Kenneth R Westerback
 * ------------------------------------------------------------
 *    HISTORY:
 *
 *  REV#   DATE      NAME                  DESCRIPTION
 *  1.00   05/01/99  ERICH CHEN            First released for NetBSD 1.4.x
 *  1.01   00/00/00  MARTIN AKESSON        Port to OpenBSD 2.8
 *  1.02   09/19/01  ASHLEY MARTENS        Cleanup and formatting
 *  2.00   01/00/02  KENNETH R WESTERBACK  Rewrite of the bus and code logic
 * ------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_message.h>

#include <dev/pci/pcidevs.h>
#include <dev/ic/trm.h>

/* #define TRM_DEBUG0 */

void	trm_check_eeprom(struct trm_adapter_nvram *, bus_space_tag_t, bus_space_handle_t);
void	trm_read_all    (struct trm_adapter_nvram *, bus_space_tag_t, bus_space_handle_t);
void	trm_write_all   (struct trm_adapter_nvram *, bus_space_tag_t, bus_space_handle_t);

void	trm_set_data (bus_space_tag_t, bus_space_handle_t, u_int8_t, u_int8_t);
void	trm_write_cmd(bus_space_tag_t, bus_space_handle_t, u_int8_t, u_int8_t);

u_int8_t trm_get_data(bus_space_tag_t, bus_space_handle_t, u_int8_t);

void	trm_wait_30us(bus_space_tag_t, bus_space_handle_t);

void	*trm_srb_alloc(void *);

void	trm_DataOutPhase0(struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_DataInPhase0 (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_StatusPhase0 (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_MsgOutPhase0 (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_MsgInPhase0  (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_DataOutPhase1(struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_DataInPhase1 (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_CommandPhase1(struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_StatusPhase1 (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_MsgOutPhase1 (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_MsgInPhase1  (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
void	trm_Nop          (struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);

void	trm_SetXferParams  (struct trm_softc *, struct trm_dcb *, int);

void	trm_DataIO_transfer(struct trm_softc *, struct trm_scsi_req_q *, u_int16_t);

int	trm_StartSRB    (struct trm_softc *, struct trm_scsi_req_q *);
void	trm_srb_reinit	(struct trm_softc *, struct trm_scsi_req_q *);
void	trm_srb_free    (void *, void *);
void	trm_RewaitSRB   (struct trm_softc *, struct trm_scsi_req_q *);
void	trm_FinishSRB   (struct trm_softc *, struct trm_scsi_req_q *);
void	trm_RequestSense(struct trm_softc *, struct trm_scsi_req_q *);

void	trm_initAdapter     (struct trm_softc *);
void	trm_Disconnect      (struct trm_softc *);
void	trm_Reselect        (struct trm_softc *);
void	trm_GoingSRB_Done   (struct trm_softc *, struct trm_dcb *);
void	trm_ScsiRstDetect   (struct trm_softc *);
void	trm_ResetSCSIBus    (struct trm_softc *);
void	trm_reset           (struct trm_softc *);
void	trm_StartWaitingSRB (struct trm_softc *);
void	trm_ResetAllDevParam(struct trm_softc *);
void	trm_RecoverSRB      (struct trm_softc *);
void	trm_linkSRB         (struct trm_softc *);

void	trm_initACB(struct trm_softc *, int);

void    trm_ResetDevParam(struct trm_softc *, struct trm_dcb *, u_int8_t);

void	trm_EnableMsgOut(struct trm_softc *, u_int8_t);

void	trm_timeout(void *);

void	trm_print_info(struct trm_softc *, struct trm_dcb *);

/*
 * ------------------------------------------------------------
 *
 *          stateV = (void *) trm_SCSI_phase0[phase]
 *
 * ------------------------------------------------------------
 */
static void *trm_SCSI_phase0[8] = {
	trm_DataOutPhase0,    /* phase:0 */
	trm_DataInPhase0,     /* phase:1 */
	trm_Nop,              /* phase:2 */
	trm_StatusPhase0,     /* phase:3 */
	trm_Nop,              /* phase:4 */
	trm_Nop,              /* phase:5 */
	trm_MsgOutPhase0,     /* phase:6 */
	trm_MsgInPhase0,      /* phase:7 */
};

/*
 * ------------------------------------------------------------
 *
 *          stateV = (void *) trm_SCSI_phase1[phase]
 *
 * ------------------------------------------------------------
 */
static void *trm_SCSI_phase1[8] = {
	trm_DataOutPhase1,    /* phase:0 */
	trm_DataInPhase1,     /* phase:1 */
	trm_CommandPhase1,    /* phase:2 */
	trm_StatusPhase1,     /* phase:3 */
	trm_Nop,              /* phase:4 */
	trm_Nop,              /* phase:5 */
	trm_MsgOutPhase1,     /* phase:6 */
	trm_MsgInPhase1,      /* phase:7 */
};


struct trm_adapter_nvram trm_eepromBuf[TRM_MAX_ADAPTER_NUM];
/*
 *Fast20:  000     50ns, 20.0 Mbytes/s
 *         001     75ns, 13.3 Mbytes/s
 *         010    100ns, 10.0 Mbytes/s
 *         011    125ns,  8.0 Mbytes/s
 *         100    150ns,  6.6 Mbytes/s
 *         101    175ns,  5.7 Mbytes/s
 *         110    200ns,  5.0 Mbytes/s
 *         111    250ns,  4.0 Mbytes/s
 *
 *Fast40:  000     25ns, 40.0 Mbytes/s
 *         001     50ns, 20.0 Mbytes/s
 *         010     75ns, 13.3 Mbytes/s
 *         011    100ns, 10.0 Mbytes/s
 *         100    125ns,  8.0 Mbytes/s
 *         101    150ns,  6.6 Mbytes/s
 *         110    175ns,  5.7 Mbytes/s
 *         111    200ns,  5.0 Mbytes/s
 */

/*
 * real period:
 */
u_int8_t trm_clock_period[8] = {
	/* nanosecond divided by 4 */
	12,	/*  48 ns 20   MB/sec */
	18,	/*  72 ns 13.3 MB/sec */
	25,	/* 100 ns 10.0 MB/sec */
	31,	/* 124 ns  8.0 MB/sec */
	37,	/* 148 ns  6.6 MB/sec */
	43,	/* 172 ns  5.7 MB/sec */
	50,	/* 200 ns  5.0 MB/sec */
	62	/* 248 ns  4.0 MB/sec */
};

/*
 * ------------------------------------------------------------
 * Function : trm_srb_alloc
 * Purpose  : Get the first free SRB
 * Inputs   :
 * Return   : NULL or a free SCSI Request block
 * ------------------------------------------------------------
 */
void *
trm_srb_alloc(void *xsc)
{
	struct trm_softc *sc = xsc;
	struct trm_scsi_req_q *pSRB;

	mtx_enter(&sc->sc_srb_mtx);
	pSRB = TAILQ_FIRST(&sc->freeSRB);
	if (pSRB != NULL)
		TAILQ_REMOVE(&sc->freeSRB, pSRB, link);
	mtx_leave(&sc->sc_srb_mtx);

#ifdef TRM_DEBUG0
	printf("%s: trm_srb_alloc. pSRB = %p, next pSRB = %p\n",
	    sc->sc_device.dv_xname, pSRB, TAILQ_FIRST(&sc->freeSRB));
#endif

	return pSRB;
}

/*
 * ------------------------------------------------------------
 * Function : trm_RewaitSRB
 * Purpose  : Q back to pending Q
 * Inputs   : struct trm_dcb * -
 *            struct trm_scsi_req_q * -
 * ------------------------------------------------------------
 */
void
trm_RewaitSRB(struct trm_softc *sc, struct trm_scsi_req_q *pSRB)
{
	int intflag;

	intflag = splbio();

	if ((pSRB->SRBFlag & TRM_ON_WAITING_SRB) != 0) {
		pSRB->SRBFlag &= ~TRM_ON_WAITING_SRB;
		TAILQ_REMOVE(&sc->waitingSRB, pSRB, link);
	}

	if ((pSRB->SRBFlag & TRM_ON_GOING_SRB) != 0) {
		pSRB->SRBFlag &= ~TRM_ON_GOING_SRB;
		TAILQ_REMOVE(&sc->goingSRB, pSRB, link);
	}

	pSRB->SRBState     = TRM_READY;
	pSRB->TargetStatus = SCSI_OK;
	pSRB->AdaptStatus  = TRM_STATUS_GOOD;

	pSRB->SRBFlag |= TRM_ON_WAITING_SRB;
	TAILQ_INSERT_HEAD(&sc->waitingSRB, pSRB, link);

	splx(intflag);
}

/*
 * ------------------------------------------------------------
 * Function : trm_StartWaitingSRB
 * Purpose  : If there is no active DCB then run robin through
 *            the DCB's to find the next waiting SRB
 *            and move it to the going list.
 * Inputs   : struct trm_softc * -
 * ------------------------------------------------------------
 */
void
trm_StartWaitingSRB(struct trm_softc *sc)
{
	struct trm_scsi_req_q *pSRB, *next;
	int intflag;

	intflag = splbio();

	if ((sc->pActiveDCB != NULL) ||
	    (TAILQ_EMPTY(&sc->waitingSRB)) ||
	    (sc->sc_Flag & (RESET_DETECT | RESET_DONE | RESET_DEV)) != 0)
		goto out;

	for (pSRB = TAILQ_FIRST(&sc->waitingSRB); pSRB != NULL; pSRB = next) {
		next = TAILQ_NEXT(pSRB, link);
		if (trm_StartSRB(sc, pSRB) == 0) {
			pSRB->SRBFlag &= ~TRM_ON_WAITING_SRB;
			TAILQ_REMOVE(&sc->waitingSRB, pSRB, link);
			pSRB->SRBFlag |= TRM_ON_GOING_SRB;
			TAILQ_INSERT_TAIL(&sc->goingSRB, pSRB, link);
			break;
		}
	}

out:
	splx(intflag);
}

/*
 * ------------------------------------------------------------
 * Function : trm_scsi_cmd
 * Purpose  : enqueues a SCSI command
 * Inputs   :
 * Call By  : GENERIC SCSI driver
 * ------------------------------------------------------------
 */
void
trm_scsi_cmd(struct scsi_xfer *xs)
{
	struct trm_scsi_req_q *pSRB;
	bus_space_handle_t ioh;
	struct trm_softc *sc;
	bus_space_tag_t	iot;
	struct trm_dcb *pDCB;
	u_int8_t target, lun;
	int i, error, intflag, timeout, xferflags;

	target = xs->sc_link->target;
	lun    = xs->sc_link->lun;

	sc  = xs->sc_link->bus->sb_adapter_softc;
	ioh = sc->sc_iohandle;
	iot = sc->sc_iotag;

#ifdef TRM_DEBUG0
	if ((xs->flags & SCSI_POLL) != 0) {
 		sc_print_addr(xs->sc_link);
		printf("trm_scsi_cmd. sc = %p, xs = %p, opcode = 0x%02x\n",
		    sc, xs, lun, xs->cmd.opcode);
	}
#endif

	if (target >= TRM_MAX_TARGETS) {
 		sc_print_addr(xs->sc_link);
		printf("target >= %d\n", TRM_MAX_TARGETS);
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}
	if (lun >= TRM_MAX_LUNS) {
 		sc_print_addr(xs->sc_link);
		printf("lun >= %d\n", TRM_MAX_LUNS);
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	pDCB = sc->pDCB[target][lun];
	if (pDCB == NULL) {
		/* Removed as a result of INQUIRY proving no device present */
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
 	}

	xferflags = xs->flags;
	if (xferflags & SCSI_RESET) {
#ifdef TRM_DEBUG0
 		sc_print_addr(xs->sc_link);
		printf("trm_reset via SCSI_RESET\n");
#endif
		trm_reset(sc);
		xs->error = XS_NOERROR;
		scsi_done(xs);
		return;
	}

	pSRB = xs->io;
	trm_srb_reinit(sc, pSRB);

	xs->error  = XS_NOERROR;
	xs->status = SCSI_OK;
	xs->resid  = 0;

	intflag = splbio();

	/*
	 * BuildSRB(pSRB,pDCB);
	 */
	if (xs->datalen != 0) {
#ifdef TRM_DEBUG0
 		sc_print_addr(xs->sc_link);
		printf("xs->datalen=%x\n", (u_int32_t)&xs->datalen);
 		sc_print_addr(xs->sc_link);
		printf("sc->sc_dmatag=0x%x\n", (u_int32_t)sc->sc_dmatag);
 		sc_print_addr(xs->sc_link);
		printf("pSRB->dmamapxfer=0x%x\n", (u_int32_t)pSRB->dmamapxfer);
 		sc_print_addr(xs->sc_link);
		printf("xs->data=0x%x\n", (u_int32_t)&xs->data);
#endif
		if ((error = bus_dmamap_load(sc->sc_dmatag, pSRB->dmamapxfer,
		    xs->data, xs->datalen, NULL,
		    (xferflags & SCSI_NOSLEEP) ? BUS_DMA_NOWAIT :
		    BUS_DMA_WAITOK)) != 0) {
			sc_print_addr(xs->sc_link);
			printf("DMA transfer map unable to load, error = %d\n",
			    error);
			xs->error = XS_DRIVER_STUFFUP;
			splx(intflag);
			scsi_done(xs);
			return;
		}

		bus_dmamap_sync(sc->sc_dmatag, pSRB->dmamapxfer,
		    0, pSRB->dmamapxfer->dm_mapsize,
		    (xferflags & SCSI_DATA_IN) ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

		/*
		 * Set up the scatter gather list
		 */
		for (i = 0; i < pSRB->dmamapxfer->dm_nsegs; i++) {
			pSRB->SegmentX[i].address = pSRB->dmamapxfer->dm_segs[i].ds_addr;
			pSRB->SegmentX[i].length  = pSRB->dmamapxfer->dm_segs[i].ds_len;
		}
		pSRB->SRBTotalXferLength = xs->datalen;
		pSRB->SRBSGCount         = pSRB->dmamapxfer->dm_nsegs;
	}

	pSRB->pSRBDCB    = pDCB;
	pSRB->xs         = xs;
	pSRB->ScsiCmdLen = xs->cmdlen;

	memcpy(pSRB->CmdBlock, &xs->cmd, xs->cmdlen);

	timeout_set(&xs->stimeout, trm_timeout, pSRB);

	pSRB->SRBFlag |= TRM_ON_WAITING_SRB;
	TAILQ_INSERT_TAIL(&sc->waitingSRB, pSRB, link);
	trm_StartWaitingSRB(sc);

	if ((xferflags & SCSI_POLL) == 0) {
		timeout_add_msec(&xs->stimeout, xs->timeout);
		splx(intflag);
		return;
	}

	splx(intflag);
	for (timeout = xs->timeout; timeout > 0; timeout--) {
		intflag = splbio();
		trm_Interrupt(sc);
		splx(intflag);
		if (ISSET(xs->flags, ITSDONE))
			break;
		DELAY(1000);
	}

	if (!ISSET(xs->flags, ITSDONE) && timeout == 0)
		trm_timeout(pSRB);

	scsi_done(xs);
}

/*
 * ------------------------------------------------------------
 * Function : trm_ResetAllDevParam
 * Purpose  :
 * Inputs   : struct trm_softc *
 * ------------------------------------------------------------
 */
void
trm_ResetAllDevParam(struct trm_softc *sc)
{
	struct trm_adapter_nvram *pEEpromBuf;
	int target, quirks;

	pEEpromBuf = &trm_eepromBuf[sc->sc_AdapterUnit];

	for (target = 0; target < TRM_MAX_TARGETS; target++) {
		if (target == sc->sc_AdaptSCSIID || sc->pDCB[target][0] == NULL)
			continue;

		if ((sc->pDCB[target][0]->DCBFlag & TRM_QUIRKS_VALID) == 0)
			quirks = SDEV_NOWIDE | SDEV_NOSYNC | SDEV_NOTAGS;
		else if (sc->pDCB[target][0]->sc_link != NULL)
			quirks = sc->pDCB[target][0]->sc_link->quirks;

		trm_ResetDevParam(sc, sc->pDCB[target][0], quirks);
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_ResetDevParam
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_ResetDevParam(struct trm_softc *sc, struct trm_dcb *pDCB, u_int8_t quirks)
{
	struct trm_adapter_nvram *pEEpromBuf = &trm_eepromBuf[sc->sc_AdapterUnit];
	u_int8_t PeriodIndex;
	const int target = pDCB->target;

	pDCB->DCBFlag &= TRM_QUIRKS_VALID;
	pDCB->DCBFlag |= (TRM_WIDE_NEGO_ENABLE | TRM_SYNC_NEGO_ENABLE);

	pDCB->SyncPeriod    = 0;
	pDCB->SyncOffset    = 0;
	pDCB->MaxNegoPeriod = 0;

	pDCB->DevMode = pEEpromBuf->NvramTarget[target].NvmTarCfg0;

	pDCB->IdentifyMsg = MSG_IDENTIFY(pDCB->lun, ((pDCB->DevMode & TRM_DISCONNECT) != 0));

	if (((quirks & SDEV_NOWIDE) == 0) &&
	    (pDCB->DevMode & TRM_WIDE) &&
	    ((sc->sc_config & HCC_WIDE_CARD) != 0))
		pDCB->DCBFlag |= TRM_WIDE_NEGO_16BIT;

	if (((quirks & SDEV_NOSYNC) == 0) &&
	    ((pDCB->DevMode & TRM_SYNC) != 0)) {
		PeriodIndex   = pEEpromBuf->NvramTarget[target].NvmTarPeriod & 0x07;
		pDCB->MaxNegoPeriod = trm_clock_period[PeriodIndex];
	}

	if (((quirks & SDEV_NOTAGS) == 0) &&
	    ((pDCB->DevMode & TRM_TAG_QUEUING) != 0) &&
	    ((pDCB->DevMode & TRM_DISCONNECT) != 0))
		/* TODO XXXX: Every device(lun) gets to queue TagMaxNum commands? */
		pDCB->DCBFlag |= TRM_USE_TAG_QUEUING;

	trm_SetXferParams(sc, pDCB, 0);
}

/*
 * ------------------------------------------------------------
 * Function : trm_RecoverSRB
 * Purpose  : Moves all SRBs from Going to Waiting for all the Link DCBs
 * Inputs   : struct trm_softc * -
 * ------------------------------------------------------------
 */
void
trm_RecoverSRB(struct trm_softc *sc)
{
	struct trm_scsi_req_q *pSRB;

	/* ASSUME we are inside splbio()/splx() */

	while ((pSRB = TAILQ_FIRST(&sc->goingSRB)) != NULL) {
		pSRB->SRBFlag &= ~TRM_ON_GOING_SRB;
		TAILQ_REMOVE(&sc->goingSRB, pSRB, link);
		pSRB->SRBFlag |= TRM_ON_WAITING_SRB;
		TAILQ_INSERT_HEAD(&sc->waitingSRB, pSRB, link);
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_reset
 * Purpose  : perform a hard reset on the SCSI bus (and TRM_S1040 chip).
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_reset(struct trm_softc *sc)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	int i, intflag;

	intflag = splbio();

	bus_space_write_1(iot, ioh, TRM_S1040_DMA_INTEN,  0);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_INTEN, 0);

	trm_ResetSCSIBus(sc);
	for (i = 0; i < 500; i++)
		DELAY(1000);

	/*
	 * Enable all SCSI interrupts except EN_SCAM
	 */
	bus_space_write_1(iot, ioh,
	    TRM_S1040_SCSI_INTEN,
	    (EN_SELECT | EN_SELTIMEOUT | EN_DISCONNECT | EN_RESELECTED |
		EN_SCSIRESET | EN_BUSSERVICE | EN_CMDDONE));
	/*
	 * Enable DMA interrupt
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_DMA_INTEN, EN_SCSIINTR);
	/*
	 * Clear DMA FIFO
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_DMA_CONTROL, CLRXFIFO);
	/*
	 * Clear SCSI FIFO
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);

	trm_ResetAllDevParam(sc);
	trm_GoingSRB_Done(sc, NULL);
	sc->pActiveDCB = NULL;

	/*
	 * RESET_DETECT, RESET_DONE, RESET_DEV
	 */
	sc->sc_Flag = 0;
	trm_StartWaitingSRB(sc);

	splx(intflag);
}

/*
 * ------------------------------------------------------------
 * Function : trm_timeout
 * Purpose  : Prints a timeout message and aborts the timed out SCSI request
 * Inputs   : void * - A struct trm_scsi_req_q * structure pointer
 * ------------------------------------------------------------
 */
void
trm_timeout(void *arg1)
{
	struct trm_scsi_req_q *pSRB;
 	struct scsi_xfer *xs;
 	struct trm_softc *sc;

 	pSRB = (struct trm_scsi_req_q *)arg1;
 	xs   = pSRB->xs;

 	if (xs != NULL) {
 		sc = xs->sc_link->bus->sb_adapter_softc;
 		sc_print_addr(xs->sc_link);
 		printf("SCSI OpCode 0x%02x ", xs->cmd.opcode);
		if (pSRB->SRBFlag & TRM_AUTO_REQSENSE)
			printf("REQUEST SENSE ");
		printf("timed out\n");
		pSRB->SRBFlag |= TRM_SCSI_TIMED_OUT;
 		trm_FinishSRB(sc, pSRB);
#ifdef TRM_DEBUG0
 		sc_print_addr(xs->sc_link);
		printf("trm_reset via trm_timeout()\n");
#endif
		trm_reset(sc);
		trm_StartWaitingSRB(sc);
 	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_StartSRB
 * Purpose  : Send the commands in the SRB to the device
 * Inputs   : struct trm_softc * -
 *            struct trm_scsi_req_q * -
 * Return   : 0 - SCSI processor is unoccupied
 *            1 - SCSI processor is occupied with an SRB
 * ------------------------------------------------------------
 */
int
trm_StartSRB(struct trm_softc *sc, struct trm_scsi_req_q *pSRB)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct trm_dcb *pDCB = pSRB->pSRBDCB;
	u_int32_t tag_mask;
	u_int8_t tag_id, scsicommand;

#ifdef TRM_DEBUG0
	printf("%s: trm_StartSRB. sc = %p, pDCB = %p, pSRB = %p\n",
	    sc->sc_device.dv_xname, sc, pDCB, pSRB);
#endif
	/*
	 * If the queue is full or the SCSI processor has a pending interrupt
	 * then try again later.
	 */
	if ((pDCB->DCBFlag & TRM_QUEUE_FULL) || (bus_space_read_2(iot, ioh,
	    TRM_S1040_SCSI_STATUS) & SCSIINTERRUPT))
		return (1);

	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_HOSTID, sc->sc_AdaptSCSIID);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_TARGETID, pDCB->target);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_SYNC, pDCB->SyncPeriod);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_OFFSET, pDCB->SyncOffset);

	if ((sc->pDCB[pDCB->target][0]->sc_link != NULL) &&
	    ((sc->pDCB[pDCB->target][0]->DCBFlag & TRM_QUIRKS_VALID) == 0)) {
		sc->pDCB[pDCB->target][0]->DCBFlag |= TRM_QUIRKS_VALID;
		trm_ResetDevParam(sc, sc->pDCB[pDCB->target][0], sc->pDCB[pDCB->target][0]->sc_link->quirks);
	}

	/*
	 * Flush FIFO
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);

	sc->MsgCnt = 1;
	sc->MsgBuf[0] = pDCB->IdentifyMsg;
	if (((pSRB->xs->flags & SCSI_POLL) != 0) ||
	    (pSRB->CmdBlock[0] == INQUIRY) ||
	    (pSRB->CmdBlock[0] == REQUEST_SENSE))
		sc->MsgBuf[0] &= ~MSG_IDENTIFY_DISCFLAG;

	scsicommand = SCMD_SEL_ATN;

	if ((pDCB->DCBFlag & (TRM_WIDE_NEGO_ENABLE | TRM_SYNC_NEGO_ENABLE)) != 0) {
		scsicommand = SCMD_SEL_ATNSTOP;
		pSRB->SRBState = TRM_MSGOUT;

	} else if ((pDCB->DCBFlag & TRM_USE_TAG_QUEUING) == 0) {
		pDCB->DCBFlag |= TRM_QUEUE_FULL;

	} else if ((sc->MsgBuf[0] & MSG_IDENTIFY_DISCFLAG) != 0) {
		if (pSRB->TagNumber == TRM_NO_TAG) {
			for (tag_id=1, tag_mask=2; tag_id < 32; tag_id++, tag_mask <<= 1)
				if ((tag_mask & pDCB->TagMask) == 0) {
					pDCB->TagMask  |= tag_mask;
					pSRB->TagNumber = tag_id;
					break;
				}

			if (tag_id >= 32) {
				pDCB->DCBFlag |= TRM_QUEUE_FULL;
				sc->MsgCnt = 0;
				return 1;
			}
		}

		/* TODO XXXX: Should send ORDERED_Q_TAG if metadata (non-block) i/o!? */
		sc->MsgBuf[sc->MsgCnt++] = MSG_SIMPLE_Q_TAG;
		sc->MsgBuf[sc->MsgCnt++] = pSRB->TagNumber;

		scsicommand = SCMD_SEL_ATN3;
	}

	pSRB->SRBState = TRM_START;
	pSRB->ScsiPhase = PH_BUS_FREE; /* SCSI bus free Phase */
	sc->pActiveDCB = pDCB;
	pDCB->pActiveSRB = pSRB;

	if (sc->MsgCnt > 0) {
		bus_space_write_1(iot, ioh, TRM_S1040_SCSI_FIFO, sc->MsgBuf[0]);
		if (sc->MsgCnt > 1) {
			DELAY(30);
			bus_space_write_multi_1(iot, ioh, TRM_S1040_SCSI_FIFO, &sc->MsgBuf[1], sc->MsgCnt - 1);
		}
		sc->MsgCnt = 0;
	}

	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH | DO_HWRESELECT);
	/*
	 * SCSI command
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, scsicommand);

	return 0;
}

/*
 * ------------------------------------------------------------
 * Function : trm_Interrupt
 * Purpose  : Catch an interrupt from the adapter
 *            Process pending device interrupts.
 * Inputs   : void * - struct trm_softc * structure pointer
 * ------------------------------------------------------------
 */
int
trm_Interrupt(void *vsc)
{
	void   (*stateV)(struct trm_softc *, struct trm_scsi_req_q *, u_int8_t *);
	struct trm_scsi_req_q *pSRB;
	bus_space_handle_t ioh;
	struct trm_softc *sc = (struct trm_softc *)vsc;
	bus_space_tag_t	iot;
	u_int16_t phase;
	u_int8_t scsi_status, scsi_intstatus;

	if (sc == NULL)
		return 0;

	ioh = sc->sc_iohandle;
	iot = sc->sc_iotag;

	scsi_status = bus_space_read_2(iot, ioh, TRM_S1040_SCSI_STATUS);
	if (!(scsi_status & SCSIINTERRUPT))
		return 0;
	scsi_intstatus = bus_space_read_1(iot, ioh, TRM_S1040_SCSI_INTSTATUS);

#ifdef TRM_DEBUG0
	printf("%s: trm_interrupt - scsi_status=0x%02x, scsi_intstatus=0x%02x\n",
	    sc->sc_device.dv_xname, scsi_status, scsi_intstatus);
#endif
	if ((scsi_intstatus & (INT_SELTIMEOUT | INT_DISCONNECT)) != 0)
		trm_Disconnect(sc);

	else if ((scsi_intstatus &  INT_RESELECTED) != 0)
		trm_Reselect(sc);

	else if ((scsi_intstatus &  INT_SCSIRESET) != 0)
		trm_ScsiRstDetect(sc);

	else if ((sc->pActiveDCB != NULL) && ((scsi_intstatus & (INT_BUSSERVICE | INT_CMDDONE)) != 0)) {
		pSRB = sc->pActiveDCB->pActiveSRB;
		/*
		 * software sequential machine
		 */
		phase = (u_int16_t) pSRB->ScsiPhase;  /* phase: */
		/*
		 * 62037 or 62137
		 * call  trm_SCSI_phase0[]... "phase entry"
		 * handle every phase before start transfer
		 */
		stateV = trm_SCSI_phase0[phase];
		stateV(sc, pSRB, &scsi_status);
		/*
		 * if any exception occurred
		 * scsi_status will be modified to bus free phase
		 * new scsi_status transfer out from previous stateV
		 */
		/*
		 * phase:0,1,2,3,4,5,6,7
		 */
		pSRB->ScsiPhase = scsi_status & PHASEMASK;
		phase = (u_int16_t) scsi_status & PHASEMASK;
		/*
		 * call  trm_SCSI_phase1[]... "phase entry"
		 * handle every phase do transfer
		 */
		stateV = trm_SCSI_phase1[phase];
		stateV(sc, pSRB, &scsi_status);

	} else {
		return 0;
	}

	return 1;
}

/*
 * ------------------------------------------------------------
 * Function : trm_MsgOutPhase0
 * Purpose  : Check the state machine before sending a message out
 * Inputs   : struct trm_softc * -
 *            struct trm_scsi_req_q * -
 *            u_int8_t * - scsi status, set to PH_BUS_FREE if not ready
 * ------------------------------------------------------------
 */
void
trm_MsgOutPhase0(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	switch (pSRB->SRBState) {
	case TRM_UNEXPECT_RESEL:
	case TRM_ABORT_SENT:
		*pscsi_status = PH_BUS_FREE; /* initial phase */
		break;

	default:
		break;
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_MsgOutPhase1
 * Purpose  : Write the message out to the bus
 * Inputs   : struct trm_softc * -
 *            struct trm_scsi_req_q * -
 *            u_int8_t * - unused
 * ------------------------------------------------------------
 */
void
trm_MsgOutPhase1(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct trm_dcb *pDCB = sc->pActiveDCB;

	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);

	if ((pDCB->DCBFlag & TRM_WIDE_NEGO_ENABLE) != 0) {
		/*
		 * WIDE DATA TRANSFER REQUEST code (03h)
		 */
		pDCB->DCBFlag &= ~TRM_WIDE_NEGO_ENABLE;
		pDCB->DCBFlag |=  TRM_DOING_WIDE_NEGO;

		sc->MsgBuf[0] = pDCB->IdentifyMsg & ~MSG_IDENTIFY_DISCFLAG;
		sc->MsgBuf[1] = MSG_EXTENDED;
		sc->MsgBuf[2] = MSG_EXT_WDTR_LEN;
		sc->MsgBuf[3] = MSG_EXT_WDTR;

		if ((pDCB->DCBFlag & TRM_WIDE_NEGO_16BIT) == 0)
			sc->MsgBuf[4] = MSG_EXT_WDTR_BUS_8_BIT;
		else
			sc->MsgBuf[4] = MSG_EXT_WDTR_BUS_16_BIT;

		sc->MsgCnt = 5;

	} else if ((pDCB->DCBFlag & TRM_SYNC_NEGO_ENABLE) != 0) {

		pDCB->DCBFlag &= ~TRM_SYNC_NEGO_ENABLE;
		pDCB->DCBFlag |= TRM_DOING_SYNC_NEGO;

		sc->MsgCnt = 0;

		if ((pDCB->DCBFlag & TRM_WIDE_NEGO_DONE) == 0)
			sc->MsgBuf[sc->MsgCnt++] = pDCB->IdentifyMsg & ~MSG_IDENTIFY_DISCFLAG;

		sc->MsgBuf[sc->MsgCnt++] = MSG_EXTENDED;
		sc->MsgBuf[sc->MsgCnt++] = MSG_EXT_SDTR_LEN;
		sc->MsgBuf[sc->MsgCnt++] = MSG_EXT_SDTR;
		sc->MsgBuf[sc->MsgCnt++] = pDCB->MaxNegoPeriod;

		if (pDCB->MaxNegoPeriod > 0)
			sc->MsgBuf[sc->MsgCnt++] = TRM_MAX_SYNC_OFFSET;
		else
			sc->MsgBuf[sc->MsgCnt++] = 0;
	}

	if (sc->MsgCnt > 0) {
		bus_space_write_multi_1(iot, ioh, TRM_S1040_SCSI_FIFO, &sc->MsgBuf[0], sc->MsgCnt);
		if (sc->MsgBuf[0] == MSG_ABORT)
			pSRB->SRBState = TRM_ABORT_SENT;
		sc->MsgCnt = 0;
	}
	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/*
	 * Transfer information out
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}

/*
 * ------------------------------------------------------------
 * Function : trm_CommandPhase1
 * Purpose  : Send commands to bus
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_CommandPhase1(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;

	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_CLRATN | DO_CLRFIFO);

	bus_space_write_multi_1(iot, ioh, TRM_S1040_SCSI_FIFO, &pSRB->CmdBlock[0], pSRB->ScsiCmdLen);

	pSRB->SRBState = TRM_COMMAND;
	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/*
	 * Transfer information out
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}

/*
 * ------------------------------------------------------------
 * Function : trm_DataOutPhase0
 * Purpose  : Ready for Data Out, clear FIFO
 * Inputs   : u_int8_t * - SCSI status, used but not set
 * ------------------------------------------------------------
 */
void
trm_DataOutPhase0(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct SGentry *pseg;
	struct trm_dcb *pDCB;
	u_int32_t dLeftCounter, TempSRBXferredLength;
	u_int16_t scsi_status;
	u_int8_t TempDMAstatus, SGIndexTemp;

	dLeftCounter = 0;

	pDCB = pSRB->pSRBDCB;
	scsi_status = *pscsi_status;

	if (pSRB->SRBState != TRM_XFERPAD) {
		if ((scsi_status & PARITYERROR) != 0)
			pSRB->SRBFlag |= TRM_PARITY_ERROR;
		if ((scsi_status & SCSIXFERDONE) == 0) {
			/*
			 * when data transfer from DMA FIFO to SCSI FIFO
			 * if there was some data left in SCSI FIFO
			 */
			dLeftCounter = (u_int32_t)(bus_space_read_1(
			    iot, ioh, TRM_S1040_SCSI_FIFOCNT) & 0x1F);
			if (pDCB->SyncPeriod & WIDE_SYNC) {
				/*
				 * if WIDE scsi SCSI FIFOCNT unit is word
				 * so need to * 2
				 */
				dLeftCounter <<= 1;
			}
		}
		/*
		 * calculate all the residue data that not yet transferred
		 * SCSI transfer counter + left in SCSI FIFO data
		 *
		 * .....TRM_S1040_SCSI_COUNTER (24bits)
		 * The counter always decrement by one for every SCSI byte
		 * transfer.
		 * .....TRM_S1040_SCSI_FIFOCNT ( 5bits)
		 * The counter is SCSI FIFO offset counter
		 */
		dLeftCounter += bus_space_read_4(iot, ioh,
		    TRM_S1040_SCSI_COUNTER);
		if (dLeftCounter == 1) {
			dLeftCounter = 0;
			bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL,
			    DO_CLRFIFO);
		}
		if (dLeftCounter == 0 ||
		    (scsi_status & SCSIXFERCNT_2_ZERO) != 0) {
			TempDMAstatus = bus_space_read_1(iot,
			    ioh, TRM_S1040_DMA_STATUS);
			while ((TempDMAstatus & DMAXFERCOMP) == 0) {
				TempDMAstatus = bus_space_read_1(iot,
				    ioh, TRM_S1040_DMA_STATUS);
			}
			pSRB->SRBTotalXferLength = 0;
		} else {
			/*
			 * Update SG list
			 */
			/*
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			if (pSRB->SRBTotalXferLength != dLeftCounter) {
				/*
				 * data that had transferred length
				 */
				TempSRBXferredLength = pSRB->SRBTotalXferLength
				    - dLeftCounter;
				/*
				 * next time to be transferred length
				 */
				pSRB->SRBTotalXferLength = dLeftCounter;
				/*
				 * parsing from last time disconnect SRBSGIndex
				 */
				pseg = &pSRB->SegmentX[pSRB->SRBSGIndex];
				for (SGIndexTemp = pSRB->SRBSGIndex;
				    SGIndexTemp < pSRB->SRBSGCount;
				    SGIndexTemp++) {
					/*
					 * find last time which SG transfer be
					 * disconnect
					 */
					if (TempSRBXferredLength >= pseg->length)
						TempSRBXferredLength -= pseg->length;
					else {
						/*
						 * update last time disconnected
						 * SG list
						 */
						/*
						 * residue data length
						 */
						pseg->length -=
						    TempSRBXferredLength;
						/*
						 * residue data pointer
						 */
						pseg->address +=
						    TempSRBXferredLength;
						pSRB->SRBSGIndex = SGIndexTemp;
						break;
					}
					pseg++;
				}
			}
		}
	}
	bus_space_write_1(iot, ioh, TRM_S1040_DMA_CONTROL, STOPDMAXFER);
}

/*
 * ------------------------------------------------------------
 * Function : trm_DataOutPhase1
 * Purpose  : Transfers data out, calls trm_DataIO_transfer
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_DataOutPhase1(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	trm_DataIO_transfer(sc, pSRB, XFERDATAOUT);
}

/*
 * ------------------------------------------------------------
 * Function : trm_DataInPhase0
 * Purpose  : Prepare for reading data in from bus
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_DataInPhase0(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct SGentry *pseg;
	u_int32_t TempSRBXferredLength, dLeftCounter;
	u_int16_t scsi_status;
	u_int8_t SGIndexTemp;

	dLeftCounter = 0;

	scsi_status = *pscsi_status;
	if (pSRB->SRBState != TRM_XFERPAD) {
		if ((scsi_status & PARITYERROR) != 0)
			pSRB->SRBFlag |= TRM_PARITY_ERROR;
		dLeftCounter += bus_space_read_4(iot, ioh,
		    TRM_S1040_SCSI_COUNTER);
		if (dLeftCounter == 0 ||
		    (scsi_status & SCSIXFERCNT_2_ZERO) != 0) {
			while ((bus_space_read_1(iot, ioh, TRM_S1040_DMA_STATUS) & DMAXFERCOMP) == 0)
				;
			pSRB->SRBTotalXferLength = 0;
		} else {
			/*
			 * phase changed
			 *
			 * parsing the case:
			 * when a transfer not yet complete
			 * but be disconnected by upper layer
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			if (pSRB->SRBTotalXferLength != dLeftCounter) {
				/*
				 * data that had transferred length
				 */
				TempSRBXferredLength = pSRB->SRBTotalXferLength
				    - dLeftCounter;
				/*
				 * next time to be transferred length
				 */
				pSRB->SRBTotalXferLength = dLeftCounter;
				/*
				 * parsing from last time disconnect SRBSGIndex
				 */
				pseg = &pSRB->SegmentX[pSRB->SRBSGIndex];
				for (SGIndexTemp = pSRB->SRBSGIndex;
				    SGIndexTemp < pSRB->SRBSGCount;
				    SGIndexTemp++) {
					/*
					 * find last time which SG transfer be
					 * disconnect
					 */
					if (TempSRBXferredLength >=
					    pseg->length) {
						TempSRBXferredLength -= pseg->length;
					} else {
						/*
						 * update last time disconnected
						 * SG list
						 *
						 * residue data length
						 */
						pseg->length -= TempSRBXferredLength;
						/*
						 * residue data pointer
						 */
						pseg->address += TempSRBXferredLength;
						pSRB->SRBSGIndex = SGIndexTemp;
						break;
					}
					pseg++;
				}
			}
		}
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_DataInPhase1
 * Purpose  : Transfer data in from bus, calls trm_DataIO_transfer
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_DataInPhase1(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	trm_DataIO_transfer(sc, pSRB, XFERDATAIN);
}

/*
 * ------------------------------------------------------------
 * Function : trm_DataIO_transfer
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_DataIO_transfer(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int16_t ioDir)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct trm_dcb *pDCB = pSRB->pSRBDCB;
	u_int8_t bval;

	if (pSRB->SRBSGIndex < pSRB->SRBSGCount) {
		if (pSRB->SRBTotalXferLength != 0) {
			/*
			 * load what physical address of Scatter/Gather list
			 * table want to be transfer
			 */
			pSRB->SRBState = TRM_DATA_XFER;
			bus_space_write_4(iot, ioh, TRM_S1040_DMA_XHIGHADDR, 0);
			bus_space_write_4(iot, ioh,
			    TRM_S1040_DMA_XLOWADDR, (pSRB->SRBSGPhyAddr +
			    ((u_int32_t)pSRB->SRBSGIndex << 3)));
			/*
			 * load how many bytes in the Scatter/Gather list table
			 */
			bus_space_write_4(iot, ioh, TRM_S1040_DMA_XCNT,
			    ((u_int32_t)(pSRB->SRBSGCount -
			    pSRB->SRBSGIndex) << 3));
			/*
			 * load total transfer length (24bits,
			 * pSRB->SRBTotalXferLength) max value 16Mbyte
			 */
			bus_space_write_4(iot, ioh,
			    TRM_S1040_SCSI_COUNTER, pSRB->SRBTotalXferLength);
			/*
			 * Start DMA transfer
			 */
			bus_space_write_2(iot,ioh,TRM_S1040_DMA_COMMAND, ioDir);
			/* bus_space_write_2(iot, ioh,
			    TRM_S1040_DMA_CONTROL, STARTDMAXFER);*/
			/*
			 * Set the transfer bus and direction
			 */
			bval = ioDir == XFERDATAOUT ? SCMD_DMA_OUT :SCMD_DMA_IN;
		} else {
			/*
			 * xfer pad
			 */
			if (pSRB->SRBSGCount)
				pSRB->AdaptStatus = TRM_OVER_UNDER_RUN;

			if (pDCB->SyncPeriod & WIDE_SYNC) {
				bus_space_write_4(iot, ioh,
				    TRM_S1040_SCSI_COUNTER, 2);
			} else {
				bus_space_write_4(iot, ioh,
				    TRM_S1040_SCSI_COUNTER, 1);
			}

			if (ioDir == XFERDATAOUT) {
				bus_space_write_2(iot,
				    ioh, TRM_S1040_SCSI_FIFO, 0);
			} else {
				bus_space_read_2(iot,
				    ioh, TRM_S1040_SCSI_FIFO);
			}
			pSRB->SRBState = TRM_XFERPAD;
			/*
			 * Set the transfer bus and direction
			 */
			bval = ioDir == XFERDATAOUT ? SCMD_FIFO_OUT : SCMD_FIFO_IN;
		}
		/*
		 * it's important for atn stop
		 */
		bus_space_write_2(iot,ioh,TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
		/*
		 * Tell the bus to do the transfer
		 */
		bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, bval);
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_StatusPhase0
 * Purpose  : Update Target Status with data from SCSI FIFO
 * Inputs   : u_int8_t * - Set to PH_BUS_FREE
 * ------------------------------------------------------------
 */
void
trm_StatusPhase0(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;

	pSRB->TargetStatus = bus_space_read_1(iot, ioh, TRM_S1040_SCSI_FIFO);

	pSRB->SRBState = TRM_COMPLETED;
	/*
	 * initial phase
	 */
	*pscsi_status = PH_BUS_FREE;
	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/*
	 * Tell bus that the message was accepted
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}

/*
 * ------------------------------------------------------------
 * Function : trm_StatusPhase1
 * Purpose  : Clear FIFO of DMA and SCSI
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_StatusPhase1(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;

	if ((bus_space_read_2(iot, ioh, TRM_S1040_DMA_COMMAND) & 0x0001) != 0) {
		if ((bus_space_read_1(iot, ioh, TRM_S1040_SCSI_FIFOCNT) & 0x40)
		    == 0) {
			bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL,
			    DO_CLRFIFO);
		}
		if ((bus_space_read_2(iot, ioh,
		    TRM_S1040_DMA_FIFOCNT) & 0x8000) == 0) {
			bus_space_write_1(iot, ioh,
			    TRM_S1040_DMA_CONTROL, CLRXFIFO);
		}
	} else {
		if ((bus_space_read_2(iot, ioh,
		    TRM_S1040_DMA_FIFOCNT) & 0x8000) == 0) {
			bus_space_write_1(iot, ioh,
			    TRM_S1040_DMA_CONTROL, CLRXFIFO);
		}
		if ((bus_space_read_1(iot, ioh,
		    TRM_S1040_SCSI_FIFOCNT) & 0x40) == 0) {
			bus_space_write_2(iot, ioh,
			    TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);
		}
	}
	pSRB->SRBState = TRM_STATUS;
	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/*
	 * Tell the bus that the command is complete
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, SCMD_COMP);
}

/*
 * ------------------------------------------------------------
 * Function : trm_MsgInPhase0
 * Purpose  :
 * Inputs   :
 *
 * extended message codes:
 *   code        description
 *   ----        -----------
 *    02h        Reserved
 *    00h        MODIFY DATA POINTER
 *    01h        SYNCHRONOUS DATA TRANSFER REQUEST
 *    03h        WIDE DATA TRANSFER REQUEST
 * 04h - 7Fh     Reserved
 * 80h - FFh     Vendor specific
 *
 * ------------------------------------------------------------
 */
void
trm_MsgInPhase0(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct trm_dcb *pDCB;
	u_int8_t message_in_code, bIndex, message_in_tag_id;

	pDCB = sc->pActiveDCB;

	message_in_code = bus_space_read_1(iot, ioh, TRM_S1040_SCSI_FIFO);

	if (pSRB->SRBState != TRM_EXTEND_MSGIN) {
		switch (message_in_code) {
		case MSG_DISCONNECT:
			pSRB->SRBState = TRM_DISCONNECTED;
			break;

		case MSG_EXTENDED:
		case MSG_SIMPLE_Q_TAG:
		case MSG_HEAD_OF_Q_TAG:
		case MSG_ORDERED_Q_TAG:
			pSRB->SRBState = TRM_EXTEND_MSGIN;
			/*
			 * extended message      (01h)
			 */
			bzero(&sc->MsgBuf[0], sizeof(sc->MsgBuf));
			sc->MsgBuf[0] = message_in_code;
			sc->MsgCnt    = 1;
			/*
			 * extended message length (n)
			 */
			break;

		case MSG_MESSAGE_REJECT:
			/*
			 * Reject message
			 */
			if ((pDCB->DCBFlag & TRM_DOING_WIDE_NEGO) != 0) {
				/*
				 * do wide nego reject
				 */
				pDCB = pSRB->pSRBDCB;

				pDCB->DCBFlag &= ~TRM_DOING_WIDE_NEGO;
				pDCB->DCBFlag |= TRM_WIDE_NEGO_DONE;

				if ((pDCB->DCBFlag & TRM_SYNC_NEGO_ENABLE) != 0) {
					/*
					 * Set ATN, in case ATN was clear
					 */
					pSRB->SRBState = TRM_MSGOUT;
					bus_space_write_2(iot, ioh,
					    TRM_S1040_SCSI_CONTROL, DO_SETATN);
				} else {
					/*
					 * Clear ATN
					 */
					bus_space_write_2(iot, ioh,
					    TRM_S1040_SCSI_CONTROL, DO_CLRATN);
				}

			} else if ((pDCB->DCBFlag & TRM_DOING_SYNC_NEGO) != 0) {
				/*
				 * do sync nego reject
				 */
				pDCB = pSRB->pSRBDCB;

				pDCB->DCBFlag &= ~TRM_DOING_SYNC_NEGO;

				pDCB->SyncPeriod = 0;
				pDCB->SyncOffset = 0;

				bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_CLRATN);
				goto  re_prog;
			}
			break;

		case MSG_IGN_WIDE_RESIDUE:
			bus_space_write_4(iot, ioh, TRM_S1040_SCSI_COUNTER, 1);
			bus_space_read_1(iot, ioh, TRM_S1040_SCSI_FIFO);
			break;

		default:
			break;
		}

	} else {

		/*
		 * We are collecting an extended message. Save the latest byte and then
		 * check to see if the message is complete. If so, process it.
		 */
		sc->MsgBuf[sc->MsgCnt++] = message_in_code;
#ifdef TRM_DEBUG0
		printf("%s: sc->MsgBuf = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		    sc->sc_device.dv_xname,
		    sc->MsgBuf[0], sc->MsgBuf[1], sc->MsgBuf[2], sc->MsgBuf[3], sc->MsgBuf[4], sc->MsgBuf[5] );
#endif
		switch (sc->MsgBuf[0]) {
		case MSG_SIMPLE_Q_TAG:
		case MSG_HEAD_OF_Q_TAG:
		case MSG_ORDERED_Q_TAG:
			if (sc->MsgCnt == 2) {
				pSRB->SRBState = TRM_FREE;
				message_in_tag_id = sc->MsgBuf[1];
				sc->MsgCnt = 0;
				TAILQ_FOREACH(pSRB, &sc->goingSRB, link) {
					if ((pSRB->pSRBDCB == pDCB) && (pSRB->TagNumber == message_in_tag_id))
						break;
				}
				if ((pSRB != NULL) && (pSRB->SRBState == TRM_DISCONNECTED)) {
					pDCB->pActiveSRB = pSRB;
					pSRB->SRBState = TRM_DATA_XFER;
				} else {
#ifdef TRM_DEBUG0
					printf("%s: TRM_UNEXPECT_RESEL!\n",
					    sc->sc_device.dv_xname);
#endif
					pSRB = &sc->SRB[0];
					trm_srb_reinit(sc, pSRB);
					pSRB->SRBState = TRM_UNEXPECT_RESEL;
					pDCB->pActiveSRB = pSRB;
					trm_EnableMsgOut(sc, MSG_ABORT_TAG);
				}
			}
			break;

		case  MSG_EXTENDED:
			/* TODO XXXX: Correctly handling target initiated negotiations? */
			if ((sc->MsgBuf[2] == MSG_EXT_WDTR) && (sc->MsgCnt == 4)) {
				/*
				 * ======================================
				 * WIDE DATA TRANSFER REQUEST
				 * ======================================
				 * byte 0 :  Extended message (01h)
				 * byte 1 :  Extended message length (02h)
				 * byte 2 :  WIDE DATA TRANSFER code (03h)
				 * byte 3 :  Transfer width exponent
				 */

				pSRB->SRBState  = TRM_FREE;
				pDCB->DCBFlag  &= ~(TRM_WIDE_NEGO_ENABLE | TRM_DOING_WIDE_NEGO);

				if (sc->MsgBuf[1] != MSG_EXT_WDTR_LEN)
					goto reject_offer;

				switch (sc->MsgBuf[3]) {
				case MSG_EXT_WDTR_BUS_32_BIT:
					if ((pDCB->DCBFlag & TRM_WIDE_NEGO_16BIT) == 0)
						sc->MsgBuf[3] = MSG_EXT_WDTR_BUS_8_BIT;
					else
						sc->MsgBuf[3] = MSG_EXT_WDTR_BUS_16_BIT;
					break;

				case MSG_EXT_WDTR_BUS_16_BIT:
					if ((pDCB->DCBFlag & TRM_WIDE_NEGO_16BIT) == 0) {
						sc->MsgBuf[3] = MSG_EXT_WDTR_BUS_8_BIT;
						break;
					}
					pDCB->SyncPeriod |= WIDE_SYNC;
					/* FALL THROUGH == ACCEPT OFFER */

				case MSG_EXT_WDTR_BUS_8_BIT:
					pSRB->SRBState  =  TRM_MSGOUT;
					pDCB->DCBFlag  |= (TRM_SYNC_NEGO_ENABLE | TRM_WIDE_NEGO_DONE);

					if (pDCB->MaxNegoPeriod == 0) {
						pDCB->SyncPeriod = 0;
						pDCB->SyncOffset = 0;
						goto re_prog;
					}
					break;

				default:
					pDCB->DCBFlag &= ~TRM_WIDE_NEGO_ENABLE;
					pDCB->DCBFlag |= TRM_WIDE_NEGO_DONE;
reject_offer:
					sc->MsgCnt    = 1;
					sc->MsgBuf[0] = MSG_MESSAGE_REJECT;
					break;
				}

				/* Echo accepted offer, or send revised offer */
				bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_SETATN);

			} else if ((sc->MsgBuf[2] == MSG_EXT_SDTR) && (sc->MsgCnt == 5)) {
				/*
				 * =================================
				 * SYNCHRONOUS DATA TRANSFER REQUEST
				 * =================================
				 * byte 0 :  Extended message (01h)
				 * byte 1 :  Extended message length (03)
				 * byte 2 :  SYNCHRONOUS DATA TRANSFER code (01h)
				 * byte 3 :  Transfer period factor
				 * byte 4 :  REQ/ACK offset
				 */

				pSRB->SRBState  = TRM_FREE;
				pDCB->DCBFlag  &= ~(TRM_SYNC_NEGO_ENABLE | TRM_DOING_SYNC_NEGO);

				if (sc->MsgBuf[1] != MSG_EXT_SDTR_LEN)
					goto reject_offer;

				if ((sc->MsgBuf[3] == 0) || (sc->MsgBuf[4] == 0)) {
					/*
					 * Asynchronous transfers
					 */
					pDCB->SyncPeriod  = 0;
					pDCB->SyncOffset  = 0;

				} else {
					/*
					 * Synchronous transfers
					 */
					/*
					 * REQ/ACK offset
					 */
					pDCB->SyncOffset = sc->MsgBuf[4];

					for (bIndex = 0; bIndex < 7; bIndex++)
						if (sc->MsgBuf[3] <= trm_clock_period[bIndex])
							break;

					pDCB->SyncPeriod |= (bIndex | ALT_SYNC);
				}

re_prog:			/*
				 *   program SCSI control register
				 */
				bus_space_write_1(iot, ioh, TRM_S1040_SCSI_SYNC, pDCB->SyncPeriod);
				bus_space_write_1(iot, ioh, TRM_S1040_SCSI_OFFSET, pDCB->SyncOffset);

				trm_SetXferParams(sc, pDCB, (pDCB->DCBFlag & TRM_QUIRKS_VALID));
			}
			break;

		default:
			break;
		}
	}

	/*
	 * initial phase
	 */
	*pscsi_status = PH_BUS_FREE;
	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/*
	 * Tell bus that the message was accepted
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}

/*
 * ------------------------------------------------------------
 * Function : trm_MsgInPhase1
 * Purpose  : Clear the FIFO
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_MsgInPhase1(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;

	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);
	bus_space_write_4(iot, ioh, TRM_S1040_SCSI_COUNTER, 1);

	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/*
	 * SCSI command
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_IN);
}

/*
 * ------------------------------------------------------------
 * Function : trm_Nop
 * Purpose  : EMPTY
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_Nop(struct trm_softc *sc, struct trm_scsi_req_q *pSRB, u_int8_t *pscsi_status)
{
}

/*
 * ------------------------------------------------------------
 * Function : trm_SetXferParams
 * Purpose  : Set the Sync period, offset and mode for each device that has
 *            the same target as the given one (struct trm_dcb *)
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_SetXferParams(struct trm_softc *sc, struct trm_dcb *pDCB, int print_info)
{
	struct trm_dcb *pDCBTemp;
	int lun, target;

	/*
	 * set all lun device's period, offset
	 */
#ifdef TRM_DEBUG0
	printf("%s: trm_SetXferParams\n", sc->sc_device.dv_xname);
#endif

	target = pDCB->target;
	for(lun = 0; lun < TRM_MAX_LUNS; lun++) {
		pDCBTemp = sc->pDCB[target][lun];
		if (pDCBTemp != NULL) {
			pDCBTemp->DevMode       = pDCB->DevMode;
			pDCBTemp->MaxNegoPeriod = pDCB->MaxNegoPeriod;
			pDCBTemp->SyncPeriod    = pDCB->SyncPeriod;
			pDCBTemp->SyncOffset    = pDCB->SyncOffset;
			pDCBTemp->DCBFlag       = pDCB->DCBFlag;
		}
	}

	if (print_info)
		trm_print_info(sc, pDCB);
}

/*
 * ------------------------------------------------------------
 * Function : trm_Disconnect
 * Purpose  :
 * Inputs   :
 *
 *    ---SCSI bus phase
 *     PH_DATA_OUT          0x00     Data out phase
 *     PH_DATA_IN           0x01     Data in phase
 *     PH_COMMAND           0x02     Command phase
 *     PH_STATUS            0x03     Status phase
 *     PH_BUS_FREE          0x04     Invalid phase used as bus free
 *     PH_BUS_FREE          0x05     Invalid phase used as bus free
 *     PH_MSG_OUT           0x06     Message out phase
 *     PH_MSG_IN            0x07     Message in phase
 * ------------------------------------------------------------
 */
void
trm_Disconnect(struct trm_softc *sc)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	struct trm_scsi_req_q *pSRB;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct trm_dcb *pDCB;
	int j;

#ifdef TRM_DEBUG0
	printf("%s: trm_Disconnect\n", sc->sc_device.dv_xname);
#endif

	pDCB = sc->pActiveDCB;
	if (pDCB == NULL) {
		/* TODO: Why use a loop? Why not use DELAY(400)? */
		for(j = 400; j > 0; --j)
			DELAY(1); /* 1 msec */
		bus_space_write_2(iot, ioh,
		    TRM_S1040_SCSI_CONTROL, (DO_CLRFIFO | DO_HWRESELECT));
		return;
	}

	pSRB = pDCB->pActiveSRB;
	sc->pActiveDCB = NULL;
	pSRB->ScsiPhase = PH_BUS_FREE; /* SCSI bus free Phase */
	bus_space_write_2(iot, ioh,
	    TRM_S1040_SCSI_CONTROL, (DO_CLRFIFO | DO_HWRESELECT));
	DELAY(100);

	switch (pSRB->SRBState) {
	case TRM_UNEXPECT_RESEL:
		pSRB->SRBState = TRM_FREE;
		break;

	case TRM_ABORT_SENT:
		trm_GoingSRB_Done(sc, pDCB);
		break;

	case TRM_START:
	case TRM_MSGOUT:
		/*
		 * Selection time out
		 */
		/* If not polling just keep trying until xs->stimeout expires */
		if ((pSRB->xs->flags & SCSI_POLL) == 0) {
			trm_RewaitSRB(sc, pSRB);
		} else {
			pSRB->TargetStatus = TRM_SCSI_SELECT_TIMEOUT;
			goto  disc1;
		}
		break;

	case TRM_COMPLETED:
disc1:
		/*
		 * TRM_COMPLETED - remove id from mask of active tags
		 */
		pDCB->pActiveSRB = NULL;
		trm_FinishSRB(sc, pSRB);
		break;

	default:
		break;
	}

	trm_StartWaitingSRB(sc);
}

/*
 * ------------------------------------------------------------
 * Function : trm_Reselect
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_Reselect(struct trm_softc *sc)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct trm_scsi_req_q *pSRB;
	struct trm_dcb *pDCB;
	u_int16_t RselTarLunId;
	u_int8_t target, lun;

#ifdef TRM_DEBUG0
	printf("%s: trm_Reselect\n", sc->sc_device.dv_xname);
#endif

	pDCB = sc->pActiveDCB;
	if (pDCB != NULL) {
		/*
		 * Arbitration lost but Reselection win
		 */
		pSRB = pDCB->pActiveSRB;
		trm_RewaitSRB(sc, pSRB);
	}

	/*
	 * Read Reselected Target Id and LUN
	 */
	RselTarLunId = bus_space_read_2(iot, ioh, TRM_S1040_SCSI_TARGETID) & 0x1FFF;
	/* TODO XXXX: Make endian independent! */
	target = RselTarLunId & 0xff;
	lun    = (RselTarLunId >> 8) & 0xff;

#ifdef TRM_DEBUG0
	printf("%s: reselect - target = %d, lun = %d\n",
	    sc->sc_device.dv_xname, target, lun);
#endif

	if ((target < TRM_MAX_TARGETS) && (lun < TRM_MAX_LUNS))
		pDCB = sc->pDCB[target][lun];
	else
		pDCB = NULL;

	if (pDCB == NULL)
		printf("%s: reselect - target = %d, lun = %d not found\n",
		    sc->sc_device.dv_xname, target, lun);

	sc->pActiveDCB = pDCB;

	/* TODO XXXX: This will crash if pDCB is ever NULL */
	if ((pDCB->DCBFlag & TRM_USE_TAG_QUEUING) != 0) {
		pSRB = &sc->SRB[0];
		pDCB->pActiveSRB = pSRB;
	} else {
		pSRB = pDCB->pActiveSRB;
		if (pSRB == NULL || (pSRB->SRBState != TRM_DISCONNECTED)) {
			/*
			 * abort command
			 */
			pSRB = &sc->SRB[0];
			pSRB->SRBState = TRM_UNEXPECT_RESEL;
			pDCB->pActiveSRB = pSRB;
			trm_EnableMsgOut(sc, MSG_ABORT);
		} else
			pSRB->SRBState = TRM_DATA_XFER;
	}
	pSRB->ScsiPhase = PH_BUS_FREE; /* SCSI bus free Phase */

	/*
	 * Program HA ID, target ID, period and offset
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_TARGETID, target);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_HOSTID, sc->sc_AdaptSCSIID);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_SYNC, pDCB->SyncPeriod);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_OFFSET, pDCB->SyncOffset);

	/*
	 * it's important for atn stop
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	DELAY(30);

	/*
	 * SCSI command
	 * to rls the /ACK signal
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}

/*
 * ------------------------------------------------------------
 * Function : trm_FinishSRB
 * Purpose  : Complete execution of a SCSI command
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_FinishSRB(struct trm_softc *sc, struct trm_scsi_req_q *pSRB)
{
	struct scsi_inquiry_data *ptr;
	struct scsi_sense_data *s1, *s2;
	struct scsi_xfer *xs = pSRB->xs;
	struct trm_dcb *pDCB = pSRB->pSRBDCB;
	int target, lun, intflag;

#ifdef TRM_DEBUG0
	printf("%s: trm_FinishSRB. sc = %p, pSRB = %p\n",
	    sc->sc_device.dv_xname, sc, pSRB);
#endif
	pDCB->DCBFlag &= ~TRM_QUEUE_FULL;

	intflag = splbio();
	if (pSRB->TagNumber != TRM_NO_TAG) {
		pSRB->pSRBDCB->TagMask &= ~(1 << pSRB->TagNumber);
		pSRB->TagNumber = TRM_NO_TAG;
	}
	/* SRB may have started & finished, or be waiting and timed out */
	if ((pSRB->SRBFlag & TRM_ON_WAITING_SRB) != 0) {
		pSRB->SRBFlag &= ~TRM_ON_WAITING_SRB;
		TAILQ_REMOVE(&sc->waitingSRB, pSRB, link);
	}
	if ((pSRB->SRBFlag & TRM_ON_GOING_SRB) != 0) {
		pSRB->SRBFlag &= ~TRM_ON_GOING_SRB;
		TAILQ_REMOVE(&sc->goingSRB, pSRB, link);
	}
	splx(intflag);

	if (xs == NULL) {
		return;
	}

	timeout_del(&xs->stimeout);

	xs->status = pSRB->TargetStatus;
	if (xs->datalen != 0) {
		bus_dmamap_sync(sc->sc_dmatag, pSRB->dmamapxfer,
		    0, pSRB->dmamapxfer->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmatag, pSRB->dmamapxfer);
	}

	switch (xs->status) {
	case SCSI_INTERM_COND_MET:
	case SCSI_COND_MET:
	case SCSI_INTERM:
	case SCSI_OK:
		switch (pSRB->AdaptStatus) {
		case TRM_STATUS_GOOD:
			if ((pSRB->SRBFlag & TRM_PARITY_ERROR) != 0) {
#ifdef TRM_DEBUG0
				sc_print_addr(xs->sc_link);
				printf(" trm_FinishSRB. TRM_PARITY_ERROR\n");
#endif
				xs->error = XS_DRIVER_STUFFUP;

			} else if ((pSRB->SRBFlag & TRM_SCSI_TIMED_OUT) != 0) {
				if ((pSRB->SRBFlag & TRM_AUTO_REQSENSE) == 0)
					xs->error = XS_TIMEOUT;
				else {
					bzero(&xs->sense, sizeof(xs->sense));
					xs->status = SCSI_CHECK;
					xs->error  = XS_SENSE;
				}

			} else if ((pSRB->SRBFlag & TRM_AUTO_REQSENSE) != 0) {
				s1 = &pSRB->scsisense;
				s2 = &xs->sense;

				*s2 = *s1;

				xs->status = SCSI_CHECK;
				xs->error  = XS_SENSE;

			} else
				xs->error = XS_NOERROR;
			break;

		case TRM_OVER_UNDER_RUN:
#ifdef TRM_DEBUG0
			sc_print_addr(xs->sc_link);
			printf("trm_FinishSRB. TRM_OVER_UNDER_RUN\n");
#endif
			xs->error = XS_DRIVER_STUFFUP;
			break;

		default:
#ifdef TRM_DEBUG0
			sc_print_addr(xs->sc_link);
			printf("trm_FinishSRB. AdaptStatus Error = 0x%02x\n",
			    pSRB->AdaptStatus);
#endif
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		break;

	case SCSI_TERMINATED:
	case SCSI_ACA_ACTIVE:
	case SCSI_CHECK:
		if ((pSRB->SRBFlag & TRM_AUTO_REQSENSE) != 0)
			xs->error = XS_DRIVER_STUFFUP;
		else {
			trm_RequestSense(sc, pSRB);
			return;
		}
		break;

	case SCSI_QUEUE_FULL:
		/* this says no more until someone completes */
		pDCB->DCBFlag |= TRM_QUEUE_FULL;
		trm_RewaitSRB(sc, pSRB);
		return;

	case SCSI_RESV_CONFLICT:
	case SCSI_BUSY:
		xs->error = XS_BUSY;
		break;

	case TRM_SCSI_UNEXP_BUS_FREE:
		xs->status = SCSI_OK;
		xs->error  = XS_DRIVER_STUFFUP;
		break;

	case TRM_SCSI_BUS_RST_DETECTED:
		xs->status = SCSI_OK;
		xs->error  = XS_RESET;
		break;

	case TRM_SCSI_SELECT_TIMEOUT:
		xs->status = SCSI_OK;
		xs->error  = XS_SELTIMEOUT;
		break;

	default:
		xs->error = XS_DRIVER_STUFFUP;
		break;
	}

	target = xs->sc_link->target;
	lun    = xs->sc_link->lun;

	if ((xs->flags & SCSI_POLL) != 0) {

		if (xs->cmd.opcode == INQUIRY && pDCB->sc_link == NULL) {

			ptr = (struct scsi_inquiry_data *) xs->data;

			if ((xs->error != XS_NOERROR) ||
			    ((ptr->device & SID_QUAL_BAD_LU) == SID_QUAL_BAD_LU)) {
#ifdef TRM_DEBUG0
				sc_print_addr(xs->sc_link);
				printf("trm_FinishSRB NO Device\n");
#endif
				free(pDCB, M_DEVBUF, 0);
				sc->pDCB[target][lun] = NULL;
				pDCB = NULL;

			} else
				pDCB->sc_link = xs->sc_link;
		}
	}

	/*
	 * Notify cmd done
	 */
#ifdef TRM_DEBUG0
	if ((xs->error != 0) || (xs->status != 0) ||
	    ((xs->flags & SCSI_POLL) != 0)) {
		sc_print_addr(xs->sc_link);
		printf("trm_FinishSRB. xs->cmd.opcode = 0x%02x, xs->error = %d, xs->status = %d\n",
		    xs->cmd.opcode, xs->error, xs->status);
	}
#endif

	if (ISSET(xs->flags, SCSI_POLL))
		SET(xs->flags, ITSDONE);
	else
		scsi_done(xs);
}

/*
 * ------------------------------------------------------------
 * Function : trm_srb_reinit
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_srb_reinit(struct trm_softc *sc, struct trm_scsi_req_q *pSRB)
{
	bzero(&pSRB->SegmentX[0], sizeof(pSRB->SegmentX));
	bzero(&pSRB->CmdBlock[0], sizeof(pSRB->CmdBlock));
	bzero(&pSRB->scsisense,   sizeof(pSRB->scsisense));

	pSRB->SRBTotalXferLength = 0;
	pSRB->SRBSGCount         = 0;
	pSRB->SRBSGIndex         = 0;
	pSRB->SRBFlag            = 0;

	pSRB->SRBState     = TRM_FREE;
	pSRB->AdaptStatus  = TRM_STATUS_GOOD;
	pSRB->TargetStatus = SCSI_OK;
	pSRB->ScsiPhase    = PH_BUS_FREE; /* SCSI bus free Phase */

	pSRB->xs      = NULL;
	pSRB->pSRBDCB = NULL;
}

/*
 * ------------------------------------------------------------
 * Function : trm_srb_free
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_srb_free(void *xsc, void *xpSRB)
{
	struct trm_softc *sc = xsc;
	struct trm_scsi_req_q *pSRB = xpSRB;

	trm_srb_reinit(sc, pSRB);

	if (pSRB != &sc->SRB[0]) {
		mtx_enter(&sc->sc_srb_mtx);
		TAILQ_INSERT_TAIL(&sc->freeSRB, pSRB, link);
		mtx_leave(&sc->sc_srb_mtx);
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_GoingSRB_Done
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_GoingSRB_Done(struct trm_softc *sc, struct trm_dcb *pDCB)
{
	struct trm_scsi_req_q *pSRB, *pNextSRB;

	/* ASSUME we are inside a splbio()/splx() pair */

	pSRB = TAILQ_FIRST(&sc->goingSRB);
	while (pSRB != NULL) {
		/*
		 * Need to save pNextSRB because trm_FinishSRB() puts
		 * pSRB in freeSRB queue, and thus its links no longer
		 * point to members of the goingSRB queue. This is why
		 * TAILQ_FOREACH() will not work for this traversal.
		 */
		pNextSRB = TAILQ_NEXT(pSRB, link);
		if (pDCB == NULL || pSRB->pSRBDCB == pDCB) {
			/* TODO XXXX: Is TIMED_OUT the best state to report? */
			pSRB->SRBFlag |= TRM_SCSI_TIMED_OUT;
			trm_FinishSRB(sc, pSRB);
		}
		pSRB = pNextSRB;
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_ResetSCSIBus
 * Purpose  : Reset the SCSI bus
 * Inputs   : struct trm_softc * -
 * ------------------------------------------------------------
 */
void
trm_ResetSCSIBus(struct trm_softc *sc)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	int intflag;

	intflag = splbio();

	sc->sc_Flag |= RESET_DEV;

	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_RSTSCSI);
	while ((bus_space_read_2(iot, ioh,
	    TRM_S1040_SCSI_INTSTATUS) & INT_SCSIRESET) == 0);

	splx(intflag);
}

/*
 * ------------------------------------------------------------
 * Function : trm_ScsiRstDetect
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_ScsiRstDetect(struct trm_softc *sc)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	int wlval;

#ifdef TRM_DEBUG0
	printf("%s: trm_ScsiRstDetect\n", sc->sc_device.dv_xname);
#endif

	wlval = 1000;
	/*
	 * delay 1 sec
	 */
	while (--wlval != 0)
		DELAY(1000);

	bus_space_write_1(iot, ioh, TRM_S1040_DMA_CONTROL, STOPDMAXFER);
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);

	if ((sc->sc_Flag & RESET_DEV) != 0)
		sc->sc_Flag |= RESET_DONE;
	else {
		sc->sc_Flag |= RESET_DETECT;
		trm_ResetAllDevParam(sc);
		trm_RecoverSRB(sc);
		sc->pActiveDCB = NULL;
		sc->sc_Flag = 0;
		trm_StartWaitingSRB(sc);
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_RequestSense
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_RequestSense(struct trm_softc *sc, struct trm_scsi_req_q *pSRB)
{
	pSRB->SRBFlag |= TRM_AUTO_REQSENSE;

	/*
	 * Status of initiator/target
	 */
	pSRB->AdaptStatus  = TRM_STATUS_GOOD;
	pSRB->TargetStatus = SCSI_OK;
	/*
	 * Status of initiator/target
	 */

	pSRB->SegmentX[0].address = pSRB->scsisensePhyAddr;
	pSRB->SegmentX[0].length  = sizeof(struct scsi_sense_data);
	pSRB->SRBTotalXferLength  = sizeof(struct scsi_sense_data);
	pSRB->SRBSGCount          = 1;
	pSRB->SRBSGIndex          = 0;

	bzero(&pSRB->CmdBlock[0], sizeof(pSRB->CmdBlock));

	pSRB->CmdBlock[0] = REQUEST_SENSE;
	pSRB->CmdBlock[1] = (pSRB->xs->sc_link->lun) << 5;
	pSRB->CmdBlock[4] = sizeof(struct scsi_sense_data);

	pSRB->ScsiCmdLen = 6;

	if ((pSRB->xs != NULL) && ((pSRB->xs->flags & SCSI_POLL) == 0))
		timeout_add_msec(&pSRB->xs->stimeout, pSRB->xs->timeout);

	if (trm_StartSRB(sc, pSRB) != 0)
		trm_RewaitSRB(sc, pSRB);
}

/*
 * ------------------------------------------------------------
 * Function : trm_EnableMsgOut
 * Purpose  : set up MsgBuf to send out a single byte message
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_EnableMsgOut(struct trm_softc *sc, u_int8_t msg)
{
	sc->MsgBuf[0] = msg;
	sc->MsgCnt    = 1;

	bus_space_write_2(sc->sc_iotag, sc->sc_iohandle, TRM_S1040_SCSI_CONTROL, DO_SETATN);
}

/*
 * ------------------------------------------------------------
 * Function : trm_linkSRB
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_linkSRB(struct trm_softc *sc)
{
	struct trm_scsi_req_q *pSRB;
	int i, intflag;

	intflag = splbio();

	for (i = 0; i < TRM_MAX_SRB_CNT; i++) {
		pSRB = &sc->SRB[i];

		pSRB->PhysSRB = sc->sc_dmamap_control->dm_segs[0].ds_addr
			+ i * sizeof(struct trm_scsi_req_q);

		pSRB->SRBSGPhyAddr = sc->sc_dmamap_control->dm_segs[0].ds_addr
			+ i * sizeof(struct trm_scsi_req_q)
			+ offsetof(struct trm_scsi_req_q, SegmentX);

		pSRB->scsisensePhyAddr = sc->sc_dmamap_control->dm_segs[0].ds_addr
			+ i * sizeof(struct trm_scsi_req_q)
			+ offsetof(struct trm_scsi_req_q, scsisense);

		/*
		 * map all SRB space
		 */
		if (bus_dmamap_create(sc->sc_dmatag, TRM_MAX_PHYSG_BYTE,
		    TRM_MAX_SG_LISTENTRY, TRM_MAX_PHYSG_BYTE, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &pSRB->dmamapxfer) != 0) {
			printf("%s: unable to create DMA transfer map\n",
			    sc->sc_device.dv_xname);
			splx(intflag);
			return;
		}

		if (i > 0)
			/* We use sc->SRB[0] directly, so *don't* link it */
			TAILQ_INSERT_TAIL(&sc->freeSRB, pSRB, link);
#ifdef TRM_DEBUG0
		printf("pSRB = %p ", pSRB);
#endif
	}
#ifdef TRM_DEBUG0
	printf("\n ");
#endif
	splx(intflag);
}

/*
 * ------------------------------------------------------------
 * Function : trm_initACB
 * Purpose  : initialize the internal structures for a given SCSI host
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_initACB(struct trm_softc *sc, int unit)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	struct trm_adapter_nvram *pEEpromBuf;
	struct trm_dcb *pDCB;
	int target, lun;

	pEEpromBuf = &trm_eepromBuf[unit];
	sc->sc_config = HCC_AUTOTERM | HCC_PARITY;

	if ((bus_space_read_1(iot, ioh, TRM_S1040_GEN_STATUS) & WIDESCSI) != 0)
		sc->sc_config |= HCC_WIDE_CARD;

	if ((pEEpromBuf->NvramChannelCfg & NAC_POWERON_SCSI_RESET) != 0)
		sc->sc_config |= HCC_SCSI_RESET;

	TAILQ_INIT(&sc->freeSRB);
	TAILQ_INIT(&sc->waitingSRB);
	TAILQ_INIT(&sc->goingSRB);

	mtx_init(&sc->sc_srb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, trm_srb_alloc, trm_srb_free);

	sc->pActiveDCB     = NULL;
	sc->sc_AdapterUnit = unit;
	sc->sc_AdaptSCSIID = pEEpromBuf->NvramScsiId;
	sc->sc_TagMaxNum   = 2 << pEEpromBuf->NvramMaxTag;
	sc->sc_Flag        = 0;

	/*
	 * put all SRB's (except [0]) onto the freeSRB list
	 */
	trm_linkSRB(sc);

	/*
	 * allocate DCB array
	 */
	for (target = 0; target < TRM_MAX_TARGETS; target++) {
		if (target == sc->sc_AdaptSCSIID)
			continue;

		for (lun = 0; lun < TRM_MAX_LUNS; lun++) {
			pDCB = (struct trm_dcb *)malloc(sizeof(struct trm_dcb),
			    M_DEVBUF, M_NOWAIT | M_ZERO);
			sc->pDCB[target][lun] = pDCB;

			if (pDCB == NULL)
				continue;

			pDCB->target     = target;
			pDCB->lun        = lun;
			pDCB->pActiveSRB = NULL;
		}
	}

	trm_reset(sc);
}

/*
 * ------------------------------------------------------------
 * Function     : trm_write_all
 * Description  : write pEEpromBuf 128 bytes to seeprom
 * Input        : iot, ioh - chip's base address
 * Output       : none
 * ------------------------------------------------------------
 */
void
trm_write_all(struct trm_adapter_nvram *pEEpromBuf,  bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	u_int8_t *bpEeprom = (u_int8_t *)pEEpromBuf;
	u_int8_t  bAddr;

	/*
	 * Enable SEEPROM
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_CONTROL,
	    (bus_space_read_1(iot, ioh, TRM_S1040_GEN_CONTROL) | EN_EEPROM));
	/*
	 * Write enable
	 */
	trm_write_cmd(iot, ioh, 0x04, 0xFF);
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, 0);
	trm_wait_30us(iot, ioh);
	for (bAddr = 0; bAddr < 128; bAddr++, bpEeprom++)
		trm_set_data(iot, ioh, bAddr, *bpEeprom);
	/*
	 * Write disable
	 */
	trm_write_cmd(iot, ioh, 0x04, 0x00);
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, 0);
	trm_wait_30us(iot, ioh);
	/*
	 * Disable SEEPROM
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_CONTROL,
	    (bus_space_read_1(iot, ioh, TRM_S1040_GEN_CONTROL) & ~EN_EEPROM));
}

/*
 * ------------------------------------------------------------
 * Function     : trm_set_data
 * Description  : write one byte to seeprom
 * Input        : iot, ioh - chip's base address
 *                  bAddr - address of SEEPROM
 *                  bData - data of SEEPROM
 * Output       : none
 * ------------------------------------------------------------
 */
void
trm_set_data(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t bAddr,
    u_int8_t bData)
{
	u_int8_t bSendData;
	int i;

	/*
	 * Send write command & address
	 */
	trm_write_cmd(iot, ioh, 0x05, bAddr);
	/*
	 * Write data
	 */
	for (i = 0; i < 8; i++, bData <<= 1) {
		bSendData = NVR_SELECT;
		if ((bData & 0x80) != 0) {      /* Start from bit 7    */
			bSendData |= NVR_BITOUT;
		}
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, bSendData);
		trm_wait_30us(iot, ioh);
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM,
		    (bSendData | NVR_CLOCK));
		trm_wait_30us(iot, ioh);
	}
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, NVR_SELECT);
	trm_wait_30us(iot, ioh);
	/*
	 * Disable chip select
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, 0);
	trm_wait_30us(iot, ioh);
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, NVR_SELECT);
	trm_wait_30us(iot, ioh);
	/*
	 * Wait for write ready
	 */
	for (;;) {
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM,
		    (NVR_SELECT | NVR_CLOCK));
		trm_wait_30us(iot, ioh);
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, NVR_SELECT);
		trm_wait_30us(iot, ioh);
		if (bus_space_read_1(iot, ioh, TRM_S1040_GEN_NVRAM) & NVR_BITIN)
			break;
	}
	/*
	 * Disable chip select
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, 0);
}

/*
 * ------------------------------------------------------------
 * Function     : trm_read_all
 * Description  : read seeprom 128 bytes to pEEpromBuf
 * Input        : pEEpromBuf, iot, ioh - chip's base address
 * Output       : none
 * ------------------------------------------------------------
 */
void
trm_read_all(struct trm_adapter_nvram *pEEpromBuf,  bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	u_int8_t *bpEeprom = (u_int8_t *)pEEpromBuf;
	u_int8_t  bAddr;

	/*
	 * Enable SEEPROM
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_CONTROL,
	    (bus_space_read_1(iot, ioh, TRM_S1040_GEN_CONTROL) | EN_EEPROM));

	for (bAddr = 0; bAddr < 128; bAddr++, bpEeprom++)
		*bpEeprom = trm_get_data(iot, ioh, bAddr);

	/*
	 * Disable SEEPROM
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_CONTROL,
	    (bus_space_read_1(iot, ioh, TRM_S1040_GEN_CONTROL) & ~EN_EEPROM));
}

/*
 * ------------------------------------------------------------
 * Function     : trm_get_data
 * Description  : read one byte from seeprom
 * Input        : iot, ioh - chip's base address
 *                     bAddr - address of SEEPROM
 * Output       : bData - data of SEEPROM
 * ------------------------------------------------------------
 */
u_int8_t
trm_get_data( bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t bAddr)
{
	u_int8_t bReadData, bData;
	int i;

	bData = 0;

	/*
	 * Send read command & address
	 */
	trm_write_cmd(iot, ioh, 0x06, bAddr);

	for (i = 0; i < 8; i++) {
		/*
		 * Read data
		 */
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM,
		    (NVR_SELECT | NVR_CLOCK));
		trm_wait_30us(iot, ioh);
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, NVR_SELECT);
		/*
		 * Get data bit while falling edge
		 */
		bReadData = bus_space_read_1(iot, ioh, TRM_S1040_GEN_NVRAM);
		bData <<= 1;
		if ((bReadData & NVR_BITIN) != 0)
			bData |= 1;
		trm_wait_30us(iot, ioh);
	}
	/*
	 * Disable chip select
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, 0);

	return bData;
}

/*
 * ------------------------------------------------------------
 * Function     : trm_wait_30us
 * Description  : wait 30 us
 * Input        : iot, ioh - chip's base address
 * Output       : none
 * ------------------------------------------------------------
 */
void
trm_wait_30us(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_TIMER, 5);

	while ((bus_space_read_1(iot, ioh, TRM_S1040_GEN_STATUS) & GTIMEOUT)
	    == 0);
}

/*
 * ------------------------------------------------------------
 * Function     : trm_write_cmd
 * Description  : write SB and Op Code into seeprom
 * Input        : iot, ioh - chip's base address
 *                  bCmd     - SB + Op Code
 *                  bAddr    - address of SEEPROM
 * Output       : none
 * ------------------------------------------------------------
 */
void
trm_write_cmd( bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t bCmd,
    u_int8_t bAddr)
{
	u_int8_t bSendData;
	int i;

	for (i = 0; i < 3; i++, bCmd <<= 1) {
		/*
		 * Program SB + OP code
		 */
		bSendData = NVR_SELECT;
		if (bCmd & 0x04)        /* Start from bit 2        */
			bSendData |= NVR_BITOUT;
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, bSendData);
		trm_wait_30us(iot, ioh);
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM,
		    (bSendData | NVR_CLOCK));
		trm_wait_30us(iot, ioh);
	}

	for (i = 0; i < 7; i++, bAddr <<= 1) {
		/*
		 * Program address
		 */
		bSendData = NVR_SELECT;
		if (bAddr & 0x40) {        /* Start from bit 6        */
			bSendData |= NVR_BITOUT;
		}
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, bSendData);
		trm_wait_30us(iot, ioh);
		bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM,
		    (bSendData | NVR_CLOCK));
		trm_wait_30us(iot, ioh);
	}
	bus_space_write_1(iot, ioh, TRM_S1040_GEN_NVRAM, NVR_SELECT);
	trm_wait_30us(iot, ioh);
}

/*
 * ------------------------------------------------------------
 * Function     : trm_check_eeprom
 * Description  : read eeprom 128 bytes to pEEpromBuf and check
 *                  checksum. If it is wrong, updated with default value.
 * Input        : eeprom, iot, ioh - chip's base address
 * Output       : none
 * ------------------------------------------------------------
 */
void
trm_check_eeprom(struct trm_adapter_nvram *pEEpromBuf, bus_space_tag_t iot,
    bus_space_handle_t ioh)
{
	u_int32_t *dpEeprom = (u_int32_t *)pEEpromBuf->NvramTarget;
	u_int32_t  dAddr;
	u_int16_t *wpEeprom = (u_int16_t *)pEEpromBuf;
	u_int16_t  wAddr, wCheckSum;

#ifdef TRM_DEBUG0
	printf("\ntrm_check_eeprom\n");
#endif
	trm_read_all(pEEpromBuf, iot, ioh);
	wCheckSum = 0;
	for (wAddr = 0; wAddr < 64; wAddr++, wpEeprom++)
		wCheckSum += *wpEeprom;

	if (wCheckSum != 0x1234) {
#ifdef TRM_DEBUG0
		printf("TRM_S1040 EEPROM Check Sum ERROR (load default)\n");
#endif
		/*
		 * Checksum error, load default
		 */
		pEEpromBuf->NvramSubVendorID[0] = (u_int8_t)PCI_VENDOR_TEKRAM2;
		pEEpromBuf->NvramSubVendorID[1] = (u_int8_t)(PCI_VENDOR_TEKRAM2
		    >> 8);
		pEEpromBuf->NvramSubSysID[0] = (u_int8_t)
		    PCI_PRODUCT_TEKRAM2_DC3X5U;
		pEEpromBuf->NvramSubSysID[1] = (u_int8_t)
		    (PCI_PRODUCT_TEKRAM2_DC3X5U >> 8);
		pEEpromBuf->NvramSubClass    = 0;
		pEEpromBuf->NvramVendorID[0] = (u_int8_t)PCI_VENDOR_TEKRAM2;
		pEEpromBuf->NvramVendorID[1] = (u_int8_t)(PCI_VENDOR_TEKRAM2
		    >> 8);
		pEEpromBuf->NvramDeviceID[0] = (u_int8_t)
		    PCI_PRODUCT_TEKRAM2_DC3X5U;
		pEEpromBuf->NvramDeviceID[1] = (u_int8_t)
		    (PCI_PRODUCT_TEKRAM2_DC3X5U >> 8);
		pEEpromBuf->NvramReserved    = 0;

		for (dAddr = 0; dAddr < 16; dAddr++, dpEeprom++)
			/*
			 * NvmTarCfg3,NvmTarCfg2,NvmTarPeriod,NvmTarCfg0
			 */
			*dpEeprom = 0x00000077;

		/*
		 * NvramMaxTag,NvramDelayTime,NvramChannelCfg,NvramScsiId
		 */
		*dpEeprom++ = 0x04000F07;

		/*
		 * NvramReserved1,NvramBootLun,NvramBootTarget,NvramReserved0
		 */
		*dpEeprom++ = 0x00000015;
		for (dAddr = 0; dAddr < 12; dAddr++, dpEeprom++)
			*dpEeprom = 0;

		pEEpromBuf->NvramCheckSum = 0;
		for (wAddr = 0, wCheckSum =0; wAddr < 63; wAddr++, wpEeprom++)
			wCheckSum += *wpEeprom;

		*wpEeprom = 0x1234 - wCheckSum;
		trm_write_all(pEEpromBuf, iot, ioh);
	}
}

/*
 * ------------------------------------------------------------
 * Function : trm_initAdapter
 * Purpose  : initialize the SCSI chip ctrl registers
 * Inputs   : psh - pointer to this host adapter's structure
 * ------------------------------------------------------------
 */
void
trm_initAdapter(struct trm_softc *sc)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	u_int16_t wval;
	u_int8_t bval;

	/*
	 * program configuration 0
	 */
	if ((sc->sc_config & HCC_PARITY) != 0) {
		bval = PHASELATCH | INITIATOR | BLOCKRST | PARITYCHECK;
	} else {
		bval = PHASELATCH | INITIATOR | BLOCKRST;
	}
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_CONFIG0, bval);
	/*
	 * program configuration 1
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_CONFIG1, 0x13);
	/*
	 * 250ms selection timeout
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_TIMEOUT, TRM_SEL_TIMEOUT);
	/*
	 * Mask all the interrupt
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_DMA_INTEN,  0);
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_INTEN, 0);
	/*
	 * Reset SCSI module
	 */
	bus_space_write_2(iot, ioh, TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);
	/*
	 * program Host ID
	 */
	bval = sc->sc_AdaptSCSIID;
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_HOSTID, bval);
	/*
	 * set asynchronous transfer
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_OFFSET, 0);
	/*
	 * Turn LED control off
	 */
	wval = bus_space_read_2(iot, ioh, TRM_S1040_GEN_CONTROL) & 0x7F;
	bus_space_write_2(iot, ioh, TRM_S1040_GEN_CONTROL, wval);
	/*
	 * DMA config
	 */
	wval = bus_space_read_2(iot, ioh, TRM_S1040_DMA_CONFIG) | DMA_ENHANCE;
	bus_space_write_2(iot, ioh, TRM_S1040_DMA_CONFIG, wval);
	/*
	 * Clear pending interrupt status
	 */
	bus_space_read_1(iot, ioh, TRM_S1040_SCSI_INTSTATUS);
	/*
	 * Enable SCSI interrupts
	 */
	bus_space_write_1(iot, ioh, TRM_S1040_SCSI_INTEN,
	    (EN_SELECT | EN_SELTIMEOUT | EN_DISCONNECT | EN_RESELECTED |
		EN_SCSIRESET | EN_BUSSERVICE | EN_CMDDONE));
	bus_space_write_1(iot, ioh, TRM_S1040_DMA_INTEN, EN_SCSIINTR);
}

/*
 * ------------------------------------------------------------
 * Function      : trm_init
 * Purpose       : initialize the internal structures for a given SCSI host
 * Inputs        : host - pointer to this host adapter's structure
 * Preconditions : when this function is called, the chip_type field of
 *                 the ACB structure MUST have been set.
 * ------------------------------------------------------------
 */
int
trm_init(struct trm_softc *sc, int unit)
{
	const bus_space_handle_t ioh = sc->sc_iohandle;
	const bus_space_tag_t iot = sc->sc_iotag;
	bus_dma_segment_t seg;
	int error, rseg, all_srbs_size;

	/*
	 * EEPROM CHECKSUM
	 */
	trm_check_eeprom(&trm_eepromBuf[unit], iot, ioh);

	/*
	 * MEMORY ALLOCATE FOR ADAPTER CONTROL BLOCK
	 */
	/*
	 * allocate the space for all SCSI control blocks (SRB) for DMA memory.
	 */
	all_srbs_size = TRM_MAX_SRB_CNT * sizeof(struct trm_scsi_req_q);

	error = bus_dmamem_alloc(sc->sc_dmatag, all_srbs_size, NBPG, 0, &seg,
	    1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: unable to allocate SCSI REQUEST BLOCKS, error = %d\n",
		    sc->sc_device.dv_xname, error);
		/*errx(error, "%s: unable to allocate SCSI request blocks",
		    sc->sc_device.dv_xname);*/
		return -1;
	}

	error = bus_dmamem_map(sc->sc_dmatag, &seg, rseg, all_srbs_size,
	    (caddr_t *)&sc->SRB, BUS_DMA_NOWAIT|BUS_DMA_COHERENT);
	if (error != 0) {
		printf("%s: unable to map SCSI REQUEST BLOCKS, error = %d\n",
		    sc->sc_device.dv_xname, error);
		/*errx(error, "unable to map SCSI request blocks");*/
		return -1;
	}

	error = bus_dmamap_create(sc->sc_dmatag, all_srbs_size, 1,
	    all_srbs_size, 0, BUS_DMA_NOWAIT,&sc->sc_dmamap_control);
	if (error != 0) {
		printf("%s: unable to create SRB DMA maps, error = %d\n",
		    sc->sc_device.dv_xname, error);
		/*errx(error, "unable to create SRB DMA maps");*/
		return -1;
	}

	error = bus_dmamap_load(sc->sc_dmatag, sc->sc_dmamap_control,
	    sc->SRB, all_srbs_size, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: unable to load SRB DMA maps, error = %d\n",
		    sc->sc_device.dv_xname, error);
		/*errx(error, "unable to load SRB DMA maps");*/
		return -1;
	}
#ifdef TRM_DEBUG0
	printf("\n\n%s: all_srbs_size=%x\n",
	    sc->sc_device.dv_xname, all_srbs_size);
#endif
	trm_initACB(sc, unit);
	trm_initAdapter(sc);

	return 0;
}

/* ------------------------------------------------------------
 * Function : trm_print_info
 * Purpose  : Print the DCB negotiation information
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_print_info(struct trm_softc *sc, struct trm_dcb *pDCB)
{
	int syncXfer, index;

	index = pDCB->SyncPeriod & ~(WIDE_SYNC | ALT_SYNC);

	printf("%s: target %d using ", sc->sc_device.dv_xname, pDCB->target);
	if ((pDCB->SyncPeriod & WIDE_SYNC) != 0)
		printf("16 bit ");
	else
		printf("8 bit ");

	if (pDCB->SyncOffset == 0)
		printf("Asynchronous ");
	else {
		syncXfer = 100000 / (trm_clock_period[index] * 4);
		printf("%d.%01d MHz, Offset %d ",
		    syncXfer / 100, syncXfer % 100, pDCB->SyncOffset);
	}
	printf("data transfers ");

	if ((pDCB->DCBFlag & TRM_USE_TAG_QUEUING) != 0)
		printf("with Tag Queuing");

	printf("\n");
}
