/*	$OpenBSD: igc_regs.h,v 1.3 2024/05/06 04:25:52 dlg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

#ifndef _IGC_REGS_H_
#define _IGC_REGS_H_

/* General Register Descriptions */
#define IGC_CTRL		0x00000	/* Device Control - RW */
#define IGC_STATUS		0x00008	/* Device Status - RO */
#define IGC_EECD		0x00010	/* EEPROM/Flash Control - RW */
/* NVM  Register Descriptions */
#define IGC_EERD		0x12014	/* EEprom mode read - RW */
#define IGC_EEWR		0x12018	/* EEprom mode write - RW */
#define IGC_CTRL_EXT		0x00018	/* Extended Device Control - RW */
#define IGC_MDIC		0x00020	/* MDI Control - RW */
#define IGC_MDICNFG		0x00E04	/* MDI Config - RW */
#define IGC_FCAL		0x00028	/* Flow Control Address Low - RW */
#define IGC_FCAH		0x0002C	/* Flow Control Address High -RW */
#define IGC_I225_FLSWCTL	0x12048	/* FLASH control register */
#define IGC_I225_FLSWDATA	0x1204C	/* FLASH data register */
#define IGC_I225_FLSWCNT	0x12050	/* FLASH Access Counter */
#define IGC_I225_FLSECU		0x12114	/* FLASH Security */
#define IGC_FCT			0x00030	/* Flow Control Type - RW */
#define IGC_CONNSW		0x00034	/* Copper/Fiber switch control - RW */
#define IGC_VET			0x00038	/* VLAN Ether Type - RW */
#define IGC_ICR			0x01500	/* Intr Cause Read - RC/W1C */
#define IGC_ITR			0x000C4	/* Interrupt Throttling Rate - RW */
#define IGC_ICS			0x01504	/* Intr Cause Set - WO */
#define IGC_IMS			0x01508	/* Intr Mask Set/Read - RW */
#define IGC_IMC			0x0150C	/* Intr Mask Clear - WO */
#define IGC_IAM			0x01510	/* Intr Ack Auto Mask- RW */
#define IGC_RCTL		0x00100	/* Rx Control - RW */
#define IGC_FCTTV		0x00170	/* Flow Control Transmit Timer Value */
#define IGC_TXCW		0x00178	/* Tx Configuration Word - RW */
#define IGC_RXCW		0x00180	/* Rx Configuration Word - RO */
#define IGC_EICR		0x01580	/* Ext. Interrupt Cause Read - R/clr */
#define IGC_EITR(_n)		(0x01680 + (0x4 * (_n)))
#define IGC_EICS		0x01520	/* Ext. Interrupt Cause Set - W0 */
#define IGC_EIMS		0x01524	/* Ext. Interrupt Mask Set/Read - RW */
#define IGC_EIMC		0x01528	/* Ext. Interrupt Mask Clear - WO */
#define IGC_EIAC		0x0152C	/* Ext. Interrupt Auto Clear - RW */
#define IGC_EIAM		0x01530	/* Ext. Interrupt Ack Auto Clear
					 * Mask */
#define IGC_GPIE		0x01514	/* General Purpose Interrupt Enable 
					 * - RW */
#define IGC_IVAR0		0x01700	/* Interrupt Vector Allocation (array)
					 * - RW */
#define IGC_IVAR_MISC		0x01740	/* IVAR for "other" causes - RW */
#define IGC_TCTL		0x00400	/* Tx Control - RW */
#define IGC_TCTL_EXT		0x00404	/* Extended Tx Control - RW */
#define IGC_TIPG		0x00410	/* Tx Inter-packet gap -RW */
#define IGC_AIT			0x00458	/* Adaptive Interframe Spacing
					 * Throttle - RW */
