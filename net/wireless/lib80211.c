/*
 * lib80211 -- common bits for IEEE802.11 drivers
 *
 * Copyright(c) 2008 John W. Linville <linville@tuxdriver.com>
 *
 * Portions copied from old ieee80211 component, w/ original copyright
 * notices below:
 *
 * Host AP crypto routines
 *
 * Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>
 * Portions Copyright (C) 2004, Intel Corporation <jketreno@linux.intel.com>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/ieee80211.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <net/lib80211.h>

#define DRV_NAME        "lib80211"

#define DRV_DESCRIPTION	"common routines for IEEE802.11 drivers"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("John W. Linville <linville@tuxdriver.com>");
MODULE_LICENSE("GPL");

struct lib80211_crypto_alg {
	struct list_head list;
	struct lib80211_crypto_ops *ops;
};

static LIST_HEAD(lib80211_crypto_algs);
static DEFINE_SPINLOCK(lib80211_crypto_lock);

static void lib80211_crypt_deinit_entries(struct lib80211_crypt_info *info,
					  int force);
static void lib80211_crypt_quiescing(struct lib80211_crypt_info *info);
static void lib80211_crypt_deinit_handler(struct timer_list *t);

int lib80211_crypt_info_init(struct lib80211_crypt_info *info, char *name,
				spinlock_t *lock)
{
	memset(info, 0, sizeof(*info));

	info->name = name;
	info->lock = lock;

	INIT_LIST_HEAD(&info->crypt_deinit_list);
	timer_setup(&info->crypt_deinit_timer, lib80211_crypt_deinit_handler,
		    0);

	return 0;
}
EXPORT_SYMBOL(lib80211_crypt_info_init);

void lib80211_crypt_info_free(struct lib80211_crypt_info *info)
{
	int i;

        lib80211_crypt_quiescing(info);
        del_timer_sync(&info->crypt_deinit_timer);
        lib80211_crypt_deinit_entries(info, 1);

        for (i = 0; i < NUM_WEP_KEYS; i++) {
                struct lib80211_crypt_data *crypt = info->crypt[i];
                if (crypt) {
                        if (crypt->ops) {
                                crypt->ops->deinit(crypt->priv);
                                module_put(crypt->ops->owner);
                        }
                        kfree(crypt);
                        info->crypt[i] = NULL;
                }
        }
}
EXPORT_SYMBOL(lib80211_crypt_info_free);

static void lib80211_crypt_deinit_entries(struct lib80211_crypt_info *info,
					  int force)
{
	struct lib80211_crypt_data *entry, *next;
	unsigned long flags;

	spin_lock_irqsave(info->lock, flags);
	list_for_each_entry_safe(entry, next, &info->crypt_deinit_list, list) {
		if (atomic_read(&entry->refcnt) != 0 && !force)
			continue;

		list_del(&entry->list);

		if (entry->ops) {
			entry->ops->deinit(entry->priv);
			module_put(entry->ops->owner);
		}
		kfree(entry);
	}
	spin_unlock_irqrestore(info->lock, flags);
}

/* After this, crypt_deinit_list won't accept new members */
static void lib80211_crypt_quiescing(struct lib80211_crypt_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(info->lock, flags);
	info->crypt_quiesced = 1;
	spin_unlock_irqrestore(info->lock, flags);
}

