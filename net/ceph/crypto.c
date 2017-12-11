// SPDX-License-Identifier: GPL-2.0

#include <linux/ceph/ceph_debug.h>

#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <crypto/aes.h>
#include <crypto/skcipher.h>
#include <linux/key-type.h>
#include <linux/sched/mm.h>

#include <keys/ceph-type.h>
#include <keys/user-type.h>
#include <linux/ceph/decode.h>
#include "crypto.h"

/*
 * Set ->key and ->tfm.  The rest of the key should be filled in before
 * this function is called.
 */
static int set_secret(struct ceph_crypto_key *key, void *buf)
{
	unsigned int noio_flag;
	int ret;

	key->key = NULL;
	key->tfm = NULL;

	switch (key->type) {
	case CEPH_CRYPTO_NONE:
		return 0; /* nothing to do */
	case CEPH_CRYPTO_AES:
		break;
	default:
		return -ENOTSUPP;
	}

	if (!key->len)
		return -EINVAL;

	key->key = kmemdup(buf, key->len, GFP_NOIO);
	if (!key->key) {
		ret = -ENOMEM;
		goto fail;
	}

	/* crypto_alloc_skcipher() allocates with GFP_KERNEL */
	noio_flag = memalloc_noio_save();
	key->tfm = crypto_alloc_skcipher("cbc(aes)", 0, CRYPTO_ALG_ASYNC);
	memalloc_noio_restore(noio_flag);
	if (IS_ERR(key->tfm)) {
		ret = PTR_ERR(key->tfm);
		key->tfm = NULL;
		goto fail;
	}

	ret = crypto_skcipher_setkey(key->tfm, key->key, key->len);
	if (ret)
		goto fail;

	return 0;

fail:
	ceph_crypto_key_destroy(key);
	return ret;
}

int ceph_crypto_key_clone(struct ceph_crypto_key *dst,
			  const struct ceph_crypto_key *src)
{
	memcpy(dst, src, sizeof(struct ceph_crypto_key));
	return set_secret(dst, src->key);
}

int ceph_crypto_key_encode(struct ceph_crypto_key *key, void **p, void *end)
{
	if (*p + sizeof(u16) + sizeof(key->created) +
	    sizeof(u16) + key->len > end)
		return -ERANGE;
	ceph_encode_16(p, key->type);
	ceph_encode_copy(p, &key->created, sizeof(key->created));
	ceph_encode_16(p, key->len);
	ceph_encode_copy(p, key->key, key->len);
	return 0;
}

int ceph_crypto_key_decode(struct ceph_crypto_key *key, void **p, void *end)
{
	int ret;

	ceph_decode_need(p, end, 2*sizeof(u16) + sizeof(key->created), bad);
	key->type = ceph_decode_16(p);
	ceph_decode_copy(p, &key->created, sizeof(key->created));
	key->len = ceph_decode_16(p);
	ceph_decode_need(p, end, key->len, bad);
	ret = set_secret(key, *p);
	*p += key->len;
	return ret;

bad:
	dout("failed to decode crypto key\n");
	return -EINVAL;
}

int ceph_crypto_key_unarmor(struct ceph_crypto_key *key, const char *inkey)
{
	int inlen = strlen(inkey);
	int blen = inlen * 3 / 4;
	void *buf, *p;
	int ret;

	dout("crypto_key_unarmor %s\n", inkey);
	buf = kmalloc(blen, GFP_NOFS);
	if (!buf)
		return -ENOMEM;
	blen = ceph_unarmor(buf, inkey, inkey+inlen);
	if (blen < 0) {
		kfree(buf);
		return blen;
	}

	p = buf;
	ret = ceph_crypto_key_decode(key, &p, p + blen);
	kfree(buf);
	if (ret)
		return ret;
	dout("crypto_key_unarmor key %p type %d len %d\n", key,
	     key->type, key->len);
	return 0;
}

void ceph_crypto_key_destroy(struct ceph_crypto_key *key)
{
	if (key) {
		kfree(key->key);
		key->key = NULL;
		crypto_free_skcipher(key->tfm);
		key->tfm = NULL;
	}
}

static const u8 *aes_iv = (u8 *)CEPH_AES_IV;

/*
 * Should be used for buffers allocated with ceph_kvmalloc().
 * Currently these are encrypt out-buffer (ceph_buffer) and decrypt
 * in-buffer (msg front).
 *
 * Dispose of @sgt with teardown_sgtable().
 *
 * @prealloc_sg is to avoid memory allocation inside sg_alloc_table()
 * in cases where a single sg is sufficient.  No attempt to reduce the
 * number of sgs by squeezing physically contiguous pages together is
 * made though, for simplicity.
 */
static int setup_sgtable(struct sg_table *sgt, struct scatterlist *prealloc_sg,
			 const void *buf, unsigned int buf_len)
{
	struct scatterlist *sg;
	const bool is_vmalloc = is_vmalloc_addr(buf);
	unsigned int off = offset_in_page(buf);
	unsigned int chunk_cnt = 1;
	unsigned int chunk_len = PAGE_ALIGN(off + buf_len);
	int i;
	int ret;

	if (buf_len == 0) {
		memset(sgt, 0, sizeof(*sgt));
		return -EINVAL;
	}

	if (is_vmalloc) {
		chunk_cnt = chunk_len >> PAGE_SHIFT;
		chunk_len = PAGE_SIZE;
	}

	if (chunk_cnt > 1) {
		ret = sg_alloc_table(sgt, chunk_cnt, GFP_NOFS);
		if (ret)
			return ret;
	} else {
		WARN_ON(chunk_cnt != 1);
		sg_init_table(prealloc_sg, 1);
		sgt->sgl = prealloc_sg;
		sgt->nents = sgt->orig_nents = 1;
	}

	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
		struct page *page;
		unsigned int len = min(chunk_len - off, buf_len);

		if (is_vmalloc)
			page = vmalloc_to_page(buf);
		else
			page = virt_to_page(buf);

		sg_set_page(sg, page, len, off);

		off = 0;
		buf += len;
		buf_len -= len;
	}
	WARN_ON(buf_len != 0);

	return 0;
}

