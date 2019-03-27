/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2015, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#ifndef _E1000_REGS_H_
#define _E1000_REGS_H_

#define E1000_CTRL	0x00000  /* Device Control - RW */
#define E1000_CTRL_DUP	0x00004  /* Device Control Duplicate (Shadow) - RW */
#define E1000_STATUS	0x00008  /* Device Status - RO */
#define E1000_EECD	0x00010  /* EEPROM/Flash Control - RW */
#define E1000_EERD	0x00014  /* EEPROM Read - RW */
#define E1000_CTRL_EXT	0x00018  /* Extended Device Control - RW */
#define E1000_FLA	0x0001C  /* Flash Access - RW */
#define E1000_MDIC	0x00020  /* MDI Control - RW */
#define E1000_MDICNFG	0x00E04  /* MDI Config - RW */
#define E1000_REGISTER_SET_SIZE		0x20000 /* CSR Size */
#define E1000_EEPROM_INIT_CTRL_WORD_2	0x0F /* EEPROM Init Ctrl Word 2 */
#define E1000_EEPROM_PCIE_CTRL_WORD_2	0x28 /* EEPROM PCIe Ctrl Word 2 */
#define E1000_BARCTRL			0x5BBC /* BAR ctrl reg */
#define E1000_BARCTRL_FLSIZE		0x0700 /* BAR ctrl Flsize */
#define E1000_BARCTRL_CSRSIZE		0x2000 /* BAR ctrl CSR size */
#define E1000_MPHY_ADDR_CTRL	0x0024 /* GbE MPHY Address Control */
#define E1000_MPHY_DATA		0x0E10 /* GBE MPHY Data */
#define E1000_MPHY_STAT		0x0E0C /* GBE MPHY Statistics */
#define E1000_PPHY_CTRL		0x5b48 /* PCIe PHY Control */
#define E1000_I350_BARCTRL		0x5BFC /* BAR ctrl reg */
#define E1000_I350_DTXMXPKTSZ		0x355C /* Maximum sent packet size reg*/
#define E1000_SCTL	0x00024  /* SerDes Control - RW */
#define E1000_FCAL	0x00028  /* Flow Control Address Low - RW */
#define E1000_FCAH	0x0002C  /* Flow Control Address High -RW */
#define E1000_FEXT	0x0002C  /* Future Extended - RW */
#define E1000_FEXTNVM	0x00028  /* Future Extended NVM - RW */
#define E1000_FEXTNVM3	0x0003C  /* Future Extended NVM 3 - RW */
#define E1000_FEXTNVM4	0x00024  /* Future Extended NVM 4 - RW */
#define E1000_FEXTNVM6	0x00010  /* Future Extended NVM 6 - RW */
#define E1000_FEXTNVM7	0x000E4  /* Future Extended NVM 7 - RW */
#define E1000_FEXTNVM9	0x5BB4  /* Future Extended NVM 9 - RW */
#define E1000_FEXTNVM11	0x5BBC  /* Future Extended NVM 11 - RW */
#define E1000_PCIEANACFG	0x00F18 /* PCIE Analog Config */
#define E1000_FCT	0x00030  /* Flow Control Type - RW */
#define E1000_CONNSW	0x00034  /* Copper/Fiber switch control - RW */
#define E1000_VET	0x00038  /* VLAN Ether Type - RW */
#define E1000_ICR	0x000C0  /* Interrupt Cause Read - R/clr */
#define E1000_ITR	0x000C4  /* Interrupt Throttling Rate - RW */
#define E1000_ICS	0x000C8  /* Interrupt Cause Set - WO */
#define E1000_IMS	0x000D0  /* Interrupt Mask Set - RW */
#define E1000_IMC	0x000D8  /* Interrupt Mask Clear - WO */
#define E1000_IAM	0x000E0  /* Interrupt Acknowledge Auto Mask */
#define E1000_IVAR	0x000E4  /* Interrupt Vector Allocation Register - RW */
#define E1000_SVCR	0x000F0
#define E1000_SVT	0x000F4
#define E1000_LPIC	0x000FC  /* Low Power IDLE control */
#define E1000_RCTL	0x00100  /* Rx Control - RW */
#define E1000_FCTTV	0x00170  /* Flow Control Transmit Timer Value - RW */
#define E1000_TXCW	0x00178  /* Tx Configuration Word - RW */
#define E1000_RXCW	0x00180  /* Rx Configuration Word - RO */
#define E1000_PBA_ECC	0x01100  /* PBA ECC Register */
#define E1000_EICR	0x01580  /* Ext. Interrupt Cause Read - R/clr */
#define E1000_EITR(_n)	(0x01680 + (0x4 * (_n)))
#define E1000_EICS	0x01520  /* Ext. Interrupt Cause Set - W0 */
#define E1000_EIMS	0x01524  /* Ext. Interrupt Mask Set/Read - RW */
#define E1000_EIMC	0x01528  /* Ext. Interrupt Mask Clear - WO */
#define E1000_EIAC	0x0152C  /* Ext. Interrupt Auto Clear - RW */
#define E1000_EIAM	0x01530  /* Ext. Interrupt Ack Auto Clear Mask - RW */
#define E1000_GPIE	0x01514  /* General Purpose Interrupt Enable - RW */
#define E1000_IVAR0	0x01700  /* Interrupt Vector Allocation (array) - RW */
#define E1000_IVAR_MISC	0x01740 /* IVAR for "other" causes - RW */
#define E1000_TCTL	0x00400  /* Tx Control - RW */
#define E1000_TCTL_EXT	0x00404  /* Extended Tx Control - RW */
#define E1000_TIPG	0x00410  /* Tx Inter-packet gap -RW */
#define E1000_TBT	0x00448  /* Tx Burst Timer - RW */
#define E1000_AIT	0x00458  /* Adaptive Interframe Spacing Throttle - RW */
#define E1000_LEDCTL	0x00E00  /* LED Control - RW */
#define E1000_LEDMUX	0x08130  /* LED MUX Control */
#define E1000_EXTCNF_CTRL	0x00F00  /* Extended Configuration Control */
#define E1000_EXTCNF_SIZE	0x00F08  /* Extended Configuration Size */
#define E1000_PHY_CTRL	0x00F10  /* PHY Control Register in CSR */
#define E1000_POEMB	E1000_PHY_CTRL /* PHY OEM Bits */
#define E1000_PBA	0x01000  /* Packet Buffer Allocation - RW */
#define E1000_PBS	0x01008  /* Packet Buffer Size */
#define E1000_PBECCSTS	0x0100C  /* Packet Buffer ECC Status - RW */
#define E1000_IOSFPC	0x00F28  /* TX corrupted data  */
#define E1000_EEMNGCTL	0x01010  /* MNG EEprom Control */
#define E1000_EEMNGCTL_I210	0x01010  /* i210 MNG EEprom Mode Control */
#define E1000_EEARBC	0x01024  /* EEPROM Auto Read Bus Control */
#define E1000_EEARBC_I210	0x12024 /* EEPROM Auto Read Bus Control */
#define E1000_FLASHT	0x01028  /* FLASH Timer Register */
#define E1000_EEWR	0x0102C  /* EEPROM Write Register - RW */
#define E1000_FLSWCTL	0x01030  /* FLASH control register */
#define E1000_FLSWDATA	0x01034  /* FLASH data register */
#define E1000_FLSWCNT	0x01038  /* FLASH Access Counter */
#define E1000_FLOP	0x0103C  /* FLASH Opcode Register */
#define E1000_I2CCMD	0x01028  /* SFPI2C Command Register - RW */
#define E1000_I2CPARAMS	0x0102C /* SFPI2C Parameters Register - RW */
#define E1000_I2CBB_EN	0x00000100  /* I2C - Bit Bang Enable */
#define E1000_I2C_CLK_OUT	0x00000200  /* I2C- Clock */
#define E1000_I2C_DATA_OUT	0x00000400  /* I2C- Data Out */
#define E1000_I2C_DATA_OE_N	0x00000800  /* I2C- Data Output Enable */
#define E1000_I2C_DATA_IN	0x00001000  /* I2C- Data In */
#define E1000_I2C_CLK_OE_N	0x00002000  /* I2C- Clock Output Enable */
#define E1000_I2C_CLK_IN	0x00004000  /* I2C- Clock In */
#define E1000_I2C_CLK_STRETCH_DIS	0x00008000 /* I2C- Dis Clk Stretching */
#define E1000_WDSTP	0x01040  /* Watchdog Setup - RW */
#define E1000_SWDSTS	0x01044  /* SW Device Status - RW */
#define E1000_FRTIMER	0x01048  /* Free Running Timer - RW */
#define E1000_TCPTIMER	0x0104C  /* TCP Timer - RW */
#define E1000_VPDDIAG	0x01060  /* VPD Diagnostic - RO */
#define E1000_ICR_V2	0x01500  /* Intr Cause - new location - RC */
#define E1000_ICS_V2	0x01504  /* Intr Cause Set - new location - WO */
#define E1000_IMS_V2	0x01508  /* Intr Mask Set/Read - new location - RW */
#define E1000_IMC_V2	0x0150C  /* Intr Mask Clear - new location - WO */
#define E1000_IAM_V2	0x01510  /* Intr Ack Auto Mask - new location - RW */
#define E1000_ERT	0x02008  /* Early Rx Threshold - RW */
#define E1000_FCRTL	0x02160  /* Flow Control Receive Threshold Low - RW */
#define E1000_FCRTH	0x02168  /* Flow Control Receive Threshold High - RW */
#define E1000_PSRCTL	0x02170  /* Packet Split Receive Control - RW */
#define E1000_RDFH	0x02410  /* Rx Data FIFO Head - RW */
#define E1000_RDFT	0x02418  /* Rx Data FIFO Tail - RW */
#define E1000_RDFHS	0x02420  /* Rx Data FIFO Head Saved - RW */
#define E1000_RDFTS	0x02428  /* Rx Data FIFO Tail Saved - RW */
#define E1000_RDFPC	0x02430  /* Rx Data FIFO Packet Count - RW */
#define E1000_PBRTH	0x02458  /* PB Rx Arbitration Threshold - RW */
#define E1000_FCRTV	0x02460  /* Flow Control Refresh Timer Value - RW */
/* Split and Replication Rx Control - RW */
#define E1000_RDPUMB	0x025CC  /* DMA Rx Descriptor uC Mailbox - RW */
#define E1000_RDPUAD	0x025D0  /* DMA Rx Descriptor uC Addr Command - RW */
#define E1000_RDPUWD	0x025D4  /* DMA Rx Descriptor uC Data Write - RW */
#define E1000_RDPURD	0x025D8  /* DMA Rx Descriptor uC Data Read - RW */
#define E1000_RDPUCTL	0x025DC  /* DMA Rx Descriptor uC Control - RW */
#define E1000_PBDIAG	0x02458  /* Packet Buffer Diagnostic - RW */
#define E1000_RXPBS	0x02404  /* Rx Packet Buffer Size - RW */
#define E1000_IRPBS	0x02404 /* Same as RXPBS, renamed for newer Si - RW */
#define E1000_PBRWAC	0x024E8 /* Rx packet buffer wrap around counter - RO */
#define E1000_RDTR	0x02820  /* Rx Delay Timer - RW */
#define E1000_RADV	0x0282C  /* Rx Interrupt Absolute Delay Timer - RW */
#define E1000_EMIADD	0x10     /* Extended Memory Indirect Address */
#define E1000_EMIDATA	0x11     /* Extended Memory Indirect Data */
#define E1000_SRWR		0x12018  /* Shadow Ram Write Register - RW */
#define E1000_I210_FLMNGCTL	0x12038
#define E1000_I210_FLMNGDATA	0x1203C
#define E1000_I210_FLMNGCNT	0x12040