static void lib80211_crypt_deinit_handler(struct timer_list *t)
{
	struct lib80211_crypt_info *info = from_timer(info, t,
						      crypt_deinit_timer);
	unsigned long flags;

	lib80211_crypt_deinit_entries(info, 0);

	spin_lock_irqsave(info->lock, flags);
	if (!list_empty(&info->crypt_deinit_list) && !info->crypt_quiesced) {
		printk(KERN_DEBUG "%s: entries remaining in delayed crypt "
		       "deletion list\n", info->name);
		info->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&info->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(info->lock, flags);
}

void lib80211_crypt_delayed_deinit(struct lib80211_crypt_info *info,
				    struct lib80211_crypt_data **crypt)
{
	struct lib80211_crypt_data *tmp;
	unsigned long flags;

	if (*crypt == NULL)
		return;

	tmp = *crypt;
	*crypt = NULL;

	/* must not run ops->deinit() while there may be pending encrypt or
	 * decrypt operations. Use a list of delayed deinits to avoid needing
	 * locking. */

	spin_lock_irqsave(info->lock, flags);
	if (!info->crypt_quiesced) {
		list_add(&tmp->list, &info->crypt_deinit_list);
		if (!timer_pending(&info->crypt_deinit_timer)) {
			info->crypt_deinit_timer.expires = jiffies + HZ;
			add_timer(&info->crypt_deinit_timer);
		}
	}
	spin_unlock_irqrestore(info->lock, flags);
}
EXPORT_SYMBOL(lib80211_crypt_delayed_deinit);

int lib80211_register_crypto_ops(struct lib80211_crypto_ops *ops)
{
	unsigned long flags;
	struct lib80211_crypto_alg *alg;

	alg = kzalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL)
		return -ENOMEM;

	alg->ops = ops;

	spin_lock_irqsave(&lib80211_crypto_lock, flags);
	list_add(&alg->list, &lib80211_crypto_algs);
	spin_unlock_irqrestore(&lib80211_crypto_lock, flags);

	printk(KERN_DEBUG "lib80211_crypt: registered algorithm '%s'\n",
	       ops->name);

	return 0;
}
EXPORT_SYMBOL(lib80211_register_crypto_ops);

int lib80211_unregister_crypto_ops(struct lib80211_crypto_ops *ops)
{
	struct lib80211_crypto_alg *alg;
	unsigned long flags;

	spin_lock_irqsave(&lib80211_crypto_lock, flags);
	list_for_each_entry(alg, &lib80211_crypto_algs, list) {
		if (alg->ops == ops)
			goto found;
	}
	spin_unlock_irqrestore(&lib80211_crypto_lock, flags);
	return -EINVAL;

      found:
	printk(KERN_DEBUG "lib80211_crypt: unregistered algorithm '%s'\n",
	       ops->name);
	list_del(&alg->list);
	spin_unlock_irqrestore(&lib80211_crypto_lock, flags);
	kfree(alg);
	return 0;
}
EXPORT_SYMBOL(lib80211_unregister_crypto_ops);

struct lib80211_crypto_ops *lib80211_get_crypto_ops(const char *name)
{
	struct lib80211_crypto_alg *alg;
	unsigned long flags;

	spin_lock_irqsave(&lib80211_crypto_lock, flags);
	list_for_each_entry(alg, &lib80211_crypto_algs, list) {
		if (strcmp(alg->ops->name, name) == 0)
			goto found;
	}
	spin_unlock_irqrestore(&lib80211_crypto_lock, flags);
	return NULL;

      found:
	spin_unlock_irqrestore(&lib80211_crypto_lock, flags);
	return alg->ops;
}
EXPORT_SYMBOL(lib80211_get_crypto_ops);

static void *lib80211_crypt_null_init(int keyidx)
{
	return (void *)1;
}

static void lib80211_crypt_null_deinit(void *priv)
{
}

static struct lib80211_crypto_ops lib80211_crypt_null = {
	.name = "NULL",
	.init = lib80211_crypt_null_init,
	.deinit = lib80211_crypt_null_deinit,
	.owner = THIS_MODULE,
};

static int __init lib80211_init(void)
{
	pr_info(DRV_DESCRIPTION "\n");
	return lib80211_register_crypto_ops(&lib80211_crypt_null);
}

static void __exit lib80211_exit(void)
{
	lib80211_unregister_crypto_ops(&lib80211_crypt_null);
	BUG_ON(!list_empty(&lib80211_crypto_algs));
}

module_init(lib80211_init);
module_exit(lib80211_exit);
