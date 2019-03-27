/*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 * @file   al_hal_udma_regs_gen.h
 *
 * @brief C Header file for the UDMA general registers
 *
 */

#ifndef __AL_HAL_UDMA_GEN_REG_H
#define __AL_HAL_UDMA_GEN_REG_H

#include "al_hal_udma_iofic_regs.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct udma_gen_dma_misc {
	/* [0x0] Reserved register for the interrupt controller */
	uint32_t int_cfg;
	/* [0x4] Revision register */
	uint32_t revision;
	/* [0x8] Reserved for future use */
	uint32_t general_cfg_1;
	/* [0xc] Reserved for future use */
	uint32_t general_cfg_2;
	/* [0x10] Reserved for future use */
	uint32_t general_cfg_3;
	/* [0x14] Reserved for future use */
	uint32_t general_cfg_4;
	/* [0x18] General timer configuration */
	uint32_t general_cfg_5;
	uint32_t rsrvd[57];
};
struct udma_gen_mailbox {
	/*
	 * [0x0] Mailbox interrupt generator.
	 * Generates interrupt to neighbor DMA
	 */
	uint32_t interrupt;
	/* [0x4] Mailbox message data out */
	uint32_t msg_out;
	/* [0x8] Mailbox message data in */
	uint32_t msg_in;
	uint32_t rsrvd[13];
};
struct udma_gen_axi {
	/* [0x0] Configuration of the AXI masters */
	uint32_t cfg_1;
	/* [0x4] Configuration of the AXI masters */
	uint32_t cfg_2;
	/* [0x8] Configuration of the AXI masters. Endianess configuration */
	uint32_t endian_cfg;
	uint32_t rsrvd[61];
};
struct udma_gen_sram_ctrl {
	/* [0x0] Timing configuration */
	uint32_t timing;
};
struct udma_gen_tgtid {
	/* [0x0] Target-ID control */
	uint32_t cfg_tgtid_0;
	/* [0x4] TX queue 0/1 Target-ID */
	uint32_t cfg_tgtid_1;
	/* [0x8] TX queue 2/3 Target-ID */
	uint32_t cfg_tgtid_2;
	/* [0xc] RX queue 0/1 Target-ID */
	uint32_t cfg_tgtid_3;
	/* [0x10] RX queue 2/3 Target-ID */
	uint32_t cfg_tgtid_4;
};
struct udma_gen_tgtaddr {
	/* [0x0] TX queue 0/1 Target-Address */
	uint32_t cfg_tgtaddr_0;
	/* [0x4] TX queue 2/3 Target-Address */
	uint32_t cfg_tgtaddr_1;
	/* [0x8] RX queue 0/1 Target-Address */
	uint32_t cfg_tgtaddr_2;
	/* [0xc] RX queue 2/3 Target-Address */
	uint32_t cfg_tgtaddr_3;
};
struct udma_gen_vmpr {
	/* [0x0] TX VMPR control */
	uint32_t cfg_vmpr_0;
	/* [0x4] TX VMPR Address High Regsiter */
	uint32_t cfg_vmpr_1;
	/* [0x8] TX queue Target-ID values */
	uint32_t cfg_vmpr_2;
	/* [0xc] TX queue Target-ID values */
	uint32_t cfg_vmpr_3;
	/* [0x10] RX VMPR control */
	uint32_t cfg_vmpr_4;
	/* [0x14] RX VMPR Buffer2 MSB address */
	uint32_t cfg_vmpr_5;
	/* [0x18] RX queue Target-ID values */
	uint32_t cfg_vmpr_6;
	/* [0x1c] RX queue BUF1 Target-ID values */
	uint32_t cfg_vmpr_7;
	/* [0x20] RX queue BUF2 Target-ID values */
	uint32_t cfg_vmpr_8;
	/* [0x24] RX queue Direct Data Placement Target-ID values */
	uint32_t cfg_vmpr_9;
	/* [0x28] RX VMPR BUF1 Address High Regsiter */
	uint32_t cfg_vmpr_10;
	/* [0x2c] RX VMPR BUF2 Address High Regsiter */
	uint32_t cfg_vmpr_11;
	/* [0x30] RX VMPR DDP Address High Regsiter */
	uint32_t cfg_vmpr_12;
	uint32_t rsrvd[3];
};

