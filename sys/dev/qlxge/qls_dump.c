/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * File: qls_dump.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include "qls_os.h"
#include "qls_hw.h"
#include "qls_def.h"
#include "qls_glbl.h"
#include "qls_dump.h"

qls_mpi_coredump_t ql_mpi_coredump;

#define Q81_CORE_SEG_NUM              1
#define Q81_TEST_LOGIC_SEG_NUM        2
#define Q81_RMII_SEG_NUM              3
#define Q81_FCMAC1_SEG_NUM            4
#define Q81_FCMAC2_SEG_NUM            5
#define Q81_FC1_MBOX_SEG_NUM          6
#define Q81_IDE_SEG_NUM               7
#define Q81_NIC1_MBOX_SEG_NUM         8
#define Q81_SMBUS_SEG_NUM             9
#define Q81_FC2_MBOX_SEG_NUM          10
#define Q81_NIC2_MBOX_SEG_NUM         11
#define Q81_I2C_SEG_NUM               12
#define Q81_MEMC_SEG_NUM              13
#define Q81_PBUS_SEG_NUM              14
#define Q81_MDE_SEG_NUM               15
#define Q81_NIC1_CONTROL_SEG_NUM      16
#define Q81_NIC2_CONTROL_SEG_NUM      17
#define Q81_NIC1_XGMAC_SEG_NUM        18
#define Q81_NIC2_XGMAC_SEG_NUM        19
#define Q81_WCS_RAM_SEG_NUM           20
#define Q81_MEMC_RAM_SEG_NUM          21
#define Q81_XAUI1_AN_SEG_NUM          22
#define Q81_XAUI1_HSS_PCS_SEG_NUM     23
#define Q81_XFI1_AN_SEG_NUM           24
#define Q81_XFI1_TRAIN_SEG_NUM        25
#define Q81_XFI1_HSS_PCS_SEG_NUM      26
#define Q81_XFI1_HSS_TX_SEG_NUM       27
#define Q81_XFI1_HSS_RX_SEG_NUM       28
#define Q81_XFI1_HSS_PLL_SEG_NUM      29
#define Q81_INTR_STATES_SEG_NUM       31
#define Q81_ETS_SEG_NUM               34
#define Q81_PROBE_DUMP_SEG_NUM        35
#define Q81_ROUTING_INDEX_SEG_NUM     36
#define Q81_MAC_PROTOCOL_SEG_NUM      37
#define Q81_XAUI2_AN_SEG_NUM          38
#define Q81_XAUI2_HSS_PCS_SEG_NUM     39
#define Q81_XFI2_AN_SEG_NUM           40
#define Q81_XFI2_TRAIN_SEG_NUM        41
#define Q81_XFI2_HSS_PCS_SEG_NUM      42
#define Q81_XFI2_HSS_TX_SEG_NUM       43
#define Q81_XFI2_HSS_RX_SEG_NUM       44
#define Q81_XFI2_HSS_PLL_SEG_NUM      45
#define Q81_WQC1_SEG_NUM              46
#define Q81_CQC1_SEG_NUM              47
#define Q81_WQC2_SEG_NUM              48
#define Q81_CQC2_SEG_NUM              49
#define Q81_SEM_REGS_SEG_NUM          50

