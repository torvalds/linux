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
 *
 * $FreeBSD$
 */
/*
 * File: qls_hw.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#ifndef _QLS_HW_H_
#define _QLS_HW_H_

#define Q8_MAX_NUM_MULTICAST_ADDRS	32
#define Q8_MAC_ADDR_LEN			6

#define BIT_0                   (0x1 << 0)
#define BIT_1                   (0x1 << 1)
#define BIT_2                   (0x1 << 2)
#define BIT_3                   (0x1 << 3)
#define BIT_4                   (0x1 << 4)
#define BIT_5                   (0x1 << 5)
#define BIT_6                   (0x1 << 6)
#define BIT_7                   (0x1 << 7)
#define BIT_8                   (0x1 << 8)
#define BIT_9                   (0x1 << 9)
#define BIT_10                  (0x1 << 10)
#define BIT_11                  (0x1 << 11)
#define BIT_12                  (0x1 << 12)
#define BIT_13                  (0x1 << 13)
#define BIT_14                  (0x1 << 14)
#define BIT_15                  (0x1 << 15)
#define BIT_16                  (0x1 << 16)
#define BIT_17                  (0x1 << 17)
#define BIT_18                  (0x1 << 18)
#define BIT_19                  (0x1 << 19)
#define BIT_20                  (0x1 << 20)
#define BIT_21                  (0x1 << 21)
#define BIT_22                  (0x1 << 22)
#define BIT_23                  (0x1 << 23)
#define BIT_24                  (0x1 << 24)
#define BIT_25                  (0x1 << 25)
#define BIT_11                  (0x1 << 11)
#define BIT_12                  (0x1 << 12)
#define BIT_13                  (0x1 << 13)
#define BIT_14                  (0x1 << 14)
#define BIT_15                  (0x1 << 15)
#define BIT_16                  (0x1 << 16)
#define BIT_17                  (0x1 << 17)
#define BIT_18                  (0x1 << 18)
#define BIT_19                  (0x1 << 19)
#define BIT_20                  (0x1 << 20)
#define BIT_21                  (0x1 << 21)
#define BIT_22                  (0x1 << 22)
#define BIT_23                  (0x1 << 23)
#define BIT_24                  (0x1 << 24)
#define BIT_25                  (0x1 << 25)
#define BIT_26                  (0x1 << 26)
#define BIT_27                  (0x1 << 27)
#define BIT_28                  (0x1 << 28)
#define BIT_29                  (0x1 << 29)
#define BIT_30                  (0x1 << 30)
#define BIT_31                  (0x1 << 31)


/*
 * Firmware Interface
 */

/*********************************************************************
 * Work Queue Register Map
 *********************************************************************/
#define Q81_WRKQ_INDEX_REG			0x00
#define		Q81_WRKQ_CONS_INDEX_MASK	0xFFFF0000
#define		Q81_WRKQ_PROD_INDEX_MASK	0x0000FFFF
#define	Q81_WRKQ_VALID_REG			0x04
#define		Q81_WRKQ_VALID_ONQ		BIT_25
#define		Q81_WRKQ_VALID_V		BIT_4

/*********************************************************************
 * Completion Queue Register Map
 *********************************************************************/
#define Q81_COMPQ_INDEX_REG			0x00
#define		Q81_COMPQ_PROD_INDEX_MASK	0xFFFF0000
#define		Q81_COMPQ_CONS_INDEX_MASK	0x0000FFFF
#define	Q81_COMPQ_VALID_REG			0x04
#define		Q81_COMPQ_VALID_V		BIT_4
#define Q81_LRGBQ_INDEX_REG			0x18
#define		Q81_LRGBQ_CONS_INDEX_MASK	0xFFFF0000
#define		Q81_LRGBQ_PROD_INDEX_MASK	0x0000FFFF
#define Q81_SMBQ_INDEX_REG			0x1C
#define		Q81_SMBQ_CONS_INDEX_MASK	0xFFFF0000
#define		Q81_SMBQ_PROD_INDEX_MASK	0x0000FFFF

/*********************************************************************
 * Control Register Definitions
 * (Access, Function Specific, Shared via Semaphore, Control by MPI FW)
 *********************************************************************/
#define Q81_CTL_PROC_ADDR		0x00 /* R/W  - Y - */
#define Q81_CTL_PROC_DATA		0x04 /* R/W  - Y - */
#define Q81_CTL_SYSTEM			0x08 /* MWR  - - - */
#define Q81_CTL_RESET			0x0C /* MWR  Y - - */
#define Q81_CTL_FUNC_SPECIFIC		0x10 /* MWR  Y - - */
#define Q81_CTL_HOST_CMD_STATUS		0x14 /* R/W  Y - - */
#define Q81_CTL_LED			0x18 /* R/W  Y - Y */
#define Q81_CTL_ICB_ACCESS_ADDR_LO	0x20 /* R/W  - Y - */
#define Q81_CTL_ICB_ACCESS_ADDR_HI	0x24 /* R/W  - Y - */
#define Q81_CTL_CONFIG			0x28 /* MWR  - - - */
#define Q81_CTL_STATUS			0x30 /* MWR  Y - - */
#define Q81_CTL_INTR_ENABLE		0x34 /* MWR  Y - - */
#define Q81_CTL_INTR_MASK		0x38 /* MWR  Y - - */
#define Q81_CTL_INTR_STATUS1		0x3C /* RO   Y - - */
#define Q81_CTL_INTR_STATUS2		0x40 /* RO   Y - - */
#define Q81_CTL_INTR_STATUS3		0x44 /* RO   Y - - */
#define Q81_CTL_INTR_STATUS4		0x48 /* RO   Y - - */
#define Q81_CTL_REV_ID			0x4C /* RO   - - - */
#define Q81_CTL_FATAL_ERR_STATUS	0x54 /* RO   Y - - */
#define Q81_CTL_COR_ECC_ERR_COUNTER	0x60 /* RO   Y - - */
#define Q81_CTL_SEMAPHORE		0x64 /* MWR  Y - - */
#define Q81_CTL_GPIO1			0x68 /* MWR  Y - - */
#define Q81_CTL_GPIO2			0x6C /* MWR  Y - - */
#define Q81_CTL_GPIO3			0x70 /* MWR  Y - - */
#define Q81_CTL_XGMAC_ADDR		0x78 /* R/W  Y Y - */
#define Q81_CTL_XGMAC_DATA		0x7C /* R/W  Y Y Y */
#define Q81_CTL_NIC_ENH_TX_SCHD		0x80 /* R/W  Y - Y */
#define Q81_CTL_CNA_ENH_TX_SCHD		0x84 /* R/W  Y - Y */
#define Q81_CTL_FLASH_ADDR		0x88 /* R/W  - Y - */
#define Q81_CTL_FLASH_DATA		0x8C /* R/W  - Y - */
#define Q81_CTL_STOP_CQ_PROCESSING	0x90 /* MWR  Y - - */
#define Q81_CTL_MAC_PROTO_ADDR_INDEX	0xA8 /* R/W  - Y - */
#define Q81_CTL_MAC_PROTO_ADDR_DATA	0xAC /* R/W  - Y - */
#define Q81_CTL_COS_DEF_CQ1		0xB0 /* R/W  Y - - */
#define Q81_CTL_COS_DEF_CQ2		0xB4 /* R/W  Y - - */
#define Q81_CTL_ETHERTYPE_SKIP_1	0xB8 /* R/W  Y - - */
#define Q81_CTL_ETHERTYPE_SKIP_2	0xBC /* R/W  Y - - */
#define Q81_CTL_SPLIT_HDR		0xC0 /* R/W  Y - - */
#define Q81_CTL_NIC_PAUSE_THRES		0xC8 /* R/W  Y - Y */
#define Q81_CTL_NIC_RCV_CONFIG		0xD4 /* MWR  Y - Y */
#define Q81_CTL_COS_TAGS_IN_NIC_FIFO	0xDC /* R/W  Y - Y */
#define Q81_CTL_MGMT_RCV_CONFIG		0xE0 /* MWR  Y - Y */
#define Q81_CTL_ROUTING_INDEX		0xE4 /* R/W  Y Y - */
#define Q81_CTL_ROUTING_DATA		0xE8 /* R/W  Y Y - */
#define Q81_CTL_XG_SERDES_ADDR		0xF0 /* R/W  Y Y Y */
#define Q81_CTL_XG_SERDES_DATA		0xF4 /* R/W  Y Y Y */
#define Q81_CTL_XG_PROBE_MUX_ADDR	0xF8 /* R/W  - Y - */
#define Q81_CTL_XG_PROBE_MUX_DATA	0xFC /* R/W  - Y - */


