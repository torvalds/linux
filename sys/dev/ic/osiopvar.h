/*	$OpenBSD: osiopvar.h,v 1.13 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: osiopvar.h,v 1.3 2002/05/14 02:58:35 matt Exp $	*/

/*
 * Copyright (c) 2001 Izumi Tsutsui.  All rights reserved.
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

/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)siopvar.h	7.1 (Berkeley) 5/8/90
 */

#define osiop_read_1(sc, reg)					\
    bus_space_read_1((sc)->sc_bst, (sc)->sc_reg, reg)
#define osiop_write_1(sc, reg, val)				\
    bus_space_write_1((sc)->sc_bst, (sc)->sc_reg, reg, val)

#define osiop_read_4(sc, reg)					\
    bus_space_read_4((sc)->sc_bst, (sc)->sc_reg, reg)
#define osiop_write_4(sc, reg, val)				\
    bus_space_write_4((sc)->sc_bst, (sc)->sc_reg, reg, val)     
        
/*
 * The largest single request will be MAXPHYS bytes which will require
 * at most MAXPHYS/NBPG+1 chain elements to describe, i.e. if none of
 * the buffer pages are physically contiguous (MAXPHYS/NBPG) and the
 * buffer is not page aligned (+1).
 */
/* XXX This should be (MAXPHYS / NBPG + 1), but hardcoded in script */
#define OSIOP_NSG	(16 + 1)
#if MAXPHYS > (PAGE_SIZE * (OSIOP_NSG - 1))
#define OSIOP_MAX_XFER	(PAGE_SIZE * (OSIOP_NSG - 1))
#else
#define OSIOP_MAX_XFER	MAXPHYS
#endif

#define OSIOP_NTGT 8
#define OSIOP_NACB 32	/* XXX (PAGE_SIZE / sizeof(osiop_ds)) is better? */

/*
 * Data Structure for SCRIPTS program
 */
typedef struct buf_table {
	u_int32_t count;
	u_int32_t addr;
} buf_table_t;

struct osiop_ds {
	u_int32_t scsi_addr;		/* SCSI ID & sync */
	u_int32_t pad1;
	buf_table_t id;			/* Identify message */
	buf_table_t cmd;		/* SCSI command */
	buf_table_t status;		/* Status */
	buf_table_t msg;		/* Message */
	buf_table_t msgin;		/* Message in */
	buf_table_t extmsg;		/* Extended message in */
	buf_table_t synmsg;		/* Sync transfer request */
	buf_table_t data[OSIOP_NSG];	/* DMA S/G buffers */

	u_int8_t msgout[8];
	u_int8_t msgbuf[8];
	u_int8_t stat[8];

	struct scsi_generic scsi_cmd;	/* DMA'able copy of xs->cmd */
	u_int32_t pad[1+3];		/* pad to 256 bytes */
} __packed;

/* status can hold the SCSI_* status values, and 2 additional values: */
#define SCSI_OSIOP_NOCHECK	0xfe	/* don't check the scsi status */
#define SCSI_OSIOP_NOSTATUS	0xff	/* device didn't report status */

#define MSG_INVALID		0xff	/* dummy value for message buffer */

#define OSIOP_DSOFF(x)		offsetof(struct osiop_ds, x)
#define OSIOP_DSIDOFF		OSIOP_DSOFF(msgout[0])
#define OSIOP_DSMSGOFF		OSIOP_DSOFF(msgbuf[0])
#define OSIOP_DSMSGINOFF	OSIOP_DSOFF(msgbuf[1])
#define OSIOP_DSEXTMSGOFF	OSIOP_DSOFF(msgbuf[2])
#define OSIOP_DSSYNMSGOFF	OSIOP_DSOFF(msgbuf[3])
#define OSIOP_DSSTATOFF		OSIOP_DSOFF(stat[0])
#define OSIOP_DSCMDOFF		OSIOP_DSOFF(scsi_cmd)

/*
 * ACB. Holds additional information for each SCSI command Comments:
 * Basically, we refrain from fiddling with the scsi_xfer struct
 * (except do the expected updating of return values).
 * We'll generally update: xs->{flags,resid,error,status} and
 * occasionally xs->retries.
 */
struct osiop_acb {
	TAILQ_ENTRY(osiop_acb) chain;
	struct scsi_xfer *xs;	/* SCSI xfer ctrl block from upper layer */
	struct osiop_softc *sc;	/* points back to our adapter */

