/* Support for hardware buffer manager.
 *
 * Copyright (C) 2016 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <net/hwbm.h>

void hwbm_buf_free(struct hwbm_pool *bm_pool, void *buf)
{
	if (likely(bm_pool->frag_size <= PAGE_SIZE))
		skb_free_frag(buf);
	else
		kfree(buf);
}
EXPORT_SYMBOL_GPL(hwbm_buf_free);

/* Refill processing for HW buffer management */
int hwbm_pool_refill(struct hwbm_pool *bm_pool, gfp_t gfp)
{
	int frag_size = bm_pool->frag_size;
	void *buf;

	if (likely(frag_size <= PAGE_SIZE))
		buf = netdev_alloc_frag(frag_size);
	else
		buf = kmalloc(frag_size, gfp);

	if (!buf)
		return -ENOMEM;

	if (bm_pool->construct)
		if (bm_pool->construct(bm_pool, buf)) {
			hwbm_buf_free(bm_pool, buf);
			return -ENOMEM;
		}

	return 0;
}
EXPORT_SYMBOL_GPL(hwbm_pool_refill);

int hwbm_pool_add(struct hwbm_pool *bm_pool, unsigned int buf_num, gfp_t gfp)
{
	int err, i;
	unsigned long flags;

	spin_lock_irqsave(&bm_pool->lock, flags);
	if (bm_pool->buf_num == bm_pool->size) {
		pr_warn("pool already filled\n");
		spin_unlock_irqrestore(&bm_pool->lock, flags);
		return bm_pool->buf_num;
	}

	if (buf_num + bm_pool->buf_num > bm_pool->size) {
		pr_warn("cannot allocate %d buffers for pool\n",
			buf_num);
		spin_unlock_irqrestore(&bm_pool->lock, flags);
		return 0;
	}

	if ((buf_num + bm_pool->buf_num) < bm_pool->buf_num) {
		pr_warn("Adding %d buffers to the %d current buffers will overflow\n",
			buf_num,  bm_pool->buf_num);
		spin_unlock_irqrestore(&bm_pool->lock, flags);
		return 0;
	}

	for (i = 0; i < buf_num; i++) {
		err = hwbm_pool_refill(bm_pool, gfp);
		if (err < 0)
			break;
	}

	/* Update BM driver with number of buffers added to pool */
	bm_pool->buf_num += i;

	pr_debug("hwpm pool: %d of %d buffers added\n", i, buf_num);
	spin_unlock_irqrestore(&bm_pool->lock, flags);

	return i;
}
EXPORT_SYMBOL_GPL(hwbm_pool_add);
