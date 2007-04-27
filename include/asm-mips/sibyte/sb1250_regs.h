/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  Register Definitions                     File: sb1250_regs.h
    *
    *  This module contains the addresses of the on-chip peripherals
    *  on the SB1250.
    *
    *  SB1250 specification level:  01/02/2002
    *
    *********************************************************************
    *
    *  Copyright 2000,2001,2002,2003
    *  Broadcom Corporation. All rights reserved.
    *
    *  This program is free software; you can redistribute it and/or
    *  modify it under the terms of the GNU General Public License as
    *  published by the Free Software Foundation; either version 2 of
    *  the License, or (at your option) any later version.
    *
    *  This program is distributed in the hope that it will be useful,
    *  but WITHOUT ANY WARRANTY; without even the implied warranty of
    *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    *  GNU General Public License for more details.
    *
    *  You should have received a copy of the GNU General Public License
    *  along with this program; if not, write to the Free Software
    *  Foundation, Inc., 59 Temple Place, Suite 330, Boston,
    *  MA 02111-1307 USA
    ********************************************************************* */


#ifndef _SB1250_REGS_H
#define _SB1250_REGS_H

#include "sb1250_defs.h"


/*  *********************************************************************
    *  Some general notes:
    *
    *  For the most part, when there is more than one peripheral
    *  of the same type on the SOC, the constants below will be
    *  offsets from the base of each peripheral.  For example,
    *  the MAC registers are described as offsets from the first
    *  MAC register, and there will be a MAC_REGISTER() macro
    *  to calculate the base address of a given MAC.
    *
    *  The information in this file is based on the SB1250 SOC
    *  manual version 0.2, July 2000.
    ********************************************************************* */


/*  *********************************************************************
    * Memory Controller Registers
    ********************************************************************* */

/*
 * XXX: can't remove MC base 0 if 112x, since it's used by other macros,
 * since there is one reg there (but it could get its addr/offset constant).
 */

#if SIBYTE_HDR_FEATURE_1250_112x		/* This MC only on 1250 & 112x */
#define A_MC_BASE_0                 0x0010051000
#define A_MC_BASE_1                 0x0010052000
#define MC_REGISTER_SPACING         0x1000

#define A_MC_BASE(ctlid)            ((ctlid)*MC_REGISTER_SPACING+A_MC_BASE_0)
#define A_MC_REGISTER(ctlid,reg)    (A_MC_BASE(ctlid)+(reg))

#define R_MC_CONFIG                 0x0000000100
#define R_MC_DRAMCMD                0x0000000120
#define R_MC_DRAMMODE               0x0000000140
#define R_MC_TIMING1                0x0000000160
#define R_MC_TIMING2                0x0000000180
#define R_MC_CS_START               0x00000001A0
#define R_MC_CS_END                 0x00000001C0
#define R_MC_CS_INTERLEAVE          0x00000001E0
#define S_MC_CS_STARTEND            16

#define R_MC_CSX_BASE               0x0000000200
#define R_MC_CSX_ROW                0x0000000000	/* relative to CSX_BASE, above */
#define R_MC_CSX_COL                0x0000000020	/* relative to CSX_BASE, above */
#define R_MC_CSX_BA                 0x0000000040	/* relative to CSX_BASE, above */
#define MC_CSX_SPACING              0x0000000060	/* relative to CSX_BASE, above */

#define R_MC_CS0_ROW                0x0000000200
#define R_MC_CS0_COL                0x0000000220
#define R_MC_CS0_BA                 0x0000000240
#define R_MC_CS1_ROW                0x0000000260
#define R_MC_CS1_COL                0x0000000280
#define R_MC_CS1_BA                 0x00000002A0
#define R_MC_CS2_ROW                0x00000002C0
#define R_MC_CS2_COL                0x00000002E0
#define R_MC_CS2_BA                 0x0000000300
#define R_MC_CS3_ROW                0x0000000320
#define R_MC_CS3_COL                0x0000000340
#define R_MC_CS3_BA                 0x0000000360
#define R_MC_CS_ATTR                0x0000000380
#define R_MC_TEST_DATA              0x0000000400
#define R_MC_TEST_ECC               0x0000000420
#define R_MC_MCLK_CFG               0x0000000500

#endif	/* 1250 & 112x */

/*  *********************************************************************
    * L2 Cache Control Registers
    ********************************************************************* */

#if SIBYTE_HDR_FEATURE_1250_112x	/* This L2C only on 1250/112x */

#define A_L2_READ_TAG               0x0010040018
#define A_L2_ECC_TAG                0x0010040038
#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define A_L2_READ_MISC              0x0010040058
#endif /* 1250 PASS3 || 112x PASS1 */
#define A_L2_WAY_DISABLE            0x0010041000
#define A_L2_MAKEDISABLE(x)         (A_L2_WAY_DISABLE | (((~(x))&0x0F) << 8))
#define A_L2_MGMT_TAG_BASE          0x00D0000000

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define A_L2_CACHE_DISABLE	   0x0010042000
#define A_L2_MAKECACHEDISABLE(x)   (A_L2_CACHE_DISABLE | (((x)&0x0F) << 8))
#define A_L2_MISC_CONFIG	   0x0010043000
#endif /* 1250 PASS2 || 112x PASS1 */

/* Backward-compatibility definitions.  */
/* XXX: discourage people from using these constants.  */
#define A_L2_READ_ADDRESS           A_L2_READ_TAG
#define A_L2_EEC_ADDRESS            A_L2_ECC_TAG

#endif


