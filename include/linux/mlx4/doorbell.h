/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MLX4_DOORBELL_H
#define MLX4_DOORBELL_H

#include <linux/types.h>
#include <linux/io.h>

#define MLX4_SEND_DOORBELL    0x14
#define MLX4_CQ_DOORBELL      0x20

#if BITS_PER_LONG == 64
/*
 * Assume that we can just write a 64-bit doorbell atomically.  s390
 * actually doesn't have writeq() but S/390 systems don't even have
 * PCI so we won't worry about it.
 */

#define MLX4_DECLARE_DOORBELL_LOCK(name)
#define MLX4_INIT_DOORBELL_LOCK(ptr)    do { } while (0)
#define MLX4_GET_DOORBELL_LOCK(ptr)      (NULL)

static inline void mlx4_write64_raw(__be64 val, void __iomem *dest)
{
	__raw_writeq((__force u64) val, dest);
}

static inline void mlx4_write64(__be32 val[2], void __iomem *dest,
				spinlock_t *doorbell_lock)
{
	__raw_writeq(*(u64 *) val, dest);
}

#else

/*
 * Just fall back to a spinlock to protect the doorbell if
 * BITS_PER_LONG is 32 -- there's no portable way to do atomic 64-bit
 * MMIO writes.
 */

#define MLX4_DECLARE_DOORBELL_LOCK(name) spinlock_t name;
#define MLX4_INIT_DOORBELL_LOCK(ptr)     spin_lock_init(ptr)
#define MLX4_GET_DOORBELL_LOCK(ptr)      (ptr)

static inline void mlx4_write64_raw(__be64 val, void __iomem *dest)
{
	__raw_writel(((__force u32 *) &val)[0], dest);
	__raw_writel(((__force u32 *) &val)[1], dest + 4);
}

static inline void mlx4_write64(__be32 val[2], void __iomem *dest,
				spinlock_t *doorbell_lock)
{
	unsigned long flags;

	spin_lock_irqsave(doorbell_lock, flags);
	__raw_writel((__force u32) val[0], dest);
	__raw_writel((__force u32) val[1], dest + 4);
	spin_unlock_irqrestore(doorbell_lock, flags);
}

#endif

#endif /* MLX4_DOORBELL_H */