/*
 * Process Address Register (0x00)
 */
#define Q81_CTL_PROC_ADDR_RDY		BIT_31
#define Q81_CTL_PROC_ADDR_READ		BIT_30
#define Q81_CTL_PROC_ADDR_ERR		BIT_29
#define Q81_CTL_PROC_ADDR_MPI_RISC	(0x00 << 16)
#define Q81_CTL_PROC_ADDR_MDE		(0x01 << 16)
#define Q81_CTL_PROC_ADDR_REG_BLOCK	(0x02 << 16)
#define Q81_CTL_PROC_ADDR_RISC_INT_REG	(0x03 << 16)


/*
 * System Register (0x08)
 */
#define Q81_CTL_SYSTEM_MASK_SHIFT		16
#define Q81_CTL_SYSTEM_ENABLE_VQM_WR		BIT_5
#define Q81_CTL_SYSTEM_ENABLE_DWC		BIT_4
#define Q81_CTL_SYSTEM_ENABLE_DA_SINGLE_THRD	BIT_3
#define Q81_CTL_SYSTEM_ENABLE_MDC		BIT_2
#define Q81_CTL_SYSTEM_ENABLE_FAE		BIT_1
#define Q81_CTL_SYSTEM_ENABLE_EFE		BIT_0

/*
 * Reset Register (0x0C)
 */
#define Q81_CTL_RESET_MASK_SHIFT		16
#define Q81_CTL_RESET_FUNC			BIT_15
#define Q81_CTL_RESET_RR_SHIFT			1

/*
 * Function Specific Control Register (0x10)
 */
#define Q81_CTL_FUNC_SPECIFIC_MASK_SHIFT	16

#define Q81_CTL_FUNC_SPECIFIC_FE		BIT_15			
#define Q81_CTL_FUNC_SPECIFIC_STE		BIT_13			
#define Q81_CTL_FUNC_SPECIFIC_DSB		BIT_12			
#define Q81_CTL_FUNC_SPECIFIC_SH		BIT_11			

#define Q81_CTL_FUNC_SPECIFIC_VM_PGSIZE_MASK	(0x7 << 8)
#define Q81_CTL_FUNC_SPECIFIC_VM_PGSIZE_2K	(0x1 << 8)
#define Q81_CTL_FUNC_SPECIFIC_VM_PGSIZE_4K	(0x2 << 8)
#define Q81_CTL_FUNC_SPECIFIC_VM_PGSIZE_8K	(0x3 << 8)
#define Q81_CTL_FUNC_SPECIFIC_VM_PGSIZE_64K	(0x6 << 8)

#define Q81_CTL_FUNC_SPECIFIC_EPC_O		BIT_7			
#define Q81_CTL_FUNC_SPECIFIC_EPC_I		BIT_6
#define Q81_CTL_FUNC_SPECIFIC_EC		BIT_5
#define Q81_CTL_FUNC_SPECIFIC_DBL_DBRST		(0x00 << 3)
#define Q81_CTL_FUNC_SPECIFIC_DBL_MAX_PAYLDSZ	(0x01 << 3)
#define Q81_CTL_FUNC_SPECIFIC_DBL_MAX_RDBRSTSZ	(0x02 << 3)
#define Q81_CTL_FUNC_SPECIFIC_DBL_128		(0x03 << 3)
#define Q81_CTL_FUNC_SPECIFIC_DBRST_256		0x00			
#define Q81_CTL_FUNC_SPECIFIC_DBRST_512		0x01			
#define Q81_CTL_FUNC_SPECIFIC_DBRST_768		0x02			
#define Q81_CTL_FUNC_SPECIFIC_DBRST_1024	0x03			


/*
 * Host Command/Status Register (0x14)
 */