#define IGC_LEDCTL		0x00E00	/* LED Control - RW */
#define IGC_LEDMUX		0x08130	/* LED MUX Control */
#define IGC_EXTCNF_CTRL		0x00F00	/* Extended Configuration Control */
#define IGC_EXTCNF_SIZE		0x00F08	/* Extended Configuration Size */
#define IGC_PHY_CTRL		0x00F10	/* PHY Control Register in CSR */
#define IGC_PBA			0x01000	/* Packet Buffer Allocation - RW */
#define IGC_PBS			0x01008	/* Packet Buffer Size */
#define IGC_EEMNGCTL		0x01010	/* MNG EEprom Control */
#define IGC_EEMNGCTL_I225	0x01010	/* i225 MNG EEprom Mode Control */
#define IGC_EEARBC_I225		0x12024	/* EEPROM Auto Read Bus Control */
#define IGC_FLOP		0x0103C	/* FLASH Opcode Register */
#define IGC_WDSTP		0x01040	/* Watchdog Setup - RW */
#define IGC_SWDSTS		0x01044	/* SW Device Status - RW */
#define IGC_FRTIMER		0x01048	/* Free Running Timer - RW */
#define IGC_TCPTIMER		0x0104C	/* TCP Timer - RW */
#define IGC_ERT			0x02008	/* Early Rx Threshold - RW */
#define IGC_FCRTL		0x02160	/* Flow Control Receive Threshold Low
					 * - RW */
#define IGC_FCRTH		0x02168	/* Flow Control Receive Threshold High
					 * - RW */
#define IGC_PSRCTL		0x02170	/* Packet Split Receive Control - RW */
#define IGC_RDFH		0x02410	/* Rx Data FIFO Head - RW */
#define IGC_RDFT		0x02418	/* Rx Data FIFO Tail - RW */
#define IGC_RDFHS		0x02420	/* Rx Data FIFO Head Saved - RW */
#define IGC_RDFTS		0x02428	/* Rx Data FIFO Tail Saved - RW */
#define IGC_RDFPC		0x02430	/* Rx Data FIFO Packet Count - RW */
#define IGC_PBRTH		0x02458	/* PB Rx Arbitration Threshold - RW */
#define IGC_FCRTV		0x02460	/* Flow Control Refresh Timer Value
					 * - RW */
/* Split and Replication Rx Control - RW */
#define IGC_RXPBS		0x02404	/* Rx Packet Buffer Size - RW */
#define IGC_RDTR		0x02820	/* Rx Delay Timer - RW */
#define IGC_RADV		0x0282C	/* Rx Interrupt Absolute Delay Timer
					 * - RW */
/* Shadow Ram Write Register - RW */
#define IGC_SRWR		0x12018
#define IGC_EEC_REG		0x12010


#define IGC_SHADOWINF		0x12068
#define IGC_FLFWUPDATE		0x12108

#define IGC_INVM_DATA_REG(_n)	(0x12120 + 4*(_n))
#define IGC_INVM_SIZE		64	/* Number of INVM Data Registers */

#define IGC_MMDAC		13	/* MMD Access Control */
#define IGC_MMDAAD		14	/* MMD Access Address/Data */
/* Convenience macros
 *
 * Note: "_n" is the queue number of the register to be written to.
 *
 * Example usage:
 * IGC_RDBAL_REG(current_rx_queue)
 */
#define IGC_RDBAL(_n)	((_n) < 4 ? (0x02800 + ((_n) * 0x100)) : \
			(0x0C000 + ((_n) * 0x40)))
#define IGC_RDBAH(_n)	((_n) < 4 ? (0x02804 + ((_n) * 0x100)) : \
			(0x0C004 + ((_n) * 0x40)))
#define IGC_RDLEN(_n)	((_n) < 4 ? (0x02808 + ((_n) * 0x100)) : \
			(0x0C008 + ((_n) * 0x40)))
#define IGC_SRRCTL(_n)	((_n) < 4 ? (0x0280C + ((_n) * 0x100)) : \
			(0x0C00C + ((_n) * 0x40)))
#define IGC_RDH(_n)	((_n) < 4 ? (0x02810 + ((_n) * 0x100)) : \
			(0x0C010 + ((_n) * 0x40)))
#define IGC_RDT(_n)	((_n) < 4 ? (0x02818 + ((_n) * 0x100)) : \
			(0x0C018 + ((_n) * 0x40)))
