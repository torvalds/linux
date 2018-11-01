/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REPLICAS_H
#define _BCACHEFS_REPLICAS_H

#include "replicas_types.h"

bool bch2_replicas_marked(struct bch_fs *, enum bch_data_type,
			  struct bch_devs_list, bool);
bool bch2_bkey_replicas_marked(struct bch_fs *,
			       struct bkey_s_c, bool);
int bch2_mark_replicas(struct bch_fs *, enum bch_data_type,
		       struct bch_devs_list);
int bch2_mark_bkey_replicas(struct bch_fs *, struct bkey_s_c);

void bch2_cpu_replicas_to_text(struct printbuf *, struct bch_replicas_cpu *);

struct replicas_status {
	struct {
		int		redundancy;
		unsigned	nr_offline;
	}			replicas[BCH_DATA_NR];
};

struct replicas_status __bch2_replicas_status(struct bch_fs *,
					      struct bch_devs_mask);
struct replicas_status bch2_replicas_status(struct bch_fs *);
bool bch2_have_enough_devs(struct replicas_status, unsigned);

int bch2_replicas_online(struct bch_fs *, bool);
unsigned bch2_dev_has_data(struct bch_fs *, struct bch_dev *);

int bch2_replicas_gc_end(struct bch_fs *, int);
int bch2_replicas_gc_start(struct bch_fs *, unsigned);

/* iterate over superblock replicas - used by userspace tools: */

#define replicas_entry_bytes(_i)					\
	(offsetof(typeof(*(_i)), devs) + (_i)->nr_devs)

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

#endif /* _BCACHEFS_REPLICAS_H */
