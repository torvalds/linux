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

/* For RequestWindow */
typedef struct win_req_t {
    u_int	Attributes;
    u_long	Base;
    u_int	Size;
    u_int	AccessSpeed;
} win_req_t;

/* Attributes for RequestWindow */
#define WIN_ADDR_SPACE		0x0001
#define WIN_ADDR_SPACE_MEM	0x0000
#define WIN_ADDR_SPACE_IO	0x0001
#define WIN_MEMORY_TYPE		0x0002
#define WIN_MEMORY_TYPE_CM	0x0000
#define WIN_MEMORY_TYPE_AM	0x0002
#define WIN_ENABLE		0x0004
#define WIN_DATA_WIDTH		0x0018
#define WIN_DATA_WIDTH_8	0x0000
#define WIN_DATA_WIDTH_16	0x0008
#define WIN_DATA_WIDTH_32	0x0010
#define WIN_PAGED		0x0020
#define WIN_SHARED		0x0040
#define WIN_FIRST_SHARED	0x0080
#define WIN_USE_WAIT		0x0100
#define WIN_STRICT_ALIGN	0x0200
#define WIN_MAP_BELOW_1MB	0x0400
#define WIN_PREFETCH		0x0800
#define WIN_CACHEABLE		0x1000
#define WIN_BAR_MASK		0xe000
#define WIN_BAR_SHIFT		13

#endif /* _LINUX_CS_H */
