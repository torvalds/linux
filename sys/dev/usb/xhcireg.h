/* $OpenBSD: xhcireg.h,v 1.20 2024/09/04 07:54:52 mglocker Exp $ */

/*-
 * Copyright (c) 2014 Martin Pieuchot. All rights reserved.
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

#ifndef _XHCIREG_H_
#define _XHCIREG_H_

/* Data Structure Boundary and Alignment Requirement. */
#define XHCI_DCBAA_ALIGN	64
#define XHCI_ICTX_ALIGN		64
#define XHCI_SCTX_ALIGN		32
#define XHCI_OCTX_ALIGN		32
#define XHCI_XFER_RING_ALIGN	16
#define XHCI_CMDS_RING_ALIGN	64
#define XHCI_EVTS_RING_ALIGN	64
#define XHCI_RING_BOUNDARY	(64 * 1024)
#define XHCI_ERST_ALIGN		64
#define XHCI_ERST_BOUNDARY	0
#define XHCI_SPAD_TABLE_ALIGN	64

/* XHCI PCI config registers */
#define PCI_CBMEM		0x10	/* configuration base MEM */

#define PCI_INTERFACE_XHCI	0x30

#define PCI_USBREV		0x60	/* RO USB protocol revision */
#define  PCI_USBREV_MASK	0xff
#define  PCI_USBREV_3_0		0x30	/* USB 3.0 */

#define PCI_XHCI_FLADJ		0x61	/* RW frame length adjust */

#define PCI_XHCI_INTEL_XUSB2PR	0xd0	/* Intel USB2 Port Routing */
#define PCI_XHCI_INTEL_XUSB2PRM	0xd4	/* Intel USB2 Port Routing Mask */
#define PCI_XHCI_INTEL_USB3_PSSEN 0xd8	/* Intel USB3 Port SuperSpeed Enable */
#define PCI_XHCI_INTEL_USB3PRM	0xdc	/* Intel USB3 Port Routing Mask */

/* XHCI capability registers */
#define XHCI_CAPLENGTH		0x00	/* RO Capability reg. length field */
#define XHCI_RESERVED		0x01	/* Reserved */
#define XHCI_HCIVERSION		0x02	/* RO Interface version number */
#define XHCI_HCIVERSION_0_9	0x0090	/* xHCI version 0.9 */
#define XHCI_HCIVERSION_1_0	0x0100	/* xHCI version 1.0 */

#define XHCI_HCSPARAMS1		0x04	/* RO structural parameters 1 */
#define  XHCI_HCS1_DEVSLOT_MAX(x)((x) & 0xff)
#define  XHCI_HCS1_IRQ_MAX(x)	(((x) >> 8) & 0x3ff)
#define  XHCI_HCS1_N_PORTS(x)	(((x) >> 24) & 0xff)

#define XHCI_HCSPARAMS2		0x08	/* RO structural parameters 2 */
#define  XHCI_HCS2_IST(x)	((x) & 0x7)
#define  XHCI_HCS2_IST_MICRO(x) (!((x) & 0x8))
#define  XHCI_HCS2_ERST_MAX(x)	(((x) >> 4) & 0xf)
#define  XHCI_HCS2_ETE(x)	(((x) >> 8) & 0x1)
#define  XHCI_HCS2_SPR(x)	(((x) >> 26) & 0x1)
#define  XHCI_HCS2_SPB_MAX(x)	((((x) >> 16) & 0x3e0) | (((x) >> 27) & 0x1f))

#define XHCI_HCSPARAMS3		0x0c	/* RO structural parameters 3 */
#define  XHCI_HCS3_U1_DEL(x)	((x) & 0xff)
#define  XHCI_HCS3_U2_DEL(x)	(((x) >> 16) & 0xffff)