/*  *********************************************************************
    * PCI Interface Registers
    ********************************************************************* */

#if SIBYTE_HDR_FEATURE_1250_112x	/* This PCI/HT only on 1250/112x */
#define A_PCI_TYPE00_HEADER         0x00DE000000
#define A_PCI_TYPE01_HEADER         0x00DE000800
#endif


/*  *********************************************************************
    * Ethernet DMA and MACs
    ********************************************************************* */

#define A_MAC_BASE_0                0x0010064000
#define A_MAC_BASE_1                0x0010065000
#if SIBYTE_HDR_FEATURE_CHIP(1250)
#define A_MAC_BASE_2                0x0010066000
#endif /* 1250 */

#define MAC_SPACING                 0x1000
#define MAC_DMA_TXRX_SPACING        0x0400
#define MAC_DMA_CHANNEL_SPACING     0x0100
#define DMA_RX                      0
#define DMA_TX                      1
#define MAC_NUM_DMACHAN		    2		    /* channels per direction */

/* XXX: not correct; depends on SOC type.  */
#define MAC_NUM_PORTS               3

#define A_MAC_CHANNEL_BASE(macnum)                  \
            (A_MAC_BASE_0 +                         \
             MAC_SPACING*(macnum))

#define A_MAC_REGISTER(macnum,reg)                  \
            (A_MAC_BASE_0 +                         \
             MAC_SPACING*(macnum) + (reg))


#define R_MAC_DMA_CHANNELS		0x800 /* Relative to A_MAC_CHANNEL_BASE */

#define A_MAC_DMA_CHANNEL_BASE(macnum,txrx,chan)    \
             ((A_MAC_CHANNEL_BASE(macnum)) +        \
             R_MAC_DMA_CHANNELS +                   \
             (MAC_DMA_TXRX_SPACING*(txrx)) +        \
             (MAC_DMA_CHANNEL_SPACING*(chan)))

#define R_MAC_DMA_CHANNEL_BASE(txrx,chan)    \
             (R_MAC_DMA_CHANNELS +                   \
             (MAC_DMA_TXRX_SPACING*(txrx)) +        \
             (MAC_DMA_CHANNEL_SPACING*(chan)))

#define A_MAC_DMA_REGISTER(macnum,txrx,chan,reg)           \
            (A_MAC_DMA_CHANNEL_BASE(macnum,txrx,chan) +    \
            (reg))

#define R_MAC_DMA_REGISTER(txrx,chan,reg)           \
            (R_MAC_DMA_CHANNEL_BASE(txrx,chan) +    \
            (reg))

/*
 * DMA channel registers, relative to A_MAC_DMA_CHANNEL_BASE
 */

#define R_MAC_DMA_CONFIG0               0x00000000
#define R_MAC_DMA_CONFIG1               0x00000008
#define R_MAC_DMA_DSCR_BASE             0x00000010
#define R_MAC_DMA_DSCR_CNT              0x00000018
#define R_MAC_DMA_CUR_DSCRA             0x00000020
#define R_MAC_DMA_CUR_DSCRB             0x00000028
#define R_MAC_DMA_CUR_DSCRADDR          0x00000030
#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define R_MAC_DMA_OODPKTLOST_RX         0x00000038	/* rx only */
#endif /* 1250 PASS3 || 112x PASS1 */

/*
 * RMON Counters
 */

#define R_MAC_RMON_TX_BYTES             0x00000000
#define R_MAC_RMON_COLLISIONS           0x00000008
#define R_MAC_RMON_LATE_COL             0x00000010
#define R_MAC_RMON_EX_COL               0x00000018
#define R_MAC_RMON_FCS_ERROR            0x00000020
#define R_MAC_RMON_TX_ABORT             0x00000028
/* Counter #6 (0x30) now reserved */
#define R_MAC_RMON_TX_BAD               0x00000038
#define R_MAC_RMON_TX_GOOD              0x00000040
#define R_MAC_RMON_TX_RUNT              0x00000048
#define R_MAC_RMON_TX_OVERSIZE          0x00000050
#define R_MAC_RMON_RX_BYTES             0x00000080
#define R_MAC_RMON_RX_MCAST             0x00000088
#define R_MAC_RMON_RX_BCAST             0x00000090
#define R_MAC_RMON_RX_BAD               0x00000098
#define R_MAC_RMON_RX_GOOD              0x000000A0
#define R_MAC_RMON_RX_RUNT              0x000000A8
#define R_MAC_RMON_RX_OVERSIZE          0x000000B0
#define R_MAC_RMON_RX_FCS_ERROR         0x000000B8
#define R_MAC_RMON_RX_LENGTH_ERROR      0x000000C0
#define R_MAC_RMON_RX_CODE_ERROR        0x000000C8
#define R_MAC_RMON_RX_ALIGN_ERROR       0x000000D0

