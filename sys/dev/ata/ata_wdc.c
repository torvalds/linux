/*      $OpenBSD: ata_wdc.c,v 1.54 2024/06/18 09:08:02 jsg Exp $	*/
/*	$NetBSD: ata_wdc.c,v 1.21 1999/08/09 09:43:11 bouyer Exp $	*/

/*
 * Copyright (c) 1998, 2001 Manuel Bouyer.
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
 */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum, by Onno van der Linden and by Manuel Bouyer.
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
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/disk.h>

#include <machine/bus.h>

#include <dev/ata/atavar.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcvar.h>
#include <dev/ata/wdvar.h>

#ifdef HIBERNATE
#include <sys/hibernate.h>
#endif

#define DEBUG_INTR   0x01
#define DEBUG_XFERS  0x02
#define DEBUG_STATUS 0x04
#define DEBUG_FUNCS  0x08
#define DEBUG_PROBE  0x10

#ifdef WDCDEBUG
#ifndef WDCDEBUG_WD_MASK
#define WDCDEBUG_WD_MASK 0x00
#endif
int wdcdebug_wd_mask = WDCDEBUG_WD_MASK;
#define WDCDEBUG_PRINT(args, level) do {	\
	if ((wdcdebug_wd_mask & (level)) != 0)	\
		printf args;			\
} while (0)
#else
#define WDCDEBUG_PRINT(args, level)
#endif

#define ATA_DELAY 45000 /* 45s for a drive I/O */

void  wdc_ata_bio_start(struct channel_softc *, struct wdc_xfer *);
void  _wdc_ata_bio_start(struct channel_softc *, struct wdc_xfer *);
int   wdc_ata_bio_intr(struct channel_softc *, struct wdc_xfer *, int);
void  wdc_ata_bio_kill_xfer(struct channel_softc *, struct wdc_xfer *);
void  wdc_ata_bio_done(struct channel_softc *, struct wdc_xfer *);
int   wdc_ata_ctrl_intr(struct channel_softc *, struct wdc_xfer *, int);
int   wdc_ata_err(struct ata_drive_datas *, struct ata_bio *);
#define WDC_ATA_NOERR 0x00 /* Drive doesn't report an error */
#define WDC_ATA_RECOV 0x01 /* There was a recovered error */
#define WDC_ATA_ERR   0x02 /* Drive reports an error */

#ifdef HIBERNATE
int
wd_hibernate_io(dev_t dev, daddr_t blkno, vaddr_t addr, size_t size, int op, void *page)
{
	struct {
		struct wd_softc wd;
		struct wdc_xfer xfer;
		struct channel_softc chp;
		daddr_t poffset;
		size_t psize;
	} *my = page;
	struct wd_softc *real_wd, *wd = &my->wd;
	struct wdc_xfer *xfer = &my->xfer;
	struct channel_softc *chp = &my->chp;
	struct ata_bio *ata_bio;
	extern struct cfdriver wd_cd;

	/* early call for initialization */
	if (op == HIB_INIT) {
		my->poffset = blkno;
		my->psize = size;
		return(0);
	}

	real_wd = (struct wd_softc *)disk_lookup(&wd_cd, DISKUNIT(dev));
	if (real_wd == NULL)
		return (ENODEV);

	if (op == HIB_DONE) {
		struct wdc_softc *wdc = chp->wdc;
		config_suspend(&wdc->sc_dev, DVACT_RESUME);
		return (0);
	}

	if (blkno > my->psize)
		return (E2BIG);
	blkno += my->poffset;

	/*
	 * Craft a fake set of softc and related structures
	 * which we think the driver modifies.  Some of these will
	 * have pointers which reach to unsafe places, but..
	 */
	bcopy(real_wd->drvp->chnl_softc, &my->chp, sizeof my->chp);
	chp->ch_drive[0].chnl_softc = chp;
	chp->ch_drive[1].chnl_softc = chp;

	bcopy(real_wd, &my->wd, sizeof my->wd);
	ata_bio = &wd->sc_wdc_bio;
	ata_bio->wd = wd;		/* fixup ata_bio->wd */
	wd->drvp = &chp->ch_drive[real_wd->drvp->drive];

	/* Fill the request and submit it */
	wd->sc_wdc_bio.blkno = blkno;
	wd->sc_wdc_bio.flags = ATA_POLL | ATA_LBA48;
	if (op == HIB_R)
		wd->sc_wdc_bio.flags |= ATA_READ;
	wd->sc_wdc_bio.bcount = size;
	wd->sc_wdc_bio.databuf = (caddr_t)addr;
	wd->sc_wdc_bio.wd = wd;

	bzero(&my->xfer, sizeof my->xfer);
	xfer->c_flags |= C_PRIVATEXFER;	/* Our xfer is totally private */
	xfer->c_flags |= C_POLL;
	xfer->drive = wd->drvp->drive;
	xfer->cmd = ata_bio;
	xfer->databuf = ata_bio->databuf;
	xfer->c_bcount = ata_bio->bcount;
	xfer->c_start = wdc_ata_bio_start;
	xfer->c_intr = wdc_ata_bio_intr;
	xfer->c_kill_xfer = wdc_ata_bio_kill_xfer;
	wdc_exec_xfer(chp, xfer);
	return (ata_bio->flags & ATA_ITSDONE) ? 0 : EIO;
}
#endif /* HIBERNATE */

