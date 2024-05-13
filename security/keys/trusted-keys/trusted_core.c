// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 IBM Corporation
 * Copyright (c) 2019-2021, Linaro Limited
 *
 * See Documentation/security/keys/trusted-encrypted.rst
 */

#include <keys/user-type.h>
#include <keys/trusted-type.h>
#include <keys/trusted_tee.h>
#include <keys/trusted_caam.h>
#include <keys/trusted_tpm.h>
#include <linux/capability.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/key-type.h>
#include <linux/module.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/static_call.h>
#include <linux/string.h>
#include <linux/uaccess.h>

static char *trusted_rng = "default";
module_param_named(rng, trusted_rng, charp, 0);
MODULE_PARM_DESC(rng, "Select trusted key RNG");

static char *trusted_key_source;
module_param_named(source, trusted_key_source, charp, 0);
MODULE_PARM_DESC(source, "Select trusted keys source (tpm, tee or caam)");

static const struct trusted_key_source trusted_key_sources[] = {
#if defined(CONFIG_TRUSTED_KEYS_TPM)
	{ "tpm", &trusted_key_tpm_ops },
#endif
#if defined(CONFIG_TRUSTED_KEYS_TEE)
	{ "tee", &trusted_key_tee_ops },
#endif
#if defined(CONFIG_TRUSTED_KEYS_CAAM)
	{ "caam", &trusted_key_caam_ops },
#endif
};

DEFINE_STATIC_CALL_NULL(trusted_key_seal, *trusted_key_sources[0].ops->seal);
DEFINE_STATIC_CALL_NULL(trusted_key_unseal,
			*trusted_key_sources[0].ops->unseal);
DEFINE_STATIC_CALL_NULL(trusted_key_get_random,
			*trusted_key_sources[0].ops->get_random);
static void (*trusted_key_exit)(void);
static unsigned char migratable;

enum {
	Opt_err,
	Opt_new, Opt_load, Opt_update,
};

static const match_table_t key_tokens = {
	{Opt_new, "new"},
	{Opt_load, "load"},
	{Opt_update, "update"},
	{Opt_err, NULL}
};

/*
 * datablob_parse - parse the keyctl data and fill in the
 *                  payload structure
 *
 * On success returns 0, otherwise -EINVAL.
 */
static int datablob_parse(char **datablob, struct trusted_key_payload *p)
{
	substring_t args[MAX_OPT_ARGS];
	long keylen;
	int ret = -EINVAL;
	int key_cmd;
	char *c;

	/* main command */
	c = strsep(datablob, " \t");
	if (!c)
		return -EINVAL;
	key_cmd = match_token(c, key_tokens, args);
	switch (key_cmd) {
	case Opt_new:
		/* first argument is key size */
		c = strsep(datablob, " \t");
		if (!c)
			return -EINVAL;
		ret = kstrtol(c, 10, &keylen);
		if (ret < 0 || keylen < MIN_KEY_SIZE || keylen > MAX_KEY_SIZE)
			return -EINVAL;
		p->key_len = keylen;
		ret = Opt_new;
		break;
	case Opt_load:
		/* first argument is sealed blob */
		c = strsep(datablob, " \t");
		if (!c)
			return -EINVAL;
		p->blob_len = strlen(c) / 2;
		if (p->blob_len > MAX_BLOB_SIZE)
			return -EINVAL;
		ret = hex2bin(p->blob, c, p->blob_len);
		if (ret < 0)
			return -EINVAL;
		ret = Opt_load;
		break;
	case Opt_update:
		ret = Opt_update;
		break;
	case Opt_err:
		return -EINVAL;
	}
	return ret;
}

static struct trusted_key_payload *trusted_payload_alloc(struct key *key)
{
	struct trusted_key_payload *p = NULL;
	int ret;

	ret = key_payload_reserve(key, sizeof(*p));
	if (ret < 0)
		goto err;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		goto err;

	p->migratable = migratable;
err:
	return p;
}

/*
 * trusted_instantiate - create a new trusted key
 *
 * Unseal an existing trusted blob or, for a new key, get a
 * random key, then seal and create a trusted key-type key,
 * adding it to the specified keyring.
 *
 * On success, return 0. Otherwise return errno.
 */
static int trusted_instantiate(struct key *key,
			       struct key_preparsed_payload *prep)
{
	struct trusted_key_payload *payload = NULL;
	size_t datalen = prep->datalen;
	char *datablob, *orig_datablob;
	int ret = 0;
	int key_cmd;
	size_t key_len;

	if (datalen <= 0 || datalen > 32767 || !prep->data)
		return -EINVAL;

	orig_datablob = datablob = kmalloc(datalen + 1, GFP_KERNEL);
	if (!datablob)
		return -ENOMEM;
	memcpy(datablob, prep->data, datalen);
	datablob[datalen] = '\0';

	payload = trusted_payload_alloc(key);
	if (!payload) {
		ret = -ENOMEM;
		goto out;
	}

	key_cmd = datablob_parse(&datablob, payload);
	if (key_cmd < 0) {
		ret = key_cmd;
		goto out;
	}

	dump_payload(payload);