#define E1000_I210_FLSWCTL	0x12048
#define E1000_I210_FLSWDATA	0x1204C
#define E1000_I210_FLSWCNT	0x12050

#define E1000_I210_FLA		0x1201C

#define E1000_INVM_DATA_REG(_n)	(0x12120 + 4*(_n))
#define E1000_INVM_SIZE		64 /* Number of INVM Data Registers */

/* QAV Tx mode control register */
#define E1000_I210_TQAVCTRL	0x3570

/* QAV Tx mode control register bitfields masks */
/* QAV enable */
#define E1000_TQAVCTRL_MODE			(1 << 0)
/* Fetching arbitration type */
#define E1000_TQAVCTRL_FETCH_ARB		(1 << 4)
/* Fetching timer enable */
#define E1000_TQAVCTRL_FETCH_TIMER_ENABLE	(1 << 5)
/* Launch arbitration type */
#define E1000_TQAVCTRL_LAUNCH_ARB		(1 << 8)
/* Launch timer enable */
#define E1000_TQAVCTRL_LAUNCH_TIMER_ENABLE	(1 << 9)
/* SP waits for SR enable */
#define E1000_TQAVCTRL_SP_WAIT_SR		(1 << 10)
/* Fetching timer correction */
#define E1000_TQAVCTRL_FETCH_TIMER_DELTA_OFFSET	16
#define E1000_TQAVCTRL_FETCH_TIMER_DELTA	\
			(0xFFFF << E1000_TQAVCTRL_FETCH_TIMER_DELTA_OFFSET)