#define Q81_CTL_HCS_CMD_NOP			(0x00 << 28)
#define Q81_CTL_HCS_CMD_SET_RISC_RESET		(0x01 << 28)
#define Q81_CTL_HCS_CMD_CLR_RISC_RESET		(0x02 << 28)
#define Q81_CTL_HCS_CMD_SET_RISC_PAUSE		(0x03 << 28)
#define Q81_CTL_HCS_CMD_CLR_RISC_PAUSE		(0x04 << 28)
#define Q81_CTL_HCS_CMD_SET_HTR_INTR		(0x05 << 28)
#define Q81_CTL_HCS_CMD_CLR_HTR_INTR		(0x06 << 28)
#define Q81_CTL_HCS_CMD_SET_PARITY_EN		(0x07 << 28)
#define Q81_CTL_HCS_CMD_FORCE_BAD_PARITY	(0x08 << 28)
#define Q81_CTL_HCS_CMD_CLR_BAD_PARITY		(0x09 << 28)
#define Q81_CTL_HCS_CMD_CLR_RTH_INTR		(0x0A << 28)

#define Q81_CTL_HCS_CMD_PAR_SHIFT		22
#define Q81_CTL_HCS_RISC_PAUSED			BIT_10
#define Q81_CTL_HCS_HTR_INTR			BIT_9
#define Q81_CTL_HCS_RISC_RESET			BIT_8
#define Q81_CTL_HCS_ERR_STATUS_MASK		0x3F


/*
 * Configuration Register (0x28)
 */
#define Q81_CTL_CONFIG_MASK_SHIFT		16
#define Q81_CTL_CONFIG_Q_NUM_SHIFT		8
#define Q81_CTL_CONFIG_Q_NUM_MASK	(0x7F << Q81_CTL_CONFIG_Q_NUM_SHIFT)
#define Q81_CTL_CONFIG_DCQ			BIT_7
#define Q81_CTL_CONFIG_LCQ			BIT_6
#define Q81_CTL_CONFIG_LE			BIT_5
#define Q81_CTL_CONFIG_DR			BIT_3
#define Q81_CTL_CONFIG_LR			BIT_2
#define Q81_CTL_CONFIG_DRQ			BIT_1
#define Q81_CTL_CONFIG_LRQ			BIT_0


/*
 * Status Register (0x30)
 */
#define Q81_CTL_STATUS_MASK_SHIFT		16
#define Q81_CTL_STATUS_NFE			BIT_12
#define Q81_CTL_STATUS_F3E			BIT_11
#define Q81_CTL_STATUS_F2E			BIT_10
#define Q81_CTL_STATUS_F1E			BIT_9
#define Q81_CTL_STATUS_F0E			BIT_8
#define Q81_CTL_STATUS_FUNC_SHIFT		6
#define Q81_CTL_STATUS_PI1			BIT_5
#define Q81_CTL_STATUS_PI0			BIT_4
#define Q81_CTL_STATUS_PL1			BIT_3
#define Q81_CTL_STATUS_PL0			BIT_2
#define Q81_CTL_STATUS_PI			BIT_1
#define Q81_CTL_STATUS_FE			BIT_0

/*
 * Interrupt Enable Register (0x34)
 */
#define Q81_CTL_INTRE_MASK_SHIFT		16
#define Q81_CTL_INTRE_EN			BIT_15
#define Q81_CTL_INTRE_EI			BIT_14
#define Q81_CTL_INTRE_IHD			BIT_13
#define Q81_CTL_INTRE_RTYPE_MASK		(0x3 << 8)
#define Q81_CTL_INTRE_RTYPE_ENABLE		(0x1 << 8)
#define Q81_CTL_INTRE_RTYPE_DISABLE		(0x2 << 8)
#define Q81_CTL_INTRE_RTYPE_SETUP_TO_RD		(0x3 << 8)
#define Q81_CTL_INTRE_HOST_INTR_MASK		0x7F

/*
 * Interrupt Mask Register (0x38)
 */
#define Q81_CTL_INTRM_MASK_SHIFT		16
#define Q81_CTL_INTRM_MC			BIT_7
#define Q81_CTL_INTRM_LSC			BIT_6
#define Q81_CTL_INTRM_LH1			BIT_4
#define Q81_CTL_INTRM_HL1			BIT_3
#define Q81_CTL_INTRM_LH0			BIT_2
#define Q81_CTL_INTRM_HL0			BIT_1
#define Q81_CTL_INTRM_PI			BIT_0

/*
 * Interrupt Status 1 Register (0x3C)
 */
#define Q81_CTL_INTRS1_COMPQ(i)			(0x1 << i)

/*
 * Interrupt Status 2 Register (0x40)
 */
#define Q81_CTL_INTRS2_COMPQ(i)			(0x1 << i)

/*
 * Interrupt Status 3 Register (0x44)
 */
#define Q81_CTL_INTRS3_COMPQ(i)			(0x1 << i)

/*
 * Interrupt Status 4 Register (0x48)
 */
#define Q81_CTL_INTRS4_COMPQ(i)			(0x1 << i)

/*
 * Revision ID Register (0x4C)
 */
#define Q81_CTL_REV_ID_CHIP_REV_MASK		(0xF << 28)
#define Q81_CTL_REV_ID_XGMAC_RCV_MASK		(0xF << 16)
#define Q81_CTL_REV_ID_XGMAC_ROLL_MASK		(0xF << 8)
#define Q81_CTL_REV_ID_NIC_REV_MASK		(0xF << 4)
#define Q81_CTL_REV_ID_NIC_ROLL_MASK		(0xF << 0)

/*
 * Semaphore Register (0x64)
 */

#define Q81_CTL_SEM_MASK_PROC_ADDR_NIC_RCV	0xC0000000

#define Q81_CTL_SEM_MASK_RIDX_DATAREG		0x30000000

#define Q81_CTL_SEM_MASK_FLASH			0x03000000

#define Q81_CTL_SEM_MASK_MAC_SERDES		0x00C00000

#define Q81_CTL_SEM_MASK_ICB			0x00300000

#define Q81_CTL_SEM_MASK_XGMAC1			0x000C0000

#define Q81_CTL_SEM_MASK_XGMAC0			0x00030000

#define Q81_CTL_SEM_SET_PROC_ADDR_NIC_RCV	0x4000
#define Q81_CTL_SEM_SET_RIDX_DATAREG		0x1000
#define Q81_CTL_SEM_SET_FLASH			0x0100
#define Q81_CTL_SEM_SET_MAC_SERDES		0x0040
#define Q81_CTL_SEM_SET_ICB			0x0010
#define Q81_CTL_SEM_SET_XGMAC1			0x0004
#define Q81_CTL_SEM_SET_XGMAC0			0x0001


/*
 * Flash Address Register (0x88)
 */