	switch (key_cmd) {
	case Opt_load:
		ret = static_call(trusted_key_unseal)(payload, datablob);
		dump_payload(payload);
		if (ret < 0)
			pr_info("key_unseal failed (%d)\n", ret);
		break;
	case Opt_new:
		key_len = payload->key_len;
		ret = static_call(trusted_key_get_random)(payload->key,
							  key_len);
		if (ret < 0)
			goto out;

		if (ret != key_len) {
			pr_info("key_create failed (%d)\n", ret);
			ret = -EIO;
			goto out;
		}

		ret = static_call(trusted_key_seal)(payload, datablob);
		if (ret < 0)
			pr_info("key_seal failed (%d)\n", ret);
		break;
	default:
		ret = -EINVAL;
	}
out:
	kfree_sensitive(orig_datablob);
	if (!ret)
		rcu_assign_keypointer(key, payload);
	else
		kfree_sensitive(payload);
	return ret;
}

static void trusted_rcu_free(struct rcu_head *rcu)
{
	struct trusted_key_payload *p;

	p = container_of(rcu, struct trusted_key_payload, rcu);
	kfree_sensitive(p);
}

/*
 * trusted_update - reseal an existing key with new PCR values
 */
static int trusted_update(struct key *key, struct key_preparsed_payload *prep)
{
	struct trusted_key_payload *p;
	struct trusted_key_payload *new_p;
	size_t datalen = prep->datalen;
	char *datablob, *orig_datablob;
	int ret = 0;

	if (key_is_negative(key))
		return -ENOKEY;
	p = key->payload.data[0];
	if (!p->migratable)
		return -EPERM;
	if (datalen <= 0 || datalen > 32767 || !prep->data)
		return -EINVAL;

	orig_datablob = datablob = kmalloc(datalen + 1, GFP_KERNEL);
	if (!datablob)
		return -ENOMEM;

	new_p = trusted_payload_alloc(key);
	if (!new_p) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(datablob, prep->data, datalen);
	datablob[datalen] = '\0';
	ret = datablob_parse(&datablob, new_p);
	if (ret != Opt_update) {
		ret = -EINVAL;
		kfree_sensitive(new_p);
		goto out;
	}

	/* copy old key values, and reseal with new pcrs */
	new_p->migratable = p->migratable;
	new_p->key_len = p->key_len;
	memcpy(new_p->key, p->key, p->key_len);
	dump_payload(p);
	dump_payload(new_p);

	ret = static_call(trusted_key_seal)(new_p, datablob);
	if (ret < 0) {
		pr_info("key_seal failed (%d)\n", ret);
		kfree_sensitive(new_p);
		goto out;
	}

	rcu_assign_keypointer(key, new_p);
	call_rcu(&p->rcu, trusted_rcu_free);
out:
	kfree_sensitive(orig_datablob);
	return ret;
}

/*
 * trusted_read - copy the sealed blob data to userspace in hex.
 * On success, return to userspace the trusted key datablob size.
 */
static long trusted_read(const struct key *key, char *buffer,
			 size_t buflen)
{
	const struct trusted_key_payload *p;
	char *bufp;
	int i;

	p = dereference_key_locked(key);
	if (!p)
		return -EINVAL;

	if (buffer && buflen >= 2 * p->blob_len) {
		bufp = buffer;
		for (i = 0; i < p->blob_len; i++)
			bufp = hex_byte_pack(bufp, p->blob[i]);
	}
	return 2 * p->blob_len;
}

/*
 * trusted_destroy - clear and free the key's payload
 */
static void trusted_destroy(struct key *key)
{
	kfree_sensitive(key->payload.data[0]);
}

struct key_type key_type_trusted = {
	.name = "trusted",
	.instantiate = trusted_instantiate,
	.update = trusted_update,
	.destroy = trusted_destroy,
	.describe = user_describe,
	.read = trusted_read,
};
EXPORT_SYMBOL_GPL(key_type_trusted);

static int kernel_get_random(unsigned char *key, size_t key_len)
{
	return get_random_bytes_wait(key, key_len) ?: key_len;
}

static int __init init_trusted(void)
{
	int (*get_random)(unsigned char *key, size_t key_len);
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(trusted_key_sources); i++) {
		if (trusted_key_source &&
		    strncmp(trusted_key_source, trusted_key_sources[i].name,
			    strlen(trusted_key_sources[i].name)))
			continue;

		/*
		 * We always support trusted.rng="kernel" and "default" as
		 * well as trusted.rng=$trusted.source if the trust source
		 * defines its own get_random callback.
		 */
		get_random = trusted_key_sources[i].ops->get_random;
		if (trusted_rng && strcmp(trusted_rng, "default")) {
			if (!strcmp(trusted_rng, "kernel")) {
				get_random = kernel_get_random;
			} else if (strcmp(trusted_rng, trusted_key_sources[i].name) ||
				   !get_random) {
				pr_warn("Unsupported RNG. Supported: kernel");
				if (get_random)
					pr_cont(", %s", trusted_key_sources[i].name);
				pr_cont(", default\n");
				return -EINVAL;
			}
		}

		if (!get_random)
			get_random = kernel_get_random;

		static_call_update(trusted_key_seal,
				   trusted_key_sources[i].ops->seal);
		static_call_update(trusted_key_unseal,
				   trusted_key_sources[i].ops->unseal);
		static_call_update(trusted_key_get_random,
				   get_random);
		trusted_key_exit = trusted_key_sources[i].ops->exit;
		migratable = trusted_key_sources[i].ops->migratable;

		ret = trusted_key_sources[i].ops->init();
		if (!ret)
			break;
	}

	/*
	 * encrypted_keys.ko depends on successful load of this module even if
	 * trusted key implementation is not found.
	 */
	if (ret == -ENODEV)
		return 0;

	return ret;
}

static void __exit cleanup_trusted(void)
{
	if (trusted_key_exit)
		(*trusted_key_exit)();
}

late_initcall(init_trusted);
module_exit(cleanup_trusted);

MODULE_LICENSE("GPL");