/* Updated to spec 0.2 */
#define R_MAC_CFG                       0x00000100
#define R_MAC_THRSH_CFG                 0x00000108
#define R_MAC_VLANTAG                   0x00000110
#define R_MAC_FRAMECFG                  0x00000118
#define R_MAC_EOPCNT                    0x00000120
#define R_MAC_FIFO_PTRS                 0x00000128
#define R_MAC_ADFILTER_CFG              0x00000200
#define R_MAC_ETHERNET_ADDR             0x00000208
#define R_MAC_PKT_TYPE                  0x00000210
#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1) || SIBYTE_HDR_FEATURE_CHIP(1480)
#define R_MAC_ADMASK0			0x00000218
#define R_MAC_ADMASK1			0x00000220
#endif /* 1250 PASS3 || 112x PASS1 || 1480 */
#define R_MAC_HASH_BASE                 0x00000240
#define R_MAC_ADDR_BASE                 0x00000280
#define R_MAC_CHLO0_BASE                0x00000300
#define R_MAC_CHUP0_BASE                0x00000320
#define R_MAC_ENABLE                    0x00000400
#define R_MAC_STATUS                    0x00000408
#define R_MAC_INT_MASK                  0x00000410
#define R_MAC_TXD_CTL                   0x00000420
#define R_MAC_MDIO                      0x00000428
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1) || SIBYTE_HDR_FEATURE_CHIP(1480)
#define R_MAC_STATUS1		        0x00000430
#endif /* 1250 PASS2 || 112x PASS1 || 1480 */
#define R_MAC_DEBUG_STATUS              0x00000448

#define MAC_HASH_COUNT			8
#define MAC_ADDR_COUNT			8
#define MAC_CHMAP_COUNT			4


/*  *********************************************************************
    * DUART Registers
    ********************************************************************* */


#if SIBYTE_HDR_FEATURE_1250_112x		/* This MC only on 1250 & 112x */
#define R_DUART_NUM_PORTS           2

#define A_DUART                     0x0010060000

#define DUART_CHANREG_SPACING       0x100
#define A_DUART_CHANREG(chan,reg)   (A_DUART + DUART_CHANREG_SPACING*(chan) + (reg))
#define R_DUART_CHANREG(chan,reg)   (DUART_CHANREG_SPACING*(chan) + (reg))
#endif	/* 1250 & 112x */

#define R_DUART_MODE_REG_1	    0x100
#define R_DUART_MODE_REG_2	    0x110
#define R_DUART_STATUS              0x120
#define R_DUART_CLK_SEL             0x130
#define R_DUART_CMD                 0x150
#define R_DUART_RX_HOLD             0x160
#define R_DUART_TX_HOLD             0x170

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1) || SIBYTE_HDR_FEATURE_CHIP(1480)
#define R_DUART_FULL_CTL	    0x140
#define R_DUART_OPCR_X		    0x180
#define R_DUART_AUXCTL_X	    0x190
#endif /* 1250 PASS2 || 112x PASS1 || 1480*/


/*
 * The IMR and ISR can't be addressed with A_DUART_CHANREG,
 * so use this macro instead.
 */

#define R_DUART_AUX_CTRL            0x310
#define R_DUART_ISR_A               0x320
#define R_DUART_IMR_A               0x330
#define R_DUART_ISR_B               0x340
#define R_DUART_IMR_B               0x350
#define R_DUART_OUT_PORT            0x360
#define R_DUART_OPCR                0x370
#define R_DUART_IN_PORT             0x380

#define R_DUART_SET_OPR		    0x3B0
#define R_DUART_CLEAR_OPR	    0x3C0

#define DUART_IMRISR_SPACING        0x20

#if SIBYTE_HDR_FEATURE_1250_112x		/* This MC only on 1250 & 112x */
#define R_DUART_IMRREG(chan)	    (R_DUART_IMR_A + (chan)*DUART_IMRISR_SPACING)
#define R_DUART_ISRREG(chan)	    (R_DUART_ISR_A + (chan)*DUART_IMRISR_SPACING)

#define A_DUART_IMRREG(chan)	    (A_DUART + R_DUART_IMRREG(chan))
#define A_DUART_ISRREG(chan)	    (A_DUART + R_DUART_ISRREG(chan))
#endif	/* 1250 & 112x */




/*
 * These constants are the absolute addresses.
 */

#define A_DUART_MODE_REG_1_A        0x0010060100
#define A_DUART_MODE_REG_2_A        0x0010060110
#define A_DUART_STATUS_A            0x0010060120
#define A_DUART_CLK_SEL_A           0x0010060130
#define A_DUART_CMD_A               0x0010060150
#define A_DUART_RX_HOLD_A           0x0010060160
#define A_DUART_TX_HOLD_A           0x0010060170

#define A_DUART_MODE_REG_1_B        0x0010060200
#define A_DUART_MODE_REG_2_B        0x0010060210
#define A_DUART_STATUS_B            0x0010060220
#define A_DUART_CLK_SEL_B           0x0010060230
#define A_DUART_CMD_B               0x0010060250
#define A_DUART_RX_HOLD_B           0x0010060260
#define A_DUART_TX_HOLD_B           0x0010060270

#define A_DUART_INPORT_CHNG         0x0010060300
#define A_DUART_AUX_CTRL            0x0010060310
#define A_DUART_ISR_A               0x0010060320
#define A_DUART_IMR_A               0x0010060330
#define A_DUART_ISR_B               0x0010060340
#define A_DUART_IMR_B               0x0010060350
#define A_DUART_OUT_PORT            0x0010060360
#define A_DUART_OPCR                0x0010060370
#define A_DUART_IN_PORT             0x0010060380
#define A_DUART_ISR                 0x0010060390
#define A_DUART_IMR                 0x00100603A0
#define A_DUART_SET_OPR             0x00100603B0
#define A_DUART_CLEAR_OPR           0x00100603C0
#define A_DUART_INPORT_CHNG_A       0x00100603D0
#define A_DUART_INPORT_CHNG_B       0x00100603E0

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define A_DUART_FULL_CTL_A	    0x0010060140
#define A_DUART_FULL_CTL_B	    0x0010060240