/*
 * Handle block I/O operation. Return WDC_COMPLETE, WDC_QUEUED, or
 * WDC_TRY_AGAIN. Must be called at splbio().
 */
int
wdc_ata_bio(struct ata_drive_datas *drvp, struct ata_bio *ata_bio)
{
	struct wdc_xfer *xfer;
	struct channel_softc *chp = drvp->chnl_softc;

	xfer = wdc_get_xfer(WDC_NOSLEEP);
	if (xfer == NULL)
		return WDC_TRY_AGAIN;
	if (ata_bio->flags & ATA_POLL)
		xfer->c_flags |= C_POLL;
	if (!(ata_bio->flags & ATA_POLL) &&
	    (drvp->drive_flags & (DRIVE_DMA | DRIVE_UDMA)) &&
	    (ata_bio->flags & ATA_SINGLE) == 0 &&
	    (ata_bio->bcount > 512 ||
	    (chp->wdc->quirks & WDC_QUIRK_NOSHORTDMA) == 0))
		xfer->c_flags |= C_DMA;
	xfer->drive = drvp->drive;
	xfer->cmd = ata_bio;
	xfer->databuf = ata_bio->databuf;
	xfer->c_bcount = ata_bio->bcount;
	xfer->c_start = wdc_ata_bio_start;
	xfer->c_intr = wdc_ata_bio_intr;
	xfer->c_kill_xfer = wdc_ata_bio_kill_xfer;
	wdc_exec_xfer(chp, xfer);
	return (ata_bio->flags & ATA_ITSDONE) ? WDC_COMPLETE : WDC_QUEUED;
}

