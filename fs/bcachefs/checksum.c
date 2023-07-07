// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "checksum.h"
#include "errcode.h"
#include "super.h"
#include "super-io.h"

#include <linux/crc32c.h>
#include <linux/crypto.h>
#include <linux/xxhash.h>
#include <linux/key.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <crypto/algapi.h>
#include <crypto/chacha.h>
#include <crypto/hash.h>
#include <crypto/poly1305.h>
#include <crypto/skcipher.h>
#include <keys/user-type.h>

/*
 * bch2_checksum state is an abstraction of the checksum state calculated over different pages.
 * it features page merging without having the checksum algorithm lose its state.
 * for native checksum aglorithms (like crc), a default seed value will do.
 * for hash-like algorithms, a state needs to be stored
 */

struct bch2_checksum_state {
	union {
		u64 seed;
		struct xxh64_state h64state;
	};
	unsigned int type;
};

static void bch2_checksum_init(struct bch2_checksum_state *state)
{
	switch (state->type) {
	case BCH_CSUM_none:
	case BCH_CSUM_crc32c:
	case BCH_CSUM_crc64:
		state->seed = 0;
		break;
	case BCH_CSUM_crc32c_nonzero:
		state->seed = U32_MAX;
		break;
	case BCH_CSUM_crc64_nonzero:
		state->seed = U64_MAX;
		break;
	case BCH_CSUM_xxhash:
		xxh64_reset(&state->h64state, 0);
		break;
	default:
		BUG();
	}
}

static u64 bch2_checksum_final(const struct bch2_checksum_state *state)
{
	switch (state->type) {
	case BCH_CSUM_none:
	case BCH_CSUM_crc32c:
	case BCH_CSUM_crc64:
		return state->seed;
	case BCH_CSUM_crc32c_nonzero:
		return state->seed ^ U32_MAX;
	case BCH_CSUM_crc64_nonzero:
		return state->seed ^ U64_MAX;
	case BCH_CSUM_xxhash:
		return xxh64_digest(&state->h64state);
	default:
		BUG();
	}
}

static void bch2_checksum_update(struct bch2_checksum_state *state, const void *data, size_t len)
{
	switch (state->type) {
	case BCH_CSUM_none:
		return;
	case BCH_CSUM_crc32c_nonzero:
	case BCH_CSUM_crc32c:
		state->seed = crc32c(state->seed, data, len);
		break;
	case BCH_CSUM_crc64_nonzero:
	case BCH_CSUM_crc64:
		state->seed = crc64_be(state->seed, data, len);
		break;
	case BCH_CSUM_xxhash:
		xxh64_update(&state->h64state, data, len);
		break;
	default:
		BUG();
	}
}

static inline int do_encrypt_sg(struct crypto_sync_skcipher *tfm,
				struct nonce nonce,
				struct scatterlist *sg, size_t len)
{
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, tfm);
	int ret;

	skcipher_request_set_sync_tfm(req, tfm);
	skcipher_request_set_crypt(req, sg, sg, len, nonce.d);

	ret = crypto_skcipher_encrypt(req);
	if (ret)
		pr_err("got error %i from crypto_skcipher_encrypt()", ret);

	return ret;
}

static inline int do_encrypt(struct crypto_sync_skcipher *tfm,
			      struct nonce nonce,
			      void *buf, size_t len)
{
	if (!is_vmalloc_addr(buf)) {
		struct scatterlist sg;

		sg_init_table(&sg, 1);
		sg_set_page(&sg,
			    is_vmalloc_addr(buf)
			    ? vmalloc_to_page(buf)
			    : virt_to_page(buf),
			    len, offset_in_page(buf));
		return do_encrypt_sg(tfm, nonce, &sg, len);
	} else {
		unsigned pages = buf_pages(buf, len);
		struct scatterlist *sg;
		size_t orig_len = len;
		int ret, i;

		sg = kmalloc_array(pages, sizeof(*sg), GFP_KERNEL);
		if (!sg)
			return -BCH_ERR_ENOMEM_do_encrypt;

		sg_init_table(sg, pages);

		for (i = 0; i < pages; i++) {
			unsigned offset = offset_in_page(buf);
			unsigned pg_len = min(len, PAGE_SIZE - offset);

			sg_set_page(sg + i, vmalloc_to_page(buf), pg_len, offset);
			buf += pg_len;
			len -= pg_len;
		}

		ret = do_encrypt_sg(tfm, nonce, sg, orig_len);
		kfree(sg);
		return ret;
	}
}

