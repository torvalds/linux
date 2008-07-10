/*
 * include/linux/firmware-map.h:
 *  Copyright (C) 2008 SUSE LINUX Products GmbH
 *  by Bernhard Walle <bwalle@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef _LINUX_FIRMWARE_MAP_H
#define _LINUX_FIRMWARE_MAP_H

#include <linux/list.h>
#include <linux/kobject.h>

/*
 * provide a dummy interface if CONFIG_FIRMWARE_MEMMAP is disabled
 */
#ifdef CONFIG_FIRMWARE_MEMMAP

/**
 * Adds a firmware mapping entry. This function uses kmalloc() for memory
 * allocation. Use firmware_map_add_early() if you want to use the bootmem
 * allocator.
 *
 * That function must be called before late_initcall.
 *
 * @start: Start of the memory range.
 * @end:   End of the memory range (inclusive).
 * @type:  Type of the memory range.
 *
 * Returns 0 on success, or -ENOMEM if no memory could be allocated.
 */
int firmware_map_add(resource_size_t start, resource_size_t end,
		     const char *type);

/**
 * Adds a firmware mapping entry. This function uses the bootmem allocator
 * for memory allocation. Use firmware_map_add() if you want to use kmalloc().
 *
 * That function must be called before late_initcall.
 *
 * @start: Start of the memory range.
 * @end:   End of the memory range (inclusive).
 * @type:  Type of the memory range.
 *
 * Returns 0 on success, or -ENOMEM if no memory could be allocated.
 */
int firmware_map_add_early(resource_size_t start, resource_size_t end,
			   const char *type);

#else /* CONFIG_FIRMWARE_MEMMAP */

static inline int firmware_map_add(resource_size_t start, resource_size_t end,
				   const char *type)
{
	return 0;
}

static inline int firmware_map_add_early(resource_size_t start,
					 resource_size_t end, const char *type)
{
	return 0;
}

#endif /* CONFIG_FIRMWARE_MEMMAP */

#endif /* _LINUX_FIRMWARE_MAP_H */
