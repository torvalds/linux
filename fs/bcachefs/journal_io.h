/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_IO_H
#define _BCACHEFS_JOURNAL_IO_H

/*
 * Only used for holding the journal entries we read in btree_journal_read()
 * during cache_registration
 */
struct journal_replay {
	struct list_head	list;
	struct bch_devs_list	devs;
	/* checksum error, but we may want to try using it anyways: */
	bool			bad;
	bool			ignore;
	/* must be last: */
	struct jset		j;
};

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
	for (entry = (jset)->start;					\
	     (entry = __jset_entry_type_next(jset, entry, type));	\
	     entry = vstruct_next(entry))

#define for_each_jset_key(k, _n, entry, jset)				\
	for_each_jset_entry_type(entry, jset, BCH_JSET_ENTRY_btree_keys)	\
		vstruct_for_each_safe(entry, k, _n)

int bch2_journal_read(struct bch_fs *, struct list_head *, u64 *, u64 *);

void bch2_journal_write(struct closure *);

#endif /* _BCACHEFS_JOURNAL_IO_H */
