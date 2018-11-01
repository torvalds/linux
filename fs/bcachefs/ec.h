/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EC_H
#define _BCACHEFS_EC_H

#include "ec_types.h"
#include "keylist_types.h"

const char *bch2_ec_key_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_ec_key_to_text(struct printbuf *, struct bch_fs *,
			 struct bkey_s_c);

#define bch2_bkey_ec_ops (struct bkey_ops) {		\
	.key_invalid	= bch2_ec_key_invalid,		\
	.val_to_text	= bch2_ec_key_to_text,		\
}

struct bch_read_bio;

struct ec_stripe_buf {
	/* might not be buffering the entire stripe: */
	unsigned		offset;
	unsigned		size;
	unsigned long		valid[BITS_TO_LONGS(EC_STRIPE_MAX)];

	void			*data[EC_STRIPE_MAX];

	union {
		struct bkey_i_stripe	key;
		u64			pad[255];
	};
};

struct ec_stripe_head;

struct ec_stripe_new {
	struct bch_fs		*c;
	struct ec_stripe_head	*h;
	struct mutex		lock;
	struct list_head	list;

	/* counts in flight writes, stripe is created when pin == 0 */
	atomic_t		pin;

	int			err;

	unsigned long		blocks_allocated[BITS_TO_LONGS(EC_STRIPE_MAX)];

	struct open_buckets	blocks;
	struct open_buckets	parity;

	struct keylist		keys;
	u64			inline_keys[BKEY_U64s * 8];

	struct ec_stripe_buf	stripe;
};

struct ec_stripe_head {
	struct list_head	list;
	struct mutex		lock;

	struct list_head	stripes;

	unsigned		target;
	unsigned		algo;
	unsigned		redundancy;

	struct bch_devs_mask	devs;
	unsigned		nr_active_devs;

	unsigned		blocksize;

	struct dev_stripe_state	block_stripe;
	struct dev_stripe_state	parity_stripe;

	struct open_buckets	blocks;
	struct open_buckets	parity;

	struct ec_stripe_new	*s;
};

int bch2_ec_read_extent(struct bch_fs *, struct bch_read_bio *);

void *bch2_writepoint_ec_buf(struct bch_fs *, struct write_point *);
void bch2_ec_add_backpointer(struct bch_fs *, struct write_point *,
			     struct bpos, unsigned);

void bch2_ec_bucket_written(struct bch_fs *, struct open_bucket *);
void bch2_ec_bucket_cancel(struct bch_fs *, struct open_bucket *);

int bch2_ec_stripe_new_alloc(struct bch_fs *, struct ec_stripe_head *);

void bch2_ec_stripe_head_put(struct ec_stripe_head *);
struct ec_stripe_head *bch2_ec_stripe_head_get(struct bch_fs *, unsigned,
					       unsigned, unsigned);

void bch2_stripes_heap_update(struct bch_fs *, struct ec_stripe *, size_t);
void bch2_stripes_heap_del(struct bch_fs *, struct ec_stripe *, size_t);
void bch2_stripes_heap_insert(struct bch_fs *, struct ec_stripe *, size_t);

void bch2_ec_stop_dev(struct bch_fs *, struct bch_dev *);

void bch2_ec_flush_new_stripes(struct bch_fs *);

int bch2_fs_ec_start(struct bch_fs *);

void bch2_fs_ec_exit(struct bch_fs *);
int bch2_fs_ec_init(struct bch_fs *);

#endif /* _BCACHEFS_EC_H */