#define XHCI_HCCPARAMS		0x10	/* RO capability parameters */
#define  XHCI_HCC_AC64(x)	(((x) >> 0) & 0x1) /* 64-bit capable */
#define  XHCI_HCC_BNC(x)	(((x) >> 1) & 0x1) /* BW negotiation */
#define  XHCI_HCC_CSZ(x)	(((x) >> 2) & 0x1) /* Context size */
#define  XHCI_HCC_PPC(x)	(((x) >> 3) & 0x1) /* Port power control */
#define  XHCI_HCC_PIND(x)	(((x) >> 4) & 0x1) /* Port indicators */
#define  XHCI_HCC_LHRC(x)	(((x) >> 5) & 0x1) /* Light HC reset */
#define  XHCI_HCC_LTC(x)	(((x) >> 6) & 0x1) /* Latency tolerance msg */
#define  XHCI_HCC_NSS(x)	(((x) >> 7) & 0x1) /* No secondary sid */
#define  XHCI_HCC_PAE(x)	(((x) >> 8) & 0x1) /* Parse All Event Data */
#define  XHCI_HCC_SPC(x)	(((x) >> 9) & 0x1) /* Short packet */
#define  XHCI_HCC_SEC(x)	(((x) >> 10) & 0x1) /* Stopped EDTLA */
#define  XHCI_HCC_CFC(x)	(((x) >> 11) & 0x1) /* Contiguous Frame ID */
#define  XHCI_HCC_MAX_PSA_SZ(x)	(((x) >> 12) & 0xf) /* Max pri. stream arr. */
#define  XHCI_HCC_XECP(x)	(((x) >> 16) & 0xffff) /* Ext. capabilities */

#define XHCI_DBOFF		0x14	/* RO doorbell offset */
#define XHCI_RTSOFF		0x18	/* RO runtime register space offset */

/*
 * XHCI operational registers.
 * Offset given by XHCI_CAPLENGTH register.
 */
#define XHCI_USBCMD		0x00	/* XHCI command */
#define  XHCI_CMD_RS		0x00000001 /* RW Run/Stop */
#define  XHCI_CMD_HCRST		0x00000002 /* RW Host Controller Reset */
#define  XHCI_CMD_INTE		0x00000004 /* RW Interrupter Enable */
#define  XHCI_CMD_HSEE		0x00000008 /* RW Host System Error Enable */
#define  XHCI_CMD_LHCRST	0x00000080 /* RO/RW Light HC Reset */
#define  XHCI_CMD_CSS		0x00000100 /* RW Controller Save State */
#define  XHCI_CMD_CRS		0x00000200 /* RW Controller Restore State */
#define  XHCI_CMD_EWE		0x00000400 /* RW Enable Wrap Event */
#define  XHCI_CMD_EU3S		0x00000800 /* RW Enable U3 MFINDEX Stop */

#define XHCI_USBSTS		0x04	/* XHCI status */
#define  XHCI_STS_HCH		0x00000001 /* RO - Host Controller Halted */
#define  XHCI_STS_HSE		0x00000004 /* RW - Host System Error */
#define  XHCI_STS_EINT		0x00000008 /* RW - Event Interrupt */
#define  XHCI_STS_PCD		0x00000010 /* RW - Port Change Detect */
#define  XHCI_STS_SSS		0x00000100 /* RO - Save State Status */
#define  XHCI_STS_RSS		0x00000200 /* RO - Restore State Status */
#define  XHCI_STS_SRE		0x00000400 /* RW - Save/Restore Error */
#define  XHCI_STS_CNR		0x00000800 /* RO - Controller Not Ready */
#define  XHCI_STS_HCE		0x00001000 /* RO - Host Controller Error */

#define XHCI_PAGESIZE		0x08	/* XHCI page size mask */
#define  XHCI_PAGESIZE_4K	0x00000001 /* 4K Page Size */
#define  XHCI_PAGESIZE_8K	0x00000002 /* 8K Page Size */
#define  XHCI_PAGESIZE_16K	0x00000004 /* 16K Page Size */
#define  XHCI_PAGESIZE_32K	0x00000008 /* 32K Page Size */
#define  XHCI_PAGESIZE_64K	0x00000010 /* 64K Page Size */

#define XHCI_DNCTRL		0x14	/* XHCI device notification control */
#define  XHCI_DNCTRL_MASK(n)	(1 << (n))

#define XHCI_CRCR_LO		0x18	/* XHCI command ring control */
#define  XHCI_CRCR_LO_RCS	0x00000001 /* RW - consumer cycle state */
#define  XHCI_CRCR_LO_CS	0x00000002 /* RW - command stop */
#define  XHCI_CRCR_LO_CA	0x00000004 /* RW - command abort */
#define  XHCI_CRCR_LO_CRR	0x00000008 /* RW - command ring running */
#define  XHCI_CRCR_LO_MASK	0x0000000F