enum
{
	Q81_PAUSE_SRC_LO               = 0x00000100,
	Q81_PAUSE_SRC_HI               = 0x00000104,
	Q81_GLOBAL_CFG                 = 0x00000108,
	Q81_GLOBAL_CFG_RESET           = (1 << 0),    /*Control*/
	Q81_GLOBAL_CFG_JUMBO           = (1 << 6),    /*Control*/
	Q81_GLOBAL_CFG_TX_STAT_EN      = (1 << 10),   /*Control*/
	Q81_GLOBAL_CFG_RX_STAT_EN      = (1 << 11),   /*Control*/
	Q81_TX_CFG                     = 0x0000010c,
	Q81_TX_CFG_RESET               = (1 << 0),    /*Control*/
	Q81_TX_CFG_EN                  = (1 << 1),    /*Control*/
	Q81_TX_CFG_PREAM               = (1 << 2),    /*Control*/
	Q81_RX_CFG                     = 0x00000110,
	Q81_RX_CFG_RESET               = (1 << 0),    /*Control*/
	Q81_RX_CFG_EN                  = (1 << 1),    /*Control*/
	Q81_RX_CFG_PREAM               = (1 << 2),    /*Control*/
	Q81_FLOW_CTL                   = 0x0000011c,
	Q81_PAUSE_OPCODE               = 0x00000120,
	Q81_PAUSE_TIMER                = 0x00000124,
	Q81_PAUSE_FRM_DEST_LO          = 0x00000128,
	Q81_PAUSE_FRM_DEST_HI          = 0x0000012c,
	Q81_MAC_TX_PARAMS              = 0x00000134,
	Q81_MAC_TX_PARAMS_JUMBO        = (1U << 31),   /*Control*/
	Q81_MAC_TX_PARAMS_SIZE_SHIFT   = 16,          /*Control*/
	Q81_MAC_RX_PARAMS              = 0x00000138,
	Q81_MAC_SYS_INT                = 0x00000144,
	Q81_MAC_SYS_INT_MASK           = 0x00000148,
	Q81_MAC_MGMT_INT               = 0x0000014c,
	Q81_MAC_MGMT_IN_MASK           = 0x00000150,
	Q81_EXT_ARB_MODE               = 0x000001fc,
	Q81_TX_PKTS                    = 0x00000200,
	Q81_TX_PKTS_LO                 = 0x00000204,
	Q81_TX_BYTES                   = 0x00000208,
	Q81_TX_BYTES_LO                = 0x0000020C,
	Q81_TX_MCAST_PKTS              = 0x00000210,
	Q81_TX_MCAST_PKTS_LO           = 0x00000214,
	Q81_TX_BCAST_PKTS              = 0x00000218,
	Q81_TX_BCAST_PKTS_LO           = 0x0000021C,
	Q81_TX_UCAST_PKTS              = 0x00000220,
	Q81_TX_UCAST_PKTS_LO           = 0x00000224,
	Q81_TX_CTL_PKTS                = 0x00000228,
	Q81_TX_CTL_PKTS_LO             = 0x0000022c,
	Q81_TX_PAUSE_PKTS              = 0x00000230,
	Q81_TX_PAUSE_PKTS_LO           = 0x00000234,
	Q81_TX_64_PKT                  = 0x00000238,
	Q81_TX_64_PKT_LO               = 0x0000023c,
	Q81_TX_65_TO_127_PKT           = 0x00000240,
	Q81_TX_65_TO_127_PKT_LO        = 0x00000244,
	Q81_TX_128_TO_255_PKT          = 0x00000248,
	Q81_TX_128_TO_255_PKT_LO       = 0x0000024c,
	Q81_TX_256_511_PKT             = 0x00000250,
	Q81_TX_256_511_PKT_LO          = 0x00000254,
	Q81_TX_512_TO_1023_PKT         = 0x00000258,
	Q81_TX_512_TO_1023_PKT_LO      = 0x0000025c,
	Q81_TX_1024_TO_1518_PKT        = 0x00000260,
	Q81_TX_1024_TO_1518_PKT_LO     = 0x00000264,
	Q81_TX_1519_TO_MAX_PKT         = 0x00000268,
	Q81_TX_1519_TO_MAX_PKT_LO      = 0x0000026c,
	Q81_TX_UNDERSIZE_PKT           = 0x00000270,
	Q81_TX_UNDERSIZE_PKT_LO        = 0x00000274,
	Q81_TX_OVERSIZE_PKT            = 0x00000278,
	Q81_TX_OVERSIZE_PKT_LO         = 0x0000027c,
	Q81_RX_HALF_FULL_DET           = 0x000002a0,
	Q81_TX_HALF_FULL_DET_LO        = 0x000002a4,
	Q81_RX_OVERFLOW_DET            = 0x000002a8,
	Q81_TX_OVERFLOW_DET_LO         = 0x000002ac,
	Q81_RX_HALF_FULL_MASK          = 0x000002b0,
	Q81_TX_HALF_FULL_MASK_LO       = 0x000002b4,
	Q81_RX_OVERFLOW_MASK           = 0x000002b8,
	Q81_TX_OVERFLOW_MASK_LO        = 0x000002bc,
	Q81_STAT_CNT_CTL               = 0x000002c0,
	Q81_STAT_CNT_CTL_CLEAR_TX      = (1 << 0),   /*Control*/
	Q81_STAT_CNT_CTL_CLEAR_RX      = (1 << 1),   /*Control*/
	Q81_AUX_RX_HALF_FULL_DET       = 0x000002d0,
	Q81_AUX_TX_HALF_FULL_DET       = 0x000002d4,
	Q81_AUX_RX_OVERFLOW_DET        = 0x000002d8,
	Q81_AUX_TX_OVERFLOW_DET        = 0x000002dc,
	Q81_AUX_RX_HALF_FULL_MASK      = 0x000002f0,
	Q81_AUX_TX_HALF_FULL_MASK      = 0x000002f4,
	Q81_AUX_RX_OVERFLOW_MASK       = 0x000002f8,
	Q81_AUX_TX_OVERFLOW_MASK       = 0x000002fc,
	Q81_RX_BYTES                   = 0x00000300,
	Q81_RX_BYTES_LO                = 0x00000304,
	Q81_RX_BYTES_OK                = 0x00000308,
	Q81_RX_BYTES_OK_LO             = 0x0000030c,
	Q81_RX_PKTS                    = 0x00000310,
	Q81_RX_PKTS_LO                 = 0x00000314,
	Q81_RX_PKTS_OK                 = 0x00000318,
	Q81_RX_PKTS_OK_LO              = 0x0000031c,
	Q81_RX_BCAST_PKTS              = 0x00000320,
	Q81_RX_BCAST_PKTS_LO           = 0x00000324,
	Q81_RX_MCAST_PKTS              = 0x00000328,
	Q81_RX_MCAST_PKTS_LO           = 0x0000032c,
	Q81_RX_UCAST_PKTS              = 0x00000330,
	Q81_RX_UCAST_PKTS_LO           = 0x00000334,
	Q81_RX_UNDERSIZE_PKTS          = 0x00000338,
	Q81_RX_UNDERSIZE_PKTS_LO       = 0x0000033c,
	Q81_RX_OVERSIZE_PKTS           = 0x00000340,
	Q81_RX_OVERSIZE_PKTS_LO        = 0x00000344,
	Q81_RX_JABBER_PKTS             = 0x00000348,
	Q81_RX_JABBER_PKTS_LO          = 0x0000034c,
	Q81_RX_UNDERSIZE_FCERR_PKTS    = 0x00000350,
	Q81_RX_UNDERSIZE_FCERR_PKTS_LO = 0x00000354,
	Q81_RX_DROP_EVENTS             = 0x00000358,
	Q81_RX_DROP_EVENTS_LO          = 0x0000035c,
	Q81_RX_FCERR_PKTS              = 0x00000360,
	Q81_RX_FCERR_PKTS_LO           = 0x00000364,
	Q81_RX_ALIGN_ERR               = 0x00000368,
	Q81_RX_ALIGN_ERR_LO            = 0x0000036c,
	Q81_RX_SYMBOL_ERR              = 0x00000370,
	Q81_RX_SYMBOL_ERR_LO           = 0x00000374,
	Q81_RX_MAC_ERR                 = 0x00000378,
	Q81_RX_MAC_ERR_LO              = 0x0000037c,
	Q81_RX_CTL_PKTS                = 0x00000380,
	Q81_RX_CTL_PKTS_LO             = 0x00000384,
	Q81_RX_PAUSE_PKTS              = 0x00000388,
	Q81_RX_PAUSE_PKTS_LO           = 0x0000038c,
	Q81_RX_64_PKTS                 = 0x00000390,
	Q81_RX_64_PKTS_LO              = 0x00000394,
	Q81_RX_65_TO_127_PKTS          = 0x00000398,
	Q81_RX_65_TO_127_PKTS_LO       = 0x0000039c,
	Q81_RX_128_255_PKTS            = 0x000003a0,
	Q81_RX_128_255_PKTS_LO         = 0x000003a4,
	Q81_RX_256_511_PKTS            = 0x000003a8,
	Q81_RX_256_511_PKTS_LO         = 0x000003ac,
	Q81_RX_512_TO_1023_PKTS        = 0x000003b0,
	Q81_RX_512_TO_1023_PKTS_LO     = 0x000003b4,
	Q81_RX_1024_TO_1518_PKTS       = 0x000003b8,
	Q81_RX_1024_TO_1518_PKTS_LO    = 0x000003bc,
	Q81_RX_1519_TO_MAX_PKTS        = 0x000003c0,
	Q81_RX_1519_TO_MAX_PKTS_LO     = 0x000003c4,
	Q81_RX_LEN_ERR_PKTS            = 0x000003c8,
	Q81_RX_LEN_ERR_PKTS_LO         = 0x000003cc,
	Q81_MDIO_TX_DATA               = 0x00000400,
	Q81_MDIO_RX_DATA               = 0x00000410,
	Q81_MDIO_CMD                   = 0x00000420,
	Q81_MDIO_PHY_ADDR              = 0x00000430,
	Q81_MDIO_PORT                  = 0x00000440,
	Q81_MDIO_STATUS                = 0x00000450,
	Q81_TX_CBFC_PAUSE_FRAMES0      = 0x00000500,
	Q81_TX_CBFC_PAUSE_FRAMES0_LO   = 0x00000504,
	Q81_TX_CBFC_PAUSE_FRAMES1      = 0x00000508,
	Q81_TX_CBFC_PAUSE_FRAMES1_LO   = 0x0000050C,
	Q81_TX_CBFC_PAUSE_FRAMES2      = 0x00000510,
	Q81_TX_CBFC_PAUSE_FRAMES2_LO   = 0x00000514,
	Q81_TX_CBFC_PAUSE_FRAMES3      = 0x00000518,
	Q81_TX_CBFC_PAUSE_FRAMES3_LO   = 0x0000051C,
	Q81_TX_CBFC_PAUSE_FRAMES4      = 0x00000520,
	Q81_TX_CBFC_PAUSE_FRAMES4_LO   = 0x00000524,
	Q81_TX_CBFC_PAUSE_FRAMES5      = 0x00000528,
	Q81_TX_CBFC_PAUSE_FRAMES5_LO   = 0x0000052C,
	Q81_TX_CBFC_PAUSE_FRAMES6      = 0x00000530,
	Q81_TX_CBFC_PAUSE_FRAMES6_LO   = 0x00000534,
	Q81_TX_CBFC_PAUSE_FRAMES7      = 0x00000538,
	Q81_TX_CBFC_PAUSE_FRAMES7_LO   = 0x0000053C,
	Q81_TX_FCOE_PKTS               = 0x00000540,
	Q81_TX_FCOE_PKTS_LO            = 0x00000544,
	Q81_TX_MGMT_PKTS               = 0x00000548,
	Q81_TX_MGMT_PKTS_LO            = 0x0000054C,
	Q81_RX_CBFC_PAUSE_FRAMES0      = 0x00000568,
	Q81_RX_CBFC_PAUSE_FRAMES0_LO   = 0x0000056C,
	Q81_RX_CBFC_PAUSE_FRAMES1      = 0x00000570,
	Q81_RX_CBFC_PAUSE_FRAMES1_LO   = 0x00000574,
	Q81_RX_CBFC_PAUSE_FRAMES2      = 0x00000578,
	Q81_RX_CBFC_PAUSE_FRAMES2_LO   = 0x0000057C,
	Q81_RX_CBFC_PAUSE_FRAMES3      = 0x00000580,
	Q81_RX_CBFC_PAUSE_FRAMES3_LO   = 0x00000584,
	Q81_RX_CBFC_PAUSE_FRAMES4      = 0x00000588,
	Q81_RX_CBFC_PAUSE_FRAMES4_LO   = 0x0000058C,
	Q81_RX_CBFC_PAUSE_FRAMES5      = 0x00000590,
	Q81_RX_CBFC_PAUSE_FRAMES5_LO   = 0x00000594,
	Q81_RX_CBFC_PAUSE_FRAMES6      = 0x00000598,
	Q81_RX_CBFC_PAUSE_FRAMES6_LO   = 0x0000059C,
	Q81_RX_CBFC_PAUSE_FRAMES7      = 0x000005A0,
	Q81_RX_CBFC_PAUSE_FRAMES7_LO   = 0x000005A4,
	Q81_RX_FCOE_PKTS               = 0x000005A8,
	Q81_RX_FCOE_PKTS_LO            = 0x000005AC,
	Q81_RX_MGMT_PKTS               = 0x000005B0,
	Q81_RX_MGMT_PKTS_LO            = 0x000005B4,
	Q81_RX_NIC_FIFO_DROP           = 0x000005B8,
	Q81_RX_NIC_FIFO_DROP_LO        = 0x000005BC,
	Q81_RX_FCOE_FIFO_DROP          = 0x000005C0,
	Q81_RX_FCOE_FIFO_DROP_LO       = 0x000005C4,
	Q81_RX_MGMT_FIFO_DROP          = 0x000005C8,
	Q81_RX_MGMT_FIFO_DROP_LO       = 0x000005CC,
	Q81_RX_PKTS_PRIORITY0          = 0x00000600,
	Q81_RX_PKTS_PRIORITY0_LO       = 0x00000604,
	Q81_RX_PKTS_PRIORITY1          = 0x00000608,
	Q81_RX_PKTS_PRIORITY1_LO       = 0x0000060C,
	Q81_RX_PKTS_PRIORITY2          = 0x00000610,
	Q81_RX_PKTS_PRIORITY2_LO       = 0x00000614,
	Q81_RX_PKTS_PRIORITY3          = 0x00000618,
	Q81_RX_PKTS_PRIORITY3_LO       = 0x0000061C,
	Q81_RX_PKTS_PRIORITY4          = 0x00000620,
	Q81_RX_PKTS_PRIORITY4_LO       = 0x00000624,
	Q81_RX_PKTS_PRIORITY5          = 0x00000628,
	Q81_RX_PKTS_PRIORITY5_LO       = 0x0000062C,
	Q81_RX_PKTS_PRIORITY6          = 0x00000630,
	Q81_RX_PKTS_PRIORITY6_LO       = 0x00000634,
	Q81_RX_PKTS_PRIORITY7          = 0x00000638,
	Q81_RX_PKTS_PRIORITY7_LO       = 0x0000063C,
	Q81_RX_OCTETS_PRIORITY0        = 0x00000640,
	Q81_RX_OCTETS_PRIORITY0_LO     = 0x00000644,
	Q81_RX_OCTETS_PRIORITY1        = 0x00000648,
	Q81_RX_OCTETS_PRIORITY1_LO     = 0x0000064C,
	Q81_RX_OCTETS_PRIORITY2        = 0x00000650,
	Q81_RX_OCTETS_PRIORITY2_LO     = 0x00000654,
	Q81_RX_OCTETS_PRIORITY3        = 0x00000658,
	Q81_RX_OCTETS_PRIORITY3_LO     = 0x0000065C,
	Q81_RX_OCTETS_PRIORITY4        = 0x00000660,
	Q81_RX_OCTETS_PRIORITY4_LO     = 0x00000664,
	Q81_RX_OCTETS_PRIORITY5        = 0x00000668,
	Q81_RX_OCTETS_PRIORITY5_LO     = 0x0000066C,
	Q81_RX_OCTETS_PRIORITY6        = 0x00000670,
	Q81_RX_OCTETS_PRIORITY6_LO     = 0x00000674,
	Q81_RX_OCTETS_PRIORITY7        = 0x00000678,
	Q81_RX_OCTETS_PRIORITY7_LO     = 0x0000067C,
	Q81_TX_PKTS_PRIORITY0          = 0x00000680,
	Q81_TX_PKTS_PRIORITY0_LO       = 0x00000684,
	Q81_TX_PKTS_PRIORITY1          = 0x00000688,
	Q81_TX_PKTS_PRIORITY1_LO       = 0x0000068C,
	Q81_TX_PKTS_PRIORITY2          = 0x00000690,
	Q81_TX_PKTS_PRIORITY2_LO       = 0x00000694,
	Q81_TX_PKTS_PRIORITY3          = 0x00000698,
	Q81_TX_PKTS_PRIORITY3_LO       = 0x0000069C,
	Q81_TX_PKTS_PRIORITY4          = 0x000006A0,
	Q81_TX_PKTS_PRIORITY4_LO       = 0x000006A4,
	Q81_TX_PKTS_PRIORITY5          = 0x000006A8,
	Q81_TX_PKTS_PRIORITY5_LO       = 0x000006AC,
	Q81_TX_PKTS_PRIORITY6          = 0x000006B0,
	Q81_TX_PKTS_PRIORITY6_LO       = 0x000006B4,
	Q81_TX_PKTS_PRIORITY7          = 0x000006B8,
	Q81_TX_PKTS_PRIORITY7_LO       = 0x000006BC,
	Q81_TX_OCTETS_PRIORITY0        = 0x000006C0,
	Q81_TX_OCTETS_PRIORITY0_LO     = 0x000006C4,
	Q81_TX_OCTETS_PRIORITY1        = 0x000006C8,
	Q81_TX_OCTETS_PRIORITY1_LO     = 0x000006CC,
	Q81_TX_OCTETS_PRIORITY2        = 0x000006D0,
	Q81_TX_OCTETS_PRIORITY2_LO     = 0x000006D4,
	Q81_TX_OCTETS_PRIORITY3        = 0x000006D8,
	Q81_TX_OCTETS_PRIORITY3_LO     = 0x000006DC,
	Q81_TX_OCTETS_PRIORITY4        = 0x000006E0,
	Q81_TX_OCTETS_PRIORITY4_LO     = 0x000006E4,
	Q81_TX_OCTETS_PRIORITY5        = 0x000006E8,
	Q81_TX_OCTETS_PRIORITY5_LO     = 0x000006EC,
	Q81_TX_OCTETS_PRIORITY6        = 0x000006F0,
	Q81_TX_OCTETS_PRIORITY6_LO     = 0x000006F4,
	Q81_TX_OCTETS_PRIORITY7        = 0x000006F8,
	Q81_TX_OCTETS_PRIORITY7_LO     = 0x000006FC,
	Q81_RX_DISCARD_PRIORITY0       = 0x00000700,
	Q81_RX_DISCARD_PRIORITY0_LO    = 0x00000704,
	Q81_RX_DISCARD_PRIORITY1       = 0x00000708,
	Q81_RX_DISCARD_PRIORITY1_LO    = 0x0000070C,
	Q81_RX_DISCARD_PRIORITY2       = 0x00000710,
	Q81_RX_DISCARD_PRIORITY2_LO    = 0x00000714,
	Q81_RX_DISCARD_PRIORITY3       = 0x00000718,
	Q81_RX_DISCARD_PRIORITY3_LO    = 0x0000071C,
	Q81_RX_DISCARD_PRIORITY4       = 0x00000720,
	Q81_RX_DISCARD_PRIORITY4_LO    = 0x00000724,
	Q81_RX_DISCARD_PRIORITY5       = 0x00000728,
	Q81_RX_DISCARD_PRIORITY5_LO    = 0x0000072C,
	Q81_RX_DISCARD_PRIORITY6       = 0x00000730,
	Q81_RX_DISCARD_PRIORITY6_LO    = 0x00000734,
	Q81_RX_DISCARD_PRIORITY7       = 0x00000738,
	Q81_RX_DISCARD_PRIORITY7_LO    = 0x0000073C
};

