/*	$OpenBSD: ncr53c9x.c,v 1.82 2024/04/13 23:44:11 jsg Exp $	*/
/*     $NetBSD: ncr53c9x.c,v 1.56 2000/11/30 14:41:46 thorpej Exp $    */

/*
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
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
 */

/*
 * Copyright (c) 1994 Peter Galbavy
 * Copyright (c) 1995 Paul Kranenburg
 * All rights reserved.
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

/*
 * Based on aic6360 by Jarle Greipsland
 *
 * Acknowledgements: Many of the algorithms used in this driver are
 * inspired by the work of Julian Elischer (julian@tfs.com) and
 * Charles Hannum (mycroft@duality.gnu.ai.mit.edu).  Thanks a million!
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/pool.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_message.h>

#include <machine/cpu.h>

#include <dev/ic/ncr53c9xreg.h>
#include <dev/ic/ncr53c9xvar.h>

#ifdef NCR53C9X_DEBUG
int ncr53c9x_debug = 0; /*NCR_SHOWPHASE|NCR_SHOWMISC|NCR_SHOWTRAC|NCR_SHOWCMDS;*/
#endif
#ifdef DEBUG
int ncr53c9x_notag = 0;
#endif

/*static*/ void	ncr53c9x_readregs(struct ncr53c9x_softc *);
/*static*/ void	ncr53c9x_select(struct ncr53c9x_softc *,
					    struct ncr53c9x_ecb *);
/*static*/ int ncr53c9x_reselect(struct ncr53c9x_softc *, int, int, int);
/*static*/ void	ncr53c9x_scsi_reset(struct ncr53c9x_softc *);
/*static*/ int	ncr53c9x_poll(struct ncr53c9x_softc *,
					    struct scsi_xfer *, int);
/*static*/ void	ncr53c9x_sched(struct ncr53c9x_softc *);
/*static*/ void	ncr53c9x_done(struct ncr53c9x_softc *,
					    struct ncr53c9x_ecb *);
/*static*/ void	ncr53c9x_msgin(struct ncr53c9x_softc *);
/*static*/ void	ncr53c9x_msgout(struct ncr53c9x_softc *);
/*static*/ void	ncr53c9x_timeout(void *arg);
/*static*/ void	ncr53c9x_abort(struct ncr53c9x_softc *,
					    struct ncr53c9x_ecb *);
/*static*/ void ncr53c9x_dequeue(struct ncr53c9x_softc *,
					    struct ncr53c9x_ecb *);

void ncr53c9x_sense(struct ncr53c9x_softc *,
					    struct ncr53c9x_ecb *);
void ncr53c9x_free_ecb(void *, void *);
void *ncr53c9x_get_ecb(void *);

static inline int ncr53c9x_stp2cpb(struct ncr53c9x_softc *, int);
static inline void ncr53c9x_setsync(struct ncr53c9x_softc *,
					    struct ncr53c9x_tinfo *);
static struct ncr53c9x_linfo *ncr53c9x_lunsearch(struct ncr53c9x_tinfo *,
    int64_t lun);

static void ncr53c9x_wrfifo(struct ncr53c9x_softc *, u_char *, int);
static int ncr53c9x_rdfifo(struct ncr53c9x_softc *, int);
#define NCR_RDFIFO_START	0
#define NCR_RDFIFO_CONTINUE	1

#define NCR_SET_COUNT(sc, size) do {						\
			NCR_WRITE_REG((sc), NCR_TCL, (size));			\
			NCR_WRITE_REG((sc), NCR_TCM, (size) >> 8);		\
			if ((sc->sc_cfg2 & NCRCFG2_FE) ||			\
			    (sc->sc_rev == NCR_VARIANT_FAS366)) {		\
				NCR_WRITE_REG((sc), NCR_TCH, (size) >> 16);	\
			}							\
			if (sc->sc_rev == NCR_VARIANT_FAS366) {			\
				NCR_WRITE_REG(sc, NCR_RCH, 0);			\
			}							\
} while (0)

static int ecb_pool_initialized = 0;
static struct scsi_iopool ecb_iopool;
static struct pool ecb_pool;

struct cfdriver esp_cd = {
	NULL, "esp", DV_DULL
};

void	ncr53c9x_scsi_cmd(struct scsi_xfer *);
int	ncr53c9x_scsi_probe(struct scsi_link *);
void	ncr53c9x_scsi_free(struct scsi_link *);

const struct scsi_adapter ncr53c9x_switch = {
	ncr53c9x_scsi_cmd, NULL, ncr53c9x_scsi_probe,
	ncr53c9x_scsi_free, NULL
};

/*
 * Names for the NCR53c9x variants, corresponding to the variant tags
 * in ncr53c9xvar.h.
 */
const char *ncr53c9x_variant_names[] = {
	"ESP100",
	"ESP100A",
	"ESP200",
	"NCR53C94",
	"NCR53C96",
	"ESP406",
	"FAS408",
	"FAS216",
	"AM53C974",
	"FAS366/HME",
};

/*
 * Search linked list for LUN info by LUN id.
 */
static struct ncr53c9x_linfo *
ncr53c9x_lunsearch(struct ncr53c9x_tinfo *ti, int64_t lun)
{
	struct ncr53c9x_linfo *li;
	LIST_FOREACH(li, &ti->luns, link)
	    if (li->lun == lun)
		    return (li);
	return (NULL);
}

/*
 * Attach this instance, and then all the sub-devices
 */
void
ncr53c9x_attach(struct ncr53c9x_softc *sc)
{
	struct scsibus_attach_args saa;

	/*
	 * Allocate SCSI message buffers.
	 * Front-ends can override allocation to avoid alignment
	 * handling in the DMA engines. Note that ncr53c9x_msgout()
	 * can request a 1 byte DMA transfer.
	 */
	if (sc->sc_omess == NULL)
		sc->sc_omess = malloc(NCR_MAX_MSG_LEN, M_DEVBUF, M_NOWAIT);

	if (sc->sc_imess == NULL)
		sc->sc_imess = malloc(NCR_MAX_MSG_LEN+1, M_DEVBUF, M_NOWAIT);

	if (sc->sc_omess == NULL || sc->sc_imess == NULL) {
		printf("out of memory\n");
		return;
	}

	/*
	 * Note, the front-end has set us up to print the chip variation.
	 */
	if (sc->sc_rev >= NCR_VARIANT_MAX) {
		printf("\n%s: unknown variant %d, devices not attached\n",
		    sc->sc_dev.dv_xname, sc->sc_rev);
		return;
	}

	printf(": %s, %dMHz\n", ncr53c9x_variant_names[sc->sc_rev],
	    sc->sc_freq);

	sc->sc_ccf = FREQTOCCF(sc->sc_freq);

	/* The value *must not* be == 1. Make it 2 */
	if (sc->sc_ccf == 1)
		sc->sc_ccf = 2;

	/*
	 * The recommended timeout is 250ms. This register is loaded
	 * with a value calculated as follows, from the docs:
	 *
	 *		(timeout period) x (CLK frequency)
	 *	reg = -------------------------------------
	 *		 8192 x (Clock Conversion Factor)
	 *
	 * Since CCF has a linear relation to CLK, this generally computes
	 * to the constant of 153.
	 */
	sc->sc_timeout = ((250 * 1000) * sc->sc_freq) / (8192 * sc->sc_ccf);

	/* CCF register only has 3 bits; 0 is actually 8 */
	sc->sc_ccf &= 7;

	/* Find how many targets we need to support */
	switch (sc->sc_rev) {
	case NCR_VARIANT_FAS366:
		sc->sc_ntarg = 16;
		break;
	default:
		sc->sc_ntarg = 8;
		break;
	}

	/* Reset state & bus */
	sc->sc_cfflags = sc->sc_dev.dv_cfdata->cf_flags;
	sc->sc_state = 0;
	ncr53c9x_init(sc, 1);

	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = sc->sc_id;
	saa.saa_adapter = &ncr53c9x_switch;
	saa.saa_adapter_buswidth = sc->sc_ntarg;
	saa.saa_luns = 8;
	saa.saa_openings = 2;
	saa.saa_pool = &ecb_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);
}

/*
 * This is the generic ncr53c9x reset function. It does not reset the SCSI bus,
 * only this controller, but kills any on-going commands, and also stops
 * and resets the DMA.
 *
 * After reset, registers are loaded with the defaults from the attach
 * routine above.
 */
void
ncr53c9x_reset(struct ncr53c9x_softc *sc)
{

	/* reset DMA first */
	NCRDMA_RESET(sc);

	/* reset SCSI chip */
	NCRCMD(sc, NCRCMD_RSTCHIP);
	NCRCMD(sc, NCRCMD_NOP);
	DELAY(500);

	/* do these backwards, and fall through */
	switch (sc->sc_rev) {
	case NCR_VARIANT_ESP406:
	case NCR_VARIANT_FAS408:
		NCR_WRITE_REG(sc, NCR_CFG5, sc->sc_cfg5 | NCRCFG5_SINT);
		NCR_WRITE_REG(sc, NCR_CFG4, sc->sc_cfg4);
	case NCR_VARIANT_AM53C974:
	case NCR_VARIANT_FAS216:
	case NCR_VARIANT_NCR53C94:
	case NCR_VARIANT_NCR53C96:
	case NCR_VARIANT_ESP200:
		sc->sc_features |= NCR_F_HASCFG3;
		NCR_WRITE_REG(sc, NCR_CFG3, sc->sc_cfg3);
	case NCR_VARIANT_ESP100A:
		sc->sc_features |= NCR_F_SELATN3;
		NCR_WRITE_REG(sc, NCR_CFG2, sc->sc_cfg2);
	case NCR_VARIANT_ESP100:
		NCR_WRITE_REG(sc, NCR_CFG1, sc->sc_cfg1);
		NCR_WRITE_REG(sc, NCR_CCF, sc->sc_ccf);
		NCR_WRITE_REG(sc, NCR_SYNCOFF, 0);
		NCR_WRITE_REG(sc, NCR_TIMEOUT, sc->sc_timeout);
		break;
	case NCR_VARIANT_FAS366:
		sc->sc_features |=
		    NCR_F_SELATN3 | NCR_F_HASCFG3 | NCR_F_FASTSCSI;
		sc->sc_cfg3 = NCRFASCFG3_FASTCLK | NCRFASCFG3_OBAUTO;
		sc->sc_cfg3_fscsi = NCRFASCFG3_FASTSCSI;
		NCR_WRITE_REG(sc, NCR_CFG3, sc->sc_cfg3);
		sc->sc_cfg2 = 0; /* NCRCFG2_HMEFE | NCRCFG2_HME32 */
		NCR_WRITE_REG(sc, NCR_CFG2, sc->sc_cfg2);
		NCR_WRITE_REG(sc, NCR_CFG1, sc->sc_cfg1);
		NCR_WRITE_REG(sc, NCR_CCF, sc->sc_ccf);
		NCR_WRITE_REG(sc, NCR_SYNCOFF, 0);
		NCR_WRITE_REG(sc, NCR_TIMEOUT, sc->sc_timeout);
		break;
	default:
		printf("%s: unknown revision code, assuming ESP100\n",
		    sc->sc_dev.dv_xname);
		NCR_WRITE_REG(sc, NCR_CFG1, sc->sc_cfg1);
		NCR_WRITE_REG(sc, NCR_CCF, sc->sc_ccf);
		NCR_WRITE_REG(sc, NCR_SYNCOFF, 0);
		NCR_WRITE_REG(sc, NCR_TIMEOUT, sc->sc_timeout);
	}

	if (sc->sc_rev == NCR_VARIANT_AM53C974)
		NCR_WRITE_REG(sc, NCR_AMDCFG4, sc->sc_cfg4);

#if 0
	printf("%s: ncr53c9x_reset: revision %d\n",
	    sc->sc_dev.dv_xname, sc->sc_rev);
	printf("%s: ncr53c9x_reset: cfg1 0x%x, cfg2 0x%x, cfg3 0x%x, ccf 0x%x, timeout 0x%x\n",
	    sc->sc_dev.dv_xname,
	    sc->sc_cfg1, sc->sc_cfg2, sc->sc_cfg3,
	    sc->sc_ccf, sc->sc_timeout);
#endif
}