struct udma_gen_regs {
	struct udma_iofic_regs interrupt_regs;					/* [0x0000] */
	struct udma_gen_dma_misc dma_misc;                   /* [0x2080] */
	struct udma_gen_mailbox mailbox[4];                  /* [0x2180] */
	struct udma_gen_axi axi;                             /* [0x2280] */
	struct udma_gen_sram_ctrl sram_ctrl[25];             /* [0x2380] */
	uint32_t rsrvd_1[2];
	struct udma_gen_tgtid tgtid;                           /* [0x23ec] */
	struct udma_gen_tgtaddr tgtaddr;                       /* [0x2400] */
	uint32_t rsrvd_2[252];
	struct udma_gen_vmpr vmpr[4];                        /* [0x2800] */
};


/*
* Registers Fields
*/


/**** int_cfg register ****/
/*
 * MSIX data width
 * 1 - 64 bit
 * 0 – 32 bit
 */
#define UDMA_GEN_DMA_MISC_INT_CFG_MSIX_64 (1 << 0)
/* General configuration */
#define UDMA_GEN_DMA_MISC_INT_CFG_RESERVED_3_1_MASK 0x0000000E
#define UDMA_GEN_DMA_MISC_INT_CFG_RESERVED_3_1_SHIFT 1
/* MSIx AXI QoS */
#define UDMA_GEN_DMA_MISC_INT_CFG_MSIX_AXI_QOS_MASK 0x00000070
#define UDMA_GEN_DMA_MISC_INT_CFG_MSIX_AXI_QOS_SHIFT 4

#define UDMA_GEN_DMA_MISC_INT_CFG_RESERVED_31_7_MASK 0xFFFFFF80
#define UDMA_GEN_DMA_MISC_INT_CFG_RESERVED_31_7_SHIFT 7

/**** revision register ****/
/* Design programming interface  revision ID */
#define UDMA_GEN_DMA_MISC_REVISION_PROGRAMMING_ID_MASK 0x00000FFF
#define UDMA_GEN_DMA_MISC_REVISION_PROGRAMMING_ID_SHIFT 0
/* Design minor revision ID */
#define UDMA_GEN_DMA_MISC_REVISION_MINOR_ID_MASK 0x00FFF000
#define UDMA_GEN_DMA_MISC_REVISION_MINOR_ID_SHIFT 12
/* Design major revision ID */
#define UDMA_GEN_DMA_MISC_REVISION_MAJOR_ID_MASK 0xFF000000
#define UDMA_GEN_DMA_MISC_REVISION_MAJOR_ID_SHIFT 24

/**** Interrupt register ****/
/* Generate interrupt to another DMA */
#define UDMA_GEN_MAILBOX_INTERRUPT_SET   (1 << 0)

/**** cfg_2 register ****/
/*
 * Enable arbitration promotion.
 * Increment master priority after configured number of arbitration cycles
 */
#define UDMA_GEN_AXI_CFG_2_ARB_PROMOTION_MASK 0x0000000F
#define UDMA_GEN_AXI_CFG_2_ARB_PROMOTION_SHIFT 0

/**** endian_cfg register ****/
/* Swap M2S descriptor read and completion descriptor write.  */
#define UDMA_GEN_AXI_ENDIAN_CFG_SWAP_M2S_DESC (1 << 0)
/* Swap M2S data read. */
#define UDMA_GEN_AXI_ENDIAN_CFG_SWAP_M2S_DATA (1 << 1)
/* Swap S2M descriptor read and completion descriptor write.  */
#define UDMA_GEN_AXI_ENDIAN_CFG_SWAP_S2M_DESC (1 << 2)
/* Swap S2M data write. */
#define UDMA_GEN_AXI_ENDIAN_CFG_SWAP_S2M_DATA (1 << 3)
/*
 * Swap 32 or 64 bit mode:
 * 0 - Swap groups of 4 bytes
 * 1 - Swap groups of 8 bytes
 */
