/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2022 Red Hat, Inc.
 * Author: Vladis Dronov <vdronoff@gmail.com>
 */

#include <asm/elf.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <crypto/skcipher.h>
#include <crypto/akcipher.h>
#include <crypto/acompress.h>
#include <crypto/rng.h>
#include <crypto/drbg.h>
#include <crypto/kpp.h>
#include <crypto/internal/simd.h>
#include <crypto/chacha.h>
#include <crypto/aead.h>
#include <crypto/hash.h>
#include <linux/crypto.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/fips.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/scatterlist.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/zlib.h>
#include <linux/once.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>

static unsigned int data_size __read_mostly = 256;
static unsigned int debug __read_mostly = 0;

/* tie all skcipher structures together */
struct skcipher_def {
	struct scatterlist sginp, sgout;
	struct crypto_skcipher *tfm;
	struct skcipher_request *req;
	struct crypto_wait wait;
};

/* Perform cipher operations with the chacha lib */
static int test_lib_chacha(u8 *revert, u8 *cipher, u8 *plain)
{
	struct chacha_state chacha_state;
	u8 iv[16], key[32];
	u64 start, end;

	memset(key, 'X', sizeof(key));
	memset(iv, 'I', sizeof(iv));

	if (debug) {
		print_hex_dump(KERN_INFO, "key: ", DUMP_PREFIX_OFFSET,
			       16, 1, key, 32, 1);

		print_hex_dump(KERN_INFO, "iv:  ", DUMP_PREFIX_OFFSET,
			       16, 1, iv, 16, 1);
	}

	/* Encrypt */
	chacha_init(&chacha_state, (u32 *)key, iv);

	start = ktime_get_ns();
	chacha_crypt_arch(&chacha_state, cipher, plain, data_size, 20);
	end = ktime_get_ns();


	if (debug)
		print_hex_dump(KERN_INFO, "encr:", DUMP_PREFIX_OFFSET,
			       16, 1, cipher,
			       (data_size > 64 ? 64 : data_size), 1);

	pr_info("lib encryption took: %lld nsec", end - start);

	/* Decrypt */
	chacha_init(&chacha_state, (u32 *)key, iv);

	start = ktime_get_ns();
	chacha_crypt_arch(&chacha_state, revert, cipher, data_size, 20);
	end = ktime_get_ns();

	if (debug)
		print_hex_dump(KERN_INFO, "decr:", DUMP_PREFIX_OFFSET,
			       16, 1, revert,
			       (data_size > 64 ? 64 : data_size), 1);

	pr_info("lib decryption took: %lld nsec", end - start);

	return 0;
}

/* Perform cipher operations with skcipher */
static unsigned int test_skcipher_encdec(struct skcipher_def *sk,
					 int enc)
{
	int rc;

	if (enc) {
		rc = crypto_wait_req(crypto_skcipher_encrypt(sk->req),
				     &sk->wait);
		if (rc)
			pr_info("skcipher encrypt returned with result"
				"%d\n", rc);
	}
	else
	{
		rc = crypto_wait_req(crypto_skcipher_decrypt(sk->req),
				     &sk->wait);
		if (rc)
			pr_info("skcipher decrypt returned with result"
				"%d\n", rc);
	}

	return rc;
}

/* Initialize and trigger cipher operations */
static int test_skcipher(char *name, u8 *revert, u8 *cipher, u8 *plain)
{
	struct skcipher_def sk;
	struct crypto_skcipher *skcipher = NULL;
	struct skcipher_request *req = NULL;
	u8 iv[16], key[32];
	u64 start, end;
	int ret = -EFAULT;

	skcipher = crypto_alloc_skcipher(name, 0, 0);
	if (IS_ERR(skcipher)) {
		pr_info("could not allocate skcipher %s handle\n", name);
		return PTR_ERR(skcipher);
	}

	req = skcipher_request_alloc(skcipher, GFP_KERNEL);
	if (!req) {
		pr_info("could not allocate skcipher request\n");
		ret = -ENOMEM;
		goto out;
	}

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					  crypto_req_done,
					  &sk.wait);

	memset(key, 'X', sizeof(key));
	memset(iv, 'I', sizeof(iv));

	if (crypto_skcipher_setkey(skcipher, key, 32)) {
		pr_info("key could not be set\n");
		ret = -EAGAIN;
		goto out;
	}

	if (debug) {
		print_hex_dump(KERN_INFO, "key: ", DUMP_PREFIX_OFFSET,
			       16, 1, key, 32, 1);

		print_hex_dump(KERN_INFO, "iv:  ", DUMP_PREFIX_OFFSET,
			       16, 1, iv, 16, 1);
	}

	sk.tfm = skcipher;
	sk.req = req;

	/* Encrypt in one pass */
	sg_init_one(&sk.sginp, plain, data_size);
	sg_init_one(&sk.sgout, cipher, data_size);
	skcipher_request_set_crypt(req, &sk.sginp, &sk.sgout,
				   data_size, iv);
	crypto_init_wait(&sk.wait);

	/* Encrypt data */
	start = ktime_get_ns();
	ret = test_skcipher_encdec(&sk, 1);
	end = ktime_get_ns();

	if (ret)
		goto out;

	pr_info("%s tfm encryption successful, took %lld nsec\n", name, end - start);

	if (debug)
		print_hex_dump(KERN_INFO, "encr:", DUMP_PREFIX_OFFSET,
			       16, 1, cipher,
			       (data_size > 64 ? 64 : data_size), 1);

	/* Prepare for decryption */
	memset(iv, 'I', sizeof(iv));

	sg_init_one(&sk.sginp, cipher, data_size);
	sg_init_one(&sk.sgout, revert, data_size);
	skcipher_request_set_crypt(req, &sk.sginp, &sk.sgout,
				   data_size, iv);
	crypto_init_wait(&sk.wait);

	/* Decrypt data */
	start = ktime_get_ns();
	ret = test_skcipher_encdec(&sk, 0);
	end = ktime_get_ns();

	if (ret)
		goto out;

	pr_info("%s tfm decryption successful, took %lld nsec\n", name, end - start);

	if (debug)
		print_hex_dump(KERN_INFO, "decr:", DUMP_PREFIX_OFFSET,
			       16, 1, revert,
			       (data_size > 64 ? 64 : data_size), 1);

	/* Dump some internal skcipher data */
	if (debug)
		pr_info("skcipher %s: cryptlen %d blksize %d stride %d "
			"ivsize %d alignmask 0x%x\n",
			name, sk.req->cryptlen,
			crypto_skcipher_blocksize(sk.tfm),
			crypto_skcipher_alg(sk.tfm)->walksize,
			crypto_skcipher_ivsize(sk.tfm),
			crypto_skcipher_alignmask(sk.tfm));