#define XHCI_CRCR_HI		0x1C	/* XHCI command ring control */
#define XHCI_DCBAAP_LO		0x30	/* XHCI dev context BA pointer */
#define XHCI_DCBAAP_HI		0x34	/* XHCI dev context BA pointer */
#define XHCI_CONFIG		0x38
#define  XHCI_CONFIG_SLOTS_MASK	0x000000ff /* RW - nb of device slots enabled */

/*
 * XHCI port status registers.
 */
#define XHCI_PORTSC(n)		(0x3f0 + (0x10 * (n)))	/* XHCI port status */
#define  XHCI_PS_CCS		0x00000001 /* RO - current connect status */
#define  XHCI_PS_PED		0x00000002 /* RW - port enabled / disabled */
#define  XHCI_PS_OCA		0x00000008 /* RO - over current active */
#define  XHCI_PS_PR		0x00000010 /* RW - port reset */
#define  XHCI_PS_GET_PLS(x)	(((x) >> 5) & 0xf) /* RW - port link state */
#define  XHCI_PS_SET_PLS(x)	(((x) & 0xf) << 5) /* RW - port link state */
#define  XHCI_PS_PP		0x00000200	/* RW - port power */
#define  XHCI_PS_SPEED(x)	(((x) >> 10) & 0xf) /* RO - port speed */
#define  XHCI_PS_GET_PIC(x)	(((x) >> 14) & 0x3) /* RW - port indicator */
#define  XHCI_PS_SET_PIC(x)	(((x) & 0x3) << 14) /* RW - port indicator */
#define  XHCI_PS_LWS		0x00010000 /* RW - link state write strobe */
#define  XHCI_PS_CSC		0x00020000 /* RW - connect status change */
#define  XHCI_PS_PEC		0x00040000 /* RW - port enable/disable change */
#define  XHCI_PS_WRC		0x00080000 /* RW - warm port reset change */
#define  XHCI_PS_OCC		0x00100000 /* RW - over-current change */
#define  XHCI_PS_PRC		0x00200000 /* RW - port reset change */
#define  XHCI_PS_PLC		0x00400000 /* RW - port link state change */
#define  XHCI_PS_CEC		0x00800000 /* RW - config error change */
#define  XHCI_PS_CAS		0x01000000 /* RO - cold attach status */
#define  XHCI_PS_WCE		0x02000000 /* RW - wake on connect enable */
#define  XHCI_PS_WDE		0x04000000 /* RW - wake on disconnect enable */
#define  XHCI_PS_WOE		0x08000000 /* RW - wake on over-current enable*/
#define  XHCI_PS_DR		0x40000000 /* RO - device removable */
#define  XHCI_PS_WPR		0x80000000U /* RW - warm port reset */
#define  XHCI_PS_CLEAR		0x80ff01ffu /* command bits */

#define XHCI_PORTPMSC(n)	(0x3f4 + (0x10 * (n))) /* XHCI status & ctrl */
#define XHCI_PM3_U1TO(x)	(((x) & 0xff) << 0)	/* RW - U1 timeout */
#define XHCI_PM3_U2TO(x)	(((x) & 0xff) << 8)	/* RW - U2 timeout */
#define XHCI_PM3_FLA		0x00010000 /* RW - Force Link PM Accept */
#define XHCI_PM2_L1S(x)		(((x) >> 0) & 0x7)	/* RO - L1 status */
#define XHCI_PM2_RWE		0x00000008 /* RW - remote wakeup enable */
#define XHCI_PM2_HIRD(x)	(((x) & 0xf) << 4)  /* RW - resume duration */
#define XHCI_PM2_L1SLOT(x)	(((x) & 0xff) << 8) /* RW - L1 device slot */
#define XHCI_PM2_HLE		0x00010000	/* RW - hardware LPM enable */
#define XHCI_PORTLI(n)		(0x3f8 + (0x10 * (n))) /* XHCI port link info */
#define XHCI_PORTRSV(n)		(0x3fC + (0x10 * (n))) /* XHCI port reserved */

/*
 * XHCI runtime registers.
 * Offset given by XHCI_CAPLENGTH + XHCI_RTSOFF registers.
 */
