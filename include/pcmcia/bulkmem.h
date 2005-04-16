/*
 * bulkmem.h -- Definitions for bulk memory services
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 */

#ifndef _LINUX_BULKMEM_H
#define _LINUX_BULKMEM_H

/* For GetFirstRegion and GetNextRegion */
typedef struct region_info_t {
    u_int		Attributes;
    u_int		CardOffset;
    u_int		RegionSize;
    u_int		AccessSpeed;
    u_int		BlockSize;
    u_int		PartMultiple;
    u_char		JedecMfr, JedecInfo;
    memory_handle_t	next;
} region_info_t;

#define REGION_TYPE		0x0001
#define REGION_TYPE_CM		0x0000
#define REGION_TYPE_AM		0x0001
#define REGION_PREFETCH		0x0008
#define REGION_CACHEABLE	0x0010
#define REGION_BAR_MASK		0xe000
#define REGION_BAR_SHIFT	13

int pcmcia_get_first_region(client_handle_t handle, region_info_t *rgn);
int pcmcia_get_next_region(client_handle_t handle, region_info_t *rgn);

#endif /* _LINUX_BULKMEM_H */
