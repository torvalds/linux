/*
 * Intel Langwell USB Device Controller driver
 * Copyright (C) 2008-2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef __LANGWELL_UDC_H
#define __LANGWELL_UDC_H


/* MACRO defines */
#define	CAP_REG_OFFSET		0x0
#define	OP_REG_OFFSET		0x28

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

#define	DQH_ALIGNMENT		2048
#define	DTD_ALIGNMENT		64
#define	DMA_BOUNDARY		4096

#define	EP0_MAX_PKT_SIZE	64
#define EP_DIR_IN		1
#define EP_DIR_OUT		0

#define FLUSH_TIMEOUT		1000
#define RESET_TIMEOUT		1000
#define SETUPSTAT_TIMEOUT	100
#define PRIME_TIMEOUT		100


/* device memory space registers */

/* Capability Registers, BAR0 + CAP_REG_OFFSET */
struct langwell_cap_regs {
	/* offset: 0x0 */
	u8	caplength;	/* offset of Operational Register */
	u8	_reserved3;
	u16	hciversion;	/* H: BCD encoding of host version */
	u32	hcsparams;	/* H: host port steering logic capability */
	u32	hccparams;	/* H: host multiple mode control capability */
#define	HCC_LEN	BIT(17)		/* Link power management (LPM) capability */
	u8	_reserved4[0x20-0xc];
	/* offset: 0x20 */
	u16	dciversion;	/* BCD encoding of device version */
	u8	_reserved5[0x24-0x22];
	u32	dccparams;	/* overall device controller capability */
#define	HOSTCAP	BIT(8)		/* host capable */
#define	DEVCAP	BIT(7)		/* device capable */
#define DEN(d)	\
	(((d)>>0)&0x1f)		/* bits 4:0, device endpoint number */
} __attribute__ ((packed));