#define XHCI_MFINDEX		0x0000		/* RO - microframe index */
#define  XHCI_GET_MFINDEX(x)	((x) & 0x3fff)
#define XHCI_IMAN(n)		(0x0020 + (0x20 * (n)))	/* intr.management */
#define  XHCI_IMAN_INTR_PEND	0x00000001	/* RW - interrupt pending */
#define  XHCI_IMAN_INTR_ENA	0x00000002	/* RW - interrupt enable */

/* XHCI interrupt moderation */
#define XHCI_IMOD(n)		(0x0024 + (0x20 * (n)))
#define  XHCI_IMOD_IVAL_GET(x)	(((x) >> 0) & 0xffff)	/* 250ns unit */
#define  XHCI_IMOD_IVAL_SET(x)	(((x) & 0xffff) << 0)	/* 250ns unit */
#define  XHCI_IMOD_ICNT_GET(x)	(((x) >> 16) & 0xffff)	/* 250ns unit */
#define  XHCI_IMOD_ICNT_SET(x)	(((x) & 0xffff) << 16)	/* 250ns unit */
#define  XHCI_IMOD_DEFAULT	0x000001F4U		/* 8000 IRQ/second */
#define  XHCI_IMOD_DEFAULT_LP	0x000003E8U		/* 4000 IRQ/second */

/* XHCI event ring segment table size */
#define XHCI_ERSTSZ(n)		(0x0028 + (0x20 * (n)))
#define  XHCI_ERSTS_SET(x)	((x) & 0xffff)

/* XHCI event ring segment table BA */
#define XHCI_ERSTBA_LO(n)	(0x0030 + (0x20 * (n)))
#define XHCI_ERSTBA_HI(n)	(0x0034 + (0x20 * (n)))

/* XHCI event ring dequeue pointer */
#define XHCI_ERDP_LO(n)	(0x0038 + (0x20 * (n)))
#define  XHCI_ERDP_LO_BUSY	0x00000008	/* RW - event handler busy */
#define XHCI_ERDP_HI(n)	(0x003c + (0x20 * (n)))

/*
 * XHCI doorbell registers.
 * Offset given by XHCI_CAPLENGTH + XHCI_DBOFF registers.
 */
#define XHCI_DOORBELL(n)	(0x0000 + (4 * (n)))
#define XHCI_DB_GET_SID(x)	(((x) >> 16) & 0xffff)	/* RW - stream ID */
#define XHCI_DB_SET_SID(x)	(((x) & 0xffff) << 16)	/* RW - stream ID */

/* XHCI legacy support */
#define XHCI_XECP_ID(x)		((x) & 0xff)
#define XHCI_XECP_NEXT(x)	(((x) >> 8) & 0xff)
#define XHCI_XECP_BIOS_SEM	0x0002
#define XHCI_XECP_OS_SEM	0x0003

/* XHCI capability ID's */
#define XHCI_ID_USB_LEGACY	0x0001
#define XHCI_ID_PROTOCOLS	0x0002
#define XHCI_ID_POWER_MGMT	0x0003
#define XHCI_ID_VIRTUALIZATION	0x0004
#define XHCI_ID_MSG_IRQ		0x0005
#define XHCI_ID_USB_LOCAL_MEM	0x0006


struct xhci_erseg {
	uint64_t		 er_addr;
	uint32_t		 er_size;
	uint32_t		 er_rsvd;
};


struct xhci_sctx {
	 uint32_t		info_lo;
#define XHCI_SCTX_ROUTE(x)		((x) & 0xfffff)
#define XHCI_SCTX_SPEED(x)		(((x) & 0xf) << 20)
#define XHCI_SCTX_MTT(x)		(((x) & 0x1) << 25)
#define XHCI_SCTX_HUB(x)		(((x) & 0x1) << 26)
#define XHCI_SCTX_DCI(x)		(((x) & 0x1f) << 27)

	 uint32_t		info_hi;
#define XHCI_SCTX_MAX_EL(x)		((x) & 0xffff)
#define XHCI_SCTX_RHPORT(x)		(((x) & 0xff) << 16)
#define XHCI_SCTX_NPORTS(x)		(((x) & 0xff) << 24)

	 uint32_t		tt;
#define XHCI_SCTX_TT_HUB_SID(x)		((x) & 0xff)
#define XHCI_SCTX_TT_PORT_NUM(x)	(((x) & 0xff) << 8)
#define XHCI_SCTX_TT_THINK_TIME(x)	(((x) & 0x3) << 16)
#define XHCI_SCTX_SET_IRQ_TARGET(x)	(((x) & 0x3ff) << 22)
#define XHCI_SCTX_GET_IRQ_TARGET(x)	(((x) >> 22) & 0x3ff)