int bch2_chacha_encrypt_key(struct bch_key *key, struct nonce nonce,
			    void *buf, size_t len)
{
	struct crypto_sync_skcipher *chacha20 =
		crypto_alloc_sync_skcipher("chacha20", 0, 0);
	int ret;

	if (!chacha20) {
		pr_err("error requesting chacha20 module: %li", PTR_ERR(chacha20));
		return PTR_ERR(chacha20);
	}

	ret = crypto_skcipher_setkey(&chacha20->base,
				     (void *) key, sizeof(*key));
	if (ret) {
		pr_err("crypto_skcipher_setkey() error: %i", ret);
		goto err;
	}

	ret = do_encrypt(chacha20, nonce, buf, len);
err:
	crypto_free_sync_skcipher(chacha20);
	return ret;
}

static int gen_poly_key(struct bch_fs *c, struct shash_desc *desc,
			struct nonce nonce)
{
	u8 key[POLY1305_KEY_SIZE];
	int ret;

	nonce.d[3] ^= BCH_NONCE_POLY;

	memset(key, 0, sizeof(key));
	ret = do_encrypt(c->chacha20, nonce, key, sizeof(key));
	if (ret)
		return ret;

	desc->tfm = c->poly1305;
	crypto_shash_init(desc);
	crypto_shash_update(desc, key, sizeof(key));
	return 0;
}

struct bch_csum bch2_checksum(struct bch_fs *c, unsigned type,
			      struct nonce nonce, const void *data, size_t len)
{
	switch (type) {
	case BCH_CSUM_none:
	case BCH_CSUM_crc32c_nonzero:
	case BCH_CSUM_crc64_nonzero:
	case BCH_CSUM_crc32c:
	case BCH_CSUM_xxhash:
	case BCH_CSUM_crc64: {
		struct bch2_checksum_state state;

		state.type = type;

		bch2_checksum_init(&state);
		bch2_checksum_update(&state, data, len);

		return (struct bch_csum) { .lo = cpu_to_le64(bch2_checksum_final(&state)) };
	}

	case BCH_CSUM_chacha20_poly1305_80:
	case BCH_CSUM_chacha20_poly1305_128: {
		SHASH_DESC_ON_STACK(desc, c->poly1305);
		u8 digest[POLY1305_DIGEST_SIZE];
		struct bch_csum ret = { 0 };

		gen_poly_key(c, desc, nonce);

		crypto_shash_update(desc, data, len);
		crypto_shash_final(desc, digest);

		memcpy(&ret, digest, bch_crc_bytes[type]);
		return ret;
	}
	default:
		BUG();
	}
}

int bch2_encrypt(struct bch_fs *c, unsigned type,
		  struct nonce nonce, void *data, size_t len)
{
	if (!bch2_csum_type_is_encryption(type))
		return 0;

	return do_encrypt(c->chacha20, nonce, data, len);
}

static struct bch_csum __bch2_checksum_bio(struct bch_fs *c, unsigned type,
					   struct nonce nonce, struct bio *bio,
					   struct bvec_iter *iter)
{
	struct bio_vec bv;

	switch (type) {
	case BCH_CSUM_none:
		return (struct bch_csum) { 0 };
	case BCH_CSUM_crc32c_nonzero:
	case BCH_CSUM_crc64_nonzero:
	case BCH_CSUM_crc32c:
	case BCH_CSUM_xxhash:
	case BCH_CSUM_crc64: {
		struct bch2_checksum_state state;

		state.type = type;
		bch2_checksum_init(&state);

#ifdef CONFIG_HIGHMEM
		__bio_for_each_segment(bv, bio, *iter, *iter) {
			void *p = kmap_atomic(bv.bv_page) + bv.bv_offset;
			bch2_checksum_update(&state, p, bv.bv_len);
			kunmap_atomic(p);
		}
#else
		__bio_for_each_bvec(bv, bio, *iter, *iter)
			bch2_checksum_update(&state, page_address(bv.bv_page) + bv.bv_offset,
				bv.bv_len);
#endif
		return (struct bch_csum) { .lo = cpu_to_le64(bch2_checksum_final(&state)) };
	}

	case BCH_CSUM_chacha20_poly1305_80:
	case BCH_CSUM_chacha20_poly1305_128: {
		SHASH_DESC_ON_STACK(desc, c->poly1305);
		u8 digest[POLY1305_DIGEST_SIZE];
		struct bch_csum ret = { 0 };

		gen_poly_key(c, desc, nonce);

#ifdef CONFIG_HIGHMEM
		__bio_for_each_segment(bv, bio, *iter, *iter) {
			void *p = kmap_atomic(bv.bv_page) + bv.bv_offset;

			crypto_shash_update(desc, p, bv.bv_len);
			kunmap_atomic(p);
		}
#else
		__bio_for_each_bvec(bv, bio, *iter, *iter)
			crypto_shash_update(desc,
				page_address(bv.bv_page) + bv.bv_offset,
				bv.bv_len);
#endif
		crypto_shash_final(desc, digest);

		memcpy(&ret, digest, bch_crc_bytes[type]);
		return ret;
	}
	default:
		BUG();
	}
}