void
wdc_ata_bio_start(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	struct ata_bio *ata_bio = xfer->cmd;
	WDCDEBUG_PRINT(("wdc_ata_bio_start %s:%d:%d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive),
	    DEBUG_XFERS);

	/* start timeout machinery */
	if ((ata_bio->flags & ATA_POLL) == 0)
		timeout_add_msec(&chp->ch_timo, ATA_DELAY);
	_wdc_ata_bio_start(chp, xfer);
}

void
_wdc_ata_bio_start(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	struct ata_bio *ata_bio = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	u_int16_t cyl;
	u_int8_t head, sect, cmd = 0;
	int nblks;
	int error, dma_flags = 0;

	WDCDEBUG_PRINT(("_wdc_ata_bio_start %s:%d:%d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive),
	    DEBUG_INTR | DEBUG_XFERS);
	/* Do control operations specially. */
	if (drvp->state < READY) {
		/*
		 * Actually, we want to be careful not to mess with the control
		 * state if the device is currently busy, but we can assume
		 * that we never get to this point if that's the case.
		 */
		/* at this point, we should only be in RECAL state */
		if (drvp->state != RECAL) {
			printf("%s:%d:%d: bad state %d in _wdc_ata_bio_start\n",
			    chp->wdc->sc_dev.dv_xname, chp->channel,
			    xfer->drive, drvp->state);
			panic("_wdc_ata_bio_start: bad state");
		}
		xfer->c_intr = wdc_ata_ctrl_intr;
		wdc_set_drive(chp, xfer->drive);
		if (wdcwait(chp, WDCS_DRDY, WDCS_DRDY, ATA_DELAY) != 0)
			goto timeout;
		wdccommandshort(chp, xfer->drive, WDCC_RECAL);
		drvp->state = RECAL_WAIT;
		if ((ata_bio->flags & ATA_POLL) == 0) {
			chp->ch_flags |= WDCF_IRQ_WAIT;
		} else {
			/* Wait for at last 400ns for status bit to be valid */
			DELAY(1);
			wdc_ata_ctrl_intr(chp, xfer, 0);
		}
		return;
	}

	if (xfer->c_flags & C_DMA) {
		if (drvp->n_xfers <= NXFER)
			drvp->n_xfers++;
		dma_flags = (ata_bio->flags & ATA_READ) ?  WDC_DMA_READ : 0;
		if (ata_bio->flags & ATA_LBA48)
			dma_flags |= WDC_DMA_LBA48;
	}
again:
	/*
	 *
	 * When starting a multi-sector transfer, or doing single-sector
	 * transfers...
	 */
	if (xfer->c_skip == 0 || (ata_bio->flags & ATA_SINGLE) != 0) {
		if (ata_bio->flags & ATA_SINGLE)
			nblks = 1;
		else
			nblks = xfer->c_bcount / ata_bio->lp->d_secsize;
		if (ata_bio->flags & ATA_LBA) {
			sect = (ata_bio->blkno >> 0) & 0xff;
			cyl = (ata_bio->blkno >> 8) & 0xffff;
			head = (ata_bio->blkno >> 24) & 0x0f;
			head |= WDSD_LBA;
		} else {
			int blkno = ata_bio->blkno;
			sect = blkno % ata_bio->lp->d_nsectors;
			sect++;    /* Sectors begin with 1, not 0. */
			blkno /= ata_bio->lp->d_nsectors;
			head = blkno % ata_bio->lp->d_ntracks;
			blkno /= ata_bio->lp->d_ntracks;
			cyl = blkno;
			head |= WDSD_CHS;
		}
		if (xfer->c_flags & C_DMA) {
			ata_bio->nblks = nblks;
			ata_bio->nbytes = xfer->c_bcount;
			if (ata_bio->flags & ATA_LBA48)
				cmd = (ata_bio->flags & ATA_READ) ?
				    WDCC_READDMA_EXT : WDCC_WRITEDMA_EXT;
			else
				cmd = (ata_bio->flags & ATA_READ) ?
				    WDCC_READDMA : WDCC_WRITEDMA;
	    		/* Init the DMA channel. */
			error = (*chp->wdc->dma_init)(chp->wdc->dma_arg,
			    chp->channel, xfer->drive,
			    (char *)xfer->databuf + xfer->c_skip,
			    ata_bio->nbytes, dma_flags);
			if (error) {
				if (error == EINVAL) {
					/*
					 * We can't do DMA on this transfer
					 * for some reason.  Fall back to
					 * PIO.
					 */
					xfer->c_flags &= ~C_DMA;
					error = 0;
					goto do_pio;
				}
				ata_bio->error = ERR_DMA;
				ata_bio->r_error = 0;
				wdc_ata_bio_done(chp, xfer);
				return;
			}
			/* Initiate command */
			wdc_set_drive(chp, xfer->drive);
			if (wait_for_ready(chp, ATA_DELAY) < 0)
				goto timeout;

			/* start the DMA channel (before) */
			if (chp->ch_flags & WDCF_DMA_BEFORE_CMD)
				(*chp->wdc->dma_start)(chp->wdc->dma_arg,
				    chp->channel, xfer->drive);

			if (ata_bio->flags & ATA_LBA48) {
				wdccommandext(chp, xfer->drive, cmd,
				    (u_int64_t)ata_bio->blkno, nblks);
			} else {
				wdccommand(chp, xfer->drive, cmd, cyl,
				    head, sect, nblks, 0);
			}

			/* start the DMA channel (after) */
			if ((chp->ch_flags & WDCF_DMA_BEFORE_CMD) == 0)
				(*chp->wdc->dma_start)(chp->wdc->dma_arg,
				    chp->channel, xfer->drive);

			chp->ch_flags |= WDCF_DMA_WAIT;
			/* wait for irq */
			goto intr;
		} /* else not DMA */
 do_pio:
		ata_bio->nblks = min(nblks, ata_bio->multi);
		ata_bio->nbytes = ata_bio->nblks * ata_bio->lp->d_secsize;
		KASSERT(nblks == 1 || (ata_bio->flags & ATA_SINGLE) == 0);
		if (ata_bio->nblks > 1) {
			if (ata_bio->flags & ATA_LBA48)
				cmd = (ata_bio->flags & ATA_READ) ?
				    WDCC_READMULTI_EXT : WDCC_WRITEMULTI_EXT;
			else
				cmd = (ata_bio->flags & ATA_READ) ?
				    WDCC_READMULTI : WDCC_WRITEMULTI;
		} else {
			if (ata_bio->flags & ATA_LBA48)
				cmd = (ata_bio->flags & ATA_READ) ?
				    WDCC_READ_EXT : WDCC_WRITE_EXT;
			else
				cmd = (ata_bio->flags & ATA_READ) ?
				    WDCC_READ : WDCC_WRITE;
		}
		/* Initiate command! */
		wdc_set_drive(chp, xfer->drive);
		if (wait_for_ready(chp, ATA_DELAY) < 0)
			goto timeout;
		if (ata_bio->flags & ATA_LBA48) {
			wdccommandext(chp, xfer->drive, cmd,
			    (u_int64_t)ata_bio->blkno, nblks);
		} else {
			wdccommand(chp, xfer->drive, cmd, cyl,
			    head, sect, nblks, 0);
		}
	} else if (ata_bio->nblks > 1) {
		/* The number of blocks in the last stretch may be smaller. */
		nblks = xfer->c_bcount / ata_bio->lp->d_secsize;
		if (ata_bio->nblks > nblks) {
			ata_bio->nblks = nblks;
			ata_bio->nbytes = xfer->c_bcount;
		}
	}
	/* If this was a write and not using DMA, push the data. */
	if ((ata_bio->flags & ATA_READ) == 0) {
		if (wait_for_drq(chp, ATA_DELAY) != 0) {
			printf("%s:%d:%d: timeout waiting for DRQ, "
			    "st=0x%b, err=0x%02x\n",
			    chp->wdc->sc_dev.dv_xname, chp->channel,
			    xfer->drive, chp->ch_status, WDCS_BITS,
			    chp->ch_error);
			if (wdc_ata_err(drvp, ata_bio) != WDC_ATA_ERR)
				ata_bio->error = TIMEOUT;
			wdc_ata_bio_done(chp, xfer);
			return;
		}
		if (wdc_ata_err(drvp, ata_bio) == WDC_ATA_ERR) {
			wdc_ata_bio_done(chp, xfer);
			return;
		}
		wdc_output_bytes(drvp, (char *)xfer->databuf + xfer->c_skip,
		    ata_bio->nbytes);
	}

intr:	/* Wait for IRQ (either real or polled) */
	if ((ata_bio->flags & ATA_POLL) == 0) {
		chp->ch_flags |= WDCF_IRQ_WAIT;
	} else {
		/* Wait for at last 400ns for status bit to be valid */
		delay(1);
		if (chp->ch_flags & WDCF_DMA_WAIT) {
			wdc_dmawait(chp, xfer, ATA_DELAY);
			chp->ch_flags &= ~WDCF_DMA_WAIT;
		}
		wdc_ata_bio_intr(chp, xfer, 0);
		if ((ata_bio->flags & ATA_ITSDONE) == 0)
			goto again;
	}
	return;
timeout:
	if (chp->ch_status == 0xff)
		return;
	printf("%s:%d:%d: not ready, st=0x%b, err=0x%02x\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
	    chp->ch_status, WDCS_BITS, chp->ch_error);
	if (wdc_ata_err(drvp, ata_bio) != WDC_ATA_ERR)
		ata_bio->error = TIMEOUT;
	wdc_ata_bio_done(chp, xfer);
	return;
}

int
wdc_ata_bio_intr(struct channel_softc *chp, struct wdc_xfer *xfer, int irq)
{
	struct ata_bio *ata_bio = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	int drv_err;

	WDCDEBUG_PRINT(("wdc_ata_bio_intr %s:%d:%d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive),
	    DEBUG_INTR | DEBUG_XFERS);


	/* Is it not a transfer, but a control operation? */
	if (drvp->state < READY) {
		printf("%s:%d:%d: bad state %d in wdc_ata_bio_intr\n",
		    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
		    drvp->state);
		panic("wdc_ata_bio_intr: bad state");
	}

	/*
	 * reset on timeout. This will cause extra resets in the case
	 * of occasional lost interrupts
	 */
	if (xfer->c_flags & C_TIMEOU)
		goto timeout;

	/* Ack interrupt done by wait_for_unbusy */
	if (wait_for_unbusy(chp,
	    (irq == 0) ? ATA_DELAY : 0) < 0) {
		if (irq)
			return 0; /* IRQ was not for us */
		printf("%s:%d:%d: device timeout, c_bcount=%d, c_skip%d\n",
		    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
		    xfer->c_bcount, xfer->c_skip);

		goto timeout;
	}
	if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
		chp->wdc->irqack(chp);

	drv_err = wdc_ata_err(drvp, ata_bio);

	if (xfer->c_flags & C_DMA) {
		if (chp->wdc->dma_status != 0) {
			if (drv_err != WDC_ATA_ERR) {
				ata_bio->error = ERR_DMA;
				drv_err = WDC_ATA_ERR;
			}
		}
		if (chp->ch_status & WDCS_DRQ) {
			if (drv_err != WDC_ATA_ERR) {
				printf("%s:%d:%d: intr with DRQ (st=0x%b)\n",
				    chp->wdc->sc_dev.dv_xname, chp->channel,
				    xfer->drive, chp->ch_status, WDCS_BITS);
				ata_bio->error = TIMEOUT;
				drv_err = WDC_ATA_ERR;
			}
		}
		if (drv_err != WDC_ATA_ERR)
			goto end;
		ata_dmaerr(drvp);
	}

	/* if we had an error, end */
	if (drv_err == WDC_ATA_ERR) {
		wdc_ata_bio_done(chp, xfer);
		return 1;
	}

	/* If this was a read and not using DMA, fetch the data. */
	if ((ata_bio->flags & ATA_READ) != 0) {
		if ((chp->ch_status & WDCS_DRQ) != WDCS_DRQ) {
			printf("%s:%d:%d: read intr before drq\n",
			    chp->wdc->sc_dev.dv_xname, chp->channel,
			    xfer->drive);
			ata_bio->error = TIMEOUT;
			wdc_ata_bio_done(chp, xfer);
			return 1;
		}
		wdc_input_bytes(drvp, (char *)xfer->databuf + xfer->c_skip,
		    ata_bio->nbytes);
	}
end:
	ata_bio->blkno += ata_bio->nblks;
	ata_bio->blkdone += ata_bio->nblks;
	xfer->c_skip += ata_bio->nbytes;
	xfer->c_bcount -= ata_bio->nbytes;
	/* See if this transfer is complete. */
	if (xfer->c_bcount > 0) {
		if ((ata_bio->flags & ATA_POLL) == 0) {
			/* Start the next operation */
			_wdc_ata_bio_start(chp, xfer);
		} else {
			/* Let _wdc_ata_bio_start do the loop */
			return 1;
		}
	} else { /* Done with this transfer */
		ata_bio->error = NOERROR;
		wdc_ata_bio_done(chp, xfer);
	}
	return 1;

timeout:
	if (xfer->c_flags & C_DMA)
		ata_dmaerr(drvp);

	ata_bio->error = TIMEOUT;
	wdc_ata_bio_done(chp, xfer);
	return 1;
}

void
wdc_ata_bio_kill_xfer(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	struct ata_bio *ata_bio = xfer->cmd;

	timeout_del(&chp->ch_timo);
	/* remove this command from xfer queue */
	wdc_free_xfer(chp, xfer);

	ata_bio->flags |= ATA_ITSDONE;
	ata_bio->error = ERR_NODEV;
	ata_bio->r_error = WDCE_ABRT;
	if ((ata_bio->flags & ATA_POLL) == 0) {
		WDCDEBUG_PRINT(("wdc_ata_done: wddone\n"), DEBUG_XFERS);
		wddone(ata_bio->wd);
	}
}

void
wdc_ata_bio_done(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	struct ata_bio *ata_bio = xfer->cmd;

	WDCDEBUG_PRINT(("wdc_ata_bio_done %s:%d:%d: flags 0x%x\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
	    (u_int)xfer->c_flags),
	    DEBUG_XFERS);

	if ((xfer->c_flags & C_PRIVATEXFER) == 0)
		timeout_del(&chp->ch_timo);

	/* feed back residual bcount to our caller */
	ata_bio->bcount = xfer->c_bcount;

	/* remove this command from xfer queue */
	wdc_free_xfer(chp, xfer);

	ata_bio->flags |= ATA_ITSDONE;
	if ((ata_bio->flags & ATA_POLL) == 0) {
		WDCDEBUG_PRINT(("wdc_ata_done: wddone\n"), DEBUG_XFERS);
		wddone(ata_bio->wd);
	}
	WDCDEBUG_PRINT(("wdcstart from wdc_ata_done, flags 0x%x\n",
	    chp->ch_flags), DEBUG_XFERS);
	wdcstart(chp);
}

/*
 * Implement operations needed before read/write.
 */
int
wdc_ata_ctrl_intr(struct channel_softc *chp, struct wdc_xfer *xfer, int irq)
{
	struct ata_bio *ata_bio = xfer->cmd;
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	char *errstring = NULL;
	int delay = (irq == 0) ? ATA_DELAY : 0;

	WDCDEBUG_PRINT(("wdc_ata_ctrl_intr: state %d\n", drvp->state),
	    DEBUG_FUNCS);

again:
	switch (drvp->state) {
	case RECAL:    /* Should not be in this state here */
		panic("wdc_ata_ctrl_intr: state==RECAL");
		break;

	case RECAL_WAIT:
		errstring = "recal";
		if (wdcwait(chp, WDCS_DRDY, WDCS_DRDY, delay))
			goto timeout;
		if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
			chp->wdc->irqack(chp);
		if (chp->ch_status & (WDCS_ERR | WDCS_DWF))
			goto error;
	/* FALLTHROUGH */

	case PIOMODE:
		/* Don't try to set modes if controller can't be adjusted */
		if ((chp->wdc->cap & WDC_CAPABILITY_MODE) == 0)
			goto geometry;
		/* Also don't try if the drive didn't report its mode */
		if ((drvp->drive_flags & DRIVE_MODE) == 0)
			goto geometry;
		/* SET FEATURES 0x08 is only for PIO mode > 2 */
		if (drvp->PIO_mode <= 2)
			goto geometry;
		wdccommand(chp, drvp->drive, SET_FEATURES, 0, 0, 0,
		    0x08 | drvp->PIO_mode, WDSF_SET_MODE);
		drvp->state = PIOMODE_WAIT;
		break;

	case PIOMODE_WAIT:
		errstring = "piomode";
		if (wdcwait(chp, WDCS_DRDY, WDCS_DRDY, delay))
			goto timeout;
		if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
			chp->wdc->irqack(chp);
		if (chp->ch_status & (WDCS_ERR | WDCS_DWF))
			goto error;
	/* FALLTHROUGH */

	case DMAMODE:
		if (drvp->drive_flags & DRIVE_UDMA) {
			wdccommand(chp, drvp->drive, SET_FEATURES, 0, 0, 0,
			    0x40 | drvp->UDMA_mode, WDSF_SET_MODE);
		} else if (drvp->drive_flags & DRIVE_DMA) {
			wdccommand(chp, drvp->drive, SET_FEATURES, 0, 0, 0,
			    0x20 | drvp->DMA_mode, WDSF_SET_MODE);
		} else {
			goto geometry;
		}
		drvp->state = DMAMODE_WAIT;
		break;
	case DMAMODE_WAIT:
		errstring = "dmamode";
		if (wdcwait(chp, WDCS_DRDY, WDCS_DRDY, delay))
			goto timeout;
		if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
			chp->wdc->irqack(chp);
		if (chp->ch_status & (WDCS_ERR | WDCS_DWF))
			goto error;
	/* FALLTHROUGH */

	case GEOMETRY:
	geometry:
		if (ata_bio->flags & ATA_LBA)
			goto multimode;
		wdccommand(chp, xfer->drive, WDCC_IDP,
		    ata_bio->lp->d_ncylinders,
		    ata_bio->lp->d_ntracks - 1, 0, ata_bio->lp->d_nsectors, 0);
		drvp->state = GEOMETRY_WAIT;
		break;

	case GEOMETRY_WAIT:
		errstring = "geometry";
		if (wdcwait(chp, WDCS_DRDY, WDCS_DRDY, delay))
			goto timeout;
		if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
			chp->wdc->irqack(chp);
		if (chp->ch_status & (WDCS_ERR | WDCS_DWF))
			goto error;
		/* FALLTHROUGH */

	case MULTIMODE:
	multimode:
		if (ata_bio->multi == 1)
			goto ready;
		wdccommand(chp, xfer->drive, WDCC_SETMULTI, 0, 0, 0,
		    ata_bio->multi, 0);
		drvp->state = MULTIMODE_WAIT;
		break;

	case MULTIMODE_WAIT:
		errstring = "setmulti";
		if (wdcwait(chp, WDCS_DRDY, WDCS_DRDY, delay))
			goto timeout;
		if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
			chp->wdc->irqack(chp);
		if (chp->ch_status & (WDCS_ERR | WDCS_DWF))
			goto error;
		/* FALLTHROUGH */

	case READY:
	ready:
		drvp->state = READY;
		/*
		 * The drive is usable now
		 */
		xfer->c_intr = wdc_ata_bio_intr;
		_wdc_ata_bio_start(chp, xfer);
		return 1;
	}

	if ((ata_bio->flags & ATA_POLL) == 0) {
		chp->ch_flags |= WDCF_IRQ_WAIT;
	} else {
		goto again;
	}
	return 1;

timeout:
	if (irq && (xfer->c_flags & C_TIMEOU) == 0) {
		return 0; /* IRQ was not for us */
	}
	printf("%s:%d:%d: %s timed out\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive, errstring);
	ata_bio->error = TIMEOUT;
	drvp->state = 0;
	wdc_ata_bio_done(chp, xfer);
	return 0;
error:
	printf("%s:%d:%d: %s ",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
	    errstring);
	if (chp->ch_status & WDCS_DWF) {
		printf("drive fault\n");
		ata_bio->error = ERR_DF;
	} else {
		printf("error (%x)\n", chp->ch_error);
		ata_bio->r_error = chp->ch_error;
		ata_bio->error = ERROR;
	}
	drvp->state = 0;
	wdc_ata_bio_done(chp, xfer);
	return 1;
}

int
wdc_ata_err(struct ata_drive_datas *drvp, struct ata_bio *ata_bio)
{
	struct channel_softc *chp = drvp->chnl_softc;
	ata_bio->error = 0;

	if (chp->ch_status == 0xff) {
		ata_bio->error = ERR_NODEV;
		return WDC_ATA_ERR;
	}
	if (chp->ch_status & WDCS_BSY) {
		ata_bio->error = TIMEOUT;
		return WDC_ATA_ERR;
	}

	if (chp->ch_status & WDCS_DWF) {
		ata_bio->error = ERR_DF;
		return WDC_ATA_ERR;
	}

	if (chp->ch_status & WDCS_ERR) {
		ata_bio->error = ERROR;
		ata_bio->r_error = chp->ch_error;
		if (drvp->drive_flags & DRIVE_UDMA &&
		    (ata_bio->r_error & WDCE_CRC)) {
			/*
			 * Record the CRC error, to avoid downgrading to
			 * multiword DMA
			 */
			drvp->drive_flags |= DRIVE_DMAERR;
		}
		if (ata_bio->r_error & (WDCE_BBK | WDCE_UNC | WDCE_IDNF |
		    WDCE_ABRT | WDCE_TK0NF | WDCE_AMNF))
			return WDC_ATA_ERR;
		return WDC_ATA_NOERR;
	}

	if (chp->ch_status & WDCS_CORR)
		ata_bio->flags |= ATA_CORR;
	return WDC_ATA_NOERR;
}