	 uint32_t		state;
#define XHCI_SCTX_DEV_ADDR(x)		((x) & 0xff)
#define XHCI_SCTX_SLOT_STATE(x)		(((x) >> 27) & 0x1f)

	 uint32_t		rsvd[4];
};

struct xhci_epctx {
	 uint32_t		info_lo;
#define XHCI_EPCTX_STATE(x)		((x) & 0x7)
#define  XHCI_EP_DISABLED       0x0
#define  XHCI_EP_RUNNING        0x1
#define  XHCI_EP_HALTED         0x2
#define  XHCI_EP_STOPPED        0x3
#define  XHCI_EP_ERROR          0x4
#define XHCI_EPCTX_SET_MULT(x)		(((x) & 0x3) << 8)
#define XHCI_EPCTX_GET_MULT(x)		(((x) >> 8) & 0x3)
#define XHCI_EPCTX_SET_MAXP_STREAMS(x)	(((x) & 0x1F) << 10)
#define XHCI_EPCTX_GET_MAXP_STREAMS(x)	(((x) >> 10) & 0x1F)
#define XHCI_EPCTX_SET_LSA(x)		(((x) & 0x1) << 15)
#define XHCI_EPCTX_GET_LSA(x)		(((x) >> 15) & 0x1)
#define XHCI_EPCTX_SET_IVAL(x)		(((x) & 0xff) << 16)
#define XHCI_EPCTX_GET_IVAL(x)		(((x) >> 16) & 0xFF)
#define XHCI_EPCTX_MAX_IVAL		15 /* Poll rates: 2^(n-1) * 0.125us */

	 uint32_t		info_hi;
#define XHCI_EPCTX_SET_CERR(x)		(((x) & 0x3) << 1)
#define XHCI_EPCTX_SET_EPTYPE(x)	(((x) & 0x7) << 3)
#define XHCI_EPCTX_GET_EPTYPE(x)	(((x) >> 3) & 0x7)
#define XHCI_EPCTX_SET_HID(x)		(((x) & 0x1) << 7)
#define XHCI_EPCTX_GET_HID(x)		(((x) >> 7) & 0x1)
#define XHCI_EPCTX_SET_MAXB(x)		(((x) & 0xff) << 8)
#define XHCI_EPCTX_GET_MAXB(x)		(((x) >> 8) & 0xff)
#define XHCI_EPCTX_SET_MPS(x)		(((x) & 0xffff) << 16)
#define XHCI_EPCTX_GET_MPS(x)		(((x) >> 16) & 0xffff)
#define  XHCI_SPEED_FULL	1
#define  XHCI_SPEED_LOW		2
#define  XHCI_SPEED_HIGH	3
#define  XHCI_SPEED_SUPER	4

	 uint64_t		deqp;

	 uint32_t		txinfo;
#define XHCI_EPCTX_AVG_TRB_LEN(x)	((x) & 0xffff)
#define XHCI_EPCTX_MAX_ESIT_PAYLOAD(x)	(((x) & 0xffff) << 16)

	 uint32_t		rsvd[3];
};


struct xhci_inctx {
	 uint32_t		drop_flags;
	 uint32_t		add_flags;
#define XHCI_INCTX_MASK_DCI(n)	(0x1 << (n))

	 uint32_t		rsvd[6];
};


struct xhci_trb {
	uint64_t trb_paddr;
#define XHCI_TRB_PORTID(x)	(((x) >> 24) & 0xff)	/* Port ID */
#define XHCI_TRB_MAXSIZE	(64 * 1024)

	uint32_t trb_status;
#define XHCI_TRB_GET_CODE(x)	(((x) >> 24) & 0xff)
#define XHCI_TRB_TDREM(x)	(((x) & 0x1f) << 17)	/* TD remaining len. */
#define XHCI_TRB_REMAIN(x)	((x) & 0xffffff)	/* Remaining length */
#define XHCI_TRB_LEN(x)		((x) & 0x1ffff)		/* Transfer length */
#define XHCI_TRB_INTR(x)	(((x) & 0x3ff) << 22)	/* MSI-X intr. target */

