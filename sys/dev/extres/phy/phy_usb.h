/*-
 * Copyright 2018 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_EXTRES_PHY_USB_H_
#define _DEV_EXTRES_PHY_USB_H_

#include <dev/extres/phy/phy.h>
#include "phynode_usb_if.h"

#define	PHY_USB_MODE_UNKNOWN	0
#define	PHY_USB_MODE_HOST	1
#define	PHY_USB_MODE_OTG	2
#define	PHY_USB_MODE_DEVICE	3

/* Standard USB phy parameters. */
struct phynode_usb_std_param {
	int	usb_mode;
};

struct phynode_usb_sc {
   struct phynode_usb_std_param		std_param;
};

/* Initialization parameters. */
struct phynode_usb_init_def {
	struct phynode_init_def		phynode_init_def;
	struct phynode_usb_std_param	std_param; /* Standard parameters */
};


/*
 * Shorthands for constructing method tables.
 */
#define	PHYNODEUSBMETHOD	KOBJMETHOD
#define	PHYNODEUSBMETHOD_END	KOBJMETHOD_END
#define phynode_usb_method_t	kobj_method_t
#define phynode_usb_class_t	kobj_class_t
DECLARE_CLASS(phynode_usb_class);

struct phynode *phynode_usb_create(device_t pdev, phynode_class_t phynode_class,
    struct phynode_usb_init_def *def);
struct phynode *phynode_usb_register(struct phynode *phynode);

#if 0
/* XXX to be implemented */
#ifdef FDT
int phynode_usb_parse_ofw_stdparam(device_t dev, phandle_t node,
    struct phynode_usb_init_def *def);
#endif
#endif

/* Phynode functions. */
int phynode_usb_set_mode(struct phynode *phynode, int usb_mode);
int phynode_usb_get_mode(struct phynode *phynode, int *usb_mode);

/* Consumer functions. */
int phy_usb_set_mode(phy_t phy, int usb_mode);
int phy_usb_get_mode(phy_t phy, int *usb_mode);

#endif /*_DEV_EXTRES_PHY_USB_H_*/
