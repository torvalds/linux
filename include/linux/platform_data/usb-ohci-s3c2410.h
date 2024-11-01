/* SPDX-License-Identifier: GPL-2.0-only */
/* arch/arm/plat-samsung/include/plat/usb-control.h
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C - USB host port information
*/

#ifndef __ASM_ARCH_USBCONTROL_H
#define __ASM_ARCH_USBCONTROL_H

#define S3C_HCDFLG_USED	(1)

struct s3c2410_hcd_port {
	unsigned char	flags;
	unsigned char	power;
	unsigned char	oc_status;
	unsigned char	oc_changed;
};

struct s3c2410_hcd_info {
	struct usb_hcd		*hcd;
	struct s3c2410_hcd_port	port[2];

	void		(*power_control)(int port, int to);
	void		(*enable_oc)(struct s3c2410_hcd_info *, int on);
	void		(*report_oc)(struct s3c2410_hcd_info *, int ports);
};

static inline void s3c2410_usb_report_oc(struct s3c2410_hcd_info *info, int ports)
{
	if (info->report_oc != NULL) {
		(info->report_oc)(info, ports);
	}
}

extern void s3c_ohci_set_platdata(struct s3c2410_hcd_info *info);

#endif /*__ASM_ARCH_USBCONTROL_H */