struct bch_csum bch2_checksum_bio(struct bch_fs *c, unsigned type,
				  struct nonce nonce, struct bio *bio)
{
	struct bvec_iter iter = bio->bi_iter;

	return __bch2_checksum_bio(c, type, nonce, bio, &iter);
}

int __bch2_encrypt_bio(struct bch_fs *c, unsigned type,
		     struct nonce nonce, struct bio *bio)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	struct scatterlist sgl[16], *sg = sgl;
	size_t bytes = 0;
	int ret = 0;

	if (!bch2_csum_type_is_encryption(type))
		return 0;

	sg_init_table(sgl, ARRAY_SIZE(sgl));

	bio_for_each_segment(bv, bio, iter) {
		if (sg == sgl + ARRAY_SIZE(sgl)) {
			sg_mark_end(sg - 1);

			ret = do_encrypt_sg(c->chacha20, nonce, sgl, bytes);
			if (ret)
				return ret;

			nonce = nonce_add(nonce, bytes);
			bytes = 0;

			sg_init_table(sgl, ARRAY_SIZE(sgl));
			sg = sgl;
		}

		sg_set_page(sg++, bv.bv_page, bv.bv_len, bv.bv_offset);
		bytes += bv.bv_len;
	}

	sg_mark_end(sg - 1);
	return do_encrypt_sg(c->chacha20, nonce, sgl, bytes);
}

struct bch_csum bch2_checksum_merge(unsigned type, struct bch_csum a,
				    struct bch_csum b, size_t b_len)
{
	struct bch2_checksum_state state;

	state.type = type;
	bch2_checksum_init(&state);
	state.seed = (u64 __force) a.lo;

	BUG_ON(!bch2_checksum_mergeable(type));

	while (b_len) {
		unsigned b = min_t(unsigned, b_len, PAGE_SIZE);

		bch2_checksum_update(&state,
				page_address(ZERO_PAGE(0)), b);
		b_len -= b;
	}
	a.lo = (__le64 __force) bch2_checksum_final(&state);
	a.lo ^= b.lo;
	a.hi ^= b.hi;
	return a;
}

int bch2_rechecksum_bio(struct bch_fs *c, struct bio *bio,
			struct bversion version,
			struct bch_extent_crc_unpacked crc_old,
			struct bch_extent_crc_unpacked *crc_a,
			struct bch_extent_crc_unpacked *crc_b,
			unsigned len_a, unsigned len_b,
			unsigned new_csum_type)
{
	struct bvec_iter iter = bio->bi_iter;
	struct nonce nonce = extent_nonce(version, crc_old);
	struct bch_csum merged = { 0 };
	struct crc_split {
		struct bch_extent_crc_unpacked	*crc;
		unsigned			len;
		unsigned			csum_type;
		struct bch_csum			csum;
	} splits[3] = {
		{ crc_a, len_a, new_csum_type },
		{ crc_b, len_b, new_csum_type },
		{ NULL,	 bio_sectors(bio) - len_a - len_b, new_csum_type },
	}, *i;
	bool mergeable = crc_old.csum_type == new_csum_type &&
		bch2_checksum_mergeable(new_csum_type);
	unsigned crc_nonce = crc_old.nonce;

	BUG_ON(len_a + len_b > bio_sectors(bio));
	BUG_ON(crc_old.uncompressed_size != bio_sectors(bio));
	BUG_ON(crc_is_compressed(crc_old));
	BUG_ON(bch2_csum_type_is_encryption(crc_old.csum_type) !=
	       bch2_csum_type_is_encryption(new_csum_type));

	for (i = splits; i < splits + ARRAY_SIZE(splits); i++) {
		iter.bi_size = i->len << 9;
		if (mergeable || i->crc)
			i->csum = __bch2_checksum_bio(c, i->csum_type,
						      nonce, bio, &iter);
		else
			bio_advance_iter(bio, &iter, i->len << 9);
		nonce = nonce_add(nonce, i->len << 9);
	}

	if (mergeable)
		for (i = splits; i < splits + ARRAY_SIZE(splits); i++)
			merged = bch2_checksum_merge(new_csum_type, merged,
						     i->csum, i->len << 9);
	else
		merged = bch2_checksum_bio(c, crc_old.csum_type,
				extent_nonce(version, crc_old), bio);

	if (bch2_crc_cmp(merged, crc_old.csum)) {
		bch_err(c, "checksum error in bch2_rechecksum_bio() (memory corruption or bug?)\n"
			"expected %0llx:%0llx got %0llx:%0llx (old type %s new type %s)",
			crc_old.csum.hi,
			crc_old.csum.lo,
			merged.hi,
			merged.lo,
			bch2_csum_types[crc_old.csum_type],
			bch2_csum_types[new_csum_type]);
		return -EIO;
	}

	for (i = splits; i < splits + ARRAY_SIZE(splits); i++) {
		if (i->crc)
			*i->crc = (struct bch_extent_crc_unpacked) {
				.csum_type		= i->csum_type,
				.compression_type	= crc_old.compression_type,
				.compressed_size	= i->len,
				.uncompressed_size	= i->len,
				.offset			= 0,
				.live_size		= i->len,
				.nonce			= crc_nonce,
				.csum			= i->csum,
			};

		if (bch2_csum_type_is_encryption(new_csum_type))
			crc_nonce += i->len;
	}

	return 0;
}