/* High credit registers where _n can be 0 or 1. */
#define E1000_I210_TQAVHC(_n)			(0x300C + 0x40 * (_n))

/* Queues fetch arbitration priority control register */
#define E1000_I210_TQAVARBCTRL			0x3574
/* Queues priority masks where _n and _p can be 0-3. */
#define E1000_TQAVARBCTRL_QUEUE_PRI(_n, _p)	((_p) << (2 * (_n)))
/* QAV Tx mode control registers where _n can be 0 or 1. */
#define E1000_I210_TQAVCC(_n)			(0x3004 + 0x40 * (_n))

/* QAV Tx mode control register bitfields masks */
#define E1000_TQAVCC_IDLE_SLOPE		0xFFFF /* Idle slope */
#define E1000_TQAVCC_KEEP_CREDITS	(1 << 30) /* Keep credits opt enable */
#define E1000_TQAVCC_QUEUE_MODE		(1U << 31) /* SP vs. SR Tx mode */

/* Good transmitted packets counter registers */
#define E1000_PQGPTC(_n)		(0x010014 + (0x100 * (_n)))

/* Queues packet buffer size masks where _n can be 0-3 and _s 0-63 [kB] */
#define E1000_I210_TXPBS_SIZE(_n, _s)	((_s) << (6 * (_n)))

#define E1000_MMDAC			13 /* MMD Access Control */
#define E1000_MMDAAD			14 /* MMD Access Address/Data */

/* Convenience macros
 *
 * Note: "_n" is the queue number of the register to be written to.
 *
 * Example usage:
 * E1000_RDBAL_REG(current_rx_queue)
 */
#define E1000_RDBAL(_n)	((_n) < 4 ? (0x02800 + ((_n) * 0x100)) : \
			 (0x0C000 + ((_n) * 0x40)))
#define E1000_RDBAH(_n)	((_n) < 4 ? (0x02804 + ((_n) * 0x100)) : \
			 (0x0C004 + ((_n) * 0x40)))
#define E1000_RDLEN(_n)	((_n) < 4 ? (0x02808 + ((_n) * 0x100)) : \
			 (0x0C008 + ((_n) * 0x40)))
#define E1000_SRRCTL(_n)	((_n) < 4 ? (0x0280C + ((_n) * 0x100)) : \
				 (0x0C00C + ((_n) * 0x40)))
#define E1000_RDH(_n)	((_n) < 4 ? (0x02810 + ((_n) * 0x100)) : \
			 (0x0C010 + ((_n) * 0x40)))
#define E1000_RXCTL(_n)	((_n) < 4 ? (0x02814 + ((_n) * 0x100)) : \
			 (0x0C014 + ((_n) * 0x40)))
#define E1000_DCA_RXCTRL(_n)	E1000_RXCTL(_n)
#define E1000_RDT(_n)	((_n) < 4 ? (0x02818 + ((_n) * 0x100)) : \
			 (0x0C018 + ((_n) * 0x40)))
#define E1000_RXDCTL(_n)	((_n) < 4 ? (0x02828 + ((_n) * 0x100)) : \
				 (0x0C028 + ((_n) * 0x40)))
#define E1000_RQDPC(_n)	((_n) < 4 ? (0x02830 + ((_n) * 0x100)) : \
			 (0x0C030 + ((_n) * 0x40)))
#define E1000_TDBAL(_n)	((_n) < 4 ? (0x03800 + ((_n) * 0x100)) : \
			 (0x0E000 + ((_n) * 0x40)))
#define E1000_TDBAH(_n)	((_n) < 4 ? (0x03804 + ((_n) * 0x100)) : \
			 (0x0E004 + ((_n) * 0x40)))
#define E1000_TDLEN(_n)	((_n) < 4 ? (0x03808 + ((_n) * 0x100)) : \
			 (0x0E008 + ((_n) * 0x40)))
#define E1000_TDH(_n)	((_n) < 4 ? (0x03810 + ((_n) * 0x100)) : \
			 (0x0E010 + ((_n) * 0x40)))
#define E1000_TXCTL(_n)	((_n) < 4 ? (0x03814 + ((_n) * 0x100)) : \
			 (0x0E014 + ((_n) * 0x40)))
#define E1000_DCA_TXCTRL(_n) E1000_TXCTL(_n)
#define E1000_TDT(_n)	((_n) < 4 ? (0x03818 + ((_n) * 0x100)) : \
			 (0x0E018 + ((_n) * 0x40)))
#define E1000_TXDCTL(_n)	((_n) < 4 ? (0x03828 + ((_n) * 0x100)) : \
				 (0x0E028 + ((_n) * 0x40)))
#define E1000_TDWBAL(_n)	((_n) < 4 ? (0x03838 + ((_n) * 0x100)) : \
				 (0x0E038 + ((_n) * 0x40)))
#define E1000_TDWBAH(_n)	((_n) < 4 ? (0x0383C + ((_n) * 0x100)) : \
				 (0x0E03C + ((_n) * 0x40)))
#define E1000_TARC(_n)		(0x03840 + ((_n) * 0x100))
#define E1000_RSRPD		0x02C00  /* Rx Small Packet Detect - RW */
#define E1000_RAID		0x02C08  /* Receive Ack Interrupt Delay - RW */
#define E1000_TXDMAC		0x03000  /* Tx DMA Control - RW */
#define E1000_KABGTXD		0x03004  /* AFE Band Gap Transmit Ref Data */
#define E1000_PSRTYPE(_i)	(0x05480 + ((_i) * 4))
#define E1000_RAL(_i)		(((_i) <= 15) ? (0x05400 + ((_i) * 8)) : \
				 (0x054E0 + ((_i - 16) * 8)))
#define E1000_RAH(_i)		(((_i) <= 15) ? (0x05404 + ((_i) * 8)) : \
				 (0x054E4 + ((_i - 16) * 8)))