/*
 * Reset the SCSI bus, but not the chip
 */
void
ncr53c9x_scsi_reset(struct ncr53c9x_softc *sc)
{

	(*sc->sc_glue->gl_dma_stop)(sc);

	printf("%s: resetting SCSI bus\n", sc->sc_dev.dv_xname);
	NCRCMD(sc, NCRCMD_RSTSCSI);
}

/*
 * Initialize ncr53c9x state machine
 */
void
ncr53c9x_init(struct ncr53c9x_softc *sc, int doreset)
{
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_linfo *li;
	int r, i;

	NCR_TRACE(("[NCR_INIT(%d) %d] ", doreset, sc->sc_state));

	if (!ecb_pool_initialized) {
		/* All instances share this pool */
		pool_init(&ecb_pool, sizeof(struct ncr53c9x_ecb), 0, IPL_BIO,
		    0, "ncr53c9x_ecb", NULL);
		scsi_iopool_init(&ecb_iopool, NULL,
		    ncr53c9x_get_ecb, ncr53c9x_free_ecb);
		ecb_pool_initialized = 1;
	}

	if (sc->sc_state == 0) {
		/* First time through; initialize. */

		TAILQ_INIT(&sc->ready_list);
		sc->sc_nexus = NULL;
		bzero(sc->sc_tinfo, sizeof(sc->sc_tinfo));
		for (r = 0; r < sc->sc_ntarg; r++) {
			LIST_INIT(&sc->sc_tinfo[r].luns);
		}
	} else {
		/* Cancel any active commands. */
		sc->sc_state = NCR_CLEANING;
		sc->sc_msgify = 0;
		if ((ecb = sc->sc_nexus) != NULL) {
			ecb->xs->error = XS_TIMEOUT;
			ncr53c9x_done(sc, ecb);
		}
		for (r = 0; r < sc->sc_ntarg; r++) {
			LIST_FOREACH(li, &sc->sc_tinfo[r].luns, link) {
				if ((ecb = li->untagged)) {
					li->untagged = NULL;
					/*
					 * XXXXXXX
					 * Should we terminate a command
					 * that never reached the disk?
					 */
					li->busy = 0;
					ecb->xs->error = XS_TIMEOUT;
					ncr53c9x_done(sc, ecb);
				}
				for (i = 0; i < 256; i++)
					if ((ecb = li->queued[i])) {
						li->queued[i] = NULL;
						ecb->xs->error = XS_TIMEOUT;
						ncr53c9x_done(sc, ecb);
					}
				li->used = 0;
			}
		}
	}

	/*
	 * reset the chip to a known state
	 */
	ncr53c9x_reset(sc);

	sc->sc_phase = sc->sc_prevphase = INVALID_PHASE;
	for (r = 0; r < sc->sc_ntarg; r++) {
		struct ncr53c9x_tinfo *ti = &sc->sc_tinfo[r];
/* XXX - config flags per target: low bits: no reselect; high bits: no synch */

                ti->flags = ((!(sc->sc_cfflags & (1 << (r + 16))) &&
		    sc->sc_minsync) ? 0 : T_SYNCHOFF) |
		    ((sc->sc_cfflags & (1 << r)) ?  T_RSELECTOFF : 0) |
		    T_NEED_TO_RESET;
#ifdef DEBUG
		 if (ncr53c9x_notag)
			 ti->flags &= ~T_TAG;
#endif
		ti->period = sc->sc_minsync;
		ti->offset = 0;
		ti->cfg3 = 0;
	}

	if (doreset) {
		sc->sc_state = NCR_SBR;
		NCRCMD(sc, NCRCMD_RSTSCSI);
		/*
		 * XXX gross...
		 * On some systems, commands issued too close to a reset
		 * do not work correctly. We'll force a short delay on
		 * known-to-be-sensitive chips.
		 */
		switch (sc->sc_rev) {
		case NCR_VARIANT_NCR53C94:
			DELAY(600000);	/* 600ms */
			break;
		case NCR_VARIANT_NCR53C96:
			DELAY(100000);	/* 100ms */
			break;
		}
	} else {
		sc->sc_state = NCR_IDLE;
		ncr53c9x_sched(sc);
	}
}

/*
 * Read the NCR registers, and save their contents for later use.
 * NCR_STAT, NCR_STEP & NCR_INTR are mostly zeroed out when reading
 * NCR_INTR - so make sure it is the last read.
 *
 * I think that (from reading the docs) most bits in these registers
 * only make sense when he DMA CSR has an interrupt showing. Call only
 * if an interrupt is pending.
 */
__inline__ void
ncr53c9x_readregs(struct ncr53c9x_softc *sc)
{

	sc->sc_espstat = NCR_READ_REG(sc, NCR_STAT);
	/* Only the stepo bits are of interest */
	sc->sc_espstep = NCR_READ_REG(sc, NCR_STEP) & NCRSTEP_MASK;

	if (sc->sc_rev == NCR_VARIANT_FAS366)
		sc->sc_espstat2 = NCR_READ_REG(sc, NCR_STAT2);

	sc->sc_espintr = NCR_READ_REG(sc, NCR_INTR);

	if (sc->sc_glue->gl_clear_latched_intr != NULL)
		(*sc->sc_glue->gl_clear_latched_intr)(sc);

	/*
	 * Determine the SCSI bus phase, return either a real SCSI bus phase
	 * or some pseudo phase we use to detect certain exceptions.
	 */

	sc->sc_phase = (sc->sc_espintr & NCRINTR_DIS)
			? /* Disconnected */ BUSFREE_PHASE
			: sc->sc_espstat & NCRSTAT_PHASE;

	NCR_MISC(("regs[intr=%02x,stat=%02x,step=%02x,stat2=%02x] ",
		sc->sc_espintr, sc->sc_espstat, sc->sc_espstep, sc->sc_espstat2));
}

/*
 * Convert Synchronous Transfer Period to chip register Clock Per Byte value.
 */
static inline int
ncr53c9x_stp2cpb(struct ncr53c9x_softc *sc, int period)
{
	int v;
	v = (sc->sc_freq * period) / 250;
	if (ncr53c9x_cpb2stp(sc, v) < period)
		/* Correct round-down error */
		v++;
	return (v);
}

static inline void
ncr53c9x_setsync(struct ncr53c9x_softc *sc, struct ncr53c9x_tinfo *ti)
{
	u_char syncoff, synctp;
	u_char cfg3 = sc->sc_cfg3 | ti->cfg3;

	if (ti->flags & T_SYNCMODE) {
		syncoff = ti->offset;
		synctp = ncr53c9x_stp2cpb(sc, ti->period);
		if (sc->sc_features & NCR_F_FASTSCSI) {
			/*
			 * If the period is 200ns or less (ti->period <= 50),
			 * put the chip in Fast SCSI mode.
			 */
			if (ti->period <= 50)
				/*
				 * There are (at least) 4 variations of the
				 * configuration 3 register.  The drive attach
				 * routine sets the appropriate bit to put the
				 * chip into Fast SCSI mode so that it doesn't
				 * have to be figured out here each time.
				 */
				cfg3 |= (sc->sc_rev == NCR_VARIANT_AM53C974) ?
				    NCRAMDCFG3_FSCSI : NCRCFG3_FSCSI;
		}

		/*
		 * Am53c974 requires different SYNCTP values when the
		 * FSCSI bit is off.
		 */
		if (sc->sc_rev == NCR_VARIANT_AM53C974 &&
		    (cfg3 & NCRAMDCFG3_FSCSI) == 0)
			synctp--;
	} else {
		syncoff = 0;
		synctp = 0;
	}

	if (sc->sc_features & NCR_F_HASCFG3)
		NCR_WRITE_REG(sc, NCR_CFG3, cfg3);

	NCR_WRITE_REG(sc, NCR_SYNCOFF, syncoff);
	NCR_WRITE_REG(sc, NCR_SYNCTP, synctp);
}

/*
 * Send a command to a target, set the driver state to NCR_SELECTING
 * and let the caller take care of the rest.
 *
 * Keeping this as a function allows me to say that this may be done
 * by DMA instead of programmed I/O soon.
 */
void
ncr53c9x_select(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	struct scsi_link *sc_link = ecb->xs->sc_link;
	int target = sc_link->target;
	int lun = sc_link->lun;
	struct ncr53c9x_tinfo *ti = &sc->sc_tinfo[target];
	int tiflags = ti->flags;
	u_char *cmd;
	int clen;
	int selatn3, selatns;
	size_t dmasize;

	NCR_TRACE(("[ncr53c9x_select(t%d,l%d,cmd:%x,tag%x,%x)] ",
		   target, lun, ecb->cmd.cmd.opcode, ecb->tag[0], ecb->tag[1]));

	sc->sc_state = NCR_SELECTING;
	/*
	 * Schedule the timeout now, the first time we will go away
	 * expecting to come back due to an interrupt, because it is
	 * always possible that the interrupt may never happen.
	 */
	if ((ecb->xs->flags & SCSI_POLL) == 0) {
		int timeout = ecb->xs->timeout;

		if (timeout > 1000000)
			timeout = (timeout / 1000) * hz;
		else
			timeout = (timeout * hz) / 1000;
		timeout_add(&ecb->xs->stimeout, timeout);
	}

	/*
	 * The docs say the target register is never reset, and I
	 * can't think of a better place to set it
	 */
	if (sc->sc_rev == NCR_VARIANT_FAS366) {
		NCRCMD(sc, NCRCMD_FLUSH);
		NCR_WRITE_REG(sc, NCR_SELID, target | NCR_BUSID_HME);
	} else {
		NCR_WRITE_REG(sc, NCR_SELID, target);
	}
	ncr53c9x_setsync(sc, ti);

	if ((ecb->flags & ECB_SENSE) != 0) {
		/*
		 * For REQUEST SENSE, we should not send an IDENTIFY or
		 * otherwise mangle the target.  There should be no MESSAGE IN
		 * phase.
		 */
		if (sc->sc_features & NCR_F_DMASELECT) {
			/* setup DMA transfer for command */
			dmasize = clen = ecb->clen;
			sc->sc_cmdlen = clen;
			sc->sc_cmdp = (caddr_t)&ecb->cmd.cmd;

			NCRDMA_SETUP(sc, &sc->sc_cmdp, &sc->sc_cmdlen, 0,
			    &dmasize);
			/* Program the SCSI counter */
			NCR_SET_COUNT(sc, dmasize);

			if (sc->sc_rev != NCR_VARIANT_FAS366)
				NCRCMD(sc, NCRCMD_NOP|NCRCMD_DMA);

			/* And get the targets attention */
			NCRCMD(sc, NCRCMD_SELNATN | NCRCMD_DMA);
			NCRDMA_GO(sc);
		} else {
			ncr53c9x_wrfifo(sc, (u_char *)&ecb->cmd.cmd, ecb->clen);
			sc->sc_cmdlen = 0;
			NCRCMD(sc, NCRCMD_SELNATN);
		}
		return;
	}

	selatn3 = selatns = 0;
	if (ecb->tag[0] != 0) {
		if (sc->sc_features & NCR_F_SELATN3)
			/* use SELATN3 to send tag messages */
			selatn3 = 1;
		else
			/* We don't have SELATN3; use SELATNS to send tags */
			selatns = 1;
	}

	if (ti->flags & T_NEGOTIATE) {
		/* We have to use SELATNS to send sync/wide messages */
		selatn3 = 0;
		selatns = 1;
	}

	cmd = (u_char *)&ecb->cmd.cmd;

	if (selatn3) {
		/* We'll use tags with SELATN3 */
		clen = ecb->clen + 3;
		cmd -= 3;
		cmd[0] = MSG_IDENTIFY(lun, 1);	/* msg[0] */
		cmd[1] = ecb->tag[0];		/* msg[1] */
		cmd[2] = ecb->tag[1];		/* msg[2] */
	} else {
		/* We don't have tags, or will send messages with SELATNS */
		clen = ecb->clen + 1;
		cmd -= 1;
		cmd[0] = MSG_IDENTIFY(lun, (tiflags & T_RSELECTOFF) == 0);
	}

	if ((sc->sc_features & NCR_F_DMASELECT) && !selatns) {

		/* setup DMA transfer for command */
		dmasize = clen;
		sc->sc_cmdlen = clen;
		sc->sc_cmdp = cmd;

		NCRDMA_SETUP(sc, &sc->sc_cmdp, &sc->sc_cmdlen, 0, &dmasize);
		/* Program the SCSI counter */
		NCR_SET_COUNT(sc, dmasize);

		/* load the count in */
		/* if (sc->sc_rev != NCR_VARIANT_FAS366) */
			NCRCMD(sc, NCRCMD_NOP|NCRCMD_DMA);

		/* And get the targets attention */
		if (selatn3) {
			sc->sc_msgout = SEND_TAG;
			sc->sc_flags |= NCR_ATN;
			NCRCMD(sc, NCRCMD_SELATN3 | NCRCMD_DMA);
		} else
			NCRCMD(sc, NCRCMD_SELATN | NCRCMD_DMA);
		NCRDMA_GO(sc);
		return;
	}

	/*
	 * Who am I. This is where we tell the target that we are
	 * happy for it to disconnect etc.
	 */

	/* Now get the command into the FIFO */
	sc->sc_cmdlen = 0;
	ncr53c9x_wrfifo(sc, cmd, clen);

	/* And get the targets attention */
	if (selatns) {
		NCR_MISC(("SELATNS \n"));
		/* Arbitrate, select and stop after IDENTIFY message */
		NCRCMD(sc, NCRCMD_SELATNS);
	} else if (selatn3) {
		sc->sc_msgout = SEND_TAG;
		sc->sc_flags |= NCR_ATN;
		NCRCMD(sc, NCRCMD_SELATN3);
	} else
		NCRCMD(sc, NCRCMD_SELATN);
}