static void
qls_mpid_seg_hdr(qls_mpid_seg_hdr_t *seg_hdr, uint32_t seg_num,
	uint32_t seg_size, unsigned char *desc)
{
	memset(seg_hdr, 0, sizeof(qls_mpid_seg_hdr_t));

	seg_hdr->cookie = Q81_MPID_COOKIE;
	seg_hdr->seg_num = seg_num;
	seg_hdr->seg_size = seg_size;

	memcpy(seg_hdr->desc, desc, (sizeof(seg_hdr->desc))-1);

	return;
}

static int
qls_wait_reg_rdy(qla_host_t *ha , uint32_t reg, uint32_t bit, uint32_t err_bit)
{
	uint32_t data;
	int count = 10;

	while (count) {

		data = READ_REG32(ha, reg);

		if (data & err_bit)
			return (-1);
		else if (data & bit)
			return (0);

		qls_mdelay(__func__, 10);
		count--;
	}
	return (-1);
}

static int
qls_rd_mpi_reg(qla_host_t *ha, uint32_t reg, uint32_t *data)
{
        int ret;

        ret = qls_wait_reg_rdy(ha, Q81_CTL_PROC_ADDR, Q81_CTL_PROC_ADDR_RDY,
			Q81_CTL_PROC_ADDR_ERR);

        if (ret)
                goto exit_qls_rd_mpi_reg;

        WRITE_REG32(ha, Q81_CTL_PROC_ADDR, reg | Q81_CTL_PROC_ADDR_READ);

        ret = qls_wait_reg_rdy(ha, Q81_CTL_PROC_ADDR, Q81_CTL_PROC_ADDR_RDY,
			Q81_CTL_PROC_ADDR_ERR);

        if (ret)
                goto exit_qls_rd_mpi_reg;

        *data = READ_REG32(ha, Q81_CTL_PROC_DATA);

exit_qls_rd_mpi_reg:
        return (ret);
}

static int
qls_wr_mpi_reg(qla_host_t *ha, uint32_t reg, uint32_t data)
{
        int ret = 0;

        ret = qls_wait_reg_rdy(ha, Q81_CTL_PROC_ADDR, Q81_CTL_PROC_ADDR_RDY,
			Q81_CTL_PROC_ADDR_ERR);
        if (ret)
                goto exit_qls_wr_mpi_reg;

        WRITE_REG32(ha, Q81_CTL_PROC_DATA, data);

        WRITE_REG32(ha, Q81_CTL_PROC_ADDR, reg);

        ret = qls_wait_reg_rdy(ha, Q81_CTL_PROC_ADDR, Q81_CTL_PROC_ADDR_RDY,
			Q81_CTL_PROC_ADDR_ERR);
exit_qls_wr_mpi_reg:
        return (ret);
}


#define Q81_TEST_LOGIC_FUNC_PORT_CONFIG 0x1002
#define Q81_INVALID_NUM		0xFFFFFFFF

#define Q81_NIC1_FUNC_ENABLE	0x00000001
#define Q81_NIC1_FUNC_MASK	0x0000000e
#define Q81_NIC1_FUNC_SHIFT	1
#define Q81_NIC2_FUNC_ENABLE	0x00000010
#define Q81_NIC2_FUNC_MASK	0x000000e0
#define Q81_NIC2_FUNC_SHIFT	5
#define Q81_FUNCTION_SHIFT	6

static uint32_t
qls_get_other_fnum(qla_host_t *ha)
{
	int		ret;
	uint32_t	o_func;
	uint32_t	test_logic;
	uint32_t	nic1_fnum = Q81_INVALID_NUM;
	uint32_t	nic2_fnum = Q81_INVALID_NUM;

	ret = qls_rd_mpi_reg(ha, Q81_TEST_LOGIC_FUNC_PORT_CONFIG, &test_logic);
	if (ret)
		return(Q81_INVALID_NUM);

	if (test_logic & Q81_NIC1_FUNC_ENABLE)
		nic1_fnum = (test_logic & Q81_NIC1_FUNC_MASK) >>
					Q81_NIC1_FUNC_SHIFT;

	if (test_logic & Q81_NIC2_FUNC_ENABLE)
		nic2_fnum = (test_logic & Q81_NIC2_FUNC_MASK) >>
					Q81_NIC2_FUNC_SHIFT;

	if (ha->pci_func == 0)
		o_func = nic2_fnum;
	else
		o_func = nic1_fnum;

	return(o_func);
}

static uint32_t
qls_rd_ofunc_reg(qla_host_t *ha, uint32_t reg)
{
	uint32_t	ofunc;
	uint32_t	data;
	int		ret = 0;

	ofunc = qls_get_other_fnum(ha);

	if (ofunc == Q81_INVALID_NUM)
		return(Q81_INVALID_NUM);

	reg = Q81_CTL_PROC_ADDR_REG_BLOCK | (ofunc << Q81_FUNCTION_SHIFT) | reg;

	ret = qls_rd_mpi_reg(ha, reg, &data);

	if (ret != 0)
		return(Q81_INVALID_NUM);

	return(data);
}

static void
qls_wr_ofunc_reg(qla_host_t *ha, uint32_t reg, uint32_t value)
{
	uint32_t ofunc;
	int ret = 0;

	ofunc = qls_get_other_fnum(ha);

	if (ofunc == Q81_INVALID_NUM)
		return;

	reg = Q81_CTL_PROC_ADDR_REG_BLOCK | (ofunc << Q81_FUNCTION_SHIFT) | reg;

	ret = qls_wr_mpi_reg(ha, reg, value);

	return;
}

static int
qls_wait_ofunc_reg_rdy(qla_host_t *ha , uint32_t reg, uint32_t bit,
	uint32_t err_bit)
{
        uint32_t data;
        int count = 10;

        while (count) {

                data = qls_rd_ofunc_reg(ha, reg);

                if (data & err_bit)
                        return (-1);
                else if (data & bit)
                        return (0);

                qls_mdelay(__func__, 10);
                count--;
        }
        return (-1);
}

#define Q81_XG_SERDES_ADDR_RDY	BIT_31
#define Q81_XG_SERDES_ADDR_READ	BIT_30

static int
qls_rd_ofunc_serdes_reg(qla_host_t *ha, uint32_t reg, uint32_t *data)
{
	int ret;

	/* wait for reg to come ready */
	ret = qls_wait_ofunc_reg_rdy(ha, (Q81_CTL_XG_SERDES_ADDR >> 2),
			Q81_XG_SERDES_ADDR_RDY, 0);
	if (ret)
		goto exit_qls_rd_ofunc_serdes_reg;

	/* set up for reg read */
	qls_wr_ofunc_reg(ha, (Q81_CTL_XG_SERDES_ADDR >> 2),
		(reg | Q81_XG_SERDES_ADDR_READ));

	/* wait for reg to come ready */
	ret = qls_wait_ofunc_reg_rdy(ha, (Q81_CTL_XG_SERDES_ADDR >> 2),
			Q81_XG_SERDES_ADDR_RDY, 0);
	if (ret)
		goto exit_qls_rd_ofunc_serdes_reg;

	/* get the data */
	*data = qls_rd_ofunc_reg(ha, (Q81_CTL_XG_SERDES_DATA >> 2));

exit_qls_rd_ofunc_serdes_reg:
	return ret;
}