out:
	if (skcipher)
		crypto_free_skcipher(skcipher);
	if (req)
		skcipher_request_free(req);
	return ret;
}

static int __init chacha_s390_test_init(void)
{
	u8 *plain = NULL, *revert = NULL;
	u8 *cipher_generic = NULL, *cipher_s390 = NULL;
	int ret = -1;

	pr_info("s390 ChaCha20 test module: size=%d debug=%d\n",
		data_size, debug);

	/* Allocate and fill buffers */
	plain = vmalloc(data_size);
	if (!plain) {
		pr_info("could not allocate plain buffer\n");
		ret = -2;
		goto out;
	}
	memset(plain, 'a', data_size);
	get_random_bytes(plain, (data_size > 256 ? 256 : data_size));

	cipher_generic = vzalloc(data_size);
	if (!cipher_generic) {
		pr_info("could not allocate cipher_generic buffer\n");
		ret = -2;
		goto out;
	}

	cipher_s390 = vzalloc(data_size);
	if (!cipher_s390) {
		pr_info("could not allocate cipher_s390 buffer\n");
		ret = -2;
		goto out;
	}

	revert = vzalloc(data_size);
	if (!revert) {
		pr_info("could not allocate revert buffer\n");
		ret = -2;
		goto out;
	}

	if (debug)
		print_hex_dump(KERN_INFO, "src: ", DUMP_PREFIX_OFFSET,
			       16, 1, plain,
			       (data_size > 64 ? 64 : data_size), 1);

	/* Use chacha20 generic */
	ret = test_skcipher("chacha20-generic", revert, cipher_generic, plain);
	if (ret)
		goto out;

	if (memcmp(plain, revert, data_size)) {
		pr_info("generic en/decryption check FAILED\n");
		ret = -2;
		goto out;
	}
	else
		pr_info("generic en/decryption check OK\n");

	memset(revert, 0, data_size);

	/* Use chacha20 s390 */
	ret = test_skcipher("chacha20-s390", revert, cipher_s390, plain);
	if (ret)
		goto out;

	if (memcmp(plain, revert, data_size)) {
		pr_info("s390 en/decryption check FAILED\n");
		ret = -2;
		goto out;
	}
	else
		pr_info("s390 en/decryption check OK\n");

	if (memcmp(cipher_generic, cipher_s390, data_size)) {
		pr_info("s390 vs generic check FAILED\n");
		ret = -2;
		goto out;
	}
	else
		pr_info("s390 vs generic check OK\n");

	memset(cipher_s390, 0, data_size);
	memset(revert, 0, data_size);

	/* Use chacha20 lib */
	test_lib_chacha(revert, cipher_s390, plain);

	if (memcmp(plain, revert, data_size)) {
		pr_info("lib en/decryption check FAILED\n");
		ret = -2;
		goto out;
	}
	else
		pr_info("lib en/decryption check OK\n");

	if (memcmp(cipher_generic, cipher_s390, data_size)) {
		pr_info("lib vs generic check FAILED\n");
		ret = -2;
		goto out;
	}
	else
		pr_info("lib vs generic check OK\n");

	pr_info("--- chacha20 s390 test end ---\n");

out:
	if (plain)
		vfree(plain);
	if (cipher_generic)
		vfree(cipher_generic);
	if (cipher_s390)
		vfree(cipher_s390);
	if (revert)
		vfree(revert);

	return -1;
}

static void __exit chacha_s390_test_exit(void)
{
	pr_info("s390 ChaCha20 test module exit\n");
}

module_param_named(size, data_size, uint, 0660);
module_param(debug, int, 0660);
MODULE_PARM_DESC(size, "Size of a plaintext");
MODULE_PARM_DESC(debug, "Debug level (0=off,1=on)");

module_init(chacha_s390_test_init);
module_exit(chacha_s390_test_exit);

MODULE_DESCRIPTION("s390 ChaCha20 self-test");
MODULE_AUTHOR("Vladis Dronov <vdronoff@gmail.com>");
MODULE_LICENSE("GPL v2");
