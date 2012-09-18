/*
 * Copyright (c) 2001-2002 by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_USB_EHCI_DEF_H
#define __LINUX_USB_EHCI_DEF_H

/* EHCI register interface, corresponds to EHCI Revision 0.95 specification */

/* Section 2.2 Host Controller Capability Registers */
struct ehci_caps {
	/* these fields are specified as 8 and 16 bit registers,
	 * but some hosts can't perform 8 or 16 bit PCI accesses.
	 * some hosts treat caplength and hciversion as parts of a 32-bit
	 * register, others treat them as two separate registers, this
	 * affects the memory map for big endian controllers.
	 */
	u32		hc_capbase;
#define HC_LENGTH(ehci, p)	(0x00ff&((p) >> /* bits 7:0 / offset 00h */ \
				(ehci_big_endian_capbase(ehci) ? 24 : 0)))
#define HC_VERSION(ehci, p)	(0xffff&((p) >> /* bits 31:16 / offset 02h */ \
				(ehci_big_endian_capbase(ehci) ? 0 : 16)))
	u32		hcs_params;     /* HCSPARAMS - offset 0x4 */
#define HCS_DEBUG_PORT(p)	(((p)>>20)&0xf)	/* bits 23:20, debug port? */
#define HCS_INDICATOR(p)	((p)&(1 << 16))	/* true: has port indicators */
#define HCS_N_CC(p)		(((p)>>12)&0xf)	/* bits 15:12, #companion HCs */
#define HCS_N_PCC(p)		(((p)>>8)&0xf)	/* bits 11:8, ports per CC */
#define HCS_PORTROUTED(p)	((p)&(1 << 7))	/* true: port routing */
#define HCS_PPC(p)		((p)&(1 << 4))	/* true: port power control */
#define HCS_N_PORTS(p)		(((p)>>0)&0xf)	/* bits 3:0, ports on HC */

	u32		hcc_params;      /* HCCPARAMS - offset 0x8 */
/* EHCI 1.1 addendum */
#define HCC_32FRAME_PERIODIC_LIST(p)	((p)&(1 << 19))
#define HCC_PER_PORT_CHANGE_EVENT(p)	((p)&(1 << 18))
#define HCC_LPM(p)			((p)&(1 << 17))
#define HCC_HW_PREFETCH(p)		((p)&(1 << 16))

#define HCC_EXT_CAPS(p)		(((p)>>8)&0xff)	/* for pci extended caps */
#define HCC_ISOC_CACHE(p)       ((p)&(1 << 7))  /* true: can cache isoc frame */
#define HCC_ISOC_THRES(p)       (((p)>>4)&0x7)  /* bits 6:4, uframes cached */
#define HCC_CANPARK(p)		((p)&(1 << 2))  /* true: can park on async qh */
#define HCC_PGM_FRAMELISTLEN(p) ((p)&(1 << 1))  /* true: periodic_size changes*/
#define HCC_64BIT_ADDR(p)       ((p)&(1))       /* true: can use 64-bit addr */
	u8		portroute[8];	 /* nibbles for routing - offset 0xC */
};


/* Section 2.3 Host Controller Operational Registers */
struct ehci_regs {

	/* USBCMD: offset 0x00 */
	u32		command;

/* EHCI 1.1 addendum */
#define CMD_HIRD	(0xf<<24)	/* host initiated resume duration */
#define CMD_PPCEE	(1<<15)		/* per port change event enable */
#define CMD_FSP		(1<<14)		/* fully synchronized prefetch */
#define CMD_ASPE	(1<<13)		/* async schedule prefetch enable */
#define CMD_PSPE	(1<<12)		/* periodic schedule prefetch enable */
/* 23:16 is r/w intr rate, in microframes; default "8" == 1/msec */
#define CMD_PARK	(1<<11)		/* enable "park" on async qh */
#define CMD_PARK_CNT(c)	(((c)>>8)&3)	/* how many transfers to park for */
#define CMD_LRESET	(1<<7)		/* partial reset (no ports, etc) */
#define CMD_IAAD	(1<<6)		/* "doorbell" interrupt async advance */
#define CMD_ASE		(1<<5)		/* async schedule enable */
#define CMD_PSE		(1<<4)		/* periodic schedule enable */
/* 3:2 is periodic frame list size */
#define CMD_RESET	(1<<1)		/* reset HC not bus */
#define CMD_RUN		(1<<0)		/* start/stop HC */