#define Q81_XGMAC_ADDR_RDY	BIT_31
#define Q81_XGMAC_ADDR_R	BIT_30
#define Q81_XGMAC_ADDR_XME	BIT_29

static int
qls_rd_ofunc_xgmac_reg(qla_host_t *ha, uint32_t reg, uint32_t *data)
{
	int ret = 0;

	ret = qls_wait_ofunc_reg_rdy(ha, (Q81_CTL_XGMAC_ADDR >> 2),
			Q81_XGMAC_ADDR_RDY, Q81_XGMAC_ADDR_XME);

	if (ret)
		goto exit_qls_rd_ofunc_xgmac_reg;

	qls_wr_ofunc_reg(ha, (Q81_XGMAC_ADDR_RDY >> 2),
		(reg | Q81_XGMAC_ADDR_R));

	ret = qls_wait_ofunc_reg_rdy(ha, (Q81_CTL_XGMAC_ADDR >> 2),
			Q81_XGMAC_ADDR_RDY, Q81_XGMAC_ADDR_XME);
	if (ret)
		goto exit_qls_rd_ofunc_xgmac_reg;

	*data = qls_rd_ofunc_reg(ha, Q81_CTL_XGMAC_DATA);

exit_qls_rd_ofunc_xgmac_reg:
	return ret;
}

static int
qls_rd_serdes_reg(qla_host_t *ha, uint32_t reg, uint32_t *data)
{
	int ret;

	ret = qls_wait_reg_rdy(ha, Q81_CTL_XG_SERDES_ADDR,
			Q81_XG_SERDES_ADDR_RDY, 0);

	if (ret)
		goto exit_qls_rd_serdes_reg;

	WRITE_REG32(ha, Q81_CTL_XG_SERDES_ADDR, \
		(reg | Q81_XG_SERDES_ADDR_READ));

	ret = qls_wait_reg_rdy(ha, Q81_CTL_XG_SERDES_ADDR,
			Q81_XG_SERDES_ADDR_RDY, 0);

	if (ret)
		goto exit_qls_rd_serdes_reg;

	*data = READ_REG32(ha, Q81_CTL_XG_SERDES_DATA);

exit_qls_rd_serdes_reg:

	return ret;
}

static void
qls_get_both_serdes(qla_host_t *ha, uint32_t addr, uint32_t *dptr,
	uint32_t *ind_ptr, uint32_t dvalid, uint32_t ind_valid)
{
	int ret = -1;

	if (dvalid)
		ret = qls_rd_serdes_reg(ha, addr, dptr);

	if (ret)
		*dptr = Q81_BAD_DATA;

	ret = -1;

	if(ind_valid)
		ret = qls_rd_ofunc_serdes_reg(ha, addr, ind_ptr);

	if (ret)
		*ind_ptr = Q81_BAD_DATA;
}

#define Q81_XFI1_POWERED_UP 0x00000005
#define Q81_XFI2_POWERED_UP 0x0000000A
#define Q81_XAUI_POWERED_UP 0x00000001

static int
qls_rd_serdes_regs(qla_host_t *ha, qls_mpi_coredump_t *mpi_dump)
{
	int ret;
	uint32_t xfi_d_valid, xfi_ind_valid, xaui_d_valid, xaui_ind_valid;
	uint32_t temp, xaui_reg, i;
	uint32_t *dptr, *indptr;

	xfi_d_valid = xfi_ind_valid = xaui_d_valid = xaui_ind_valid = 0;

	xaui_reg = 0x800;

	ret = qls_rd_ofunc_serdes_reg(ha, xaui_reg, &temp);
	if (ret)
		temp = 0;

	if ((temp & Q81_XAUI_POWERED_UP) == Q81_XAUI_POWERED_UP)
		xaui_ind_valid = 1;

	ret = qls_rd_serdes_reg(ha, xaui_reg, &temp);
	if (ret)
		temp = 0;

	if ((temp & Q81_XAUI_POWERED_UP) == Q81_XAUI_POWERED_UP)
		xaui_d_valid = 1;

	ret = qls_rd_serdes_reg(ha, 0x1E06, &temp);
	if (ret)
		temp = 0;

	if ((temp & Q81_XFI1_POWERED_UP) == Q81_XFI1_POWERED_UP) {

		if (ha->pci_func & 1)
         		xfi_ind_valid = 1; /* NIC 2, so the indirect
						 (NIC1) xfi is up*/
		else
			xfi_d_valid = 1;
	}

	if((temp & Q81_XFI2_POWERED_UP) == Q81_XFI2_POWERED_UP) {

		if(ha->pci_func & 1)
			xfi_d_valid = 1; /* NIC 2, so the indirect (NIC1)
						xfi is up */
		else
			xfi_ind_valid = 1;
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xaui_an);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xaui_an);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xaui_an);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xaui_an);
	}

	for (i = 0; i <= 0x000000034; i += 4, dptr ++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xaui_d_valid, xaui_ind_valid);
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xaui_hss_pcs);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xaui_hss_pcs);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xaui_hss_pcs);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xaui_hss_pcs);
	}

	for (i = 0x800; i <= 0x880; i += 4, dptr ++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xaui_d_valid, xaui_ind_valid);
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xfi_an);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xfi_an);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xfi_an);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xfi_an);
	}

	for (i = 0x1000; i <= 0x1034; i += 4, dptr ++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xfi_d_valid, xfi_ind_valid);
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xfi_train);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xfi_train);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xfi_train);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xfi_train);
	}

	for (i = 0x1050; i <= 0x107c; i += 4, dptr ++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xfi_d_valid, xfi_ind_valid);
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_pcs);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_pcs);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_pcs);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_pcs);
	}

	for (i = 0x1800; i <= 0x1838; i += 4, dptr++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xfi_d_valid, xfi_ind_valid);
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_tx);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_tx);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_tx);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_tx);
	}

	for (i = 0x1c00; i <= 0x1c1f; i++, dptr ++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xfi_d_valid, xfi_ind_valid);
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_rx);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_rx);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_rx);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_rx);
	}

	for (i = 0x1c40; i <= 0x1c5f; i++, dptr ++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xfi_d_valid, xfi_ind_valid);
	}

	if (ha->pci_func & 1) {
		dptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_pll);
		indptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_pll);
	} else {
		dptr = (uint32_t *)(&mpi_dump->serdes1_xfi_hss_pll);
		indptr = (uint32_t *)(&mpi_dump->serdes2_xfi_hss_pll);
	}

	for (i = 0x1e00; i <= 0x1e1f; i++, dptr ++, indptr ++) {
		qls_get_both_serdes(ha, i, dptr, indptr,
			xfi_d_valid, xfi_ind_valid);
	}

	return(0);
}

static int
qls_unpause_mpi_risc(qla_host_t *ha)
{
	uint32_t data;

	data = READ_REG32(ha, Q81_CTL_HOST_CMD_STATUS);

	if (!(data & Q81_CTL_HCS_RISC_PAUSED))
		return -1;

	WRITE_REG32(ha, Q81_CTL_HOST_CMD_STATUS, \
		Q81_CTL_HCS_CMD_CLR_RISC_PAUSE);

	return 0;
}

static int
qls_pause_mpi_risc(qla_host_t *ha)
{
	uint32_t data;
	int count = 10;

	WRITE_REG32(ha, Q81_CTL_HOST_CMD_STATUS, \
		Q81_CTL_HCS_CMD_SET_RISC_PAUSE);

	do {
		data = READ_REG32(ha, Q81_CTL_HOST_CMD_STATUS);

		if (data & Q81_CTL_HCS_RISC_PAUSED)
			break;

		qls_mdelay(__func__, 10);

		count--;

	} while (count);

	return ((count == 0) ? -1 : 0);
}

static void
qls_get_intr_states(qla_host_t *ha, uint32_t *buf)
{
	int i;

	for (i = 0; i < MAX_RX_RINGS; i++, buf++) {

		WRITE_REG32(ha, Q81_CTL_INTR_ENABLE, (0x037f0300 + i));

		*buf = READ_REG32(ha, Q81_CTL_INTR_ENABLE);
	}
}

static int
qls_rd_xgmac_reg(qla_host_t *ha, uint32_t reg, uint32_t*data)
{
	int ret = 0;

	ret = qls_wait_reg_rdy(ha, Q81_CTL_XGMAC_ADDR, Q81_XGMAC_ADDR_RDY,
			Q81_XGMAC_ADDR_XME);
	if (ret)
		goto exit_qls_rd_xgmac_reg;

	WRITE_REG32(ha, Q81_CTL_XGMAC_ADDR, (reg | Q81_XGMAC_ADDR_R));

	ret = qls_wait_reg_rdy(ha, Q81_CTL_XGMAC_ADDR, Q81_XGMAC_ADDR_RDY,
			Q81_XGMAC_ADDR_XME);
	if (ret)
		goto exit_qls_rd_xgmac_reg;

	*data = READ_REG32(ha, Q81_CTL_XGMAC_DATA);

exit_qls_rd_xgmac_reg:
	return ret;
}