#define E1000_SHRAL(_i)		(0x05438 + ((_i) * 8))
#define E1000_SHRAH(_i)		(0x0543C + ((_i) * 8))
#define E1000_IP4AT_REG(_i)	(0x05840 + ((_i) * 8))
#define E1000_IP6AT_REG(_i)	(0x05880 + ((_i) * 4))
#define E1000_WUPM_REG(_i)	(0x05A00 + ((_i) * 4))
#define E1000_FFMT_REG(_i)	(0x09000 + ((_i) * 8))
#define E1000_FFVT_REG(_i)	(0x09800 + ((_i) * 8))
#define E1000_FFLT_REG(_i)	(0x05F00 + ((_i) * 8))
#define E1000_PBSLAC		0x03100  /* Pkt Buffer Slave Access Control */
#define E1000_PBSLAD(_n)	(0x03110 + (0x4 * (_n)))  /* Pkt Buffer DWORD */
#define E1000_TXPBS		0x03404  /* Tx Packet Buffer Size - RW */
/* Same as TXPBS, renamed for newer Si - RW */
#define E1000_ITPBS		0x03404
#define E1000_TDFH		0x03410  /* Tx Data FIFO Head - RW */
#define E1000_TDFT		0x03418  /* Tx Data FIFO Tail - RW */
#define E1000_TDFHS		0x03420  /* Tx Data FIFO Head Saved - RW */
#define E1000_TDFTS		0x03428  /* Tx Data FIFO Tail Saved - RW */
#define E1000_TDFPC		0x03430  /* Tx Data FIFO Packet Count - RW */
#define E1000_TDPUMB		0x0357C  /* DMA Tx Desc uC Mail Box - RW */
#define E1000_TDPUAD		0x03580  /* DMA Tx Desc uC Addr Command - RW */
#define E1000_TDPUWD		0x03584  /* DMA Tx Desc uC Data Write - RW */
#define E1000_TDPURD		0x03588  /* DMA Tx Desc uC Data  Read  - RW */
#define E1000_TDPUCTL		0x0358C  /* DMA Tx Desc uC Control - RW */
#define E1000_DTXCTL		0x03590  /* DMA Tx Control - RW */
#define E1000_DTXTCPFLGL	0x0359C /* DMA Tx Control flag low - RW */
#define E1000_DTXTCPFLGH	0x035A0 /* DMA Tx Control flag high - RW */
/* DMA Tx Max Total Allow Size Reqs - RW */
#define E1000_DTXMXSZRQ		0x03540
#define E1000_TIDV	0x03820  /* Tx Interrupt Delay Value - RW */
#define E1000_TADV	0x0382C  /* Tx Interrupt Absolute Delay Val - RW */
#define E1000_TSPMT	0x03830  /* TCP Segmentation PAD & Min Threshold - RW */
#define E1000_CRCERRS	0x04000  /* CRC Error Count - R/clr */
#define E1000_ALGNERRC	0x04004  /* Alignment Error Count - R/clr */
#define E1000_SYMERRS	0x04008  /* Symbol Error Count - R/clr */
#define E1000_RXERRC	0x0400C  /* Receive Error Count - R/clr */
#define E1000_MPC	0x04010  /* Missed Packet Count - R/clr */
#define E1000_SCC	0x04014  /* Single Collision Count - R/clr */
#define E1000_ECOL	0x04018  /* Excessive Collision Count - R/clr */
#define E1000_MCC	0x0401C  /* Multiple Collision Count - R/clr */
#define E1000_LATECOL	0x04020  /* Late Collision Count - R/clr */
#define E1000_COLC	0x04028  /* Collision Count - R/clr */
#define E1000_DC	0x04030  /* Defer Count - R/clr */
#define E1000_TNCRS	0x04034  /* Tx-No CRS - R/clr */
#define E1000_SEC	0x04038  /* Sequence Error Count - R/clr */
#define E1000_CEXTERR	0x0403C  /* Carrier Extension Error Count - R/clr */
#define E1000_RLEC	0x04040  /* Receive Length Error Count - R/clr */
#define E1000_XONRXC	0x04048  /* XON Rx Count - R/clr */
#define E1000_XONTXC	0x0404C  /* XON Tx Count - R/clr */
#define E1000_XOFFRXC	0x04050  /* XOFF Rx Count - R/clr */
#define E1000_XOFFTXC	0x04054  /* XOFF Tx Count - R/clr */
#define E1000_FCRUC	0x04058  /* Flow Control Rx Unsupported Count- R/clr */
#define E1000_PRC64	0x0405C  /* Packets Rx (64 bytes) - R/clr */
#define E1000_PRC127	0x04060  /* Packets Rx (65-127 bytes) - R/clr */
#define E1000_PRC255	0x04064  /* Packets Rx (128-255 bytes) - R/clr */
#define E1000_PRC511	0x04068  /* Packets Rx (255-511 bytes) - R/clr */
#define E1000_PRC1023	0x0406C  /* Packets Rx (512-1023 bytes) - R/clr */
#define E1000_PRC1522	0x04070  /* Packets Rx (1024-1522 bytes) - R/clr */
#define E1000_GPRC	0x04074  /* Good Packets Rx Count - R/clr */
#define E1000_BPRC	0x04078  /* Broadcast Packets Rx Count - R/clr */
#define E1000_MPRC	0x0407C  /* Multicast Packets Rx Count - R/clr */
#define E1000_GPTC	0x04080  /* Good Packets Tx Count - R/clr */
#define E1000_GORCL	0x04088  /* Good Octets Rx Count Low - R/clr */
#define E1000_GORCH	0x0408C  /* Good Octets Rx Count High - R/clr */
#define E1000_GOTCL	0x04090  /* Good Octets Tx Count Low - R/clr */
#define E1000_GOTCH	0x04094  /* Good Octets Tx Count High - R/clr */
#define E1000_RNBC	0x040A0  /* Rx No Buffers Count - R/clr */
#define E1000_RUC	0x040A4  /* Rx Undersize Count - R/clr */
#define E1000_RFC	0x040A8  /* Rx Fragment Count - R/clr */
#define E1000_ROC	0x040AC  /* Rx Oversize Count - R/clr */
#define E1000_RJC	0x040B0  /* Rx Jabber Count - R/clr */
#define E1000_MGTPRC	0x040B4  /* Management Packets Rx Count - R/clr */
#define E1000_MGTPDC	0x040B8  /* Management Packets Dropped Count - R/clr */
#define E1000_MGTPTC	0x040BC  /* Management Packets Tx Count - R/clr */
#define E1000_TORL	0x040C0  /* Total Octets Rx Low - R/clr */
#define E1000_TORH	0x040C4  /* Total Octets Rx High - R/clr */
#define E1000_TOTL	0x040C8  /* Total Octets Tx Low - R/clr */
#define E1000_TOTH	0x040CC  /* Total Octets Tx High - R/clr */
#define E1000_TPR	0x040D0  /* Total Packets Rx - R/clr */
#define E1000_TPT	0x040D4  /* Total Packets Tx - R/clr */
#define E1000_PTC64	0x040D8  /* Packets Tx (64 bytes) - R/clr */
#define E1000_PTC127	0x040DC  /* Packets Tx (65-127 bytes) - R/clr */
#define E1000_PTC255	0x040E0  /* Packets Tx (128-255 bytes) - R/clr */
#define E1000_PTC511	0x040E4  /* Packets Tx (256-511 bytes) - R/clr */
#define E1000_PTC1023	0x040E8  /* Packets Tx (512-1023 bytes) - R/clr */
#define E1000_PTC1522	0x040EC  /* Packets Tx (1024-1522 Bytes) - R/clr */
#define E1000_MPTC	0x040F0  /* Multicast Packets Tx Count - R/clr */
#define E1000_BPTC	0x040F4  /* Broadcast Packets Tx Count - R/clr */
#define E1000_TSCTC	0x040F8  /* TCP Segmentation Context Tx - R/clr */
#define E1000_TSCTFC	0x040FC  /* TCP Segmentation Context Tx Fail - R/clr */
#define E1000_IAC	0x04100  /* Interrupt Assertion Count */
#define E1000_ICRXPTC	0x04104  /* Interrupt Cause Rx Pkt Timer Expire Count */
#define E1000_ICRXATC	0x04108  /* Interrupt Cause Rx Abs Timer Expire Count */
#define E1000_ICTXPTC	0x0410C  /* Interrupt Cause Tx Pkt Timer Expire Count */
#define E1000_ICTXATC	0x04110  /* Interrupt Cause Tx Abs Timer Expire Count */
#define E1000_ICTXQEC	0x04118  /* Interrupt Cause Tx Queue Empty Count */
#define E1000_ICTXQMTC	0x0411C  /* Interrupt Cause Tx Queue Min Thresh Count */
#define E1000_ICRXDMTC	0x04120  /* Interrupt Cause Rx Desc Min Thresh Count */
#define E1000_ICRXOC	0x04124  /* Interrupt Cause Receiver Overrun Count */
#define E1000_CRC_OFFSET	0x05F50  /* CRC Offset register */

