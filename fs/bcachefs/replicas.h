/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REPLICAS_H
#define _BCACHEFS_REPLICAS_H

bool bch2_replicas_marked(struct bch_fs *, enum bch_data_type,
			  struct bch_devs_list);
bool bch2_bkey_replicas_marked(struct bch_fs *, enum bch_data_type,
			       struct bkey_s_c);
int bch2_mark_replicas(struct bch_fs *, enum bch_data_type,
		       struct bch_devs_list);
int bch2_mark_bkey_replicas(struct bch_fs *, enum bch_data_type,
			    struct bkey_s_c);

int bch2_cpu_replicas_to_text(struct bch_replicas_cpu *, char *, size_t);
int bch2_sb_replicas_to_text(struct bch_sb_field_replicas *, char *, size_t);

struct replicas_status {
	struct {
		unsigned	nr_online;
		unsigned	nr_offline;
	}			replicas[BCH_DATA_NR];
};

struct replicas_status __bch2_replicas_status(struct bch_fs *,
					      struct bch_devs_mask);
struct replicas_status bch2_replicas_status(struct bch_fs *);
bool bch2_have_enough_devs(struct replicas_status, unsigned);

unsigned bch2_replicas_online(struct bch_fs *, bool);
unsigned bch2_dev_has_data(struct bch_fs *, struct bch_dev *);

int bch2_replicas_gc_end(struct bch_fs *, int);
int bch2_replicas_gc_start(struct bch_fs *, unsigned);

/* iterate over superblock replicas - used by userspace tools: */

static inline struct bch_replicas_entry *
replicas_entry_next(struct bch_replicas_entry *i)
{
	return (void *) i + offsetof(struct bch_replicas_entry, devs) + i->nr;
}

#define for_each_replicas_entry(_r, _i)					\
	for (_i = (_r)->entries;					\
	     (void *) (_i) < vstruct_end(&(_r)->field) && (_i)->data_type;\
	     (_i) = replicas_entry_next(_i))

int bch2_sb_replicas_to_cpu_replicas(struct bch_fs *);

extern const struct bch_sb_field_ops bch_sb_field_ops_replicas;

#endif /* _BCACHEFS_REPLICAS_H */
