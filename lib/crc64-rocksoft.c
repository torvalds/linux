// SPDX-License-Identifier: GPL-2.0-only

#include <linux/types.h>
#include <linux/module.h>
#include <linux/crc64.h>
#include <linux/err.h>
#include <linux/init.h>
#include <crypto/hash.h>
#include <crypto/algapi.h>
#include <linux/static_key.h>
#include <linux/notifier.h>

static struct crypto_shash __rcu *crc64_rocksoft_tfm;
static DEFINE_STATIC_KEY_TRUE(crc64_rocksoft_fallback);
static DEFINE_MUTEX(crc64_rocksoft_mutex);
static struct work_struct crc64_rocksoft_rehash_work;

static int crc64_rocksoft_notify(struct notifier_block *self, unsigned long val, void *data)
{
	struct crypto_alg *alg = data;

	if (val != CRYPTO_MSG_ALG_LOADED ||
	    strcmp(alg->cra_name, CRC64_ROCKSOFT_STRING))
		return NOTIFY_DONE;

	schedule_work(&crc64_rocksoft_rehash_work);
	return NOTIFY_OK;
}

static void crc64_rocksoft_rehash(struct work_struct *work)
{
	struct crypto_shash *new, *old;

	mutex_lock(&crc64_rocksoft_mutex);
	old = rcu_dereference_protected(crc64_rocksoft_tfm,
					lockdep_is_held(&crc64_rocksoft_mutex));
	new = crypto_alloc_shash(CRC64_ROCKSOFT_STRING, 0, 0);
	if (IS_ERR(new)) {
		mutex_unlock(&crc64_rocksoft_mutex);
		return;
	}
	rcu_assign_pointer(crc64_rocksoft_tfm, new);
	mutex_unlock(&crc64_rocksoft_mutex);

	if (old) {
		synchronize_rcu();
		crypto_free_shash(old);
	} else {
		static_branch_disable(&crc64_rocksoft_fallback);
	}
}

static struct notifier_block crc64_rocksoft_nb = {
	.notifier_call = crc64_rocksoft_notify,
};

u64 crc64_rocksoft_update(u64 crc, const unsigned char *buffer, size_t len)
{
	struct {
		struct shash_desc shash;
		u64 crc;
	} desc;
	int err;

	if (static_branch_unlikely(&crc64_rocksoft_fallback))
		return crc64_rocksoft_generic(crc, buffer, len);

	rcu_read_lock();
	desc.shash.tfm = rcu_dereference(crc64_rocksoft_tfm);
	desc.crc = crc;
	err = crypto_shash_update(&desc.shash, buffer, len);
	rcu_read_unlock();

	BUG_ON(err);

	return desc.crc;
}
EXPORT_SYMBOL_GPL(crc64_rocksoft_update);

u64 crc64_rocksoft(const unsigned char *buffer, size_t len)
{
	return crc64_rocksoft_update(0, buffer, len);
}
EXPORT_SYMBOL_GPL(crc64_rocksoft);

static int __init crc64_rocksoft_mod_init(void)
{
	INIT_WORK(&crc64_rocksoft_rehash_work, crc64_rocksoft_rehash);
	crypto_register_notifier(&crc64_rocksoft_nb);
	crc64_rocksoft_rehash(&crc64_rocksoft_rehash_work);
	return 0;
}

static void __exit crc64_rocksoft_mod_fini(void)
{
	crypto_unregister_notifier(&crc64_rocksoft_nb);
	cancel_work_sync(&crc64_rocksoft_rehash_work);
	crypto_free_shash(rcu_dereference_protected(crc64_rocksoft_tfm, 1));
}

module_init(crc64_rocksoft_mod_init);
module_exit(crc64_rocksoft_mod_fini);

static int crc64_rocksoft_transform_show(char *buffer, const struct kernel_param *kp)
{
	struct crypto_shash *tfm;
	int len;

	if (static_branch_unlikely(&crc64_rocksoft_fallback))
		return sprintf(buffer, "fallback\n");

	rcu_read_lock();
	tfm = rcu_dereference(crc64_rocksoft_tfm);
	len = snprintf(buffer, PAGE_SIZE, "%s\n",
		       crypto_shash_driver_name(tfm));
	rcu_read_unlock();

	return len;
}

module_param_call(transform, NULL, crc64_rocksoft_transform_show, NULL, 0444);

MODULE_AUTHOR("Keith Busch <kbusch@kernel.org>");
MODULE_DESCRIPTION("Rocksoft model CRC64 calculation (library API)");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: crc64");