#define A_DUART_OPCR_A	  	    0x0010060180
#define A_DUART_OPCR_B	  	    0x0010060280

#define A_DUART_INPORT_CHNG_DEBUG   0x00100603F0
#endif /* 1250 PASS2 || 112x PASS1 */


/*  *********************************************************************
    * Synchronous Serial Registers
    ********************************************************************* */


#if SIBYTE_HDR_FEATURE_1250_112x	/* sync serial only on 1250/112x */

#define A_SER_BASE_0                0x0010060400
#define A_SER_BASE_1                0x0010060800
#define SER_SPACING                 0x400

#define SER_DMA_TXRX_SPACING        0x80

#define SER_NUM_PORTS               2

#define A_SER_CHANNEL_BASE(sernum)                  \
            (A_SER_BASE_0 +                         \
             SER_SPACING*(sernum))

#define A_SER_REGISTER(sernum,reg)                  \
            (A_SER_BASE_0 +                         \
             SER_SPACING*(sernum) + (reg))


#define R_SER_DMA_CHANNELS		0   /* Relative to A_SER_BASE_x */

#define A_SER_DMA_CHANNEL_BASE(sernum,txrx)    \
             ((A_SER_CHANNEL_BASE(sernum)) +        \
             R_SER_DMA_CHANNELS +                   \
             (SER_DMA_TXRX_SPACING*(txrx)))

#define A_SER_DMA_REGISTER(sernum,txrx,reg)           \
            (A_SER_DMA_CHANNEL_BASE(sernum,txrx) +    \
            (reg))


/*
 * DMA channel registers, relative to A_SER_DMA_CHANNEL_BASE
 */

#define R_SER_DMA_CONFIG0           0x00000000
#define R_SER_DMA_CONFIG1           0x00000008
#define R_SER_DMA_DSCR_BASE         0x00000010
#define R_SER_DMA_DSCR_CNT          0x00000018
#define R_SER_DMA_CUR_DSCRA         0x00000020
#define R_SER_DMA_CUR_DSCRB         0x00000028
#define R_SER_DMA_CUR_DSCRADDR      0x00000030

#define R_SER_DMA_CONFIG0_RX        0x00000000
#define R_SER_DMA_CONFIG1_RX        0x00000008
#define R_SER_DMA_DSCR_BASE_RX      0x00000010
#define R_SER_DMA_DSCR_COUNT_RX     0x00000018
#define R_SER_DMA_CUR_DSCR_A_RX     0x00000020
#define R_SER_DMA_CUR_DSCR_B_RX     0x00000028
#define R_SER_DMA_CUR_DSCR_ADDR_RX  0x00000030

#define R_SER_DMA_CONFIG0_TX        0x00000080
#define R_SER_DMA_CONFIG1_TX        0x00000088
#define R_SER_DMA_DSCR_BASE_TX      0x00000090
#define R_SER_DMA_DSCR_COUNT_TX     0x00000098
#define R_SER_DMA_CUR_DSCR_A_TX     0x000000A0
#define R_SER_DMA_CUR_DSCR_B_TX     0x000000A8
#define R_SER_DMA_CUR_DSCR_ADDR_TX  0x000000B0

#define R_SER_MODE                  0x00000100
#define R_SER_MINFRM_SZ             0x00000108
#define R_SER_MAXFRM_SZ             0x00000110
#define R_SER_ADDR                  0x00000118
#define R_SER_USR0_ADDR             0x00000120
#define R_SER_USR1_ADDR             0x00000128
#define R_SER_USR2_ADDR             0x00000130
#define R_SER_USR3_ADDR             0x00000138
#define R_SER_CMD                   0x00000140
#define R_SER_TX_RD_THRSH           0x00000160
#define R_SER_TX_WR_THRSH           0x00000168
#define R_SER_RX_RD_THRSH           0x00000170
#define R_SER_LINE_MODE		    0x00000178
#define R_SER_DMA_ENABLE            0x00000180
#define R_SER_INT_MASK              0x00000190
#define R_SER_STATUS                0x00000188
#define R_SER_STATUS_DEBUG          0x000001A8
#define R_SER_RX_TABLE_BASE         0x00000200
#define SER_RX_TABLE_COUNT          16
#define R_SER_TX_TABLE_BASE         0x00000300
#define SER_TX_TABLE_COUNT          16

/* RMON Counters */
#define R_SER_RMON_TX_BYTE_LO       0x000001C0
#define R_SER_RMON_TX_BYTE_HI       0x000001C8
#define R_SER_RMON_RX_BYTE_LO       0x000001D0
#define R_SER_RMON_RX_BYTE_HI       0x000001D8
#define R_SER_RMON_TX_UNDERRUN      0x000001E0
#define R_SER_RMON_RX_OVERFLOW      0x000001E8
#define R_SER_RMON_RX_ERRORS        0x000001F0
#define R_SER_RMON_RX_BADADDR       0x000001F8

#endif	/* 1250/112x */

/*  *********************************************************************
    * Generic Bus Registers
    ********************************************************************* */

#define IO_EXT_CFG_COUNT            8

#define A_IO_EXT_BASE		    0x0010061000
#define A_IO_EXT_REG(r)		    (A_IO_EXT_BASE + (r))

#define A_IO_EXT_CFG_BASE           0x0010061000
#define A_IO_EXT_MULT_SIZE_BASE     0x0010061100
#define A_IO_EXT_START_ADDR_BASE    0x0010061200
#define A_IO_EXT_TIME_CFG0_BASE     0x0010061600
#define A_IO_EXT_TIME_CFG1_BASE     0x0010061700