/* Operational Registers, BAR0 + OP_REG_OFFSET */
struct langwell_op_regs {
	/* offset: 0x28 */
	u32	extsts;
#define	EXTS_TI1	BIT(4)	/* general purpose timer interrupt 1 */
#define	EXTS_TI1TI0	BIT(3)	/* general purpose timer interrupt 0 */
#define	EXTS_TI1UPI	BIT(2)	/* USB host periodic interrupt */
#define	EXTS_TI1UAI	BIT(1)	/* USB host asynchronous interrupt */
#define	EXTS_TI1NAKI	BIT(0)	/* NAK interrupt */
	u32	extintr;
#define	EXTI_TIE1	BIT(4)	/* general purpose timer interrupt enable 1 */
#define	EXTI_TIE0	BIT(3)	/* general purpose timer interrupt enable 0 */
#define	EXTI_UPIE	BIT(2)	/* USB host periodic interrupt enable */
#define	EXTI_UAIE	BIT(1)	/* USB host asynchronous interrupt enable */
#define	EXTI_NAKE	BIT(0)	/* NAK interrupt enable */
	/* offset: 0x30 */
	u32	usbcmd;
#define	CMD_HIRD(u)	\
	(((u)>>24)&0xf)		/* bits 27:24, host init resume duration */
#define	CMD_ITC(u)	\
	(((u)>>16)&0xff)	/* bits 23:16, interrupt threshold control */
#define	CMD_PPE		BIT(15)	/* per-port change events enable */
#define	CMD_ATDTW	BIT(14)	/* add dTD tripwire */
#define	CMD_SUTW	BIT(13)	/* setup tripwire */
#define	CMD_ASPE	BIT(11) /* asynchronous schedule park mode enable */
#define	CMD_FS2		BIT(10)	/* frame list size */
#define	CMD_ASP1	BIT(9)	/* asynchronous schedule park mode count */
#define	CMD_ASP0	BIT(8)
#define	CMD_LR		BIT(7)	/* light host/device controller reset */
#define	CMD_IAA		BIT(6)	/* interrupt on async advance doorbell */
#define	CMD_ASE		BIT(5)	/* asynchronous schedule enable */
#define	CMD_PSE		BIT(4)	/* periodic schedule enable */
#define	CMD_FS1		BIT(3)
#define	CMD_FS0		BIT(2)
#define	CMD_RST		BIT(1)	/* controller reset */
#define	CMD_RUNSTOP	BIT(0)	/* run/stop */
	u32	usbsts;
#define	STS_PPCI(u)	\
	(((u)>>16)&0xffff)	/* bits 31:16, port-n change detect */
#define	STS_AS		BIT(15)	/* asynchronous schedule status */
#define	STS_PS		BIT(14)	/* periodic schedule status */
#define	STS_RCL		BIT(13)	/* reclamation */
#define	STS_HCH		BIT(12)	/* HC halted */
#define	STS_ULPII	BIT(10)	/* ULPI interrupt */
#define	STS_SLI		BIT(8)	/* DC suspend */
#define	STS_SRI		BIT(7)	/* SOF received */
#define	STS_URI		BIT(6)	/* USB reset received */
#define	STS_AAI		BIT(5)	/* interrupt on async advance */
#define	STS_SEI		BIT(4)	/* system error */
#define	STS_FRI		BIT(3)	/* frame list rollover */
#define	STS_PCI		BIT(2)	/* port change detect */
#define	STS_UEI		BIT(1)	/* USB error interrupt */
#define	STS_UI		BIT(0)	/* USB interrupt */
	u32	usbintr;
/* bits 31:16, per-port interrupt enable */
#define	INTR_PPCE(u)	(((u)>>16)&0xffff)
#define	INTR_ULPIE	BIT(10)	/* ULPI enable */
#define	INTR_SLE	BIT(8)	/* DC sleep/suspend enable */
#define	INTR_SRE	BIT(7)	/* SOF received enable */
#define	INTR_URE	BIT(6)	/* USB reset enable */
#define	INTR_AAE	BIT(5)	/* interrupt on async advance enable */
#define	INTR_SEE	BIT(4)	/* system error enable */
#define	INTR_FRE	BIT(3)	/* frame list rollover enable */
#define	INTR_PCE	BIT(2)	/* port change detect enable */
#define	INTR_UEE	BIT(1)	/* USB error interrupt enable */
#define	INTR_UE		BIT(0)	/* USB interrupt enable */
	u32	frindex;	/* frame index */
#define	FRINDEX_MASK	(0x3fff << 0)
	u32	ctrldssegment;	/* not used */
	u32	deviceaddr;
#define USBADR_SHIFT	25
#define	USBADR(d)	\
	(((d)>>25)&0x7f)	/* bits 31:25, device address */
#define USBADR_MASK	(0x7f << 25)
#define	USBADRA		BIT(24)	/* device address advance */
	u32	endpointlistaddr;/* endpoint list top memory address */
/* bits 31:11, endpoint list pointer */
#define	EPBASE(d)	(((d)>>11)&0x1fffff)
#define	ENDPOINTLISTADDR_MASK	(0x1fffff << 11)
	u32	ttctrl;		/* H: TT operatin, not used */
	/* offset: 0x50 */
	u32	burstsize;	/* burst size of data movement */
#define	TXPBURST(b)	\
	(((b)>>8)&0xff)		/* bits 15:8, TX burst length */
#define	RXPBURST(b)	\
	(((b)>>0)&0xff)		/* bits 7:0, RX burst length */
	u32	txfilltuning;	/* TX tuning */
	u32	txttfilltuning;	/* H: TX TT tuning */
	u32	ic_usb;		/* control the IC_USB FS/LS transceiver */
	/* offset: 0x60 */
	u32	ulpi_viewport;	/* indirect access to ULPI PHY */
#define	ULPIWU		BIT(31)	/* ULPI wakeup */
#define	ULPIRUN		BIT(30)	/* ULPI read/write run */
#define	ULPIRW		BIT(29)	/* ULPI read/write control */
#define	ULPISS		BIT(27)	/* ULPI sync state */
#define	ULPIPORT(u)	\
	(((u)>>24)&7)		/* bits 26:24, ULPI port number */
#define	ULPIADDR(u)	\
	(((u)>>16)&0xff)	/* bits 23:16, ULPI data address */
#define	ULPIDATRD(u)	\
	(((u)>>8)&0xff)		/* bits 15:8, ULPI data read */
#define	ULPIDATWR(u)	\
	(((u)>>0)&0xff)		/* bits 7:0, ULPI date write */
	u8	_reserved6[0x70-0x64];
	/* offset: 0x70 */
	u32	configflag;	/* H: not used */
	u32	portsc1;	/* port status */
#define	DA(p)	\
	(((p)>>25)&0x7f)	/* bits 31:25, device address */
#define	PORTS_SSTS	(BIT(24) | BIT(23))	/* suspend status */
#define	PORTS_WKOC	BIT(22)	/* wake on over-current enable */
#define	PORTS_WKDS	BIT(21)	/* wake on disconnect enable */
#define	PORTS_WKCN	BIT(20)	/* wake on connect enable */
#define	PORTS_PTC(p)	(((p)>>16)&0xf)	/* bits 19:16, port test control */
#define	PORTS_PIC	(BIT(15) | BIT(14))	/* port indicator control */
#define	PORTS_PO	BIT(13)	/* port owner */
#define	PORTS_PP	BIT(12)	/* port power */
#define	PORTS_LS	(BIT(11) | BIT(10))	/* line status */
#define	PORTS_SLP	BIT(9)	/* suspend using L1 */
#define	PORTS_PR	BIT(8)	/* port reset */
#define	PORTS_SUSP	BIT(7)	/* suspend */
#define	PORTS_FPR	BIT(6)	/* force port resume */
#define	PORTS_OCC	BIT(5)	/* over-current change */
#define	PORTS_OCA	BIT(4)	/* over-current active */
#define	PORTS_PEC	BIT(3)	/* port enable/disable change */
#define	PORTS_PE	BIT(2)	/* port enable/disable */
#define	PORTS_CSC	BIT(1)	/* connect status change */
#define	PORTS_CCS	BIT(0)	/* current connect status */
	u8	_reserved7[0xb4-0x78];
	/* offset: 0xb4 */
	u32	devlc;		/* control LPM and each USB port behavior */
/* bits 31:29, parallel transceiver select */
#define	LPM_PTS(d)	(((d)>>29)&7)
#define	LPM_STS		BIT(28)	/* serial transceiver select */
#define	LPM_PTW		BIT(27)	/* parallel transceiver width */
#define	LPM_PSPD(d)	(((d)>>25)&3)	/* bits 26:25, port speed */
#define LPM_PSPD_MASK	(BIT(26) | BIT(25))
#define LPM_SPEED_FULL	0
#define LPM_SPEED_LOW	1
#define LPM_SPEED_HIGH	2
#define	LPM_SRT		BIT(24)	/* shorten reset time */
#define	LPM_PFSC	BIT(23)	/* port force full speed connect */
#define	LPM_PHCD	BIT(22) /* PHY low power suspend clock disable */
#define	LPM_STL		BIT(16)	/* STALL reply to LPM token */
#define	LPM_BA(d)	\
	(((d)>>1)&0x7ff)	/* bits 11:1, BmAttributes */
#define	LPM_NYT_ACK	BIT(0)	/* NYET/ACK reply to LPM token */
	u8	_reserved8[0xf4-0xb8];
	/* offset: 0xf4 */
	u32	otgsc;		/* On-The-Go status and control */
#define	OTGSC_DPIE	BIT(30)	/* data pulse interrupt enable */
#define	OTGSC_MSE	BIT(29)	/* 1 ms timer interrupt enable */
#define	OTGSC_BSEIE	BIT(28)	/* B session end interrupt enable */
#define	OTGSC_BSVIE	BIT(27)	/* B session valid interrupt enable */
#define	OTGSC_ASVIE	BIT(26)	/* A session valid interrupt enable */
#define	OTGSC_AVVIE	BIT(25)	/* A VBUS valid interrupt enable */
#define	OTGSC_IDIE	BIT(24)	/* USB ID interrupt enable */
#define	OTGSC_DPIS	BIT(22)	/* data pulse interrupt status */
#define	OTGSC_MSS	BIT(21)	/* 1 ms timer interrupt status */
#define	OTGSC_BSEIS	BIT(20)	/* B session end interrupt status */
#define	OTGSC_BSVIS	BIT(19)	/* B session valid interrupt status */
#define	OTGSC_ASVIS	BIT(18)	/* A session valid interrupt status */
#define	OTGSC_AVVIS	BIT(17)	/* A VBUS valid interrupt status */
#define	OTGSC_IDIS	BIT(16)	/* USB ID interrupt status */
#define	OTGSC_DPS	BIT(14)	/* data bus pulsing status */
#define	OTGSC_MST	BIT(13)	/* 1 ms timer toggle */
#define	OTGSC_BSE	BIT(12)	/* B session end */
#define	OTGSC_BSV	BIT(11)	/* B session valid */
#define	OTGSC_ASV	BIT(10)	/* A session valid */
#define	OTGSC_AVV	BIT(9)	/* A VBUS valid */
#define	OTGSC_USBID	BIT(8)	/* USB ID */
#define	OTGSC_HABA	BIT(7)	/* hw assist B-disconnect to A-connect */
#define	OTGSC_HADP	BIT(6)	/* hw assist data pulse */
#define	OTGSC_IDPU	BIT(5)	/* ID pullup */
#define	OTGSC_DP	BIT(4)	/* data pulsing */
#define	OTGSC_OT	BIT(3)	/* OTG termination */
#define	OTGSC_HAAR	BIT(2)	/* hw assist auto reset */
#define	OTGSC_VC	BIT(1)	/* VBUS charge */
#define	OTGSC_VD	BIT(0)	/* VBUS discharge */
	u32	usbmode;
#define	MODE_VBPS	BIT(5)	/* R/W VBUS power select */
#define	MODE_SDIS	BIT(4)	/* R/W stream disable mode */
#define	MODE_SLOM	BIT(3)	/* R/W setup lockout mode */
#define	MODE_ENSE	BIT(2)	/* endian select */
#define	MODE_CM(u)	(((u)>>0)&3)	/* bits 1:0, controller mode */
#define	MODE_IDLE	0
#define	MODE_DEVICE	2
#define	MODE_HOST	3
	u8	_reserved9[0x100-0xfc];
	/* offset: 0x100 */
	u32	endptnak;
#define	EPTN(e)		\
	(((e)>>16)&0xffff)	/* bits 31:16, TX endpoint NAK */
#define	EPRN(e)		\
	(((e)>>0)&0xffff)	/* bits 15:0, RX endpoint NAK */
	u32	endptnaken;
#define	EPTNE(e)	\
	(((e)>>16)&0xffff)	/* bits 31:16, TX endpoint NAK enable */
#define	EPRNE(e)	\
	(((e)>>0)&0xffff)	/* bits 15:0, RX endpoint NAK enable */
	u32	endptsetupstat;
#define	SETUPSTAT_MASK		(0xffff << 0)	/* bits 15:0 */
#define EP0SETUPSTAT_MASK	1
	u32	endptprime;
/* bits 31:16, prime endpoint transmit buffer */
#define	PETB(e)		(((e)>>16)&0xffff)
/* bits 15:0, prime endpoint receive buffer */
#define	PERB(e)		(((e)>>0)&0xffff)
	/* offset: 0x110 */
	u32	endptflush;
/* bits 31:16, flush endpoint transmit buffer */
#define	FETB(e)		(((e)>>16)&0xffff)
/* bits 15:0, flush endpoint receive buffer */
#define	FERB(e)		(((e)>>0)&0xffff)
	u32	endptstat;
/* bits 31:16, endpoint transmit buffer ready */
#define	ETBR(e)		(((e)>>16)&0xffff)
/* bits 15:0, endpoint receive buffer ready */
#define	ERBR(e)		(((e)>>0)&0xffff)
	u32	endptcomplete;
/* bits 31:16, endpoint transmit complete event */
#define	ETCE(e)		(((e)>>16)&0xffff)
/* bits 15:0, endpoint receive complete event */
#define	ERCE(e)		(((e)>>0)&0xffff)
	/* offset: 0x11c */
	u32	endptctrl[16];
#define	EPCTRL_TXE	BIT(23)	/* TX endpoint enable */
#define	EPCTRL_TXR	BIT(22)	/* TX data toggle reset */
#define	EPCTRL_TXI	BIT(21)	/* TX data toggle inhibit */
#define	EPCTRL_TXT(e)	(((e)>>18)&3)	/* bits 19:18, TX endpoint type */
#define	EPCTRL_TXT_SHIFT	18
#define	EPCTRL_TXD	BIT(17)	/* TX endpoint data source */
#define	EPCTRL_TXS	BIT(16)	/* TX endpoint STALL */
#define	EPCTRL_RXE	BIT(7)	/* RX endpoint enable */
#define	EPCTRL_RXR	BIT(6)	/* RX data toggle reset */
#define	EPCTRL_RXI	BIT(5)	/* RX data toggle inhibit */
#define	EPCTRL_RXT(e)	(((e)>>2)&3)	/* bits 3:2, RX endpoint type */
#define	EPCTRL_RXT_SHIFT	2	/* bits 19:18, TX endpoint type */
#define	EPCTRL_RXD	BIT(1)	/* RX endpoint data sink */
#define	EPCTRL_RXS	BIT(0)	/* RX endpoint STALL */
} __attribute__ ((packed));

#endif /* __LANGWELL_UDC_H */

