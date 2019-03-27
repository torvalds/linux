/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/* \file cn23xx_pf_regs.h
 * \brief Host Driver: Register Address and Register Mask values for
 * CN23XX devices.
 */

#ifndef __CN23XX_PF_REGS_H__
#define __CN23XX_PF_REGS_H__

#define LIO_CN23XX_CFG_PCIE_DEVCTL		0x78
#define LIO_CN23XX_CFG_PCIE_UNCORRECT_ERR_MASK	0x108
#define LIO_CN23XX_CFG_PCIE_CORRECT_ERR_STATUS	0x110
#define LIO_CN23XX_CFG_PCIE_DEVCTL_MASK		0x00040000

#define LIO_CN23XX_PCIE_SRIOV_FDL		0x188
#define LIO_CN23XX_PCIE_SRIOV_FDL_BIT_POS	0x10
#define LIO_CN23XX_PCIE_SRIOV_FDL_MASK		0xFF

/* ##############  BAR0 Registers ################ */

#define LIO_CN23XX_SLI_CTL_PORT_START		0x286E0
#define LIO_CN23XX_PORT_OFFSET			0x10

#define LIO_CN23XX_SLI_CTL_PORT(p)			\
		(LIO_CN23XX_SLI_CTL_PORT_START +	\
		 ((p) * LIO_CN23XX_PORT_OFFSET))

/* 2 scatch registers (64-bit)  */
#define LIO_CN23XX_SLI_WINDOW_CTL		0x282E0
#define LIO_CN23XX_SLI_SCRATCH1			0x283C0
#define LIO_CN23XX_SLI_SCRATCH2			0x283D0
#define LIO_CN23XX_SLI_WINDOW_CTL_DEFAULT	0x200000ULL

/* 1 registers (64-bit)  - SLI_CTL_STATUS */
#define LIO_CN23XX_SLI_CTL_STATUS		0x28570

/*
 * SLI Packet Input Jabber Register (64 bit register)
 * <31:0> for Byte count for limiting sizes of packet sizes
 * that are allowed for sli packet inbound packets.
 * the default value is 0xFA00(=64000).
 */
#define LIO_CN23XX_SLI_PKT_IN_JABBER	0x29170

#define LIO_CN23XX_SLI_WIN_WR_ADDR_LO	0x20000
#define LIO_CN23XX_SLI_WIN_WR_ADDR64	LIO_CN23XX_SLI_WIN_WR_ADDR_LO

#define LIO_CN23XX_SLI_WIN_RD_ADDR_LO	0x20010
#define LIO_CN23XX_SLI_WIN_RD_ADDR_HI	0x20014
#define LIO_CN23XX_SLI_WIN_RD_ADDR64	LIO_CN23XX_SLI_WIN_RD_ADDR_LO

#define LIO_CN23XX_SLI_WIN_WR_DATA_LO	0x20020
#define LIO_CN23XX_SLI_WIN_WR_DATA_HI	0x20024
#define LIO_CN23XX_SLI_WIN_WR_DATA64	LIO_CN23XX_SLI_WIN_WR_DATA_LO

#define LIO_CN23XX_SLI_WIN_RD_DATA_LO	0x20040
#define LIO_CN23XX_SLI_WIN_RD_DATA_HI	0x20044
#define LIO_CN23XX_SLI_WIN_RD_DATA64	LIO_CN23XX_SLI_WIN_RD_DATA_LO

#define LIO_CN23XX_SLI_WIN_WR_MASK_REG	0x20030
#define LIO_CN23XX_SLI_MAC_CREDIT_CNT	0x23D70

/*
 * 4 registers (64-bit) for mapping IOQs to MACs(PEMs)-
 * SLI_PKT_MAC(0..3)_PF(0..1)_RINFO
 */
#define LIO_CN23XX_SLI_PKT_MAC_RINFO_START64	0x29030

/*1 register (64-bit) to determine whether IOQs are in reset. */
#define LIO_CN23XX_SLI_PKT_IOQ_RING_RST		0x291E0

/* Each Input Queue register is at a 16-byte Offset in BAR0 */
#define LIO_CN23XX_IQ_OFFSET			0x20000

#define LIO_CN23XX_MAC_RINFO_OFFSET		0x20
#define LIO_CN23XX_PF_RINFO_OFFSET		0x10

#define LIO_CN23XX_SLI_PKT_MAC_RINFO64(mac, pf)			\
		(LIO_CN23XX_SLI_PKT_MAC_RINFO_START64 +		\
		 ((mac) * LIO_CN23XX_MAC_RINFO_OFFSET) +	\
		 ((pf) * LIO_CN23XX_PF_RINFO_OFFSET))