static void teardown_sgtable(struct sg_table *sgt)
{
	if (sgt->orig_nents > 1)
		sg_free_table(sgt);
}

static int ceph_aes_crypt(const struct ceph_crypto_key *key, bool encrypt,
			  void *buf, int buf_len, int in_len, int *pout_len)
{
	SKCIPHER_REQUEST_ON_STACK(req, key->tfm);
	struct sg_table sgt;
	struct scatterlist prealloc_sg;
	char iv[AES_BLOCK_SIZE] __aligned(8);
	int pad_byte = AES_BLOCK_SIZE - (in_len & (AES_BLOCK_SIZE - 1));
	int crypt_len = encrypt ? in_len + pad_byte : in_len;
	int ret;

	WARN_ON(crypt_len > buf_len);
	if (encrypt)
		memset(buf + in_len, pad_byte, pad_byte);
	ret = setup_sgtable(&sgt, &prealloc_sg, buf, crypt_len);
	if (ret)
		return ret;

	memcpy(iv, aes_iv, AES_BLOCK_SIZE);
	skcipher_request_set_tfm(req, key->tfm);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sgt.sgl, sgt.sgl, crypt_len, iv);

	/*
	print_hex_dump(KERN_ERR, "key: ", DUMP_PREFIX_NONE, 16, 1,
		       key->key, key->len, 1);
	print_hex_dump(KERN_ERR, " in: ", DUMP_PREFIX_NONE, 16, 1,
		       buf, crypt_len, 1);
	*/
	if (encrypt)
		ret = crypto_skcipher_encrypt(req);
	else
		ret = crypto_skcipher_decrypt(req);
	skcipher_request_zero(req);
	if (ret) {
		pr_err("%s %scrypt failed: %d\n", __func__,
		       encrypt ? "en" : "de", ret);
		goto out_sgt;
	}
	/*
	print_hex_dump(KERN_ERR, "out: ", DUMP_PREFIX_NONE, 16, 1,
		       buf, crypt_len, 1);
	*/

	if (encrypt) {
		*pout_len = crypt_len;
	} else {
		pad_byte = *(char *)(buf + in_len - 1);
		if (pad_byte > 0 && pad_byte <= AES_BLOCK_SIZE &&
		    in_len >= pad_byte) {
			*pout_len = in_len - pad_byte;
		} else {
			pr_err("%s got bad padding %d on in_len %d\n",
			       __func__, pad_byte, in_len);
			ret = -EPERM;
			goto out_sgt;
		}
	}

out_sgt:
	teardown_sgtable(&sgt);
	return ret;
}

int ceph_crypt(const struct ceph_crypto_key *key, bool encrypt,
	       void *buf, int buf_len, int in_len, int *pout_len)
{
	switch (key->type) {
	case CEPH_CRYPTO_NONE:
		*pout_len = in_len;
		return 0;
	case CEPH_CRYPTO_AES:
		return ceph_aes_crypt(key, encrypt, buf, buf_len, in_len,
				      pout_len);
	default:
		return -ENOTSUPP;
	}
}

static int ceph_key_preparse(struct key_preparsed_payload *prep)
{
	struct ceph_crypto_key *ckey;
	size_t datalen = prep->datalen;
	int ret;
	void *p;

	ret = -EINVAL;
	if (datalen <= 0 || datalen > 32767 || !prep->data)
		goto err;

	ret = -ENOMEM;
	ckey = kmalloc(sizeof(*ckey), GFP_KERNEL);
	if (!ckey)
		goto err;

	/* TODO ceph_crypto_key_decode should really take const input */
	p = (void *)prep->data;
	ret = ceph_crypto_key_decode(ckey, &p, (char*)prep->data+datalen);
	if (ret < 0)
		goto err_ckey;

	prep->payload.data[0] = ckey;
	prep->quotalen = datalen;
	return 0;

err_ckey:
	kfree(ckey);
err:
	return ret;
}

static void ceph_key_free_preparse(struct key_preparsed_payload *prep)
{
	struct ceph_crypto_key *ckey = prep->payload.data[0];
	ceph_crypto_key_destroy(ckey);
	kfree(ckey);
}

static void ceph_key_destroy(struct key *key)
{
	struct ceph_crypto_key *ckey = key->payload.data[0];

	ceph_crypto_key_destroy(ckey);
	kfree(ckey);
}

struct key_type key_type_ceph = {
	.name		= "ceph",
	.preparse	= ceph_key_preparse,
	.free_preparse	= ceph_key_free_preparse,
	.instantiate	= generic_key_instantiate,
	.destroy	= ceph_key_destroy,
};

int ceph_crypto_init(void) {
	return register_key_type(&key_type_ceph);
}

void ceph_crypto_shutdown(void) {
	unregister_key_type(&key_type_ceph);
}
