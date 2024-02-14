/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2021, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _CRYPTO_KDF_SELFTEST_H
#define _CRYPTO_KDF_SELFTEST_H

#include <crypto/hash.h>
#include <linux/uio.h>

struct kdf_testvec {
	unsigned char *key;
	size_t keylen;
	unsigned char *ikm;
	size_t ikmlen;
	struct kvec info;
	unsigned char *expected;
	size_t expectedlen;
};

static inline int
kdf_test(const struct kdf_testvec *test, const char *name,
	 int (*crypto_kdf_setkey)(struct crypto_shash *kmd,
				  const u8 *key, size_t keylen,
				  const u8 *ikm, size_t ikmlen),
	 int (*crypto_kdf_generate)(struct crypto_shash *kmd,
				    const struct kvec *info,
				    unsigned int info_nvec,
				    u8 *dst, unsigned int dlen))
{
	struct crypto_shash *kmd;
	int ret;
	u8 *buf = kzalloc(test->expectedlen, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	kmd = crypto_alloc_shash(name, 0, 0);
	if (IS_ERR(kmd)) {
		pr_err("alg: kdf: could not allocate hash handle for %s\n",
		       name);
		kfree(buf);
		return -ENOMEM;
	}

	ret = crypto_kdf_setkey(kmd, test->key, test->keylen,
				test->ikm, test->ikmlen);
	if (ret) {
		pr_err("alg: kdf: could not set key derivation key\n");
		goto err;
	}

	ret = crypto_kdf_generate(kmd, &test->info, 1, buf, test->expectedlen);
	if (ret) {
		pr_err("alg: kdf: could not obtain key data\n");
		goto err;
	}

	ret = memcmp(test->expected, buf, test->expectedlen);
	if (ret)
		ret = -EINVAL;

err:
	crypto_free_shash(kmd);
	kfree(buf);
	return ret;
}

#endif /* _CRYPTO_KDF_SELFTEST_H */
