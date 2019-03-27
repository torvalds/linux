/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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
 * $FreeBSD$
 */

/* ATA register defines */
#define ATA_DATA                        0       /* (RW) data */

#define ATA_FEATURE                     1       /* (W) feature */
#define         ATA_F_DMA               0x01    /* enable DMA */
#define         ATA_F_OVL               0x02    /* enable overlap */

#define ATA_COUNT                       2       /* (W) sector count */

#define ATA_SECTOR                      3       /* (RW) sector # */
#define ATA_CYL_LSB                     4       /* (RW) cylinder# LSB */
#define ATA_CYL_MSB                     5       /* (RW) cylinder# MSB */
#define ATA_DRIVE                       6       /* (W) Sector/Drive/Head */
#define         ATA_D_LBA               0x40    /* use LBA addressing */
#define         ATA_D_IBM               0xa0    /* 512 byte sectors, ECC */

#define ATA_COMMAND                     7       /* (W) command */

#define ATA_ERROR                       8       /* (R) error */
#define         ATA_E_ILI               0x01    /* illegal length */
#define         ATA_E_NM                0x02    /* no media */
#define         ATA_E_ABORT             0x04    /* command aborted */
#define         ATA_E_MCR               0x08    /* media change request */
#define         ATA_E_IDNF              0x10    /* ID not found */
#define         ATA_E_MC                0x20    /* media changed */
#define         ATA_E_UNC               0x40    /* uncorrectable data */
#define         ATA_E_ICRC              0x80    /* UDMA crc error */
#define		ATA_E_ATAPI_SENSE_MASK	0xf0	/* ATAPI sense key mask */

#define ATA_IREASON                     9       /* (R) interrupt reason */
#define         ATA_I_CMD               0x01    /* cmd (1) | data (0) */
#define         ATA_I_IN                0x02    /* read (1) | write (0) */
#define         ATA_I_RELEASE           0x04    /* released bus (1) */
#define         ATA_I_TAGMASK           0xf8    /* tag mask */

#define ATA_STATUS                      10      /* (R) status */
#define ATA_ALTSTAT                     11      /* (R) alternate status */
#define         ATA_S_ERROR             0x01    /* error */
#define         ATA_S_INDEX             0x02    /* index */
#define         ATA_S_CORR              0x04    /* data corrected */
#define         ATA_S_DRQ               0x08    /* data request */
#define         ATA_S_DSC               0x10    /* drive seek completed */
#define         ATA_S_SERVICE           0x10    /* drive needs service */
#define         ATA_S_DWF               0x20    /* drive write fault */
#define         ATA_S_DMA               0x20    /* DMA ready */
#define         ATA_S_READY             0x40    /* drive ready */
#define         ATA_S_BUSY              0x80    /* busy */

#define ATA_CONTROL                     12      /* (W) control */
#define         ATA_A_IDS               0x02    /* disable interrupts */
#define         ATA_A_RESET             0x04    /* RESET controller */
#define         ATA_A_4BIT              0x08    /* 4 head bits */
#define         ATA_A_HOB               0x80    /* High Order Byte enable */

/* SATA register defines */
#define ATA_SSTATUS                     13
#define         ATA_SS_DET_MASK         0x0000000f
#define         ATA_SS_DET_NO_DEVICE    0x00000000
#define         ATA_SS_DET_DEV_PRESENT  0x00000001
#define         ATA_SS_DET_PHY_ONLINE   0x00000003
#define         ATA_SS_DET_PHY_OFFLINE  0x00000004

#define         ATA_SS_SPD_MASK         0x000000f0
#define         ATA_SS_SPD_NO_SPEED     0x00000000
#define         ATA_SS_SPD_GEN1         0x00000010
#define         ATA_SS_SPD_GEN2         0x00000020
#define         ATA_SS_SPD_GEN3         0x00000030

#define         ATA_SS_IPM_MASK         0x00000f00
#define         ATA_SS_IPM_NO_DEVICE    0x00000000
#define         ATA_SS_IPM_ACTIVE       0x00000100
#define         ATA_SS_IPM_PARTIAL      0x00000200
#define         ATA_SS_IPM_SLUMBER      0x00000600