static int
qls_rd_xgmac_regs(qla_host_t *ha, uint32_t *buf, uint32_t o_func)
{
	int ret = 0;
	int i;

	for (i = 0; i < Q81_XGMAC_REGISTER_END; i += 4, buf ++) {

		switch (i) {
		case  Q81_PAUSE_SRC_LO               :
		case  Q81_PAUSE_SRC_HI               :
		case  Q81_GLOBAL_CFG                 :
		case  Q81_TX_CFG                     :
		case  Q81_RX_CFG                     :
		case  Q81_FLOW_CTL                   :
		case  Q81_PAUSE_OPCODE               :
		case  Q81_PAUSE_TIMER                :
		case  Q81_PAUSE_FRM_DEST_LO          :
		case  Q81_PAUSE_FRM_DEST_HI          :
		case  Q81_MAC_TX_PARAMS              :
		case  Q81_MAC_RX_PARAMS              :
		case  Q81_MAC_SYS_INT                :
		case  Q81_MAC_SYS_INT_MASK           :
		case  Q81_MAC_MGMT_INT               :
		case  Q81_MAC_MGMT_IN_MASK           :
		case  Q81_EXT_ARB_MODE               :
		case  Q81_TX_PKTS                    :
		case  Q81_TX_PKTS_LO                 :
		case  Q81_TX_BYTES                   :
		case  Q81_TX_BYTES_LO                :
		case  Q81_TX_MCAST_PKTS              :
		case  Q81_TX_MCAST_PKTS_LO           :
		case  Q81_TX_BCAST_PKTS              :
		case  Q81_TX_BCAST_PKTS_LO           :
		case  Q81_TX_UCAST_PKTS              :
		case  Q81_TX_UCAST_PKTS_LO           :
		case  Q81_TX_CTL_PKTS                :
		case  Q81_TX_CTL_PKTS_LO             :
		case  Q81_TX_PAUSE_PKTS              :
		case  Q81_TX_PAUSE_PKTS_LO           :
		case  Q81_TX_64_PKT                  :
		case  Q81_TX_64_PKT_LO               :
		case  Q81_TX_65_TO_127_PKT           :
		case  Q81_TX_65_TO_127_PKT_LO        :
		case  Q81_TX_128_TO_255_PKT          :
		case  Q81_TX_128_TO_255_PKT_LO       :
		case  Q81_TX_256_511_PKT             :
		case  Q81_TX_256_511_PKT_LO          :
		case  Q81_TX_512_TO_1023_PKT         :
		case  Q81_TX_512_TO_1023_PKT_LO      :
		case  Q81_TX_1024_TO_1518_PKT        :
		case  Q81_TX_1024_TO_1518_PKT_LO     :
		case  Q81_TX_1519_TO_MAX_PKT         :
		case  Q81_TX_1519_TO_MAX_PKT_LO      :
		case  Q81_TX_UNDERSIZE_PKT           :
		case  Q81_TX_UNDERSIZE_PKT_LO        :
		case  Q81_TX_OVERSIZE_PKT            :
		case  Q81_TX_OVERSIZE_PKT_LO         :
		case  Q81_RX_HALF_FULL_DET           :
		case  Q81_TX_HALF_FULL_DET_LO        :
		case  Q81_RX_OVERFLOW_DET            :
		case  Q81_TX_OVERFLOW_DET_LO         :
		case  Q81_RX_HALF_FULL_MASK          :
		case  Q81_TX_HALF_FULL_MASK_LO       :
		case  Q81_RX_OVERFLOW_MASK           :
		case  Q81_TX_OVERFLOW_MASK_LO        :
		case  Q81_STAT_CNT_CTL               :
		case  Q81_AUX_RX_HALF_FULL_DET       :
		case  Q81_AUX_TX_HALF_FULL_DET       :
		case  Q81_AUX_RX_OVERFLOW_DET        :
		case  Q81_AUX_TX_OVERFLOW_DET        :
		case  Q81_AUX_RX_HALF_FULL_MASK      :
		case  Q81_AUX_TX_HALF_FULL_MASK      :
		case  Q81_AUX_RX_OVERFLOW_MASK       :
		case  Q81_AUX_TX_OVERFLOW_MASK       :
		case  Q81_RX_BYTES                   :
		case  Q81_RX_BYTES_LO                :
		case  Q81_RX_BYTES_OK                :
		case  Q81_RX_BYTES_OK_LO             :
		case  Q81_RX_PKTS                    :
		case  Q81_RX_PKTS_LO                 :
		case  Q81_RX_PKTS_OK                 :
		case  Q81_RX_PKTS_OK_LO              :
		case  Q81_RX_BCAST_PKTS              :
		case  Q81_RX_BCAST_PKTS_LO           :
		case  Q81_RX_MCAST_PKTS              :
		case  Q81_RX_MCAST_PKTS_LO           :
		case  Q81_RX_UCAST_PKTS              :
		case  Q81_RX_UCAST_PKTS_LO           :
		case  Q81_RX_UNDERSIZE_PKTS          :
		case  Q81_RX_UNDERSIZE_PKTS_LO       :
		case  Q81_RX_OVERSIZE_PKTS           :
		case  Q81_RX_OVERSIZE_PKTS_LO        :
		case  Q81_RX_JABBER_PKTS             :
		case  Q81_RX_JABBER_PKTS_LO          :
		case  Q81_RX_UNDERSIZE_FCERR_PKTS    :
		case  Q81_RX_UNDERSIZE_FCERR_PKTS_LO :
		case  Q81_RX_DROP_EVENTS             :
		case  Q81_RX_DROP_EVENTS_LO          :
		case  Q81_RX_FCERR_PKTS              :
		case  Q81_RX_FCERR_PKTS_LO           :
		case  Q81_RX_ALIGN_ERR               :
		case  Q81_RX_ALIGN_ERR_LO            :
		case  Q81_RX_SYMBOL_ERR              :
		case  Q81_RX_SYMBOL_ERR_LO           :
		case  Q81_RX_MAC_ERR                 :
		case  Q81_RX_MAC_ERR_LO              :
		case  Q81_RX_CTL_PKTS                :
		case  Q81_RX_CTL_PKTS_LO             :
		case  Q81_RX_PAUSE_PKTS              :
		case  Q81_RX_PAUSE_PKTS_LO           :
		case  Q81_RX_64_PKTS                 :
		case  Q81_RX_64_PKTS_LO              :
		case  Q81_RX_65_TO_127_PKTS          :
		case  Q81_RX_65_TO_127_PKTS_LO       :
		case  Q81_RX_128_255_PKTS            :
		case  Q81_RX_128_255_PKTS_LO         :
		case  Q81_RX_256_511_PKTS            :
		case  Q81_RX_256_511_PKTS_LO         :
		case  Q81_RX_512_TO_1023_PKTS        :
		case  Q81_RX_512_TO_1023_PKTS_LO     :
		case  Q81_RX_1024_TO_1518_PKTS       :
		case  Q81_RX_1024_TO_1518_PKTS_LO    :
		case  Q81_RX_1519_TO_MAX_PKTS        :
		case  Q81_RX_1519_TO_MAX_PKTS_LO     :
		case  Q81_RX_LEN_ERR_PKTS            :
		case  Q81_RX_LEN_ERR_PKTS_LO         :
		case  Q81_MDIO_TX_DATA               :
		case  Q81_MDIO_RX_DATA               :
		case  Q81_MDIO_CMD                   :
		case  Q81_MDIO_PHY_ADDR              :
		case  Q81_MDIO_PORT                  :
		case  Q81_MDIO_STATUS                :
		case  Q81_TX_CBFC_PAUSE_FRAMES0      :
		case  Q81_TX_CBFC_PAUSE_FRAMES0_LO   :
		case  Q81_TX_CBFC_PAUSE_FRAMES1      :
		case  Q81_TX_CBFC_PAUSE_FRAMES1_LO   :
		case  Q81_TX_CBFC_PAUSE_FRAMES2      :
		case  Q81_TX_CBFC_PAUSE_FRAMES2_LO   :
		case  Q81_TX_CBFC_PAUSE_FRAMES3      :
		case  Q81_TX_CBFC_PAUSE_FRAMES3_LO   :
		case  Q81_TX_CBFC_PAUSE_FRAMES4      :
		case  Q81_TX_CBFC_PAUSE_FRAMES4_LO   :
		case  Q81_TX_CBFC_PAUSE_FRAMES5      :
		case  Q81_TX_CBFC_PAUSE_FRAMES5_LO   :
		case  Q81_TX_CBFC_PAUSE_FRAMES6      :
		case  Q81_TX_CBFC_PAUSE_FRAMES6_LO   :
		case  Q81_TX_CBFC_PAUSE_FRAMES7      :
		case  Q81_TX_CBFC_PAUSE_FRAMES7_LO   :
		case  Q81_TX_FCOE_PKTS               :
		case  Q81_TX_FCOE_PKTS_LO            :
		case  Q81_TX_MGMT_PKTS               :
		case  Q81_TX_MGMT_PKTS_LO            :
		case  Q81_RX_CBFC_PAUSE_FRAMES0      :
		case  Q81_RX_CBFC_PAUSE_FRAMES0_LO   :
		case  Q81_RX_CBFC_PAUSE_FRAMES1      :
		case  Q81_RX_CBFC_PAUSE_FRAMES1_LO   :
		case  Q81_RX_CBFC_PAUSE_FRAMES2      :
		case  Q81_RX_CBFC_PAUSE_FRAMES2_LO   :
		case  Q81_RX_CBFC_PAUSE_FRAMES3      :
		case  Q81_RX_CBFC_PAUSE_FRAMES3_LO   :
		case  Q81_RX_CBFC_PAUSE_FRAMES4      :
		case  Q81_RX_CBFC_PAUSE_FRAMES4_LO   :
		case  Q81_RX_CBFC_PAUSE_FRAMES5      :
		case  Q81_RX_CBFC_PAUSE_FRAMES5_LO   :
		case  Q81_RX_CBFC_PAUSE_FRAMES6      :
		case  Q81_RX_CBFC_PAUSE_FRAMES6_LO   :
		case  Q81_RX_CBFC_PAUSE_FRAMES7      :
		case  Q81_RX_CBFC_PAUSE_FRAMES7_LO   :
		case  Q81_RX_FCOE_PKTS               :
		case  Q81_RX_FCOE_PKTS_LO            :
		case  Q81_RX_MGMT_PKTS               :
		case  Q81_RX_MGMT_PKTS_LO            :
		case  Q81_RX_NIC_FIFO_DROP           :
		case  Q81_RX_NIC_FIFO_DROP_LO        :
		case  Q81_RX_FCOE_FIFO_DROP          :
		case  Q81_RX_FCOE_FIFO_DROP_LO       :
		case  Q81_RX_MGMT_FIFO_DROP          :
		case  Q81_RX_MGMT_FIFO_DROP_LO       :
		case  Q81_RX_PKTS_PRIORITY0          :
		case  Q81_RX_PKTS_PRIORITY0_LO       :
		case  Q81_RX_PKTS_PRIORITY1          :
		case  Q81_RX_PKTS_PRIORITY1_LO       :
		case  Q81_RX_PKTS_PRIORITY2          :
		case  Q81_RX_PKTS_PRIORITY2_LO       :
		case  Q81_RX_PKTS_PRIORITY3          :
		case  Q81_RX_PKTS_PRIORITY3_LO       :
		case  Q81_RX_PKTS_PRIORITY4          :
		case  Q81_RX_PKTS_PRIORITY4_LO       :
		case  Q81_RX_PKTS_PRIORITY5          :
		case  Q81_RX_PKTS_PRIORITY5_LO       :
		case  Q81_RX_PKTS_PRIORITY6          :
		case  Q81_RX_PKTS_PRIORITY6_LO       :
		case  Q81_RX_PKTS_PRIORITY7          :
		case  Q81_RX_PKTS_PRIORITY7_LO       :
		case  Q81_RX_OCTETS_PRIORITY0        :
		case  Q81_RX_OCTETS_PRIORITY0_LO     :
		case  Q81_RX_OCTETS_PRIORITY1        :
		case  Q81_RX_OCTETS_PRIORITY1_LO     :
		case  Q81_RX_OCTETS_PRIORITY2        :
		case  Q81_RX_OCTETS_PRIORITY2_LO     :
		case  Q81_RX_OCTETS_PRIORITY3        :
		case  Q81_RX_OCTETS_PRIORITY3_LO     :
		case  Q81_RX_OCTETS_PRIORITY4        :
		case  Q81_RX_OCTETS_PRIORITY4_LO     :
		case  Q81_RX_OCTETS_PRIORITY5        :
		case  Q81_RX_OCTETS_PRIORITY5_LO     :
		case  Q81_RX_OCTETS_PRIORITY6        :
		case  Q81_RX_OCTETS_PRIORITY6_LO     :
		case  Q81_RX_OCTETS_PRIORITY7        :
		case  Q81_RX_OCTETS_PRIORITY7_LO     :
		case  Q81_TX_PKTS_PRIORITY0          :
		case  Q81_TX_PKTS_PRIORITY0_LO       :
		case  Q81_TX_PKTS_PRIORITY1          :
		case  Q81_TX_PKTS_PRIORITY1_LO       :
		case  Q81_TX_PKTS_PRIORITY2          :
		case  Q81_TX_PKTS_PRIORITY2_LO       :
		case  Q81_TX_PKTS_PRIORITY3          :
		case  Q81_TX_PKTS_PRIORITY3_LO       :
		case  Q81_TX_PKTS_PRIORITY4          :
		case  Q81_TX_PKTS_PRIORITY4_LO       :
		case  Q81_TX_PKTS_PRIORITY5          :
		case  Q81_TX_PKTS_PRIORITY5_LO       :
		case  Q81_TX_PKTS_PRIORITY6          :
		case  Q81_TX_PKTS_PRIORITY6_LO       :
		case  Q81_TX_PKTS_PRIORITY7          :
		case  Q81_TX_PKTS_PRIORITY7_LO       :
		case  Q81_TX_OCTETS_PRIORITY0        :
		case  Q81_TX_OCTETS_PRIORITY0_LO     :
		case  Q81_TX_OCTETS_PRIORITY1        :
		case  Q81_TX_OCTETS_PRIORITY1_LO     :
		case  Q81_TX_OCTETS_PRIORITY2        :
		case  Q81_TX_OCTETS_PRIORITY2_LO     :
		case  Q81_TX_OCTETS_PRIORITY3        :
		case  Q81_TX_OCTETS_PRIORITY3_LO     :
		case  Q81_TX_OCTETS_PRIORITY4        :
		case  Q81_TX_OCTETS_PRIORITY4_LO     :
		case  Q81_TX_OCTETS_PRIORITY5        :
		case  Q81_TX_OCTETS_PRIORITY5_LO     :
		case  Q81_TX_OCTETS_PRIORITY6        :
		case  Q81_TX_OCTETS_PRIORITY6_LO     :
		case  Q81_TX_OCTETS_PRIORITY7        :
		case  Q81_TX_OCTETS_PRIORITY7_LO     :
		case  Q81_RX_DISCARD_PRIORITY0       :
		case  Q81_RX_DISCARD_PRIORITY0_LO    :
		case  Q81_RX_DISCARD_PRIORITY1       :
		case  Q81_RX_DISCARD_PRIORITY1_LO    :
		case  Q81_RX_DISCARD_PRIORITY2       :
		case  Q81_RX_DISCARD_PRIORITY2_LO    :
		case  Q81_RX_DISCARD_PRIORITY3       :
		case  Q81_RX_DISCARD_PRIORITY3_LO    :
		case  Q81_RX_DISCARD_PRIORITY4       :
		case  Q81_RX_DISCARD_PRIORITY4_LO    :
		case  Q81_RX_DISCARD_PRIORITY5       :
		case  Q81_RX_DISCARD_PRIORITY5_LO    :
		case  Q81_RX_DISCARD_PRIORITY6       :
		case  Q81_RX_DISCARD_PRIORITY6_LO    :
		case  Q81_RX_DISCARD_PRIORITY7       :
		case  Q81_RX_DISCARD_PRIORITY7_LO    :

			if (o_func)
				ret = qls_rd_ofunc_xgmac_reg(ha,
						i, buf);
			else
				ret = qls_rd_xgmac_reg(ha, i, buf);

			if (ret)
				*buf = Q81_BAD_DATA;

			break;

		default:
			break;

		}
	}
	return 0;
}