#define Q81_CTL_FLASH_ADDR_RDY			BIT_31
#define Q81_CTL_FLASH_ADDR_R			BIT_30
#define Q81_CTL_FLASH_ADDR_ERR			BIT_29
#define Q81_CTL_FLASH_ADDR_MASK			0x7FFFFF

/*
 * Stop CQ Processing Register (0x90)
 */
#define Q81_CTL_STOP_CQ_MASK_SHIFT		16
#define Q81_CTL_STOP_CQ_EN			BIT_15
#define Q81_CTL_STOP_CQ_RQ_STARTQ		(0x1 << 8)
#define Q81_CTL_STOP_CQ_RQ_STOPQ		(0x2 << 8)
#define Q81_CTL_STOP_CQ_RQ_READ			(0x3 << 8)
#define Q81_CTL_STOP_CQ_MASK			0x7F

/*
 * MAC Protocol Address Index Register (0xA8)
 */
#define Q81_CTL_MAC_PROTO_AI_MW			BIT_31
#define Q81_CTL_MAC_PROTO_AI_MR			BIT_30
#define Q81_CTL_MAC_PROTO_AI_E			BIT_27
#define Q81_CTL_MAC_PROTO_AI_RS			BIT_26
#define Q81_CTL_MAC_PROTO_AI_ADR		BIT_25
#define Q81_CTL_MAC_PROTO_AI_TYPE_SHIFT		16
#define Q81_CTL_MAC_PROTO_AI_TYPE_MASK		0xF0000
#define Q81_CTL_MAC_PROTO_AI_IDX_SHIFT		4
#define Q81_CTL_MAC_PROTO_AI_IDX_MASK		0xFFF0
#define Q81_CTL_MAC_PROTO_AI_OFF_MASK		0xF

#define Q81_CTL_MAC_PROTO_AI_TYPE_CAM_MAC	(0 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_MCAST		(1 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_VLAN		(2 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_MCAST_FILTER	(3 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_MGMT_MAC	(5 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_MGMMT_VLAN	(6 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_MGMT_IPV4	(7 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_MGMT_IPV6	(8 << 16)
#define Q81_CTL_MAC_PROTO_AI_TYPE_MGMT_PORT	(9 << 16) /* TCP/UDP Port */

/*
 * CAM MAC offset 2 definitions
 */
#define Q81_CAM_MAC_OFF2_ROUTE_FC		0x00000000
#define Q81_CAM_MAC_OFF2_ROUTE_NIC		0x00000001
#define Q81_CAM_MAC_OFF2_FUNC_SHIFT		2
#define Q81_CAM_MAC_OFF2_RV			0x00000010
#define Q81_CAM_MAC_OFF2_CQID_SHIFT		5
#define Q81_CAM_MAC_OFF2_SH			0x00008000
#define Q81_CAM_MAC_OFF2_MHT			0x40000000
#define Q81_CAM_MAC_OFF2_VLD			0x80000000

/*
 * NIC Pause Threshold Register (0xC8)
 */
#define Q81_CTL_NIC_PAUSE_THRES_PAUSE_SHIFT	16
#define Q81_CTL_NIC_PAUSE_THRES_RESUME_SHIFT	0

/*
 * NIC Receive Configuration Register (0xD4)
 */
#define Q81_CTL_NIC_RCVC_MASK_SHIFT		16
#define Q81_CTL_NIC_RCVC_DCQ_SHIFT		8
#define Q81_CTL_NIC_RCVC_DCQ_MASK		0x7F00
#define Q81_CTL_NIC_RCVC_DTP			BIT_5
#define Q81_CTL_NIC_RCVC_R4T			BIT_4
#define Q81_CTL_NIC_RCVC_RV			BIT_3
#define Q81_CTL_NIC_RCVC_VLAN_ALL		(0x0 << 1)
#define Q81_CTL_NIC_RCVC_VLAN_ONLY		(0x1 << 1)
#define Q81_CTL_NIC_RCVC_VLAN_NON_VLAN		(0x2 << 1)
#define Q81_CTL_NIC_RCVC_VLAN_REJECT		(0x3 << 1)
#define Q81_CTL_NIC_RCVC_PPE			BIT_0


/*
 * Routing Index Register (0xE4)
 */
#define Q81_CTL_RI_MW				BIT_31
#define Q81_CTL_RI_MR				BIT_30
#define Q81_CTL_RI_E				BIT_27
#define Q81_CTL_RI_RS				BIT_26

#define Q81_CTL_RI_DST_RSS			(0x00 << 20)
#define Q81_CTL_RI_DST_CAMQ			(0x01 << 20)
#define Q81_CTL_RI_DST_COSQ			(0x02 << 20)
#define Q81_CTL_RI_DST_DFLTQ			(0x03 << 20)
#define Q81_CTL_RI_DST_DESTQ			(0x04 << 20)
#define Q81_CTL_RI_DST_DROP			(0x07 << 20)

#define Q81_CTL_RI_TYPE_RTMASK			(0x00 << 16)
#define Q81_CTL_RI_TYPE_RTINVMASK		(0x01 << 16)
#define Q81_CTL_RI_TYPE_NICQMASK		(0x02 << 16)
#define Q81_CTL_RI_TYPE_NICQINVMASK		(0x03 << 16)

/* these indices for the Routing Index Register are user defined */
#define Q81_CTL_RI_IDX_ALL_ERROR		(0x00 << 8)
#define Q81_CTL_RI_IDX_MAC_ERROR		(0x00 << 8)
#define Q81_CTL_RI_IDX_IPCSUM_ERROR		(0x01 << 8)
#define Q81_CTL_RI_IDX_TCPCSUM_ERROR		(0x02 << 8)
#define Q81_CTL_RI_IDX_BCAST			(0x03 << 8)
#define Q81_CTL_RI_IDX_MCAST_MATCH		(0x04 << 8)
#define Q81_CTL_RI_IDX_ALLMULTI			(0x05 << 8)
#define Q81_CTL_RI_IDX_RSS_MATCH		(0x08 << 8)
#define Q81_CTL_RI_IDX_RSS_IPV4			(0x08 << 8)
#define Q81_CTL_RI_IDX_RSS_IPV6			(0x09 << 8)
#define Q81_CTL_RI_IDX_RSS_TCPV4		(0x0A << 8)
#define Q81_CTL_RI_IDX_RSS_TCPV6		(0x0B << 8)
#define Q81_CTL_RI_IDX_CAM_HIT			(0x0C << 8)
#define Q81_CTL_RI_IDX_PROMISCUOUS		(0x0F << 8)