/* mask for total rings, setting TRS to base */
#define LIO_CN23XX_PKT_MAC_CTL_RINFO_TRS	BIT_ULL(16)

/* Starting bit of the TRS field in LIO_CN23XX_SLI_PKT_MAC_RINFO64 register */
#define LIO_CN23XX_PKT_MAC_CTL_RINFO_TRS_BIT_POS	16

/*###################### REQUEST QUEUE #########################*/

/* 64 registers for Input Queue Instr Count - SLI_PKT_IN_DONE0_CNTS */
#define LIO_CN23XX_SLI_PKT_IN_DONE_CNTS_START64	0x10040

/* 64 registers for Input Queues Start Addr - SLI_PKT0_INSTR_BADDR */
#define LIO_CN23XX_SLI_PKT_INSTR_BADDR_START64	0x10010

/* 64 registers for Input Doorbell - SLI_PKT0_INSTR_BAOFF_DBELL */
#define LIO_CN23XX_SLI_PKT_INSTR_BADDR_DBELL_START	0x10020

/* 64 registers for Input Queue size - SLI_PKT0_INSTR_FIFO_RSIZE */
#define LIO_CN23XX_SLI_PKT_INSTR_FIFO_RSIZE_START	0x10030

/*
 * 64 registers (64-bit) - ES, RO, NS, Arbitration for Input Queue Data &
 * gather list fetches. SLI_PKT(0..63)_INPUT_CONTROL.
 */
#define LIO_CN23XX_SLI_PKT_INPUT_CONTROL_START64	0x10000

/*------- Request Queue Macros ---------*/
#define LIO_CN23XX_SLI_IQ_PKT_CONTROL64(iq)				\
		(LIO_CN23XX_SLI_PKT_INPUT_CONTROL_START64 +		\
		 ((iq) * LIO_CN23XX_IQ_OFFSET))

#define LIO_CN23XX_SLI_IQ_BASE_ADDR64(iq)				\
		(LIO_CN23XX_SLI_PKT_INSTR_BADDR_START64 +		\
		 ((iq) * LIO_CN23XX_IQ_OFFSET))

#define LIO_CN23XX_SLI_IQ_SIZE(iq)					\
		(LIO_CN23XX_SLI_PKT_INSTR_FIFO_RSIZE_START +		\
		 ((iq) * LIO_CN23XX_IQ_OFFSET))

#define LIO_CN23XX_SLI_IQ_DOORBELL(iq)					\
		(LIO_CN23XX_SLI_PKT_INSTR_BADDR_DBELL_START +		\
		 ((iq) * LIO_CN23XX_IQ_OFFSET))

#define LIO_CN23XX_SLI_IQ_INSTR_COUNT64(iq)				\
		(LIO_CN23XX_SLI_PKT_IN_DONE_CNTS_START64 +		\
		 ((iq) * LIO_CN23XX_IQ_OFFSET))

/*------------------ Masks ----------------*/
#define LIO_CN23XX_PKT_INPUT_CTL_VF_NUM	BIT_ULL(32)
#define LIO_CN23XX_PKT_INPUT_CTL_MAC_NUM	BIT(29)
/*
 * Number of instructions to be read in one MAC read request.
 * setting to Max value(4)
 */
#define LIO_CN23XX_PKT_INPUT_CTL_RDSIZE		(3 << 25)
#define LIO_CN23XX_PKT_INPUT_CTL_IS_64B		BIT(24)
#define LIO_CN23XX_PKT_INPUT_CTL_RST		BIT(23)
#define LIO_CN23XX_PKT_INPUT_CTL_QUIET		BIT(28)
#define LIO_CN23XX_PKT_INPUT_CTL_RING_ENB	BIT(22)
#define LIO_CN23XX_PKT_INPUT_CTL_DATA_ES_64B_SWAP	BIT(6)
#define LIO_CN23XX_PKT_INPUT_CTL_USE_CSR	BIT(4)
#define LIO_CN23XX_PKT_INPUT_CTL_GATHER_ES_64B_SWAP	(2)

#define LIO_CN23XX_PKT_INPUT_CTL_PF_NUM_POS	(45)
/* These bits[43:32] select the function number within the PF */
#define LIO_CN23XX_PKT_INPUT_CTL_MAC_NUM_POS	(29)
#define LIO_CN23XX_PKT_IN_DONE_WMARK_MASK	(0xFFFFULL)
#define LIO_CN23XX_PKT_IN_DONE_WMARK_BIT_POS	(32)
#define LIO_CN23XX_PKT_IN_DONE_CNT_MASK		0x00000000FFFFFFFFULL