#define IO_EXT_REGISTER_SPACING	    8
#define A_IO_EXT_CS_BASE(cs)	    (A_IO_EXT_CFG_BASE+IO_EXT_REGISTER_SPACING*(cs))
#define R_IO_EXT_REG(reg,cs)	    ((cs)*IO_EXT_REGISTER_SPACING + (reg))

#define R_IO_EXT_CFG		    0x0000
#define R_IO_EXT_MULT_SIZE          0x0100
#define R_IO_EXT_START_ADDR	    0x0200
#define R_IO_EXT_TIME_CFG0          0x0600
#define R_IO_EXT_TIME_CFG1          0x0700


#define A_IO_INTERRUPT_STATUS       0x0010061A00
#define A_IO_INTERRUPT_DATA0        0x0010061A10
#define A_IO_INTERRUPT_DATA1        0x0010061A18
#define A_IO_INTERRUPT_DATA2        0x0010061A20
#define A_IO_INTERRUPT_DATA3        0x0010061A28
#define A_IO_INTERRUPT_ADDR0        0x0010061A30
#define A_IO_INTERRUPT_ADDR1        0x0010061A40
#define A_IO_INTERRUPT_PARITY       0x0010061A50
#define A_IO_PCMCIA_CFG             0x0010061A60
#define A_IO_PCMCIA_STATUS          0x0010061A70
#define A_IO_DRIVE_0		    0x0010061300
#define A_IO_DRIVE_1		    0x0010061308
#define A_IO_DRIVE_2		    0x0010061310
#define A_IO_DRIVE_3		    0x0010061318
#define A_IO_DRIVE_BASE		    A_IO_DRIVE_0
#define IO_DRIVE_REGISTER_SPACING   8
#define R_IO_DRIVE(x)		    ((x)*IO_DRIVE_REGISTER_SPACING)
#define A_IO_DRIVE(x)		    (A_IO_DRIVE_BASE + R_IO_DRIVE(x))

#define R_IO_INTERRUPT_STATUS       0x0A00
#define R_IO_INTERRUPT_DATA0        0x0A10
#define R_IO_INTERRUPT_DATA1        0x0A18
#define R_IO_INTERRUPT_DATA2        0x0A20
#define R_IO_INTERRUPT_DATA3        0x0A28
#define R_IO_INTERRUPT_ADDR0        0x0A30
#define R_IO_INTERRUPT_ADDR1        0x0A40
#define R_IO_INTERRUPT_PARITY       0x0A50
#define R_IO_PCMCIA_CFG             0x0A60
#define R_IO_PCMCIA_STATUS          0x0A70

/*  *********************************************************************
    * GPIO Registers
    ********************************************************************* */

#define A_GPIO_CLR_EDGE             0x0010061A80
#define A_GPIO_INT_TYPE             0x0010061A88
#define A_GPIO_INPUT_INVERT         0x0010061A90
#define A_GPIO_GLITCH               0x0010061A98
#define A_GPIO_READ                 0x0010061AA0
#define A_GPIO_DIRECTION            0x0010061AA8
#define A_GPIO_PIN_CLR              0x0010061AB0
#define A_GPIO_PIN_SET              0x0010061AB8

#define A_GPIO_BASE		    0x0010061A80

#define R_GPIO_CLR_EDGE             0x00
#define R_GPIO_INT_TYPE             0x08
#define R_GPIO_INPUT_INVERT         0x10
#define R_GPIO_GLITCH               0x18
#define R_GPIO_READ                 0x20
#define R_GPIO_DIRECTION            0x28
#define R_GPIO_PIN_CLR              0x30
#define R_GPIO_PIN_SET              0x38

/*  *********************************************************************
    * SMBus Registers
    ********************************************************************* */

#define A_SMB_XTRA_0                0x0010060000
#define A_SMB_XTRA_1                0x0010060008
#define A_SMB_FREQ_0                0x0010060010
#define A_SMB_FREQ_1                0x0010060018
#define A_SMB_STATUS_0              0x0010060020
#define A_SMB_STATUS_1              0x0010060028
#define A_SMB_CMD_0                 0x0010060030
#define A_SMB_CMD_1                 0x0010060038
#define A_SMB_START_0               0x0010060040
#define A_SMB_START_1               0x0010060048
#define A_SMB_DATA_0                0x0010060050
#define A_SMB_DATA_1                0x0010060058
#define A_SMB_CONTROL_0             0x0010060060
#define A_SMB_CONTROL_1             0x0010060068
#define A_SMB_PEC_0                 0x0010060070
#define A_SMB_PEC_1                 0x0010060078

#define A_SMB_0                     0x0010060000
#define A_SMB_1                     0x0010060008
#define SMB_REGISTER_SPACING        0x8
#define A_SMB_BASE(idx)             (A_SMB_0+(idx)*SMB_REGISTER_SPACING)
#define A_SMB_REGISTER(idx,reg)     (A_SMB_BASE(idx)+(reg))

#define R_SMB_XTRA                  0x0000000000
#define R_SMB_FREQ                  0x0000000010
#define R_SMB_STATUS                0x0000000020
#define R_SMB_CMD                   0x0000000030
#define R_SMB_START                 0x0000000040
#define R_SMB_DATA                  0x0000000050
#define R_SMB_CONTROL               0x0000000060
#define R_SMB_PEC                   0x0000000070

/*  *********************************************************************
    * Timer Registers
    ********************************************************************* */

/*
 * Watchdog timers
 */

