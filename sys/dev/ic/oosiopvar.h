/* $OpenBSD: oosiopvar.h,v 1.6 2020/07/22 13:16:04 krw Exp $ */
/* $NetBSD: oosiopvar.h,v 1.2 2003/05/03 18:11:23 wiz Exp $ */

/*
 * Copyright (c) 2001 Shuichiro URATA.  All rights reserved.
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
 */

#include <uvm/uvm_extern.h>

#define	OOSIOP_NTGT	8		/* Max targets */
#define	OOSIOP_NCB	32		/* Initial command buffers */
#define	OOSIOP_NSG	(MIN(atop(MAXPHYS) + 1, 32)) /* Max S/G operation */
#define	OOSIOP_MAX_XFER	ptoa(OOSIOP_NSG - 1)

struct oosiop_xfer {
	/* script for scatter/gather DMA (move*nsg+jump) */
	u_int32_t datain_scr[(OOSIOP_NSG + 1) * 2];
	u_int32_t dataout_scr[(OOSIOP_NSG + 1) * 2];

	u_int8_t msgin[8];
	u_int8_t msgout[8];
	u_int8_t status;
	u_int8_t pad[7];
} __packed;

#define	SCSI_OOSIOP_NOSTATUS	0xff	/* device didn't report status */

#define	OOSIOP_XFEROFF(x)	offsetof(struct oosiop_xfer, x)
#define	OOSIOP_DINSCROFF	OOSIOP_XFEROFF(datain_scr[0])
#define	OOSIOP_DOUTSCROFF	OOSIOP_XFEROFF(dataout_scr[0])
#define	OOSIOP_MSGINOFF		OOSIOP_XFEROFF(msgin[0])
#define	OOSIOP_MSGOUTOFF	OOSIOP_XFEROFF(msgout[0])

#define	OOSIOP_XFERSCR_SYNC(sc, cb, ops)				\
	bus_dmamap_sync((sc)->sc_dmat, (cb)->xferdma, OOSIOP_DINSCROFF,	\
	    OOSIOP_MSGINOFF - OOSIOP_DINSCROFF, (ops))
#define	OOSIOP_DINSCR_SYNC(sc, cb, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (cb)->xferdma, OOSIOP_DINSCROFF,	\
	    OOSIOP_DOUTSCROFF - OOSIOP_DINSCROFF, (ops))
#define	OOSIOP_DOUTSCR_SYNC(sc, cb, ops)				\
	bus_dmamap_sync((sc)->sc_dmat, (cb)->xferdma, OOSIOP_DOUTSCROFF,\
	    OOSIOP_MSGINOFF - OOSIOP_DOUTSCROFF, (ops))
#define	OOSIOP_XFERMSG_SYNC(sc, cb, ops)				\
	bus_dmamap_sync((sc)->sc_dmat, (cb)->xferdma, OOSIOP_MSGINOFF,	\
	    sizeof(struct oosiop_xfer) - OOSIOP_MSGINOFF, (ops))

#define	OOSIOP_SCRIPT_SYNC(sc, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_scrdma,			\
	    0, sizeof(oosiop_script), (ops))

struct oosiop_cb {
	TAILQ_ENTRY(oosiop_cb) chain;

	struct scsi_xfer *xs;		/* SCSI xfer ctrl block from above */
	int flags;
	int id;				/* target scsi id */
	int lun;			/* target lun */

	bus_dmamap_t cmddma;		/* DMA map for command out */
	bus_dmamap_t datadma;		/* DMA map for data I/O */
	bus_dmamap_t xferdma;		/* DMA map for xfer block */

	int curdp;			/* current data pointer */
	int savedp;			/* saved data pointer */
	int msgoutlen;

	int xsflags;			/* copy of xs->flags */
	int datalen;			/* copy of xs->datalen */
	int cmdlen;			/* copy of xs->cmdlen */

	struct oosiop_xfer *xfer;	/* DMA xfer block */
};

/* oosiop_cb flags */
#define	CBF_SELTOUT	0x01	/* Selection timeout */
#define	CBF_TIMEOUT	0x02	/* Command timeout */
#define	CBF_AUTOSENSE	0x04	/* Request sense due to SCSI_CHECK */

struct oosiop_target {
	struct oosiop_cb *nexus;
	int flags;
	u_int8_t scf;		/* synchronous clock divisor */
	u_int8_t sxfer;		/* synchronous period and offset */
};

/* target flags */
#define	TGTF_SYNCNEG	0x01	/* Trigger synchronous negotiation */
#define	TGTF_WAITSDTR	0x02	/* Waiting SDTR from target */

struct oosiop_softc {
	struct device sc_dev;

	bus_space_tag_t	sc_bst;		/* bus space tag */
	bus_space_handle_t sc_bsh;	/* bus space handle */

	bus_dma_tag_t sc_dmat;		/* bus DMA tag */
	bus_dmamap_t sc_scrdma;		/* script DMA map */

	bus_addr_t sc_scrbase;		/* script DMA base address */
	u_int32_t *sc_scr;		/* ptr to script memory */

	int sc_chip;			/* 700 or 700-66 */
#define	OOSIOP_700	0
#define	OOSIOP_700_66	1

	int		sc_id;		/* SCSI ID of this interface */
	int		sc_freq;	/* SCLK frequency */
	int		sc_ccf;		/* asynchronous divisor (*10) */
	u_int8_t	sc_dcntl;
	u_int8_t	sc_minperiod;

	struct oosiop_target sc_tgt[OOSIOP_NTGT];

	/* Lists of command blocks */
	TAILQ_HEAD(oosiop_cb_queue, oosiop_cb) sc_free_cb,
					       sc_cbq;

	struct mutex		sc_cb_mtx;
	struct scsi_iopool	sc_iopool;

	struct oosiop_cb *sc_curcb;	/* current command */
	struct oosiop_cb *sc_lastcb;	/* last activated command */

	bus_addr_t sc_reselbuf;		/* msgin buffer for reselection */
	int sc_resid;			/* reselected target id */

	int sc_active;
	int sc_nextdsp;

	uint8_t	sc_scntl0;
	uint8_t sc_dmode;
	uint8_t sc_dwt;
	uint8_t sc_ctest7;
};

#define	oosiop_read_1(sc, addr)						\
    bus_space_read_1((sc)->sc_bst, (sc)->sc_bsh, (addr))
#define	oosiop_write_1(sc, addr, data)					\
    bus_space_write_1((sc)->sc_bst, (sc)->sc_bsh, (addr), (data))
/* XXX byte swapping should be handled by MD bus_space(9)? */
#define	oosiop_read_4(sc, addr)						\
    letoh32(bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (addr)))
#define	oosiop_write_4(sc, addr, data)					\
    bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (addr), htole32(data))

void oosiop_attach(struct oosiop_softc *);
int oosiop_intr(struct oosiop_softc *);
