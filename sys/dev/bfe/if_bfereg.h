/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Stuart Walsh
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS 'AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* $FreeBSD$ */

#ifndef _BFE_H
#define _BFE_H

/* PCI registers */
#define BFE_PCI_MEMLO           0x10
#define BFE_PCI_MEMHIGH         0x14
#define BFE_PCI_INTLINE         0x3C

/* Register layout. */
#define BFE_DEVCTRL         0x00000000  /* Device Control */
#define BFE_PFE             0x00000080  /* Pattern Filtering Enable */
#define BFE_IPP             0x00000400  /* Internal EPHY Present */
#define BFE_EPR             0x00008000  /* EPHY Reset */
#define BFE_PME             0x00001000  /* PHY Mode Enable */
#define BFE_PMCE            0x00002000  /* PHY Mode Clocks Enable */
#define BFE_PADDR           0x0007c000  /* PHY Address */
#define BFE_PADDR_SHIFT     18

#define BFE_BIST_STAT       0x0000000C  /* Built-In Self-Test Status */
#define BFE_WKUP_LEN        0x00000010  /* Wakeup Length */

#define BFE_ISTAT           0x00000020  /* Interrupt Status */
#define BFE_ISTAT_PME       0x00000040 /* Power Management Event */
#define BFE_ISTAT_TO        0x00000080 /* General Purpose Timeout */
#define BFE_ISTAT_DSCE      0x00000400 /* Descriptor Error */
#define BFE_ISTAT_DATAE     0x00000800 /* Data Error */
#define BFE_ISTAT_DPE       0x00001000 /* Descr. Protocol Error */
#define BFE_ISTAT_RDU       0x00002000 /* Receive Descr. Underflow */
#define BFE_ISTAT_RFO       0x00004000 /* Receive FIFO Overflow */
#define BFE_ISTAT_TFU       0x00008000 /* Transmit FIFO Underflow */
#define BFE_ISTAT_RX        0x00010000 /* RX Interrupt */
#define BFE_ISTAT_TX        0x01000000 /* TX Interrupt */
#define BFE_ISTAT_EMAC      0x04000000 /* EMAC Interrupt */
#define BFE_ISTAT_MII_WRITE 0x08000000 /* MII Write Interrupt */
#define BFE_ISTAT_MII_READ  0x10000000 /* MII Read Interrupt */
#define BFE_ISTAT_ERRORS    (BFE_ISTAT_DSCE | BFE_ISTAT_DATAE | BFE_ISTAT_DPE |\
	BFE_ISTAT_RDU | BFE_ISTAT_RFO | BFE_ISTAT_TFU)

#define BFE_IMASK           0x00000024 /* Interrupt Mask */
#define BFE_IMASK_DEF       (BFE_ISTAT_ERRORS | BFE_ISTAT_TO | BFE_ISTAT_RX | \
	BFE_ISTAT_TX)

#define BFE_MAC_CTRL        0x000000A8 /* MAC Control */
#define BFE_CTRL_CRC32_ENAB 0x00000001 /* CRC32 Generation Enable */
#define BFE_CTRL_PDOWN      0x00000004 /* Onchip EPHY Powerdown */
#define BFE_CTRL_EDET       0x00000008 /* Onchip EPHY Energy Detected */
#define BFE_CTRL_LED        0x000000e0 /* Onchip EPHY LED Control */
#define BFE_CTRL_LED_SHIFT  5

#define BFE_MAC_FLOW        0x000000AC /* MAC Flow Control */
#define BFE_FLOW_RX_HIWAT   0x000000ff /* Onchip FIFO HI Water Mark */
#define BFE_FLOW_PAUSE_ENAB 0x00008000 /* Enable Pause Frame Generation */

#define BFE_RCV_LAZY        0x00000100 /* Lazy Interrupt Control */
#define BFE_LAZY_TO_MASK    0x00ffffff /* Timeout */
#define BFE_LAZY_FC_MASK    0xff000000 /* Frame Count */
#define BFE_LAZY_FC_SHIFT   24

#define BFE_DMATX_CTRL      0x00000200 /* DMA TX Control */
#define BFE_TX_CTRL_ENABLE  0x00000001 /* Enable */
#define BFE_TX_CTRL_SUSPEND 0x00000002 /* Suepend Request */
#define BFE_TX_CTRL_LPBACK  0x00000004 /* Loopback Enable */
#define BFE_TX_CTRL_FAIRPRI 0x00000008 /* Fair Priority */
#define BFE_TX_CTRL_FLUSH   0x00000010 /* Flush Request */