/* Routing Masks to be loaded into Routing Data Register */
#define Q81_CTL_RD_BCAST			BIT_0
#define Q81_CTL_RD_MCAST			BIT_1
#define Q81_CTL_RD_MCAST_MATCH			BIT_2
#define Q81_CTL_RD_MCAST_REG_MATCH		BIT_3
#define Q81_CTL_RD_MCAST_HASH_MATCH		BIT_4
#define Q81_CTL_RD_CAM_HIT			BIT_7
#define Q81_CTL_RD_CAM_BIT0			BIT_8
#define Q81_CTL_RD_CAM_BIT1			BIT_9
#define Q81_CTL_RD_VLAN_TAG_PRESENT		BIT_10
#define Q81_CTL_RD_VLAN_MATCH			BIT_11
#define Q81_CTL_RD_VLAN_FILTER_PASS		BIT_12
#define Q81_CTL_RD_SKIP_ETHERTYPE_1		BIT_13
#define Q81_CTL_RD_SKIP_ETHERTYPE_2		BIT_14
#define Q81_CTL_RD_BCAST_OR_MCAST_MATCH		BIT_15
#define Q81_CTL_RD_802_3_PKT			BIT_16
#define Q81_CTL_RD_LLDP_PKT			BIT_17
#define Q81_CTL_RD_TUNNELED_PKT			BIT_18
#define Q81_CTL_RD_ERROR_PKT			BIT_22
#define Q81_CTL_RD_VALID_PKT			BIT_23
#define Q81_CTL_RD_TCP_UDP_CSUM_ERR		BIT_24
#define Q81_CTL_RD_IPCSUM_ERR			BIT_25
#define Q81_CTL_RD_MAC_ERR			BIT_26
#define Q81_CTL_RD_RSS_TCP_IPV6			BIT_27
#define Q81_CTL_RD_RSS_TCP_IPV4			BIT_28
#define Q81_CTL_RD_RSS_IPV6			BIT_29
#define Q81_CTL_RD_RSS_IPV4			BIT_30
#define Q81_CTL_RD_RSS_MATCH			BIT_31


/*********************************************************************
 * Host Data Structures *
 *********************************************************************/

/*
 * Work Queue Initialization Control Block
 */

typedef struct _q81_wq_icb {

	uint16_t	length_v;
#define Q81_WQ_ICB_VALID			BIT_4

	uint8_t		pri;
#define Q81_WQ_ICB_PRI_SHIFT			1

	uint8_t		flags;
#define Q81_WQ_ICB_FLAGS_LO			BIT_7
#define Q81_WQ_ICB_FLAGS_LI			BIT_6
#define Q81_WQ_ICB_FLAGS_LB			BIT_5
#define Q81_WQ_ICB_FLAGS_LC			BIT_4

	uint16_t	wqcqid_rss;
#define Q81_WQ_ICB_RSS_V			BIT_15

	uint16_t	rsrvd;

	uint32_t	baddr_lo;
	uint32_t	baddr_hi;

	uint32_t	ci_addr_lo;
	uint32_t	ci_addr_hi;
} __packed q81_wq_icb_t;


/*
 * Completion Queue Initialization Control Block
 */

typedef struct _q81_cq_icb {
	uint8_t		msix_vector;
	uint16_t	rsrvd0;
	uint8_t		flags;
#define Q81_CQ_ICB_FLAGS_LC			BIT_7
#define Q81_CQ_ICB_FLAGS_LI			BIT_6
#define Q81_CQ_ICB_FLAGS_LL			BIT_5
#define Q81_CQ_ICB_FLAGS_LS			BIT_4
#define Q81_CQ_ICB_FLAGS_LV			BIT_3

	uint16_t	length_v;
#define Q81_CQ_ICB_VALID			BIT_4

	uint16_t	rsrvd1;

	uint32_t	cq_baddr_lo;
	uint32_t	cq_baddr_hi;

	uint32_t	cqi_addr_lo;
	uint32_t	cqi_addr_hi;

	uint16_t	pkt_idelay;
	uint16_t	idelay;

	uint32_t	lbq_baddr_lo;
	uint32_t	lbq_baddr_hi;
	uint16_t	lbq_bsize;
	uint16_t	lbq_length;

	uint32_t	sbq_baddr_lo;
	uint32_t	sbq_baddr_hi;
	uint16_t	sbq_bsize;
	uint16_t	sbq_length;
} __packed q81_cq_icb_t;

/*
 * RSS Initialization Control Block
 */
typedef struct _q81_rss_icb {
	uint16_t	flags_base_cq_num;
#define Q81_RSS_ICB_FLAGS_L4K		BIT_7
#define Q81_RSS_ICB_FLAGS_L6K		BIT_8
#define Q81_RSS_ICB_FLAGS_LI		BIT_9
#define Q81_RSS_ICB_FLAGS_LB		BIT_10
#define Q81_RSS_ICB_FLAGS_LM		BIT_11
#define Q81_RSS_ICB_FLAGS_RI4		BIT_12
#define Q81_RSS_ICB_FLAGS_RT4		BIT_13
#define Q81_RSS_ICB_FLAGS_RI6		BIT_14
#define Q81_RSS_ICB_FLAGS_RT6		BIT_15

	uint16_t	mask; /* bits 9-0 are valid */

#define Q81_RSS_ICB_NUM_INDTBL_ENTRIES	1024
	/* Indirection Table */
	uint8_t		cq_id[Q81_RSS_ICB_NUM_INDTBL_ENTRIES];

	/* Hash Keys */
	uint32_t	ipv6_rss_hash_key[10];
	uint32_t	ipv4_rss_hash_key[4];
} __packed q81_rss_icb_t;



/*
 * Transmit Buffer Descriptor
 */

typedef struct _q81_txb_desc {
	uint64_t	baddr;
	uint16_t	length;

	uint16_t	flags;
#define Q81_TXB_DESC_FLAGS_E	BIT_15
#define Q81_TXB_DESC_FLAGS_C	BIT_14

} __packed q81_txb_desc_t;


/*
 * Receive Buffer Descriptor
 */

typedef struct _q81_rxb_desc {
	uint32_t	baddr_lo;
#define Q81_RXB_DESC_BADDR_LO_S	BIT_1

	uint64_t	baddr;

	uint16_t	length;

	uint16_t	flags;
#define Q81_RXB_DESC_FLAGS_E	BIT_15
#define Q81_RXB_DESC_FLAGS_C	BIT_14

} __packed q81_rxb_desc_t;