#define UDMA_GEN_AXI_ENDIAN_CFG_SWAP_64B_EN (1 << 4)

/**** timing register ****/
/* Write margin */
#define UDMA_GEN_SRAM_CTRL_TIMING_RMA_MASK 0x0000000F
#define UDMA_GEN_SRAM_CTRL_TIMING_RMA_SHIFT 0
/* Write margin enable */
#define UDMA_GEN_SRAM_CTRL_TIMING_RMEA   (1 << 8)
/* Read margin */
#define UDMA_GEN_SRAM_CTRL_TIMING_RMB_MASK 0x000F0000
#define UDMA_GEN_SRAM_CTRL_TIMING_RMB_SHIFT 16
/* Read margin enable */
#define UDMA_GEN_SRAM_CTRL_TIMING_RMEB   (1 << 24)

/**** cfg_tgtid_0 register ****/
/* For M2S queues 3:0, enable usage of the Target-ID from the buffer address 63:56 */
#define UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_DESC_EN_MASK 0x0000000F
#define UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_DESC_EN_SHIFT 0
/*
 * For M2S queues 3:0, enable usage of the Target-ID from the configuration register
 * (cfg_tgtid_1/2 used for M2S queue_x)
 */
#define UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_QUEUE_EN_MASK 0x000000F0
#define UDMA_GEN_TGTID_CFG_TGTID_0_TX_Q_TGTID_QUEUE_EN_SHIFT 4
/* use Target-ID_n [7:0] from MSI-X Controller for MSI-X message  */
#define UDMA_GEN_TGTID_CFG_TGTID_0_MSIX_TGTID_SEL (1 << 8)
/* Enable write to all Target-ID_n registers in the MSI-X Controller */
#define UDMA_GEN_TGTID_CFG_TGTID_0_MSIX_TGTID_ACCESS_EN (1 << 9)
/* For S2M queues 3:0, enable usage of the Target-ID from the buffer address 63:56 */
#define UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_DESC_EN_MASK 0x000F0000
#define UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_DESC_EN_SHIFT 16
/*
 * For S2M queues 3:0, enable usage of the Target-ID from the configuration register
 * (cfg_tgtid_3/4 used for M2S queue_x)
 */
#define UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_QUEUE_EN_MASK 0x00F00000
#define UDMA_GEN_TGTID_CFG_TGTID_0_RX_Q_TGTID_QUEUE_EN_SHIFT 20

#define UDMA_GEN_TGTID_CFG_TGTID_SHIFT(qid)	(((qid) & 0x1) ? 16 : 0)
#define UDMA_GEN_TGTID_CFG_TGTID_MASK(qid)	(((qid) & 0x1) ? 0xFFFF0000 : 0x0000FFFF)

