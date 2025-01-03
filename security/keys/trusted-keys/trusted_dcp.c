// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 sigma star gmbh
 */

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/gcm.h>
#include <crypto/skcipher.h>
#include <keys/trusted-type.h>
#include <linux/key-type.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <soc/fsl/dcp.h>

#define DCP_BLOB_VERSION 1
#define DCP_BLOB_AUTHLEN 16

/**
 * DOC: dcp blob format
 *
 * The Data Co-Processor (DCP) provides hardware-bound AES keys using its
 * AES encryption engine only. It does not provide direct key sealing/unsealing.
 * To make DCP hardware encryption keys usable as trust source, we define
 * our own custom format that uses a hardware-bound key to secure the sealing
 * key stored in the key blob.
 *
 * Whenever a new trusted key using DCP is generated, we generate a random 128-bit
 * blob encryption key (BEK) and 128-bit nonce. The BEK and nonce are used to
 * encrypt the trusted key payload using AES-128-GCM.
 *
 * The BEK itself is encrypted using the hardware-bound key using the DCP's AES
 * encryption engine with AES-128-ECB. The encrypted BEK, generated nonce,
 * BEK-encrypted payload and authentication tag make up the blob format together
 * with a version number, payload length and authentication tag.
 */

/**
 * struct dcp_blob_fmt - DCP BLOB format.
 *
 * @fmt_version: Format version, currently being %1.
 * @blob_key: Random AES 128 key which is used to encrypt @payload,
 *            @blob_key itself is encrypted with OTP or UNIQUE device key in
 *            AES-128-ECB mode by DCP.
 * @nonce: Random nonce used for @payload encryption.
 * @payload_len: Length of the plain text @payload.
 * @payload: The payload itself, encrypted using AES-128-GCM and @blob_key,
 *           GCM auth tag of size DCP_BLOB_AUTHLEN is attached at the end of it.
 *
 * The total size of a DCP BLOB is sizeof(struct dcp_blob_fmt) + @payload_len +
 * DCP_BLOB_AUTHLEN.
 */
struct dcp_blob_fmt {
	__u8 fmt_version;
	__u8 blob_key[AES_KEYSIZE_128];
	__u8 nonce[AES_KEYSIZE_128];
	__le32 payload_len;
	__u8 payload[];
} __packed;

static bool use_otp_key;
module_param_named(dcp_use_otp_key, use_otp_key, bool, 0);
MODULE_PARM_DESC(dcp_use_otp_key, "Use OTP instead of UNIQUE key for sealing");

static bool skip_zk_test;
module_param_named(dcp_skip_zk_test, skip_zk_test, bool, 0);
MODULE_PARM_DESC(dcp_skip_zk_test, "Don't test whether device keys are zero'ed");

static unsigned int calc_blob_len(unsigned int payload_len)
{
	return sizeof(struct dcp_blob_fmt) + payload_len + DCP_BLOB_AUTHLEN;
}

static int do_dcp_crypto(u8 *in, u8 *out, bool do_encrypt)
{
	struct skcipher_request *req = NULL;
	struct scatterlist src_sg, dst_sg;
	struct crypto_skcipher *tfm;
	u8 paes_key[DCP_PAES_KEYSIZE];
	DECLARE_CRYPTO_WAIT(wait);
	int res = 0;

	if (use_otp_key)
		paes_key[0] = DCP_PAES_KEY_OTP;
	else
		paes_key[0] = DCP_PAES_KEY_UNIQUE;

	tfm = crypto_alloc_skcipher("ecb-paes-dcp", CRYPTO_ALG_INTERNAL,
				    CRYPTO_ALG_INTERNAL);
	if (IS_ERR(tfm)) {
		res = PTR_ERR(tfm);
		tfm = NULL;
		goto out;
	}

	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		res = -ENOMEM;
		goto out;
	}

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &wait);
	res = crypto_skcipher_setkey(tfm, paes_key, sizeof(paes_key));
	if (res < 0)
		goto out;

	sg_init_one(&src_sg, in, AES_KEYSIZE_128);
	sg_init_one(&dst_sg, out, AES_KEYSIZE_128);
	skcipher_request_set_crypt(req, &src_sg, &dst_sg, AES_KEYSIZE_128,
				   NULL);

	if (do_encrypt)
		res = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);
	else
		res = crypto_wait_req(crypto_skcipher_decrypt(req), &wait);

out:
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);

	return res;
}

static int do_aead_crypto(u8 *in, u8 *out, size_t len, u8 *key, u8 *nonce,
			  bool do_encrypt)
{
	struct aead_request *aead_req = NULL;
	struct scatterlist src_sg, dst_sg;
	struct crypto_aead *aead;
	int ret;
	DECLARE_CRYPTO_WAIT(wait);

	aead = crypto_alloc_aead("gcm(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(aead)) {
		ret = PTR_ERR(aead);
		goto out;
	}

	ret = crypto_aead_setauthsize(aead, DCP_BLOB_AUTHLEN);
	if (ret < 0) {
		pr_err("Can't set crypto auth tag len: %d\n", ret);
		goto free_aead;
	}

	aead_req = aead_request_alloc(aead, GFP_KERNEL);
	if (!aead_req) {
		ret = -ENOMEM;
		goto free_aead;
	}

	sg_init_one(&src_sg, in, len);
	if (do_encrypt) {
		/*
		 * If we encrypt our buffer has extra space for the auth tag.
		 */
		sg_init_one(&dst_sg, out, len + DCP_BLOB_AUTHLEN);
	} else {
		sg_init_one(&dst_sg, out, len);
	}

	aead_request_set_crypt(aead_req, &src_sg, &dst_sg, len, nonce);
	aead_request_set_callback(aead_req, CRYPTO_TFM_REQ_MAY_SLEEP,
				  crypto_req_done, &wait);
	aead_request_set_ad(aead_req, 0);

	if (crypto_aead_setkey(aead, key, AES_KEYSIZE_128)) {
		pr_err("Can't set crypto AEAD key\n");
		ret = -EINVAL;
		goto free_req;
	}

	if (do_encrypt)
		ret = crypto_wait_req(crypto_aead_encrypt(aead_req), &wait);
	else
		ret = crypto_wait_req(crypto_aead_decrypt(aead_req), &wait);

free_req:
	aead_request_free(aead_req);
free_aead:
	crypto_free_aead(aead);
out:
	return ret;
}