	/* USBSTS: offset 0x04 */
	u32		status;
#define STS_PPCE_MASK	(0xff<<16)	/* Per-Port change event 1-16 */
#define STS_ASS		(1<<15)		/* Async Schedule Status */
#define STS_PSS		(1<<14)		/* Periodic Schedule Status */
#define STS_RECL	(1<<13)		/* Reclamation */
#define STS_HALT	(1<<12)		/* Not running (any reason) */
/* some bits reserved */
	/* these STS_* flags are also intr_enable bits (USBINTR) */
#define STS_IAA		(1<<5)		/* Interrupted on async advance */
#define STS_FATAL	(1<<4)		/* such as some PCI access errors */
#define STS_FLR		(1<<3)		/* frame list rolled over */
#define STS_PCD		(1<<2)		/* port change detect */
#define STS_ERR		(1<<1)		/* "error" completion (overflow, ...) */
#define STS_INT		(1<<0)		/* "normal" completion (short, ...) */

	/* USBINTR: offset 0x08 */
	u32		intr_enable;

	/* FRINDEX: offset 0x0C */
	u32		frame_index;	/* current microframe number */
	/* CTRLDSSEGMENT: offset 0x10 */
	u32		segment;	/* address bits 63:32 if needed */
	/* PERIODICLISTBASE: offset 0x14 */
	u32		frame_list;	/* points to periodic list */
	/* ASYNCLISTADDR: offset 0x18 */
	u32		async_next;	/* address of next async queue head */

	u32		reserved1[2];

	/* TXFILLTUNING: offset 0x24 */
	u32		txfill_tuning;	/* TX FIFO Tuning register */
#define TXFIFO_DEFAULT	(8<<16)		/* FIFO burst threshold 8 */

	u32		reserved2[6];

	/* CONFIGFLAG: offset 0x40 */
	u32		configured_flag;
#define FLAG_CF		(1<<0)		/* true: we'll support "high speed" */

	/* PORTSC: offset 0x44 */
	u32		port_status[0];	/* up to N_PORTS */
/* EHCI 1.1 addendum */
#define PORTSC_SUSPEND_STS_ACK 0
#define PORTSC_SUSPEND_STS_NYET 1
#define PORTSC_SUSPEND_STS_STALL 2
#define PORTSC_SUSPEND_STS_ERR 3

#define PORT_DEV_ADDR	(0x7f<<25)		/* device address */
#define PORT_SSTS	(0x3<<23)		/* suspend status */
/* 31:23 reserved */
#define PORT_WKOC_E	(1<<22)		/* wake on overcurrent (enable) */
#define PORT_WKDISC_E	(1<<21)		/* wake on disconnect (enable) */
#define PORT_WKCONN_E	(1<<20)		/* wake on connect (enable) */
/* 19:16 for port testing */
#define PORT_TEST(x)	(((x)&0xf)<<16)	/* Port Test Control */
#define PORT_TEST_PKT	PORT_TEST(0x4)	/* Port Test Control - packet test */
#define PORT_TEST_FORCE	PORT_TEST(0x5)	/* Port Test Control - force enable */
#define PORT_LED_OFF	(0<<14)
#define PORT_LED_AMBER	(1<<14)
#define PORT_LED_GREEN	(2<<14)
#define PORT_LED_MASK	(3<<14)
#define PORT_OWNER	(1<<13)		/* true: companion hc owns this port */
#define PORT_POWER	(1<<12)		/* true: has power (see PPC) */
#define PORT_USB11(x) (((x)&(3<<10)) == (1<<10))	/* USB 1.1 device */
/* 11:10 for detecting lowspeed devices (reset vs release ownership) */
/* 9 reserved */
#define PORT_LPM	(1<<9)		/* LPM transaction */
#define PORT_RESET	(1<<8)		/* reset port */
#define PORT_SUSPEND	(1<<7)		/* suspend port */
#define PORT_RESUME	(1<<6)		/* resume it */
#define PORT_OCC	(1<<5)		/* over current change */
#define PORT_OC		(1<<4)		/* over current active */
#define PORT_PEC	(1<<3)		/* port enable change */
#define PORT_PE		(1<<2)		/* port enable */
#define PORT_CSC	(1<<1)		/* connect status change */
#define PORT_CONNECT	(1<<0)		/* device connected */
#define PORT_RWC_BITS   (PORT_CSC | PORT_PEC | PORT_OCC)