/*
 * DRIVER FUNCTIONS CALLABLE FROM HIGHER LEVEL DRIVERS
 */

void *
ncr53c9x_get_ecb(void *null)
{
	struct ncr53c9x_ecb *ecb;

	ecb = pool_get(&ecb_pool, M_NOWAIT|M_ZERO);
	if (ecb == NULL)
		return (NULL);

	ecb->flags |= ECB_ALLOC;

	return (ecb);
}

void
ncr53c9x_free_ecb(void *null, void *ecb)
{
	pool_put(&ecb_pool, ecb);
}

int
ncr53c9x_scsi_probe(struct scsi_link *sc_link)
{
	struct ncr53c9x_softc *sc = sc_link->bus->sb_adapter_softc;
	struct ncr53c9x_tinfo *ti = &sc->sc_tinfo[sc_link->target];
	struct ncr53c9x_linfo *li;
	int64_t lun = sc_link->lun;
	int s;

	/* Initialize LUN info and add to list. */
	li = malloc(sizeof(*li), M_DEVBUF, M_WAITOK | M_ZERO);
	if (li == NULL)
		return (ENOMEM);

	li->last_used = getuptime();
	li->lun = lun;

	s = splbio();
	LIST_INSERT_HEAD(&ti->luns, li, link);
	if (lun < NCR_NLUN)
		ti->lun[lun] = li;
	splx(s);

	return (0);

}

void
ncr53c9x_scsi_free(struct scsi_link *sc_link)
{
	struct ncr53c9x_softc *sc = sc_link->bus->sb_adapter_softc;
	struct ncr53c9x_tinfo *ti = &sc->sc_tinfo[sc_link->target];
	struct ncr53c9x_linfo *li;
	int64_t lun = sc_link->lun;
	int s;

	s = splbio();
	li = TINFO_LUN(ti, lun);

	LIST_REMOVE(li, link);
	if (lun < NCR_NLUN)
		ti->lun[lun] = NULL;
	splx(s);

	free(li, M_DEVBUF, 0);
}

/*
 * Start a SCSI-command
 * This function is called by the higher level SCSI-driver to queue/run
 * SCSI-commands.
 */
void
ncr53c9x_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *sc_link = xs->sc_link;
	struct ncr53c9x_softc *sc = sc_link->bus->sb_adapter_softc;
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_tinfo *ti;
	struct ncr53c9x_linfo *li;
	int64_t lun = sc_link->lun;
	int s, flags;

	NCR_TRACE(("[ncr53c9x_scsi_cmd] "));
	NCR_CMDS(("[0x%x, %d]->%d ", (int)xs->cmd.opcode, xs->cmdlen,
	    sc_link->target));

	/*
	 * Commands larger than 12 bytes seem to confuse the chip
	 * (at least on FAS366 flavours).
	 */
	if (xs->cmdlen > 12) {
		memset(&xs->sense, 0, sizeof(xs->sense));
		/* sense data borrowed from gdt(4) */
		xs->sense.error_code = SSD_ERRCODE_VALID | SSD_ERRCODE_CURRENT;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20; /* illcmd */
		xs->error = XS_SENSE;
		scsi_done(xs);
		return;
	}

	flags = xs->flags;
	ti = &sc->sc_tinfo[sc_link->target];
	li = TINFO_LUN(ti, lun);

	/* Initialize ecb */
	ecb = xs->io;
	ecb->xs = xs;
	timeout_set(&xs->stimeout, ncr53c9x_timeout, ecb);

	if (flags & SCSI_RESET) {
		ecb->flags |= ECB_RESET;
		ecb->clen = 0;
		ecb->dleft = 0;
	} else {
		bcopy(&xs->cmd, &ecb->cmd.cmd, xs->cmdlen);
		ecb->clen = xs->cmdlen;
		ecb->daddr = xs->data;
		ecb->dleft = xs->datalen;
	}
	ecb->stat = 0;

	s = splbio();

	TAILQ_INSERT_TAIL(&sc->ready_list, ecb, chain);
	ecb->flags |= ECB_READY;
	if (sc->sc_state == NCR_IDLE)
		ncr53c9x_sched(sc);

	splx(s);

	if ((flags & SCSI_POLL) == 0)
		return;

	/* Not allowed to use interrupts, use polling instead */
	if (ncr53c9x_poll(sc, xs, xs->timeout)) {
		ncr53c9x_timeout(ecb);
		if (ncr53c9x_poll(sc, xs, xs->timeout))
			ncr53c9x_timeout(ecb);
	}
}

/*
 * Used when interrupt driven I/O isn't allowed, e.g. during boot.
 */
int
ncr53c9x_poll(struct ncr53c9x_softc *sc, struct scsi_xfer *xs, int count)
{
	int s;

	NCR_TRACE(("[ncr53c9x_poll] "));
	while (count) {
		if (NCRDMA_ISINTR(sc)) {
			s = splbio();
			ncr53c9x_intr(sc);
			splx(s);
		}
#if alternatively
		if (NCR_READ_REG(sc, NCR_STAT) & NCRSTAT_INT) {
			s = splbio();
			ncr53c9x_intr(sc);
			splx(s);
		}
#endif
		if ((xs->flags & ITSDONE) != 0)
			return (0);
		s = splbio();
		if (sc->sc_state == NCR_IDLE) {
			NCR_TRACE(("[ncr53c9x_poll: rescheduling] "));
			ncr53c9x_sched(sc);
		}
		splx(s);
		DELAY(1000);
		count--;
	}
	return (1);
}


/*
 * LOW LEVEL SCSI UTILITIES
 */

/*
 * Schedule a scsi operation.  This has now been pulled out of the interrupt
 * handler so that we may call it from ncr53c9x_scsi_cmd and ncr53c9x_done.
 * This may save us an unnecessary interrupt just to get things going.
 * Should only be called when state == NCR_IDLE and at bio pl.
 */
void
ncr53c9x_sched(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_ecb *ecb;
	struct scsi_link *sc_link;
	struct ncr53c9x_tinfo *ti;
	int lun;
	struct ncr53c9x_linfo *li;
	int s, tag;

	NCR_TRACE(("[ncr53c9x_sched] "));
	if (sc->sc_state != NCR_IDLE)
		panic("ncr53c9x_sched: not IDLE (state=%d)", sc->sc_state);

	/*
	 * Find first ecb in ready queue that is for a target/lunit
	 * combinations that is not busy.
	 */
	TAILQ_FOREACH(ecb, &sc->ready_list, chain) {
		sc_link = ecb->xs->sc_link;
		ti = &sc->sc_tinfo[sc_link->target];
		lun = sc_link->lun;

		/* Select type of tag for this command */
		if ((ti->flags & (T_RSELECTOFF)) != 0)
			tag = 0;
		else if ((ti->flags & T_TAG) == 0)
			tag = 0;
		else if ((ecb->flags & ECB_SENSE) != 0)
			tag = 0;
		else
			tag = MSG_SIMPLE_Q_TAG;
#if 0
		/* XXXX Use tags for polled commands? */
		if (ecb->xs->flags & SCSI_POLL)
			tag = 0;
#endif
		s = splbio();
		li = TINFO_LUN(ti, lun);
		if (!li) {
			/* Initialize LUN info and add to list. */
			if ((li = malloc(sizeof(*li), M_DEVBUF,
			    M_NOWAIT | M_ZERO)) == NULL) {
				splx(s);
				continue;
			}
			li->lun = lun;

			LIST_INSERT_HEAD(&ti->luns, li, link);
			if (lun < NCR_NLUN)
				ti->lun[lun] = li;
		}
		li->last_used = getuptime();
		if (!tag) {
			/* Try to issue this as an un-tagged command */
			if (!li->untagged)
				li->untagged = ecb;
		}
		if (li->untagged) {
			tag = 0;
			if ((li->busy != 1) && !li->used) {
				/* We need to issue this untagged command now */
				ecb = li->untagged;
				sc_link = ecb->xs->sc_link;
			}
			else {
				/* Not ready yet */
				splx(s);
				continue;
			}
		}
		ecb->tag[0] = tag;
		if (tag) {
			int i;

			/* Allocate a tag */
			if (li->used == 255) {
				/* no free tags */
				splx(s);
				continue;
			}
			/* Start from the last used location */
			for (i=li->avail; i<256; i++) {
				if (li->queued[i] == NULL)
					break;
			}
			/* Couldn't find one, start again from the beginning */
			if (i == 256) {
				for (i = 0; i<256; i++) {
					if (li->queued[i] == NULL)
						break;
				}
			}
#ifdef DIAGNOSTIC
			if (i == 256)
				panic("ncr53c9x_sched: tag alloc failure");
#endif

			/* Save where to start next time. */
			li->avail = i+1;
			li->used++;

			li->queued[i] = ecb;
			ecb->tag[1] = i;
		}
		splx(s);
		if (li->untagged && (li->busy != 1)) {
			li->busy = 1;
			TAILQ_REMOVE(&sc->ready_list, ecb, chain);
			ecb->flags &= ~ECB_READY;
			sc->sc_nexus = ecb;
			ncr53c9x_select(sc, ecb);
			break;
		}
		if (!li->untagged && tag) {
			TAILQ_REMOVE(&sc->ready_list, ecb, chain);
			ecb->flags &= ~ECB_READY;
			sc->sc_nexus = ecb;
			ncr53c9x_select(sc, ecb);
			break;
		} else
			NCR_MISC(("%d:%d busy\n",
				  sc_link->target,
				  sc_link->lun));
	}
}

