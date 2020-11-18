// SPDX-License-Identifier: GPL-2.0-only
/*
 * T10 Data Integrity Field CRC16 calculation
 *
 * Copyright (c) 2007 Oracle Corporation.  All rights reserved.
 * Written by Martin K. Petersen <martin.petersen@oracle.com>
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/crc-t10dif.h>
#include <linux/err.h>
#include <linux/init.h>
#include <crypto/hash.h>
#include <crypto/algapi.h>
#include <linux/static_key.h>
#include <linux/notifier.h>

static struct crypto_shash __rcu *crct10dif_tfm;
static DEFINE_STATIC_KEY_TRUE(crct10dif_fallback);
static DEFINE_MUTEX(crc_t10dif_mutex);
static struct work_struct crct10dif_rehash_work;

static int crc_t10dif_notify(struct notifier_block *self, unsigned long val, void *data)
{
	struct crypto_alg *alg = data;

	if (val != CRYPTO_MSG_ALG_LOADED ||
	    strcmp(alg->cra_name, CRC_T10DIF_STRING))
		return NOTIFY_DONE;

	schedule_work(&crct10dif_rehash_work);
	return NOTIFY_OK;
}

static void crc_t10dif_rehash(struct work_struct *work)
{
	struct crypto_shash *new, *old;

	mutex_lock(&crc_t10dif_mutex);
	old = rcu_dereference_protected(crct10dif_tfm,
					lockdep_is_held(&crc_t10dif_mutex));
	new = crypto_alloc_shash(CRC_T10DIF_STRING, 0, 0);
	if (IS_ERR(new)) {
		mutex_unlock(&crc_t10dif_mutex);
		return;
	}
	rcu_assign_pointer(crct10dif_tfm, new);
	mutex_unlock(&crc_t10dif_mutex);

	if (old) {
		synchronize_rcu();
		crypto_free_shash(old);
	} else {
		static_branch_disable(&crct10dif_fallback);
	}
}

static struct notifier_block crc_t10dif_nb = {
	.notifier_call = crc_t10dif_notify,
};

__u16 crc_t10dif_update(__u16 crc, const unsigned char *buffer, size_t len)
{
	struct {
		struct shash_desc shash;
		__u16 crc;
	} desc;
	int err;

	if (static_branch_unlikely(&crct10dif_fallback))
		return crc_t10dif_generic(crc, buffer, len);

	rcu_read_lock();
	desc.shash.tfm = rcu_dereference(crct10dif_tfm);
	desc.crc = crc;
	err = crypto_shash_update(&desc.shash, buffer, len);
	rcu_read_unlock();

	BUG_ON(err);

	return desc.crc;
}
EXPORT_SYMBOL(crc_t10dif_update);

__u16 crc_t10dif(const unsigned char *buffer, size_t len)
{
	return crc_t10dif_update(0, buffer, len);
}
EXPORT_SYMBOL(crc_t10dif);

static int __init crc_t10dif_mod_init(void)
{
	INIT_WORK(&crct10dif_rehash_work, crc_t10dif_rehash);
	crypto_register_notifier(&crc_t10dif_nb);
	crc_t10dif_rehash(&crct10dif_rehash_work);
	return 0;
}

static void __exit crc_t10dif_mod_fini(void)
{
	crypto_unregister_notifier(&crc_t10dif_nb);
	cancel_work_sync(&crct10dif_rehash_work);
	crypto_free_shash(rcu_dereference_protected(crct10dif_tfm, 1));
}

module_init(crc_t10dif_mod_init);
module_exit(crc_t10dif_mod_fini);

static int crc_t10dif_transform_show(char *buffer, const struct kernel_param *kp)
{
	struct crypto_shash *tfm;
	int len;

	if (static_branch_unlikely(&crct10dif_fallback))
		return sprintf(buffer, "fallback\n");

	rcu_read_lock();
	tfm = rcu_dereference(crct10dif_tfm);
	len = snprintf(buffer, PAGE_SIZE, "%s\n",
		       crypto_shash_driver_name(tfm));
	rcu_read_unlock();

	return len;
}

module_param_call(transform, NULL, crc_t10dif_transform_show, NULL, 0444);

MODULE_DESCRIPTION("T10 DIF CRC calculation (library API)");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: crct10dif");
