/*	$OpenBSD: ncr53c9xvar.h,v 1.26 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: ncr53c9xvar.h,v 1.13 1998/05/26 23:17:34 thorpej Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

/*
 * Copyright (c) 1994 Peter Galbavy.  All rights reserved.
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
 */

/* Set this to 1 for normal debug, or 2 for per-target tracing. */
#if !defined(SMALL_KERNEL)
#define NCR53C9X_DEBUG		1
#endif

/* Wide or differential can have 16 targets */
#define NCR_NTARG		16
#define NCR_NLUN		8

#define	NCR_ABORT_TIMEOUT	2000	/* time to wait for abort */
#define	NCR_SENSE_TIMEOUT	1000	/* time to wait for sense */

#define FREQTOCCF(freq)	(((freq + 4) / 5))

/*
 * NCR 53c9x variants.  Note, these values are used as indexes into
 * a table; don't modify them unless you know what you're doing.
 */
#define	NCR_VARIANT_ESP100		0
#define	NCR_VARIANT_ESP100A		1
#define	NCR_VARIANT_ESP200		2
#define	NCR_VARIANT_NCR53C94		3
#define	NCR_VARIANT_NCR53C96		4
#define	NCR_VARIANT_ESP406		5
#define	NCR_VARIANT_FAS408		6
#define	NCR_VARIANT_FAS216		7
#define	NCR_VARIANT_AM53C974		8
#define	NCR_VARIANT_FAS366		9
#define	NCR_VARIANT_MAX			10

/*
 * ECB. Holds additional information for each SCSI command Comments: We
 * need a separate scsi command block because we may need to overwrite it
 * with a request sense command.  Basically, we refrain from fiddling with
 * the scsi_xfer struct (except do the expected updating of return values).
 * We'll generally update: xs->{flags,resid,error,sense,status} and
 * occasionally xs->retries.
 */
struct ncr53c9x_ecb {
	TAILQ_ENTRY(ncr53c9x_ecb) chain;
	struct scsi_xfer *xs;	/* SCSI xfer ctrl block from above */
	int flags;
#define	ECB_ALLOC		0x01
#define	ECB_READY		0x02
#define	ECB_SENSE		0x04
#define	ECB_ABORT		0x40
#define	ECB_RESET		0x80
#define	ECB_TENTATIVE_DONE	0x100

	struct {
		u_char	msg[3];			/* Selection Id msg */
		struct scsi_generic cmd;	/* SCSI command block */
	} cmd;
	char	*daddr;		/* Saved data pointer */
	int	clen;		/* Size of command in cmd.cmd */
	int	dleft;		/* Residue */
	u_char	stat;		/* SCSI status byte */
	u_char	tag[2];		/* TAG bytes */
	u_char	pad[1];

#if NCR53C9X_DEBUG > 1
	char trace[1000];
#endif
};
#if NCR53C9X_DEBUG > 1
#define ECB_TRACE(ecb, msg, a, b) do { \
	const char *f = "[" msg "]"; \
	int n = strlen((ecb)->trace); \
	if (n < (sizeof((ecb)->trace)-100)) \
		snprintf((ecb)->trace + n, sizeof((ecb)->trace) - n, f,  a, b); \
} while(0)
#else
#define ECB_TRACE(ecb, msg, a, b)
#endif

/*
 * Some info about ech (possible) target and LUN on the SCSI bus.
 *
 * SCSI I and II devices can have up to 8 LUNs, each with up to 256
 * outstanding tags.  SCSI III devices have 64-bit LUN identifiers
 * that can be sparsely allocated.
 *
 * Since SCSI II devices can have up to 8 LUNs, we use an array
 * of 8 pointers to ncr53c9x_linfo structures for fast lookup.
 * Longer LUNs need to traverse the linked list.
 */

struct ncr53c9x_linfo {
	int64_t			lun;
	LIST_ENTRY(ncr53c9x_linfo) link;
	time_t			last_used;
	unsigned char		used;	/* # slots in use */
	unsigned char		avail;	/* where to start scanning */
	unsigned char		busy;
	struct ncr53c9x_ecb	*untagged;
	struct ncr53c9x_ecb	*queued[256];
};

