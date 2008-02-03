/*
 * cs_types.h
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

#ifndef _LINUX_CS_TYPES_H
#define _LINUX_CS_TYPES_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#endif

#if defined(__arm__) || defined(__mips__) || defined(__avr32__)
/* This (ioaddr_t) is exposed to userspace & hence cannot be changed. */
typedef u_int   ioaddr_t;
#else
typedef u_short	ioaddr_t;
#endif
typedef unsigned long kio_addr_t;

typedef u_short	socket_t;
typedef u_int	event_t;
typedef u_char	cisdata_t;
typedef u_short	page_t;

struct pcmcia_device;
typedef struct pcmcia_device *client_handle_t;

struct window_t;
typedef struct window_t *window_handle_t;

struct region_t;
typedef struct region_t *memory_handle_t;

#ifndef DEV_NAME_LEN
#define DEV_NAME_LEN 32
#endif

typedef char dev_info_t[DEV_NAME_LEN];

#endif /* _LINUX_CS_TYPES_H */