#define E1000_VFGPRC	0x00F10
#define E1000_VFGORC	0x00F18
#define E1000_VFMPRC	0x00F3C
#define E1000_VFGPTC	0x00F14
#define E1000_VFGOTC	0x00F34
#define E1000_VFGOTLBC	0x00F50
#define E1000_VFGPTLBC	0x00F44
#define E1000_VFGORLBC	0x00F48
#define E1000_VFGPRLBC	0x00F40
/* Virtualization statistical counters */
#define E1000_PFVFGPRC(_n)	(0x010010 + (0x100 * (_n)))
#define E1000_PFVFGPTC(_n)	(0x010014 + (0x100 * (_n)))
#define E1000_PFVFGORC(_n)	(0x010018 + (0x100 * (_n)))
#define E1000_PFVFGOTC(_n)	(0x010034 + (0x100 * (_n)))
#define E1000_PFVFMPRC(_n)	(0x010038 + (0x100 * (_n)))
#define E1000_PFVFGPRLBC(_n)	(0x010040 + (0x100 * (_n)))
#define E1000_PFVFGPTLBC(_n)	(0x010044 + (0x100 * (_n)))
#define E1000_PFVFGORLBC(_n)	(0x010048 + (0x100 * (_n)))
#define E1000_PFVFGOTLBC(_n)	(0x010050 + (0x100 * (_n)))

/* LinkSec */
#define E1000_LSECTXUT		0x04300  /* Tx Untagged Pkt Cnt */
#define E1000_LSECTXPKTE	0x04304  /* Encrypted Tx Pkts Cnt */
#define E1000_LSECTXPKTP	0x04308  /* Protected Tx Pkt Cnt */
#define E1000_LSECTXOCTE	0x0430C  /* Encrypted Tx Octets Cnt */
#define E1000_LSECTXOCTP	0x04310  /* Protected Tx Octets Cnt */
#define E1000_LSECRXUT		0x04314  /* Untagged non-Strict Rx Pkt Cnt */
#define E1000_LSECRXOCTD	0x0431C  /* Rx Octets Decrypted Count */
#define E1000_LSECRXOCTV	0x04320  /* Rx Octets Validated */
#define E1000_LSECRXBAD		0x04324  /* Rx Bad Tag */
#define E1000_LSECRXNOSCI	0x04328  /* Rx Packet No SCI Count */
#define E1000_LSECRXUNSCI	0x0432C  /* Rx Packet Unknown SCI Count */
#define E1000_LSECRXUNCH	0x04330  /* Rx Unchecked Packets Count */
#define E1000_LSECRXDELAY	0x04340  /* Rx Delayed Packet Count */
#define E1000_LSECRXLATE	0x04350  /* Rx Late Packets Count */
#define E1000_LSECRXOK(_n)	(0x04360 + (0x04 * (_n))) /* Rx Pkt OK Cnt */
#define E1000_LSECRXINV(_n)	(0x04380 + (0x04 * (_n))) /* Rx Invalid Cnt */
#define E1000_LSECRXNV(_n)	(0x043A0 + (0x04 * (_n))) /* Rx Not Valid Cnt */
#define E1000_LSECRXUNSA	0x043C0  /* Rx Unused SA Count */
#define E1000_LSECRXNUSA	0x043D0  /* Rx Not Using SA Count */
#define E1000_LSECTXCAP		0x0B000  /* Tx Capabilities Register - RO */
#define E1000_LSECRXCAP		0x0B300  /* Rx Capabilities Register - RO */
#define E1000_LSECTXCTRL	0x0B004  /* Tx Control - RW */
#define E1000_LSECRXCTRL	0x0B304  /* Rx Control - RW */
#define E1000_LSECTXSCL		0x0B008  /* Tx SCI Low - RW */
#define E1000_LSECTXSCH		0x0B00C  /* Tx SCI High - RW */
#define E1000_LSECTXSA		0x0B010  /* Tx SA0 - RW */
#define E1000_LSECTXPN0		0x0B018  /* Tx SA PN 0 - RW */
#define E1000_LSECTXPN1		0x0B01C  /* Tx SA PN 1 - RW */
#define E1000_LSECRXSCL		0x0B3D0  /* Rx SCI Low - RW */
#define E1000_LSECRXSCH		0x0B3E0  /* Rx SCI High - RW */
/* LinkSec Tx 128-bit Key 0 - WO */
#define E1000_LSECTXKEY0(_n)	(0x0B020 + (0x04 * (_n)))
/* LinkSec Tx 128-bit Key 1 - WO */
#define E1000_LSECTXKEY1(_n)	(0x0B030 + (0x04 * (_n)))
#define E1000_LSECRXSA(_n)	(0x0B310 + (0x04 * (_n))) /* Rx SAs - RW */
#define E1000_LSECRXPN(_n)	(0x0B330 + (0x04 * (_n))) /* Rx SAs - RW */
/* LinkSec Rx Keys  - where _n is the SA no. and _m the 4 dwords of the 128 bit
 * key - RW.
 */
#define E1000_LSECRXKEY(_n, _m)	(0x0B350 + (0x10 * (_n)) + (0x04 * (_m)))