struct ncr53c9x_tinfo {
	int	cmds;		/* # of commands processed */
	int	dconns;		/* # of disconnects */
	int	touts;		/* # of timeouts */
	int	perrs;		/* # of parity errors */
	int	senses;		/* # of request sense commands sent */
	u_char  flags;
#define T_NEED_TO_RESET	0x01	/* Should send a BUS_DEV_RESET */
#define T_NEGOTIATE	0x02	/* (Re)Negotiate synchronous options */
#define T_BUSY		0x04	/* Target is busy, i.e. cmd in progress */
#define T_SYNCMODE	0x08	/* sync mode has been negotiated */
#define T_SYNCHOFF	0x10	/* .. */
#define T_RSELECTOFF	0x20	/* .. */
#define T_TAG		0x40	/* TAG QUEUEs are on */
#define T_WIDE		0x80	/* Negotiate wide options */
	u_char  period;		/* Period suggestion */
	u_char  offset;		/* Offset suggestion */
	u_char	cfg3;		/* per target config 3 */
	u_char	nextag;		/* Next available tag */
	u_char	width;		/* width suggestion */
	LIST_HEAD(lun_list, ncr53c9x_linfo) luns;
	struct ncr53c9x_linfo *lun[NCR_NLUN];	/* For speedy lookups */
};

/* Look up a lun in a tinfo */
#define TINFO_LUN(t, l)		((((l) < NCR_NLUN) && (((t)->lun[(l)]) != NULL)) ? \
    ((t)->lun[(l)]) : ncr53c9x_lunsearch((t), (int64_t)(l)))

/* Register a linenumber (for debugging) */
#define LOGLINE(p)

#define NCR_SHOWECBS	0x01
#define NCR_SHOWINTS	0x02
#define NCR_SHOWCMDS	0x04
#define NCR_SHOWMISC	0x08
#define NCR_SHOWTRAC	0x10
#define NCR_SHOWSTART	0x20
#define NCR_SHOWPHASE	0x40
#define NCR_SHOWDMA	0x80
#define NCR_SHOWCCMDS	0x100
#define NCR_SHOWMSGS	0x200

#ifdef NCR53C9X_DEBUG
extern int ncr53c9x_debug;
#define NCR_ECBS(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWECBS) printf str;} while (0)
#define NCR_MISC(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWMISC) printf str;} while (0)
#define NCR_INTS(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWINTS) printf str;} while (0)
#define NCR_TRACE(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWTRAC) printf str;} while (0)
#define NCR_CMDS(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWCMDS) printf str;} while (0)
#define NCR_START(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWSTART) printf str;}while (0)
#define NCR_PHASE(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWPHASE) printf str;}while (0)
#define NCR_DMA(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWDMA) printf str;}while (0)
#define NCR_MSGS(str)	\
	do {if (ncr53c9x_debug & NCR_SHOWMSGS) printf str;}while (0)
#else
#define NCR_ECBS(str)
#define NCR_MISC(str)
#define NCR_INTS(str)
#define NCR_TRACE(str)
#define NCR_CMDS(str)
#define NCR_START(str)
#define NCR_PHASE(str)
#define NCR_DMA(str)
#define NCR_MSGS(str)
#endif

#define NCR_MAX_MSG_LEN 8

struct ncr53c9x_softc;

/*
 * Function switch used as glue to MD code.
 */
struct ncr53c9x_glue {
	/* Mandatory entry points. */
	u_char	(*gl_read_reg)(struct ncr53c9x_softc *, int);
	void	(*gl_write_reg)(struct ncr53c9x_softc *, int, u_char);
	int	(*gl_dma_isintr)(struct ncr53c9x_softc *);
	void	(*gl_dma_reset)(struct ncr53c9x_softc *);
	int	(*gl_dma_intr)(struct ncr53c9x_softc *);
	int	(*gl_dma_setup)(struct ncr53c9x_softc *,
		    caddr_t *, size_t *, int, size_t *);
	void	(*gl_dma_go)(struct ncr53c9x_softc *);
	void	(*gl_dma_stop)(struct ncr53c9x_softc *);
	int	(*gl_dma_isactive)(struct ncr53c9x_softc *);