#define A_SCD_WDOG_0		    0x0010020050
#define A_SCD_WDOG_1                0x0010020150
#define SCD_WDOG_SPACING            0x100
#define SCD_NUM_WDOGS		    2
#define A_SCD_WDOG_BASE(w)          (A_SCD_WDOG_0+SCD_WDOG_SPACING*(w))
#define A_SCD_WDOG_REGISTER(w,r)    (A_SCD_WDOG_BASE(w) + (r))

#define R_SCD_WDOG_INIT		    0x0000000000
#define R_SCD_WDOG_CNT		    0x0000000008
#define R_SCD_WDOG_CFG		    0x0000000010

#define A_SCD_WDOG_INIT_0           0x0010020050
#define A_SCD_WDOG_CNT_0            0x0010020058
#define A_SCD_WDOG_CFG_0            0x0010020060

#define A_SCD_WDOG_INIT_1           0x0010020150
#define A_SCD_WDOG_CNT_1            0x0010020158
#define A_SCD_WDOG_CFG_1            0x0010020160

/*
 * Generic timers
 */

#define A_SCD_TIMER_0		    0x0010020070
#define A_SCD_TIMER_1               0x0010020078
#define A_SCD_TIMER_2		    0x0010020170
#define A_SCD_TIMER_3               0x0010020178
#define SCD_NUM_TIMERS		    4
#define A_SCD_TIMER_BASE(w)         (A_SCD_TIMER_0+0x08*((w)&1)+0x100*(((w)&2)>>1))
#define A_SCD_TIMER_REGISTER(w,r)   (A_SCD_TIMER_BASE(w) + (r))

#define R_SCD_TIMER_INIT	    0x0000000000
#define R_SCD_TIMER_CNT		    0x0000000010
#define R_SCD_TIMER_CFG		    0x0000000020

#define A_SCD_TIMER_INIT_0          0x0010020070
#define A_SCD_TIMER_CNT_0           0x0010020080
#define A_SCD_TIMER_CFG_0           0x0010020090

#define A_SCD_TIMER_INIT_1          0x0010020078
#define A_SCD_TIMER_CNT_1           0x0010020088
#define A_SCD_TIMER_CFG_1           0x0010020098

#define A_SCD_TIMER_INIT_2          0x0010020170
#define A_SCD_TIMER_CNT_2           0x0010020180
#define A_SCD_TIMER_CFG_2           0x0010020190

#define A_SCD_TIMER_INIT_3          0x0010020178
#define A_SCD_TIMER_CNT_3           0x0010020188
#define A_SCD_TIMER_CFG_3           0x0010020198

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define A_SCD_SCRATCH		   0x0010020C10
#endif /* 1250 PASS2 || 112x PASS1 */

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1) || SIBYTE_HDR_FEATURE_CHIP(1480)
#define A_SCD_ZBBUS_CYCLE_COUNT	   0x0010030000
#define A_SCD_ZBBUS_CYCLE_CP0	   0x0010020C00
#define A_SCD_ZBBUS_CYCLE_CP1	   0x0010020C08
#endif

/*  *********************************************************************
    * System Control Registers
    ********************************************************************* */

#define A_SCD_SYSTEM_REVISION       0x0010020000
#define A_SCD_SYSTEM_CFG            0x0010020008
#define A_SCD_SYSTEM_MANUF          0x0010038000

/*  *********************************************************************
    * System Address Trap Registers
    ********************************************************************* */

#define A_ADDR_TRAP_INDEX           0x00100200B0
#define A_ADDR_TRAP_REG             0x00100200B8
#define A_ADDR_TRAP_UP_0            0x0010020400
#define A_ADDR_TRAP_UP_1            0x0010020408
#define A_ADDR_TRAP_UP_2            0x0010020410
#define A_ADDR_TRAP_UP_3            0x0010020418
#define A_ADDR_TRAP_DOWN_0          0x0010020420
#define A_ADDR_TRAP_DOWN_1          0x0010020428
#define A_ADDR_TRAP_DOWN_2          0x0010020430
#define A_ADDR_TRAP_DOWN_3          0x0010020438
#define A_ADDR_TRAP_CFG_0           0x0010020440
#define A_ADDR_TRAP_CFG_1           0x0010020448
#define A_ADDR_TRAP_CFG_2           0x0010020450
#define A_ADDR_TRAP_CFG_3           0x0010020458
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1) || SIBYTE_HDR_FEATURE_CHIP(1480)
#define A_ADDR_TRAP_REG_DEBUG	    0x0010020460
#endif /* 1250 PASS2 || 112x PASS1 || 1480 */

#define ADDR_TRAP_SPACING 8
#define NUM_ADDR_TRAP 4
#define A_ADDR_TRAP_UP(n) (A_ADDR_TRAP_UP_0 + ((n) * ADDR_TRAP_SPACING))
#define A_ADDR_TRAP_DOWN(n) (A_ADDR_TRAP_DOWN_0 + ((n) * ADDR_TRAP_SPACING))
#define A_ADDR_TRAP_CFG(n) (A_ADDR_TRAP_CFG_0 + ((n) * ADDR_TRAP_SPACING))


/*  *********************************************************************
    * System Interrupt Mapper Registers
    ********************************************************************* */

#define A_IMR_CPU0_BASE                 0x0010020000
#define A_IMR_CPU1_BASE                 0x0010022000
#define IMR_REGISTER_SPACING            0x2000
#define IMR_REGISTER_SPACING_SHIFT      13