void
ncr53c9x_sense(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	struct scsi_xfer *xs = ecb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct ncr53c9x_tinfo *ti = &sc->sc_tinfo[sc_link->target];
	struct scsi_sense *ss = (void *)&ecb->cmd.cmd;
	struct ncr53c9x_linfo *li;
	int lun = sc_link->lun;

	NCR_MISC(("requesting sense "));
	/* Next, setup a request sense command block */
	bzero(ss, sizeof(*ss));
	ss->opcode = REQUEST_SENSE;
	ss->byte2 = sc_link->lun << 5;
	ss->length = sizeof(struct scsi_sense_data);
	ecb->clen = sizeof(*ss);
	ecb->daddr = (char *)&xs->sense;
	ecb->dleft = sizeof(struct scsi_sense_data);
	ecb->flags |= ECB_SENSE;
	xs->timeout = NCR_SENSE_TIMEOUT;
	ti->senses++;
	li = TINFO_LUN(ti, lun);
	if (li->busy) li->busy = 0;
	ncr53c9x_dequeue(sc, ecb);
	li->untagged = ecb;
	li->busy = 2;
	if (ecb == sc->sc_nexus) {
		ncr53c9x_select(sc, ecb);
	} else {
		TAILQ_INSERT_HEAD(&sc->ready_list, ecb, chain);
		ecb->flags |= ECB_READY;
		if (sc->sc_state == NCR_IDLE)
			ncr53c9x_sched(sc);
	}
}

/*
 * POST PROCESSING OF SCSI_CMD (usually current)
 */
void
ncr53c9x_done(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	struct scsi_xfer *xs = ecb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct ncr53c9x_tinfo *ti = &sc->sc_tinfo[sc_link->target];
	int lun = sc_link->lun;
	struct ncr53c9x_linfo *li = TINFO_LUN(ti, lun);

	NCR_TRACE(("[ncr53c9x_done(error:%x)] ", xs->error));

	timeout_del(&ecb->xs->stimeout);

	if (ecb->stat == SCSI_QUEUE_FULL) {
		/*
		 * Set current throttle -- we should reset
		 * this periodically
		 */
		sc_link->openings = li->used - 1;
		printf("\n%s: QFULL -- throttling to %d commands\n",
		    sc->sc_dev.dv_xname, sc_link->openings);
	}

	/*
	 * Now, if we've come here with no error code, i.e. we've kept the
	 * initial XS_NOERROR, and the status code signals that we should
	 * check sense, we'll need to set up a request sense cmd block and
	 * push the command back into the ready queue *before* any other
	 * commands for this target/lunit, else we lose the sense info.
	 * We don't support chk sense conditions for the request sense cmd.
	 */
	if (xs->error == XS_NOERROR) {
		xs->status = ecb->stat;
		if ((ecb->flags & ECB_ABORT) != 0) {
			xs->error = XS_TIMEOUT;
		} else if ((ecb->flags & ECB_SENSE) != 0) {
			xs->error = XS_SENSE;
		} else if ((ecb->stat & ST_MASK) == SCSI_CHECK) {
			/* First, save the return values */
			xs->resid = ecb->dleft;
			ncr53c9x_sense(sc, ecb);
			return;
		} else {
			xs->resid = ecb->dleft;
		}
	}

#ifdef NCR53C9X_DEBUG
	if (ncr53c9x_debug & NCR_SHOWMISC) {
		if (xs->resid != 0)
			printf("resid=%lu ", (unsigned long)xs->resid);
		if (xs->error == XS_SENSE)
			printf("sense=0x%02x\n", xs->sense.error_code);
		else
			printf("error=%d\n", xs->error);
	}
#endif

	/*
	 * Remove the ECB from whatever queue it's on.
	 */
	ncr53c9x_dequeue(sc, ecb);
	if (ecb == sc->sc_nexus) {
		sc->sc_nexus = NULL;
		if (sc->sc_state != NCR_CLEANING) {
			sc->sc_state = NCR_IDLE;
			ncr53c9x_sched(sc);
		}
	}

	ti->cmds++;
	scsi_done(xs);
}

void
ncr53c9x_dequeue(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	struct ncr53c9x_tinfo *ti =
	    &sc->sc_tinfo[ecb->xs->sc_link->target];
	struct ncr53c9x_linfo *li;
	int64_t lun = ecb->xs->sc_link->lun;

	li = TINFO_LUN(ti, lun);
#ifdef DIAGNOSTIC
	if ((!li) || (li->lun != lun))
		panic("ncr53c9x_dequeue: lun %llx for ecb %p does not exist",
		    (long long)lun, ecb);
#endif
	if (li->untagged == ecb) {
		li->busy = 0;
		li->untagged = NULL;
	}
	if (ecb->tag[0] && li->queued[ecb->tag[1]]) {
#ifdef DIAGNOSTIC
		if (li->queued[ecb->tag[1]] && (li->queued[ecb->tag[1]] != ecb))
			panic("ncr53c9x_dequeue: slot %d for lun %llx has %p "
			    "instead of ecb %p", ecb->tag[1],
			    (long long)lun,
			    li->queued[ecb->tag[1]], ecb);
#endif
		li->queued[ecb->tag[1]] = NULL;
		li->used --;

	}
	if (ecb->flags & ECB_READY) {
		ecb->flags &= ~ECB_READY;
		TAILQ_REMOVE(&sc->ready_list, ecb, chain);
	}
 }

/*
 * INTERRUPT/PROTOCOL ENGINE
 */

/*
 * Schedule an outgoing message by prioritizing it, and asserting
 * attention on the bus. We can only do this when we are the initiator
 * else there will be an illegal command interrupt.
 */
#define ncr53c9x_sched_msgout(m) \
	do {							\
		NCR_MISC(("ncr53c9x_sched_msgout %x %d ", m, __LINE__));	\
		NCRCMD(sc, NCRCMD_SETATN);			\
		sc->sc_flags |= NCR_ATN;			\
		sc->sc_msgpriq |= (m);				\
	} while (0)

static void
ncr53c9x_flushfifo(struct ncr53c9x_softc *sc)
{
	NCR_MISC(("[flushfifo] "));

	NCRCMD(sc, NCRCMD_FLUSH);

	if (sc->sc_phase == COMMAND_PHASE ||
	    sc->sc_phase == MESSAGE_OUT_PHASE)
		DELAY(2);
}

static int
ncr53c9x_rdfifo(struct ncr53c9x_softc *sc, int how)
{
	int i, n;
	u_char *buf;

	switch(how) {
	case NCR_RDFIFO_START:
		buf = sc->sc_imess;
		sc->sc_imlen = 0;
		break;
	case NCR_RDFIFO_CONTINUE:
		buf = sc->sc_imess + sc->sc_imlen;
		break;
	default:
		panic("ncr53c9x_rdfifo: bad flag");
		break;
	}

	/*
	 * XXX buffer (sc_imess) size for message
	 */

	n = NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF;

	if (sc->sc_rev == NCR_VARIANT_FAS366) {
		n *= 2;

		for (i = 0; i < n; i++)
			buf[i] = NCR_READ_REG(sc, NCR_FIFO);

		if (sc->sc_espstat2 & FAS_STAT2_ISHUTTLE) {

			NCR_WRITE_REG(sc, NCR_FIFO, 0);
			buf[i++] = NCR_READ_REG(sc, NCR_FIFO);

			NCR_READ_REG(sc, NCR_FIFO);

			ncr53c9x_flushfifo(sc);
		}
	} else {
		for (i = 0; i < n; i++)
			buf[i] = NCR_READ_REG(sc, NCR_FIFO);
	}

	sc->sc_imlen += i;

#ifdef NCR53C9X_DEBUG
	{
		int j;

		NCR_TRACE(("\n[rdfifo %s (%d):",
		    (how == NCR_RDFIFO_START) ? "start" : "cont",
		    (int)sc->sc_imlen));
		if (ncr53c9x_debug & NCR_SHOWTRAC) {
			for (j = 0; j < sc->sc_imlen; j++)
				printf(" %02x", sc->sc_imess[j]);
			printf("]\n");
		}
	}
#endif
	return sc->sc_imlen;
}

static void
ncr53c9x_wrfifo(struct ncr53c9x_softc *sc, u_char *p, int len)
{
	int i;

#ifdef NCR53C9X_DEBUG
	NCR_MISC(("[wrfifo(%d):", len));
	if (ncr53c9x_debug & NCR_SHOWTRAC) {
		for (i = 0; i < len; i++)
			printf(" %02x", p[i]);
		printf("]\n");
	}
#endif

	for (i = 0; i < len; i++) {
		NCR_WRITE_REG(sc, NCR_FIFO, p[i]);

		if (sc->sc_rev == NCR_VARIANT_FAS366)
			NCR_WRITE_REG(sc, NCR_FIFO, 0);
	}
}

int
ncr53c9x_reselect(struct ncr53c9x_softc *sc, int message, int tagtype,
    int tagid)
{
	u_char selid, target, lun;
	struct ncr53c9x_ecb *ecb = NULL;
	struct ncr53c9x_tinfo *ti;
	struct ncr53c9x_linfo *li;

	if (sc->sc_rev == NCR_VARIANT_FAS366) {
		target = sc->sc_selid;
	} else {
		/*
		 * The SCSI chip made a snapshot of the data bus while the reselection
		 * was being negotiated.  This enables us to determine which target did
		 * the reselect.
		 */
		selid = sc->sc_selid & ~(1 << sc->sc_id);
		if (selid & (selid - 1)) {
			printf("%s: reselect with invalid selid %02x;"
			    " sending DEVICE RESET\n", sc->sc_dev.dv_xname, selid);
			goto reset;

		}
		target = ffs(selid) - 1;
	}
	lun = message & 0x07;

	/*
	 * Search wait queue for disconnected cmd
	 * The list should be short, so I haven't bothered with
	 * any more sophisticated structures than a simple
	 * singly linked list.
	 */
	ti = &sc->sc_tinfo[target];
	li = TINFO_LUN(ti, lun);

	/*
	 * We can get as far as the LUN with the IDENTIFY
	 * message.  Check to see if we're running an
	 * un-tagged command.  Otherwise ack the IDENTIFY
	 * and wait for a tag message.
	 */
	if (li != NULL) {
		if (li->untagged != NULL && li->busy)
			ecb = li->untagged;
		else if (tagtype != MSG_SIMPLE_Q_TAG) {
			/* Wait for tag to come by */
			sc->sc_state = NCR_IDENTIFIED;
			return (0);
		} else if (tagtype) ecb = li->queued[tagid];
	}
	if (ecb == NULL) {
		printf("%s: reselect from target %d lun %d tag %x:%x with no nexus;"
		    " sending ABORT\n", sc->sc_dev.dv_xname, target, lun, tagtype, tagid);
		goto abort;
	}

	/* Make this nexus active again. */
	sc->sc_state = NCR_CONNECTED;
	sc->sc_nexus = ecb;
	ncr53c9x_setsync(sc, ti);

	if (ecb->flags & ECB_RESET)
		ncr53c9x_sched_msgout(SEND_DEV_RESET);
	else if (ecb->flags & ECB_ABORT)
		ncr53c9x_sched_msgout(SEND_ABORT);

	/* Do an implicit RESTORE POINTERS. */
	sc->sc_dp = ecb->daddr;
	sc->sc_dleft = ecb->dleft;

	return (0);

reset:
	ncr53c9x_sched_msgout(SEND_DEV_RESET);
	return (1);

abort:
	ncr53c9x_sched_msgout(SEND_ABORT);
	return (1);
}

static inline int
__verify_msg_format(u_char *p, int len)
{

	if (len == 1 && IS1BYTEMSG(p[0]))
		return 1;
	if (len == 2 && IS2BYTEMSG(p[0]))
		return 1;
	if (len >= 3 && ISEXTMSG(p[0]) &&
	    len == p[1] + 2)
		return 1;

	return 0;
}

/*
 * Get an incoming message as initiator.
 *
 * The SCSI bus must already be in MESSAGE_IN_PHASE and there is a
 * byte in the FIFO
 */
