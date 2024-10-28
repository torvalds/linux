/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_CHECKSUM_H
#define _BCACHEFS_CHECKSUM_H

#include "bcachefs.h"
#include "extents_types.h"
#include "super-io.h"

#include <linux/crc64.h>
#include <crypto/chacha.h>

static inline bool bch2_checksum_mergeable(unsigned type)
{

	switch (type) {
	case BCH_CSUM_none:
	case BCH_CSUM_crc32c:
	case BCH_CSUM_crc64:
		return true;
	default:
		return false;
	}
}

struct bch_csum bch2_checksum_merge(unsigned, struct bch_csum,
				    struct bch_csum, size_t);

#define BCH_NONCE_EXTENT	cpu_to_le32(1 << 28)
#define BCH_NONCE_BTREE		cpu_to_le32(2 << 28)
#define BCH_NONCE_JOURNAL	cpu_to_le32(3 << 28)
#define BCH_NONCE_PRIO		cpu_to_le32(4 << 28)
#define BCH_NONCE_POLY		cpu_to_le32(1 << 31)

struct bch_csum bch2_checksum(struct bch_fs *, unsigned, struct nonce,
			     const void *, size_t);

/*
 * This is used for various on disk data structures - bch_sb, prio_set, bset,
 * jset: The checksum is _always_ the first field of these structs
 */
#define csum_vstruct(_c, _type, _nonce, _i)				\
({									\
	const void *_start = ((const void *) (_i)) + sizeof((_i)->csum);\
									\
	bch2_checksum(_c, _type, _nonce, _start, vstruct_end(_i) - _start);\
})

static inline void bch2_csum_to_text(struct printbuf *out,
				     enum bch_csum_type type,
				     struct bch_csum csum)
{
	const u8 *p = (u8 *) &csum;
	unsigned bytes = type < BCH_CSUM_NR ? bch_crc_bytes[type] : 16;

	for (unsigned i = 0; i < bytes; i++)
		prt_hex_byte(out, p[i]);
}

static inline void bch2_csum_err_msg(struct printbuf *out,
				     enum bch_csum_type type,
				     struct bch_csum expected,
				     struct bch_csum got)
{
	prt_str(out, "checksum error, type ");
	bch2_prt_csum_type(out, type);
	prt_str(out, ": got ");
	bch2_csum_to_text(out, type, got);
	prt_str(out, " should be ");
	bch2_csum_to_text(out, type, expected);
}

int bch2_chacha_encrypt_key(struct bch_key *, struct nonce, void *, size_t);
int bch2_request_key(struct bch_sb *, struct bch_key *);
#ifndef __KERNEL__
int bch2_revoke_key(struct bch_sb *);
#endif

int bch2_encrypt(struct bch_fs *, unsigned, struct nonce,
		 void *data, size_t);

struct bch_csum bch2_checksum_bio(struct bch_fs *, unsigned,
				  struct nonce, struct bio *);

int bch2_rechecksum_bio(struct bch_fs *, struct bio *, struct bversion,
			struct bch_extent_crc_unpacked,
			struct bch_extent_crc_unpacked *,
			struct bch_extent_crc_unpacked *,
			unsigned, unsigned, unsigned);

int __bch2_encrypt_bio(struct bch_fs *, unsigned,
		       struct nonce, struct bio *);

static inline int bch2_encrypt_bio(struct bch_fs *c, unsigned type,
				   struct nonce nonce, struct bio *bio)
{
	return bch2_csum_type_is_encryption(type)
		? __bch2_encrypt_bio(c, type, nonce, bio)
		: 0;
}

extern const struct bch_sb_field_ops bch_sb_field_ops_crypt;

int bch2_decrypt_sb_key(struct bch_fs *, struct bch_sb_field_crypt *,
			struct bch_key *);

int bch2_disable_encryption(struct bch_fs *);
int bch2_enable_encryption(struct bch_fs *, bool);

void bch2_fs_encryption_exit(struct bch_fs *);
int bch2_fs_encryption_init(struct bch_fs *);