static int
qls_get_mpi_regs(qla_host_t *ha, uint32_t *buf, uint32_t offset, uint32_t count)
{
	int i, ret = 0;

	for (i = 0; i < count; i++, buf++) {

		ret = qls_rd_mpi_reg(ha, (offset + i), buf);

		if (ret)
			return ret;
	}

	return (ret);
}

static int
qls_get_mpi_shadow_regs(qla_host_t *ha, uint32_t *buf)
{
	uint32_t	i;
	int		ret;

#define Q81_RISC_124 0x0000007c
#define Q81_RISC_127 0x0000007f
#define Q81_SHADOW_OFFSET 0xb0000000

	for (i = 0; i < Q81_MPI_CORE_SH_REGS_CNT; i++, buf++) {

		ret = qls_wr_mpi_reg(ha,
				(Q81_CTL_PROC_ADDR_RISC_INT_REG | Q81_RISC_124),
                                (Q81_SHADOW_OFFSET | i << 20));
		if (ret)
			goto exit_qls_get_mpi_shadow_regs;

		ret = qls_mpi_risc_rd_reg(ha,
				(Q81_CTL_PROC_ADDR_RISC_INT_REG | Q81_RISC_127),
				 buf);
		if (ret)
			goto exit_qls_get_mpi_shadow_regs;
	}

exit_qls_get_mpi_shadow_regs:
	return ret;
}

#define SYS_CLOCK (0x00)
#define PCI_CLOCK (0x80)
#define FC_CLOCK  (0x140)
#define XGM_CLOCK (0x180)

#define Q81_ADDRESS_REGISTER_ENABLE 0x00010000
#define Q81_UP                      0x00008000
#define Q81_MAX_MUX                 0x40
#define Q81_MAX_MODULES             0x1F

static uint32_t *
qls_get_probe(qla_host_t *ha, uint32_t clock, uint8_t *valid, uint32_t *buf)
{
	uint32_t module, mux_sel, probe, lo_val, hi_val;

	for (module = 0; module < Q81_MAX_MODULES; module ++) {

		if (valid[module]) {

			for (mux_sel = 0; mux_sel < Q81_MAX_MUX; mux_sel++) {

				probe = clock | Q81_ADDRESS_REGISTER_ENABLE |
						mux_sel | (module << 9);
				WRITE_REG32(ha, Q81_CTL_XG_PROBE_MUX_ADDR,\
					probe);

				lo_val = READ_REG32(ha,\
						Q81_CTL_XG_PROBE_MUX_DATA);

				if (mux_sel == 0) {
					*buf = probe;
					buf ++;
				}

				probe |= Q81_UP;

				WRITE_REG32(ha, Q81_CTL_XG_PROBE_MUX_ADDR,\
					probe);
				hi_val = READ_REG32(ha,\
						Q81_CTL_XG_PROBE_MUX_DATA);

				*buf = lo_val;
				buf++;
				*buf = hi_val;
				buf++;
			}
		}
	}

	return(buf);
}