/*
 * IOCB Types
 */

#define Q81_IOCB_TX_MAC		0x01
#define Q81_IOCB_TX_TSO		0x02
#define Q81_IOCB_RX		0x20
#define Q81_IOCB_MPI		0x21
#define Q81_IOCB_SYS		0x3F


/*
 * IOCB Definitions
 */

/*
 * MAC Tx Frame IOCB
 * Total Size of each IOCB Entry = 4 * 32 = 128 bytes
 */
#define MAX_TX_MAC_DESC		8

typedef struct _q81_tx_mac {

	uint8_t		opcode;

	uint16_t	flags;
#define Q81_TX_MAC_FLAGS_D		BIT_3
#define Q81_TX_MAC_FLAGS_I		BIT_1
#define Q81_TX_MAC_FLAGS_OI		BIT_0

	uint8_t		vlan_off;
#define Q81_TX_MAC_VLAN_OFF_SHIFT	3
#define Q81_TX_MAC_VLAN_OFF_V		BIT_2
#define Q81_TX_MAC_VLAN_OFF_DFP		BIT_1

	uint32_t	rsrvd1;
	uint32_t	rsrvd2;

	uint16_t	frame_length; /* only bits0-13 are valid */
	uint16_t	rsrvd3;

	uint32_t	tid_lo;
	uint32_t	tid_hi;

	uint32_t	rsrvd4;

	uint16_t	vlan_tci;
	uint16_t	rsrvd5;

	q81_txb_desc_t	txd[MAX_TX_MAC_DESC];
} __packed q81_tx_mac_t;
	
	
/*
 * MAC Tx Frame with TSO IOCB
 * Total Size of each IOCB Entry = 4 * 32 = 128 bytes
 */
typedef struct _q81_tx_tso {
	uint8_t		opcode;

	uint16_t	flags;
#define Q81_TX_TSO_FLAGS_OI		BIT_0
#define Q81_TX_TSO_FLAGS_I		BIT_1
#define Q81_TX_TSO_FLAGS_D		BIT_3
#define Q81_TX_TSO_FLAGS_IPV4		BIT_6
#define Q81_TX_TSO_FLAGS_IPV6		BIT_7
#define Q81_TX_TSO_FLAGS_LSO		BIT_13
#define Q81_TX_TSO_FLAGS_UC		BIT_14
#define Q81_TX_TSO_FLAGS_TC		BIT_15

	uint8_t		vlan_off;
#define Q81_TX_TSO_VLAN_OFF_SHIFT	3
#define Q81_TX_TSO_VLAN_OFF_V		BIT_2
#define Q81_TX_TSO_VLAN_OFF_DFP		BIT_1
#define Q81_TX_TSO_VLAN_OFF_IC		BIT_0

	uint32_t	rsrvd1;
	uint32_t	rsrvd2;

	uint32_t	length;
	uint32_t	tid_lo;
	uint32_t	tid_hi;

	uint16_t	phdr_length;

	uint16_t	phdr_offsets;
#define Q81_TX_TSO_PHDR_SHIFT		6

	uint16_t	vlan_tci;
	uint16_t	mss;

	q81_txb_desc_t	txd[MAX_TX_MAC_DESC];
} __packed q81_tx_tso_t;
	
typedef struct _q81_tx_cmd {
	uint8_t		bytes[128];
} __packed q81_tx_cmd_t;

/*
 * MAC TX Frame Completion
 * Total Size of each IOCB Entry = 4 * 16 = 64 bytes
 */

typedef struct _q81_tx_mac_comp {
	uint8_t		opcode;

	uint8_t		flags;
#define Q81_TX_MAC_COMP_FLAGS_OI	BIT_0
#define Q81_TX_MAC_COMP_FLAGS_I		BIT_1
#define Q81_TX_MAC_COMP_FLAGS_E		BIT_3
#define Q81_TX_MAC_COMP_FLAGS_S		BIT_4
#define Q81_TX_MAC_COMP_FLAGS_L		BIT_5
#define Q81_TX_MAC_COMP_FLAGS_P		BIT_6

	uint8_t		rsrvd0;

	uint8_t		err;
#define Q81_TX_MAC_COMP_ERR_B		BIT_7

	uint32_t	tid_lo;
	uint32_t	tid_hi;

	uint32_t	rsrvd1[13];
} __packed q81_tx_mac_comp_t;


/*
 * MAC TX Frame with LSO Completion
 * Total Size of each IOCB Entry = 4 * 16 = 64 bytes
 */

typedef struct _q81_tx_tso_comp {
	uint8_t		opcode;

	uint8_t		flags;
#define Q81_TX_TSO_COMP_FLAGS_OI	BIT_0
#define Q81_TX_TSO_COMP_FLAGS_I		BIT_1
#define Q81_TX_TSO_COMP_FLAGS_E		BIT_3
#define Q81_TX_TSO_COMP_FLAGS_S		BIT_4
#define Q81_TX_TSO_COMP_FLAGS_P		BIT_6

	uint8_t		rsrvd0;

	uint8_t		err;
#define Q81_TX_TSO_COMP_ERR_B		BIT_7

	uint32_t	tid_lo;
	uint32_t	tid_hi;

	uint32_t	rsrvd1[13];
} __packed q81_tx_tso_comp_t;


/*
 * SYS - Chip Event Notification Completion
 * Total Size of each IOCB Entry = 4 * 16 = 64 bytes
 */

typedef struct _q81_sys_comp {
	uint8_t		opcode;

	uint8_t		flags;
#define Q81_SYS_COMP_FLAGS_OI		BIT_0
#define Q81_SYS_COMP_FLAGS_I		BIT_1

	uint8_t		etype;
#define Q81_SYS_COMPE_LINK_UP		0x00
#define Q81_SYS_COMPE_LINK_DOWN		0x01
#define Q81_SYS_COMPE_MULTI_CAM_LOOKUP	0x06
#define Q81_SYS_COMPE_SOFT_ECC		0x07
#define Q81_SYS_COMPE_MPI_FATAL_ERROR	0x08
#define Q81_SYS_COMPE_MAC_INTR		0x09
#define Q81_SYS_COMPE_GPI0_HTOL		0x10
#define Q81_SYS_COMPE_GPI0_LTOH		0x20
#define Q81_SYS_COMPE_GPI1_HTOL		0x11
#define Q81_SYS_COMPE_GPI1_LTOH		0x21

	uint8_t		q_id; /* only bits 0-6 are valid */

	uint32_t	rsrvd1[15];
} __packed q81_sys_comp_t;