#define ATA_SERROR                      14
#define         ATA_SE_DATA_CORRECTED   0x00000001
#define         ATA_SE_COMM_CORRECTED   0x00000002
#define         ATA_SE_DATA_ERR         0x00000100
#define         ATA_SE_COMM_ERR         0x00000200
#define         ATA_SE_PROT_ERR         0x00000400
#define         ATA_SE_HOST_ERR         0x00000800
#define         ATA_SE_PHY_CHANGED      0x00010000
#define         ATA_SE_PHY_IERROR       0x00020000
#define         ATA_SE_COMM_WAKE        0x00040000
#define         ATA_SE_DECODE_ERR       0x00080000
#define         ATA_SE_PARITY_ERR       0x00100000
#define         ATA_SE_CRC_ERR          0x00200000
#define         ATA_SE_HANDSHAKE_ERR    0x00400000
#define         ATA_SE_LINKSEQ_ERR      0x00800000
#define         ATA_SE_TRANSPORT_ERR    0x01000000
#define         ATA_SE_UNKNOWN_FIS      0x02000000

#define ATA_SCONTROL                    15
#define         ATA_SC_DET_MASK         0x0000000f
#define         ATA_SC_DET_IDLE         0x00000000
#define         ATA_SC_DET_RESET        0x00000001
#define         ATA_SC_DET_DISABLE      0x00000004

#define         ATA_SC_SPD_MASK         0x000000f0
#define         ATA_SC_SPD_NO_SPEED     0x00000000
#define         ATA_SC_SPD_SPEED_GEN1   0x00000010
#define         ATA_SC_SPD_SPEED_GEN2   0x00000020
#define         ATA_SC_SPD_SPEED_GEN3   0x00000030

#define         ATA_SC_IPM_MASK         0x00000f00
#define         ATA_SC_IPM_NONE         0x00000000
#define         ATA_SC_IPM_DIS_PARTIAL  0x00000100
#define         ATA_SC_IPM_DIS_SLUMBER  0x00000200

#define ATA_SACTIVE                     16

/*
 * Global registers
 */
#define SIIS_GCTL		0x0040		/* Global Control	*/
#define SIIS_GCTL_GRESET	  0x80000000	/* Global Reset		*/
#define SIIS_GCTL_MSIACK	  0x40000000	/* MSI Ack		*/
#define SIIS_GCTL_I2C_IE	  0x20000000	/* I2C int enable	*/
#define SIIS_GCTL_300CAP	  0x01000000	/* 3Gb/s capable (R)	*/
#define SIIS_GCTL_PIE(n)	  (1 << (n))	/* Port int enable	*/
#define SIIS_IS			0x0044		/* Interrupt Status	*/
#define SIIS_IS_I2C		  0x20000000	/* I2C Int Status	*/
#define SIIS_IS_PORT(n)		  (1 << (n))	/* Port interrupt stat	*/
#define SIIS_PHYCONF		0x0048		/* PHY Configuration */
#define SIIS_BIST_CTL		0x0050
#define SIIS_BIST_PATTERN	0x0054	/* 32 bit pattern */
#define SIIS_BIST_STATUS	0x0058
#define SIIS_I2C_CTL		0x0060
#define SIIS_I2C_STS		0x0064
#define SIIS_I2C_SADDR		0x0068
#define SIIS_I2C_DATA		0x006C
#define SIIS_FLASH_ADDR		0x0070
#define SIIS_GPIO		0x0074

/*
 * Port registers
 */