#define BFE_DMATX_ADDR      0x00000204 /* DMA TX Descriptor Ring Address */
#define BFE_DMATX_PTR       0x00000208 /* DMA TX Last Posted Descriptor */
#define BFE_DMATX_STAT      0x0000020C /* DMA TX Current Active Desc. + Status */
#define BFE_STAT_CDMASK     0x00000fff /* Current Descriptor Mask */
#define BFE_STAT_SMASK      0x0000f000 /* State Mask */
#define BFE_STAT_DISABLE    0x00000000 /* State Disabled */
#define BFE_STAT_SACTIVE    0x00001000 /* State Active */
#define BFE_STAT_SIDLE      0x00002000 /* State Idle Wait */
#define BFE_STAT_STOPPED    0x00003000 /* State Stopped */
#define BFE_STAT_SSUSP      0x00004000 /* State Suspend Pending */
#define BFE_STAT_EMASK      0x000f0000 /* Error Mask */
#define BFE_STAT_ENONE      0x00000000 /* Error None */
#define BFE_STAT_EDPE       0x00010000 /* Error Desc. Protocol Error */
#define BFE_STAT_EDFU       0x00020000 /* Error Data FIFO Underrun */
#define BFE_STAT_EBEBR      0x00030000 /* Error Bus Error on Buffer Read */
#define BFE_STAT_EBEDA      0x00040000 /* Error Bus Error on Desc. Access */
#define BFE_STAT_FLUSHED    0x00100000 /* Flushed */

#define BFE_DMARX_CTRL      0x00000210 /* DMA RX Control */
#define BFE_RX_CTRL_ENABLE  0x00000001 /* Enable */
#define BFE_RX_CTRL_ROMASK  0x000000fe /* Receive Offset Mask */
#define BFE_RX_CTRL_ROSHIFT 1           /* Receive Offset Shift */

#define BFE_DMARX_ADDR      0x00000214 /* DMA RX Descriptor Ring Address */
#define BFE_DMARX_PTR       0x00000218 /* DMA RX Last Posted Descriptor */
#define BFE_DMARX_STAT      0x0000021C /* DMA RX Current Active Desc. + Status */

#define BFE_RXCONF          0x00000400 /* EMAC RX Config */
#define BFE_RXCONF_DBCAST   0x00000001 /* Disable Broadcast */
#define BFE_RXCONF_ALLMULTI 0x00000002 /* Accept All Multicast */
#define BFE_RXCONF_NORXTX   0x00000004 /* Receive Disable While Transmitting */
#define BFE_RXCONF_PROMISC  0x00000008 /* Promiscuous Enable */
#define BFE_RXCONF_LPBACK   0x00000010 /* Loopback Enable */
#define BFE_RXCONF_FLOW     0x00000020 /* Flow Control Enable */
#define BFE_RXCONF_ACCEPT   0x00000040 /* Accept Unicast Flow Control Frame */
#define BFE_RXCONF_RFILT    0x00000080 /* Reject Filter */

#define BFE_RXMAXLEN        0x00000404 /* EMAC RX Max Packet Length */
#define BFE_TXMAXLEN        0x00000408 /* EMAC TX Max Packet Length */

#define BFE_MDIO_CTRL       0x00000410 /* EMAC MDIO Control */
#define BFE_MDIO_MAXF_MASK  0x0000007f /* MDC Frequency */
#define BFE_MDIO_PREAMBLE   0x00000080 /* MII Preamble Enable */

#define BFE_MDIO_DATA       0x00000414 /* EMAC MDIO Data */
#define BFE_MDIO_DATA_DATA  0x0000ffff /* R/W Data */
#define BFE_MDIO_TA_MASK    0x00030000 /* Turnaround Value */
#define BFE_MDIO_TA_SHIFT   16
#define BFE_MDIO_TA_VALID   2

#define BFE_MDIO_RA_MASK    0x007c0000 /* Register Address */
#define BFE_MDIO_PMD_MASK   0x0f800000 /* Physical Media Device */
#define BFE_MDIO_OP_MASK    0x30000000 /* Opcode */
#define BFE_MDIO_SB_MASK    0xc0000000 /* Start Bits */
#define BFE_MDIO_SB_START   0x40000000 /* Start Of Frame */
#define BFE_MDIO_RA_SHIFT   18
#define BFE_MDIO_PMD_SHIFT  23
#define BFE_MDIO_OP_SHIFT   28
#define BFE_MDIO_OP_WRITE   1
#define BFE_MDIO_OP_READ    2
#define BFE_MDIO_SB_SHIFT   30