static int
qls_get_probe_dump(qla_host_t *ha, uint32_t *buf)
{

	uint8_t sys_clock_valid_modules[0x20] = {
		1,   // 0x00
		1,   // 0x01
		1,   // 0x02
		0,   // 0x03
		1,   // 0x04
		1,   // 0x05
		1,   // 0x06
		1,   // 0x07
		1,   // 0x08
		1,   // 0x09
		1,   // 0x0A
		1,   // 0x0B
		1,   // 0x0C
		1,   // 0x0D
		1,   // 0x0E
		0,   // 0x0F
		1,   // 0x10
		1,   // 0x11
		1,   // 0x12
		1,   // 0x13
		0,   // 0x14
		0,   // 0x15
		0,   // 0x16
		0,   // 0x17
		0,   // 0x18
		0,   // 0x19
		0,   // 0x1A
		0,   // 0x1B
		0,   // 0x1C
		0,   // 0x1D
		0,   // 0x1E
		0    // 0x1F
	};


	uint8_t pci_clock_valid_modules[0x20] = {
		1,   // 0x00
		0,   // 0x01
		0,   // 0x02
		0,   // 0x03
		0,   // 0x04
		0,   // 0x05
		1,   // 0x06
		1,   // 0x07
		0,   // 0x08
		0,   // 0x09
		0,   // 0x0A
		0,   // 0x0B
		0,   // 0x0C
		0,   // 0x0D
		1,   // 0x0E
		0,   // 0x0F
		0,   // 0x10
		0,   // 0x11
		0,   // 0x12
		0,   // 0x13
		0,   // 0x14
		0,   // 0x15
		0,   // 0x16
		0,   // 0x17
		0,   // 0x18
		0,   // 0x19
		0,   // 0x1A
		0,   // 0x1B
		0,   // 0x1C
		0,   // 0x1D
		0,   // 0x1E
		0    // 0x1F
	};


	uint8_t xgm_clock_valid_modules[0x20] = {
		1,   // 0x00
		0,   // 0x01
		0,   // 0x02
		1,   // 0x03
		0,   // 0x04
		0,   // 0x05
		0,   // 0x06
		0,   // 0x07
		1,   // 0x08
		1,   // 0x09
		0,   // 0x0A
		0,   // 0x0B
		1,   // 0x0C
		1,   // 0x0D
		1,   // 0x0E
		0,   // 0x0F
		1,   // 0x10
		1,   // 0x11
		0,   // 0x12
		0,   // 0x13
		0,   // 0x14
		0,   // 0x15
		0,   // 0x16
		0,   // 0x17
		0,   // 0x18
		0,   // 0x19
		0,   // 0x1A
		0,   // 0x1B
		0,   // 0x1C
		0,   // 0x1D
		0,   // 0x1E
		0    // 0x1F
	};

	uint8_t fc_clock_valid_modules[0x20] = {
		1,   // 0x00
		0,   // 0x01
		0,   // 0x02
		0,   // 0x03
		0,   // 0x04
		0,   // 0x05
		0,   // 0x06
		0,   // 0x07
		0,   // 0x08
		0,   // 0x09
		0,   // 0x0A
		0,   // 0x0B
		1,   // 0x0C
		1,   // 0x0D
		0,   // 0x0E
		0,   // 0x0F
		0,   // 0x10
		0,   // 0x11
		0,   // 0x12
		0,   // 0x13
		0,   // 0x14
		0,   // 0x15
		0,   // 0x16
		0,   // 0x17
		0,   // 0x18
		0,   // 0x19
		0,   // 0x1A
		0,   // 0x1B
		0,   // 0x1C
		0,   // 0x1D
		0,   // 0x1E
		0    // 0x1F
	};

	qls_wr_mpi_reg(ha, 0x100e, 0x18a20000);

	buf = qls_get_probe(ha, SYS_CLOCK, sys_clock_valid_modules, buf);

	buf = qls_get_probe(ha, PCI_CLOCK, pci_clock_valid_modules, buf);

	buf = qls_get_probe(ha, XGM_CLOCK, xgm_clock_valid_modules, buf);

	buf = qls_get_probe(ha, FC_CLOCK, fc_clock_valid_modules, buf);

	return(0);
}

static void
qls_get_ridx_registers(qla_host_t *ha, uint32_t *buf)
{
	uint32_t type, idx, idx_max;
	uint32_t r_idx;
	uint32_t r_data;
	uint32_t val;

	for (type = 0; type < 4; type ++) {
		if (type < 2)
			idx_max = 8;
		else
			idx_max = 16;

		for (idx = 0; idx < idx_max; idx ++) {

			val = 0x04000000 | (type << 16) | (idx << 8);
			WRITE_REG32(ha, Q81_CTL_ROUTING_INDEX, val);

			r_idx = 0;
			while ((r_idx & 0x40000000) == 0)
				r_idx = READ_REG32(ha, Q81_CTL_ROUTING_INDEX);

			r_data = READ_REG32(ha, Q81_CTL_ROUTING_DATA);

			*buf = type;
			buf ++;
			*buf = idx;
			buf ++;
			*buf = r_idx;
			buf ++;
			*buf = r_data;
			buf ++;
		}
	}
}

static void
qls_get_mac_proto_regs(qla_host_t *ha, uint32_t* buf)
{

#define Q81_RS_AND_ADR 0x06000000
#define Q81_RS_ONLY    0x04000000
#define Q81_NUM_TYPES  10

	uint32_t result_index, result_data;
	uint32_t type;
	uint32_t index;
	uint32_t offset;
	uint32_t val;
	uint32_t initial_val;
	uint32_t max_index;
	uint32_t max_offset;

	for (type = 0; type < Q81_NUM_TYPES; type ++) {
		switch (type) {

		case 0: // CAM
			initial_val = Q81_RS_AND_ADR;
			max_index = 512;
			max_offset = 3;
			break;

		case 1: // Multicast MAC Address
			initial_val = Q81_RS_ONLY;
			max_index = 32;
			max_offset = 2;
			break;

		case 2: // VLAN filter mask
		case 3: // MC filter mask
			initial_val = Q81_RS_ONLY;
			max_index = 4096;
			max_offset = 1;
			break;

		case 4: // FC MAC addresses
			initial_val = Q81_RS_ONLY;
			max_index = 4;
			max_offset = 2;
			break;

		case 5: // Mgmt MAC addresses
			initial_val = Q81_RS_ONLY;
			max_index = 8;
			max_offset = 2;
			break;

		case 6: // Mgmt VLAN addresses
			initial_val = Q81_RS_ONLY;
			max_index = 16;
			max_offset = 1;
			break;

		case 7: // Mgmt IPv4 address
			initial_val = Q81_RS_ONLY;
			max_index = 4;
			max_offset = 1;
			break;

		case 8: // Mgmt IPv6 address
			initial_val = Q81_RS_ONLY;
			max_index = 4;
			max_offset = 4;
			break;

		case 9: // Mgmt TCP/UDP Dest port
			initial_val = Q81_RS_ONLY;
			max_index = 4;
			max_offset = 1;
			break;

		default:
			printf("Bad type!!! 0x%08x\n", type);
			max_index = 0;
			max_offset = 0;
			break;
		}

		for (index = 0; index < max_index; index ++) {

			for (offset = 0; offset < max_offset; offset ++) {

				val = initial_val | (type << 16) |
					(index << 4) | (offset);

				WRITE_REG32(ha, Q81_CTL_MAC_PROTO_ADDR_INDEX,\
					val);

				result_index = 0;

				while ((result_index & 0x40000000) == 0)
					result_index =
						READ_REG32(ha, \
						Q81_CTL_MAC_PROTO_ADDR_INDEX);

				result_data = READ_REG32(ha,\
						Q81_CTL_MAC_PROTO_ADDR_DATA);

				*buf = result_index;
				buf ++;

				*buf = result_data;
				buf ++;
			}
		}
	}
}

static int
qls_get_ets_regs(qla_host_t *ha, uint32_t *buf)
{
	int ret = 0;
	int i;

	for(i = 0; i < 8; i ++, buf ++) {
		WRITE_REG32(ha, Q81_CTL_NIC_ENH_TX_SCHD, \
			((i << 29) | 0x08000000));
		*buf = READ_REG32(ha, Q81_CTL_NIC_ENH_TX_SCHD);
	}

	for(i = 0; i < 2; i ++, buf ++) {
		WRITE_REG32(ha, Q81_CTL_CNA_ENH_TX_SCHD, \
			((i << 29) | 0x08000000));
		*buf = READ_REG32(ha, Q81_CTL_CNA_ENH_TX_SCHD);
	}

	return ret;
}