#if BYTE_ORDER == LITTLE_ENDIAN
#define LIO_CN23XX_PKT_INPUT_CTL_MASK					\
		(LIO_CN23XX_PKT_INPUT_CTL_RDSIZE		|	\
		 LIO_CN23XX_PKT_INPUT_CTL_DATA_ES_64B_SWAP	|	\
		 LIO_CN23XX_PKT_INPUT_CTL_USE_CSR)
#else	/* BYTE_ORDER != LITTLE_ENDIAN */
#define LIO_CN23XX_PKT_INPUT_CTL_MASK					\
		(LIO_CN23XX_PKT_INPUT_CTL_RDSIZE		|	\
		 LIO_CN23XX_PKT_INPUT_CTL_DATA_ES_64B_SWAP	|	\
		 LIO_CN23XX_PKT_INPUT_CTL_USE_CSR		|	\
		 LIO_CN23XX_PKT_INPUT_CTL_GATHER_ES_64B_SWAP)
#endif	/* BYTE_ORDER == LITTLE_ENDIAN */

/*############################ OUTPUT QUEUE #########################*/

/* 64 registers for Output queue control - SLI_PKT(0..63)_OUTPUT_CONTROL */
#define LIO_CN23XX_SLI_PKT_OUTPUT_CONTROL_START	0x10050

/* 64 registers for Output queue buffer and info size - SLI_PKT0_OUT_SIZE */
#define LIO_CN23XX_SLI_PKT_OUT_SIZE	0x10060

/* 64 registers for Output Queue Start Addr - SLI_PKT0_SLIST_BADDR */
#define LIO_CN23XX_SLI_SLIST_BADDR_START64	0x10070

/* 64 registers for Output Queue Packet Credits - SLI_PKT0_SLIST_BAOFF_DBELL */
#define LIO_CN23XX_SLI_PKT_SLIST_BAOFF_DBELL_START	0x10080

/* 64 registers for Output Queue size - SLI_PKT0_SLIST_FIFO_RSIZE */
#define LIO_CN23XX_SLI_PKT_SLIST_FIFO_RSIZE_START	0x10090

/* 64 registers for Output Queue Packet Count - SLI_PKT0_CNTS */
#define LIO_CN23XX_SLI_PKT_CNTS_START	0x100B0

/* 64 registers for Output Queue INT Levels - SLI_PKT0_INT_LEVELS */
#define LIO_CN23XX_SLI_PKT_INT_LEVELS_START64	0x100A0

/* Each Output Queue register is at a 16-byte Offset in BAR0 */
#define LIO_CN23XX_OQ_OFFSET	0x20000

/* 1 (64-bit register) for Output Queue backpressure across all rings. */
#define LIO_CN23XX_SLI_OQ_WMARK	0x29180

/* Global pkt control register */
#define LIO_CN23XX_SLI_GBL_CONTROL	0x29210

/* Backpressure enable register for PF0  */
#define LIO_CN23XX_SLI_OUT_BP_EN_W1S	0x29260

/* Backpressure enable register for PF1  */
#define LIO_CN23XX_SLI_OUT_BP_EN2_W1S	0x29270

/*------- Output Queue Macros ---------*/

#define LIO_CN23XX_SLI_OQ_PKT_CONTROL(oq)				\
		(LIO_CN23XX_SLI_PKT_OUTPUT_CONTROL_START +		\
		 ((oq) * LIO_CN23XX_OQ_OFFSET))

#define LIO_CN23XX_SLI_OQ_BASE_ADDR64(oq)				\
		(LIO_CN23XX_SLI_SLIST_BADDR_START64 +			\
		 ((oq) * LIO_CN23XX_OQ_OFFSET))

#define LIO_CN23XX_SLI_OQ_SIZE(oq)					\
		(LIO_CN23XX_SLI_PKT_SLIST_FIFO_RSIZE_START +		\
		 ((oq) * LIO_CN23XX_OQ_OFFSET))

#define LIO_CN23XX_SLI_OQ_BUFF_INFO_SIZE(oq)				\
		(LIO_CN23XX_SLI_PKT_OUT_SIZE +				\
		 ((oq) * LIO_CN23XX_OQ_OFFSET))

#define LIO_CN23XX_SLI_OQ_PKTS_SENT(oq)					\
		(LIO_CN23XX_SLI_PKT_CNTS_START +			\
		 ((oq) * LIO_CN23XX_OQ_OFFSET))

