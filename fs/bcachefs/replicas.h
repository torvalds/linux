/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REPLICAS_H
#define _BCACHEFS_REPLICAS_H

#include "bkey.h"
#include "eytzinger.h"
#include "replicas_types.h"

void bch2_replicas_entry_sort(struct bch_replicas_entry_v1 *);
void bch2_replicas_entry_to_text(struct printbuf *,
				 struct bch_replicas_entry_v1 *);
int bch2_replicas_entry_validate(struct bch_replicas_entry_v1 *,
				 struct bch_sb *, struct printbuf *);
void bch2_cpu_replicas_to_text(struct printbuf *, struct bch_replicas_cpu *);

static inline struct bch_replicas_entry_v1 *
cpu_replicas_entry(struct bch_replicas_cpu *r, unsigned i)
{
	return (void *) r->entries + r->entry_size * i;
}

int bch2_replicas_entry_idx(struct bch_fs *,
			    struct bch_replicas_entry_v1 *);

void bch2_devlist_to_replicas(struct bch_replicas_entry_v1 *,
			      enum bch_data_type,
			      struct bch_devs_list);
bool bch2_replicas_marked(struct bch_fs *, struct bch_replicas_entry_v1 *);
int bch2_mark_replicas(struct bch_fs *,
		       struct bch_replicas_entry_v1 *);

static inline struct replicas_delta *
replicas_delta_next(struct replicas_delta *d)
{
	return (void *) d + replicas_entry_bytes(&d->r) + 8;
}

int bch2_replicas_delta_list_mark(struct bch_fs *, struct replicas_delta_list *);

void bch2_bkey_to_replicas(struct bch_replicas_entry_v1 *, struct bkey_s_c);

static inline void bch2_replicas_entry_cached(struct bch_replicas_entry_v1 *e,
					      unsigned dev)
{
	e->data_type	= BCH_DATA_cached;
	e->nr_devs	= 1;
	e->nr_required	= 1;
	e->devs[0]	= dev;
}

bool bch2_have_enough_devs(struct bch_fs *, struct bch_devs_mask,
			   unsigned, bool);

unsigned bch2_sb_dev_has_data(struct bch_sb *, unsigned);
unsigned bch2_dev_has_data(struct bch_fs *, struct bch_dev *);

int bch2_replicas_gc_end(struct bch_fs *, int);
int bch2_replicas_gc_start(struct bch_fs *, unsigned);
int bch2_replicas_gc2(struct bch_fs *);

int bch2_replicas_set_usage(struct bch_fs *,
			    struct bch_replicas_entry_v1 *,
			    u64);

#define for_each_cpu_replicas_entry(_r, _i)				\
	for (_i = (_r)->entries;					\
	     (void *) (_i) < (void *) (_r)->entries + (_r)->nr * (_r)->entry_size;\
	     _i = (void *) (_i) + (_r)->entry_size)

/* iterate over superblock replicas - used by userspace tools: */

#define replicas_entry_next(_i)						\
	((typeof(_i)) ((void *) (_i) + replicas_entry_bytes(_i)))

#define for_each_replicas_entry(_r, _i)					\
	for (_i = (_r)->entries;					\
	     (void *) (_i) < vstruct_end(&(_r)->field) && (_i)->data_type;\
	     (_i) = replicas_entry_next(_i))

#define for_each_replicas_entry_v0(_r, _i)				\
	for (_i = (_r)->entries;					\
	     (void *) (_i) < vstruct_end(&(_r)->field) && (_i)->data_type;\
	     (_i) = replicas_entry_next(_i))

int bch2_sb_replicas_to_cpu_replicas(struct bch_fs *);

extern const struct bch_sb_field_ops bch_sb_field_ops_replicas;
extern const struct bch_sb_field_ops bch_sb_field_ops_replicas_v0;

void bch2_fs_replicas_exit(struct bch_fs *);
int bch2_fs_replicas_init(struct bch_fs *);

#endif /* _BCACHEFS_REPLICAS_H */