/*
 * Mac Rx Packet Completion
 * Total Size of each IOCB Entry = 4 * 16 = 64 bytes
 */

typedef struct _q81_rx {
	uint8_t		opcode;

	uint8_t		flags0;
#define Q81_RX_FLAGS0_OI		BIT_0
#define Q81_RX_FLAGS0_I			BIT_1
#define Q81_RX_FLAGS0_TE		BIT_2
#define Q81_RX_FLAGS0_NU		BIT_3
#define Q81_RX_FLAGS0_IE		BIT_4

#define Q81_RX_FLAGS0_MCAST_MASK	(0x03 << 5)
#define Q81_RX_FLAGS0_MCAST_NONE	(0x00 << 5)
#define Q81_RX_FLAGS0_MCAST_HASH_MATCH	(0x01 << 5)
#define Q81_RX_FLAGS0_MCAST_REG_MATCH	(0x02 << 5)
#define Q81_RX_FLAGS0_MCAST_PROMISC	(0x03 << 5)

#define Q81_RX_FLAGS0_B			BIT_7

	uint16_t	flags1;
#define Q81_RX_FLAGS1_P			BIT_0
#define Q81_RX_FLAGS1_V			BIT_1

#define Q81_RX_FLAGS1_ERR_NONE		(0x00 << 2)
#define Q81_RX_FLAGS1_ERR_CODE		(0x01 << 2)
#define Q81_RX_FLAGS1_ERR_OSIZE		(0x02 << 2)
#define Q81_RX_FLAGS1_ERR_USIZE		(0x04 << 2)
#define Q81_RX_FLAGS1_ERR_PREAMBLE	(0x05 << 2)
#define Q81_RX_FLAGS1_ERR_FRAMELENGTH	(0x06 << 2)
#define Q81_RX_FLAGS1_ERR_CRC		(0x07 << 2)
#define Q81_RX_FLAGS1_ERR_MASK		(0x07 << 2)

#define Q81_RX_FLAGS1_U			BIT_5
#define Q81_RX_FLAGS1_T			BIT_6
#define Q81_RX_FLAGS1_FO		BIT_7
#define Q81_RX_FLAGS1_RSS_NO_MATCH	(0x00 << 8)
#define Q81_RX_FLAGS1_RSS_IPV4_MATCH	(0x04 << 8)
#define Q81_RX_FLAGS1_RSS_IPV6_MATCH	(0x02 << 8)
#define Q81_RX_FLAGS1_RSS_TCPIPV4_MATCH	(0x05 << 8)
#define Q81_RX_FLAGS1_RSS_TCPIPV4_MATCH	(0x05 << 8)
#define Q81_RX_FLAGS1_RSS_MATCH_MASK	(0x07 << 8)
#define Q81_RX_FLAGS1_V4		BIT_11
#define Q81_RX_FLAGS1_V6		BIT_12
#define Q81_RX_FLAGS1_IH		BIT_13
#define Q81_RX_FLAGS1_DS		BIT_14
#define Q81_RX_FLAGS1_DL		BIT_15

	uint32_t	length;
	uint64_t	b_paddr;

	uint32_t	rss;
	uint16_t	vlan_tag;
	uint16_t	rsrvd;
	uint32_t	rsrvd1;
	uint32_t	flags2;
#define Q81_RX_FLAGS2_HV		BIT_13
#define Q81_RX_FLAGS2_HS		BIT_14
#define Q81_RX_FLAGS2_HL		BIT_15

	uint32_t	hdr_length;
	uint32_t	hdr_baddr_lo;
	uint32_t	hdr_baddr_hi;

} __packed q81_rx_t;

typedef struct _q81_cq_e {
	uint8_t		opcode;
	uint8_t		bytes[63];
} __packed q81_cq_e_t;

typedef struct _q81_bq_addr_e {
	uint32_t	addr_lo;
	uint32_t	addr_hi;
} __packed q81_bq_addr_e_t;


/*
 * Macros for reading and writing registers
 */

#if defined(__i386__) || defined(__amd64__)
#define Q8_MB()    __asm volatile("mfence" ::: "memory")
#define Q8_WMB()   __asm volatile("sfence" ::: "memory")
#define Q8_RMB()   __asm volatile("lfence" ::: "memory")
#else
#define Q8_MB()
#define Q8_WMB()
#define Q8_RMB()
#endif

#define READ_REG32(ha, reg) bus_read_4((ha->pci_reg), reg)
#define READ_REG64(ha, reg) bus_read_8((ha->pci_reg), reg)

#define WRITE_REG32_ONLY(ha, reg, val) bus_write_4((ha->pci_reg), reg, val)

#define WRITE_REG32(ha, reg, val) bus_write_4((ha->pci_reg), reg, val)

#define Q81_CTL_INTRE_MASK_VALUE \
	(((Q81_CTL_INTRE_RTYPE_MASK | Q81_CTL_INTRE_HOST_INTR_MASK) << \
		Q81_CTL_INTRE_MASK_SHIFT) | Q81_CTL_INTRE_RTYPE_ENABLE)

#define Q81_ENABLE_INTR(ha, idx) \
	WRITE_REG32(ha, Q81_CTL_INTR_ENABLE, (Q81_CTL_INTRE_MASK_VALUE | idx))

#define Q81_CTL_INTRD_MASK_VALUE \
	(((Q81_CTL_INTRE_RTYPE_MASK | Q81_CTL_INTRE_HOST_INTR_MASK) << \
		Q81_CTL_INTRE_MASK_SHIFT) | Q81_CTL_INTRE_RTYPE_DISABLE)

#define Q81_DISABLE_INTR(ha, idx) \
	WRITE_REG32(ha, Q81_CTL_INTR_ENABLE, (Q81_CTL_INTRD_MASK_VALUE | idx))

#define Q81_WR_WQ_PROD_IDX(wq_idx, idx) bus_write_4((ha->pci_reg1),\
		(ha->tx_ring[wq_idx].wq_db_offset + Q81_WRKQ_INDEX_REG), idx)

#define Q81_RD_WQ_IDX(wq_idx) bus_read_4((ha->pci_reg1),\
		(ha->tx_ring[wq_idx].wq_db_offset + Q81_WRKQ_INDEX_REG))