#define LIO_CN23XX_SLI_OQ_PKTS_CREDIT(oq)				\
		(LIO_CN23XX_SLI_PKT_SLIST_BAOFF_DBELL_START +		\
		 ((oq) * LIO_CN23XX_OQ_OFFSET))

#define LIO_CN23XX_SLI_OQ_PKT_INT_LEVELS(oq)				\
		(LIO_CN23XX_SLI_PKT_INT_LEVELS_START64 +		\
		 ((oq) * LIO_CN23XX_OQ_OFFSET))

/*------------------ Masks ----------------*/
#define LIO_CN23XX_PKT_OUTPUT_CTL_TENB		BIT(13)
#define LIO_CN23XX_PKT_OUTPUT_CTL_CENB		BIT(12)
#define LIO_CN23XX_PKT_OUTPUT_CTL_IPTR		BIT(11)
#define LIO_CN23XX_PKT_OUTPUT_CTL_ES		BIT(9)
#define LIO_CN23XX_PKT_OUTPUT_CTL_NSR		BIT(8)
#define LIO_CN23XX_PKT_OUTPUT_CTL_ROR		BIT(7)
#define LIO_CN23XX_PKT_OUTPUT_CTL_DPTR		BIT(6)
#define LIO_CN23XX_PKT_OUTPUT_CTL_BMODE		BIT(5)
#define LIO_CN23XX_PKT_OUTPUT_CTL_ES_P		BIT(3)
#define LIO_CN23XX_PKT_OUTPUT_CTL_NSR_P		BIT(2)
#define LIO_CN23XX_PKT_OUTPUT_CTL_ROR_P		BIT(1)
#define LIO_CN23XX_PKT_OUTPUT_CTL_RING_ENB	BIT(0)

/*######################## MSIX TABLE #########################*/

#define LIO_CN23XX_MSIX_TABLE_ADDR_START	0x0
#define	CN23XX_MSIX_TABLE_DATA_START		0x8
#define	CN23XX_MSIX_TABLE_SIZE			0x10

#define	CN23XX_MSIX_TABLE_ADDR(idx)		\
	(LIO_CN23XX_MSIX_TABLE_ADDR_START +	\
	 ((idx) * LIO_CN23XX_MSIX_TABLE_SIZE))

#define	CN23XX_MSIX_TABLE_DATA(idx)		\
	(LIO_CN23XX_MSIX_TABLE_DATA_START +	\
	 ((idx) * LIO_CN23XX_MSIX_TABLE_SIZE))

/*######################## INTERRUPTS #########################*/
#define LIO_CN23XX_MAC_INT_OFFSET	0x20
#define LIO_CN23XX_PF_INT_OFFSET	0x10

/* 1 register (64-bit) for Interrupt Summary */
#define LIO_CN23XX_SLI_INT_SUM64	0x27000

/* 4 registers (64-bit) for Interrupt Enable for each Port */
#define LIO_CN23XX_SLI_INT_ENB64	0x27080

#define LIO_CN23XX_SLI_MAC_PF_INT_SUM64(mac, pf)			\
		(LIO_CN23XX_SLI_INT_SUM64 +				\
		 ((mac) * LIO_CN23XX_MAC_INT_OFFSET) +			\
		 ((pf) * LIO_CN23XX_PF_INT_OFFSET))

#define LIO_CN23XX_SLI_MAC_PF_INT_ENB64(mac, pf)			\
		(LIO_CN23XX_SLI_INT_ENB64 +				\
		 ((mac) * LIO_CN23XX_MAC_INT_OFFSET) +			\
		 ((pf) * LIO_CN23XX_PF_INT_OFFSET))

/* 1 register (64-bit) to indicate which Output Queue reached pkt threshold */
#define LIO_CN23XX_SLI_PKT_CNT_INT	0x29130

/* 1 register (64-bit) to indicate which Output Queue reached time threshold */
#define LIO_CN23XX_SLI_PKT_TIME_INT	0x29140

/*------------------ Interrupt Masks ----------------*/

#define LIO_CN23XX_INTR_PO_INT	BIT_ULL(63)
#define LIO_CN23XX_INTR_PI_INT	BIT_ULL(62)
#define LIO_CN23XX_INTR_RESEND		BIT_ULL(60)

#define LIO_CN23XX_INTR_CINT_ENB	BIT_ULL(48)

#define LIO_CN23XX_INTR_MIO_INT		BIT(1)
#define LIO_CN23XX_INTR_PKT_TIME	BIT(5)
#define LIO_CN23XX_INTR_M0UPB0_ERR	BIT(8)
#define LIO_CN23XX_INTR_M0UPWI_ERR	BIT(9)
#define LIO_CN23XX_INTR_M0UNB0_ERR	BIT(10)
#define LIO_CN23XX_INTR_M0UNWI_ERR	BIT(11)