#define IGC_RXDCTL(_n)	((_n) < 4 ? (0x02828 + ((_n) * 0x100)) : \
			(0x0C028 + ((_n) * 0x40)))
#define IGC_RQDPC(_n)	((_n) < 4 ? (0x02830 + ((_n) * 0x100)) : \
			(0x0C030 + ((_n) * 0x40)))
#define IGC_TDBAL(_n)	((_n) < 4 ? (0x03800 + ((_n) * 0x100)) : \
			(0x0E000 + ((_n) * 0x40)))
#define IGC_TDBAH(_n)	((_n) < 4 ? (0x03804 + ((_n) * 0x100)) : \
			(0x0E004 + ((_n) * 0x40)))
#define IGC_TDLEN(_n)	((_n) < 4 ? (0x03808 + ((_n) * 0x100)) : \
			(0x0E008 + ((_n) * 0x40)))
#define IGC_TDH(_n)	((_n) < 4 ? (0x03810 + ((_n) * 0x100)) : \
			(0x0E010 + ((_n) * 0x40)))
#define IGC_TDT(_n)	((_n) < 4 ? (0x03818 + ((_n) * 0x100)) : \
			(0x0E018 + ((_n) * 0x40)))
#define IGC_TXDCTL(_n)	((_n) < 4 ? (0x03828 + ((_n) * 0x100)) : \
			(0x0E028 + ((_n) * 0x40)))
#define IGC_TARC(_n)		(0x03840 + ((_n) * 0x100))
#define IGC_RSRPD		0x02C00	/* Rx Small Packet Detect - RW */
#define IGC_RAID		0x02C08	/* Receive Ack Interrupt Delay - RW */
#define IGC_KABGTXD		0x03004	/* AFE Band Gap Transmit Ref Data */
#define IGC_PSRTYPE(_i)		(0x05480 + ((_i) * 4))
#define IGC_RAL(_i)		(((_i) <= 15) ? (0x05400 + ((_i) * 8)) : \
				(0x054E0 + ((_i - 16) * 8)))
#define IGC_RAH(_i)		(((_i) <= 15) ? (0x05404 + ((_i) * 8)) : \
				(0x054E4 + ((_i - 16) * 8)))
#define IGC_VLANPQF		0x055B0	/* VLAN Priority Queue Filter VLAPQF */