#define SIIS_P_LRAM		0x0000
#define   SIIS_P_LRAM_SLOT(i)	  (SIIS_P_LRAM + i * 128)
#define SIIS_P_PMPSTS(i)	(0x0F80 + i * 8)
#define SIIS_P_PMPQACT(i)	(0x0F80 + i * 8 + 4)
#define SIIS_P_STS		0x1000
#define SIIS_P_CTLSET		0x1000
#define SIIS_P_CTLCLR		0x1004
#define   SIIS_P_CTL_READY	  0x80000000
#define   SIIS_P_CTL_OOBB	  0x02000000
#define   SIIS_P_CTL_ACT	  0x001F0000
#define   SIIS_P_CTL_ACT_SHIFT	  16
#define   SIIS_P_CTL_LED_ON	  0x00008000
#define   SIIS_P_CTL_AIA	  0x00004000
#define   SIIS_P_CTL_PME	  0x00002000
#define   SIIS_P_CTL_IA		  0x00001000
#define   SIIS_P_CTL_IR		  0x00000800
#define   SIIS_P_CTL_32BIT	  0x00000400
#define   SIIS_P_CTL_SCR_DIS	  0x00000200
#define   SIIS_P_CTL_CONT_DIS	  0x00000100
#define   SIIS_P_CTL_TBIST	  0x00000080
#define   SIIS_P_CTL_RESUME	  0x00000040
#define   SIIS_P_CTL_PLENGTH	  0x00000020
#define   SIIS_P_CTL_LED_DIS	  0x00000010
#define   SIIS_P_CTL_INT_NCOR	  0x00000008
#define   SIIS_P_CTL_PORT_INIT  0x00000004
#define   SIIS_P_CTL_DEV_RESET  0x00000002
#define   SIIS_P_CTL_PORT_RESET 0x00000001
#define SIIS_P_IS		0x1008
#define   SIIS_P_IX_SDBN	  0x00000800
#define   SIIS_P_IX_HS_ET	  0x00000400
#define   SIIS_P_IX_CRC_ET	  0x00000200
#define   SIIS_P_IX_8_10_ET	  0x00000100
#define   SIIS_P_IX_DEX		  0x00000080
#define   SIIS_P_IX_UNRECFIS	  0x00000040
#define   SIIS_P_IX_COMWAKE	  0x00000020
#define   SIIS_P_IX_PHYRDYCHG	  0x00000010
#define   SIIS_P_IX_PMCHG	  0x00000008
#define   SIIS_P_IX_READY	  0x00000004
#define   SIIS_P_IX_COMMERR	  0x00000002
#define   SIIS_P_IX_COMMCOMP	  0x00000001
#define   SIIS_P_IX_ENABLED	  SIIS_P_IX_COMMCOMP | SIIS_P_IX_COMMERR | \
    SIIS_P_IX_PHYRDYCHG | SIIS_P_IX_SDBN
#define SIIS_P_IESET		0x1010
#define SIIS_P_IECLR		0x1014
#define SIIS_P_CACTU		0x101C
#define SIIS_P_CMDEFIFO		0x1020
#define SIIS_P_CMDERR		0x1024
#define   SIIS_P_CMDERR_DEV		1
#define   SIIS_P_CMDERR_SDB		2
#define   SIIS_P_CMDERR_DATAFIS		3
#define   SIIS_P_CMDERR_SENDFIS		4
#define   SIIS_P_CMDERR_INCSTATE	5
#define   SIIS_P_CMDERR_DIRECTION	6
#define   SIIS_P_CMDERR_UNDERRUN	7
#define   SIIS_P_CMDERR_OVERRUN		8
#define   SIIS_P_CMDERR_LLOVERRUN	9
#define   SIIS_P_CMDERR_PPE		11
#define   SIIS_P_CMDERR_SGTALIGN	16
#define   SIIS_P_CMDERR_PCITASGT	17
#define   SIIS_P_CMDERR_OCIMASGT	18
#define   SIIS_P_CMDERR_PCIPESGT	19
#define   SIIS_P_CMDERR_PRBALIGN	24
#define   SIIS_P_CMDERR_PCITAPRB	25
#define   SIIS_P_CMDERR_PCIMAPRB	26
#define   SIIS_P_CMDERR_PCIPEPRB	27
#define   SIIS_P_CMDERR_PCITADATA	33
#define   SIIS_P_CMDERR_PCIMADATA	34
#define   SIIS_P_CMDERR_PCIPEDATA	35
#define   SIIS_P_CMDERR_SERVICE		36
#define SIIS_P_FISCFG		0x1028
#define SIIS_P_PCIEFIFOTH	0x102C
#define SIIS_P_8_10_DEC_ERR	0x1040
#define SIIS_P_CRC_ERR		0x1044
#define SIIS_P_HS_ERR		0x1048
#define SIIS_P_PHYCFG		0x1050
#define SIIS_P_SS		0x1800
#define   SIIS_P_SS_ATTN	  0x80000000
#define SIIS_P_CACTL(i)		(0x1C00 + i * 8)
#define SIIS_P_CACTH(i)		(0x1C00 + i * 8 + 4)
#define SIIS_P_CTX		0x1E04
#define   SIIS_P_CTX_SLOT	  0x0000001F
#define   SIIS_P_CTX_SLOT_SHIFT	  0
#define   SIIS_P_CTX_PMP	  0x000001E0
#define   SIIS_P_CTX_PMP_SHIFT	  5