/**** cfg_tgtid_1 register ****/
/* TX queue 0 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_1_TX_Q_0_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_TGTID_CFG_TGTID_1_TX_Q_0_TGTID_SHIFT 0
/* TX queue 1 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_1_TX_Q_1_TGTID_MASK 0xFFFF0000
#define UDMA_GEN_TGTID_CFG_TGTID_1_TX_Q_1_TGTID_SHIFT 16

/**** cfg_tgtid_2 register ****/
/* TX queue 2 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_2_TX_Q_2_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_TGTID_CFG_TGTID_2_TX_Q_2_TGTID_SHIFT 0
/* TX queue 3 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_2_TX_Q_3_TGTID_MASK 0xFFFF0000
#define UDMA_GEN_TGTID_CFG_TGTID_2_TX_Q_3_TGTID_SHIFT 16

/**** cfg_tgtid_3 register ****/
/* RX queue 0 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_3_RX_Q_0_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_TGTID_CFG_TGTID_3_RX_Q_0_TGTID_SHIFT 0
/* RX queue 1 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_3_RX_Q_1_TGTID_MASK 0xFFFF0000
#define UDMA_GEN_TGTID_CFG_TGTID_3_RX_Q_1_TGTID_SHIFT 16

/**** cfg_tgtid_4 register ****/
/* RX queue 2 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_4_RX_Q_2_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_TGTID_CFG_TGTID_4_RX_Q_2_TGTID_SHIFT 0
/* RX queue 3 Target-ID value */
#define UDMA_GEN_TGTID_CFG_TGTID_4_RX_Q_3_TGTID_MASK 0xFFFF0000
#define UDMA_GEN_TGTID_CFG_TGTID_4_RX_Q_3_TGTID_SHIFT 16

#define UDMA_GEN_TGTADDR_CFG_SHIFT(qid)	(((qid) & 0x1) ? 16 : 0)
#define UDMA_GEN_TGTADDR_CFG_MASK(qid)	(((qid) & 0x1) ? 0xFFFF0000 : 0x0000FFFF)

/**** cfg_tgtaddr_0 register ****/
/* TX queue 0 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_0_TX_Q_0_TGTADDR_MASK 0x0000FFFF
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_0_TX_Q_0_TGTADDR_SHIFT 0
/* TX queue 1 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_0_TX_Q_1_TGTADDR_MASK 0xFFFF0000
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_0_TX_Q_1_TGTADDR_SHIFT 16

/**** cfg_tgtaddr_1 register ****/
/* TX queue 2 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_1_TX_Q_2_TGTADDR_MASK 0x0000FFFF
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_1_TX_Q_2_TGTADDR_SHIFT 0
/* TX queue 3 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_1_TX_Q_3_TGTADDR_MASK 0xFFFF0000
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_1_TX_Q_3_TGTADDR_SHIFT 16

/**** cfg_tgtaddr_2 register ****/
/* RX queue 0 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_2_RX_Q_0_TGTADDR_MASK 0x0000FFFF
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_2_RX_Q_0_TGTADDR_SHIFT 0
/* RX queue 1 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_2_RX_Q_1_TGTADDR_MASK 0xFFFF0000
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_2_RX_Q_1_TGTADDR_SHIFT 16

/**** cfg_tgtaddr_3 register ****/
/* RX queue 2 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_3_RX_Q_2_TGTADDR_MASK 0x0000FFFF
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_3_RX_Q_2_TGTADDR_SHIFT 0
/* RX queue 3 Target-Address value */
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_3_RX_Q_3_TGTADDR_MASK 0xFFFF0000
#define UDMA_GEN_TGTADDR_CFG_TGTADDR_3_RX_Q_3_TGTADDR_SHIFT 16

/**** cfg_vmpr_0 register ****/
/* TX High Address Select Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_0_TX_Q_HISEL_MASK 0x0000003F
#define UDMA_GEN_VMPR_CFG_VMPR_0_TX_Q_HISEL_SHIFT 0
/* TX Data Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_0_TX_Q_DATA_TGTID_EN (1 << 7)
/* TX Prefetch Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_0_TX_Q_PREF_TGTID_EN (1 << 28)
/* TX Completions Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_0_TX_Q_CMPL_TGTID_EN (1 << 29)

/**** cfg_vmpr_2 register ****/
/* TX queue Prefetch Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_2_TX_Q_PREF_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_VMPR_CFG_VMPR_2_TX_Q_PREF_TGTID_SHIFT 0
/* TX queue Completion Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_2_TX_Q_CMPL_TGTID_MASK 0xFFFF0000
#define UDMA_GEN_VMPR_CFG_VMPR_2_TX_Q_CMPL_TGTID_SHIFT 16

/**** cfg_vmpr_3 register ****/
/* TX queue Data Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_3_TX_Q_DATA_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_VMPR_CFG_VMPR_3_TX_Q_DATA_TGTID_SHIFT 0
/* TX queue Data Target-ID select */
#define UDMA_GEN_VMPR_CFG_VMPR_3_TX_Q_DATA_TGTID_SEL_MASK 0xFFFF0000
#define UDMA_GEN_VMPR_CFG_VMPR_3_TX_Q_DATA_TGTID_SEL_SHIFT 16

