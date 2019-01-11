// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "checksum.h"
#include "super.h"
#include "super-io.h"

#include <linux/crc32c.h>
#include <linux/crypto.h>
#include <linux/key.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <crypto/algapi.h>
#include <crypto/chacha.h>
#include <crypto/hash.h>
#include <crypto/poly1305.h>
#include <crypto/skcipher.h>
#include <keys/user-type.h>

static u64 bch2_checksum_init(unsigned type)
{
	switch (type) {
	case BCH_CSUM_NONE:
		return 0;
	case BCH_CSUM_CRC32C_NONZERO:
		return U32_MAX;
	case BCH_CSUM_CRC64_NONZERO:
		return U64_MAX;
	case BCH_CSUM_CRC32C:
		return 0;
	case BCH_CSUM_CRC64:
		return 0;
	default:
		BUG();
	}
}

static u64 bch2_checksum_final(unsigned type, u64 crc)
{
	switch (type) {
	case BCH_CSUM_NONE:
		return 0;
	case BCH_CSUM_CRC32C_NONZERO:
		return crc ^ U32_MAX;
	case BCH_CSUM_CRC64_NONZERO:
		return crc ^ U64_MAX;
	case BCH_CSUM_CRC32C:
		return crc;
	case BCH_CSUM_CRC64:
		return crc;
	default:
		BUG();
	}
}

static u64 bch2_checksum_update(unsigned type, u64 crc, const void *data, size_t len)
{
	switch (type) {
	case BCH_CSUM_NONE:
		return 0;
	case BCH_CSUM_CRC32C_NONZERO:
	case BCH_CSUM_CRC32C:
		return crc32c(crc, data, len);
	case BCH_CSUM_CRC64_NONZERO:
	case BCH_CSUM_CRC64:
		return bch2_crc64_update(crc, data, len);
	default:
		BUG();
	}
}

static inline void do_encrypt_sg(struct crypto_sync_skcipher *tfm,
				 struct nonce nonce,
				 struct scatterlist *sg, size_t len)
{
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, tfm);
	int ret;

	skcipher_request_set_sync_tfm(req, tfm);
	skcipher_request_set_crypt(req, sg, sg, len, nonce.d);

	ret = crypto_skcipher_encrypt(req);
	BUG_ON(ret);
}

static inline void do_encrypt(struct crypto_sync_skcipher *tfm,
			      struct nonce nonce,
			      void *buf, size_t len)
{
	struct scatterlist sg;

	sg_init_one(&sg, buf, len);
	do_encrypt_sg(tfm, nonce, &sg, len);
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

	do_encrypt(chacha20, nonce, buf, len);
err:
	crypto_free_sync_skcipher(chacha20);
	return ret;
}

static void gen_poly_key(struct bch_fs *c, struct shash_desc *desc,
			 struct nonce nonce)
{
	u8 key[POLY1305_KEY_SIZE];

	nonce.d[3] ^= BCH_NONCE_POLY;

	memset(key, 0, sizeof(key));
	do_encrypt(c->chacha20, nonce, key, sizeof(key));

	desc->tfm = c->poly1305;
	crypto_shash_init(desc);
	crypto_shash_update(desc, key, sizeof(key));
}

