/*
 * Host AP crypto routines
 *
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Portions Copyright (C) 2004, Intel Corporation <jketreno@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 *
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/ieee80211.h>

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("HostAP crypto");
MODULE_LICENSE("GPL");

struct ieee80211_crypto_alg {
	struct list_head list;
	struct ieee80211_crypto_ops *ops;
};

static LIST_HEAD(ieee80211_crypto_algs);
static DEFINE_SPINLOCK(ieee80211_crypto_lock);

void ieee80211_crypt_deinit_entries(struct ieee80211_device *ieee, int force)
{
	struct ieee80211_crypt_data *entry, *next;
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock, flags);
	list_for_each_entry_safe(entry, next, &ieee->crypt_deinit_list, list) {
		if (atomic_read(&entry->refcnt) != 0 && !force)
			continue;

		list_del(&entry->list);

		if (entry->ops) {
			entry->ops->deinit(entry->priv);
			module_put(entry->ops->owner);
		}
		kfree(entry);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

/* After this, crypt_deinit_list won't accept new members */
void ieee80211_crypt_quiescing(struct ieee80211_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock, flags);
	ieee->crypt_quiesced = 1;
	spin_unlock_irqrestore(&ieee->lock, flags);
}

void ieee80211_crypt_deinit_handler(unsigned long data)
{
	struct ieee80211_device *ieee = (struct ieee80211_device *)data;
	unsigned long flags;

	ieee80211_crypt_deinit_entries(ieee, 0);

	spin_lock_irqsave(&ieee->lock, flags);
	if (!list_empty(&ieee->crypt_deinit_list) && !ieee->crypt_quiesced) {
		printk(KERN_DEBUG "%s: entries remaining in delayed crypt "
		       "deletion list\n", ieee->dev->name);
		ieee->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&ieee->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

void ieee80211_crypt_delayed_deinit(struct ieee80211_device *ieee,
				    struct ieee80211_crypt_data **crypt)
{
	struct ieee80211_crypt_data *tmp;
	unsigned long flags;

	if (*crypt == NULL)
		return;

	tmp = *crypt;
	*crypt = NULL;

	/* must not run ops->deinit() while there may be pending encrypt or
	 * decrypt operations. Use a list of delayed deinits to avoid needing
	 * locking. */

	spin_lock_irqsave(&ieee->lock, flags);
	if (!ieee->crypt_quiesced) {
		list_add(&tmp->list, &ieee->crypt_deinit_list);
		if (!timer_pending(&ieee->crypt_deinit_timer)) {
			ieee->crypt_deinit_timer.expires = jiffies + HZ;
			add_timer(&ieee->crypt_deinit_timer);
		}
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

int ieee80211_register_crypto_ops(struct ieee80211_crypto_ops *ops)
{
	unsigned long flags;
	struct ieee80211_crypto_alg *alg;

	alg = kzalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL)
		return -ENOMEM;

	alg->ops = ops;

	spin_lock_irqsave(&ieee80211_crypto_lock, flags);
	list_add(&alg->list, &ieee80211_crypto_algs);
	spin_unlock_irqrestore(&ieee80211_crypto_lock, flags);

	printk(KERN_DEBUG "ieee80211_crypt: registered algorithm '%s'\n",
	       ops->name);

	return 0;
}

int ieee80211_unregister_crypto_ops(struct ieee80211_crypto_ops *ops)
{
	struct ieee80211_crypto_alg *alg;
	unsigned long flags;

	spin_lock_irqsave(&ieee80211_crypto_lock, flags);
	list_for_each_entry(alg, &ieee80211_crypto_algs, list) {
		if (alg->ops == ops)
			goto found;
	}
	spin_unlock_irqrestore(&ieee80211_crypto_lock, flags);
	return -EINVAL;

      found:
	printk(KERN_DEBUG "ieee80211_crypt: unregistered algorithm "
	       "'%s'\n", ops->name);
	list_del(&alg->list);
	spin_unlock_irqrestore(&ieee80211_crypto_lock, flags);
	kfree(alg);
	return 0;
}

struct ieee80211_crypto_ops *ieee80211_get_crypto_ops(const char *name)
{
	struct ieee80211_crypto_alg *alg;
	unsigned long flags;

	spin_lock_irqsave(&ieee80211_crypto_lock, flags);
	list_for_each_entry(alg, &ieee80211_crypto_algs, list) {
		if (strcmp(alg->ops->name, name) == 0)
			goto found;
	}
	spin_unlock_irqrestore(&ieee80211_crypto_lock, flags);
	return NULL;

      found:
	spin_unlock_irqrestore(&ieee80211_crypto_lock, flags);
	return alg->ops;
}

static void *ieee80211_crypt_null_init(int keyidx)
{
	return (void *)1;
}

static void ieee80211_crypt_null_deinit(void *priv)
{
}

static struct ieee80211_crypto_ops ieee80211_crypt_null = {
	.name = "NULL",
	.init = ieee80211_crypt_null_init,
	.deinit = ieee80211_crypt_null_deinit,
	.owner = THIS_MODULE,
};

static int __init ieee80211_crypto_init(void)
{
	return ieee80211_register_crypto_ops(&ieee80211_crypt_null);
}

static void __exit ieee80211_crypto_deinit(void)
{
	ieee80211_unregister_crypto_ops(&ieee80211_crypt_null);
	BUG_ON(!list_empty(&ieee80211_crypto_algs));
}

EXPORT_SYMBOL(ieee80211_crypt_deinit_entries);
EXPORT_SYMBOL(ieee80211_crypt_deinit_handler);
EXPORT_SYMBOL(ieee80211_crypt_delayed_deinit);
EXPORT_SYMBOL(ieee80211_crypt_quiescing);

EXPORT_SYMBOL(ieee80211_register_crypto_ops);
EXPORT_SYMBOL(ieee80211_unregister_crypto_ops);
EXPORT_SYMBOL(ieee80211_get_crypto_ops);

module_init(ieee80211_crypto_init);
module_exit(ieee80211_crypto_deinit);