#define BFE_EMAC_IMASK      0x00000418 /* EMAC Interrupt Mask */
#define BFE_EMAC_ISTAT      0x0000041C /* EMAC Interrupt Status */
#define BFE_EMAC_INT_MII    0x00000001 /* MII MDIO Interrupt */
#define BFE_EMAC_INT_MIB    0x00000002 /* MIB Interrupt */
#define BFE_EMAC_INT_FLOW   0x00000003 /* Flow Control Interrupt */

#define BFE_CAM_DATA_LO     0x00000420 /* EMAC CAM Data Low */
#define BFE_CAM_DATA_HI     0x00000424 /* EMAC CAM Data High */
#define BFE_CAM_HI_VALID    0x00010000 /* Valid Bit */

#define BFE_CAM_CTRL        0x00000428 /* EMAC CAM Control */
#define BFE_CAM_ENABLE      0x00000001 /* CAM Enable */
#define BFE_CAM_MSEL        0x00000002 /* Mask Select */
#define BFE_CAM_READ        0x00000004 /* Read */
#define BFE_CAM_WRITE       0x00000008 /* Read */
#define BFE_CAM_INDEX_MASK  0x003f0000 /* Index Mask */
#define BFE_CAM_BUSY        0x80000000 /* CAM Busy */
#define BFE_CAM_INDEX_SHIFT 16

#define BFE_ENET_CTRL       0x0000042C /* EMAC ENET Control */
#define BFE_ENET_ENABLE     0x00000001 /* EMAC Enable */
#define BFE_ENET_DISABLE    0x00000002 /* EMAC Disable */
#define BFE_ENET_SRST       0x00000004 /* EMAC Soft Reset */
#define BFE_ENET_EPSEL      0x00000008 /* External PHY Select */

#define BFE_TX_CTRL         0x00000430 /* EMAC TX Control */
#define BFE_TX_DUPLEX       0x00000001 /* Full Duplex */
#define BFE_TX_FMODE        0x00000002 /* Flow Mode */
#define BFE_TX_SBENAB       0x00000004 /* Single Backoff Enable */
#define BFE_TX_SMALL_SLOT   0x00000008 /* Small Slottime */

#define BFE_TX_WMARK        0x00000434 /* EMAC TX Watermark */

#define BFE_MIB_CTRL        0x00000438 /* EMAC MIB Control */
#define BFE_MIB_CLR_ON_READ 0x00000001 /* Autoclear on Read */

