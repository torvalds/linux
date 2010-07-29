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

/* For RequestConfiguration */
typedef struct config_req_t {
    u_int	Attributes;
} config_req_t;

/* Attributes for RequestConfiguration */
#define CONF_ENABLE_IRQ		0x01
#define CONF_ENABLE_DMA		0x02
#define CONF_ENABLE_SPKR	0x04
#define CONF_ENABLE_PULSE_IRQ	0x08
#define CONF_ENABLE_ESR		0x10
#define CONF_VALID_CLIENT	0x100

#endif /* _LINUX_CS_H */
