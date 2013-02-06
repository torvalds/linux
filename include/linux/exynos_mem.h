/* linux/include/linux/exynos_mem.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __INCLUDE_EXYNOS_MEM_H
#define __INCLUDE_EXYNOS_MEM_H __FILE__

/* IOCTL commands */
#define EXYNOS_MEM_SET_CACHEABLE	_IOW('M', 200, bool)
#define EXYNOS_MEM_PADDR_CACHE_FLUSH	_IOW('M', 201, struct exynos_mem_flush_range)
#define EXYNOS_MEM_SET_PHYADDR		_IOW('M', 202, unsigned int)
#define EXYNOS_MEM_PADDR_CACHE_CLEAN	_IOW('M', 203, struct exynos_mem_flush_range)

struct exynos_mem_flush_range {
	phys_addr_t	start;
	size_t		length;
};

#endif /* __INCLUDE_EXYNOS_MEM_H */