#define E1000_SSVPC		0x041A0 /* Switch Security Violation Pkt Cnt */
#define E1000_IPSCTRL		0xB430  /* IpSec Control Register */
#define E1000_IPSRXCMD		0x0B408 /* IPSec Rx Command Register - RW */
#define E1000_IPSRXIDX		0x0B400 /* IPSec Rx Index - RW */
/* IPSec Rx IPv4/v6 Address - RW */
#define E1000_IPSRXIPADDR(_n)	(0x0B420 + (0x04 * (_n)))
/* IPSec Rx 128-bit Key - RW */
#define E1000_IPSRXKEY(_n)	(0x0B410 + (0x04 * (_n)))
#define E1000_IPSRXSALT		0x0B404  /* IPSec Rx Salt - RW */
#define E1000_IPSRXSPI		0x0B40C  /* IPSec Rx SPI - RW */
/* IPSec Tx 128-bit Key - RW */
#define E1000_IPSTXKEY(_n)	(0x0B460 + (0x04 * (_n)))
#define E1000_IPSTXSALT		0x0B454  /* IPSec Tx Salt - RW */
#define E1000_IPSTXIDX		0x0B450  /* IPSec Tx SA IDX - RW */
#define E1000_PCS_CFG0	0x04200  /* PCS Configuration 0 - RW */
#define E1000_PCS_LCTL	0x04208  /* PCS Link Control - RW */
#define E1000_PCS_LSTAT	0x0420C  /* PCS Link Status - RO */
#define E1000_CBTMPC	0x0402C  /* Circuit Breaker Tx Packet Count */
#define E1000_HTDPMC	0x0403C  /* Host Transmit Discarded Packets */
#define E1000_CBRDPC	0x04044  /* Circuit Breaker Rx Dropped Count */
#define E1000_CBRMPC	0x040FC  /* Circuit Breaker Rx Packet Count */
#define E1000_RPTHC	0x04104  /* Rx Packets To Host */
#define E1000_HGPTC	0x04118  /* Host Good Packets Tx Count */
#define E1000_HTCBDPC	0x04124  /* Host Tx Circuit Breaker Dropped Count */
#define E1000_HGORCL	0x04128  /* Host Good Octets Received Count Low */
#define E1000_HGORCH	0x0412C  /* Host Good Octets Received Count High */
#define E1000_HGOTCL	0x04130  /* Host Good Octets Transmit Count Low */
#define E1000_HGOTCH	0x04134  /* Host Good Octets Transmit Count High */
#define E1000_LENERRS	0x04138  /* Length Errors Count */
#define E1000_SCVPC	0x04228  /* SerDes/SGMII Code Violation Pkt Count */
#define E1000_HRMPC	0x0A018  /* Header Redirection Missed Packet Count */
#define E1000_PCS_ANADV	0x04218  /* AN advertisement - RW */
#define E1000_PCS_LPAB	0x0421C  /* Link Partner Ability - RW */
#define E1000_PCS_NPTX	0x04220  /* AN Next Page Transmit - RW */
#define E1000_PCS_LPABNP	0x04224 /* Link Partner Ability Next Pg - RW */
#define E1000_RXCSUM	0x05000  /* Rx Checksum Control - RW */
#define E1000_RLPML	0x05004  /* Rx Long Packet Max Length */
#define E1000_RFCTL	0x05008  /* Receive Filter Control*/
#define E1000_MTA	0x05200  /* Multicast Table Array - RW Array */
#define E1000_RA	0x05400  /* Receive Address - RW Array */
#define E1000_RA2	0x054E0  /* 2nd half of Rx address array - RW Array */
#define E1000_VFTA	0x05600  /* VLAN Filter Table Array - RW Array */
#define E1000_VT_CTL	0x0581C  /* VMDq Control - RW */
#define E1000_CIAA	0x05B88  /* Config Indirect Access Address - RW */
#define E1000_CIAD	0x05B8C  /* Config Indirect Access Data - RW */
#define E1000_VFQA0	0x0B000  /* VLAN Filter Queue Array 0 - RW Array */
#define E1000_VFQA1	0x0B200  /* VLAN Filter Queue Array 1 - RW Array */
#define E1000_WUC	0x05800  /* Wakeup Control - RW */
#define E1000_WUFC	0x05808  /* Wakeup Filter Control - RW */
#define E1000_WUS	0x05810  /* Wakeup Status - RO */
#define E1000_MANC	0x05820  /* Management Control - RW */
#define E1000_IPAV	0x05838  /* IP Address Valid - RW */
#define E1000_IP4AT	0x05840  /* IPv4 Address Table - RW Array */
#define E1000_IP6AT	0x05880  /* IPv6 Address Table - RW Array */
#define E1000_WUPL	0x05900  /* Wakeup Packet Length - RW */
#define E1000_WUPM	0x05A00  /* Wakeup Packet Memory - RO A */
#define E1000_PBACL	0x05B68  /* MSIx PBA Clear - Read/Write 1's to clear */
#define E1000_FFLT	0x05F00  /* Flexible Filter Length Table - RW Array */
#define E1000_HOST_IF	0x08800  /* Host Interface */
#define E1000_HIBBA	0x8F40   /* Host Interface Buffer Base Address */
/* Flexible Host Filter Table */
#define E1000_FHFT(_n)	(0x09000 + ((_n) * 0x100))
/* Ext Flexible Host Filter Table */
#define E1000_FHFT_EXT(_n)	(0x09A00 + ((_n) * 0x100))


#define E1000_KMRNCTRLSTA	0x00034 /* MAC-PHY interface - RW */
#define E1000_MANC2H		0x05860 /* Management Control To Host - RW */
/* Management Decision Filters */
#define E1000_MDEF(_n)		(0x05890 + (4 * (_n)))
#define E1000_SW_FW_SYNC	0x05B5C /* SW-FW Synchronization - RW */
#define E1000_CCMCTL	0x05B48 /* CCM Control Register */
#define E1000_GIOCTL	0x05B44 /* GIO Analog Control Register */
#define E1000_SCCTL	0x05B4C /* PCIc PLL Configuration Register */
#define E1000_GCR	0x05B00 /* PCI-Ex Control */
#define E1000_GCR2	0x05B64 /* PCI-Ex Control #2 */
#define E1000_GSCL_1	0x05B10 /* PCI-Ex Statistic Control #1 */
#define E1000_GSCL_2	0x05B14 /* PCI-Ex Statistic Control #2 */
#define E1000_GSCL_3	0x05B18 /* PCI-Ex Statistic Control #3 */
#define E1000_GSCL_4	0x05B1C /* PCI-Ex Statistic Control #4 */
#define E1000_FACTPS	0x05B30 /* Function Active and Power State to MNG */
#define E1000_SWSM	0x05B50 /* SW Semaphore */
#define E1000_FWSM	0x05B54 /* FW Semaphore */
/* Driver-only SW semaphore (not used by BOOT agents) */
#define E1000_SWSM2	0x05B58
#define E1000_DCA_ID	0x05B70 /* DCA Requester ID Information - RO */
#define E1000_DCA_CTRL	0x05B74 /* DCA Control - RW */
#define E1000_UFUSE	0x05B78 /* UFUSE - RO */
#define E1000_FFLT_DBG	0x05F04 /* Debug Register */
#define E1000_HICR	0x08F00 /* Host Interface Control */
#define E1000_FWSTS	0x08F0C /* FW Status */