#define LIO_CN23XX_INTR_DMA0_FORCE	BIT_ULL(32)
#define LIO_CN23XX_INTR_DMA1_FORCE	BIT_ULL(33)

#define LIO_CN23XX_INTR_DMA0_TIME	BIT_ULL(36)
#define LIO_CN23XX_INTR_DMA1_TIME	BIT_ULL(37)

#define LIO_CN23XX_INTR_DMAPF_ERR	BIT_ULL(59)

#define LIO_CN23XX_INTR_PKTPF_ERR	BIT_ULL(61)
#define LIO_CN23XX_INTR_PPPF_ERR	BIT_ULL(63)

#define LIO_CN23XX_INTR_DMA0_DATA	(LIO_CN23XX_INTR_DMA0_TIME)
#define LIO_CN23XX_INTR_DMA1_DATA	(LIO_CN23XX_INTR_DMA1_TIME)

#define LIO_CN23XX_INTR_DMA_DATA			\
		(LIO_CN23XX_INTR_DMA0_DATA | LIO_CN23XX_INTR_DMA1_DATA)

/* By fault only TIME based */
#define LIO_CN23XX_INTR_PKT_DATA	(LIO_CN23XX_INTR_PKT_TIME)

/* Sum of interrupts for error events */
#define LIO_CN23XX_INTR_ERR				\
		(LIO_CN23XX_INTR_M0UPB0_ERR	|	\
		 LIO_CN23XX_INTR_M0UPWI_ERR	|	\
		 LIO_CN23XX_INTR_M0UNB0_ERR	|	\
		 LIO_CN23XX_INTR_M0UNWI_ERR	|	\
		 LIO_CN23XX_INTR_DMAPF_ERR	|	\
		 LIO_CN23XX_INTR_PKTPF_ERR	|	\
		 LIO_CN23XX_INTR_PPPF_ERR)

/* Programmed Mask for Interrupt Sum */
#define LIO_CN23XX_INTR_MASK				\
		(LIO_CN23XX_INTR_DMA_DATA	|	\
		 LIO_CN23XX_INTR_DMA0_FORCE	|	\
		 LIO_CN23XX_INTR_DMA1_FORCE	|	\
		 LIO_CN23XX_INTR_MIO_INT	|	\
		 LIO_CN23XX_INTR_ERR)

/* 4 Registers (64 - bit) */
#define LIO_CN23XX_SLI_S2M_PORT_CTL_START	0x23D80
#define LIO_CN23XX_SLI_S2M_PORTX_CTL(port)		\
		(LIO_CN23XX_SLI_S2M_PORT_CTL_START +	\
		 ((port) * 0x10))

#define LIO_CN23XX_SLI_MAC_NUMBER	0x20050

/*
 *  PEM(0..3)_BAR1_INDEX(0..15)address is defined as
 *  addr = (0x00011800C0000100  |port <<24 |idx <<3 )
 *  Here, port is PEM(0..3) & idx is INDEX(0..15)
 */
#define LIO_CN23XX_PEM_BAR1_INDEX_START	0x00011800C0000100ULL
#define LIO_CN23XX_PEM_OFFSET		24
#define LIO_CN23XX_BAR1_INDEX_OFFSET	3

#define LIO_CN23XX_PEM_BAR1_INDEX_REG(port, idx)		\
		(LIO_CN23XX_PEM_BAR1_INDEX_START +		\
		 ((port) << LIO_CN23XX_PEM_OFFSET) +		\
		 ((idx) << LIO_CN23XX_BAR1_INDEX_OFFSET))

/*############################ DPI #########################*/
/* 4 Registers (64-bit) */
#define LIO_CN23XX_DPI_SLI_PRT_CFG_START	0x0001df0000000900ULL
#define LIO_CN23XX_DPI_SLI_PRTX_CFG(port)		\
		((IO_CN23XX_DPI_SLI_PRT_CFG_START +	\
		 ((port) * 0x8))

/*############################ RST #########################*/

#define LIO_CN23XX_RST_BOOT			0x0001180006001600ULL
#define LIO_CN23XX_RST_SOFT_RST			0x0001180006001680ULL

#define LIO_CN23XX_LMC0_RESET_CTL		0x0001180088000180ULL
#define LIO_CN23XX_LMC0_RESET_CTL_DDR3RST_MASK	0x0000000000000001ULL

#endif	/* __CN23XX_PF_REGS_H__ */