#define IGC_SHRAL(_i)		(0x05438 + ((_i) * 8))
#define IGC_SHRAH(_i)		(0x0543C + ((_i) * 8))
#define IGC_IP4AT_REG(_i)	(0x05840 + ((_i) * 8))
#define IGC_IP6AT_REG(_i)	(0x05880 + ((_i) * 4))
#define IGC_WUPM_REG(_i)	(0x05A00 + ((_i) * 4))
#define IGC_FFMT_REG(_i)	(0x09000 + ((_i) * 8))
#define IGC_FFVT_REG(_i)	(0x09800 + ((_i) * 8))
#define IGC_FFLT_REG(_i)	(0x05F00 + ((_i) * 8))
#define IGC_TXPBS		0x03404  /* Tx Packet Buffer Size - RW */
#define IGC_TIDV		0x03820  /* Tx Interrupt Delay Value - RW */
#define IGC_TADV		0x0382C  /* Tx Interrupt Absolute Delay Val - RW */
/* Statistics Register Descriptions */
#define IGC_CRCERRS		0x04000  /* CRC Error Count - R/clr */
#define IGC_ALGNERRC		0x04004  /* Alignment Error Count - R/clr */
#define IGC_RXERRC		0x04004  /* Receive Error Count - R/clr */
#define IGC_MPC			0x04010  /* Missed Packet Count - R/clr */
#define IGC_SCC			0x04014  /* Single Collision Count - R/clr */
#define IGC_ECOL		0x04018  /* Excessive Collision Count - R/clr */
#define IGC_MCC			0x0401C  /* Multiple Collision Count - R/clr */
#define IGC_LATECOL		0x04020  /* Late Collision Count - R/clr */
#define IGC_COLC		0x04028  /* Collision Count - R/clr */
#define IGC_RERC		0x0402C  /* Receive Error Count - R/clr */
#define IGC_DC			0x04030  /* Defer Count - R/clr */
#define IGC_TNCRS		0x04034  /* Tx-No CRS - R/clr */
#define IGC_HTDPMC		0x0403C  /* Host Transmit Discarded by MAC - R/clr */
#define IGC_RLEC		0x04040  /* Receive Length Error Count - R/clr */
#define IGC_XONRXC		0x04048  /* XON Rx Count - R/clr */
#define IGC_XONTXC		0x0404C  /* XON Tx Count - R/clr */
#define IGC_XOFFRXC		0x04050  /* XOFF Rx Count - R/clr */
#define IGC_XOFFTXC		0x04054  /* XOFF Tx Count - R/clr */
#define IGC_FCRUC		0x04058  /* Flow Control Rx Unsupported Count- R/clr */
#define IGC_PRC64		0x0405C  /* Packets Rx (64 bytes) - R/clr */
#define IGC_PRC127		0x04060  /* Packets Rx (65-127 bytes) - R/clr */
#define IGC_PRC255		0x04064  /* Packets Rx (128-255 bytes) - R/clr */
#define IGC_PRC511		0x04068  /* Packets Rx (255-511 bytes) - R/clr */
#define IGC_PRC1023		0x0406C  /* Packets Rx (512-1023 bytes) - R/clr */
#define IGC_PRC1522		0x04070  /* Packets Rx (1024-1522 bytes) - R/clr */
#define IGC_GPRC		0x04074  /* Good Packets Rx Count - R/clr */
#define IGC_BPRC		0x04078  /* Broadcast Packets Rx Count - R/clr */
#define IGC_MPRC		0x0407C  /* Multicast Packets Rx Count - R/clr */
#define IGC_GPTC		0x04080  /* Good Packets Tx Count - R/clr */
#define IGC_GORCL		0x04088  /* Good Octets Rx Count Low - R/clr */
#define IGC_GORCH		0x0408C  /* Good Octets Rx Count High - R/clr */
#define IGC_GOTCL		0x04090  /* Good Octets Tx Count Low - R/clr */
#define IGC_GOTCH		0x04094  /* Good Octets Tx Count High - R/clr */
#define IGC_RNBC		0x040A0  /* Rx No Buffers Count - R/clr */
#define IGC_RUC			0x040A4  /* Rx Undersize Count - R/clr */
#define IGC_RFC			0x040A8  /* Rx Fragment Count - R/clr */
#define IGC_ROC			0x040AC  /* Rx Oversize Count - R/clr */
#define IGC_RJC			0x040B0  /* Rx Jabber Count - R/clr */
#define IGC_MGTPRC		0x040B4  /* Management Packets Rx Count - R/clr */
#define IGC_MGTPDC		0x040B8  /* Management Packets Dropped Count - R/clr */
#define IGC_MGTPTC		0x040BC  /* Management Packets Tx Count - R/clr */
#define IGC_TORL		0x040C0  /* Total Octets Rx Low - R/clr */
#define IGC_TORH		0x040C4  /* Total Octets Rx High - R/clr */
#define IGC_TOTL		0x040C8  /* Total Octets Tx Low - R/clr */
#define IGC_TOTH		0x040CC  /* Total Octets Tx High - R/clr */
#define IGC_TPR			0x040D0  /* Total Packets Rx - R/clr */
#define IGC_TPT			0x040D4  /* Total Packets Tx - R/clr */
#define IGC_PTC64		0x040D8  /* Packets Tx (64 bytes) - R/clr */
#define IGC_PTC127		0x040DC  /* Packets Tx (65-127 bytes) - R/clr */
#define IGC_PTC255		0x040E0  /* Packets Tx (128-255 bytes) - R/clr */
#define IGC_PTC511		0x040E4  /* Packets Tx (256-511 bytes) - R/clr */
#define IGC_PTC1023		0x040E8  /* Packets Tx (512-1023 bytes) - R/clr */
#define IGC_PTC1522		0x040EC  /* Packets Tx (1024-1522 Bytes) - R/clr */
#define IGC_MPTC		0x040F0  /* Multicast Packets Tx Count - R/clr */
#define IGC_BPTC		0x040F4  /* Broadcast Packets Tx Count - R/clr */
#define IGC_TSCTC		0x040F8  /* TCP Segmentation Context Tx - R/clr */
#define IGC_IAC			0x04100  /* Interrupt Assertion Count */
#define IGC_RPTHC		0x04104  /* Rx Packets To Host */
#define IGC_HGPTC		0x04118  /* Host Good Packets Tx Count */
#define IGC_RXDMTC		0x04120  /* Rx Descriptor Minimum Threshold Count */
#define IGC_HGORCL		0x04128  /* Host Good Octets Received Count Low */
#define IGC_HGORCH		0x0412C  /* Host Good Octets Received Count High */
#define IGC_HGOTCL		0x04130  /* Host Good Octets Transmit Count Low */
#define IGC_HGOTCH		0x04134  /* Host Good Octets Transmit Count High */
#define IGC_LENERRS		0x04138  /* Length Errors Count */