void
ncr53c9x_msgin(struct ncr53c9x_softc *sc)
{

	NCR_TRACE(("[ncr53c9x_msgin(curmsglen:%ld)] ", (long)sc->sc_imlen));

	if (sc->sc_imlen == 0) {
		printf("%s: msgin: no msg byte available\n", sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Prepare for a new message.  A message should (according
	 * to the SCSI standard) be transmitted in one single
	 * MESSAGE_IN_PHASE. If we have been in some other phase,
	 * then this is a new message.
	 */
	if (sc->sc_prevphase != MESSAGE_IN_PHASE && sc->sc_state != NCR_RESELECTED) {
		printf("%s: phase change, dropping message, prev %d, state %d\n",
		    sc->sc_dev.dv_xname, sc->sc_prevphase, sc->sc_state);
		sc->sc_flags &= ~NCR_DROP_MSGI;
		sc->sc_imlen = 0;
	}

	NCR_TRACE(("<msgbyte:0x%02x>", sc->sc_imess[0]));

	/*
	 * If we're going to reject the message, don't bother storing
	 * the incoming bytes.  But still, we need to ACK them.
	 */
	if ((sc->sc_flags & NCR_DROP_MSGI)) {
		NCRCMD(sc, NCRCMD_MSGOK);
		printf("<dropping msg byte %x>",
			sc->sc_imess[sc->sc_imlen]);
		return;
	}

	if (sc->sc_imlen >= NCR_MAX_MSG_LEN) {
		ncr53c9x_sched_msgout(SEND_REJECT);
		sc->sc_flags |= NCR_DROP_MSGI;
	} else {
		u_char *pb;
		int plen;

		switch (sc->sc_state) {
			/*
			 * if received message is the first of reselection
			 * then first byte is selid, and then message
			 */
		case NCR_RESELECTED:
			pb = sc->sc_imess + 1;
			plen = sc->sc_imlen - 1;
			break;
		default:
			pb = sc->sc_imess;
			plen = sc->sc_imlen;
			break;
		}

		if (__verify_msg_format(pb, plen))
			goto gotit;
	}

	/* Ack what we have so far */
	NCRCMD(sc, NCRCMD_MSGOK);
	return;

gotit:
	NCR_MSGS(("gotmsg(%x) state %d", sc->sc_imess[0], sc->sc_state));
	/* we got complete message, flush the imess, XXX nobody uses imlen below */
	sc->sc_imlen = 0;

	/*
	 * Now we should have a complete message (1 byte, 2 byte
	 * and moderately long extended messages).  We only handle
	 * extended messages which total length is shorter than
	 * NCR_MAX_MSG_LEN.  Longer messages will be amputated.
	 */
	switch (sc->sc_state) {
		struct ncr53c9x_ecb *ecb;
		struct ncr53c9x_tinfo *ti;
		struct ncr53c9x_linfo *li;
		int lun;

	case NCR_CONNECTED:
		ecb = sc->sc_nexus;
		ti = &sc->sc_tinfo[ecb->xs->sc_link->target];

		switch (sc->sc_imess[0]) {
		case MSG_CMDCOMPLETE:
			NCR_MSGS(("cmdcomplete "));
			if (sc->sc_dleft < 0) {
				sc_print_addr(ecb->xs->sc_link);
				printf("got %ld extra bytes\n",
				    -(long)sc->sc_dleft);
				sc->sc_dleft = 0;
			}
			ecb->dleft = (ecb->flags & ECB_TENTATIVE_DONE)
				? 0
				: sc->sc_dleft;
			if ((ecb->flags & ECB_SENSE) == 0)
				ecb->xs->resid = ecb->dleft;
			sc->sc_state = NCR_CMDCOMPLETE;
			break;

		case MSG_MESSAGE_REJECT:
			NCR_MSGS(("msg reject (msgout=%x) ", sc->sc_msgout));
			switch (sc->sc_msgout) {
			case SEND_TAG:
				/* Target does not like tagged queuing.
				 *  - Flush the command queue
				 *  - Disable tagged queuing for the target
				 *  - Dequeue ecb from the queued array.
				 */
				printf("%s: tagged queuing rejected: target %d\n",
				    sc->sc_dev.dv_xname, ecb->xs->sc_link->target);

				NCR_MSGS(("(rejected sent tag)"));
				NCRCMD(sc, NCRCMD_FLUSH);
				DELAY(1);
				ti->flags &= ~T_TAG;
				lun = ecb->xs->sc_link->lun;
				li = TINFO_LUN(ti, lun);
				if (ecb->tag[0] &&
				    li->queued[ecb->tag[1]] != NULL) {
					li->queued[ecb->tag[1]] = NULL;
					li->used--;
				}
				ecb->tag[0] = ecb->tag[1] = 0;
				li->untagged = ecb;
				li->busy = 1;
				break;

			case SEND_SDTR:
				printf("%s: sync transfer rejected: target %d\n",
				    sc->sc_dev.dv_xname, ecb->xs->sc_link->target);
				sc->sc_flags &= ~NCR_SYNCHNEGO;
				ti->flags &= ~(T_NEGOTIATE | T_SYNCMODE);
				ncr53c9x_setsync(sc, ti);
				break;

			case SEND_WDTR:
				printf("%s: wide transfer rejected: target %d\n",
				    sc->sc_dev.dv_xname, ecb->xs->sc_link->target);
				ti->flags &= ~T_WIDE;
				ti->width = 0;
				break;

			case SEND_INIT_DET_ERR:
				goto abort;
			}
			break;

		case MSG_NOOP:
			NCR_MSGS(("noop "));
			break;

		case MSG_HEAD_OF_Q_TAG:
		case MSG_SIMPLE_Q_TAG:
		case MSG_ORDERED_Q_TAG:
			NCR_MSGS(("TAG %x:%x", sc->sc_imess[0], sc->sc_imess[1]));
			break;

		case MSG_DISCONNECT:
			NCR_MSGS(("disconnect "));
			ti->dconns++;
			sc->sc_state = NCR_DISCONNECT;

			/*
			 * Mark the fact that all bytes have moved. The
			 * target may not bother to do a SAVE POINTERS
			 * at this stage. This flag will set the residual
			 * count to zero on MSG COMPLETE.
			 */
			if (sc->sc_dleft == 0)
				ecb->flags |= ECB_TENTATIVE_DONE;

			break;

		case MSG_SAVEDATAPOINTER:
			NCR_MSGS(("save datapointer "));
			ecb->daddr = sc->sc_dp;
			ecb->dleft = sc->sc_dleft;
			break;

		case MSG_RESTOREPOINTERS:
			NCR_MSGS(("restore datapointer "));
			sc->sc_dp = ecb->daddr;
			sc->sc_dleft = ecb->dleft;
			break;

		case MSG_EXTENDED:
			NCR_MSGS(("extended(%x) ", sc->sc_imess[2]));
			switch (sc->sc_imess[2]) {
			case MSG_EXT_SDTR:
				NCR_MSGS(("SDTR period %d, offset %d ",
					sc->sc_imess[3], sc->sc_imess[4]));
				if (sc->sc_imess[1] != 3)
					goto reject;
				ti->period = sc->sc_imess[3];
				ti->offset = sc->sc_imess[4];
				ti->flags &= ~T_NEGOTIATE;
				if (sc->sc_minsync == 0 ||
				    ti->offset == 0 ||
				    ti->period > 124) {
#ifdef NCR53C9X_DEBUG
					sc_print_addr(ecb->xs->sc_link);
					printf("async mode\n");
#endif
					if ((sc->sc_flags&NCR_SYNCHNEGO)
					    == 0) {
						/*
						 * target initiated negotiation
						 */
						ti->offset = 0;
						ti->flags &= ~T_SYNCMODE;
						ncr53c9x_sched_msgout(
						    SEND_SDTR);
					} else {
						/* we are async */
						ti->flags &= ~T_SYNCMODE;
					}
				} else {
#ifdef NCR53C9X_DEBUG
					int r, s;
#endif
					int p;

					p = ncr53c9x_stp2cpb(sc, ti->period);
					ti->period = ncr53c9x_cpb2stp(sc, p);
#ifdef NCR53C9X_DEBUG
					sc_print_addr(ecb->xs->sc_link);
					r = 250/ti->period;
					s = (100*250)/ti->period - 100*r;
					printf("max sync rate %d.%02dMB/s\n",
						r, s);
#endif
					if ((sc->sc_flags&NCR_SYNCHNEGO) == 0) {
						/*
						 * target initiated negotiation
						 */
						if (ti->period <
						    sc->sc_minsync)
							ti->period =
							    sc->sc_minsync;
						if (ti->offset > 15)
							ti->offset = 15;
						ti->flags &= ~T_SYNCMODE;
						ncr53c9x_sched_msgout(
						    SEND_SDTR);
					} else {
						/* we are sync */
						ti->flags |= T_SYNCMODE;
					}
				}
				sc->sc_flags &= ~NCR_SYNCHNEGO;
				ncr53c9x_setsync(sc, ti);
				break;

			case MSG_EXT_WDTR:
				printf("%s: wide mode %d\n",
				    sc->sc_dev.dv_xname, sc->sc_imess[3]);
				if (sc->sc_imess[3] == 1) {
					ti->cfg3 |= NCRFASCFG3_EWIDE;
					ncr53c9x_setsync(sc, ti);
				} else
					ti->width = 0;
				ti->flags &= ~T_WIDE;
				break;
			default:
				sc_print_addr(ecb->xs->sc_link);
				printf("unrecognized MESSAGE EXTENDED;"
				    " sending REJECT\n");
				goto reject;
			}
			break;

		default:
			NCR_MSGS(("ident "));
			sc_print_addr(ecb->xs->sc_link);
			printf("unrecognized MESSAGE; sending REJECT\n");
		reject:
			ncr53c9x_sched_msgout(SEND_REJECT);
			break;
		}
		break;

	case NCR_IDENTIFIED:
		/*
		 * IDENTIFY message was received and queue tag is expected now
		 */
		if ((sc->sc_imess[0] != MSG_SIMPLE_Q_TAG) ||
		    (sc->sc_msgify == 0)) {
			printf("%s: TAG reselect without IDENTIFY;"
			    " MSG %x;"
			    " sending DEVICE RESET\n",
			    sc->sc_dev.dv_xname,
			    sc->sc_imess[0]);
			goto reset;
		}
		(void) ncr53c9x_reselect(sc, sc->sc_msgify,
		    sc->sc_imess[0], sc->sc_imess[1]);
		break;

	case NCR_RESELECTED:
		if (MSG_ISIDENTIFY(sc->sc_imess[1])) {
			sc->sc_msgify = sc->sc_imess[1];
		} else {
			printf("%s: reselect without IDENTIFY;"
			    " MSG %x;"
			    " sending DEVICE RESET\n",
			    sc->sc_dev.dv_xname,
			    sc->sc_imess[1]);
			goto reset;
		}
		(void) ncr53c9x_reselect(sc, sc->sc_msgify, 0, 0);
		break;

	default:
		printf("%s: unexpected MESSAGE IN; sending DEVICE RESET\n",
		    sc->sc_dev.dv_xname);
	reset:
		ncr53c9x_sched_msgout(SEND_DEV_RESET);
		break;

	abort:
		ncr53c9x_sched_msgout(SEND_ABORT);
		break;
	}

	/* if we have more messages to send set ATN */
	if (sc->sc_msgpriq)
		NCRCMD(sc, NCRCMD_SETATN);

	/* Ack last message byte */
	NCRCMD(sc, NCRCMD_MSGOK);

	/* Done, reset message pointer. */
	sc->sc_flags &= ~NCR_DROP_MSGI;
	sc->sc_imlen = 0;
}


/*
 * Send the highest priority, scheduled message
 */
void
ncr53c9x_msgout(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_tinfo *ti;
	struct ncr53c9x_ecb *ecb;
	size_t size;

	NCR_TRACE(("[ncr53c9x_msgout(priq:%x, prevphase:%x)]",
	    sc->sc_msgpriq, sc->sc_prevphase));

	/*
	 * XXX - the NCR_ATN flag is not in sync with the actual ATN
	 *	 condition on the SCSI bus. The 53c9x chip
	 *	 automatically turns off ATN before sending the
	 *	 message byte.  (see also the comment below in the
	 *	 default case when picking out a message to send)
	 */
	if (sc->sc_flags & NCR_ATN) {
		if (sc->sc_prevphase != MESSAGE_OUT_PHASE) {
		new:
			NCRCMD(sc, NCRCMD_FLUSH);
/*			DELAY(1); */
			sc->sc_msgoutq = 0;
			sc->sc_omlen = 0;
		}
	} else {
		if (sc->sc_prevphase == MESSAGE_OUT_PHASE) {
			ncr53c9x_sched_msgout(sc->sc_msgoutq);
			goto new;
		} else {
			printf("%s at line %d: unexpected MESSAGE OUT phase\n",
			    sc->sc_dev.dv_xname, __LINE__);
		}
	}

	if (sc->sc_omlen == 0) {
		/* Pick up highest priority message */
		sc->sc_msgout = sc->sc_msgpriq & -sc->sc_msgpriq;
		sc->sc_msgoutq |= sc->sc_msgout;
		sc->sc_msgpriq &= ~sc->sc_msgout;
		sc->sc_omlen = 1;		/* "Default" message len */
		switch (sc->sc_msgout) {
		case SEND_SDTR:
			ecb = sc->sc_nexus;
			ti = &sc->sc_tinfo[ecb->xs->sc_link->target];
			sc->sc_omess[0] = MSG_EXTENDED;
			sc->sc_omess[1] = 3;
			sc->sc_omess[2] = MSG_EXT_SDTR;
			sc->sc_omess[3] = ti->period;
			sc->sc_omess[4] = ti->offset;
			sc->sc_omlen = 5;
			if ((sc->sc_flags & NCR_SYNCHNEGO) == 0) {
				ti->flags |= T_SYNCMODE;
				ncr53c9x_setsync(sc, ti);
			}
			break;
		case SEND_WDTR:
			ecb = sc->sc_nexus;
			ti = &sc->sc_tinfo[ecb->xs->sc_link->target];
			sc->sc_omess[0] = MSG_EXTENDED;
			sc->sc_omess[1] = 2;
			sc->sc_omess[2] = MSG_EXT_WDTR;
			sc->sc_omess[3] = ti->width;
			sc->sc_omlen = 4;
			break;
		case SEND_IDENTIFY:
			if (sc->sc_state != NCR_CONNECTED) {
				printf("%s at line %d: no nexus\n",
				    sc->sc_dev.dv_xname, __LINE__);
			}
			ecb = sc->sc_nexus;
			sc->sc_omess[0] =
			    MSG_IDENTIFY(ecb->xs->sc_link->lun, 0);
			break;
		case SEND_TAG:
			if (sc->sc_state != NCR_CONNECTED) {
				printf("%s at line %d: no nexus\n",
				    sc->sc_dev.dv_xname, __LINE__);
			}
			ecb = sc->sc_nexus;
			sc->sc_omess[0] = ecb->tag[0];
			sc->sc_omess[1] = ecb->tag[1];
			sc->sc_omlen = 2;
			break;
		case SEND_DEV_RESET:
			sc->sc_flags |= NCR_ABORTING;
			sc->sc_omess[0] = MSG_BUS_DEV_RESET;
			ecb = sc->sc_nexus;
			ti = &sc->sc_tinfo[ecb->xs->sc_link->target];
			ti->flags &= ~T_SYNCMODE;
			if ((ti->flags & T_SYNCHOFF) == 0)
				/* We can re-start sync negotiation */
				ti->flags |= T_NEGOTIATE;
			break;
		case SEND_PARITY_ERROR:
			sc->sc_omess[0] = MSG_PARITY_ERROR;
			break;
		case SEND_ABORT:
			sc->sc_flags |= NCR_ABORTING;
			sc->sc_omess[0] = MSG_ABORT;
			break;
		case SEND_INIT_DET_ERR:
			sc->sc_omess[0] = MSG_INITIATOR_DET_ERR;
			break;
		case SEND_REJECT:
			sc->sc_omess[0] = MSG_MESSAGE_REJECT;
			break;
		default:
			/*
			 * We normally do not get here, since the chip
			 * automatically turns off ATN before the last
			 * byte of a message is sent to the target.
			 * However, if the target rejects our (multi-byte)
			 * message early by switching to MSG IN phase
			 * ATN remains on, so the target may return to
			 * MSG OUT phase. If there are no scheduled messages
			 * left we send a NO-OP.
			 *
			 * XXX - Note that this leaves no useful purpose for
			 * the NCR_ATN flag.
			 */
			sc->sc_flags &= ~NCR_ATN;
			sc->sc_omess[0] = MSG_NOOP;
			break;
		}
		sc->sc_omp = sc->sc_omess;
	}

#ifdef DEBUG
	{
		int i;

		for (i = 0; i<sc->sc_omlen; i++)
			NCR_MISC(("<msgbyte:0x%02x>", sc->sc_omess[i]));
	}
#endif
	if (sc->sc_rev == NCR_VARIANT_FAS366) {
		/*
		 * XXX fifo size
		 */
		ncr53c9x_flushfifo(sc);
		ncr53c9x_wrfifo(sc, sc->sc_omp, sc->sc_omlen);
		sc->sc_cmdlen = 0;
		NCRCMD(sc, NCRCMD_TRANS);
	} else {
		/* (re)send the message */
		size = min(sc->sc_omlen, sc->sc_maxxfer);
		NCRDMA_SETUP(sc, &sc->sc_omp, &sc->sc_omlen, 0, &size);
		/* Program the SCSI counter */
		NCR_SET_COUNT(sc, size);

               /* Load the count in and start the message-out transfer */
		NCRCMD(sc, NCRCMD_NOP|NCRCMD_DMA);
		NCRCMD(sc, NCRCMD_TRANS|NCRCMD_DMA);
		NCRDMA_GO(sc);
	}
}

/*
 * This is the most critical part of the driver, and has to know
 * how to deal with *all* error conditions and phases from the SCSI
 * bus. If there are no errors and the DMA was active, then call the
 * DMA pseudo-interrupt handler. If this returns 1, then that was it
 * and we can return from here without further processing.
 *
 * Most of this needs verifying.
 */
int sdebug = 0;
int
ncr53c9x_intr(void *arg)
{
	struct ncr53c9x_softc *sc = arg;
	struct ncr53c9x_ecb *ecb;
	struct scsi_link *sc_link;
	struct ncr53c9x_tinfo *ti;
	size_t size;
	int nfifo;

	NCR_TRACE(("[ncr53c9x_intr: state %d] ", sc->sc_state));

	if (!NCRDMA_ISINTR(sc))
		return (0);

again:
	/* and what do the registers say... */
	ncr53c9x_readregs(sc);

	/*
	 * At the moment, only a SCSI Bus Reset or Illegal
	 * Command are classed as errors. A disconnect is a
	 * valid condition, and we let the code check is the
	 * "NCR_BUSFREE_OK" flag was set before declaring it
	 * and error.
	 *
	 * Also, the status register tells us about "Gross
	 * Errors" and "Parity errors". Only the Gross Error
	 * is really bad, and the parity errors are dealt
	 * with later
	 *
	 * TODO
	 *      If there are too many parity error, go to slow
	 *      cable mode ?
	 */

	/* SCSI Reset */
	if (sc->sc_espintr & NCRINTR_SBR) {
		if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
			NCRCMD(sc, NCRCMD_FLUSH);
			DELAY(1);
		}
		if (sc->sc_state != NCR_SBR) {
			printf("%s: SCSI bus reset\n",
				sc->sc_dev.dv_xname);
			ncr53c9x_init(sc, 0); /* Restart everything */
			return (1);
		}
#if 0
/*XXX*/		printf("<expected bus reset: "
			"[intr %x, stat %x, step %d]>\n",
			sc->sc_espintr, sc->sc_espstat,
			sc->sc_espstep);
#endif
		if (sc->sc_nexus)
			panic("%s: nexus in reset state",
		      sc->sc_dev.dv_xname);
		goto sched;
	}

	ecb = sc->sc_nexus;

#define NCRINTR_ERR (NCRINTR_SBR|NCRINTR_ILL)
	if (sc->sc_espintr & NCRINTR_ERR ||
	    sc->sc_espstat & NCRSTAT_GE) {

		if (sc->sc_espstat & NCRSTAT_GE) {
			/* Gross Error; no target ? */
			if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
				NCRCMD(sc, NCRCMD_FLUSH);
				DELAY(1);
			}
			if (sc->sc_state == NCR_CONNECTED ||
			    sc->sc_state == NCR_SELECTING) {
				ecb->xs->error = XS_TIMEOUT;
				ncr53c9x_done(sc, ecb);
			}
			return (1);
		}

		if (sc->sc_espintr & NCRINTR_ILL) {
			if (sc->sc_flags & NCR_EXPECT_ILLCMD) {
				/*
				 * Eat away "Illegal command" interrupt
				 * on a ESP100 caused by a re-selection
				 * while we were trying to select
				 * another target.
				 */
#ifdef DEBUG
				printf("%s: ESP100 work-around activated\n",
					sc->sc_dev.dv_xname);
#endif
				sc->sc_flags &= ~NCR_EXPECT_ILLCMD;
				return (1);
			}
			/* illegal command, out of sync ? */
			printf("%s: illegal command: 0x%x "
			    "(state %d, phase %x, prevphase %x)\n",
				sc->sc_dev.dv_xname, sc->sc_lastcmd,
				sc->sc_state, sc->sc_phase,
				sc->sc_prevphase);
			if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
				NCRCMD(sc, NCRCMD_FLUSH);
				DELAY(1);
			}
			ncr53c9x_init(sc, 1); /* Restart everything */
			return (1);
		}
	}
	sc->sc_flags &= ~NCR_EXPECT_ILLCMD;

	/*
	 * Call if DMA is active.
	 *
	 * If DMA_INTR returns true, then maybe go 'round the loop
	 * again in case there is no more DMA queued, but a phase
	 * change is expected.
	 */
	if (NCRDMA_ISACTIVE(sc)) {
		int r = NCRDMA_INTR(sc);
		if (r == -1) {
			printf("%s: DMA error; resetting\n",
				sc->sc_dev.dv_xname);
			ncr53c9x_init(sc, 1);
			return (1);
		}
		/* If DMA active here, then go back to work... */
		if (NCRDMA_ISACTIVE(sc))
			return (1);

		if ((sc->sc_espstat & NCRSTAT_TC) == 0) {
			/*
			 * DMA not completed.  If we can not find a
			 * acceptable explanation, print a diagnostic.
			 */
			if (sc->sc_state == NCR_SELECTING)
				/*
				 * This can happen if we are reselected
				 * while using DMA to select a target.
				 */
				/*void*/;
			else if (sc->sc_prevphase == MESSAGE_OUT_PHASE){
				/*
				 * Our (multi-byte) message (eg SDTR)
				 * was interrupted by the target to
				 * send a MSG REJECT.
				 * Print diagnostic if current phase
				 * is not MESSAGE IN.
				 */
				if (sc->sc_phase != MESSAGE_IN_PHASE)
				    printf("%s: !TC on MSG OUT"
				       " [intr %x, stat %x, step %d]"
				       " prevphase %x, resid %lx\n",
					sc->sc_dev.dv_xname,
					sc->sc_espintr,
					sc->sc_espstat,
					sc->sc_espstep,
					sc->sc_prevphase,
					(u_long)sc->sc_omlen);
			} else if (sc->sc_dleft == 0) {
				/*
				 * The DMA operation was started for
				 * a DATA transfer. Print a diagnostic
				 * if the DMA counter and TC bit
				 * appear to be out of sync.
				 */
				printf("%s: !TC on DATA XFER"
				       " [intr %x, stat %x, step %d]"
				       " prevphase %x, resid %x\n",
					sc->sc_dev.dv_xname,
					sc->sc_espintr,
					sc->sc_espstat,
					sc->sc_espstep,
					sc->sc_prevphase,
					ecb?ecb->dleft:-1);
			}
		}
	}

	/*
	 * check for less serious errors
	 */
	if (sc->sc_espstat & NCRSTAT_PE) {
		printf("%s: SCSI bus parity error\n", sc->sc_dev.dv_xname);
		if (sc->sc_prevphase == MESSAGE_IN_PHASE)
			ncr53c9x_sched_msgout(SEND_PARITY_ERROR);
		else
			ncr53c9x_sched_msgout(SEND_INIT_DET_ERR);
	}

	if (sc->sc_espintr & NCRINTR_DIS) {
		sc->sc_msgify = 0;
		NCR_MISC(("<DISC [intr %x, stat %x, step %d]>",
			sc->sc_espintr,sc->sc_espstat,sc->sc_espstep));
		if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
			NCRCMD(sc, NCRCMD_FLUSH);
/*			DELAY(1); */
		}
		/*
		 * This command must (apparently) be issued within
		 * 250mS of a disconnect. So here you are...
		 */
		NCRCMD(sc, NCRCMD_ENSEL);

		switch (sc->sc_state) {
		case NCR_RESELECTED:
			goto sched;

		case NCR_SELECTING:
			ecb->xs->error = XS_SELTIMEOUT;
			goto finish;

		case NCR_CONNECTED:
			if ((sc->sc_flags & NCR_SYNCHNEGO)) {
#ifdef NCR53C9X_DEBUG
				if (ecb)
					sc_print_addr(ecb->xs->sc_link);
				printf("sync nego not completed!\n");
#endif
				ti = &sc->sc_tinfo[ecb->xs->sc_link->target];
				sc->sc_flags &= ~NCR_SYNCHNEGO;
				ti->flags &= ~(T_NEGOTIATE | T_SYNCMODE);
			}

			/* it may be OK to disconnect */
			if ((sc->sc_flags & NCR_ABORTING) == 0) {
				/*
				 * Section 5.1.1 of the SCSI 2 spec
				 * suggests issuing a REQUEST SENSE
				 * following an unexpected disconnect.
				 * Some devices go into a contingent
				 * allegiance condition when
				 * disconnecting, and this is necessary
				 * to clean up their state.
				 */
				printf("%s: unexpected disconnect; ",
				    sc->sc_dev.dv_xname);
				if (ecb->flags & ECB_SENSE) {
					printf("resetting\n");
					goto reset;
				}
				printf("sending REQUEST SENSE\n");
				timeout_del(&ecb->xs->stimeout);
				ncr53c9x_sense(sc, ecb);
				goto out;
			}

			ecb->xs->error = XS_TIMEOUT;
			goto finish;

		case NCR_DISCONNECT:
			sc->sc_nexus = NULL;
			goto sched;

		case NCR_CMDCOMPLETE:
			goto finish;
		}
	}

	switch (sc->sc_state) {

	case NCR_SBR:
		printf("%s: waiting for SCSI Bus Reset to happen\n",
			sc->sc_dev.dv_xname);
		return (1);

	case NCR_RESELECTED:
		/*
		 * we must be continuing a message ?
		 */
		if (sc->sc_phase != MESSAGE_IN_PHASE) {
			printf("%s: target didn't identify\n",
				sc->sc_dev.dv_xname);
			ncr53c9x_init(sc, 1);
			return (1);
		}
printf("<<RESELECT CONT'd>>");
#if XXXX
		ncr53c9x_msgin(sc);
		if (sc->sc_state != NCR_CONNECTED) {
			/* IDENTIFY fail?! */
			printf("%s: identify failed\n",
				sc->sc_dev.dv_xname);
			ncr53c9x_init(sc, 1);
			return (1);
		}
#endif
		break;

	case NCR_IDENTIFIED:
		ecb = sc->sc_nexus;
		if (sc->sc_phase != MESSAGE_IN_PHASE) {
			int i = (NCR_READ_REG(sc, NCR_FFLAG)
			    & NCRFIFO_FF);
			/*
			 * Things are seriously fucked up.
			 * Pull the brakes, i.e. reset
			 */
			printf("%s: target didn't send tag: %d bytes in fifo\n",
			    sc->sc_dev.dv_xname, i);
			/* Drain and display fifo */
			while (i-- > 0)
				printf("[%d] ", NCR_READ_REG(sc, NCR_FIFO));
			ncr53c9x_init(sc, 1);
			return (1);
		} else
			goto msgin;

		break;
	case NCR_IDLE:
	case NCR_SELECTING:
		ecb = sc->sc_nexus;
		if (sc->sc_espintr & NCRINTR_RESEL) {
			sc->sc_msgpriq = sc->sc_msgout = sc->sc_msgoutq = 0;
			sc->sc_flags = 0;
			/*
			 * If we're trying to select a
			 * target ourselves, push our command
			 * back into the ready list.
			 */
			if (sc->sc_state == NCR_SELECTING) {
				NCR_MISC(("backoff selector "));
				timeout_del(&ecb->xs->stimeout);
				ncr53c9x_dequeue(sc, ecb);
				TAILQ_INSERT_HEAD(&sc->ready_list, ecb, chain);
				ecb->flags |= ECB_READY;
				ecb = sc->sc_nexus = NULL;
			}
			sc->sc_state = NCR_RESELECTED;
			if (sc->sc_phase != MESSAGE_IN_PHASE) {
				/*
				 * Things are seriously fucked up.
				 * Pull the brakes, i.e. reset
				 */
				printf("%s: target didn't identify\n",
					sc->sc_dev.dv_xname);
				ncr53c9x_init(sc, 1);
				return (1);
			}
			/*
			 * The C90 only inhibits FIFO writes until
			 * reselection is complete, instead of
			 * waiting until the interrupt status register
			 * has been read. So, if the reselect happens
			 * while we were entering a command bytes (for
			 * another target) some of those bytes can
			 * appear in the FIFO here, after the
			 * interrupt is taken.
			 */
			nfifo = ncr53c9x_rdfifo(sc, NCR_RDFIFO_START);

			if (nfifo < 2 ||
			    (nfifo > 2 &&
			     sc->sc_rev != NCR_VARIANT_ESP100)) {
				printf("%s: RESELECT: "
				    "%d bytes in FIFO! "
				    "[intr %x, stat %x, step %d, prevphase %x]\n",
					sc->sc_dev.dv_xname,
					nfifo,
					sc->sc_espintr,
					sc->sc_espstat,
					sc->sc_espstep,
					sc->sc_prevphase);
				ncr53c9x_init(sc, 1);
				return (1);
			}
			sc->sc_selid = sc->sc_imess[0];
			NCR_MISC(("selid=%2x ", sc->sc_selid));

			/* Handle identify message */
			ncr53c9x_msgin(sc);
			if (nfifo != 2) {
				/*
				 * Note: this should not happen
				 * with `dmaselect' on.
				 */
				sc->sc_flags |= NCR_EXPECT_ILLCMD;
				NCRCMD(sc, NCRCMD_FLUSH);
			} else if (sc->sc_features & NCR_F_DMASELECT &&
				   sc->sc_rev == NCR_VARIANT_ESP100) {
				sc->sc_flags |= NCR_EXPECT_ILLCMD;
			}

			if (sc->sc_state != NCR_CONNECTED &&
			    sc->sc_state != NCR_IDENTIFIED) {
				/* IDENTIFY fail?! */
				printf("%s: identify failed, state %d, intr %02x\n",
					sc->sc_dev.dv_xname, sc->sc_state,
				    sc->sc_espintr);
				ncr53c9x_init(sc, 1);
				return (1);
			}
			goto shortcut; /* ie. next phase expected soon */
		}

#define	NCRINTR_DONE	(NCRINTR_FC|NCRINTR_BS)
		if ((sc->sc_espintr & NCRINTR_DONE) == NCRINTR_DONE) {
			/*
			 * Arbitration won; examine the `step' register
			 * to determine how far the selection could progress.
			 */
			ecb = sc->sc_nexus;
			if (!ecb)
				panic("ncr53c9x: no nexus");

			sc_link = ecb->xs->sc_link;
			ti = &sc->sc_tinfo[sc_link->target];

			switch (sc->sc_espstep) {
			case 0:
				/*
				 * The target did not respond with a
				 * message out phase - probably an old
				 * device that doesn't recognize ATN.
				 * Clear ATN and just continue, the
				 * target should be in the command
				 * phase.
				 * XXXX check for command phase?
				 */
				NCRCMD(sc, NCRCMD_RSTATN);
				break;
			case 1:
				if ((ti->flags & T_NEGOTIATE) == 0 &&
				    ecb->tag[0] == 0) {
					printf("%s: step 1 & !NEG\n",
						sc->sc_dev.dv_xname);
					goto reset;
				}
				if (sc->sc_phase != MESSAGE_OUT_PHASE) {
					printf("%s: !MSGOUT\n",
						sc->sc_dev.dv_xname);
					goto reset;
				}
				if (ti->flags & T_WIDE) {
					ncr53c9x_sched_msgout(SEND_WDTR);
				}
				if (ti->flags & T_NEGOTIATE) {
					/* Start negotiating */
					ti->period = sc->sc_minsync;
					ti->offset = 15;
					sc->sc_flags |= NCR_SYNCHNEGO;
					if (ecb->tag[0])
						ncr53c9x_sched_msgout(SEND_TAG|SEND_SDTR);
					else
						ncr53c9x_sched_msgout(SEND_SDTR);
				} else {
					/* Could not do ATN3 so send TAG */
					ncr53c9x_sched_msgout(SEND_TAG);
				}
				sc->sc_prevphase = MESSAGE_OUT_PHASE; /* XXXX */
				break;
			case 3:
				/*
				 * Grr, this is supposed to mean
				 * "target left command phase  prematurely".
				 * It seems to happen regularly when
				 * sync mode is on.
				 * Look at FIFO to see if command went out.
				 * (Timing problems?)
				 */
				if (sc->sc_features & NCR_F_DMASELECT) {
					if (sc->sc_cmdlen == 0)
						/* Hope for the best.. */
						break;
				} else if ((NCR_READ_REG(sc, NCR_FFLAG)
					    & NCRFIFO_FF) == 0) {
					/* Hope for the best.. */
					break;
				}
				printf("(%s:%d:%d): selection failed;"
					" %d left in FIFO "
					"[intr %x, stat %x, step %d]\n",
					sc->sc_dev.dv_xname,
					sc_link->target,
					sc_link->lun,
					NCR_READ_REG(sc, NCR_FFLAG)
					 & NCRFIFO_FF,
					sc->sc_espintr, sc->sc_espstat,
					sc->sc_espstep);
				NCRCMD(sc, NCRCMD_FLUSH);
				ncr53c9x_sched_msgout(SEND_ABORT);
				return (1);
			case 2:
				/* Select stuck at Command Phase */
				NCRCMD(sc, NCRCMD_FLUSH);
				break;
			case 4:
				if (sc->sc_features & NCR_F_DMASELECT &&
				    sc->sc_cmdlen != 0)
					printf("(%s:%d:%d): select; "
					       "%lu left in DMA buffer "
					"[intr %x, stat %x, step %d]\n",
						sc->sc_dev.dv_xname,
						sc_link->target,
						sc_link->lun,
						(u_long)sc->sc_cmdlen,
						sc->sc_espintr,
						sc->sc_espstat,
						sc->sc_espstep);
				/* So far, everything went fine */
				break;
			}

			sc->sc_prevphase = INVALID_PHASE; /* ?? */
			/* Do an implicit RESTORE POINTERS. */
			sc->sc_dp = ecb->daddr;
			sc->sc_dleft = ecb->dleft;
			sc->sc_state = NCR_CONNECTED;
			break;

		} else {

			printf("%s: unexpected status after select"
				": [intr %x, stat %x, step %x]\n",
				sc->sc_dev.dv_xname,
				sc->sc_espintr, sc->sc_espstat,
				sc->sc_espstep);
			NCRCMD(sc, NCRCMD_FLUSH);
			DELAY(1);
			goto reset;
		}
		if (sc->sc_state == NCR_IDLE) {
			printf("%s: stray interrupt\n",
			    sc->sc_dev.dv_xname);
			return (0);
		}
		break;

	case NCR_CONNECTED:
		if (sc->sc_flags & NCR_ICCS) {
			/* "Initiate Command Complete Steps" in progress */
			u_char msg;

			sc->sc_flags &= ~NCR_ICCS;

			if (!(sc->sc_espintr & NCRINTR_DONE)) {
				printf("%s: ICCS: "
				      ": [intr %x, stat %x, step %x]\n",
					sc->sc_dev.dv_xname,
					sc->sc_espintr, sc->sc_espstat,
					sc->sc_espstep);
			}
			ncr53c9x_rdfifo(sc, NCR_RDFIFO_START);
			if (sc->sc_imlen < 2)
				printf("%s: can't get status, only %d bytes\n",
				    sc->sc_dev.dv_xname, (int)sc->sc_imlen);
			ecb->stat = sc->sc_imess[sc->sc_imlen - 2];
			msg = sc->sc_imess[sc->sc_imlen - 1];
			NCR_PHASE(("<stat:(%x,%x)>", ecb->stat, msg));
			if (msg == MSG_CMDCOMPLETE) {
				ecb->dleft = (ecb->flags & ECB_TENTATIVE_DONE)
					? 0
					: sc->sc_dleft;
				if ((ecb->flags & ECB_SENSE) == 0)
					ecb->xs->resid = ecb->dleft;
				sc->sc_state = NCR_CMDCOMPLETE;
			} else
				printf("%s: STATUS_PHASE: msg %d\n",
					sc->sc_dev.dv_xname, msg);
			sc->sc_imlen = 0;
			NCRCMD(sc, NCRCMD_MSGOK);
			goto shortcut; /* ie. wait for disconnect */
		}
		break;
	default:
		/* Don't panic: reset. */
		printf("%s: invalid state: %d\n",
		      sc->sc_dev.dv_xname,
		      sc->sc_state);
		ncr53c9x_scsi_reset(sc);
		goto out;
		break;
	}

	/*
	 * Driver is now in state NCR_CONNECTED, i.e. we
	 * have a current command working the SCSI bus.
	 */
	if (sc->sc_state != NCR_CONNECTED || ecb == NULL) {
		panic("ncr53c9x no nexus");
	}

	switch (sc->sc_phase) {
	case MESSAGE_OUT_PHASE:
		NCR_PHASE(("MESSAGE_OUT_PHASE "));
		ncr53c9x_msgout(sc);
		sc->sc_prevphase = MESSAGE_OUT_PHASE;
		break;
	case MESSAGE_IN_PHASE:
msgin:
		NCR_PHASE(("MESSAGE_IN_PHASE "));
		if (sc->sc_espintr & NCRINTR_BS) {
			if ((sc->sc_rev != NCR_VARIANT_FAS366) ||
			    !(sc->sc_espstat2 & FAS_STAT2_EMPTY)) {
				NCRCMD(sc, NCRCMD_FLUSH);
			}
			sc->sc_flags |= NCR_WAITI;
			NCRCMD(sc, NCRCMD_TRANS);
		} else if (sc->sc_espintr & NCRINTR_FC) {
			if ((sc->sc_flags & NCR_WAITI) == 0) {
				printf("%s: MSGIN: unexpected FC bit: "
					"[intr %x, stat %x, step %x]\n",
				sc->sc_dev.dv_xname,
				sc->sc_espintr, sc->sc_espstat,
				sc->sc_espstep);
			}
			sc->sc_flags &= ~NCR_WAITI;
			ncr53c9x_rdfifo(sc,
			    (sc->sc_prevphase == sc->sc_phase) ?
			    NCR_RDFIFO_CONTINUE : NCR_RDFIFO_START);
			ncr53c9x_msgin(sc);
		} else {
			printf("%s: MSGIN: weird bits: "
				"[intr %x, stat %x, step %x]\n",
				sc->sc_dev.dv_xname,
				sc->sc_espintr, sc->sc_espstat,
				sc->sc_espstep);
		}
		sc->sc_prevphase = MESSAGE_IN_PHASE;
		goto shortcut;	/* i.e. expect data to be ready */
		break;
	case COMMAND_PHASE:
		/*
		 * Send the command block. Normally we don't see this
		 * phase because the SEL_ATN command takes care of
		 * all this. However, we end up here if either the
		 * target or we wanted to exchange some more messages
		 * first (e.g. to start negotiations).
		 */

		NCR_PHASE(("COMMAND_PHASE 0x%02x (%d) ",
			ecb->cmd.cmd.opcode, ecb->clen));
		if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
			NCRCMD(sc, NCRCMD_FLUSH);
/*			DELAY(1); */
		}
		if (sc->sc_features & NCR_F_DMASELECT) {
			/* setup DMA transfer for command */
			size = ecb->clen;
			sc->sc_cmdlen = size;
			sc->sc_cmdp = (caddr_t)&ecb->cmd.cmd;
			NCRDMA_SETUP(sc, &sc->sc_cmdp, &sc->sc_cmdlen,
				     0, &size);
			/* Program the SCSI counter */
			NCR_SET_COUNT(sc, size);

			/* load the count in */
			NCRCMD(sc, NCRCMD_NOP|NCRCMD_DMA);

			/* start the command transfer */
			NCRCMD(sc, NCRCMD_TRANS | NCRCMD_DMA);
			NCRDMA_GO(sc);
		} else {
			ncr53c9x_wrfifo(sc, (u_char *)&ecb->cmd.cmd, ecb->clen);
			sc->sc_cmdlen = 0;
			NCRCMD(sc, NCRCMD_TRANS);
		}
		sc->sc_prevphase = COMMAND_PHASE;
		break;
	case DATA_OUT_PHASE:
		NCR_PHASE(("DATA_OUT_PHASE [%ld] ",(long)sc->sc_dleft));
		NCRCMD(sc, NCRCMD_FLUSH);
		size = min(sc->sc_dleft, sc->sc_maxxfer);
		NCRDMA_SETUP(sc, &sc->sc_dp, &sc->sc_dleft,
			  0, &size);
		sc->sc_prevphase = DATA_OUT_PHASE;
		goto setup_xfer;
	case DATA_IN_PHASE:
		NCR_PHASE(("DATA_IN_PHASE "));
		if (sc->sc_rev == NCR_VARIANT_ESP100)
			NCRCMD(sc, NCRCMD_FLUSH);
		size = min(sc->sc_dleft, sc->sc_maxxfer);
		NCRDMA_SETUP(sc, &sc->sc_dp, &sc->sc_dleft,
			  1, &size);
		sc->sc_prevphase = DATA_IN_PHASE;
	setup_xfer:
		/* Target returned to data phase: wipe "done" memory */
		ecb->flags &= ~ECB_TENTATIVE_DONE;

		/* Program the SCSI counter */
		NCR_SET_COUNT(sc, size);

		/* load the count in */
		NCRCMD(sc, NCRCMD_NOP|NCRCMD_DMA);

		/*
		 * Note that if `size' is 0, we've already transceived
		 * all the bytes we want but we're still in DATA PHASE.
		 * Apparently, the device needs padding. Also, a
		 * transfer size of 0 means "maximum" to the chip
		 * DMA logic.
		 */
		NCRCMD(sc,
		       (size==0?NCRCMD_TRPAD:NCRCMD_TRANS)|NCRCMD_DMA);
		NCRDMA_GO(sc);
		return (1);
	case STATUS_PHASE:
		NCR_PHASE(("STATUS_PHASE "));
		sc->sc_flags |= NCR_ICCS;
		NCRCMD(sc, NCRCMD_ICCS);
		sc->sc_prevphase = STATUS_PHASE;
		goto shortcut;	/* i.e. expect status results soon */
		break;
	case INVALID_PHASE:
		break;
	default:
		printf("%s: unexpected bus phase; resetting\n",
		    sc->sc_dev.dv_xname);
		goto reset;
	}