/* Status registers */
#define BFE_TX_GOOD_O       0x00000500 /* MIB TX Good Octets */
#define BFE_TX_GOOD_P       0x00000504 /* MIB TX Good Packets */
#define BFE_TX_O            0x00000508 /* MIB TX Octets */
#define BFE_TX_P            0x0000050C /* MIB TX Packets */
#define BFE_TX_BCAST        0x00000510 /* MIB TX Broadcast Packets */
#define BFE_TX_MCAST        0x00000514 /* MIB TX Multicast Packets */
#define BFE_TX_64           0x00000518 /* MIB TX <= 64 byte Packets */
#define BFE_TX_65_127       0x0000051C /* MIB TX 65 to 127 byte Packets */
#define BFE_TX_128_255      0x00000520 /* MIB TX 128 to 255 byte Packets */
#define BFE_TX_256_511      0x00000524 /* MIB TX 256 to 511 byte Packets */
#define BFE_TX_512_1023     0x00000528 /* MIB TX 512 to 1023 byte Packets */
#define BFE_TX_1024_MAX     0x0000052C /* MIB TX 1024 to max byte Packets */
#define BFE_TX_JABBER       0x00000530 /* MIB TX Jabber Packets */
#define BFE_TX_OSIZE        0x00000534 /* MIB TX Oversize Packets */
#define BFE_TX_FRAG         0x00000538 /* MIB TX Fragment Packets */
#define BFE_TX_URUNS        0x0000053C /* MIB TX Underruns */
#define BFE_TX_TCOLS        0x00000540 /* MIB TX Total Collisions */
#define BFE_TX_SCOLS        0x00000544 /* MIB TX Single Collisions */
#define BFE_TX_MCOLS        0x00000548 /* MIB TX Multiple Collisions */
#define BFE_TX_ECOLS        0x0000054C /* MIB TX Excessive Collisions */
#define BFE_TX_LCOLS        0x00000550 /* MIB TX Late Collisions */
#define BFE_TX_DEFERED      0x00000554 /* MIB TX Defered Packets */
#define BFE_TX_CLOST        0x00000558 /* MIB TX Carrier Lost */
#define BFE_TX_PAUSE        0x0000055C /* MIB TX Pause Packets */
#define BFE_RX_GOOD_O       0x00000580 /* MIB RX Good Octets */
#define BFE_RX_GOOD_P       0x00000584 /* MIB RX Good Packets */
#define BFE_RX_O            0x00000588 /* MIB RX Octets */
#define BFE_RX_P            0x0000058C /* MIB RX Packets */
#define BFE_RX_BCAST        0x00000590 /* MIB RX Broadcast Packets */
#define BFE_RX_MCAST        0x00000594 /* MIB RX Multicast Packets */
#define BFE_RX_64           0x00000598 /* MIB RX <= 64 byte Packets */
#define BFE_RX_65_127       0x0000059C /* MIB RX 65 to 127 byte Packets */
#define BFE_RX_128_255      0x000005A0 /* MIB RX 128 to 255 byte Packets */
#define BFE_RX_256_511      0x000005A4 /* MIB RX 256 to 511 byte Packets */
#define BFE_RX_512_1023     0x000005A8 /* MIB RX 512 to 1023 byte Packets */
#define BFE_RX_1024_MAX     0x000005AC /* MIB RX 1024 to max byte Packets */
#define BFE_RX_JABBER       0x000005B0 /* MIB RX Jabber Packets */
#define BFE_RX_OSIZE        0x000005B4 /* MIB RX Oversize Packets */
#define BFE_RX_FRAG         0x000005B8 /* MIB RX Fragment Packets */
#define BFE_RX_MISS         0x000005BC /* MIB RX Missed Packets */
#define BFE_RX_CRCA         0x000005C0 /* MIB RX CRC Align Errors */
#define BFE_RX_USIZE        0x000005C4 /* MIB RX Undersize Packets */
#define BFE_RX_CRC          0x000005C8 /* MIB RX CRC Errors */
#define BFE_RX_ALIGN        0x000005CC /* MIB RX Align Errors */
#define BFE_RX_SYM          0x000005D0 /* MIB RX Symbol Errors */
#define BFE_RX_PAUSE        0x000005D4 /* MIB RX Pause Packets */
#define BFE_RX_NPAUSE       0x000005D8 /* MIB RX Non-Pause Packets */

#define BFE_SBIMSTATE       0x00000F90 /* BFE_SB Initiator Agent State */
#define BFE_PC              0x0000000f /* Pipe Count */
#define BFE_AP_MASK         0x00000030 /* Arbitration Priority */
#define BFE_AP_BOTH         0x00000000 /* Use both timeslices and token */
#define BFE_AP_TS           0x00000010 /* Use timeslices only */
#define BFE_AP_TK           0x00000020 /* Use token only */
#define BFE_AP_RSV          0x00000030 /* Reserved */
#define BFE_IBE             0x00020000 /* In Band Error */
#define BFE_TO              0x00040000 /* Timeout */


/* Seems the bcm440x has a fairly generic core, we only need be concerned with
 * a couple of these
 */
#define BFE_SBINTVEC        0x00000F94 /* BFE_SB Interrupt Mask */
#define BFE_INTVEC_PCI      0x00000001 /* Enable interrupts for PCI */
#define BFE_INTVEC_ENET0    0x00000002 /* Enable interrupts for enet 0 */
#define BFE_INTVEC_ILINE20  0x00000004 /* Enable interrupts for iline20 */
#define BFE_INTVEC_CODEC    0x00000008 /* Enable interrupts for v90 codec */
#define BFE_INTVEC_USB      0x00000010 /* Enable interrupts for usb */
#define BFE_INTVEC_EXTIF    0x00000020 /* Enable interrupts for external i/f */
#define BFE_INTVEC_ENET1    0x00000040 /* Enable interrupts for enet 1 */

#define BFE_SBTMSLOW        0x00000F98 /* BFE_SB Target State Low */
#define BFE_RESET           0x00000001 /* Reset */
#define BFE_REJECT          0x00000002 /* Reject */
#define BFE_CLOCK           0x00010000 /* Clock Enable */
#define BFE_FGC             0x00020000 /* Force Gated Clocks On */
#define BFE_PE              0x40000000 /* Power Management Enable */
#define BFE_BE              0x80000000 /* BIST Enable */

