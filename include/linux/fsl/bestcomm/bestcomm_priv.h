/*
 * Private header for the MPC52xx processor BestComm driver
 *
 * By private, we mean that driver should not use it directly. It's meant
 * to be used by the BestComm engine driver itself and by the intermediate
 * layer between the core and the drivers.
 *
 * Copyright (C) 2006      Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2005      Varma Electronics Oy,
 *                         ( by Andrey Volkov <avolkov@varma-el.com> )
 * Copyright (C) 2003-2004 MontaVista, Software, Inc.
 *                         ( by Dale Farnsworth <dfarnsworth@mvista.com> )
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __BESTCOMM_PRIV_H__
#define __BESTCOMM_PRIV_H__

#include <linux/spinlock.h>
#include <linux/of.h>
#include <asm/io.h>
#include <asm/mpc52xx.h>

#include "sram.h"


/* ======================================================================== */
/* Engine related stuff                                                     */
/* ======================================================================== */

/* Zones sizes and needed alignments */
#define BCOM_MAX_TASKS		16
#define BCOM_MAX_VAR		24
#define BCOM_MAX_INC		8
#define BCOM_MAX_FDT		64
#define BCOM_MAX_CTX		20
#define BCOM_CTX_SIZE		(BCOM_MAX_CTX * sizeof(u32))
#define BCOM_CTX_ALIGN		0x100
#define BCOM_VAR_SIZE		(BCOM_MAX_VAR * sizeof(u32))
#define BCOM_INC_SIZE		(BCOM_MAX_INC * sizeof(u32))
#define BCOM_VAR_ALIGN		0x80
#define BCOM_FDT_SIZE		(BCOM_MAX_FDT * sizeof(u32))
#define BCOM_FDT_ALIGN		0x100

/**
 * struct bcom_tdt - Task Descriptor Table Entry
 *
 */
struct bcom_tdt {
	u32 start;
	u32 stop;
	u32 var;
	u32 fdt;
	u32 exec_status;	/* used internally by BestComm engine */
	u32 mvtp;		/* used internally by BestComm engine */
	u32 context;
	u32 litbase;
};

/**
 * struct bcom_engine
 *
 * This holds all info needed globaly to handle the engine
 */
struct bcom_engine {
	struct device_node		*ofnode;
	struct mpc52xx_sdma __iomem     *regs;
	phys_addr_t                      regs_base;

	struct bcom_tdt			*tdt;
	u32				*ctx;
	u32				*var;
	u32				*fdt;

	spinlock_t			lock;
};

extern struct bcom_engine *bcom_eng;


/* ======================================================================== */
/* Tasks related stuff                                                      */
/* ======================================================================== */

/* Tasks image header */
#define BCOM_TASK_MAGIC		0x4243544B	/* 'BCTK' */

struct bcom_task_header {
	u32	magic;
	u8	desc_size;	/* the size fields     */
	u8	var_size;	/* are given in number */
	u8	inc_size;	/* of 32-bits words    */
	u8	first_var;
	u8	reserved[8];
};

/* Descriptors structure & co */
#define BCOM_DESC_NOP		0x000001f8
#define BCOM_LCD_MASK		0x80000000
#define BCOM_DRD_EXTENDED	0x40000000
#define BCOM_DRD_INITIATOR_SHIFT	21

/* Tasks pragma */
#define BCOM_PRAGMA_BIT_RSV		7	/* reserved pragma bit */
#define BCOM_PRAGMA_BIT_PRECISE_INC	6	/* increment 0=when possible, */
						/*           1=iter end */
#define BCOM_PRAGMA_BIT_RST_ERROR_NO	5	/* don't reset errors on */
						/* task enable */
#define BCOM_PRAGMA_BIT_PACK		4	/* pack data enable */
#define BCOM_PRAGMA_BIT_INTEGER		3	/* data alignment */
						/* 0=frac(msb), 1=int(lsb) */
#define BCOM_PRAGMA_BIT_SPECREAD	2	/* XLB speculative read */
#define BCOM_PRAGMA_BIT_CW		1	/* write line buffer enable */
#define BCOM_PRAGMA_BIT_RL		0	/* read line buffer enable */

	/* Looks like XLB speculative read generates XLB errors when a buffer
	 * is at the end of the physical memory. i.e. when accessing the
	 * lasts words, the engine tries to prefetch the next but there is no
	 * next ...
	 */