#ifdef __KERNEL__
static int __bch2_request_key(char *key_description, struct bch_key *key)
{
	struct key *keyring_key;
	const struct user_key_payload *ukp;
	int ret;

	keyring_key = request_key(&key_type_user, key_description, NULL);
	if (IS_ERR(keyring_key))
		return PTR_ERR(keyring_key);

	down_read(&keyring_key->sem);
	ukp = dereference_key_locked(keyring_key);
	if (ukp->datalen == sizeof(*key)) {
		memcpy(key, ukp->data, ukp->datalen);
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	up_read(&keyring_key->sem);
	key_put(keyring_key);

	return ret;
}
#else
#include <keyutils.h>

static int __bch2_request_key(char *key_description, struct bch_key *key)
{
	key_serial_t key_id;

	key_id = request_key("user", key_description, NULL,
			     KEY_SPEC_USER_KEYRING);
	if (key_id < 0)
		return -errno;

	if (keyctl_read(key_id, (void *) key, sizeof(*key)) != sizeof(*key))
		return -1;

	return 0;
}
#endif

int bch2_request_key(struct bch_sb *sb, struct bch_key *key)
{
	struct printbuf key_description = PRINTBUF;
	int ret;

	prt_printf(&key_description, "bcachefs:");
	pr_uuid(&key_description, sb->user_uuid.b);

	ret = __bch2_request_key(key_description.buf, key);
	printbuf_exit(&key_description);
	return ret;
}

int bch2_decrypt_sb_key(struct bch_fs *c,
			struct bch_sb_field_crypt *crypt,
			struct bch_key *key)
{
	struct bch_encrypted_key sb_key = crypt->key;
	struct bch_key user_key;
	int ret = 0;

	/* is key encrypted? */
	if (!bch2_key_is_encrypted(&sb_key))
		goto out;

	ret = bch2_request_key(c->disk_sb.sb, &user_key);
	if (ret) {
		bch_err(c, "error requesting encryption key: %s", bch2_err_str(ret));
		goto err;
	}

	/* decrypt real key: */
	ret = bch2_chacha_encrypt_key(&user_key, bch2_sb_key_nonce(c),
			     &sb_key, sizeof(sb_key));
	if (ret)
		goto err;

	if (bch2_key_is_encrypted(&sb_key)) {
		bch_err(c, "incorrect encryption key");
		ret = -EINVAL;
		goto err;
	}
out:
	*key = sb_key.key;
err:
	memzero_explicit(&sb_key, sizeof(sb_key));
	memzero_explicit(&user_key, sizeof(user_key));
	return ret;
}

static int bch2_alloc_ciphers(struct bch_fs *c)
{
	int ret;

	if (!c->chacha20)
		c->chacha20 = crypto_alloc_sync_skcipher("chacha20", 0, 0);
	ret = PTR_ERR_OR_ZERO(c->chacha20);

	if (ret) {
		bch_err(c, "error requesting chacha20 module: %s", bch2_err_str(ret));
		return ret;
	}

	if (!c->poly1305)
		c->poly1305 = crypto_alloc_shash("poly1305", 0, 0);
	ret = PTR_ERR_OR_ZERO(c->poly1305);

	if (ret) {
		bch_err(c, "error requesting poly1305 module: %s", bch2_err_str(ret));
		return ret;
	}

	return 0;
}

int bch2_disable_encryption(struct bch_fs *c)
{
	struct bch_sb_field_crypt *crypt;
	struct bch_key key;
	int ret = -EINVAL;

	mutex_lock(&c->sb_lock);

	crypt = bch2_sb_get_crypt(c->disk_sb.sb);
	if (!crypt)
		goto out;

	/* is key encrypted? */
	ret = 0;
	if (bch2_key_is_encrypted(&crypt->key))
		goto out;

	ret = bch2_decrypt_sb_key(c, crypt, &key);
	if (ret)
		goto out;

	crypt->key.magic	= cpu_to_le64(BCH_KEY_MAGIC);
	crypt->key.key		= key;

	SET_BCH_SB_ENCRYPTION_TYPE(c->disk_sb.sb, 0);
	bch2_write_super(c);
out:
	mutex_unlock(&c->sb_lock);

	return ret;
}

int bch2_enable_encryption(struct bch_fs *c, bool keyed)
{
	struct bch_encrypted_key key;
	struct bch_key user_key;
	struct bch_sb_field_crypt *crypt;
	int ret = -EINVAL;

	mutex_lock(&c->sb_lock);

	/* Do we already have an encryption key? */
	if (bch2_sb_get_crypt(c->disk_sb.sb))
		goto err;

	ret = bch2_alloc_ciphers(c);
	if (ret)
		goto err;

	key.magic = cpu_to_le64(BCH_KEY_MAGIC);
	get_random_bytes(&key.key, sizeof(key.key));

	if (keyed) {
		ret = bch2_request_key(c->disk_sb.sb, &user_key);
		if (ret) {
			bch_err(c, "error requesting encryption key: %s", bch2_err_str(ret));
			goto err;
		}

		ret = bch2_chacha_encrypt_key(&user_key, bch2_sb_key_nonce(c),
					      &key, sizeof(key));
		if (ret)
			goto err;
	}

	ret = crypto_skcipher_setkey(&c->chacha20->base,
			(void *) &key.key, sizeof(key.key));
	if (ret)
		goto err;

	crypt = bch2_sb_resize_crypt(&c->disk_sb, sizeof(*crypt) / sizeof(u64));
	if (!crypt) {
		ret = -BCH_ERR_ENOSPC_sb_crypt;
		goto err;
	}

	crypt->key = key;

	/* write superblock */
	SET_BCH_SB_ENCRYPTION_TYPE(c->disk_sb.sb, 1);
	bch2_write_super(c);
err:
	mutex_unlock(&c->sb_lock);
	memzero_explicit(&user_key, sizeof(user_key));
	memzero_explicit(&key, sizeof(key));
	return ret;
}

void bch2_fs_encryption_exit(struct bch_fs *c)
{
	if (!IS_ERR_OR_NULL(c->poly1305))
		crypto_free_shash(c->poly1305);
	if (!IS_ERR_OR_NULL(c->chacha20))
		crypto_free_sync_skcipher(c->chacha20);
	if (!IS_ERR_OR_NULL(c->sha256))
		crypto_free_shash(c->sha256);
}

int bch2_fs_encryption_init(struct bch_fs *c)
{
	struct bch_sb_field_crypt *crypt;
	struct bch_key key;
	int ret = 0;

	c->sha256 = crypto_alloc_shash("sha256", 0, 0);
	ret = PTR_ERR_OR_ZERO(c->sha256);
	if (ret) {
		bch_err(c, "error requesting sha256 module: %s", bch2_err_str(ret));
		goto out;
	}

	crypt = bch2_sb_get_crypt(c->disk_sb.sb);
	if (!crypt)
		goto out;

	ret = bch2_alloc_ciphers(c);
	if (ret)
		goto out;

	ret = bch2_decrypt_sb_key(c, crypt, &key);
	if (ret)
		goto out;

	ret = crypto_skcipher_setkey(&c->chacha20->base,
			(void *) &key.key, sizeof(key.key));
	if (ret)
		goto out;
out:
	memzero_explicit(&key, sizeof(key));
	return ret;
}