#define BFE_SBTMSHIGH       0x00000F9C /* BFE_SB Target State High */
#define BFE_SERR            0x00000001 /* S-error */
#define BFE_INT             0x00000002 /* Interrupt */
#define BFE_BUSY            0x00000004 /* Busy */
#define BFE_GCR             0x20000000 /* Gated Clock Request */
#define BFE_BISTF           0x40000000 /* BIST Failed */
#define BFE_BISTD           0x80000000 /* BIST Done */

#define BFE_SBBWA0          0x00000FA0 /* BFE_SB Bandwidth Allocation Table 0 */
#define BFE_TAB0_MASK       0x0000ffff /* Lookup Table 0 */
#define BFE_TAB1_MASK       0xffff0000 /* Lookup Table 0 */
#define BFE_TAB0_SHIFT      0
#define BFE_TAB1_SHIFT      16

#define BFE_SBIMCFGLOW      0x00000FA8 /* BFE_SB Initiator Configuration Low */
#define BFE_STO_MASK        0x00000003 /* Service Timeout */
#define BFE_RTO_MASK        0x00000030 /* Request Timeout */
#define BFE_CID_MASK        0x00ff0000 /* Connection ID */
#define BFE_RTO_SHIFT       4
#define BFE_CID_SHIFT       16

#define BFE_SBIMCFGHIGH     0x00000FAC /* BFE_SB Initiator Configuration High */
#define BFE_IEM_MASK        0x0000000c /* Inband Error Mode */
#define BFE_TEM_MASK        0x00000030 /* Timeout Error Mode */
#define BFE_BEM_MASK        0x000000c0 /* Bus Error Mode */
#define BFE_TEM_SHIFT       4
#define BFE_BEM_SHIFT       6

#define BFE_SBTMCFGLOW      0x00000FB8 /* BFE_SB Target Configuration Low */
#define BFE_LOW_CD_MASK     0x000000ff /* Clock Divide Mask */
#define BFE_LOW_CO_MASK     0x0000f800 /* Clock Offset Mask */
#define BFE_LOW_IF_MASK     0x00fc0000 /* Interrupt Flags Mask */
#define BFE_LOW_IM_MASK     0x03000000 /* Interrupt Mode Mask */
#define BFE_LOW_CO_SHIFT    11
#define BFE_LOW_IF_SHIFT    18
#define BFE_LOW_IM_SHIFT    24

#define BFE_SBTMCFGHIGH     0x00000FBC /* BFE_SB Target Configuration High */
#define BFE_HIGH_BM_MASK    0x00000003 /* Busy Mode */
#define BFE_HIGH_RM_MASK    0x0000000C /* Retry Mode */
#define BFE_HIGH_SM_MASK    0x00000030 /* Stop Mode */
#define BFE_HIGH_EM_MASK    0x00000300 /* Error Mode */
#define BFE_HIGH_IM_MASK    0x00000c00 /* Interrupt Mode */
#define BFE_HIGH_RM_SHIFT   2
#define BFE_HIGH_SM_SHIFT   4
#define BFE_HIGH_EM_SHIFT   8
#define BFE_HIGH_IM_SHIFT   10

#define BFE_SBBCFG          0x00000FC0 /* BFE_SB Broadcast Configuration */
#define BFE_LAT_MASK        0x00000003 /* BFE_SB Latency */
#define BFE_MAX0_MASK       0x000f0000 /* MAX Counter 0 */
#define BFE_MAX1_MASK       0x00f00000 /* MAX Counter 1 */
#define BFE_MAX0_SHIFT      16
#define BFE_MAX1_SHIFT      20

#define BFE_SBBSTATE        0x00000FC8 /* BFE_SB Broadcast State */
#define BFE_SBBSTATE_SRD    0x00000001 /* ST Reg Disable */
#define BFE_SBBSTATE_HRD    0x00000002 /* Hold Reg Disable */

#define BFE_SBACTCNFG       0x00000FD8 /* BFE_SB Activate Configuration */
#define BFE_SBFLAGST        0x00000FE8 /* BFE_SB Current BFE_SBFLAGS */