	uint32_t trb_flags;
#define XHCI_TRB_CYCLE		(1 << 0) 	/* Enqueue point of xfer ring */
#define XHCI_TRB_ENT		(1 << 1)	/* Evaluate next TRB */
#define XHCI_TRB_LINKSEG	XHCI_TRB_ENT	/* Link to next segment */
#define XHCI_TRB_ISP		(1 << 2)	/* Interrupt on short packet */
#define XHCI_TRB_NOSNOOP	(1 << 3)	/* PCIe no snoop */
#define XHCI_TRB_CHAIN		(1 << 4)	/* Chained with next TRB */
#define XHCI_TRB_IOC		(1 << 5)	/* Interrupt On Completion */
#define XHCI_TRB_IDT		(1 << 6)	/* Immediate DaTa */
#define XHCI_TRB_ISOC_TBC(x)	(((x) & 0x3) << 7) /* Transfer Burst Count */
#define XHCI_TRB_BSR		(1 << 9)	/* Block Set Address Request */
#define XHCI_TRB_ISOC_BEI	(1 << 9)	/* Block Event Interrupt */
#define XHCI_TRB_DIR_IN		(1 << 16)
#define XHCI_TRB_TRT_OUT	(2 << 16)
#define XHCI_TRB_TRT_IN		(3 << 16)
#define XHCI_TRB_GET_EP(x)	(((x) >> 16) & 0x1f)
#define XHCI_TRB_SET_EP(x)	(((x) & 0x1f) << 16)
#define XHCI_TRB_ISOC_TLBPC(x)	(((x) & 0xf) << 16)
#define XHCI_TRB_ISOC_FRAME(x)	(((x) & 0x7ff) << 20)
#define XHCI_TRB_GET_SLOT(x)	(((x) >> 24) & 0xff)
#define XHCI_TRB_SET_SLOT(x)	(((x) & 0xff) << 24)
#define XHCI_TRB_SIA		(1U << 31)
};

#define XHCI_TRB_FLAGS_BITMASK						\
    "\20" "\040SIA" "\022TRT_OUT" "\021DIR_IN" "\012BSR" "\007IDT" 	\
    "\006IOC" "\005CHAIN" "\004NOSNOOP" "\003ISP" "\002LINKSEG" "\001CYCLE"

#define XHCI_TRB_TYPE_MASK	0xfc00
#define XHCI_TRB_TYPE(x)	(((x) & XHCI_TRB_TYPE_MASK) >> 10)

/* Transfer Ring Types */
#define XHCI_TRB_TYPE_NORMAL	(1 << 10)
#define XHCI_TRB_TYPE_SETUP	(2 << 10)	/* Setup stage	(ctrl only) */
#define XHCI_TRB_TYPE_DATA	(3 << 10)	/* Data stage	(ctrl only) */
#define XHCI_TRB_TYPE_STATUS	(4 << 10)	/* Status stage	(ctrl only) */
#define XHCI_TRB_TYPE_ISOCH	(5 << 10)
#define XHCI_TRB_TYPE_LINK	(6 << 10)	/* Link next seg. (all+cmd) */
#define XHCI_TRB_TYPE_EVENT	(7 << 10)	/* Generate event (all) */
#define XHCI_TRB_TYPE_NOOP	(8 << 10)	/* No-Op (all) */

/* Command ring Types */
#define XHCI_CMD_ENABLE_SLOT	(9 << 10)
#define XHCI_CMD_DISABLE_SLOT	(10 << 10)
#define XHCI_CMD_ADDRESS_DEVICE	(11 << 10)
#define XHCI_CMD_CONFIG_EP	(12 << 10)
#define XHCI_CMD_EVAL_CTX	(13 << 10)
#define XHCI_CMD_RESET_EP	(14 << 10)
#define XHCI_CMD_STOP_EP	(15 << 10)
#define XHCI_CMD_SET_TR_DEQ	(16 << 10)
#define XHCI_CMD_RESET_DEV	(17 << 10)
#define XHCI_CMD_FEVENT		(18 << 10)
#define XHCI_CMD_NEG_BW		(19 << 10)	/* Negotiate bandwidth */
#define XHCI_CMD_SET_LT  	(20 << 10)	/* Set latency tolerance */
#define XHCI_CMD_GET_BW		(21 << 10)	/* Get port bandwidth */
#define XHCI_CMD_FHEADER	(22 << 10)
#define XHCI_CMD_NOOP		(23 << 10)	/* To test the command ring */

