/*
 * Copyright (c) 2015, Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/types.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include "tee_wait_queue.h"

struct tee_wait_queue {
	struct list_head link;
	struct completion comp;
	u32 key;
};

void tee_wait_queue_init(struct tee_wait_queue_private *priv)
{
	mutex_init(&priv->mu);
	INIT_LIST_HEAD(&priv->db);
}
EXPORT_SYMBOL(tee_wait_queue_init);

void tee_wait_queue_exit(struct tee_wait_queue_private *priv)
{
	mutex_destroy(&priv->mu);
}
EXPORT_SYMBOL(tee_wait_queue_exit);

static struct tee_wait_queue *tee_wait_queue_get(struct device *dev,
				struct tee_wait_queue_private *priv, u32 key)
{
	struct tee_wait_queue *w;

	mutex_lock(&priv->mu);

	list_for_each_entry(w, &priv->db, link)
		if (w->key == key)
			goto out;

	w = kmalloc(sizeof(struct tee_wait_queue), GFP_KERNEL);
	if (!w)
		goto out;

	init_completion(&w->comp);
	w->key = key;
	list_add_tail(&w->link, &priv->db);
out:
	mutex_unlock(&priv->mu);
	return w;
}

void tee_wait_queue_sleep(struct device *dev,
			struct tee_wait_queue_private *priv, u32 key)
{
	struct tee_wait_queue *w = tee_wait_queue_get(dev, priv, key);

	if (!w)
		return;

	wait_for_completion(&w->comp);
	mutex_lock(&priv->mu);
	list_del(&w->link);
	mutex_unlock(&priv->mu);
	kfree(w);
}
EXPORT_SYMBOL(tee_wait_queue_sleep);

void tee_wait_queue_wakeup(struct device *dev,
			struct tee_wait_queue_private *priv, u32 key)
{
	struct tee_wait_queue *w = tee_wait_queue_get(dev, priv, key);

	if (!w)
		return;

	complete(&w->comp);
}
EXPORT_SYMBOL(tee_wait_queue_wakeup);
