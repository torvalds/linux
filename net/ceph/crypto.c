
#include <linux/ceph/ceph_debug.h>

#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <crypto/hash.h>
#include <linux/key-type.h>

#include <keys/ceph-type.h>
#include <keys/user-type.h>
#include <linux/ceph/decode.h>
#include "crypto.h"

int ceph_crypto_key_clone(struct ceph_crypto_key *dst,
			  const struct ceph_crypto_key *src)
{
	memcpy(dst, src, sizeof(struct ceph_crypto_key));
	dst->key = kmemdup(src->key, src->len, GFP_NOFS);
	if (!dst->key)
		return -ENOMEM;
	return 0;
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
	ceph_decode_need(p, end, 2*sizeof(u16) + sizeof(key->created), bad);
	key->type = ceph_decode_16(p);
	ceph_decode_copy(p, &key->created, sizeof(key->created));
	key->len = ceph_decode_16(p);
	ceph_decode_need(p, end, key->len, bad);
	key->key = kmalloc(key->len, GFP_NOFS);
	if (!key->key)
		return -ENOMEM;
	ceph_decode_copy(p, key->key, key->len);
	return 0;

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



#define AES_KEY_SIZE 16

static struct crypto_blkcipher *ceph_crypto_alloc_cipher(void)
{
	return crypto_alloc_blkcipher("cbc(aes)", 0, CRYPTO_ALG_ASYNC);
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

static int ceph_aes_encrypt(const void *key, int key_len,
			    void *dst, size_t *dst_len,
			    const void *src, size_t src_len)
{
	struct scatterlist sg_in[2], prealloc_sg;
	struct sg_table sg_out;
	struct crypto_blkcipher *tfm = ceph_crypto_alloc_cipher();
	struct blkcipher_desc desc = { .tfm = tfm, .flags = 0 };
	int ret;
	void *iv;
	int ivsize;
	size_t zero_padding = (0x10 - (src_len & 0x0f));
	char pad[16];

	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	memset(pad, zero_padding, zero_padding);

	*dst_len = src_len + zero_padding;

	sg_init_table(sg_in, 2);
	sg_set_buf(&sg_in[0], src, src_len);
	sg_set_buf(&sg_in[1], pad, zero_padding);
	ret = setup_sgtable(&sg_out, &prealloc_sg, dst, *dst_len);
	if (ret)
		goto out_tfm;

	crypto_blkcipher_setkey((void *)tfm, key, key_len);
	iv = crypto_blkcipher_crt(tfm)->iv;
	ivsize = crypto_blkcipher_ivsize(tfm);
	memcpy(iv, aes_iv, ivsize);

	/*
	print_hex_dump(KERN_ERR, "enc key: ", DUMP_PREFIX_NONE, 16, 1,
		       key, key_len, 1);
	print_hex_dump(KERN_ERR, "enc src: ", DUMP_PREFIX_NONE, 16, 1,
			src, src_len, 1);
	print_hex_dump(KERN_ERR, "enc pad: ", DUMP_PREFIX_NONE, 16, 1,
			pad, zero_padding, 1);
	*/
	ret = crypto_blkcipher_encrypt(&desc, sg_out.sgl, sg_in,
				     src_len + zero_padding);
	if (ret < 0) {
		pr_err("ceph_aes_crypt failed %d\n", ret);
		goto out_sg;
	}
	/*
	print_hex_dump(KERN_ERR, "enc out: ", DUMP_PREFIX_NONE, 16, 1,
		       dst, *dst_len, 1);
	*/

out_sg:
	teardown_sgtable(&sg_out);
out_tfm:
	crypto_free_blkcipher(tfm);
	return ret;
}

static int ceph_aes_encrypt2(const void *key, int key_len, void *dst,
			     size_t *dst_len,
			     const void *src1, size_t src1_len,
			     const void *src2, size_t src2_len)
{
	struct scatterlist sg_in[3], prealloc_sg;
	struct sg_table sg_out;
	struct crypto_blkcipher *tfm = ceph_crypto_alloc_cipher();
	struct blkcipher_desc desc = { .tfm = tfm, .flags = 0 };
	int ret;
	void *iv;
	int ivsize;
	size_t zero_padding = (0x10 - ((src1_len + src2_len) & 0x0f));
	char pad[16];

	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	memset(pad, zero_padding, zero_padding);

	*dst_len = src1_len + src2_len + zero_padding;

	sg_init_table(sg_in, 3);
	sg_set_buf(&sg_in[0], src1, src1_len);
	sg_set_buf(&sg_in[1], src2, src2_len);
	sg_set_buf(&sg_in[2], pad, zero_padding);
	ret = setup_sgtable(&sg_out, &prealloc_sg, dst, *dst_len);
	if (ret)
		goto out_tfm;

	crypto_blkcipher_setkey((void *)tfm, key, key_len);
	iv = crypto_blkcipher_crt(tfm)->iv;
	ivsize = crypto_blkcipher_ivsize(tfm);
	memcpy(iv, aes_iv, ivsize);

	/*
	print_hex_dump(KERN_ERR, "enc  key: ", DUMP_PREFIX_NONE, 16, 1,
		       key, key_len, 1);
	print_hex_dump(KERN_ERR, "enc src1: ", DUMP_PREFIX_NONE, 16, 1,
			src1, src1_len, 1);
	print_hex_dump(KERN_ERR, "enc src2: ", DUMP_PREFIX_NONE, 16, 1,
			src2, src2_len, 1);
	print_hex_dump(KERN_ERR, "enc  pad: ", DUMP_PREFIX_NONE, 16, 1,
			pad, zero_padding, 1);
	*/
	ret = crypto_blkcipher_encrypt(&desc, sg_out.sgl, sg_in,
				     src1_len + src2_len + zero_padding);
	if (ret < 0) {
		pr_err("ceph_aes_crypt2 failed %d\n", ret);
		goto out_sg;
	}
	/*
	print_hex_dump(KERN_ERR, "enc  out: ", DUMP_PREFIX_NONE, 16, 1,
		       dst, *dst_len, 1);
	*/

out_sg:
	teardown_sgtable(&sg_out);
out_tfm:
	crypto_free_blkcipher(tfm);
	return ret;
}

static int ceph_aes_decrypt(const void *key, int key_len,
			    void *dst, size_t *dst_len,
			    const void *src, size_t src_len)
{
	struct sg_table sg_in;
	struct scatterlist sg_out[2], prealloc_sg;
	struct crypto_blkcipher *tfm = ceph_crypto_alloc_cipher();
	struct blkcipher_desc desc = { .tfm = tfm };
	char pad[16];
	void *iv;
	int ivsize;
	int ret;
	int last_byte;

	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	sg_init_table(sg_out, 2);
	sg_set_buf(&sg_out[0], dst, *dst_len);
	sg_set_buf(&sg_out[1], pad, sizeof(pad));
	ret = setup_sgtable(&sg_in, &prealloc_sg, src, src_len);
	if (ret)
		goto out_tfm;

	crypto_blkcipher_setkey((void *)tfm, key, key_len);
	iv = crypto_blkcipher_crt(tfm)->iv;
	ivsize = crypto_blkcipher_ivsize(tfm);
	memcpy(iv, aes_iv, ivsize);

	/*
	print_hex_dump(KERN_ERR, "dec key: ", DUMP_PREFIX_NONE, 16, 1,
		       key, key_len, 1);
	print_hex_dump(KERN_ERR, "dec  in: ", DUMP_PREFIX_NONE, 16, 1,
		       src, src_len, 1);
	*/
	ret = crypto_blkcipher_decrypt(&desc, sg_out, sg_in.sgl, src_len);
	if (ret < 0) {
		pr_err("ceph_aes_decrypt failed %d\n", ret);
		goto out_sg;
	}

	if (src_len <= *dst_len)
		last_byte = ((char *)dst)[src_len - 1];
	else
		last_byte = pad[src_len - *dst_len - 1];
	if (last_byte <= 16 && src_len >= last_byte) {
		*dst_len = src_len - last_byte;
	} else {
		pr_err("ceph_aes_decrypt got bad padding %d on src len %d\n",
		       last_byte, (int)src_len);
		return -EPERM;  /* bad padding */
	}
	/*
	print_hex_dump(KERN_ERR, "dec out: ", DUMP_PREFIX_NONE, 16, 1,
		       dst, *dst_len, 1);
	*/

out_sg:
	teardown_sgtable(&sg_in);
out_tfm:
	crypto_free_blkcipher(tfm);
	return ret;
}

static int ceph_aes_decrypt2(const void *key, int key_len,
			     void *dst1, size_t *dst1_len,
			     void *dst2, size_t *dst2_len,
			     const void *src, size_t src_len)
{
	struct sg_table sg_in;
	struct scatterlist sg_out[3], prealloc_sg;
	struct crypto_blkcipher *tfm = ceph_crypto_alloc_cipher();
	struct blkcipher_desc desc = { .tfm = tfm };
	char pad[16];
	void *iv;
	int ivsize;
	int ret;
	int last_byte;

	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	sg_init_table(sg_out, 3);
	sg_set_buf(&sg_out[0], dst1, *dst1_len);
	sg_set_buf(&sg_out[1], dst2, *dst2_len);
	sg_set_buf(&sg_out[2], pad, sizeof(pad));
	ret = setup_sgtable(&sg_in, &prealloc_sg, src, src_len);
	if (ret)
		goto out_tfm;

	crypto_blkcipher_setkey((void *)tfm, key, key_len);
	iv = crypto_blkcipher_crt(tfm)->iv;
	ivsize = crypto_blkcipher_ivsize(tfm);
	memcpy(iv, aes_iv, ivsize);

	/*
	print_hex_dump(KERN_ERR, "dec  key: ", DUMP_PREFIX_NONE, 16, 1,
		       key, key_len, 1);
	print_hex_dump(KERN_ERR, "dec   in: ", DUMP_PREFIX_NONE, 16, 1,
		       src, src_len, 1);
	*/
	ret = crypto_blkcipher_decrypt(&desc, sg_out, sg_in.sgl, src_len);
	if (ret < 0) {
		pr_err("ceph_aes_decrypt failed %d\n", ret);
		goto out_sg;
	}

	if (src_len <= *dst1_len)
		last_byte = ((char *)dst1)[src_len - 1];
	else if (src_len <= *dst1_len + *dst2_len)
		last_byte = ((char *)dst2)[src_len - *dst1_len - 1];
	else
		last_byte = pad[src_len - *dst1_len - *dst2_len - 1];
	if (last_byte <= 16 && src_len >= last_byte) {
		src_len -= last_byte;
	} else {
		pr_err("ceph_aes_decrypt got bad padding %d on src len %d\n",
		       last_byte, (int)src_len);
		return -EPERM;  /* bad padding */
	}

	if (src_len < *dst1_len) {
		*dst1_len = src_len;
		*dst2_len = 0;
	} else {
		*dst2_len = src_len - *dst1_len;
	}
	/*
	print_hex_dump(KERN_ERR, "dec  out1: ", DUMP_PREFIX_NONE, 16, 1,
		       dst1, *dst1_len, 1);
	print_hex_dump(KERN_ERR, "dec  out2: ", DUMP_PREFIX_NONE, 16, 1,
		       dst2, *dst2_len, 1);
	*/

out_sg:
	teardown_sgtable(&sg_in);
out_tfm:
	crypto_free_blkcipher(tfm);
	return ret;
}


int ceph_decrypt(struct ceph_crypto_key *secret, void *dst, size_t *dst_len,
		 const void *src, size_t src_len)
{
	switch (secret->type) {
	case CEPH_CRYPTO_NONE:
		if (*dst_len < src_len)
			return -ERANGE;
		memcpy(dst, src, src_len);
		*dst_len = src_len;
		return 0;

	case CEPH_CRYPTO_AES:
		return ceph_aes_decrypt(secret->key, secret->len, dst,
					dst_len, src, src_len);

	default:
		return -EINVAL;
	}
}

int ceph_decrypt2(struct ceph_crypto_key *secret,
			void *dst1, size_t *dst1_len,
			void *dst2, size_t *dst2_len,
			const void *src, size_t src_len)
{
	size_t t;

	switch (secret->type) {
	case CEPH_CRYPTO_NONE:
		if (*dst1_len + *dst2_len < src_len)
			return -ERANGE;
		t = min(*dst1_len, src_len);
		memcpy(dst1, src, t);
		*dst1_len = t;
		src += t;
		src_len -= t;
		if (src_len) {
			t = min(*dst2_len, src_len);
			memcpy(dst2, src, t);
			*dst2_len = t;
		}
		return 0;

	case CEPH_CRYPTO_AES:
		return ceph_aes_decrypt2(secret->key, secret->len,
					 dst1, dst1_len, dst2, dst2_len,
					 src, src_len);

	default:
		return -EINVAL;
	}
}

int ceph_encrypt(struct ceph_crypto_key *secret, void *dst, size_t *dst_len,
		 const void *src, size_t src_len)
{
	switch (secret->type) {
	case CEPH_CRYPTO_NONE:
		if (*dst_len < src_len)
			return -ERANGE;
		memcpy(dst, src, src_len);
		*dst_len = src_len;
		return 0;

	case CEPH_CRYPTO_AES:
		return ceph_aes_encrypt(secret->key, secret->len, dst,
					dst_len, src, src_len);

	default:
		return -EINVAL;
	}
}

int ceph_encrypt2(struct ceph_crypto_key *secret, void *dst, size_t *dst_len,
		  const void *src1, size_t src1_len,
		  const void *src2, size_t src2_len)
{
	switch (secret->type) {
	case CEPH_CRYPTO_NONE:
		if (*dst_len < src1_len + src2_len)
			return -ERANGE;
		memcpy(dst, src1, src1_len);
		memcpy(dst + src1_len, src2, src2_len);
		*dst_len = src1_len + src2_len;
		return 0;

	case CEPH_CRYPTO_AES:
		return ceph_aes_encrypt2(secret->key, secret->len, dst, dst_len,
					 src1, src1_len, src2, src2_len);

	default:
		return -EINVAL;
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

	prep->payload[0] = ckey;
	prep->quotalen = datalen;
	return 0;

err_ckey:
	kfree(ckey);
err:
	return ret;
}

static void ceph_key_free_preparse(struct key_preparsed_payload *prep)
{
	struct ceph_crypto_key *ckey = prep->payload[0];
	ceph_crypto_key_destroy(ckey);
	kfree(ckey);
}

static void ceph_key_destroy(struct key *key)
{
	struct ceph_crypto_key *ckey = key->payload.data;

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