#define IGC_VFGPRC		0x00F10
#define IGC_VFGORC		0x00F18
#define IGC_VFMPRC		0x00F3C
#define IGC_VFGPTC		0x00F14
#define IGC_VFGOTC		0x00F34
#define IGC_VFGOTLBC		0x00F50
#define IGC_VFGPTLBC		0x00F44
#define IGC_VFGORLBC		0x00F48
#define IGC_VFGPRLBC		0x00F40

#define IGC_PCS_ANADV		0x04218  /* AN advertisement - RW */
#define IGC_PCS_LPAB		0x0421C  /* Link Partner Ability - RW */
#define IGC_RXCSUM		0x05000  /* Rx Checksum Control - RW */
#define IGC_RLPML		0x05004  /* Rx Long Packet Max Length */
#define IGC_RFCTL		0x05008  /* Receive Filter Control*/
#define IGC_MTA			0x05200  /* Multicast Table Array - RW Array */
#define IGC_RA			0x05400  /* Receive Address - RW Array */
#define IGC_VFTA		0x05600  /* VLAN Filter Table Array - RW Array */
#define IGC_WUC			0x05800  /* Wakeup Control - RW */
#define IGC_WUFC		0x05808  /* Wakeup Filter Control - RW */
#define IGC_WUS			0x05810  /* Wakeup Status - RO */
/* Management registers */
#define IGC_MANC		0x05820  /* Management Control - RW */
#define IGC_IPAV		0x05838  /* IP Address Valid - RW */
#define IGC_IP4AT		0x05840  /* IPv4 Address Table - RW Array */
#define IGC_IP6AT		0x05880  /* IPv6 Address Table - RW Array */
#define IGC_WUPL		0x05900  /* Wakeup Packet Length - RW */
#define IGC_WUPM		0x05A00  /* Wakeup Packet Memory - RO A */
#define IGC_WUPM_EXT		0x0B800  /* Wakeup Packet Memory Extended - RO Array */
#define IGC_WUFC_EXT		0x0580C  /* Wakeup Filter Control Extended - RW */
#define IGC_WUS_EXT		0x05814  /* Wakeup Status Extended - RW1C */
#define IGC_FHFTSL		0x05804  /* Flex Filter Indirect Table Select - RW */
#define IGC_PROXYFCEX		0x05590  /* Proxy Filter Control Extended - RW1C */
#define IGC_PROXYEXS		0x05594  /* Proxy Extended Status - RO */
#define IGC_WFUTPF		0x05500  /* Wake Flex UDP TCP Port Filter - RW Array */
#define IGC_RFUTPF		0x05580  /* Range Flex UDP TCP Port Filter - RW */
#define IGC_RWPFC		0x05584  /* Range Wake Port Filter Control - RW */
#define IGC_WFUTPS		0x05588  /* Wake Filter UDP TCP Status - RW1C */
#define IGC_WCS			0x0558C  /* Wake Control Status - RW1C */
/* MSI-X Table Register Descriptions */
#define IGC_PBACL		0x05B68  /* MSIx PBA Clear - Read/Write 1's to clear */
#define IGC_FFLT		0x05F00  /* Flexible Filter Length Table - RW Array */
#define IGC_HOST_IF		0x08800  /* Host Interface */
/* Flexible Host Filter Table */
#define IGC_FHFT(_n)		(0x09000 + ((_n) * 0x100))
/* Ext Flexible Host Filter Table */
#define IGC_FHFT_EXT(_n)	(0x09A00 + ((_n) * 0x100))


