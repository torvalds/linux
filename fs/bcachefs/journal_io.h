/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_IO_H
#define _BCACHEFS_JOURNAL_IO_H

#include "darray.h"

void bch2_journal_pos_from_member_info_set(struct bch_fs *);
void bch2_journal_pos_from_member_info_resume(struct bch_fs *);

struct journal_ptr {
	bool		csum_good;
	u8		dev;
	u32		bucket;
	u32		bucket_offset;
	u64		sector;
};

/*
 * Only used for holding the journal entries we read in btree_journal_read()
 * during cache_registration
 */
struct journal_replay {
	DARRAY_PREALLOCATED(struct journal_ptr, 8) ptrs;

	bool			csum_good;
	bool			ignore_blacklisted;
	bool			ignore_not_dirty;
	/* must be last: */
	struct jset		j;
};

static inline bool journal_replay_ignore(struct journal_replay *i)
{
	return !i || i->ignore_blacklisted || i->ignore_not_dirty;
}

static inline struct jset_entry *__jset_entry_type_next(struct jset *jset,
					struct jset_entry *entry, unsigned type)
{
	while (entry < vstruct_last(jset)) {
		if (entry->type == type)
			return entry;

		entry = vstruct_next(entry);
	}

	return NULL;
}

#define for_each_jset_entry_type(entry, jset, type)			\
	for (struct jset_entry *entry = (jset)->start;			\
	     (entry = __jset_entry_type_next(jset, entry, type));	\
	     entry = vstruct_next(entry))

#define jset_entry_for_each_key(_e, _k)					\
	for (struct bkey_i *_k = (_e)->start;				\
	     _k < vstruct_last(_e);					\
	     _k = bkey_next(_k))

#define for_each_jset_key(k, entry, jset)				\
	for_each_jset_entry_type(entry, jset, BCH_JSET_ENTRY_btree_keys)\
		jset_entry_for_each_key(entry, k)

int bch2_journal_entry_validate(struct bch_fs *, struct jset *,
				struct jset_entry *, unsigned, int,
				struct bkey_validate_context);
void bch2_journal_entry_to_text(struct printbuf *, struct bch_fs *,
				struct jset_entry *);

void bch2_journal_ptrs_to_text(struct printbuf *, struct bch_fs *,
			       struct journal_replay *);

int bch2_journal_read(struct bch_fs *, u64 *, u64 *, u64 *);

CLOSURE_CALLBACK(bch2_journal_write);

static inline struct jset_entry *jset_entry_init(struct jset_entry **end, size_t size)
{
	struct jset_entry *entry = *end;
	unsigned u64s = DIV_ROUND_UP(size, sizeof(u64));

	memset(entry, 0, u64s * sizeof(u64));
	/*
	 * The u64s field counts from the start of data, ignoring the shared
	 * fields.
	 */
	entry->u64s = cpu_to_le16(u64s - 1);

	*end = vstruct_next(*end);
	return entry;
}

#endif /* _BCACHEFS_JOURNAL_IO_H */
