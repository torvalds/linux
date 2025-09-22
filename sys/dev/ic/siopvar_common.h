/*	$OpenBSD: siopvar_common.h,v 1.32 2020/07/22 13:16:04 krw Exp $ */
/*	$NetBSD: siopvar_common.h,v 1.33 2005/11/18 23:10:32 bouyer Exp $ */

/*
 * Copyright (c) 2000 Manuel Bouyer.
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

/* common struct and routines used by siop and esiop */

#ifndef SIOP_DEFAULT_TARGET
#define SIOP_DEFAULT_TARGET 7
#endif

/* tables used by SCRIPT */
typedef struct scr_table {
	u_int32_t count;
	u_int32_t addr;
} __packed scr_table_t;

/* Number of scatter/gather entries */
/* XXX Ensure alignment of siop_xfer's. */
#define SIOP_NSG	17	/* XXX (MAXPHYS/PAGE_SIZE + 1) */
#define SIOP_MAXFER	((SIOP_NSG - 1) * PAGE_SIZE)

/*
 * This structure interfaces the SCRIPT with the driver; it describes a full
 * transfer. If you change something here, don't forget to update offsets in
 * {s,es}iop.ss
 */
struct siop_common_xfer {
	u_int8_t msg_out[16];		/*   0 */
	u_int8_t msg_in[16];		/*  16 */
	u_int32_t status;		/*  32 */
	u_int32_t pad1;			/*  36 */
	u_int32_t id;			/*  40 */
	struct scsi_generic xscmd; 	/*  44 */
	scr_table_t t_msgin;		/*  60 */
	scr_table_t t_extmsgin;		/*  68 */
	scr_table_t t_extmsgdata; 	/*  76 */
	scr_table_t t_msgout;		/*  84 */
	scr_table_t cmd;		/*  92 */
	scr_table_t t_status;		/* 100 */
	scr_table_t data[SIOP_NSG]; 	/* 108 */
} __packed;

/* status can hold the SCSI_* status values, and 2 additional values: */
#define SCSI_SIOP_NOCHECK	0xfe	/* don't check the scsi status */
#define SCSI_SIOP_NOSTATUS	0xff	/* device didn't report status */

/* offset is initialised to SIOP_NOOFFSET, used to check if it was updated */
#define SIOP_NOOFFSET 0xffffffff

/*
 * This describes a command handled by the SCSI controller
 */
struct siop_common_cmd {
	struct siop_common_softc *siop_sc; /* points back to our adapter */
	struct siop_common_target *siop_target; /* pointer to our target def */
	struct scsi_xfer *xs; /* xfer from the upper level */
	struct siop_common_xfer *siop_tables; /* tables for this cmd */
	struct scsi_sense_data *sense;
	bus_addr_t	dsa; /* DSA value to load */
	bus_dmamap_t	dmamap_data;
	int status;
	int flags;
	int tag;	/* tag used for tagged command queuing */
	int resid;	/* valid when CMDFL_RESID is set */
};

/* status defs */
#define CMDST_FREE		0 /* cmd slot is free */
#define CMDST_READY		1 /* cmd slot is waiting for processing */
#define CMDST_ACTIVE		2 /* cmd slot is being processed */
#define CMDST_SENSE		3 /* cmd slot is requesting sense */
#define CMDST_SENSE_ACTIVE	4 /* request sense active */
#define CMDST_SENSE_DONE 	5 /* request sense done */
#define CMDST_DONE		6 /* cmd slot has been processed */
/* flags defs */
#define CMDFL_TIMEOUT	0x0001 /* cmd timed out */
#define CMDFL_TAG	0x0002 /* tagged cmd */
#define CMDFL_RESID	0x0004 /* current offset in table is partial */

/* per-target struct */
struct siop_common_target {
	int status;	/* target status, see below */
	int flags;	/* target flags, see below */
	u_int32_t id;	/* for SELECT FROM */
	int period;
	int offset;
};

/* target status */
#define TARST_PROBING	0 /* target is being probed */
#define TARST_ASYNC	1 /* target needs sync/wide negotiation */
#define TARST_WIDE_NEG	2 /* target is doing wide negotiation */
#define TARST_SYNC_NEG	3 /* target is doing sync negotiation */
#define TARST_PPR_NEG	4 /* target is doing sync negotiation */
#define TARST_OK	5 /* sync/wide agreement is valid */

/* target flags */
#define TARF_SYNC	0x01 /* target can do sync */
#define TARF_WIDE	0x02 /* target can do wide */
#define TARF_TAG	0x04 /* target can do tags */
#define TARF_DT		0x08 /* target can do DT clocking */
#define TARF_ISWIDE	0x10 /* target is wide */
#define TARF_ISDT	0x20 /* target is doing DT clocking */

