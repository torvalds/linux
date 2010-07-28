/*
 * cs.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999             David A. Hinds
 */

#ifndef _LINUX_CS_H
#define _LINUX_CS_H

#ifdef __KERNEL__
#include <linux/interrupt.h>
#endif

/* ModifyConfiguration */
typedef struct modconf_t {
    u_int	Attributes;
    u_int	Vcc, Vpp1, Vpp2;
} modconf_t;

/* Attributes for ModifyConfiguration */
#define CONF_IRQ_CHANGE_VALID	0x0100
#define CONF_VCC_CHANGE_VALID	0x0200
#define CONF_VPP1_CHANGE_VALID	0x0400
#define CONF_VPP2_CHANGE_VALID	0x0800
#define CONF_IO_CHANGE_WIDTH	0x1000

/* For RequestConfiguration */
typedef struct config_req_t {
    u_int	Attributes;
    u_int	Vpp; /* both Vpp1 and Vpp2 */
    u_int	IntType;
    u_int	ConfigBase;
    u_char	Status, Pin, Copy, ExtStatus;
    u_char	ConfigIndex;
    u_int	Present;
} config_req_t;

/* Attributes for RequestConfiguration */
#define CONF_ENABLE_IRQ		0x01
#define CONF_ENABLE_DMA		0x02
#define CONF_ENABLE_SPKR	0x04
#define CONF_ENABLE_PULSE_IRQ	0x08
#define CONF_VALID_CLIENT	0x100

/* IntType field */
#define INT_MEMORY		0x01
#define INT_MEMORY_AND_IO	0x02
#define INT_CARDBUS		0x04
#define INT_ZOOMED_VIDEO	0x08

/* Configuration registers present */
#define PRESENT_OPTION		0x001
#define PRESENT_STATUS		0x002
#define PRESENT_PIN_REPLACE	0x004
#define PRESENT_COPY		0x008
#define PRESENT_EXT_STATUS	0x010
#define PRESENT_IOBASE_0	0x020
#define PRESENT_IOBASE_1	0x040
#define PRESENT_IOBASE_2	0x080
#define PRESENT_IOBASE_3	0x100
#define PRESENT_IOSIZE		0x200

#endif /* _LINUX_CS_H */