#define A_IMR_MAPPER(cpu) (A_IMR_CPU0_BASE+(cpu)*IMR_REGISTER_SPACING)
#define A_IMR_REGISTER(cpu,reg) (A_IMR_MAPPER(cpu)+(reg))

#define R_IMR_INTERRUPT_DIAG            0x0010
#define R_IMR_INTERRUPT_LDT             0x0018
#define R_IMR_INTERRUPT_MASK            0x0028
#define R_IMR_INTERRUPT_TRACE           0x0038
#define R_IMR_INTERRUPT_SOURCE_STATUS   0x0040
#define R_IMR_LDT_INTERRUPT_SET         0x0048
#define R_IMR_LDT_INTERRUPT             0x0018
#define R_IMR_LDT_INTERRUPT_CLR         0x0020
#define R_IMR_MAILBOX_CPU               0x00c0
#define R_IMR_ALIAS_MAILBOX_CPU         0x1000
#define R_IMR_MAILBOX_SET_CPU           0x00C8
#define R_IMR_ALIAS_MAILBOX_SET_CPU     0x1008
#define R_IMR_MAILBOX_CLR_CPU           0x00D0
#define R_IMR_INTERRUPT_STATUS_BASE     0x0100
#define R_IMR_INTERRUPT_STATUS_COUNT    7
#define R_IMR_INTERRUPT_MAP_BASE        0x0200
#define R_IMR_INTERRUPT_MAP_COUNT       64

/*
 * these macros work together to build the address of a mailbox
 * register, e.g., A_MAILBOX_REGISTER(R_IMR_MAILBOX_SET_CPU,1)
 * for mbox_0_set_cpu2 returns 0x00100240C8
 */
#define A_MAILBOX_REGISTER(reg,cpu) \
    (A_IMR_CPU0_BASE + (cpu * IMR_REGISTER_SPACING) + reg)

/*  *********************************************************************
    * System Performance Counter Registers
    ********************************************************************* */

#define A_SCD_PERF_CNT_CFG          0x00100204C0
#define A_SCD_PERF_CNT_0            0x00100204D0
#define A_SCD_PERF_CNT_1            0x00100204D8
#define A_SCD_PERF_CNT_2            0x00100204E0
#define A_SCD_PERF_CNT_3            0x00100204E8

#define SCD_NUM_PERF_CNT 4
#define SCD_PERF_CNT_SPACING 8
#define A_SCD_PERF_CNT(n) (A_SCD_PERF_CNT_0+(n*SCD_PERF_CNT_SPACING))

/*  *********************************************************************
    * System Bus Watcher Registers
    ********************************************************************* */

#define A_SCD_BUS_ERR_STATUS        0x0010020880
#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define A_SCD_BUS_ERR_STATUS_DEBUG  0x00100208D0
#define A_BUS_ERR_STATUS_DEBUG  0x00100208D0
#endif /* 1250 PASS2 || 112x PASS1 */
#define A_BUS_ERR_DATA_0            0x00100208A0
#define A_BUS_ERR_DATA_1            0x00100208A8
#define A_BUS_ERR_DATA_2            0x00100208B0
#define A_BUS_ERR_DATA_3            0x00100208B8
#define A_BUS_L2_ERRORS             0x00100208C0
#define A_BUS_MEM_IO_ERRORS         0x00100208C8

/*  *********************************************************************
    * System Debug Controller Registers
    ********************************************************************* */

#define A_SCD_JTAG_BASE             0x0010000000

/*  *********************************************************************
    * System Trace Buffer Registers
    ********************************************************************* */

#define A_SCD_TRACE_CFG             0x0010020A00
#define A_SCD_TRACE_READ            0x0010020A08
#define A_SCD_TRACE_EVENT_0         0x0010020A20
#define A_SCD_TRACE_EVENT_1         0x0010020A28
#define A_SCD_TRACE_EVENT_2         0x0010020A30
#define A_SCD_TRACE_EVENT_3         0x0010020A38
#define A_SCD_TRACE_SEQUENCE_0      0x0010020A40
#define A_SCD_TRACE_SEQUENCE_1      0x0010020A48
#define A_SCD_TRACE_SEQUENCE_2      0x0010020A50
#define A_SCD_TRACE_SEQUENCE_3      0x0010020A58
#define A_SCD_TRACE_EVENT_4         0x0010020A60
#define A_SCD_TRACE_EVENT_5         0x0010020A68
#define A_SCD_TRACE_EVENT_6         0x0010020A70
#define A_SCD_TRACE_EVENT_7         0x0010020A78
#define A_SCD_TRACE_SEQUENCE_4      0x0010020A80
#define A_SCD_TRACE_SEQUENCE_5      0x0010020A88
#define A_SCD_TRACE_SEQUENCE_6      0x0010020A90
#define A_SCD_TRACE_SEQUENCE_7      0x0010020A98

#define TRACE_REGISTER_SPACING 8
#define TRACE_NUM_REGISTERS    8
#define A_SCD_TRACE_EVENT(n) (((n) & 4) ? \
   (A_SCD_TRACE_EVENT_4 + (((n) & 3) * TRACE_REGISTER_SPACING)) : \
   (A_SCD_TRACE_EVENT_0 + ((n) * TRACE_REGISTER_SPACING)))
#define A_SCD_TRACE_SEQUENCE(n) (((n) & 4) ? \
   (A_SCD_TRACE_SEQUENCE_4 + (((n) & 3) * TRACE_REGISTER_SPACING)) : \
   (A_SCD_TRACE_SEQUENCE_0 + ((n) * TRACE_REGISTER_SPACING)))