#define BCOM_STD_PRAGMA		((0 << BCOM_PRAGMA_BIT_RSV)		| \
				 (0 << BCOM_PRAGMA_BIT_PRECISE_INC)	| \
				 (0 << BCOM_PRAGMA_BIT_RST_ERROR_NO)	| \
				 (0 << BCOM_PRAGMA_BIT_PACK)		| \
				 (0 << BCOM_PRAGMA_BIT_INTEGER)		| \
				 (0 << BCOM_PRAGMA_BIT_SPECREAD)	| \
				 (1 << BCOM_PRAGMA_BIT_CW)		| \
				 (1 << BCOM_PRAGMA_BIT_RL))

#define BCOM_PCI_PRAGMA		((0 << BCOM_PRAGMA_BIT_RSV)		| \
				 (0 << BCOM_PRAGMA_BIT_PRECISE_INC)	| \
				 (0 << BCOM_PRAGMA_BIT_RST_ERROR_NO)	| \
				 (0 << BCOM_PRAGMA_BIT_PACK)		| \
				 (1 << BCOM_PRAGMA_BIT_INTEGER)		| \
				 (0 << BCOM_PRAGMA_BIT_SPECREAD)	| \
				 (1 << BCOM_PRAGMA_BIT_CW)		| \
				 (1 << BCOM_PRAGMA_BIT_RL))

#define BCOM_ATA_PRAGMA		BCOM_STD_PRAGMA
#define BCOM_CRC16_DP_0_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_CRC16_DP_1_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_FEC_RX_BD_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_FEC_TX_BD_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_DP_0_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_DP_1_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_DP_2_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_DP_3_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_DP_BD_0_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_DP_BD_1_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_RX_BD_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_TX_BD_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_GEN_LPC_PRAGMA	BCOM_STD_PRAGMA
#define BCOM_PCI_RX_PRAGMA	BCOM_PCI_PRAGMA
#define BCOM_PCI_TX_PRAGMA	BCOM_PCI_PRAGMA

/* Initiators number */
#define BCOM_INITIATOR_ALWAYS	 0
#define BCOM_INITIATOR_SCTMR_0	 1
#define BCOM_INITIATOR_SCTMR_1	 2
#define BCOM_INITIATOR_FEC_RX	 3
#define BCOM_INITIATOR_FEC_TX	 4
#define BCOM_INITIATOR_ATA_RX	 5
#define BCOM_INITIATOR_ATA_TX	 6
#define BCOM_INITIATOR_SCPCI_RX	 7
#define BCOM_INITIATOR_SCPCI_TX	 8
#define BCOM_INITIATOR_PSC3_RX	 9
#define BCOM_INITIATOR_PSC3_TX	10
#define BCOM_INITIATOR_PSC2_RX	11
#define BCOM_INITIATOR_PSC2_TX	12
#define BCOM_INITIATOR_PSC1_RX	13
#define BCOM_INITIATOR_PSC1_TX	14
#define BCOM_INITIATOR_SCTMR_2	15
#define BCOM_INITIATOR_SCLPC	16
#define BCOM_INITIATOR_PSC5_RX	17
#define BCOM_INITIATOR_PSC5_TX	18
#define BCOM_INITIATOR_PSC4_RX	19
#define BCOM_INITIATOR_PSC4_TX	20
#define BCOM_INITIATOR_I2C2_RX	21
#define BCOM_INITIATOR_I2C2_TX	22
#define BCOM_INITIATOR_I2C1_RX	23
#define BCOM_INITIATOR_I2C1_TX	24
#define BCOM_INITIATOR_PSC6_RX	25
#define BCOM_INITIATOR_PSC6_TX	26
#define BCOM_INITIATOR_IRDA_RX	25
#define BCOM_INITIATOR_IRDA_TX	26
#define BCOM_INITIATOR_SCTMR_3	27
#define BCOM_INITIATOR_SCTMR_4	28
#define BCOM_INITIATOR_SCTMR_5	29
#define BCOM_INITIATOR_SCTMR_6	30
#define BCOM_INITIATOR_SCTMR_7	31