struct bch_csum bch2_checksum(struct bch_fs *c, unsigned type,
			      struct nonce nonce, const void *data, size_t len)
{
	switch (type) {
	case BCH_CSUM_NONE:
	case BCH_CSUM_CRC32C_NONZERO:
	case BCH_CSUM_CRC64_NONZERO:
	case BCH_CSUM_CRC32C:
	case BCH_CSUM_CRC64: {
		u64 crc = bch2_checksum_init(type);

		crc = bch2_checksum_update(type, crc, data, len);
		crc = bch2_checksum_final(type, crc);

		return (struct bch_csum) { .lo = cpu_to_le64(crc) };
	}

	case BCH_CSUM_CHACHA20_POLY1305_80:
	case BCH_CSUM_CHACHA20_POLY1305_128: {
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

void bch2_encrypt(struct bch_fs *c, unsigned type,
		  struct nonce nonce, void *data, size_t len)
{
	if (!bch2_csum_type_is_encryption(type))
		return;

	do_encrypt(c->chacha20, nonce, data, len);
}

static struct bch_csum __bch2_checksum_bio(struct bch_fs *c, unsigned type,
					   struct nonce nonce, struct bio *bio,
					   struct bvec_iter *iter)
{
	struct bio_vec bv;

	switch (type) {
	case BCH_CSUM_NONE:
		return (struct bch_csum) { 0 };
	case BCH_CSUM_CRC32C_NONZERO:
	case BCH_CSUM_CRC64_NONZERO:
	case BCH_CSUM_CRC32C:
	case BCH_CSUM_CRC64: {
		u64 crc = bch2_checksum_init(type);

#ifdef CONFIG_HIGHMEM
		__bio_for_each_segment(bv, bio, *iter, *iter) {
			void *p = kmap_atomic(bv.bv_page) + bv.bv_offset;
			crc = bch2_checksum_update(type,
				crc, p, bv.bv_len);
			kunmap_atomic(p);
		}
#else
		__bio_for_each_contig_segment(bv, bio, *iter, *iter)
			crc = bch2_checksum_update(type, crc,
				page_address(bv.bv_page) + bv.bv_offset,
				bv.bv_len);
#endif
		crc = bch2_checksum_final(type, crc);
		return (struct bch_csum) { .lo = cpu_to_le64(crc) };
	}

	case BCH_CSUM_CHACHA20_POLY1305_80:
	case BCH_CSUM_CHACHA20_POLY1305_128: {
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
		__bio_for_each_contig_segment(bv, bio, *iter, *iter)
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

void bch2_encrypt_bio(struct bch_fs *c, unsigned type,
		      struct nonce nonce, struct bio *bio)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	struct scatterlist sgl[16], *sg = sgl;
	size_t bytes = 0;

	if (!bch2_csum_type_is_encryption(type))
		return;

	sg_init_table(sgl, ARRAY_SIZE(sgl));

	bio_for_each_segment(bv, bio, iter) {
		if (sg == sgl + ARRAY_SIZE(sgl)) {
			sg_mark_end(sg - 1);
			do_encrypt_sg(c->chacha20, nonce, sgl, bytes);

			nonce = nonce_add(nonce, bytes);
			bytes = 0;

			sg_init_table(sgl, ARRAY_SIZE(sgl));
			sg = sgl;
		}

		sg_set_page(sg++, bv.bv_page, bv.bv_len, bv.bv_offset);
		bytes += bv.bv_len;
	}

	sg_mark_end(sg - 1);
	do_encrypt_sg(c->chacha20, nonce, sgl, bytes);
}

static inline bool bch2_checksum_mergeable(unsigned type)
{

	switch (type) {
	case BCH_CSUM_NONE:
	case BCH_CSUM_CRC32C:
	case BCH_CSUM_CRC64:
		return true;
	default:
		return false;
	}
}

static struct bch_csum bch2_checksum_merge(unsigned type,
					   struct bch_csum a,
					   struct bch_csum b, size_t b_len)
{
	BUG_ON(!bch2_checksum_mergeable(type));

	while (b_len) {
		unsigned b = min_t(unsigned, b_len, PAGE_SIZE);

		a.lo = bch2_checksum_update(type, a.lo,
				page_address(ZERO_PAGE(0)), b);
		b_len -= b;
	}

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
	BUG_ON(crc_old.compression_type);
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

	if (bch2_crc_cmp(merged, crc_old.csum))
		return -EIO;

	for (i = splits; i < splits + ARRAY_SIZE(splits); i++) {
		if (i->crc)
			*i->crc = (struct bch_extent_crc_unpacked) {
				.csum_type		= i->csum_type,
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
int bch2_request_key(struct bch_sb *sb, struct bch_key *key)
{
	char key_description[60];
	struct key *keyring_key;
	const struct user_key_payload *ukp;
	int ret;

	snprintf(key_description, sizeof(key_description),
		 "bcachefs:%pUb", &sb->user_uuid);

	keyring_key = request_key(&key_type_logon, key_description, NULL);
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
#include <uuid/uuid.h>

int bch2_request_key(struct bch_sb *sb, struct bch_key *key)
{
	key_serial_t key_id;
	char key_description[60];
	char uuid[40];

	uuid_unparse_lower(sb->user_uuid.b, uuid);
	sprintf(key_description, "bcachefs:%s", uuid);

	key_id = request_key("user", key_description, NULL,
			     KEY_SPEC_USER_KEYRING);
	if (key_id < 0)
		return -errno;

	if (keyctl_read(key_id, (void *) key, sizeof(*key)) != sizeof(*key))
		return -1;

	return 0;
}
#endif

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
		bch_err(c, "error requesting encryption key: %i", ret);
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
	if (!c->chacha20)
		c->chacha20 = crypto_alloc_sync_skcipher("chacha20", 0, 0);
	if (IS_ERR(c->chacha20)) {
		bch_err(c, "error requesting chacha20 module: %li",
			PTR_ERR(c->chacha20));
		return PTR_ERR(c->chacha20);
	}

	if (!c->poly1305)
		c->poly1305 = crypto_alloc_shash("poly1305", 0, 0);
	if (IS_ERR(c->poly1305)) {
		bch_err(c, "error requesting poly1305 module: %li",
			PTR_ERR(c->poly1305));
		return PTR_ERR(c->poly1305);
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

	crypt->key.magic	= BCH_KEY_MAGIC;
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

	key.magic = BCH_KEY_MAGIC;
	get_random_bytes(&key.key, sizeof(key.key));

	if (keyed) {
		ret = bch2_request_key(c->disk_sb.sb, &user_key);
		if (ret) {
			bch_err(c, "error requesting encryption key: %i", ret);
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
		ret = -ENOMEM; /* XXX this technically could be -ENOSPC */
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

	pr_verbose_init(c->opts, "");

	c->sha256 = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(c->sha256)) {
		bch_err(c, "error requesting sha256 module");
		ret = PTR_ERR(c->sha256);
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
	pr_verbose_init(c->opts, "ret %i", ret);
	return ret;
}