int
qls_mpi_core_dump(qla_host_t *ha)
{
	int ret;
	int i;
	uint32_t reg, reg_val;

	qls_mpi_coredump_t *mpi_dump = &ql_mpi_coredump;

	ret = qls_pause_mpi_risc(ha);
	if (ret) {
		printf("Failed RISC pause. Status = 0x%.08x\n",ret);
		return(-1);
	}

	memset(&(mpi_dump->mpi_global_header), 0,
			sizeof(qls_mpid_glbl_hdr_t));

	mpi_dump->mpi_global_header.cookie = Q81_MPID_COOKIE;
	mpi_dump->mpi_global_header.hdr_size =
		sizeof(qls_mpid_glbl_hdr_t);
	mpi_dump->mpi_global_header.img_size =
		sizeof(qls_mpi_coredump_t);

	memcpy(mpi_dump->mpi_global_header.id, "MPI Coredump",
		sizeof(mpi_dump->mpi_global_header.id));

	qls_mpid_seg_hdr(&mpi_dump->nic1_regs_seg_hdr,
		Q81_NIC1_CONTROL_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->nic1_regs)),
		"NIC1 Registers");

	qls_mpid_seg_hdr(&mpi_dump->nic2_regs_seg_hdr,
		Q81_NIC2_CONTROL_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->nic2_regs)),
		"NIC2 Registers");

	qls_mpid_seg_hdr(&mpi_dump->xgmac1_seg_hdr,
		Q81_NIC1_XGMAC_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->xgmac1)),
		"NIC1 XGMac Registers");

	qls_mpid_seg_hdr(&mpi_dump->xgmac2_seg_hdr,
		Q81_NIC2_XGMAC_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->xgmac2)),
		"NIC2 XGMac Registers");

	if (ha->pci_func & 1) {
		for (i = 0; i < 64; i++)
			mpi_dump->nic2_regs[i] =
				READ_REG32(ha, i * sizeof(uint32_t));

		for (i = 0; i < 64; i++)
			mpi_dump->nic1_regs[i] =
				qls_rd_ofunc_reg(ha,
					(i * sizeof(uint32_t)) / 4);

		qls_rd_xgmac_regs(ha, &mpi_dump->xgmac2[0], 0);
		qls_rd_xgmac_regs(ha, &mpi_dump->xgmac1[0], 1);
	} else {
		for (i = 0; i < 64; i++)
			mpi_dump->nic1_regs[i] =
				READ_REG32(ha, i * sizeof(uint32_t));

		for (i = 0; i < 64; i++)
			mpi_dump->nic2_regs[i] =
				qls_rd_ofunc_reg(ha,
					(i * sizeof(uint32_t)) / 4);

		qls_rd_xgmac_regs(ha, &mpi_dump->xgmac1[0], 0);
		qls_rd_xgmac_regs(ha, &mpi_dump->xgmac2[0], 1);
	}


	qls_mpid_seg_hdr(&mpi_dump->xaui1_an_hdr,
		Q81_XAUI1_AN_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes1_xaui_an)),
		"XAUI1 AN Registers");

	qls_mpid_seg_hdr(&mpi_dump->xaui1_hss_pcs_hdr,
		Q81_XAUI1_HSS_PCS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes1_xaui_hss_pcs)),
		"XAUI1 HSS PCS Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi1_an_hdr,
		Q81_XFI1_AN_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->serdes1_xfi_an)),
		"XFI1 AN Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi1_train_hdr,
		Q81_XFI1_TRAIN_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes1_xfi_train)),
		"XFI1 TRAIN Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi1_hss_pcs_hdr,
		Q81_XFI1_HSS_PCS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes1_xfi_hss_pcs)),
		"XFI1 HSS PCS Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi1_hss_tx_hdr,
		Q81_XFI1_HSS_TX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes1_xfi_hss_tx)),
		"XFI1 HSS TX Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi1_hss_rx_hdr,
		Q81_XFI1_HSS_RX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes1_xfi_hss_rx)),
		"XFI1 HSS RX Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi1_hss_pll_hdr,
		Q81_XFI1_HSS_PLL_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes1_xfi_hss_pll)),
		"XFI1 HSS PLL Registers");

	qls_mpid_seg_hdr(&mpi_dump->xaui2_an_hdr,
		Q81_XAUI2_AN_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes2_xaui_an)),
		"XAUI2 AN Registers");

	qls_mpid_seg_hdr(&mpi_dump->xaui2_hss_pcs_hdr,
		Q81_XAUI2_HSS_PCS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes2_xaui_hss_pcs)),
		"XAUI2 HSS PCS Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi2_an_hdr,
		Q81_XFI2_AN_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->serdes2_xfi_an)),
		"XFI2 AN Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi2_train_hdr,
		Q81_XFI2_TRAIN_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes2_xfi_train)),
		"XFI2 TRAIN Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi2_hss_pcs_hdr,
		Q81_XFI2_HSS_PCS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes2_xfi_hss_pcs)),
		"XFI2 HSS PCS Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi2_hss_tx_hdr,
		Q81_XFI2_HSS_TX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes2_xfi_hss_tx)),
		"XFI2 HSS TX Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi2_hss_rx_hdr,
		Q81_XFI2_HSS_RX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes2_xfi_hss_rx)),
		"XFI2 HSS RX Registers");

	qls_mpid_seg_hdr(&mpi_dump->xfi2_hss_pll_hdr,
		Q81_XFI2_HSS_PLL_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->serdes2_xfi_hss_pll)),
		"XFI2 HSS PLL Registers");

	qls_rd_serdes_regs(ha, mpi_dump);

	qls_mpid_seg_hdr(&mpi_dump->core_regs_seg_hdr,
		Q81_CORE_SEG_NUM,
		(sizeof(mpi_dump->core_regs_seg_hdr) +
		 sizeof(mpi_dump->mpi_core_regs) +
		 sizeof(mpi_dump->mpi_core_sh_regs)),
		"Core Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->mpi_core_regs[0],
			Q81_MPI_CORE_REGS_ADDR, Q81_MPI_CORE_REGS_CNT);

	ret = qls_get_mpi_shadow_regs(ha,
			&mpi_dump->mpi_core_sh_regs[0]);

	qls_mpid_seg_hdr(&mpi_dump->test_logic_regs_seg_hdr,
		Q81_TEST_LOGIC_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->test_logic_regs)),
		"Test Logic Regs");

	ret = qls_get_mpi_regs(ha, &mpi_dump->test_logic_regs[0],
                            Q81_TEST_REGS_ADDR, Q81_TEST_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->rmii_regs_seg_hdr,
		Q81_RMII_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->rmii_regs)),
		"RMII Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->rmii_regs[0],
                            Q81_RMII_REGS_ADDR, Q81_RMII_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->fcmac1_regs_seg_hdr,
		Q81_FCMAC1_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->fcmac1_regs)),
		"FCMAC1 Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->fcmac1_regs[0],
                            Q81_FCMAC1_REGS_ADDR, Q81_FCMAC_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->fcmac2_regs_seg_hdr,
		Q81_FCMAC2_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->fcmac2_regs)),
		"FCMAC2 Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->fcmac2_regs[0],
                            Q81_FCMAC2_REGS_ADDR, Q81_FCMAC_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->fc1_mbx_regs_seg_hdr,
		Q81_FC1_MBOX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->fc1_mbx_regs)),
		"FC1 MBox Regs");

	ret = qls_get_mpi_regs(ha, &mpi_dump->fc1_mbx_regs[0],
                            Q81_FC1_MBX_REGS_ADDR, Q81_FC_MBX_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->ide_regs_seg_hdr,
		Q81_IDE_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->ide_regs)),
		"IDE Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->ide_regs[0],
                            Q81_IDE_REGS_ADDR, Q81_IDE_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->nic1_mbx_regs_seg_hdr,
		Q81_NIC1_MBOX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->nic1_mbx_regs)),
		"NIC1 MBox Regs");

	ret = qls_get_mpi_regs(ha, &mpi_dump->nic1_mbx_regs[0],
                            Q81_NIC1_MBX_REGS_ADDR, Q81_NIC_MBX_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->smbus_regs_seg_hdr,
		Q81_SMBUS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->smbus_regs)),
		"SMBus Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->smbus_regs[0],
                            Q81_SMBUS_REGS_ADDR, Q81_SMBUS_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->fc2_mbx_regs_seg_hdr,
		Q81_FC2_MBOX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->fc2_mbx_regs)),
		"FC2 MBox Regs");

	ret = qls_get_mpi_regs(ha, &mpi_dump->fc2_mbx_regs[0],
                            Q81_FC2_MBX_REGS_ADDR, Q81_FC_MBX_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->nic2_mbx_regs_seg_hdr,
		Q81_NIC2_MBOX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->nic2_mbx_regs)),
		"NIC2 MBox Regs");

	ret = qls_get_mpi_regs(ha, &mpi_dump->nic2_mbx_regs[0],
                            Q81_NIC2_MBX_REGS_ADDR, Q81_NIC_MBX_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->i2c_regs_seg_hdr,
		Q81_I2C_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) +
			sizeof(mpi_dump->i2c_regs)),
		"I2C Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->i2c_regs[0],
                            Q81_I2C_REGS_ADDR, Q81_I2C_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->memc_regs_seg_hdr,
		Q81_MEMC_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->memc_regs)),
		"MEMC Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->memc_regs[0],
                            Q81_MEMC_REGS_ADDR, Q81_MEMC_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->pbus_regs_seg_hdr,
		Q81_PBUS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->pbus_regs)),
		"PBUS Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->pbus_regs[0],
                            Q81_PBUS_REGS_ADDR, Q81_PBUS_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->mde_regs_seg_hdr,
		Q81_MDE_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->mde_regs)),
		"MDE Registers");

	ret = qls_get_mpi_regs(ha, &mpi_dump->mde_regs[0],
                            Q81_MDE_REGS_ADDR, Q81_MDE_REGS_CNT);

	qls_mpid_seg_hdr(&mpi_dump->intr_states_seg_hdr,
		Q81_INTR_STATES_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->intr_states)),
		"INTR States");

	qls_get_intr_states(ha, &mpi_dump->intr_states[0]);

	qls_mpid_seg_hdr(&mpi_dump->probe_dump_seg_hdr,
		Q81_PROBE_DUMP_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->probe_dump)),
		"Probe Dump");

	qls_get_probe_dump(ha, &mpi_dump->probe_dump[0]);

	qls_mpid_seg_hdr(&mpi_dump->routing_reg_seg_hdr,
		Q81_ROUTING_INDEX_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->routing_regs)),
		"Routing Regs");

	qls_get_ridx_registers(ha, &mpi_dump->routing_regs[0]);

	qls_mpid_seg_hdr(&mpi_dump->mac_prot_reg_seg_hdr,
		Q81_MAC_PROTOCOL_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->mac_prot_regs)),
		"MAC Prot Regs");

	qls_get_mac_proto_regs(ha, &mpi_dump->mac_prot_regs[0]);

	qls_mpid_seg_hdr(&mpi_dump->ets_seg_hdr,
		Q81_ETS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->ets)),
		"ETS Registers");

	ret = qls_get_ets_regs(ha, &mpi_dump->ets[0]);

	qls_mpid_seg_hdr(&mpi_dump->sem_regs_seg_hdr,
		Q81_SEM_REGS_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->sem_regs)),
		"Sem Registers");

	for(i = 0; i < Q81_MAX_SEMAPHORE_FUNCTIONS ; i ++) {

		reg = Q81_CTL_PROC_ADDR_REG_BLOCK | (i << Q81_FUNCTION_SHIFT) |
				(Q81_CTL_SEMAPHORE >> 2);

		ret = qls_mpi_risc_rd_reg(ha, reg, &reg_val);
		mpi_dump->sem_regs[i] = reg_val;

		if (ret != 0)
			mpi_dump->sem_regs[i] = Q81_BAD_DATA;
	}

	ret = qls_unpause_mpi_risc(ha);
	if (ret)
		printf("Failed RISC unpause. Status = 0x%.08x\n",ret);

	ret = qls_mpi_reset(ha);
	if (ret)
		printf("Failed RISC reset. Status = 0x%.08x\n",ret);

	WRITE_REG32(ha, Q81_CTL_FUNC_SPECIFIC, 0x80008000);

	qls_mpid_seg_hdr(&mpi_dump->memc_ram_seg_hdr,
		Q81_MEMC_RAM_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->memc_ram)),
		"MEMC RAM");

	ret = qls_mbx_dump_risc_ram(ha, &mpi_dump->memc_ram[0],
			Q81_MEMC_RAM_ADDR, Q81_MEMC_RAM_CNT);
	if (ret)
		printf("Failed Dump of MEMC RAM. Status = 0x%.08x\n",ret);

	qls_mpid_seg_hdr(&mpi_dump->code_ram_seg_hdr,
		Q81_WCS_RAM_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->code_ram)),
		"WCS RAM");

	ret = qls_mbx_dump_risc_ram(ha, &mpi_dump->memc_ram[0],
			Q81_CODE_RAM_ADDR, Q81_CODE_RAM_CNT);
	if (ret)
		printf("Failed Dump of CODE RAM. Status = 0x%.08x\n",ret);

	qls_mpid_seg_hdr(&mpi_dump->wqc1_seg_hdr,
		Q81_WQC1_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->wqc1)),
		"WQC 1");

	qls_mpid_seg_hdr(&mpi_dump->wqc2_seg_hdr,
		Q81_WQC2_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->wqc2)),
		"WQC 2");

	qls_mpid_seg_hdr(&mpi_dump->cqc1_seg_hdr,
		Q81_CQC1_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->cqc1)),
		"CQC 1");

	qls_mpid_seg_hdr(&mpi_dump->cqc2_seg_hdr,
		Q81_CQC2_SEG_NUM,
		(sizeof(qls_mpid_seg_hdr_t) + sizeof(mpi_dump->cqc2)),
		"CQC 2");

	return 0;
}