out:
	return (1);

reset:
	ncr53c9x_init(sc, 1);
	goto out;

finish:
	ncr53c9x_done(sc, ecb);
	goto out;

sched:
	sc->sc_state = NCR_IDLE;
	ncr53c9x_sched(sc);
	goto out;

shortcut:
	/*
	 * The idea is that many of the SCSI operations take very little
	 * time, and going away and getting interrupted is too high an
	 * overhead to pay. For example, selecting, sending a message
	 * and command and then doing some work can be done in one "pass".
	 *
	 * The delay is a heuristic. It is 2 when at 20MHz, 2 at 25MHz and 1
	 * at 40MHz. This needs testing.
	 */
	{
		struct timeval wait, cur;

		microuptime(&wait);
		wait.tv_usec += 50/sc->sc_freq;
		if (wait.tv_usec >= 1000000) {
			wait.tv_sec++;
			wait.tv_usec -= 1000000;
		}
		do {
			if (NCRDMA_ISINTR(sc))
				goto again;
			microuptime(&cur);
		} while (timercmp(&cur, &wait, <=));
	}
	goto out;
}

void
ncr53c9x_abort(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{

	/* 2 secs for the abort */
	ecb->xs->timeout = NCR_ABORT_TIMEOUT;
	ecb->flags |= ECB_ABORT;

	if (ecb == sc->sc_nexus) {
		int timeout = ecb->xs->timeout;

		/*
		 * If we're still selecting, the message will be scheduled
		 * after selection is complete.
		 */
		if (sc->sc_state == NCR_CONNECTED)
			ncr53c9x_sched_msgout(SEND_ABORT);

		/*
		 * Reschedule timeout.
		 */
		if (timeout > 1000000)
			timeout = (timeout / 1000) * hz;
		else
			timeout = (timeout * hz) / 1000;
		timeout_add(&ecb->xs->stimeout, timeout);
	} else {
		/*
		 * Just leave the command where it is.
		 * XXX - what choice do we have but to reset the SCSI
		 *	 eventually?
		 */
		if (sc->sc_state == NCR_IDLE)
			ncr53c9x_sched(sc);
	}
}

void
ncr53c9x_timeout(void *arg)
{
	struct ncr53c9x_ecb *ecb = arg;
	struct scsi_xfer *xs = ecb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct ncr53c9x_softc *sc = sc_link->bus->sb_adapter_softc;
	struct ncr53c9x_tinfo *ti = &sc->sc_tinfo[sc_link->target];
	int s;

	sc_print_addr(sc_link);
	printf("timed out [ecb %p (flags 0x%x, dleft %x, stat %x)], "
	       "<state %d, nexus %p, phase(l %x, c %x, p %x), resid %lx, "
	       "msg(q %x,o %x) %s>",
		ecb, ecb->flags, ecb->dleft, ecb->stat,
		sc->sc_state, sc->sc_nexus,
		NCR_READ_REG(sc, NCR_STAT),
		sc->sc_phase, sc->sc_prevphase,
		(long)sc->sc_dleft, sc->sc_msgpriq, sc->sc_msgout,
		NCRDMA_ISACTIVE(sc) ? "DMA active" : "");
#if NCR53C9X_DEBUG > 1
	printf("TRACE: %s.", ecb->trace);
#endif

	s = splbio();

	if (ecb->flags & ECB_ABORT) {
		/* abort timed out */
		printf(" AGAIN\n");

		ncr53c9x_init(sc, 1);
	} else {
		/* abort the operation that has timed out */
		printf("\n");
		xs->error = XS_TIMEOUT;
		ncr53c9x_abort(sc, ecb);

		/* Disable sync mode if stuck in a data phase */
		if (ecb == sc->sc_nexus &&
		    (ti->flags & T_SYNCMODE) != 0 &&
		    (sc->sc_phase & (MSGI|CDI)) == 0) {
			sc_print_addr(sc_link);
			printf("sync negotiation disabled\n");
			sc->sc_cfflags |= (1 << (sc_link->target + 16));
		}
	}

	splx(s);
}