#define BFE_SBIDLOW         0x00000FF8 /* BFE_SB Identification Low */
#define BFE_CS_MASK         0x00000003 /* Config Space Mask */
#define BFE_AR_MASK         0x00000038 /* Num Address Ranges Supported */
#define BFE_SYNCH           0x00000040 /* Sync */
#define BFE_INIT            0x00000080 /* Initiator */
#define BFE_MINLAT_MASK     0x00000f00 /* Minimum Backplane Latency */
#define BFE_MAXLAT_MASK     0x0000f000 /* Maximum Backplane Latency */
#define BFE_FIRST           0x00010000 /* This Initiator is First */
#define BFE_CW_MASK         0x000c0000 /* Cycle Counter Width */
#define BFE_TP_MASK         0x00f00000 /* Target Ports */
#define BFE_IP_MASK         0x0f000000 /* Initiator Ports */
#define BFE_AR_SHIFT        3
#define BFE_MINLAT_SHIFT    8
#define BFE_MAXLAT_SHIFT    12
#define BFE_CW_SHIFT        18
#define BFE_TP_SHIFT        20
#define BFE_IP_SHIFT        24

#define BFE_SBIDHIGH        0x00000FFC /* BFE_SB Identification High */
#define BFE_RC_MASK         0x0000000f /* Revision Code */
#define BFE_CC_MASK         0x0000fff0 /* Core Code */
#define BFE_VC_MASK         0xffff0000 /* Vendor Code */
#define BFE_CC_SHIFT        4
#define BFE_VC_SHIFT        16

#define BFE_CORE_ILINE20    0x801
#define BFE_CORE_SDRAM      0x803
#define BFE_CORE_PCI        0x804
#define BFE_CORE_MIPS       0x805
#define BFE_CORE_ENET       0x806
#define BFE_CORE_CODEC      0x807
#define BFE_CORE_USB        0x808
#define BFE_CORE_ILINE100   0x80a
#define BFE_CORE_EXTIF      0x811

/* SSB PCI config space registers.  */
#define BFE_BAR0_WIN        0x80
#define BFE_BAR1_WIN        0x84
#define BFE_SPROM_CONTROL   0x88
#define BFE_BAR1_CONTROL    0x8c

/* SSB core and hsot control registers.  */
#define BFE_SSB_CONTROL     0x00000000
#define BFE_SSB_ARBCONTROL  0x00000010
#define BFE_SSB_ISTAT       0x00000020
#define BFE_SSB_IMASK       0x00000024
#define BFE_SSB_MBOX        0x00000028
#define BFE_SSB_BCAST_ADDR  0x00000050
#define BFE_SSB_BCAST_DATA  0x00000054
#define BFE_SSB_PCI_TRANS_0 0x00000100
#define BFE_SSB_PCI_TRANS_1 0x00000104
#define BFE_SSB_PCI_TRANS_2 0x00000108
#define BFE_SSB_SPROM       0x00000800

#define BFE_SSB_PCI_MEM     0x00000000
#define BFE_SSB_PCI_IO      0x00000001
#define BFE_SSB_PCI_CFG0    0x00000002
#define BFE_SSB_PCI_CFG1    0x00000003
#define BFE_SSB_PCI_PREF    0x00000004
#define BFE_SSB_PCI_BURST   0x00000008
#define BFE_SSB_PCI_MASK0   0xfc000000
#define BFE_SSB_PCI_MASK1   0xfc000000
#define BFE_SSB_PCI_MASK2   0xc0000000

#define BFE_DESC_LEN        0x00001fff
#define BFE_DESC_CMASK      0x0ff00000 /* Core specific bits */
#define BFE_DESC_EOT        0x10000000 /* End of Table */
#define BFE_DESC_IOC        0x20000000 /* Interrupt On Completion */
#define BFE_DESC_EOF        0x40000000 /* End of Frame */
#define BFE_DESC_SOF        0x80000000 /* Start of Frame */

#define BFE_RX_CP_THRESHOLD 256
#define BFE_RX_HEADER_LEN   28

#define BFE_RX_FLAG_OFIFO   0x00000001 /* FIFO Overflow */
#define BFE_RX_FLAG_CRCERR  0x00000002 /* CRC Error */
#define BFE_RX_FLAG_SERR    0x00000004 /* Receive Symbol Error */
#define BFE_RX_FLAG_ODD     0x00000008 /* Frame has odd number of nibbles */
#define BFE_RX_FLAG_LARGE   0x00000010 /* Frame is > RX MAX Length */
#define BFE_RX_FLAG_MCAST   0x00000020 /* Dest is Multicast Address */
#define BFE_RX_FLAG_BCAST   0x00000040 /* Dest is Broadcast Address */
#define BFE_RX_FLAG_MISS    0x00000080 /* Received due to promisc mode */
#define BFE_RX_FLAG_LAST    0x00000800 /* Last buffer in frame */
#define BFE_RX_FLAG_ERRORS  (BFE_RX_FLAG_ODD | BFE_RX_FLAG_SERR |           \
	BFE_RX_FLAG_CRCERR | BFE_RX_FLAG_OFIFO)