#define Q81_SET_WQ_VALID(wq_idx) bus_write_4((ha->pci_reg1),\
		(ha->tx_ring[wq_idx].wq_db_offset + Q81_WRKQ_VALID_REG),\
			Q81_COMPQ_VALID_V)

#define Q81_SET_WQ_INVALID(wq_idx) bus_write_4((ha->pci_reg1),\
		(ha->tx_ring[wq_idx].wq_db_offset + Q81_WRKQ_VALID_REG),\
			(~Q81_COMPQ_VALID_V))

#define Q81_WR_CQ_CONS_IDX(cq_idx, idx) bus_write_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_COMPQ_INDEX_REG), idx)

#define Q81_RD_CQ_IDX(cq_idx) bus_read_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_COMPQ_INDEX_REG))

#define Q81_SET_CQ_VALID(cq_idx) bus_write_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_COMPQ_VALID_REG),\
			Q81_COMPQ_VALID_V)

#define Q81_SET_CQ_INVALID(cq_idx) bus_write_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_COMPQ_VALID_REG),\
			~Q81_COMPQ_VALID_V)

#define Q81_WR_LBQ_PROD_IDX(cq_idx, idx) bus_write_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_LRGBQ_INDEX_REG), idx)

#define Q81_RD_LBQ_IDX(cq_idx) bus_read_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_LRGBQ_INDEX_REG))

#define Q81_WR_SBQ_PROD_IDX(cq_idx, idx) bus_write_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_SMBQ_INDEX_REG), idx)

#define Q81_RD_SBQ_IDX(cq_idx) bus_read_4((ha->pci_reg1),\
		(ha->rx_ring[cq_idx].cq_db_offset + Q81_SMBQ_INDEX_REG))


/*
 * Flash Related
 */

#define Q81_F0_FLASH_OFFSET	0x140200
#define Q81_F1_FLASH_OFFSET	0x140600
#define Q81_FLASH_ID		"8000"

typedef struct _q81_flash {

	uint8_t		id[4]; /* equal to "8000" */

	uint16_t	version;
	uint16_t	size;
	uint16_t	csum;
	uint16_t	rsrvd0;
	uint16_t	total_size;
	uint16_t	nentries;

	uint8_t		dtype0;
	uint8_t		dsize0;
	uint8_t		mac_addr0[6];

	uint8_t		dtype1;
	uint8_t		dsize1;
	uint8_t		mac_addr1[6];

	uint8_t		dtype2;
	uint8_t		dsize2;
	uint16_t	vlan_id;

	uint8_t		dtype3;
	uint8_t		dsize3;
	uint16_t	last;

	uint8_t		rsrvd1[464];

	uint16_t	subsys_vid;
	uint16_t	subsys_did;

	uint8_t		rsrvd2[4];
} __packed q81_flash_t;


/*
 * MPI Related 
 */

#define Q81_NUM_MBX_REGISTERS	16
#define Q81_NUM_AEN_REGISTERS	9

#define Q81_FUNC0_MBX_IN_REG0	0x1180
#define Q81_FUNC0_MBX_OUT_REG0	0x1190

#define Q81_FUNC1_MBX_IN_REG0	0x1280
#define Q81_FUNC1_MBX_OUT_REG0	0x1290

#define Q81_MBX_NOP		0x0000
#define Q81_MBX_EXEC_FW		0x0002
#define Q81_MBX_REG_TEST	0x0006
#define Q81_MBX_VERIFY_CHKSUM	0x0007
#define Q81_MBX_ABOUT_FW	0x0008
#define Q81_MBX_RISC_MEMCPY	0x000A
#define Q81_MBX_LOAD_RISC_RAM	0x000B
#define Q81_MBX_DUMP_RISC_RAM	0x000C
#define Q81_MBX_WR_RAM_WORD	0x000D
#define Q81_MBX_INIT_RISC_RAM	0x000E
#define Q81_MBX_RD_RAM_WORD	0x000F
#define Q81_MBX_STOP_FW		0x0014
#define Q81_MBX_GEN_SYS_ERR	0x002A
#define Q81_MBX_WR_SFP_PLUS	0x0030
#define Q81_MBX_RD_SFP_PLUS	0x0031
#define Q81_MBX_INIT_FW		0x0060
#define Q81_MBX_GET_IFCB	0x0061
#define Q81_MBX_GET_FW_STATE	0x0069
#define Q81_MBX_IDC_REQ		0x0100
#define Q81_MBX_IDC_ACK		0x0101
#define Q81_MBX_IDC_TIME_EXTEND	0x0102
#define Q81_MBX_WOL_MODE	0x0110
#define Q81_MBX_SET_WOL_FILTER	0x0111
#define Q81_MBX_CLR_WOL_FILTER	0x0112
#define Q81_MBX_SET_WOL_MAGIC	0x0113
#define Q81_MBX_WOL_MODE_IMM	0x0115
#define Q81_MBX_PORT_RESET	0x0120
#define Q81_MBX_SET_PORT_CFG	0x0122
#define Q81_MBX_GET_PORT_CFG	0x0123
#define Q81_MBX_GET_LNK_STATUS	0x0124
#define Q81_MBX_SET_LED_CFG	0x0125
#define Q81_MBX_GET_LED_CFG	0x0126
#define Q81_MBX_SET_DCBX_CTLB	0x0130
#define Q81_MBX_GET_DCBX_CTLB	0x0131
#define Q81_MBX_GET_DCBX_TLV	0x0132
#define Q81_MBX_DIAG_CMDS	0x0150
#define Q81_MBX_SET_MGMT_CTL	0x0160
#define		Q81_MBX_SET_MGMT_CTL_STOP	0x01
#define		Q81_MBX_SET_MGMT_CTL_RESUME	0x02
#define Q81_MBX_GET_MGMT_CTL	0x0161
#define		Q81_MBX_GET_MGMT_CTL_MASK	~0x3
#define		Q81_MBX_GET_MGMT_CTL_FIFO_EMPTY	0x02
#define		Q81_MBX_GET_MGMT_CTL_SET_MGMT	0x01

#define Q81_MBX_CMD_COMPLETE	0x4000
#define Q81_MBX_CMD_INVALID	0x4001
#define Q81_MBX_CMD_TEST_FAILED	0x4003
#define Q81_MBX_CMD_ERROR	0x4005
#define Q81_MBX_CMD_PARAM_ERROR	0x4006

#endif /* #ifndef _QLS_HW_H_ */