/**** cfg_vmpr_4 register ****/
/* RX Data Buffer1 - High Address Select Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF1_HISEL_MASK 0x0000003F
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF1_HISEL_SHIFT 0
/* RX Data Buffer1 Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF1_TGTID_EN (1 << 7)
/* RX Data Buffer2 - High Address Select Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF2_HISEL_MASK 0x00003F00
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF2_HISEL_SHIFT 8
/* RX Data Buffer2 Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF2_TGTID_EN (1 << 15)
/* RX Direct Data Placement - High Address Select Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_DDP_HISEL_MASK 0x003F0000
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_DDP_HISEL_SHIFT 16
/* RX Direct Data Placement Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_DDP_TGTID_EN (1 << 23)
/* RX Buffer 2 MSB address word selects per bytes, per queue */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF2_MSB_ADDR_SEL_MASK 0x0F000000
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_BUF2_MSB_ADDR_SEL_SHIFT 24
/* RX Prefetch Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_PREF_TGTID_EN (1 << 28)
/* RX Completions Target-ID Enable Per Q */
#define UDMA_GEN_VMPR_CFG_VMPR_4_RX_Q_CMPL_TGTID_EN (1 << 29)

/**** cfg_vmpr_6 register ****/
/* RX queue Prefetch Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_6_RX_Q_PREF_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_VMPR_CFG_VMPR_6_RX_Q_PREF_TGTID_SHIFT 0
/* RX queue Completion Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_6_RX_Q_CMPL_TGTID_MASK 0xFFFF0000
#define UDMA_GEN_VMPR_CFG_VMPR_6_RX_Q_CMPL_TGTID_SHIFT 16

/**** cfg_vmpr_7 register ****/
/* RX queue Data Buffer 1 Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_7_RX_Q_BUF1_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_VMPR_CFG_VMPR_7_RX_Q_BUF1_TGTID_SHIFT 0
/* RX queue Data Buffer 1 Target-ID select */
#define UDMA_GEN_VMPR_CFG_VMPR_7_RX_Q_BUF1_TGTID_SEL_MASK 0xFFFF0000
#define UDMA_GEN_VMPR_CFG_VMPR_7_RX_Q_BUF1_TGTID_SEL_SHIFT 16

/**** cfg_vmpr_8 register ****/
/* RX queue Data Buffer 2 Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_8_RX_Q_BUF2_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_VMPR_CFG_VMPR_8_RX_Q_BUF2_TGTID_SHIFT 0
/* RX queue Data Buffer 2 Target-ID select */
#define UDMA_GEN_VMPR_CFG_VMPR_8_RX_Q_BUF2_TGTID_SEL_MASK 0xFFFF0000
#define UDMA_GEN_VMPR_CFG_VMPR_8_RX_Q_BUF2_TGTID_SEL_SHIFT 16

/**** cfg_vmpr_9 register ****/
/* RX queue DDP Target-ID */
#define UDMA_GEN_VMPR_CFG_VMPR_9_RX_Q_DDP_TGTID_MASK 0x0000FFFF
#define UDMA_GEN_VMPR_CFG_VMPR_9_RX_Q_DDP_TGTID_SHIFT 0
/* RX queue DDP Target-ID select */
#define UDMA_GEN_VMPR_CFG_VMPR_9_RX_Q_DDP_TGTID_SEL_MASK 0xFFFF0000
#define UDMA_GEN_VMPR_CFG_VMPR_9_RX_Q_DDP_TGTID_SEL_SHIFT 16

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_UDMA_GEN_REG_H */