	/* Optional entry points. */
	void	(*gl_clear_latched_intr)(struct ncr53c9x_softc *);
};

struct ncr53c9x_softc {
	struct device sc_dev;			/* us as a device */

	struct ncr53c9x_glue *sc_glue;		/* glue to MD code */

	int	sc_ntarg;			/* number of targets */
	int	sc_cfflags;			/* Copy of config flags */

	/* register defaults */
	u_char	sc_cfg1;			/* Config 1 */
	u_char	sc_cfg2;			/* Config 2, not ESP100 */
	u_char	sc_cfg3;			/* Config 3, only ESP200 */
	u_char	sc_cfg3_fscsi;			/* Chip specific FSCSI bit */
	u_char	sc_cfg4;			/* Config 4 */
	u_char	sc_cfg5;			/* Config 5 */
	u_char	sc_ccf;				/* Clock Conversion */
	u_char	sc_timeout;

	/* register copies, see espreadregs() */
	u_char	sc_espintr;
	u_char	sc_espstat;
	u_char	sc_espstep;
	u_char	sc_espstat2;
	u_char	sc_espfflags;

	/* Lists of command blocks */
	TAILQ_HEAD(ecb_list, ncr53c9x_ecb) ready_list;

	struct ncr53c9x_ecb *sc_nexus;		/* Current command */
	struct ncr53c9x_tinfo sc_tinfo[NCR_NTARG];

	/* Data about the current nexus (updated for every cmd switch) */
	caddr_t	sc_dp;		/* Current data pointer */
	ssize_t	sc_dleft;	/* Data left to transfer */

	/* Adapter state */
	int	sc_phase;	/* Copy of what bus phase we are in */
	int	sc_prevphase;	/* Copy of what bus phase we were in */
	u_char	sc_state;	/* State applicable to the adapter */
	u_char	sc_flags;	/* See below */
	u_char	sc_selid;
	u_char	sc_lastcmd;

	/* Message stuff */
	u_short sc_msgify;	/* IDENTIFY message associated with this nexus */
	u_short sc_msgout;	/* What message is on its way out? */
	u_short sc_msgpriq;	/* One or more messages to send (encoded) */
	u_short sc_msgoutq;	/* What messages have been sent so far? */

	u_char	*sc_omess;	/* MSGOUT buffer */
	caddr_t	sc_omp;		/* Message pointer (for multibyte messages) */
	size_t	sc_omlen;
	u_char	*sc_imess;	/* MSGIN buffer */
	caddr_t	sc_imp;		/* Message pointer (for multibyte messages) */
	size_t	sc_imlen;

	caddr_t	sc_cmdp;	/* Command pointer (for DMAed commands) */
	size_t	sc_cmdlen;	/* Size of command in transit */

	/* Hardware attributes */
	int sc_freq;		/* SCSI bus frequency in MHz */
	int sc_id;		/* Our SCSI id */
	int sc_rev;		/* Chip revision */
	int sc_features;	/* Chip features */
	int sc_minsync;		/* Minimum sync period / 4 */
	int sc_maxxfer;		/* Maximum transfer size */
};

/* values for sc_state */
#define NCR_IDLE	1	/* waiting for something to do */
#define NCR_SELECTING	2	/* SCSI command is arbiting  */
#define NCR_RESELECTED	3	/* Has been reselected */
#define NCR_IDENTIFIED	4	/* Has gotten IFY but not TAG */
#define NCR_CONNECTED	5	/* Actively using the SCSI bus */
#define NCR_DISCONNECT	6	/* MSG_DISCONNECT received */
#define NCR_CMDCOMPLETE	7	/* MSG_CMDCOMPLETE received */
#define NCR_CLEANING	8
#define NCR_SBR		9	/* Expect a SCSI RST because we commanded it */

/* values for sc_flags */
#define NCR_DROP_MSGI	0x01	/* Discard all msgs (parity err detected) */
#define NCR_ABORTING	0x02	/* Bailing out */
#define NCR_DOINGDMA	0x04	/* The FIFO data path is active! */
#define NCR_SYNCHNEGO	0x08	/* Synch negotiation in progress. */
#define NCR_ICCS	0x10	/* Expect status phase results */
#define NCR_WAITI	0x20	/* Waiting for non-DMA data to arrive */
#define	NCR_ATN		0x40	/* ATN asserted */
#define	NCR_EXPECT_ILLCMD	0x80	/* Expect Illegal Command Interrupt */