#define IGC_KMRNCTRLSTA		0x00034 /* MAC-PHY interface - RW */
#define IGC_MANC2H		0x05860 /* Management Control To Host - RW */
/* Management Decision Filters */
#define IGC_MDEF(_n)		(0x05890 + (4 * (_n)))
/* Semaphore registers */
#define IGC_SW_FW_SYNC		0x05B5C /* SW-FW Synchronization - RW */
/* Function Active and Power State to MNG */
#define IGC_FACTPS		0x05B30
#define IGC_SWSM		0x05B50 /* SW Semaphore */
#define IGC_FWSM		0x05B54 /* FW Semaphore */
/* Driver-only SW semaphore (not used by BOOT agents) */
#define IGC_SWSM2		0x05B58
#define IGC_FFLT_DBG		0x05F04 /* Debug Register */
#define IGC_HICR		0x08F00 /* Host Interface Control */
#define IGC_FWSTS		0x08F0C /* FW Status */

/* RSS registers */
#define IGC_MRQC	0x05818 /* Multiple Receive Control - RW */
#define IGC_IMIR(_i)	(0x05A80 + ((_i) * 4))  /* Immediate Interrupt */
#define IGC_IMIREXT(_i)	(0x05AA0 + ((_i) * 4)) /* Immediate INTR Ext*/
#define IGC_IMIRVP	0x05AC0 /* Immediate INT Rx VLAN Priority -RW */
#define IGC_MSIXBM(_i)	(0x01600 + ((_i) * 4)) /* MSI-X Alloc Reg -RW */
/* Redirection Table - RW Array */
#define IGC_RETA(_i)	(0x05C00 + ((_i) * 4))
/* RSS Random Key - RW Array */
#define IGC_RSSRK(_i)	(0x05C80 + ((_i) * 4))
#define IGC_RSSIM	0x05864 /* RSS Interrupt Mask */
#define IGC_RSSIR	0x05868 /* RSS Interrupt Request */
#define IGC_UTA		0x0A000 /* Unicast Table Array - RW */
#define IGC_TSYNCRXCTL	0x0B620 /* Rx Time Sync Control register - RW */
#define IGC_TSYNCTXCTL	0x0B614 /* Tx Time Sync Control register - RW */
#define IGC_TSYNCRXCFG	0x05F50 /* Time Sync Rx Configuration - RW */
#define IGC_RXSTMPL	0x0B624 /* Rx timestamp Low - RO */
#define IGC_RXSTMPH	0x0B628 /* Rx timestamp High - RO */
#define IGC_RXSATRL	0x0B62C /* Rx timestamp attribute low - RO */
#define IGC_RXSATRH	0x0B630 /* Rx timestamp attribute high - RO */
#define IGC_TXSTMPL	0x0B618 /* Tx timestamp value Low - RO */
#define IGC_TXSTMPH	0x0B61C /* Tx timestamp value High - RO */
#define IGC_SYSTIML	0x0B600 /* System time register Low - RO */
#define IGC_SYSTIMH	0x0B604 /* System time register High - RO */
#define IGC_TIMINCA	0x0B608 /* Increment attributes register - RW */
#define IGC_TIMADJL	0x0B60C /* Time sync time adjustment offset Low - RW */
#define IGC_TIMADJH	0x0B610 /* Time sync time adjustment offset High - RW */
#define IGC_TSAUXC	0x0B640 /* Timesync Auxiliary Control register */
#define IGC_SYSTIMR	0x0B6F8 /* System time register Residue */
#define IGC_TSICR	0x0B66C /* Interrupt Cause Register */
#define IGC_TSIM	0x0B674 /* Interrupt Mask Register */