/*  *********************************************************************
    * System Generic DMA Registers
    ********************************************************************* */

#define A_DM_0		  	    0x0010020B00
#define A_DM_1		  	    0x0010020B20
#define A_DM_2			    0x0010020B40
#define A_DM_3			    0x0010020B60
#define DM_REGISTER_SPACING	    0x20
#define DM_NUM_CHANNELS		    4
#define A_DM_BASE(idx) (A_DM_0 + ((idx) * DM_REGISTER_SPACING))
#define A_DM_REGISTER(idx,reg) (A_DM_BASE(idx) + (reg))

#define R_DM_DSCR_BASE		    0x0000000000
#define R_DM_DSCR_COUNT		    0x0000000008
#define R_DM_CUR_DSCR_ADDR	    0x0000000010
#define R_DM_DSCR_BASE_DEBUG	    0x0000000018

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define A_DM_PARTIAL_0		    0x0010020ba0
#define A_DM_PARTIAL_1		    0x0010020ba8
#define A_DM_PARTIAL_2		    0x0010020bb0
#define A_DM_PARTIAL_3		    0x0010020bb8
#define DM_PARTIAL_REGISTER_SPACING 0x8
#define A_DM_PARTIAL(idx)	    (A_DM_PARTIAL_0 + ((idx) * DM_PARTIAL_REGISTER_SPACING))
#endif /* 1250 PASS3 || 112x PASS1 */

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define A_DM_CRC_0		    0x0010020b80
#define A_DM_CRC_1		    0x0010020b90
#define DM_CRC_REGISTER_SPACING	    0x10
#define DM_CRC_NUM_CHANNELS	    2
#define A_DM_CRC_BASE(idx)	    (A_DM_CRC_0 + ((idx) * DM_CRC_REGISTER_SPACING))
#define A_DM_CRC_REGISTER(idx,reg)  (A_DM_CRC_BASE(idx) + (reg))

#define R_CRC_DEF_0		    0x00
#define R_CTCP_DEF_0		    0x08
#endif /* 1250 PASS3 || 112x PASS1 */

/*  *********************************************************************
    *  Physical Address Map
    ********************************************************************* */

#if SIBYTE_HDR_FEATURE_1250_112x
#define A_PHYS_MEMORY_0                 _SB_MAKE64(0x0000000000)
#define A_PHYS_MEMORY_SIZE              _SB_MAKE64((256*1024*1024))
#define A_PHYS_SYSTEM_CTL               _SB_MAKE64(0x0010000000)
#define A_PHYS_IO_SYSTEM                _SB_MAKE64(0x0010060000)
#define A_PHYS_GENBUS			_SB_MAKE64(0x0010090000)
#define A_PHYS_GENBUS_END		_SB_MAKE64(0x0040000000)
#define A_PHYS_LDTPCI_IO_MATCH_BYTES_32 _SB_MAKE64(0x0040000000)
#define A_PHYS_LDTPCI_IO_MATCH_BITS_32  _SB_MAKE64(0x0060000000)
#define A_PHYS_MEMORY_1                 _SB_MAKE64(0x0080000000)
#define A_PHYS_MEMORY_2                 _SB_MAKE64(0x0090000000)
#define A_PHYS_MEMORY_3                 _SB_MAKE64(0x00C0000000)
#define A_PHYS_L2_CACHE_TEST            _SB_MAKE64(0x00D0000000)
#define A_PHYS_LDT_SPECIAL_MATCH_BYTES  _SB_MAKE64(0x00D8000000)
#define A_PHYS_LDTPCI_IO_MATCH_BYTES    _SB_MAKE64(0x00DC000000)
#define A_PHYS_LDTPCI_CFG_MATCH_BYTES   _SB_MAKE64(0x00DE000000)
#define A_PHYS_LDT_SPECIAL_MATCH_BITS   _SB_MAKE64(0x00F8000000)
#define A_PHYS_LDTPCI_IO_MATCH_BITS     _SB_MAKE64(0x00FC000000)
#define A_PHYS_LDTPCI_CFG_MATCH_BITS    _SB_MAKE64(0x00FE000000)
#define A_PHYS_MEMORY_EXP               _SB_MAKE64(0x0100000000)
#define A_PHYS_MEMORY_EXP_SIZE          _SB_MAKE64((508*1024*1024*1024))
#define A_PHYS_LDT_EXP                  _SB_MAKE64(0x8000000000)
#define A_PHYS_PCI_FULLACCESS_BYTES     _SB_MAKE64(0xF000000000)
#define A_PHYS_PCI_FULLACCESS_BITS      _SB_MAKE64(0xF100000000)
#define A_PHYS_RESERVED                 _SB_MAKE64(0xF200000000)
#define A_PHYS_RESERVED_SPECIAL_LDT     _SB_MAKE64(0xFD00000000)

#define A_PHYS_L2CACHE_WAY_SIZE         _SB_MAKE64(0x0000020000)
#define PHYS_L2CACHE_NUM_WAYS           4
#define A_PHYS_L2CACHE_TOTAL_SIZE       _SB_MAKE64(0x0000080000)
#define A_PHYS_L2CACHE_WAY0             _SB_MAKE64(0x00D0180000)
#define A_PHYS_L2CACHE_WAY1             _SB_MAKE64(0x00D01A0000)
#define A_PHYS_L2CACHE_WAY2             _SB_MAKE64(0x00D01C0000)
#define A_PHYS_L2CACHE_WAY3             _SB_MAKE64(0x00D01E0000)
#endif


#endif