	bus_dmamap_t datadma;	/* DMA map for data transfer */

	struct osiop_ds *ds;	/* data structure for this acb */
	bus_size_t dsoffset;	/* offset of data structure for this acb */

	int	xsflags;	/* copy of xs->flags */
	int	datalen;
	void *data;		/* transfer data buffer ptr */

	bus_addr_t curaddr;	/* current transfer data buffer */
	bus_size_t curlen;	/* current transfer data length */

	int status;		/* status of this acb */
/* status defs */
#define ACB_S_FREE	0	/* cmd slot is free */
#define ACB_S_READY	1	/* cmd slot is waiting for processing */
#define ACB_S_ACTIVE	2	/* cmd slot is being processed */
#define ACB_S_DONE	3	/* cmd slot has been processed */

	int flags;		/* cmd slot flags */
#define ACB_F_TIMEOUT	0x01	/* command timeout */
#define ACB_F_AUTOSENSE 0x02	/* request sense due to SCSI_CHECK */

	u_int8_t intstat;	/* buffer to save sc_flags on disconnect */
};

/*
 * Some info about each (possible) target on the SCSI bus.  This should
 * probably have been a "per target+lunit" structure, but we'll leave it at
 * this for now.  Is there a way to reliably hook it up to sc->fordriver??
 */
struct osiop_tinfo {
	int cmds;		/* number of commands processed */
	int senses;		/* number of sense requests */
	int dconns;		/* number of disconnects */
	int touts;		/* number of timeouts */
	int perrs;		/* number of parity errors */
	int lubusy;		/* What local units/subr. are busy? */
	int period;		/* Period suggestion */
	int offset;		/* Offset suggestion */
	int flags;		/* misc flags per each target */
#define TI_NOSYNC	0x01	/* disable sync xfer on this target */
#define TI_NODISC	0x02	/* disable disconnect on this target */
	int state;		/* negotiation state */
	u_int8_t sxfer;		/* value for SXFER reg */
	u_int8_t sbcl;		/* value for SBCL reg */
};

struct osiop_softc {
	struct device sc_dev;

	bus_space_tag_t sc_bst;		/* bus space tag */
	bus_space_handle_t sc_reg;	/* register I/O handle */

	bus_dma_tag_t sc_dmat;		/* bus dma tag */
	bus_dmamap_t sc_scrdma;		/* script dma map */
	bus_dmamap_t sc_dsdma;		/* script data dma map */

	u_int32_t *sc_script;		/* ptr to script memory */
	struct osiop_ds *sc_ds;		/* ptr to data structure memory */

	int sc_id;			/* adapter SCSI id */
	int sc_active;			/* number of active I/O's */

	struct osiop_acb *sc_nexus;	/* current command */
	struct osiop_acb *sc_acb;	/* the real command blocks */

	/* Lists of command blocks */
	TAILQ_HEAD(acb_list, osiop_acb) free_list,
				        ready_list,
				        nexus_list;
	struct mutex free_list_mtx;

	struct scsi_iopool  sc_iopool;

	struct osiop_tinfo sc_tinfo[OSIOP_NTGT];

	int sc_clock_freq;
	int sc_tcp[4];
	int sc_flags;
#define OSIOP_INTSOFF	0x80	/* Interrupts turned off */
#define OSIOP_INTDEFER	0x40	/* MD interrupt has been deferred */
#define OSIOP_NODMA	0x02	/* No DMA transfer */
#define OSIOP_ALIVE	0x01	/* controller initialized */

	int sc_cfflags;			/* copy of config flags */

	int sc_minsync;

	u_int8_t sc_dstat;
	u_int8_t sc_sstat0;
	u_int8_t sc_sstat1;
	u_int8_t sc_istat;
	u_int8_t sc_dcntl;
	u_int8_t sc_ctest7;
	u_int8_t sc_dmode;
	u_int8_t sc_sien;
	u_int8_t sc_dien;
};

/* negotiation states */
#define NEG_INIT	0	/* Initial negotiate state */
#define NEG_SYNC	NEG_INIT /* Negotiate synch transfers */
#define NEG_WAITS	1	/* Waiting for synch negotiation response */
#define NEG_DONE	2	/* Wide and/or sync negotiation done */

void osiop_attach(struct osiop_softc *);
void osiop_intr(struct osiop_softc *);