#define SIIS_P_SCTL		0x1F00
#define SIIS_P_SSTS		0x1F04
#define SIIS_P_SERR		0x1F08
#define SIIS_P_SACT		0x1F0C
#define SIIS_P_SNTF		0x1F10

#define SIIS_MAX_PORTS		4
#define SIIS_MAX_SLOTS		31

#define SIIS_OFFSET		0x100
#define SIIS_STEP		0x80

/* Just to be sure, if building as module. */
#if MAXPHYS < 512 * 1024
#undef MAXPHYS
#define MAXPHYS			512 * 1024
#endif
/* Pessimistic prognosis on number of required S/G entries */
#define SIIS_SG_ENTRIES		(roundup(btoc(MAXPHYS), 4) + 1)
/* Command tables. Up to 32 commands, Each, 128byte aligned. */
#define SIIS_CT_OFFSET	0
#define SIIS_CT_SIZE		(32 + 16 + SIIS_SG_ENTRIES * 16)
/* Total main work area. */
#define SIIS_WORK_SIZE		(SIIS_CT_OFFSET + SIIS_CT_SIZE * SIIS_MAX_SLOTS)

struct siis_dma_prd {
    u_int64_t			dba;
    u_int32_t			dbc;
    u_int32_t			control;
#define SIIS_PRD_TRM		0x80000000
#define SIIS_PRD_LNK		0x40000000
#define SIIS_PRD_DRD		0x20000000
#define SIIS_PRD_XCF		0x10000000
} __packed;

struct siis_cmd_ata {
    struct siis_dma_prd	prd[1 + SIIS_SG_ENTRIES];
} __packed;

struct siis_cmd_atapi {
    u_int8_t			ccb[16];
    struct siis_dma_prd	prd[SIIS_SG_ENTRIES];
} __packed;

struct siis_cmd {
    u_int16_t			control;
#define SIIS_PRB_PROTOCOL_OVERRIDE	0x0001
#define SIIS_PRB_RETRANSMIT		0x0002
#define SIIS_PRB_EXTERNAL_COMMAND	0x0004
#define SIIS_PRB_RECEIVE		0x0008
#define SIIS_PRB_PACKET_READ		0x0010
#define SIIS_PRB_PACKET_WRITE		0x0020
#define SIIS_PRB_INTERRUPT_MASK		0x0040
#define SIIS_PRB_SOFT_RESET		0x0080
    u_int16_t			protocol_override;
#define SIIS_PRB_PROTO_PACKET		0x0001
#define SIIS_PRB_PROTO_TCQ		0x0002
#define SIIS_PRB_PROTO_NCQ		0x0004
#define SIIS_PRB_PROTO_READ		0x0008
#define SIIS_PRB_PROTO_WRITE		0x0010
#define SIIS_PRB_PROTO_TRANSPARENT	0x0020
    u_int32_t			transfer_count;
    u_int8_t			fis[24];
    union {
	struct siis_cmd_ata	ata;
	struct siis_cmd_atapi	atapi;
    } u;
} __packed;

/* misc defines */
#define ATA_IRQ_RID                     0
#define ATA_INTR_FLAGS                  (INTR_MPSAFE|INTR_TYPE_BIO|INTR_ENTROPY)

struct ata_dmaslot {
    bus_dmamap_t                data_map;       /* data DMA map */
    int				nsegs;		/* Number of segs loaded */
};

/* structure holding DMA related information */
struct ata_dma {
    bus_dma_tag_t               work_tag;       /* workspace DMA tag */
    bus_dmamap_t                work_map;       /* workspace DMA map */
    uint8_t                     *work;          /* workspace */
    bus_addr_t                  work_bus;       /* bus address of work */
    bus_dma_tag_t               data_tag;       /* data DMA tag */
};

enum siis_slot_states {
	SIIS_SLOT_EMPTY,
	SIIS_SLOT_LOADING,
	SIIS_SLOT_RUNNING,
	SIIS_SLOT_WAITING
};

struct siis_slot {
    device_t                    dev;            /* Device handle */
    u_int8_t			slot;           /* Number of this slot */
    enum siis_slot_states	state;          /* Slot state */
    union ccb			*ccb;		/* CCB occupying slot */
    struct ata_dmaslot          dma;            /* DMA data of this slot */
    struct callout              timeout;        /* Execution timeout */
};

struct siis_device {
	int			revision;
	int			mode;
	u_int			bytecount;
	u_int			atapi;
	u_int			tags;
	u_int			caps;
};

