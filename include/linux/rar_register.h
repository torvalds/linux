/*
 * Copyright (C) 2010 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General
 * Public License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 * The full GNU General Public License is included in this
 * distribution in the file called COPYING.
 */


#ifndef _RAR_REGISTER_H
#define _RAR_REGISTER_H

#include <linux/types.h>

/* following are used both in drivers as well as user space apps */

#define	RAR_TYPE_VIDEO	0
#define	RAR_TYPE_AUDIO	1
#define	RAR_TYPE_IMAGE	2
#define	RAR_TYPE_DATA	3

#ifdef __KERNEL__

struct rar_device;

#if defined(CONFIG_RAR_REGISTER)
int register_rar(int num,
		int (*callback)(unsigned long data), unsigned long data);
void unregister_rar(int num);
int rar_get_address(int rar_index, dma_addr_t *start, dma_addr_t *end);
int rar_lock(int rar_index);
#else
extern void unregister_rar(int num)  { }
extern int rar_lock(int rar_index) { return -EIO; }

extern inline int register_rar(int num,
		int (*callback)(unsigned long data), unsigned long data)
{
	return -ENODEV;
}

extern int rar_get_address(int rar_index, dma_addr_t *start, dma_addr_t *end)
{
	return -ENODEV;
}
#endif	/* RAR_REGISTER */

#endif  /* __KERNEL__ */
#endif  /* _RAR_REGISTER_H */