/* Filtering Registers */
#define IGC_SAQF(_n)	(0x05980 + (4 * (_n))) /* Source Address Queue Fltr */
#define IGC_DAQF(_n)	(0x059A0 + (4 * (_n))) /* Dest Address Queue Fltr */
#define IGC_SPQF(_n)	(0x059C0 + (4 * (_n))) /* Source Port Queue Fltr */
#define IGC_FTQF(_n)	(0x059E0 + (4 * (_n))) /* 5-tuple Queue Fltr */
#define IGC_TTQF(_n)	(0x059E0 + (4 * (_n))) /* 2-tuple Queue Fltr */
#define IGC_SYNQF(_n)	(0x055FC + (4 * (_n))) /* SYN Packet Queue Fltr */
#define IGC_ETQF(_n)	(0x05CB0 + (4 * (_n))) /* EType Queue Fltr */

/* ETQF register bit definitions */
#define IGC_ETQF_FILTER_ENABLE	(1 << 26)
#define IGC_ETQF_IMM_INT	(1 << 29)
#define IGC_ETQF_QUEUE_ENABLE	(1U << 31)
#define IGC_ETQF_QUEUE_SHIFT	16
#define IGC_ETQF_QUEUE_MASK	0x00070000
#define IGC_ETQF_ETYPE_MASK	0x0000FFFF

#define IGC_RTTDCS		0x3600 /* Reedtown Tx Desc plane control and status */
#define IGC_RTTPCS		0x3474 /* Reedtown Tx Packet Plane control and status */
#define IGC_RTRPCS		0x2474 /* Rx packet plane control and status */
#define IGC_RTRUP2TC		0x05AC4 /* Rx User Priority to Traffic Class */
#define IGC_RTTUP2TC		0x0418 /* Transmit User Priority to Traffic Class */
/* Tx Desc plane TC Rate-scheduler config */
#define IGC_RTTDTCRC(_n)	(0x3610 + ((_n) * 4))
/* Tx Packet plane TC Rate-Scheduler Config */
#define IGC_RTTPTCRC(_n)	(0x3480 + ((_n) * 4))
/* Rx Packet plane TC Rate-Scheduler Config */
#define IGC_RTRPTCRC(_n)	(0x2480 + ((_n) * 4))
/* Tx Desc Plane TC Rate-Scheduler Status */
#define IGC_RTTDTCRS(_n)	(0x3630 + ((_n) * 4))
/* Tx Desc Plane TC Rate-Scheduler MMW */
#define IGC_RTTDTCRM(_n)	(0x3650 + ((_n) * 4))
/* Tx Packet plane TC Rate-Scheduler Status */
#define IGC_RTTPTCRS(_n)	(0x34A0 + ((_n) * 4))
/* Tx Packet plane TC Rate-scheduler MMW */
#define IGC_RTTPTCRM(_n)	(0x34C0 + ((_n) * 4))
/* Rx Packet plane TC Rate-Scheduler Status */
#define IGC_RTRPTCRS(_n)	(0x24A0 + ((_n) * 4))
/* Rx Packet plane TC Rate-Scheduler MMW */
#define IGC_RTRPTCRM(_n)	(0x24C0 + ((_n) * 4))
/* Tx Desc plane VM Rate-Scheduler MMW*/
#define IGC_RTTDVMRM(_n)	(0x3670 + ((_n) * 4))
/* Tx BCN Rate-Scheduler MMW */
#define IGC_RTTBCNRM(_n)	(0x3690 + ((_n) * 4))
#define IGC_RTTDQSEL		0x3604  /* Tx Desc Plane Queue Select */
#define IGC_RTTDVMRC		0x3608  /* Tx Desc Plane VM Rate-Scheduler Config */
#define IGC_RTTDVMRS		0x360C  /* Tx Desc Plane VM Rate-Scheduler Status */
#define IGC_RTTBCNRC		0x36B0  /* Tx BCN Rate-Scheduler Config */
#define IGC_RTTBCNRS		0x36B4  /* Tx BCN Rate-Scheduler Status */
#define IGC_RTTBCNCR		0xB200  /* Tx BCN Control Register */
#define IGC_RTTBCNTG		0x35A4  /* Tx BCN Tagging */
#define IGC_RTTBCNCP		0xB208  /* Tx BCN Congestion point */
#define IGC_RTRBCNCR		0xB20C  /* Rx BCN Control Register */
#define IGC_RTTBCNRD		0x36B8  /* Tx BCN Rate Drift */
#define IGC_PFCTOP		0x1080  /* Priority Flow Control Type and Opcode */
#define IGC_RTTBCNIDX		0xB204  /* Tx BCN Congestion Point */
#define IGC_RTTBCNACH		0x0B214 /* Tx BCN Control High */
#define IGC_RTTBCNACL		0x0B210 /* Tx BCN Control Low */