/* values for sc_features */
#define	NCR_F_HASCFG3	0x01	/* chip has CFG3 register */
#define	NCR_F_FASTSCSI	0x02	/* chip supports Fast mode */
#define	NCR_F_DMASELECT	0x04	/* can do dma select */
#define	NCR_F_SELATN3	0x08	/* can do selatn3 */

/* values for sc_msgout */
#define SEND_DEV_RESET		0x0001
#define SEND_PARITY_ERROR	0x0002
#define SEND_INIT_DET_ERR	0x0004
#define SEND_REJECT		0x0008
#define SEND_IDENTIFY  		0x0010
#define SEND_ABORT		0x0020
#define SEND_WDTR		0x0040
#define SEND_SDTR		0x0080
#define SEND_TAG		0x0100

/* SCSI Status codes */
#define ST_MASK			0x3e /* bit 0,6,7 is reserved */

/* phase bits */
#define IOI			0x01
#define CDI			0x02
#define MSGI			0x04

/* Information transfer phases */
#define DATA_OUT_PHASE		(0)
#define DATA_IN_PHASE		(IOI)
#define COMMAND_PHASE		(CDI)
#define STATUS_PHASE		(CDI|IOI)
#define MESSAGE_OUT_PHASE	(MSGI|CDI)
#define MESSAGE_IN_PHASE	(MSGI|CDI|IOI)

#define PHASE_MASK		(MSGI|CDI|IOI)

/* Some pseudo phases for getphase()*/
#define BUSFREE_PHASE		0x100	/* Re/Selection no longer valid */
#define INVALID_PHASE		0x101	/* Re/Selection valid, but no REQ yet */
#define PSEUDO_PHASE		0x100	/* "pseudo" bit */

/*
 * Macros to read and write the chip's registers.
 */
#define	NCR_READ_REG(sc, reg)		\
				(*(sc)->sc_glue->gl_read_reg)((sc), (reg))
#define	NCR_WRITE_REG(sc, reg, val)	\
			(*(sc)->sc_glue->gl_write_reg)((sc), (reg), (val))

#ifdef NCR53C9X_DEBUG
#define	NCRCMD(sc, cmd) do {						\
	if (ncr53c9x_debug & NCR_SHOWCCMDS)				\
		printf("<cmd:0x%x %d>", (unsigned)cmd, __LINE__);	\
	sc->sc_lastcmd = cmd;						\
	NCR_WRITE_REG(sc, NCR_CMD, cmd);				\
} while (0)
#else
#define	NCRCMD(sc, cmd)		NCR_WRITE_REG(sc, NCR_CMD, cmd)
#endif

/*
 * DMA macros for NCR53c9x
 */
#define	NCRDMA_ISINTR(sc)	(*(sc)->sc_glue->gl_dma_isintr)((sc))
#define	NCRDMA_RESET(sc)	(*(sc)->sc_glue->gl_dma_reset)((sc))
#define	NCRDMA_INTR(sc)		(*(sc)->sc_glue->gl_dma_intr)((sc))
#define	NCRDMA_SETUP(sc, addr, len, datain, dmasize)	\
     (*(sc)->sc_glue->gl_dma_setup)((sc), (addr), (len), (datain), (dmasize))
#define	NCRDMA_GO(sc)		(*(sc)->sc_glue->gl_dma_go)((sc))
#define	NCRDMA_ISACTIVE(sc)	(*(sc)->sc_glue->gl_dma_isactive)((sc))

/*
 * Macro to convert the chip register Clock Per Byte value to
 * Synchronous Transfer Period.
 */
#define	ncr53c9x_cpb2stp(sc, cpb)	\
	((250 * (cpb)) / (sc)->sc_freq)

void	ncr53c9x_attach(struct ncr53c9x_softc *);
void	ncr53c9x_reset(struct ncr53c9x_softc *);
int	ncr53c9x_intr(void *);
void	ncr53c9x_init(struct ncr53c9x_softc *, int);