static inline enum bch_csum_type bch2_csum_opt_to_type(enum bch_csum_opt type,
						       bool data)
{
	switch (type) {
	case BCH_CSUM_OPT_none:
		return BCH_CSUM_none;
	case BCH_CSUM_OPT_crc32c:
		return data ? BCH_CSUM_crc32c : BCH_CSUM_crc32c_nonzero;
	case BCH_CSUM_OPT_crc64:
		return data ? BCH_CSUM_crc64 : BCH_CSUM_crc64_nonzero;
	case BCH_CSUM_OPT_xxhash:
		return BCH_CSUM_xxhash;
	default:
		BUG();
	}
}

static inline enum bch_csum_type bch2_data_checksum_type(struct bch_fs *c,
							 struct bch_io_opts opts)
{
	if (opts.nocow)
		return 0;

	if (c->sb.encryption_type)
		return c->opts.wide_macs
			? BCH_CSUM_chacha20_poly1305_128
			: BCH_CSUM_chacha20_poly1305_80;

	return bch2_csum_opt_to_type(opts.data_checksum, true);
}

static inline enum bch_csum_type bch2_meta_checksum_type(struct bch_fs *c)
{
	if (c->sb.encryption_type)
		return BCH_CSUM_chacha20_poly1305_128;

	return bch2_csum_opt_to_type(c->opts.metadata_checksum, false);
}

static inline bool bch2_checksum_type_valid(const struct bch_fs *c,
					   unsigned type)
{
	if (type >= BCH_CSUM_NR)
		return false;

	if (bch2_csum_type_is_encryption(type) && !c->chacha20)
		return false;

	return true;
}

/* returns true if not equal */
static inline bool bch2_crc_cmp(struct bch_csum l, struct bch_csum r)
{
	/*
	 * XXX: need some way of preventing the compiler from optimizing this
	 * into a form that isn't constant time..
	 */
	return ((l.lo ^ r.lo) | (l.hi ^ r.hi)) != 0;
}

/* for skipping ahead and encrypting/decrypting at an offset: */
static inline struct nonce nonce_add(struct nonce nonce, unsigned offset)
{
	EBUG_ON(offset & (CHACHA_BLOCK_SIZE - 1));

	le32_add_cpu(&nonce.d[0], offset / CHACHA_BLOCK_SIZE);
	return nonce;
}

static inline struct nonce null_nonce(void)
{
	struct nonce ret;

	memset(&ret, 0, sizeof(ret));
	return ret;
}

static inline struct nonce extent_nonce(struct bversion version,
					struct bch_extent_crc_unpacked crc)
{
	unsigned compression_type = crc_is_compressed(crc)
		? crc.compression_type
		: 0;
	unsigned size = compression_type ? crc.uncompressed_size : 0;
	struct nonce nonce = (struct nonce) {{
		[0] = cpu_to_le32(size << 22),
		[1] = cpu_to_le32(version.lo),
		[2] = cpu_to_le32(version.lo >> 32),
		[3] = cpu_to_le32(version.hi|
				  (compression_type << 24))^BCH_NONCE_EXTENT,
	}};

	return nonce_add(nonce, crc.nonce << 9);
}

static inline bool bch2_key_is_encrypted(struct bch_encrypted_key *key)
{
	return le64_to_cpu(key->magic) != BCH_KEY_MAGIC;
}

static inline struct nonce __bch2_sb_key_nonce(struct bch_sb *sb)
{
	__le64 magic = __bch2_sb_magic(sb);

	return (struct nonce) {{
		[0] = 0,
		[1] = 0,
		[2] = ((__le32 *) &magic)[0],
		[3] = ((__le32 *) &magic)[1],
	}};
}

static inline struct nonce bch2_sb_key_nonce(struct bch_fs *c)
{
	__le64 magic = bch2_sb_magic(c);

	return (struct nonce) {{
		[0] = 0,
		[1] = 0,
		[2] = ((__le32 *) &magic)[0],
		[3] = ((__le32 *) &magic)[1],
	}};
}

#endif /* _BCACHEFS_CHECKSUM_H */
