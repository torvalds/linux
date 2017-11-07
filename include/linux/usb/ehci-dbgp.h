/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Standalone EHCI usb debug driver
 *
 * Originally written by:
 *  Eric W. Biederman" <ebiederm@xmission.com> and
 *  Yinghai Lu <yhlu.kernel@gmail.com>
 *
 * Changes for early/late printk and HW errata:
 *  Jason Wessel <jason.wessel@windriver.com>
 *  Copyright (C) 2009 Wind River Systems, Inc.
 *
 */

#ifndef __LINUX_USB_EHCI_DBGP_H
#define __LINUX_USB_EHCI_DBGP_H

#include <linux/console.h>
#include <linux/types.h>

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
extern int early_dbgp_init(char *s);
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
extern int dbgp_reset_prep(struct usb_hcd *);
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

#endif /* __LINUX_USB_EHCI_DBGP_H */