	u32		reserved3[9];

	/* USBMODE: offset 0x68 */
	u32		usbmode;	/* USB Device mode */
#define USBMODE_SDIS	(1<<3)		/* Stream disable */
#define USBMODE_BE	(1<<2)		/* BE/LE endianness select */
#define USBMODE_CM_HC	(3<<0)		/* host controller mode */
#define USBMODE_CM_IDLE	(0<<0)		/* idle state */

	u32		reserved4[7];

/* Moorestown has some non-standard registers, partially due to the fact that
 * its EHCI controller has both TT and LPM support. HOSTPCx are extensions to
 * PORTSCx
 */
	/* HOSTPC: offset 0x84 */
	u32		hostpc[0];	/* HOSTPC extension */
#define HOSTPC_PHCD	(1<<22)		/* Phy clock disable */
#define HOSTPC_PSPD	(3<<25)		/* Port speed detection */

	u32		reserved5[17];

	/* USBMODE_EX: offset 0xc8 */
	u32		usbmode_ex;	/* USB Device mode extension */
#define USBMODE_EX_VBPS	(1<<5)		/* VBus Power Select On */
#define USBMODE_EX_HC	(3<<0)		/* host controller mode */
};

/* Appendix C, Debug port ... intended for use with special "debug devices"
 * that can help if there's no serial console.  (nonstandard enumeration.)
 */
struct ehci_dbg_port {
	u32	control;
#define DBGP_OWNER	(1<<30)
#define DBGP_ENABLED	(1<<28)
#define DBGP_DONE	(1<<16)
#define DBGP_INUSE	(1<<10)
#define DBGP_ERRCODE(x)	(((x)>>7)&0x07)
#	define DBGP_ERR_BAD	1
#	define DBGP_ERR_SIGNAL	2
#define DBGP_ERROR	(1<<6)
#define DBGP_GO		(1<<5)
#define DBGP_OUT	(1<<4)
#define DBGP_LEN(x)	(((x)>>0)&0x0f)
	u32	pids;
#define DBGP_PID_GET(x)		(((x)>>16)&0xff)
#define DBGP_PID_SET(data, tok)	(((data)<<8)|(tok))
	u32	data03;
	u32	data47;
	u32	address;
#define DBGP_EPADDR(dev, ep)	(((dev)<<8)|(ep))
};

#ifdef CONFIG_EARLY_PRINTK_DBGP
#include <linux/init.h>
extern int __init early_dbgp_init(char *s);
extern struct console early_dbgp_console;
#endif /* CONFIG_EARLY_PRINTK_DBGP */

struct usb_hcd;

#ifdef CONFIG_XEN_DOM0
extern int xen_dbgp_reset_prep(struct usb_hcd *);
extern int xen_dbgp_external_startup(struct usb_hcd *);
#else
static inline int xen_dbgp_reset_prep(struct usb_hcd *hcd)
{
	return 1; /* Shouldn't this be 0? */
}

static inline int xen_dbgp_external_startup(struct usb_hcd *hcd)
{
	return -1;
}
#endif

#ifdef CONFIG_EARLY_PRINTK_DBGP
/* Call backs from ehci host driver to ehci debug driver */
extern int dbgp_external_startup(struct usb_hcd *);
extern int dbgp_reset_prep(struct usb_hcd *hcd);
#else
static inline int dbgp_reset_prep(struct usb_hcd *hcd)
{
	return xen_dbgp_reset_prep(hcd);
}
static inline int dbgp_external_startup(struct usb_hcd *hcd)
{
	return xen_dbgp_external_startup(hcd);
}
#endif

#endif /* __LINUX_USB_EHCI_DEF_H */