#define BFE_MCAST_TBL_SIZE  32
#define BFE_PCI_DMA         0x40000000
#define BFE_REG_PCI         0x18002000

#define BCOM_VENDORID           0x14E4
#define BCOM_DEVICEID_BCM4401   0x4401
#define BCOM_DEVICEID_BCM4401B0	0x170c

#define PCI_SETBIT(dev, reg, x, s)  \
    pci_write_config(dev, reg, (pci_read_config(dev, reg, s) | (x)), s)
#define PCI_CLRBIT(dev, reg, x, s)  \
    pci_write_config(dev, reg, (pci_read_config(dev, reg, s) & ~(x)), s)

#define BFE_TX_LIST_CNT         128
#define BFE_RX_LIST_CNT         128
#define BFE_TX_LIST_SIZE        BFE_TX_LIST_CNT * sizeof(struct bfe_desc)
#define BFE_RX_LIST_SIZE        BFE_RX_LIST_CNT * sizeof(struct bfe_desc)
#define BFE_RX_OFFSET           30
#define BFE_TX_QLEN             256

#define	BFE_RX_RING_ALIGN	4096
#define	BFE_TX_RING_ALIGN	4096
#define	BFE_MAXTXSEGS		16
#define	BFE_DMA_MAXADDR		0x3FFFFFFF	/* 1GB DMA address limit. */
#define	BFE_ADDR_LO(x)		((uint64_t)(x) & 0xFFFFFFFF)

#define CSR_READ_4(sc, reg)		bus_read_4(sc->bfe_res, reg)

#define CSR_WRITE_4(sc, reg, val)	bus_write_4(sc->bfe_res, reg, val)

#define BFE_OR(sc, name, val)                                               \
	CSR_WRITE_4(sc, name, CSR_READ_4(sc, name) | val)

#define BFE_AND(sc, name, val)                                              \
	CSR_WRITE_4(sc, name, CSR_READ_4(sc, name) & val)

#define BFE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->bfe_mtx, MA_OWNED)
#define BFE_LOCK(_sc)		mtx_lock(&(_sc)->bfe_mtx)
#define BFE_UNLOCK(_sc)		mtx_unlock(&(_sc)->bfe_mtx)

#define BFE_INC(x, y)       (x) = ((x) == ((y)-1)) ? 0 : (x)+1

struct bfe_tx_data {
    struct mbuf     *bfe_mbuf;
    bus_dmamap_t     bfe_map;
};

struct bfe_rx_data {
    struct mbuf     *bfe_mbuf;
    bus_dmamap_t     bfe_map;
    u_int32_t        bfe_ctrl;
};

struct bfe_desc {
    u_int32_t         bfe_ctrl;
    u_int32_t         bfe_addr;
};

struct bfe_rxheader {
    u_int16_t    len;
    u_int16_t    flags;
    u_int16_t    pad[12];
};

#define MIB_TX_GOOD_O		0
#define MIB_TX_GOOD_P		1
#define MIB_TX_O		2	
#define MIB_TX_P		3
#define MIB_TX_BCAST		4
#define MIB_TX_MCAST		5
#define MIB_TX_64		6
#define MIB_TX_65_127		7
#define MIB_TX_128_255		8
#define MIB_TX_256_511		9
#define MIB_TX_512_1023		10
#define MIB_TX_1024_MAX		11
#define MIB_TX_JABBER		12
#define MIB_TX_OSIZE		13
#define MIB_TX_FRAG		14
#define MIB_TX_URUNS		15
#define MIB_TX_TCOLS		16
#define MIB_TX_SCOLS		17
#define MIB_TX_MCOLS		18
#define MIB_TX_ECOLS		19
#define MIB_TX_LCOLS		20
#define MIB_TX_DEFERED		21
#define MIB_TX_CLOST		22
#define MIB_TX_PAUSE		23
#define MIB_RX_GOOD_O		24
#define MIB_RX_GOOD_P		25
#define MIB_RX_O		26
#define MIB_RX_P		27
#define MIB_RX_BCAST		28
#define MIB_RX_MCAST		29
#define MIB_RX_64		30
#define MIB_RX_65_127		31
#define MIB_RX_128_255		32
#define MIB_RX_256_511		33
#define MIB_RX_512_1023		34
#define MIB_RX_1024_MAX		35
#define MIB_RX_JABBER		36
#define MIB_RX_OSIZE		37
#define MIB_RX_FRAG		38
#define MIB_RX_MISS		39
#define MIB_RX_CRCA		40
#define MIB_RX_USIZE		41
#define MIB_RX_CRC		42
#define MIB_RX_ALIGN		43
#define MIB_RX_SYM		44
#define MIB_RX_PAUSE		45
#define MIB_RX_NPAUSE		46