/* DMA Coalescing registers */
#define IGC_DMACR		0x02508 /* Control Register */
#define IGC_DMCTXTH		0x03550 /* Transmit Threshold */
#define IGC_DMCTLX		0x02514 /* Time to Lx Request */
#define IGC_DMCRTRH		0x05DD0 /* Receive Packet Rate Threshold */
#define IGC_DMCCNT		0x05DD4 /* Current Rx Count */
#define IGC_FCRTC		0x02170 /* Flow Control Rx high watermark */
#define IGC_PCIEMISC		0x05BB8 /* PCIE misc config register */

/* PCIe Parity Status Register */
#define IGC_PCIEERRSTS		0x05BA8

#define IGC_PROXYS		0x5F64 /* Proxying Status */
#define IGC_PROXYFC		0x5F60 /* Proxying Filter Control */
/* Thermal sensor configuration and status registers */
#define IGC_THMJT		0x08100 /* Junction Temperature */
#define IGC_THLOWTC		0x08104 /* Low Threshold Control */
#define IGC_THMIDTC		0x08108 /* Mid Threshold Control */
#define IGC_THHIGHTC		0x0810C /* High Threshold Control */
#define IGC_THSTAT		0x08110 /* Thermal Sensor Status */

/* Energy Efficient Ethernet "EEE" registers */
#define IGC_IPCNFG		0x0E38 /* Internal PHY Configuration */
#define IGC_LTRC		0x01A0 /* Latency Tolerance Reporting Control */
#define IGC_EEER		0x0E30 /* Energy Efficient Ethernet "EEE"*/
#define IGC_EEE_SU		0x0E34 /* EEE Setup */
#define IGC_EEE_SU_2P5		0x0E3C /* EEE 2.5G Setup */
#define IGC_TLPIC		0x4148 /* EEE Tx LPI Count - TLPIC */
#define IGC_RLPIC		0x414C /* EEE Rx LPI Count - RLPIC */

/* OS2BMC Registers */
#define IGC_B2OSPC		0x08FE0 /* BMC2OS packets sent by BMC */
#define IGC_B2OGPRC		0x04158 /* BMC2OS packets received by host */
#define IGC_O2BGPTC		0x08FE4 /* OS2BMC packets received by BMC */
#define IGC_O2BSPC		0x0415C /* OS2BMC packets transmitted by host */

#define IGC_LTRMINV		0x5BB0 /* LTR Minimum Value */
#define IGC_LTRMAXV		0x5BB4 /* LTR Maximum Value */


/* IEEE 1588 TIMESYNCH */
#define IGC_TRGTTIML0		0x0B644 /* Target Time Register 0 Low  - RW */
#define IGC_TRGTTIMH0		0x0B648 /* Target Time Register 0 High - RW */
#define IGC_TRGTTIML1		0x0B64C /* Target Time Register 1 Low  - RW */
#define IGC_TRGTTIMH1		0x0B650 /* Target Time Register 1 High - RW */
#define IGC_FREQOUT0		0x0B654 /* Frequency Out 0 Control Register - RW */
#define IGC_FREQOUT1		0x0B658 /* Frequency Out 1 Control Register - RW */
#define IGC_TSSDP		0x0003C  /* Time Sync SDP Configuration Register - RW */


#endif	/* _IGC_REGS_H_ */