/* RSS registers */
#define E1000_CPUVEC	0x02C10 /* CPU Vector Register - RW */
#define E1000_MRQC	0x05818 /* Multiple Receive Control - RW */
#define E1000_IMIR(_i)	(0x05A80 + ((_i) * 4))  /* Immediate Interrupt */
#define E1000_IMIREXT(_i)	(0x05AA0 + ((_i) * 4)) /* Immediate INTR Ext*/
#define E1000_IMIRVP		0x05AC0 /* Immediate INT Rx VLAN Priority -RW */
#define E1000_MSIXBM(_i)	(0x01600 + ((_i) * 4)) /* MSI-X Alloc Reg -RW */
#define E1000_RETA(_i)	(0x05C00 + ((_i) * 4)) /* Redirection Table - RW */
#define E1000_RSSRK(_i)	(0x05C80 + ((_i) * 4)) /* RSS Random Key - RW */
#define E1000_RSSIM	0x05864 /* RSS Interrupt Mask */
#define E1000_RSSIR	0x05868 /* RSS Interrupt Request */
/* VT Registers */
#define E1000_SWPBS	0x03004 /* Switch Packet Buffer Size - RW */
#define E1000_MBVFICR	0x00C80 /* Mailbox VF Cause - RWC */
#define E1000_MBVFIMR	0x00C84 /* Mailbox VF int Mask - RW */
#define E1000_VFLRE	0x00C88 /* VF Register Events - RWC */
#define E1000_VFRE	0x00C8C /* VF Receive Enables */
#define E1000_VFTE	0x00C90 /* VF Transmit Enables */
#define E1000_QDE	0x02408 /* Queue Drop Enable - RW */
#define E1000_DTXSWC	0x03500 /* DMA Tx Switch Control - RW */
#define E1000_WVBR	0x03554 /* VM Wrong Behavior - RWS */
#define E1000_RPLOLR	0x05AF0 /* Replication Offload - RW */
#define E1000_UTA	0x0A000 /* Unicast Table Array - RW */
#define E1000_IOVCTL	0x05BBC /* IOV Control Register */
#define E1000_VMRCTL	0X05D80 /* Virtual Mirror Rule Control */
#define E1000_VMRVLAN	0x05D90 /* Virtual Mirror Rule VLAN */
#define E1000_VMRVM	0x05DA0 /* Virtual Mirror Rule VM */
#define E1000_MDFB	0x03558 /* Malicious Driver free block */
#define E1000_LVMMC	0x03548 /* Last VM Misbehavior cause */
#define E1000_TXSWC	0x05ACC /* Tx Switch Control */
#define E1000_SCCRL	0x05DB0 /* Storm Control Control */
#define E1000_BSCTRH	0x05DB8 /* Broadcast Storm Control Threshold */
#define E1000_MSCTRH	0x05DBC /* Multicast Storm Control Threshold */
/* These act per VF so an array friendly macro is used */
#define E1000_V2PMAILBOX(_n)	(0x00C40 + (4 * (_n)))
#define E1000_P2VMAILBOX(_n)	(0x00C00 + (4 * (_n)))
#define E1000_VMBMEM(_n)	(0x00800 + (64 * (_n)))
#define E1000_VFVMBMEM(_n)	(0x00800 + (_n))
#define E1000_VMOLR(_n)		(0x05AD0 + (4 * (_n)))
/* VLAN Virtual Machine Filter - RW */
#define E1000_VLVF(_n)		(0x05D00 + (4 * (_n)))
#define E1000_VMVIR(_n)		(0x03700 + (4 * (_n)))
#define E1000_DVMOLR(_n)	(0x0C038 + (0x40 * (_n))) /* DMA VM offload */
#define E1000_VTCTRL(_n)	(0x10000 + (0x100 * (_n))) /* VT Control */
#define E1000_TSYNCRXCTL	0x0B620 /* Rx Time Sync Control register - RW */
#define E1000_TSYNCTXCTL	0x0B614 /* Tx Time Sync Control register - RW */
#define E1000_TSYNCRXCFG	0x05F50 /* Time Sync Rx Configuration - RW */
#define E1000_RXSTMPL	0x0B624 /* Rx timestamp Low - RO */
#define E1000_RXSTMPH	0x0B628 /* Rx timestamp High - RO */
#define E1000_RXSATRL	0x0B62C /* Rx timestamp attribute low - RO */
#define E1000_RXSATRH	0x0B630 /* Rx timestamp attribute high - RO */
#define E1000_TXSTMPL	0x0B618 /* Tx timestamp value Low - RO */
#define E1000_TXSTMPH	0x0B61C /* Tx timestamp value High - RO */
#define E1000_SYSTIML	0x0B600 /* System time register Low - RO */
#define E1000_SYSTIMH	0x0B604 /* System time register High - RO */
#define E1000_TIMINCA	0x0B608 /* Increment attributes register - RW */
#define E1000_TIMADJL	0x0B60C /* Time sync time adjustment offset Low - RW */
#define E1000_TIMADJH	0x0B610 /* Time sync time adjustment offset High - RW */
#define E1000_TSAUXC	0x0B640 /* Timesync Auxiliary Control register */
#define	E1000_SYSSTMPL	0x0B648 /* HH Timesync system stamp low register */
#define	E1000_SYSSTMPH	0x0B64C /* HH Timesync system stamp hi register */
#define	E1000_PLTSTMPL	0x0B640 /* HH Timesync platform stamp low register */
#define	E1000_PLTSTMPH	0x0B644 /* HH Timesync platform stamp hi register */
#define E1000_SYSTIMR	0x0B6F8 /* System time register Residue */
#define E1000_TSICR	0x0B66C /* Interrupt Cause Register */
#define E1000_TSIM	0x0B674 /* Interrupt Mask Register */
#define E1000_RXMTRL	0x0B634 /* Time sync Rx EtherType and Msg Type - RW */
#define E1000_RXUDP	0x0B638 /* Time Sync Rx UDP Port - RW */

