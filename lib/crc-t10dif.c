/*
 * T10 Data Integrity Field CRC16 calculation
 *
 * Copyright (c) 2007 Oracle Corporation.  All rights reserved.
 * Written by Martin K. Petersen <martin.petersen@oracle.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/crc-t10dif.h>
#include <linux/err.h>
#include <linux/init.h>
#include <crypto/hash.h>
#include <linux/static_key.h>

static struct crypto_shash *crct10dif_tfm;
static struct static_key crct10dif_fallback __read_mostly;

__u16 crc_t10dif_update(__u16 crc, const unsigned char *buffer, size_t len)
{
	struct {
		struct shash_desc shash;
		char ctx[2];
	} desc;
	int err;

	if (static_key_false(&crct10dif_fallback))
		return crc_t10dif_generic(crc, buffer, len);

	desc.shash.tfm = crct10dif_tfm;
	desc.shash.flags = 0;
	*(__u16 *)desc.ctx = crc;

	err = crypto_shash_update(&desc.shash, buffer, len);
	BUG_ON(err);

	return *(__u16 *)desc.ctx;
}
EXPORT_SYMBOL(crc_t10dif_update);

__u16 crc_t10dif(const unsigned char *buffer, size_t len)
{
	return crc_t10dif_update(0, buffer, len);
}
EXPORT_SYMBOL(crc_t10dif);

static int __init crc_t10dif_mod_init(void)
{
	crct10dif_tfm = crypto_alloc_shash("crct10dif", 0, 0);
	if (IS_ERR(crct10dif_tfm)) {
		static_key_slow_inc(&crct10dif_fallback);
		crct10dif_tfm = NULL;
	}
	return 0;
}

static void __exit crc_t10dif_mod_fini(void)
{
	crypto_free_shash(crct10dif_tfm);
}

module_init(crc_t10dif_mod_init);
module_exit(crc_t10dif_mod_fini);

MODULE_DESCRIPTION("T10 DIF CRC calculation");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: crct10dif");