/* Event ring Types */
#define XHCI_EVT_XFER		(32 << 10)	/* Transfer event */
#define XHCI_EVT_CMD_COMPLETE	(33 << 10)
#define XHCI_EVT_PORT_CHANGE	(34 << 10)	/* Port status change */
#define XHCI_EVT_BW_REQUEST     (35 << 10)
#define XHCI_EVT_DOORBELL	(36 << 10)
#define XHCI_EVT_HOST_CTRL	(37 << 10)
#define XHCI_EVT_DEVICE_NOTIFY	(38 << 10)
#define XHCI_EVT_MFINDEX_WRAP	(39 << 10)

/* TRB Completion codes */
#define XHCI_CODE_INVALID	 0	/* Producer didn't update the code. */
#define XHCI_CODE_SUCCESS	 1	/* Badaboum, plaf, plouf, yeepee! */
#define XHCI_CODE_DATA_BUF	 2	/* Overrun or underrun */
#define XHCI_CODE_BABBLE	 3	/* Device is "babbling" */
#define XHCI_CODE_TXERR		 4	/* USB Transaction error */
#define XHCI_CODE_TRB		 5	/* Invalid TRB  */
#define XHCI_CODE_STALL		 6	/* Stall condition */
#define XHCI_CODE_RESOURCE	 7	/* No resource available for the cmd */
#define XHCI_CODE_BANDWIDTH	 8	/* Not enough bandwidth  for the cmd */
#define XHCI_CODE_NO_SLOTS	 9	/* MaxSlots limit reached */
#define XHCI_CODE_STREAM_TYPE	10	/* Stream Context Type value detected */
#define XHCI_CODE_SLOT_NOT_ON	11	/* Related device slot is disabled */
#define XHCI_CODE_ENDP_NOT_ON	12	/* Related endpoint is disabled */
#define XHCI_CODE_SHORT_XFER	13	/* Short packet */
#define XHCI_CODE_RING_UNDERRUN	14	/* Empty ring when transmitting isoc */
#define XHCI_CODE_RING_OVERRUN	15	/* Empty ring when receiving isoc */
#define XHCI_CODE_VF_RING_FULL	16	/* VF's event ring is full */
#define XHCI_CODE_PARAMETER	17	/* Context parameter is invalid */
#define XHCI_CODE_BW_OVERRUN	18 	/* TD exceeds the bandwidth */
#define XHCI_CODE_CONTEXT_STATE	19	/* Transition from illegal ctx state */
#define XHCI_CODE_NO_PING_RESP	20	/* Unable to complete periodic xfer */
#define XHCI_CODE_EV_RING_FULL	21	/* Unable to post an evt to the ring */
#define XHCI_CODE_INCOMPAT_DEV	22	/* Device cannot be accessed */
#define XHCI_CODE_MISSED_SRV	23	/* Unable to service isoc EP in ESIT */
#define XHCI_CODE_CMD_RING_STOP	24 	/* Command Stop (CS) requested */
#define XHCI_CODE_CMD_ABORTED	25 	/* Command Abort (CA) operation */
#define XHCI_CODE_XFER_STOPPED	26 	/* xfer terminated by a stop endpoint */
#define XHCI_CODE_XFER_STOPINV	27 	/* TRB transfer length invalid */
#define XHCI_CODE_XFER_SHORTPKT	28 	/* Stopped before reaching end of TD */
#define XHCI_CODE_MELAT		29	/* Max Exit Latency too large */
#define XHCI_CODE_RESERVED	30
#define XHCI_CODE_ISOC_OVERRUN	31	/* IN data buffer < Max ESIT Payload */
#define XHCI_CODE_EVENT_LOST	32 	/* Internal overrun - impl. specific */
#define XHCI_CODE_UNDEFINED	33 	/* Fatal error - impl. specific */
#define XHCI_CODE_INVALID_SID	34 	/* Invalid stream ID received */
#define XHCI_CODE_SEC_BW	35 	/* Cannot alloc secondary BW Domain */
#define XHCI_CODE_SPLITERR	36 	/* USB2 split transaction */

#endif	/* _XHCIREG_H_ */