/* Filtering Registers */
#define E1000_SAQF(_n)	(0x05980 + (4 * (_n))) /* Source Address Queue Fltr */
#define E1000_DAQF(_n)	(0x059A0 + (4 * (_n))) /* Dest Address Queue Fltr */
#define E1000_SPQF(_n)	(0x059C0 + (4 * (_n))) /* Source Port Queue Fltr */
#define E1000_FTQF(_n)	(0x059E0 + (4 * (_n))) /* 5-tuple Queue Fltr */
#define E1000_TTQF(_n)	(0x059E0 + (4 * (_n))) /* 2-tuple Queue Fltr */
#define E1000_SYNQF(_n)	(0x055FC + (4 * (_n))) /* SYN Packet Queue Fltr */
#define E1000_ETQF(_n)	(0x05CB0 + (4 * (_n))) /* EType Queue Fltr */

#define E1000_RTTDCS	0x3600 /* Reedtown Tx Desc plane control and status */
#define E1000_RTTPCS	0x3474 /* Reedtown Tx Packet Plane control and status */
#define E1000_RTRPCS	0x2474 /* Rx packet plane control and status */
#define E1000_RTRUP2TC	0x05AC4 /* Rx User Priority to Traffic Class */
#define E1000_RTTUP2TC	0x0418 /* Transmit User Priority to Traffic Class */
/* Tx Desc plane TC Rate-scheduler config */
#define E1000_RTTDTCRC(_n)	(0x3610 + ((_n) * 4))
/* Tx Packet plane TC Rate-Scheduler Config */
#define E1000_RTTPTCRC(_n)	(0x3480 + ((_n) * 4))
/* Rx Packet plane TC Rate-Scheduler Config */
#define E1000_RTRPTCRC(_n)	(0x2480 + ((_n) * 4))
/* Tx Desc Plane TC Rate-Scheduler Status */
#define E1000_RTTDTCRS(_n)	(0x3630 + ((_n) * 4))
/* Tx Desc Plane TC Rate-Scheduler MMW */
#define E1000_RTTDTCRM(_n)	(0x3650 + ((_n) * 4))
/* Tx Packet plane TC Rate-Scheduler Status */
#define E1000_RTTPTCRS(_n)	(0x34A0 + ((_n) * 4))
/* Tx Packet plane TC Rate-scheduler MMW */
#define E1000_RTTPTCRM(_n)	(0x34C0 + ((_n) * 4))
/* Rx Packet plane TC Rate-Scheduler Status */
#define E1000_RTRPTCRS(_n)	(0x24A0 + ((_n) * 4))
/* Rx Packet plane TC Rate-Scheduler MMW */
#define E1000_RTRPTCRM(_n)	(0x24C0 + ((_n) * 4))
/* Tx Desc plane VM Rate-Scheduler MMW*/
#define E1000_RTTDVMRM(_n)	(0x3670 + ((_n) * 4))
/* Tx BCN Rate-Scheduler MMW */
#define E1000_RTTBCNRM(_n)	(0x3690 + ((_n) * 4))
#define E1000_RTTDQSEL	0x3604  /* Tx Desc Plane Queue Select */
#define E1000_RTTDVMRC	0x3608  /* Tx Desc Plane VM Rate-Scheduler Config */
#define E1000_RTTDVMRS	0x360C  /* Tx Desc Plane VM Rate-Scheduler Status */
#define E1000_RTTBCNRC	0x36B0  /* Tx BCN Rate-Scheduler Config */
#define E1000_RTTBCNRS	0x36B4  /* Tx BCN Rate-Scheduler Status */
#define E1000_RTTBCNCR	0xB200  /* Tx BCN Control Register */
#define E1000_RTTBCNTG	0x35A4  /* Tx BCN Tagging */
#define E1000_RTTBCNCP	0xB208  /* Tx BCN Congestion point */
#define E1000_RTRBCNCR	0xB20C  /* Rx BCN Control Register */
#define E1000_RTTBCNRD	0x36B8  /* Tx BCN Rate Drift */
#define E1000_PFCTOP	0x1080  /* Priority Flow Control Type and Opcode */
#define E1000_RTTBCNIDX	0xB204  /* Tx BCN Congestion Point */
#define E1000_RTTBCNACH	0x0B214 /* Tx BCN Control High */
#define E1000_RTTBCNACL	0x0B210 /* Tx BCN Control Low */

/* DMA Coalescing registers */
#define E1000_DMACR	0x02508 /* Control Register */
#define E1000_DMCTXTH	0x03550 /* Transmit Threshold */
#define E1000_DMCTLX	0x02514 /* Time to Lx Request */
#define E1000_DMCRTRH	0x05DD0 /* Receive Packet Rate Threshold */
#define E1000_DMCCNT	0x05DD4 /* Current Rx Count */
#define E1000_FCRTC	0x02170 /* Flow Control Rx high watermark */
#define E1000_PCIEMISC	0x05BB8 /* PCIE misc config register */

/* PCIe Parity Status Register */
#define E1000_PCIEERRSTS	0x05BA8

#define E1000_PROXYS	0x5F64 /* Proxying Status */
#define E1000_PROXYFC	0x5F60 /* Proxying Filter Control */
/* Thermal sensor configuration and status registers */
#define E1000_THMJT	0x08100 /* Junction Temperature */
#define E1000_THLOWTC	0x08104 /* Low Threshold Control */
#define E1000_THMIDTC	0x08108 /* Mid Threshold Control */
#define E1000_THHIGHTC	0x0810C /* High Threshold Control */
#define E1000_THSTAT	0x08110 /* Thermal Sensor Status */

/* Energy Efficient Ethernet "EEE" registers */
#define E1000_IPCNFG	0x0E38 /* Internal PHY Configuration */
#define E1000_LTRC	0x01A0 /* Latency Tolerance Reporting Control */
#define E1000_EEER	0x0E30 /* Energy Efficient Ethernet "EEE"*/
#define E1000_EEE_SU	0x0E34 /* EEE Setup */
#define E1000_TLPIC	0x4148 /* EEE Tx LPI Count - TLPIC */
#define E1000_RLPIC	0x414C /* EEE Rx LPI Count - RLPIC */

/* OS2BMC Registers */
#define E1000_B2OSPC	0x08FE0 /* BMC2OS packets sent by BMC */
#define E1000_B2OGPRC	0x04158 /* BMC2OS packets received by host */
#define E1000_O2BGPTC	0x08FE4 /* OS2BMC packets received by BMC */
#define E1000_O2BSPC	0x0415C /* OS2BMC packets transmitted by host */

#define E1000_DOBFFCTL	0x3F24 /* DMA OBFF Control Register */


#endif