#define	BFE_MIB_CNT		(MIB_RX_NPAUSE - MIB_TX_GOOD_O + 1)

struct bfe_hw_stats {
    uint64_t	tx_good_octets;
    uint64_t	tx_good_frames;
    uint64_t	tx_octets;
    uint64_t	tx_frames;
    uint64_t	tx_bcast_frames;
    uint64_t	tx_mcast_frames;
    uint64_t	tx_pkts_64;
    uint64_t	tx_pkts_65_127;
    uint64_t	tx_pkts_128_255;
    uint64_t	tx_pkts_256_511;
    uint64_t	tx_pkts_512_1023;
    uint64_t	tx_pkts_1024_max;
    uint32_t	tx_jabbers;
    uint64_t	tx_oversize_frames;
    uint64_t	tx_frag_frames;
    uint32_t	tx_underruns;
    uint32_t	tx_colls;
    uint32_t	tx_single_colls;
    uint32_t	tx_multi_colls;
    uint32_t	tx_excess_colls;
    uint32_t	tx_late_colls;
    uint32_t	tx_deferrals;
    uint32_t	tx_carrier_losts;
    uint32_t	tx_pause_frames;

    uint64_t	rx_good_octets;
    uint64_t	rx_good_frames;
    uint64_t	rx_octets;
    uint64_t	rx_frames;
    uint64_t	rx_bcast_frames;
    uint64_t	rx_mcast_frames;
    uint64_t	rx_pkts_64;
    uint64_t	rx_pkts_65_127;
    uint64_t	rx_pkts_128_255;
    uint64_t	rx_pkts_256_511;
    uint64_t	rx_pkts_512_1023;
    uint64_t	rx_pkts_1024_max;
    uint32_t	rx_jabbers;
    uint64_t	rx_oversize_frames;
    uint64_t	rx_frag_frames;
    uint32_t	rx_missed_frames;
    uint32_t	rx_crc_align_errs;
    uint32_t	rx_runts;
    uint32_t	rx_crc_errs;
    uint32_t	rx_align_errs;
    uint32_t	rx_symbol_errs;
    uint32_t	rx_pause_frames;
    uint32_t	rx_control_frames;
};

struct bfe_softc
{
    struct ifnet            *bfe_ifp;     /* interface info */
    device_t                bfe_dev;
    device_t                bfe_miibus;
    bus_dma_tag_t           bfe_tag;
    bus_dma_tag_t           bfe_parent_tag;
    bus_dma_tag_t           bfe_tx_tag, bfe_rx_tag;
    bus_dmamap_t            bfe_tx_map, bfe_rx_map;
    bus_dma_tag_t           bfe_txmbuf_tag, bfe_rxmbuf_tag;
    bus_dmamap_t            bfe_rx_sparemap;
    void                    *bfe_intrhand;
    struct resource         *bfe_irq;
    struct resource         *bfe_res;
    struct callout          bfe_stat_co;
    struct bfe_hw_stats     bfe_stats;
    struct bfe_desc         *bfe_tx_list, *bfe_rx_list;
    struct bfe_tx_data      bfe_tx_ring[BFE_TX_LIST_CNT]; /* XXX */
    struct bfe_rx_data      bfe_rx_ring[BFE_RX_LIST_CNT]; /* XXX */
    struct mtx              bfe_mtx;
    u_int32_t               bfe_flags;
#define	BFE_FLAG_DETACH		0x4000
#define	BFE_FLAG_LINK		0x8000
    u_int32_t               bfe_imask;
    u_int32_t               bfe_dma_offset;
    u_int32_t               bfe_tx_cnt, bfe_tx_cons, bfe_tx_prod;
    u_int32_t               bfe_rx_prod, bfe_rx_cons;
    u_int32_t               bfe_tx_dma, bfe_rx_dma;
    int                     bfe_watchdog_timer;
    u_int8_t                bfe_phyaddr; /* Address of the card's PHY */
    u_int8_t                bfe_mdc_port;
    u_int8_t                bfe_core_unit;
    u_char                  bfe_enaddr[6];
    int                     bfe_if_flags;
};

struct bfe_type
{
    u_int16_t   bfe_vid;
    u_int16_t   bfe_did;
    char        *bfe_name;
};

#endif /* _BFE_H */
