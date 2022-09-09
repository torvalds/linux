/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Intel Corporation
 * Author: johannes@sipsolutions.net
 */
#ifndef __LOGIC_IOMEM_H
#define __LOGIC_IOMEM_H
#include <linux/types.h>
#include <linux/ioport.h>

/**
 * struct logic_iomem_ops - emulated IO memory ops
 * @read: read an 8, 16, 32 or 64 bit quantity from the given offset,
 *	size is given in bytes (1, 2, 4 or 8)
 *	(64-bit only necessary if CONFIG_64BIT is set)
 * @write: write an 8, 16 32 or 64 bit quantity to the given offset,
 *	size is given in bytes (1, 2, 4 or 8)
 *	(64-bit only necessary if CONFIG_64BIT is set)
 * @set: optional, for memset_io()
 * @copy_from: optional, for memcpy_fromio()
 * @copy_to: optional, for memcpy_toio()
 * @unmap: optional, this region is getting unmapped
 */
struct logic_iomem_ops {
	unsigned long (*read)(void *priv, unsigned int offset, int size);
	void (*write)(void *priv, unsigned int offset, int size,
		      unsigned long val);

	void (*set)(void *priv, unsigned int offset, u8 value, int size);
	void (*copy_from)(void *priv, void *buffer, unsigned int offset,
			  int size);
	void (*copy_to)(void *priv, unsigned int offset, const void *buffer,
			int size);

	void (*unmap)(void *priv);
};

/**
 * struct logic_iomem_region_ops - ops for an IO memory handler
 * @map: map a range in the registered IO memory region, must
 *	fill *ops with the ops and may fill *priv to be passed
 *	to the ops. The offset is given as the offset into the
 *	registered resource region.
 *	The return value is negative for errors, or >= 0 for
 *	success. On success, the return value is added to the
 *	offset for later ops, to allow for partial mappings.
 */
struct logic_iomem_region_ops {
	long (*map)(unsigned long offset, size_t size,
		    const struct logic_iomem_ops **ops,
		    void **priv);
};

/**
 * logic_iomem_add_region - register an IO memory region
 * @resource: the resource description for this region
 * @ops: the IO memory mapping ops for this resource
 */
int logic_iomem_add_region(struct resource *resource,
			   const struct logic_iomem_region_ops *ops);

#endif /* __LOGIC_IOMEM_H */