/* structure describing an ATA channel */
struct siis_channel {
	device_t		dev;            /* Device handle */
	int			unit;           /* Physical channel */
	struct resource		*r_mem;		/* Memory of this channel */
	struct resource		*r_irq;         /* Interrupt of this channel */
	void			*ih;            /* Interrupt handle */
	struct ata_dma		dma;            /* DMA data */
	struct cam_sim		*sim;
	struct cam_path		*path;
	struct cdev		*led;		/* Activity led led(4) cdev. */
	int			quirks;
	int			pm_level;	/* power management level */

	struct siis_slot	slot[SIIS_MAX_SLOTS];
	union ccb		*hold[SIIS_MAX_SLOTS];
	struct mtx		mtx;		/* state lock */
	int			devices;        /* What is present */
	int			pm_present;	/* PM presence reported */
	uint32_t		oslots;		/* Occupied slots */
	uint32_t		rslots;		/* Running slots */
	uint32_t		aslots;		/* Slots with atomic commands */
	uint32_t		eslots;		/* Slots in error */
	uint32_t		toslots;	/* Slots in timeout */
	int			numrslots;	/* Number of running slots */
	int			numtslots[SIIS_MAX_SLOTS]; /* Number of tagged slots */
	int			numhslots;	/* Number of held slots */
	int			recoverycmd;	/* Our READ LOG active */
	int			fatalerr;	/* Fatal error happened */
	int			recovery;	/* Some slots are in error */
	union ccb		*frozen;	/* Frozen command */

	struct siis_device	user[16];	/* User-specified settings */
	struct siis_device	curr[16];	/* Current settings */
};

/* structure describing a SIIS controller */
struct siis_controller {
	device_t		dev;
	int			r_grid;
	struct resource		*r_gmem;
	int			r_rid;
	struct resource		*r_mem;
	struct rman		sc_iomem;
	struct siis_controller_irq {
		struct resource		*r_irq;
		void			*handle;
		int			r_irq_rid;
	} irq;
	int			quirks;
	int			channels;
	uint32_t		gctl;
	struct {
		void			(*function)(void *);
		void			*argument;
	} interrupt[SIIS_MAX_PORTS];
};

enum siis_err_type {
	SIIS_ERR_NONE,		/* No error */
	SIIS_ERR_INVALID,	/* Error detected by us before submitting. */
	SIIS_ERR_INNOCENT,	/* Innocent victim. */
	SIIS_ERR_TFE,		/* Task File Error. */
	SIIS_ERR_SATA,		/* SATA error. */
	SIIS_ERR_TIMEOUT,	/* Command execution timeout. */
	SIIS_ERR_NCQ,		/* NCQ command error. CCB should be put on hold
				 * until READ LOG executed to reveal error. */
};

/* macros to hide busspace uglyness */
#define ATA_INB(res, offset) \
	bus_read_1((res), (offset))
#define ATA_INW(res, offset) \
	bus_read_2((res), (offset))
#define ATA_INL(res, offset) \
	bus_read_4((res), (offset))
#define ATA_INSW(res, offset, addr, count) \
	bus_read_multi_2((res), (offset), (addr), (count))
#define ATA_INSW_STRM(res, offset, addr, count) \
	bus_read_multi_stream_2((res), (offset), (addr), (count))
#define ATA_INSL(res, offset, addr, count) \
	bus_read_multi_4((res), (offset), (addr), (count))
#define ATA_INSL_STRM(res, offset, addr, count) \
	bus_read_multi_stream_4((res), (offset), (addr), (count))
#define ATA_OUTB(res, offset, value) \
	bus_write_1((res), (offset), (value))
#define ATA_OUTW(res, offset, value) \
	bus_write_2((res), (offset), (value))
#define ATA_OUTL(res, offset, value) \
	bus_write_4((res), (offset), (value))
#define ATA_OUTSW(res, offset, addr, count) \
	bus_write_multi_2((res), (offset), (addr), (count))
#define ATA_OUTSW_STRM(res, offset, addr, count) \
	bus_write_multi_stream_2((res), (offset), (addr), (count))
#define ATA_OUTSL(res, offset, addr, count) \
	bus_write_multi_4((res), (offset), (addr), (count))
#define ATA_OUTSL_STRM(res, offset, addr, count) \
	bus_write_multi_stream_4((res), (offset), (addr), (count))