/* Initiators priorities */
#define BCOM_IPR_ALWAYS		7
#define BCOM_IPR_SCTMR_0	2
#define BCOM_IPR_SCTMR_1	2
#define BCOM_IPR_FEC_RX		6
#define BCOM_IPR_FEC_TX		5
#define BCOM_IPR_ATA_RX		7
#define BCOM_IPR_ATA_TX		7
#define BCOM_IPR_SCPCI_RX	2
#define BCOM_IPR_SCPCI_TX	2
#define BCOM_IPR_PSC3_RX	2
#define BCOM_IPR_PSC3_TX	2
#define BCOM_IPR_PSC2_RX	2
#define BCOM_IPR_PSC2_TX	2
#define BCOM_IPR_PSC1_RX	2
#define BCOM_IPR_PSC1_TX	2
#define BCOM_IPR_SCTMR_2	2
#define BCOM_IPR_SCLPC		2
#define BCOM_IPR_PSC5_RX	2
#define BCOM_IPR_PSC5_TX	2
#define BCOM_IPR_PSC4_RX	2
#define BCOM_IPR_PSC4_TX	2
#define BCOM_IPR_I2C2_RX	2
#define BCOM_IPR_I2C2_TX	2
#define BCOM_IPR_I2C1_RX	2
#define BCOM_IPR_I2C1_TX	2
#define BCOM_IPR_PSC6_RX	2
#define BCOM_IPR_PSC6_TX	2
#define BCOM_IPR_IRDA_RX	2
#define BCOM_IPR_IRDA_TX	2
#define BCOM_IPR_SCTMR_3	2
#define BCOM_IPR_SCTMR_4	2
#define BCOM_IPR_SCTMR_5	2
#define BCOM_IPR_SCTMR_6	2
#define BCOM_IPR_SCTMR_7	2


/* ======================================================================== */
/* API                                                                      */
/* ======================================================================== */

extern struct bcom_task *bcom_task_alloc(int bd_count, int bd_size, int priv_size);
extern void bcom_task_free(struct bcom_task *tsk);
extern int bcom_load_image(int task, u32 *task_image);
extern void bcom_set_initiator(int task, int initiator);


#define TASK_ENABLE             0x8000

/**
 * bcom_disable_prefetch - Hook to disable bus prefetching
 *
 * ATA DMA and the original MPC5200 need this due to silicon bugs.  At the
 * moment disabling prefetch is a one-way street.  There is no mechanism
 * in place to turn prefetch back on after it has been disabled.  There is
 * no reason it couldn't be done, it would just be more complex to implement.
 */
static inline void bcom_disable_prefetch(void)
{
	u16 regval;

	regval = in_be16(&bcom_eng->regs->PtdCntrl);
	out_be16(&bcom_eng->regs->PtdCntrl, regval | 1);
};

static inline void
bcom_enable_task(int task)
{
        u16 reg;
        reg = in_be16(&bcom_eng->regs->tcr[task]);
        out_be16(&bcom_eng->regs->tcr[task],  reg | TASK_ENABLE);
}

static inline void
bcom_disable_task(int task)
{
        u16 reg = in_be16(&bcom_eng->regs->tcr[task]);
        out_be16(&bcom_eng->regs->tcr[task], reg & ~TASK_ENABLE);
}


static inline u32 *
bcom_task_desc(int task)
{
	return bcom_sram_pa2va(bcom_eng->tdt[task].start);
}

static inline int
bcom_task_num_descs(int task)
{
	return (bcom_eng->tdt[task].stop - bcom_eng->tdt[task].start)/sizeof(u32) + 1;
}

static inline u32 *
bcom_task_var(int task)
{
	return bcom_sram_pa2va(bcom_eng->tdt[task].var);
}

static inline u32 *
bcom_task_inc(int task)
{
	return &bcom_task_var(task)[BCOM_MAX_VAR];
}


static inline int
bcom_drd_is_extended(u32 desc)
{
	return (desc) & BCOM_DRD_EXTENDED;
}

static inline int
bcom_desc_is_drd(u32 desc)
{
	return !(desc & BCOM_LCD_MASK) && desc != BCOM_DESC_NOP;
}

static inline int
bcom_desc_initiator(u32 desc)
{
	return (desc >> BCOM_DRD_INITIATOR_SHIFT) & 0x1f;
}

static inline void
bcom_set_desc_initiator(u32 *desc, int initiator)
{
	*desc = (*desc & ~(0x1f << BCOM_DRD_INITIATOR_SHIFT)) |
			((initiator & 0x1f) << BCOM_DRD_INITIATOR_SHIFT);
}


static inline void
bcom_set_task_pragma(int task, int pragma)
{
	u32 *fdt = &bcom_eng->tdt[task].fdt;
	*fdt = (*fdt & ~0xff) | pragma;
}

static inline void
bcom_set_task_auto_start(int task, int next_task)
{
	u16 __iomem *tcr = &bcom_eng->regs->tcr[task];
	out_be16(tcr, (in_be16(tcr) & ~0xff) | 0x00c0 | next_task);
}

static inline void
bcom_set_tcr_initiator(int task, int initiator)
{
	u16 __iomem *tcr = &bcom_eng->regs->tcr[task];
	out_be16(tcr, (in_be16(tcr) & ~0x1f00) | ((initiator & 0x1f) << 8));
}


#endif /* __BESTCOMM_PRIV_H__ */