static int decrypt_blob_key(u8 *encrypted_key, u8 *plain_key)
{
	return do_dcp_crypto(encrypted_key, plain_key, false);
}

static int encrypt_blob_key(u8 *plain_key, u8 *encrypted_key)
{
	return do_dcp_crypto(plain_key, encrypted_key, true);
}

static int trusted_dcp_seal(struct trusted_key_payload *p, char *datablob)
{
	struct dcp_blob_fmt *b = (struct dcp_blob_fmt *)p->blob;
	int blen, ret;
	u8 plain_blob_key[AES_KEYSIZE_128];

	blen = calc_blob_len(p->key_len);
	if (blen > MAX_BLOB_SIZE)
		return -E2BIG;

	b->fmt_version = DCP_BLOB_VERSION;
	get_random_bytes(b->nonce, AES_KEYSIZE_128);
	get_random_bytes(plain_blob_key, AES_KEYSIZE_128);

	ret = do_aead_crypto(p->key, b->payload, p->key_len, plain_blob_key,
			     b->nonce, true);
	if (ret) {
		pr_err("Unable to encrypt blob payload: %i\n", ret);
		goto out;
	}

	ret = encrypt_blob_key(plain_blob_key, b->blob_key);
	if (ret) {
		pr_err("Unable to encrypt blob key: %i\n", ret);
		goto out;
	}

	put_unaligned_le32(p->key_len, &b->payload_len);
	p->blob_len = blen;
	ret = 0;

out:
	memzero_explicit(plain_blob_key, sizeof(plain_blob_key));

	return ret;
}

static int trusted_dcp_unseal(struct trusted_key_payload *p, char *datablob)
{
	struct dcp_blob_fmt *b = (struct dcp_blob_fmt *)p->blob;
	int blen, ret;
	u8 plain_blob_key[AES_KEYSIZE_128];

	if (b->fmt_version != DCP_BLOB_VERSION) {
		pr_err("DCP blob has bad version: %i, expected %i\n",
		       b->fmt_version, DCP_BLOB_VERSION);
		ret = -EINVAL;
		goto out;
	}

	p->key_len = le32_to_cpu(b->payload_len);
	blen = calc_blob_len(p->key_len);
	if (blen != p->blob_len) {
		pr_err("DCP blob has bad length: %i != %i\n", blen,
		       p->blob_len);
		ret = -EINVAL;
		goto out;
	}

	ret = decrypt_blob_key(b->blob_key, plain_blob_key);
	if (ret) {
		pr_err("Unable to decrypt blob key: %i\n", ret);
		goto out;
	}

	ret = do_aead_crypto(b->payload, p->key, p->key_len + DCP_BLOB_AUTHLEN,
			     plain_blob_key, b->nonce, false);
	if (ret) {
		pr_err("Unwrap of DCP payload failed: %i\n", ret);
		goto out;
	}

	ret = 0;
out:
	memzero_explicit(plain_blob_key, sizeof(plain_blob_key));

	return ret;
}

static int test_for_zero_key(void)
{
	/*
	 * Encrypting a plaintext of all 0x55 bytes will yield
	 * this ciphertext in case the DCP test key is used.
	 */
	static const u8 bad[] = {0x9a, 0xda, 0xe0, 0x54, 0xf6, 0x3d, 0xfa, 0xff,
				 0x5e, 0xa1, 0x8e, 0x45, 0xed, 0xf6, 0xea, 0x6f};
	void *buf = NULL;
	int ret = 0;

	if (skip_zk_test)
		goto out;

	buf = kmalloc(AES_BLOCK_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	memset(buf, 0x55, AES_BLOCK_SIZE);

	ret = do_dcp_crypto(buf, buf, true);
	if (ret)
		goto out;

	if (memcmp(buf, bad, AES_BLOCK_SIZE) == 0) {
		pr_warn("Device neither in secure nor trusted mode!\n");
		ret = -EINVAL;
	}
out:
	kfree(buf);
	return ret;
}

static int trusted_dcp_init(void)
{
	int ret;

	if (use_otp_key)
		pr_info("Using DCP OTP key\n");

	ret = test_for_zero_key();
	if (ret) {
		pr_warn("Test for zero'ed keys failed: %i\n", ret);

		return -EINVAL;
	}

	return register_key_type(&key_type_trusted);
}

static void trusted_dcp_exit(void)
{
	unregister_key_type(&key_type_trusted);
}

struct trusted_key_ops dcp_trusted_key_ops = {
	.exit = trusted_dcp_exit,
	.init = trusted_dcp_init,
	.seal = trusted_dcp_seal,
	.unseal = trusted_dcp_unseal,
	.migratable = 0,
};
