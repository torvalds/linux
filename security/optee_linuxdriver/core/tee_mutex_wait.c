/*
 * Copyright (c) 2014, Linaro Limited
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
#include <linux/slab.h>
#include "tee_mutex_wait.h"

struct tee_mutex_wait {
	struct list_head link;
	struct completion comp;
	struct mutex mu;
	u32 wait_after;
	u32 key;
};

/*
 * Compares two serial numbers using Serial Number Arithmetic
 * (https://www.ietf.org/rfc/rfc1982.txt).
 */
#define TICK_GT(t1, t2) \
	(((t1) < (t2) && (t2) - (t1) > 0xFFFFFFFFu) || \
	((t1) > (t2) && (t1) - (t2) < 0xFFFFFFFFu))

static struct tee_mutex_wait *tee_mutex_wait_get(struct device *dev,
				struct tee_mutex_wait_private *priv, u32 key)
{
	struct tee_mutex_wait *w;

	mutex_lock(&priv->mu);

	list_for_each_entry(w, &priv->db, link)
		if (w->key == key)
			goto out;

	w = kmalloc(sizeof(struct tee_mutex_wait), GFP_KERNEL);
	if (!w) {
		dev_err(dev, "kmalloc <struct tee_mutex_wait> failed\n");
		goto out;
	}

	init_completion(&w->comp);
	mutex_init(&w->mu);
	w->wait_after = 0;
	w->key = key;
	list_add_tail(&w->link, &priv->db);
out:
	mutex_unlock(&priv->mu);
	return w;
}

static void tee_mutex_wait_delete_entry(struct tee_mutex_wait *w)
{
	list_del(&w->link);
	mutex_destroy(&w->mu);
	kfree(w);
}

void tee_mutex_wait_delete(struct device *dev,
			struct tee_mutex_wait_private *priv,
			u32 key)
{
	struct tee_mutex_wait *w;

	mutex_lock(&priv->mu);

	list_for_each_entry(w, &priv->db, link) {
		if (w->key == key) {
			tee_mutex_wait_delete_entry(w);
			break;
		}
	}

	mutex_unlock(&priv->mu);
}
EXPORT_SYMBOL(tee_mutex_wait_delete);

void tee_mutex_wait_wakeup(struct device *dev,
			struct tee_mutex_wait_private *priv,
			u32 key, u32 wait_after)
{
	struct tee_mutex_wait *w = tee_mutex_wait_get(dev, priv, key);

	if (!w)
		return;

	mutex_lock(&w->mu);
	w->wait_after = wait_after;
	mutex_unlock(&w->mu);
	complete(&w->comp);
}
EXPORT_SYMBOL(tee_mutex_wait_wakeup);

void tee_mutex_wait_sleep(struct device *dev,
			struct tee_mutex_wait_private *priv,
			u32 key, u32 wait_tick)
{
	struct tee_mutex_wait *w = tee_mutex_wait_get(dev, priv, key);
	u32 wait_after;

	if (!w)
		return;

	mutex_lock(&w->mu);
	wait_after = w->wait_after;
	mutex_unlock(&w->mu);

	if (TICK_GT(wait_tick, wait_after))
		wait_for_completion_timeout(&w->comp, HZ);
}
EXPORT_SYMBOL(tee_mutex_wait_sleep);

int tee_mutex_wait_init(struct tee_mutex_wait_private *priv)
{
	mutex_init(&priv->mu);
	INIT_LIST_HEAD(&priv->db);
	return 0;
}
EXPORT_SYMBOL(tee_mutex_wait_init);

void tee_mutex_wait_exit(struct tee_mutex_wait_private *priv)
{
	/*
	 * It's the callers responibility to ensure that no one is using
	 * anything inside priv.
	 */

	mutex_destroy(&priv->mu);
	while (!list_empty(&priv->db)) {
		struct tee_mutex_wait *w =
				list_first_entry(&priv->db,
						 struct tee_mutex_wait,
						 link);
		tee_mutex_wait_delete_entry(w);
	}
}
EXPORT_SYMBOL(tee_mutex_wait_exit);