/* Driver internal state */
struct siop_common_softc {
	struct device sc_dev;
	u_int16_t sc_id;		/* adapter's target on bus */
	int features;			/* chip's features */
	int ram_size;
	int maxburst;
	int maxoff;
	int clock_div;			/* async. clock divider (scntl3) */
	int clock_period;		/* clock period (ns * 10) */
	int st_minsync;			/* min and max sync period, */
	int dt_minsync;
	int st_maxsync;			/* as sent in or PPR messages */
	int dt_maxsync;
	int mode;			/* current SE/LVD/HVD mode */
	bus_space_tag_t sc_rt;		/* bus_space registers tag */
	bus_space_handle_t sc_rh;	/* bus_space registers handle */
	bus_addr_t sc_raddr;		/* register addresses */
	bus_space_tag_t sc_ramt;	/* bus_space ram tag */
	bus_space_handle_t sc_ramh;	/* bus_space ram handle */
	bus_dma_tag_t sc_dmat;		/* bus DMA tag */
	void (*sc_reset)(struct siop_common_softc*); /* reset callback */
	bus_dmamap_t  sc_scriptdma;	/* DMA map for script */
	bus_addr_t sc_scriptaddr;	/* on-board ram or physical address */
	u_int32_t *sc_script;		/* script location in memory */
	struct siop_common_target *targets[16]; /* per-target states */
};

/* features */
#define SF_BUS_WIDE	0x00000001 /* wide bus */
#define SF_BUS_ULTRA	0x00000002 /* Ultra (20MHz) bus */
#define SF_BUS_ULTRA2	0x00000004 /* Ultra2 (40MHz) bus */
#define SF_BUS_ULTRA3	0x00000008 /* Ultra3 (80MHz) bus */
#define SF_BUS_DIFF	0x00000010 /* differential bus */

#define SF_CHIP_LED0	0x00000100 /* led on GPIO0 */
#define SF_CHIP_LEDC	0x00000200 /* led on GPIO0 with hardware control */
#define SF_CHIP_DBLR	0x00000400 /* clock doubler or quadrupler */
#define SF_CHIP_QUAD	0x00000800 /* clock quadrupler, with PPL */
#define SF_CHIP_FIFO	0x00001000 /* large fifo */
#define SF_CHIP_PF	0x00002000 /* Instructions prefetch */
#define SF_CHIP_RAM	0x00004000 /* on-board RAM */
#define SF_CHIP_LS	0x00008000 /* load/store instruction */
#define SF_CHIP_10REGS	0x00010000 /* 10 scratch registers */
#define SF_CHIP_DFBC	0x00020000 /* Use DFBC register */
#define SF_CHIP_DT	0x00040000 /* DT clocking */
#define SF_CHIP_GEBUG	0x00080000 /* SCSI gross error bug */
#define SF_CHIP_AAIP	0x00100000 /* Always generate AIP regardless of SNCTL4*/
#define SF_CHIP_BE	0x00200000 /* big-endian */

#define SF_PCI_RL	0x01000000 /* PCI read line */
#define SF_PCI_RM	0x02000000 /* PCI read multiple */
#define SF_PCI_BOF	0x04000000 /* PCI burst opcode fetch */
#define SF_PCI_CLS	0x08000000 /* PCI cache line size */
#define SF_PCI_WRI	0x10000000 /* PCI write and invalidate */

int	siop_common_attach(struct siop_common_softc *);
void	siop_common_reset(struct siop_common_softc *);
void	siop_setuptables(struct siop_common_cmd *);
int	siop_modechange(struct siop_common_softc *);

int	siop_wdtr_neg(struct siop_common_cmd *);
int	siop_sdtr_neg(struct siop_common_cmd *);
int     siop_ppr_neg(struct siop_common_cmd *);
void	siop_sdtr_msg(struct siop_common_cmd *, int, int, int);
void	siop_wdtr_msg(struct siop_common_cmd *, int, int);
void    siop_ppr_msg(struct siop_common_cmd *, int, int, int);
void	siop_update_xfer_mode(struct siop_common_softc *, int);
int	siop_iwr(struct siop_common_cmd *);
/* actions to take at return of siop_wdtr_neg() and siop_sdtr_neg() */
#define SIOP_NEG_NOP	0x0
#define SIOP_NEG_MSGOUT	0x1
#define SIOP_NEG_ACK	0x2

void 	siop_ma(struct siop_common_cmd *);
void 	siop_sdp(struct siop_common_cmd *, int);
void 	siop_update_resid(struct siop_common_cmd *, int);
void	siop_clearfifo(struct siop_common_softc *);
void	siop_resetbus(struct siop_common_softc *);

#define siop_htoc32(sc, x) \
  (((sc)->features & SF_CHIP_BE) ? htobe32((x)) : htole32((x)))

#define siop_ctoh32(sc, x) \
  (((sc)->features & SF_CHIP_BE) ? betoh32((x)) : letoh32((x)))
