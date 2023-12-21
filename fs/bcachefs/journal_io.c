// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_io.h"
#include "btree_update_interior.h"
#include "btree_write_buffer.h"
#include "buckets.h"
#include "checksum.h"
#include "disk_groups.h"
#include "error.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "replicas.h"
#include "sb-clean.h"
#include "trace.h"

static struct nonce journal_nonce(const struct jset *jset)
{
	return (struct nonce) {{
		[0] = 0,
		[1] = ((__le32 *) &jset->seq)[0],
		[2] = ((__le32 *) &jset->seq)[1],
		[3] = BCH_NONCE_JOURNAL,
	}};
}

static bool jset_csum_good(struct bch_fs *c, struct jset *j)
{
	return bch2_checksum_type_valid(c, JSET_CSUM_TYPE(j)) &&
		!bch2_crc_cmp(j->csum,
			      csum_vstruct(c, JSET_CSUM_TYPE(j), journal_nonce(j), j));
}

static inline u32 journal_entry_radix_idx(struct bch_fs *c, u64 seq)
{
	return (seq - c->journal_entries_base_seq) & (~0U >> 1);
}

static void __journal_replay_free(struct bch_fs *c,
				  struct journal_replay *i)
{
	struct journal_replay **p =
		genradix_ptr(&c->journal_entries,
			     journal_entry_radix_idx(c, le64_to_cpu(i->j.seq)));

	BUG_ON(*p != i);
	*p = NULL;
	kvpfree(i, offsetof(struct journal_replay, j) +
		vstruct_bytes(&i->j));
}

static void journal_replay_free(struct bch_fs *c, struct journal_replay *i)
{
	i->ignore = true;

	if (!c->opts.read_entire_journal)
		__journal_replay_free(c, i);
}

struct journal_list {
	struct closure		cl;
	u64			last_seq;
	struct mutex		lock;
	int			ret;
};

#define JOURNAL_ENTRY_ADD_OK		0
#define JOURNAL_ENTRY_ADD_OUT_OF_RANGE	5

/*
 * Given a journal entry we just read, add it to the list of journal entries to
 * be replayed:
 */
static int journal_entry_add(struct bch_fs *c, struct bch_dev *ca,
			     struct journal_ptr entry_ptr,
			     struct journal_list *jlist, struct jset *j)
{
	struct genradix_iter iter;
	struct journal_replay **_i, *i, *dup;
	struct journal_ptr *ptr;
	size_t bytes = vstruct_bytes(j);
	u64 last_seq = !JSET_NO_FLUSH(j) ? le64_to_cpu(j->last_seq) : 0;
	int ret = JOURNAL_ENTRY_ADD_OK;

	/* Is this entry older than the range we need? */
	if (!c->opts.read_entire_journal &&
	    le64_to_cpu(j->seq) < jlist->last_seq)
		return JOURNAL_ENTRY_ADD_OUT_OF_RANGE;

	/*
	 * genradixes are indexed by a ulong, not a u64, so we can't index them
	 * by sequence number directly: Assume instead that they will all fall
	 * within the range of +-2billion of the filrst one we find.
	 */
	if (!c->journal_entries_base_seq)
		c->journal_entries_base_seq = max_t(s64, 1, le64_to_cpu(j->seq) - S32_MAX);

	/* Drop entries we don't need anymore */
	if (last_seq > jlist->last_seq && !c->opts.read_entire_journal) {
		genradix_for_each_from(&c->journal_entries, iter, _i,
				       journal_entry_radix_idx(c, jlist->last_seq)) {
			i = *_i;

			if (!i || i->ignore)
				continue;

			if (le64_to_cpu(i->j.seq) >= last_seq)
				break;
			journal_replay_free(c, i);
		}
	}

	jlist->last_seq = max(jlist->last_seq, last_seq);

	_i = genradix_ptr_alloc(&c->journal_entries,
				journal_entry_radix_idx(c, le64_to_cpu(j->seq)),
				GFP_KERNEL);
	if (!_i)
		return -BCH_ERR_ENOMEM_journal_entry_add;

	/*
	 * Duplicate journal entries? If so we want the one that didn't have a
	 * checksum error:
	 */
	dup = *_i;
	if (dup) {
		if (bytes == vstruct_bytes(&dup->j) &&
		    !memcmp(j, &dup->j, bytes)) {
			i = dup;
			goto found;
		}

		if (!entry_ptr.csum_good) {
			i = dup;
			goto found;
		}

		if (!dup->csum_good)
			goto replace;

		fsck_err(c, journal_entry_replicas_data_mismatch,
			 "found duplicate but non identical journal entries (seq %llu)",
			 le64_to_cpu(j->seq));
		i = dup;
		goto found;
	}
replace:
	i = kvpmalloc(offsetof(struct journal_replay, j) + bytes, GFP_KERNEL);
	if (!i)
		return -BCH_ERR_ENOMEM_journal_entry_add;

	i->nr_ptrs	= 0;
	i->csum_good	= entry_ptr.csum_good;
	i->ignore	= false;
	unsafe_memcpy(&i->j, j, bytes, "embedded variable length struct");
	i->ptrs[i->nr_ptrs++] = entry_ptr;

	if (dup) {
		if (dup->nr_ptrs >= ARRAY_SIZE(dup->ptrs)) {
			bch_err(c, "found too many copies of journal entry %llu",
				le64_to_cpu(i->j.seq));
			dup->nr_ptrs = ARRAY_SIZE(dup->ptrs) - 1;
		}

		/* The first ptr should represent the jset we kept: */
		memcpy(i->ptrs + i->nr_ptrs,
		       dup->ptrs,
		       sizeof(dup->ptrs[0]) * dup->nr_ptrs);
		i->nr_ptrs += dup->nr_ptrs;
		__journal_replay_free(c, dup);
	}

	*_i = i;
	return 0;
found:
	for (ptr = i->ptrs; ptr < i->ptrs + i->nr_ptrs; ptr++) {
		if (ptr->dev == ca->dev_idx) {
			bch_err(c, "duplicate journal entry %llu on same device",
				le64_to_cpu(i->j.seq));
			goto out;
		}
	}

	if (i->nr_ptrs >= ARRAY_SIZE(i->ptrs)) {
		bch_err(c, "found too many copies of journal entry %llu",
			le64_to_cpu(i->j.seq));
		goto out;
	}

	i->ptrs[i->nr_ptrs++] = entry_ptr;
out:
fsck_err:
	return ret;
}

/* this fills in a range with empty jset_entries: */
static void journal_entry_null_range(void *start, void *end)
{
	struct jset_entry *entry;

	for (entry = start; entry != end; entry = vstruct_next(entry))
		memset(entry, 0, sizeof(*entry));
}

#define JOURNAL_ENTRY_REREAD	5
#define JOURNAL_ENTRY_NONE	6
#define JOURNAL_ENTRY_BAD	7

static void journal_entry_err_msg(struct printbuf *out,
				  u32 version,
				  struct jset *jset,
				  struct jset_entry *entry)
{
	prt_str(out, "invalid journal entry, version=");
	bch2_version_to_text(out, version);

	if (entry) {
		prt_str(out, " type=");
		prt_str(out, bch2_jset_entry_types[entry->type]);
	}

	if (!jset) {
		prt_printf(out, " in superblock");
	} else {

		prt_printf(out, " seq=%llu", le64_to_cpu(jset->seq));

		if (entry)
			prt_printf(out, " offset=%zi/%u",
				   (u64 *) entry - jset->_data,
				   le32_to_cpu(jset->u64s));
	}

	prt_str(out, ": ");
}

#define journal_entry_err(c, version, jset, entry, _err, msg, ...)	\
({									\
	struct printbuf _buf = PRINTBUF;				\
									\
	journal_entry_err_msg(&_buf, version, jset, entry);		\
	prt_printf(&_buf, msg, ##__VA_ARGS__);				\
									\
	switch (flags & BKEY_INVALID_WRITE) {				\
	case READ:							\
		mustfix_fsck_err(c, _err, "%s", _buf.buf);		\
		break;							\
	case WRITE:							\
		bch2_sb_error_count(c, BCH_FSCK_ERR_##_err);		\
		bch_err(c, "corrupt metadata before write: %s\n", _buf.buf);\
		if (bch2_fs_inconsistent(c)) {				\
			ret = -BCH_ERR_fsck_errors_not_fixed;		\
			goto fsck_err;					\
		}							\
		break;							\
	}								\
									\
	printbuf_exit(&_buf);						\
	true;								\
})

#define journal_entry_err_on(cond, ...)					\
	((cond) ? journal_entry_err(__VA_ARGS__) : false)

#define FSCK_DELETED_KEY	5

static int journal_validate_key(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned level, enum btree_id btree_id,
				struct bkey_i *k,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	int write = flags & BKEY_INVALID_WRITE;
	void *next = vstruct_next(entry);
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (journal_entry_err_on(!k->k.u64s,
				 c, version, jset, entry,
				 journal_entry_bkey_u64s_0,
				 "k->u64s 0")) {
		entry->u64s = cpu_to_le16((u64 *) k - entry->_data);
		journal_entry_null_range(vstruct_next(entry), next);
		return FSCK_DELETED_KEY;
	}

	if (journal_entry_err_on((void *) bkey_next(k) >
				 (void *) vstruct_next(entry),
				 c, version, jset, entry,
				 journal_entry_bkey_past_end,
				 "extends past end of journal entry")) {
		entry->u64s = cpu_to_le16((u64 *) k - entry->_data);
		journal_entry_null_range(vstruct_next(entry), next);
		return FSCK_DELETED_KEY;
	}

	if (journal_entry_err_on(k->k.format != KEY_FORMAT_CURRENT,
				 c, version, jset, entry,
				 journal_entry_bkey_bad_format,
				 "bad format %u", k->k.format)) {
		le16_add_cpu(&entry->u64s, -((u16) k->k.u64s));
		memmove(k, bkey_next(k), next - (void *) bkey_next(k));
		journal_entry_null_range(vstruct_next(entry), next);
		return FSCK_DELETED_KEY;
	}

	if (!write)
		bch2_bkey_compat(level, btree_id, version, big_endian,
				 write, NULL, bkey_to_packed(k));

	if (bch2_bkey_invalid(c, bkey_i_to_s_c(k),
			      __btree_node_type(level, btree_id), write, &buf)) {
		printbuf_reset(&buf);
		journal_entry_err_msg(&buf, version, jset, entry);
		prt_newline(&buf);
		printbuf_indent_add(&buf, 2);

		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(k));
		prt_newline(&buf);
		bch2_bkey_invalid(c, bkey_i_to_s_c(k),
				  __btree_node_type(level, btree_id), write, &buf);

		mustfix_fsck_err(c, journal_entry_bkey_invalid,
				 "%s", buf.buf);

		le16_add_cpu(&entry->u64s, -((u16) k->k.u64s));
		memmove(k, bkey_next(k), next - (void *) bkey_next(k));
		journal_entry_null_range(vstruct_next(entry), next);

		printbuf_exit(&buf);
		return FSCK_DELETED_KEY;
	}

	if (write)
		bch2_bkey_compat(level, btree_id, version, big_endian,
				 write, NULL, bkey_to_packed(k));
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static int journal_entry_btree_keys_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	struct bkey_i *k = entry->start;

	while (k != vstruct_last(entry)) {
		int ret = journal_validate_key(c, jset, entry,
					       entry->level,
					       entry->btree_id,
					       k, version, big_endian,
					       flags|BKEY_INVALID_JOURNAL);
		if (ret == FSCK_DELETED_KEY)
			continue;

		k = bkey_next(k);
	}

	return 0;
}

static void journal_entry_btree_keys_to_text(struct printbuf *out, struct bch_fs *c,
					     struct jset_entry *entry)
{
	struct bkey_i *k;
	bool first = true;

	jset_entry_for_each_key(entry, k) {
		if (!first) {
			prt_newline(out);
			prt_printf(out, "%s: ", bch2_jset_entry_types[entry->type]);
		}
		prt_printf(out, "btree=%s l=%u ", bch2_btree_id_str(entry->btree_id), entry->level);
		bch2_bkey_val_to_text(out, c, bkey_i_to_s_c(k));
		first = false;
	}
}

static int journal_entry_btree_root_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	struct bkey_i *k = entry->start;
	int ret = 0;

	if (journal_entry_err_on(!entry->u64s ||
				 le16_to_cpu(entry->u64s) != k->k.u64s,
				 c, version, jset, entry,
				 journal_entry_btree_root_bad_size,
				 "invalid btree root journal entry: wrong number of keys")) {
		void *next = vstruct_next(entry);
		/*
		 * we don't want to null out this jset_entry,
		 * just the contents, so that later we can tell
		 * we were _supposed_ to have a btree root
		 */
		entry->u64s = 0;
		journal_entry_null_range(vstruct_next(entry), next);
		return 0;
	}

	ret = journal_validate_key(c, jset, entry, 1, entry->btree_id, k,
				   version, big_endian, flags);
	if (ret == FSCK_DELETED_KEY)
		ret = 0;
fsck_err:
	return ret;
}

static void journal_entry_btree_root_to_text(struct printbuf *out, struct bch_fs *c,
					     struct jset_entry *entry)
{
	journal_entry_btree_keys_to_text(out, c, entry);
}

static int journal_entry_prio_ptrs_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	/* obsolete, don't care: */
	return 0;
}

static void journal_entry_prio_ptrs_to_text(struct printbuf *out, struct bch_fs *c,
					    struct jset_entry *entry)
{
}

static int journal_entry_blacklist_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	int ret = 0;

	if (journal_entry_err_on(le16_to_cpu(entry->u64s) != 1,
				 c, version, jset, entry,
				 journal_entry_blacklist_bad_size,
		"invalid journal seq blacklist entry: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
	}
fsck_err:
	return ret;
}

static void journal_entry_blacklist_to_text(struct printbuf *out, struct bch_fs *c,
					    struct jset_entry *entry)
{
	struct jset_entry_blacklist *bl =
		container_of(entry, struct jset_entry_blacklist, entry);

	prt_printf(out, "seq=%llu", le64_to_cpu(bl->seq));
}

static int journal_entry_blacklist_v2_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	struct jset_entry_blacklist_v2 *bl_entry;
	int ret = 0;

	if (journal_entry_err_on(le16_to_cpu(entry->u64s) != 2,
				 c, version, jset, entry,
				 journal_entry_blacklist_v2_bad_size,
		"invalid journal seq blacklist entry: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		goto out;
	}

	bl_entry = container_of(entry, struct jset_entry_blacklist_v2, entry);

	if (journal_entry_err_on(le64_to_cpu(bl_entry->start) >
				 le64_to_cpu(bl_entry->end),
				 c, version, jset, entry,
				 journal_entry_blacklist_v2_start_past_end,
		"invalid journal seq blacklist entry: start > end")) {
		journal_entry_null_range(entry, vstruct_next(entry));
	}
out:
fsck_err:
	return ret;
}

static void journal_entry_blacklist_v2_to_text(struct printbuf *out, struct bch_fs *c,
					       struct jset_entry *entry)
{
	struct jset_entry_blacklist_v2 *bl =
		container_of(entry, struct jset_entry_blacklist_v2, entry);

	prt_printf(out, "start=%llu end=%llu",
	       le64_to_cpu(bl->start),
	       le64_to_cpu(bl->end));
}

static int journal_entry_usage_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	struct jset_entry_usage *u =
		container_of(entry, struct jset_entry_usage, entry);
	unsigned bytes = jset_u64s(le16_to_cpu(entry->u64s)) * sizeof(u64);
	int ret = 0;

	if (journal_entry_err_on(bytes < sizeof(*u),
				 c, version, jset, entry,
				 journal_entry_usage_bad_size,
				 "invalid journal entry usage: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

fsck_err:
	return ret;
}

static void journal_entry_usage_to_text(struct printbuf *out, struct bch_fs *c,
					struct jset_entry *entry)
{
	struct jset_entry_usage *u =
		container_of(entry, struct jset_entry_usage, entry);

	prt_printf(out, "type=%s v=%llu",
	       bch2_fs_usage_types[u->entry.btree_id],
	       le64_to_cpu(u->v));
}

static int journal_entry_data_usage_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	struct jset_entry_data_usage *u =
		container_of(entry, struct jset_entry_data_usage, entry);
	unsigned bytes = jset_u64s(le16_to_cpu(entry->u64s)) * sizeof(u64);
	struct printbuf err = PRINTBUF;
	int ret = 0;

	if (journal_entry_err_on(bytes < sizeof(*u) ||
				 bytes < sizeof(*u) + u->r.nr_devs,
				 c, version, jset, entry,
				 journal_entry_data_usage_bad_size,
				 "invalid journal entry usage: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		goto out;
	}

	if (journal_entry_err_on(bch2_replicas_entry_validate(&u->r, c->disk_sb.sb, &err),
				 c, version, jset, entry,
				 journal_entry_data_usage_bad_size,
				 "invalid journal entry usage: %s", err.buf)) {
		journal_entry_null_range(entry, vstruct_next(entry));
		goto out;
	}
out:
fsck_err:
	printbuf_exit(&err);
	return ret;
}

static void journal_entry_data_usage_to_text(struct printbuf *out, struct bch_fs *c,
					     struct jset_entry *entry)
{
	struct jset_entry_data_usage *u =
		container_of(entry, struct jset_entry_data_usage, entry);

	bch2_replicas_entry_to_text(out, &u->r);
	prt_printf(out, "=%llu", le64_to_cpu(u->v));
}

static int journal_entry_clock_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	struct jset_entry_clock *clock =
		container_of(entry, struct jset_entry_clock, entry);
	unsigned bytes = jset_u64s(le16_to_cpu(entry->u64s)) * sizeof(u64);
	int ret = 0;

	if (journal_entry_err_on(bytes != sizeof(*clock),
				 c, version, jset, entry,
				 journal_entry_clock_bad_size,
				 "bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

	if (journal_entry_err_on(clock->rw > 1,
				 c, version, jset, entry,
				 journal_entry_clock_bad_rw,
				 "bad rw")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

fsck_err:
	return ret;
}

static void journal_entry_clock_to_text(struct printbuf *out, struct bch_fs *c,
					struct jset_entry *entry)
{
	struct jset_entry_clock *clock =
		container_of(entry, struct jset_entry_clock, entry);

	prt_printf(out, "%s=%llu", clock->rw ? "write" : "read", le64_to_cpu(clock->time));
}

static int journal_entry_dev_usage_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	struct jset_entry_dev_usage *u =
		container_of(entry, struct jset_entry_dev_usage, entry);
	unsigned bytes = jset_u64s(le16_to_cpu(entry->u64s)) * sizeof(u64);
	unsigned expected = sizeof(*u);
	unsigned dev;
	int ret = 0;

	if (journal_entry_err_on(bytes < expected,
				 c, version, jset, entry,
				 journal_entry_dev_usage_bad_size,
				 "bad size (%u < %u)",
				 bytes, expected)) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

	dev = le32_to_cpu(u->dev);

	if (journal_entry_err_on(!bch2_dev_exists2(c, dev),
				 c, version, jset, entry,
				 journal_entry_dev_usage_bad_dev,
				 "bad dev")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

	if (journal_entry_err_on(u->pad,
				 c, version, jset, entry,
				 journal_entry_dev_usage_bad_pad,
				 "bad pad")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

fsck_err:
	return ret;
}

static void journal_entry_dev_usage_to_text(struct printbuf *out, struct bch_fs *c,
					    struct jset_entry *entry)
{
	struct jset_entry_dev_usage *u =
		container_of(entry, struct jset_entry_dev_usage, entry);
	unsigned i, nr_types = jset_entry_dev_usage_nr_types(u);

	prt_printf(out, "dev=%u", le32_to_cpu(u->dev));

	for (i = 0; i < nr_types; i++) {
		if (i < BCH_DATA_NR)
			prt_printf(out, " %s", bch2_data_types[i]);
		else
			prt_printf(out, " (unknown data type %u)", i);
		prt_printf(out, ": buckets=%llu sectors=%llu fragmented=%llu",
		       le64_to_cpu(u->d[i].buckets),
		       le64_to_cpu(u->d[i].sectors),
		       le64_to_cpu(u->d[i].fragmented));
	}
}

static int journal_entry_log_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	return 0;
}

static void journal_entry_log_to_text(struct printbuf *out, struct bch_fs *c,
				      struct jset_entry *entry)
{
	struct jset_entry_log *l = container_of(entry, struct jset_entry_log, entry);
	unsigned bytes = vstruct_bytes(entry) - offsetof(struct jset_entry_log, d);

	prt_printf(out, "%.*s", bytes, l->d);
}

static int journal_entry_overwrite_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	return journal_entry_btree_keys_validate(c, jset, entry,
				version, big_endian, READ);
}

static void journal_entry_overwrite_to_text(struct printbuf *out, struct bch_fs *c,
					    struct jset_entry *entry)
{
	journal_entry_btree_keys_to_text(out, c, entry);
}

static int journal_entry_write_buffer_keys_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	return journal_entry_btree_keys_validate(c, jset, entry,
				version, big_endian, READ);
}

static void journal_entry_write_buffer_keys_to_text(struct printbuf *out, struct bch_fs *c,
					    struct jset_entry *entry)
{
	journal_entry_btree_keys_to_text(out, c, entry);
}

struct jset_entry_ops {
	int (*validate)(struct bch_fs *, struct jset *,
			struct jset_entry *, unsigned, int,
			enum bkey_invalid_flags);
	void (*to_text)(struct printbuf *, struct bch_fs *, struct jset_entry *);
};

static const struct jset_entry_ops bch2_jset_entry_ops[] = {
#define x(f, nr)						\
	[BCH_JSET_ENTRY_##f]	= (struct jset_entry_ops) {	\
		.validate	= journal_entry_##f##_validate,	\
		.to_text	= journal_entry_##f##_to_text,	\
	},
	BCH_JSET_ENTRY_TYPES()
#undef x
};

int bch2_journal_entry_validate(struct bch_fs *c,
				struct jset *jset,
				struct jset_entry *entry,
				unsigned version, int big_endian,
				enum bkey_invalid_flags flags)
{
	return entry->type < BCH_JSET_ENTRY_NR
		? bch2_jset_entry_ops[entry->type].validate(c, jset, entry,
				version, big_endian, flags)
		: 0;
}

void bch2_journal_entry_to_text(struct printbuf *out, struct bch_fs *c,
				struct jset_entry *entry)
{
	if (entry->type < BCH_JSET_ENTRY_NR) {
		prt_printf(out, "%s: ", bch2_jset_entry_types[entry->type]);
		bch2_jset_entry_ops[entry->type].to_text(out, c, entry);
	} else {
		prt_printf(out, "(unknown type %u)", entry->type);
	}
}

static int jset_validate_entries(struct bch_fs *c, struct jset *jset,
				 enum bkey_invalid_flags flags)
{
	unsigned version = le32_to_cpu(jset->version);
	int ret = 0;

	vstruct_for_each(jset, entry) {
		if (journal_entry_err_on(vstruct_next(entry) > vstruct_last(jset),
				c, version, jset, entry,
				journal_entry_past_jset_end,
				"journal entry extends past end of jset")) {
			jset->u64s = cpu_to_le32((u64 *) entry - jset->_data);
			break;
		}

		ret = bch2_journal_entry_validate(c, jset, entry,
					version, JSET_BIG_ENDIAN(jset), flags);
		if (ret)
			break;
	}
fsck_err:
	return ret;
}

static int jset_validate(struct bch_fs *c,
			 struct bch_dev *ca,
			 struct jset *jset, u64 sector,
			 enum bkey_invalid_flags flags)
{
	unsigned version;
	int ret = 0;

	if (le64_to_cpu(jset->magic) != jset_magic(c))
		return JOURNAL_ENTRY_NONE;

	version = le32_to_cpu(jset->version);
	if (journal_entry_err_on(!bch2_version_compatible(version),
			c, version, jset, NULL,
			jset_unsupported_version,
			"%s sector %llu seq %llu: incompatible journal entry version %u.%u",
			ca ? ca->name : c->name,
			sector, le64_to_cpu(jset->seq),
			BCH_VERSION_MAJOR(version),
			BCH_VERSION_MINOR(version))) {
		/* don't try to continue: */
		return -EINVAL;
	}

	if (journal_entry_err_on(!bch2_checksum_type_valid(c, JSET_CSUM_TYPE(jset)),
			c, version, jset, NULL,
			jset_unknown_csum,
			"%s sector %llu seq %llu: journal entry with unknown csum type %llu",
			ca ? ca->name : c->name,
			sector, le64_to_cpu(jset->seq),
			JSET_CSUM_TYPE(jset)))
		ret = JOURNAL_ENTRY_BAD;

	/* last_seq is ignored when JSET_NO_FLUSH is true */
	if (journal_entry_err_on(!JSET_NO_FLUSH(jset) &&
				 le64_to_cpu(jset->last_seq) > le64_to_cpu(jset->seq),
				 c, version, jset, NULL,
				 jset_last_seq_newer_than_seq,
				 "invalid journal entry: last_seq > seq (%llu > %llu)",
				 le64_to_cpu(jset->last_seq),
				 le64_to_cpu(jset->seq))) {
		jset->last_seq = jset->seq;
		return JOURNAL_ENTRY_BAD;
	}

	ret = jset_validate_entries(c, jset, flags);
fsck_err:
	return ret;
}

static int jset_validate_early(struct bch_fs *c,
			 struct bch_dev *ca,
			 struct jset *jset, u64 sector,
			 unsigned bucket_sectors_left,
			 unsigned sectors_read)
{
	size_t bytes = vstruct_bytes(jset);
	unsigned version;
	enum bkey_invalid_flags flags = BKEY_INVALID_JOURNAL;
	int ret = 0;

	if (le64_to_cpu(jset->magic) != jset_magic(c))
		return JOURNAL_ENTRY_NONE;

	version = le32_to_cpu(jset->version);
	if (journal_entry_err_on(!bch2_version_compatible(version),
			c, version, jset, NULL,
			jset_unsupported_version,
			"%s sector %llu seq %llu: unknown journal entry version %u.%u",
			ca ? ca->name : c->name,
			sector, le64_to_cpu(jset->seq),
			BCH_VERSION_MAJOR(version),
			BCH_VERSION_MINOR(version))) {
		/* don't try to continue: */
		return -EINVAL;
	}

	if (bytes > (sectors_read << 9) &&
	    sectors_read < bucket_sectors_left)
		return JOURNAL_ENTRY_REREAD;

	if (journal_entry_err_on(bytes > bucket_sectors_left << 9,
			c, version, jset, NULL,
			jset_past_bucket_end,
			"%s sector %llu seq %llu: journal entry too big (%zu bytes)",
			ca ? ca->name : c->name,
			sector, le64_to_cpu(jset->seq), bytes))
		le32_add_cpu(&jset->u64s,
			     -((bytes - (bucket_sectors_left << 9)) / 8));
fsck_err:
	return ret;
}

struct journal_read_buf {
	void		*data;
	size_t		size;
};

static int journal_read_buf_realloc(struct journal_read_buf *b,
				    size_t new_size)
{
	void *n;

	/* the bios are sized for this many pages, max: */
	if (new_size > JOURNAL_ENTRY_SIZE_MAX)
		return -BCH_ERR_ENOMEM_journal_read_buf_realloc;

	new_size = roundup_pow_of_two(new_size);
	n = kvpmalloc(new_size, GFP_KERNEL);
	if (!n)
		return -BCH_ERR_ENOMEM_journal_read_buf_realloc;

	kvpfree(b->data, b->size);
	b->data = n;
	b->size = new_size;
	return 0;
}

static int journal_read_bucket(struct bch_dev *ca,
			       struct journal_read_buf *buf,
			       struct journal_list *jlist,
			       unsigned bucket)
{
	struct bch_fs *c = ca->fs;
	struct journal_device *ja = &ca->journal;
	struct jset *j = NULL;
	unsigned sectors, sectors_read = 0;
	u64 offset = bucket_to_sector(ca, ja->buckets[bucket]),
	    end = offset + ca->mi.bucket_size;
	bool saw_bad = false, csum_good;
	int ret = 0;

	pr_debug("reading %u", bucket);

	while (offset < end) {
		if (!sectors_read) {
			struct bio *bio;
			unsigned nr_bvecs;
reread:
			sectors_read = min_t(unsigned,
				end - offset, buf->size >> 9);
			nr_bvecs = buf_pages(buf->data, sectors_read << 9);

			bio = bio_kmalloc(nr_bvecs, GFP_KERNEL);
			bio_init(bio, ca->disk_sb.bdev, bio->bi_inline_vecs, nr_bvecs, REQ_OP_READ);

			bio->bi_iter.bi_sector = offset;
			bch2_bio_map(bio, buf->data, sectors_read << 9);

			ret = submit_bio_wait(bio);
			kfree(bio);

			if (bch2_dev_io_err_on(ret, ca, BCH_MEMBER_ERROR_read,
					       "journal read error: sector %llu",
					       offset) ||
			    bch2_meta_read_fault("journal")) {
				/*
				 * We don't error out of the recovery process
				 * here, since the relevant journal entry may be
				 * found on a different device, and missing or
				 * no journal entries will be handled later
				 */
				return 0;
			}

			j = buf->data;
		}

		ret = jset_validate_early(c, ca, j, offset,
				    end - offset, sectors_read);
		switch (ret) {
		case 0:
			sectors = vstruct_sectors(j, c->block_bits);
			break;
		case JOURNAL_ENTRY_REREAD:
			if (vstruct_bytes(j) > buf->size) {
				ret = journal_read_buf_realloc(buf,
							vstruct_bytes(j));
				if (ret)
					return ret;
			}
			goto reread;
		case JOURNAL_ENTRY_NONE:
			if (!saw_bad)
				return 0;
			/*
			 * On checksum error we don't really trust the size
			 * field of the journal entry we read, so try reading
			 * again at next block boundary:
			 */
			sectors = block_sectors(c);
			goto next_block;
		default:
			return ret;
		}

		/*
		 * This happens sometimes if we don't have discards on -
		 * when we've partially overwritten a bucket with new
		 * journal entries. We don't need the rest of the
		 * bucket:
		 */
		if (le64_to_cpu(j->seq) < ja->bucket_seq[bucket])
			return 0;

		ja->bucket_seq[bucket] = le64_to_cpu(j->seq);

		csum_good = jset_csum_good(c, j);
		if (bch2_dev_io_err_on(!csum_good, ca, BCH_MEMBER_ERROR_checksum,
				       "journal checksum error"))
			saw_bad = true;

		ret = bch2_encrypt(c, JSET_CSUM_TYPE(j), journal_nonce(j),
			     j->encrypted_start,
			     vstruct_end(j) - (void *) j->encrypted_start);
		bch2_fs_fatal_err_on(ret, c,
				"error decrypting journal entry: %i", ret);

		mutex_lock(&jlist->lock);
		ret = journal_entry_add(c, ca, (struct journal_ptr) {
					.csum_good	= csum_good,
					.dev		= ca->dev_idx,
					.bucket		= bucket,
					.bucket_offset	= offset -
						bucket_to_sector(ca, ja->buckets[bucket]),
					.sector		= offset,
					}, jlist, j);
		mutex_unlock(&jlist->lock);

		switch (ret) {
		case JOURNAL_ENTRY_ADD_OK:
			break;
		case JOURNAL_ENTRY_ADD_OUT_OF_RANGE:
			break;
		default:
			return ret;
		}
next_block:
		pr_debug("next");
		offset		+= sectors;
		sectors_read	-= sectors;
		j = ((void *) j) + (sectors << 9);
	}

	return 0;
}

static CLOSURE_CALLBACK(bch2_journal_read_device)
{
	closure_type(ja, struct journal_device, read);
	struct bch_dev *ca = container_of(ja, struct bch_dev, journal);
	struct bch_fs *c = ca->fs;
	struct journal_list *jlist =
		container_of(cl->parent, struct journal_list, cl);
	struct journal_replay *r, **_r;
	struct genradix_iter iter;
	struct journal_read_buf buf = { NULL, 0 };
	unsigned i;
	int ret = 0;

	if (!ja->nr)
		goto out;

	ret = journal_read_buf_realloc(&buf, PAGE_SIZE);
	if (ret)
		goto err;

	pr_debug("%u journal buckets", ja->nr);

	for (i = 0; i < ja->nr; i++) {
		ret = journal_read_bucket(ca, &buf, jlist, i);
		if (ret)
			goto err;
	}

	ja->sectors_free = ca->mi.bucket_size;

	mutex_lock(&jlist->lock);
	genradix_for_each_reverse(&c->journal_entries, iter, _r) {
		r = *_r;

		if (!r)
			continue;

		for (i = 0; i < r->nr_ptrs; i++) {
			if (r->ptrs[i].dev == ca->dev_idx) {
				unsigned wrote = bucket_remainder(ca, r->ptrs[i].sector) +
					vstruct_sectors(&r->j, c->block_bits);

				ja->cur_idx = r->ptrs[i].bucket;
				ja->sectors_free = ca->mi.bucket_size - wrote;
				goto found;
			}
		}
	}
found:
	mutex_unlock(&jlist->lock);

	if (ja->bucket_seq[ja->cur_idx] &&
	    ja->sectors_free == ca->mi.bucket_size) {
#if 0
		/*
		 * Debug code for ZNS support, where we (probably) want to be
		 * correlated where we stopped in the journal to the zone write
		 * points:
		 */
		bch_err(c, "ja->sectors_free == ca->mi.bucket_size");
		bch_err(c, "cur_idx %u/%u", ja->cur_idx, ja->nr);
		for (i = 0; i < 3; i++) {
			unsigned idx = (ja->cur_idx + ja->nr - 1 + i) % ja->nr;

			bch_err(c, "bucket_seq[%u] = %llu", idx, ja->bucket_seq[idx]);
		}
#endif
		ja->sectors_free = 0;
	}

	/*
	 * Set dirty_idx to indicate the entire journal is full and needs to be
	 * reclaimed - journal reclaim will immediately reclaim whatever isn't
	 * pinned when it first runs:
	 */
	ja->discard_idx = ja->dirty_idx_ondisk =
		ja->dirty_idx = (ja->cur_idx + 1) % ja->nr;
out:
	bch_verbose(c, "journal read done on device %s, ret %i", ca->name, ret);
	kvpfree(buf.data, buf.size);
	percpu_ref_put(&ca->io_ref);
	closure_return(cl);
	return;
err:
	mutex_lock(&jlist->lock);
	jlist->ret = ret;
	mutex_unlock(&jlist->lock);
	goto out;
}

void bch2_journal_ptrs_to_text(struct printbuf *out, struct bch_fs *c,
			       struct journal_replay *j)
{
	unsigned i;

	for (i = 0; i < j->nr_ptrs; i++) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, j->ptrs[i].dev);
		u64 offset;

		div64_u64_rem(j->ptrs[i].sector, ca->mi.bucket_size, &offset);

		if (i)
			prt_printf(out, " ");
		prt_printf(out, "%u:%u:%u (sector %llu)",
		       j->ptrs[i].dev,
		       j->ptrs[i].bucket,
		       j->ptrs[i].bucket_offset,
		       j->ptrs[i].sector);
	}
}

int bch2_journal_read(struct bch_fs *c,
		      u64 *last_seq,
		      u64 *blacklist_seq,
		      u64 *start_seq)
{
	struct journal_list jlist;
	struct journal_replay *i, **_i, *prev = NULL;
	struct genradix_iter radix_iter;
	struct printbuf buf = PRINTBUF;
	bool degraded = false, last_write_torn = false;
	u64 seq;
	int ret = 0;

	closure_init_stack(&jlist.cl);
	mutex_init(&jlist.lock);
	jlist.last_seq = 0;
	jlist.ret = 0;

	for_each_member_device(c, ca) {
		if (!c->opts.fsck &&
		    !(bch2_dev_has_data(c, ca) & (1 << BCH_DATA_journal)))
			continue;

		if ((ca->mi.state == BCH_MEMBER_STATE_rw ||
		     ca->mi.state == BCH_MEMBER_STATE_ro) &&
		    percpu_ref_tryget(&ca->io_ref))
			closure_call(&ca->journal.read,
				     bch2_journal_read_device,
				     system_unbound_wq,
				     &jlist.cl);
		else
			degraded = true;
	}

	closure_sync(&jlist.cl);

	if (jlist.ret)
		return jlist.ret;

	*last_seq	= 0;
	*start_seq	= 0;
	*blacklist_seq	= 0;

	/*
	 * Find most recent flush entry, and ignore newer non flush entries -
	 * those entries will be blacklisted:
	 */
	genradix_for_each_reverse(&c->journal_entries, radix_iter, _i) {
		enum bkey_invalid_flags flags = BKEY_INVALID_JOURNAL;

		i = *_i;

		if (!i || i->ignore)
			continue;

		if (!*start_seq)
			*blacklist_seq = *start_seq = le64_to_cpu(i->j.seq) + 1;

		if (JSET_NO_FLUSH(&i->j)) {
			i->ignore = true;
			continue;
		}

		if (!last_write_torn && !i->csum_good) {
			last_write_torn = true;
			i->ignore = true;
			continue;
		}

		if (journal_entry_err_on(le64_to_cpu(i->j.last_seq) > le64_to_cpu(i->j.seq),
					 c, le32_to_cpu(i->j.version), &i->j, NULL,
					 jset_last_seq_newer_than_seq,
					 "invalid journal entry: last_seq > seq (%llu > %llu)",
					 le64_to_cpu(i->j.last_seq),
					 le64_to_cpu(i->j.seq)))
			i->j.last_seq = i->j.seq;

		*last_seq	= le64_to_cpu(i->j.last_seq);
		*blacklist_seq	= le64_to_cpu(i->j.seq) + 1;
		break;
	}

	if (!*start_seq) {
		bch_info(c, "journal read done, but no entries found");
		return 0;
	}

	if (!*last_seq) {
		fsck_err(c, dirty_but_no_journal_entries_post_drop_nonflushes,
			 "journal read done, but no entries found after dropping non-flushes");
		return 0;
	}

	bch_info(c, "journal read done, replaying entries %llu-%llu",
		 *last_seq, *blacklist_seq - 1);

	if (*start_seq != *blacklist_seq)
		bch_info(c, "dropped unflushed entries %llu-%llu",
			 *blacklist_seq, *start_seq - 1);

	/* Drop blacklisted entries and entries older than last_seq: */
	genradix_for_each(&c->journal_entries, radix_iter, _i) {
		i = *_i;

		if (!i || i->ignore)
			continue;

		seq = le64_to_cpu(i->j.seq);
		if (seq < *last_seq) {
			journal_replay_free(c, i);
			continue;
		}

		if (bch2_journal_seq_is_blacklisted(c, seq, true)) {
			fsck_err_on(!JSET_NO_FLUSH(&i->j), c,
				    jset_seq_blacklisted,
				    "found blacklisted journal entry %llu", seq);
			i->ignore = true;
		}
	}

	/* Check for missing entries: */
	seq = *last_seq;
	genradix_for_each(&c->journal_entries, radix_iter, _i) {
		i = *_i;

		if (!i || i->ignore)
			continue;

		BUG_ON(seq > le64_to_cpu(i->j.seq));

		while (seq < le64_to_cpu(i->j.seq)) {
			u64 missing_start, missing_end;
			struct printbuf buf1 = PRINTBUF, buf2 = PRINTBUF;

			while (seq < le64_to_cpu(i->j.seq) &&
			       bch2_journal_seq_is_blacklisted(c, seq, false))
				seq++;

			if (seq == le64_to_cpu(i->j.seq))
				break;

			missing_start = seq;

			while (seq < le64_to_cpu(i->j.seq) &&
			       !bch2_journal_seq_is_blacklisted(c, seq, false))
				seq++;

			if (prev) {
				bch2_journal_ptrs_to_text(&buf1, c, prev);
				prt_printf(&buf1, " size %zu", vstruct_sectors(&prev->j, c->block_bits));
			} else
				prt_printf(&buf1, "(none)");
			bch2_journal_ptrs_to_text(&buf2, c, i);

			missing_end = seq - 1;
			fsck_err(c, journal_entries_missing,
				 "journal entries %llu-%llu missing! (replaying %llu-%llu)\n"
				 "  prev at %s\n"
				 "  next at %s",
				 missing_start, missing_end,
				 *last_seq, *blacklist_seq - 1,
				 buf1.buf, buf2.buf);

			printbuf_exit(&buf1);
			printbuf_exit(&buf2);
		}

		prev = i;
		seq++;
	}

	genradix_for_each(&c->journal_entries, radix_iter, _i) {
		struct bch_replicas_padded replicas = {
			.e.data_type = BCH_DATA_journal,
			.e.nr_required = 1,
		};
		unsigned ptr;

		i = *_i;
		if (!i || i->ignore)
			continue;

		for (ptr = 0; ptr < i->nr_ptrs; ptr++) {
			struct bch_dev *ca = bch_dev_bkey_exists(c, i->ptrs[ptr].dev);

			if (!i->ptrs[ptr].csum_good)
				bch_err_dev_offset(ca, i->ptrs[ptr].sector,
						   "invalid journal checksum, seq %llu%s",
						   le64_to_cpu(i->j.seq),
						   i->csum_good ? " (had good copy on another device)" : "");
		}

		ret = jset_validate(c,
				    bch_dev_bkey_exists(c, i->ptrs[0].dev),
				    &i->j,
				    i->ptrs[0].sector,
				    READ);
		if (ret)
			goto err;

		for (ptr = 0; ptr < i->nr_ptrs; ptr++)
			replicas.e.devs[replicas.e.nr_devs++] = i->ptrs[ptr].dev;

		bch2_replicas_entry_sort(&replicas.e);

		printbuf_reset(&buf);
		bch2_replicas_entry_to_text(&buf, &replicas.e);

		if (!degraded &&
		    !bch2_replicas_marked(c, &replicas.e) &&
		    (le64_to_cpu(i->j.seq) == *last_seq ||
		     fsck_err(c, journal_entry_replicas_not_marked,
			      "superblock not marked as containing replicas for journal entry %llu\n  %s",
			      le64_to_cpu(i->j.seq), buf.buf))) {
			ret = bch2_mark_replicas(c, &replicas.e);
			if (ret)
				goto err;
		}
	}
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

/* journal write: */

static void __journal_write_alloc(struct journal *j,
				  struct journal_buf *w,
				  struct dev_alloc_list *devs_sorted,
				  unsigned sectors,
				  unsigned *replicas,
				  unsigned replicas_want)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_device *ja;
	struct bch_dev *ca;
	unsigned i;

	if (*replicas >= replicas_want)
		return;

	for (i = 0; i < devs_sorted->nr; i++) {
		ca = rcu_dereference(c->devs[devs_sorted->devs[i]]);
		if (!ca)
			continue;

		ja = &ca->journal;

		/*
		 * Check that we can use this device, and aren't already using
		 * it:
		 */
		if (!ca->mi.durability ||
		    ca->mi.state != BCH_MEMBER_STATE_rw ||
		    !ja->nr ||
		    bch2_bkey_has_device_c(bkey_i_to_s_c(&w->key), ca->dev_idx) ||
		    sectors > ja->sectors_free)
			continue;

		bch2_dev_stripe_increment(ca, &j->wp.stripe);

		bch2_bkey_append_ptr(&w->key,
			(struct bch_extent_ptr) {
				  .offset = bucket_to_sector(ca,
					ja->buckets[ja->cur_idx]) +
					ca->mi.bucket_size -
					ja->sectors_free,
				  .dev = ca->dev_idx,
		});

		ja->sectors_free -= sectors;
		ja->bucket_seq[ja->cur_idx] = le64_to_cpu(w->data->seq);

		*replicas += ca->mi.durability;

		if (*replicas >= replicas_want)
			break;
	}
}

/**
 * journal_write_alloc - decide where to write next journal entry
 *
 * @j:		journal object
 * @w:		journal buf (entry to be written)
 *
 * Returns: 0 on success, or -EROFS on failure
 */
static int journal_write_alloc(struct journal *j, struct journal_buf *w)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_devs_mask devs;
	struct journal_device *ja;
	struct bch_dev *ca;
	struct dev_alloc_list devs_sorted;
	unsigned sectors = vstruct_sectors(w->data, c->block_bits);
	unsigned target = c->opts.metadata_target ?:
		c->opts.foreground_target;
	unsigned i, replicas = 0, replicas_want =
		READ_ONCE(c->opts.metadata_replicas);

	rcu_read_lock();
retry:
	devs = target_rw_devs(c, BCH_DATA_journal, target);

	devs_sorted = bch2_dev_alloc_list(c, &j->wp.stripe, &devs);

	__journal_write_alloc(j, w, &devs_sorted,
			      sectors, &replicas, replicas_want);

	if (replicas >= replicas_want)
		goto done;

	for (i = 0; i < devs_sorted.nr; i++) {
		ca = rcu_dereference(c->devs[devs_sorted.devs[i]]);
		if (!ca)
			continue;

		ja = &ca->journal;

		if (sectors > ja->sectors_free &&
		    sectors <= ca->mi.bucket_size &&
		    bch2_journal_dev_buckets_available(j, ja,
					journal_space_discarded)) {
			ja->cur_idx = (ja->cur_idx + 1) % ja->nr;
			ja->sectors_free = ca->mi.bucket_size;

			/*
			 * ja->bucket_seq[ja->cur_idx] must always have
			 * something sensible:
			 */
			ja->bucket_seq[ja->cur_idx] = le64_to_cpu(w->data->seq);
		}
	}

	__journal_write_alloc(j, w, &devs_sorted,
			      sectors, &replicas, replicas_want);

	if (replicas < replicas_want && target) {
		/* Retry from all devices: */
		target = 0;
		goto retry;
	}
done:
	rcu_read_unlock();

	BUG_ON(bkey_val_u64s(&w->key.k) > BCH_REPLICAS_MAX);

	return replicas >= c->opts.metadata_replicas_required ? 0 : -EROFS;
}

static void journal_buf_realloc(struct journal *j, struct journal_buf *buf)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);

	/* we aren't holding j->lock: */
	unsigned new_size = READ_ONCE(j->buf_size_want);
	void *new_buf;

	if (buf->buf_size >= new_size)
		return;

	size_t btree_write_buffer_size = new_size / 64;

	if (bch2_btree_write_buffer_resize(c, btree_write_buffer_size))
		return;

	new_buf = kvpmalloc(new_size, GFP_NOFS|__GFP_NOWARN);
	if (!new_buf)
		return;

	memcpy(new_buf, buf->data, buf->buf_size);

	spin_lock(&j->lock);
	swap(buf->data,		new_buf);
	swap(buf->buf_size,	new_size);
	spin_unlock(&j->lock);

	kvpfree(new_buf, new_size);
}

static inline struct journal_buf *journal_last_unwritten_buf(struct journal *j)
{
	return j->buf + (journal_last_unwritten_seq(j) & JOURNAL_BUF_MASK);
}

static CLOSURE_CALLBACK(journal_write_done)
{
	closure_type(j, struct journal, io);
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *w = journal_last_unwritten_buf(j);
	struct bch_replicas_padded replicas;
	union journal_res_state old, new;
	u64 v, seq;
	int err = 0;

	bch2_time_stats_update(!JSET_NO_FLUSH(w->data)
			       ? j->flush_write_time
			       : j->noflush_write_time, j->write_start_time);

	if (!w->devs_written.nr) {
		bch_err(c, "unable to write journal to sufficient devices");
		err = -EIO;
	} else {
		bch2_devlist_to_replicas(&replicas.e, BCH_DATA_journal,
					 w->devs_written);
		if (bch2_mark_replicas(c, &replicas.e))
			err = -EIO;
	}

	if (err)
		bch2_fatal_error(c);

	spin_lock(&j->lock);
	seq = le64_to_cpu(w->data->seq);

	if (seq >= j->pin.front)
		journal_seq_pin(j, seq)->devs = w->devs_written;

	if (!err) {
		if (!JSET_NO_FLUSH(w->data)) {
			j->flushed_seq_ondisk = seq;
			j->last_seq_ondisk = w->last_seq;

			bch2_do_discards(c);
			closure_wake_up(&c->freelist_wait);

			bch2_reset_alloc_cursors(c);
		}
	} else if (!j->err_seq || seq < j->err_seq)
		j->err_seq	= seq;

	j->seq_ondisk		= seq;

	/*
	 * Updating last_seq_ondisk may let bch2_journal_reclaim_work() discard
	 * more buckets:
	 *
	 * Must come before signaling write completion, for
	 * bch2_fs_journal_stop():
	 */
	if (j->watermark != BCH_WATERMARK_stripe)
		journal_reclaim_kick(&c->journal);

	/* also must come before signalling write completion: */
	closure_debug_destroy(cl);

	v = atomic64_read(&j->reservations.counter);
	do {
		old.v = new.v = v;
		BUG_ON(journal_state_count(new, new.unwritten_idx));

		new.unwritten_idx++;
	} while ((v = atomic64_cmpxchg(&j->reservations.counter,
				       old.v, new.v)) != old.v);

	bch2_journal_reclaim_fast(j);
	bch2_journal_space_available(j);

	track_event_change(&c->times[BCH_TIME_blocked_journal_max_in_flight],
			   &j->max_in_flight_start, false);

	closure_wake_up(&w->wait);
	journal_wake(j);

	if (!journal_state_count(new, new.unwritten_idx) &&
	    journal_last_unwritten_seq(j) <= journal_cur_seq(j)) {
		spin_unlock(&j->lock);
		closure_call(&j->io, bch2_journal_write, c->io_complete_wq, NULL);
	} else if (journal_last_unwritten_seq(j) == journal_cur_seq(j) &&
		   new.cur_entry_offset < JOURNAL_ENTRY_CLOSED_VAL) {
		struct journal_buf *buf = journal_cur_buf(j);
		long delta = buf->expires - jiffies;

		/*
		 * We don't close a journal entry to write it while there's
		 * previous entries still in flight - the current journal entry
		 * might want to be written now:
		 */

		spin_unlock(&j->lock);
		mod_delayed_work(c->io_complete_wq, &j->write_work, max(0L, delta));
	} else {
		spin_unlock(&j->lock);
	}
}

static void journal_write_endio(struct bio *bio)
{
	struct bch_dev *ca = bio->bi_private;
	struct journal *j = &ca->fs->journal;
	struct journal_buf *w = journal_last_unwritten_buf(j);
	unsigned long flags;

	if (bch2_dev_io_err_on(bio->bi_status, ca, BCH_MEMBER_ERROR_write,
			       "error writing journal entry %llu: %s",
			       le64_to_cpu(w->data->seq),
			       bch2_blk_status_to_str(bio->bi_status)) ||
	    bch2_meta_write_fault("journal")) {
		spin_lock_irqsave(&j->err_lock, flags);
		bch2_dev_list_drop_dev(&w->devs_written, ca->dev_idx);
		spin_unlock_irqrestore(&j->err_lock, flags);
	}

	closure_put(&j->io);
	percpu_ref_put(&ca->io_ref);
}

static CLOSURE_CALLBACK(do_journal_write)
{
	closure_type(j, struct journal, io);
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	struct journal_buf *w = journal_last_unwritten_buf(j);
	struct bio *bio;
	unsigned sectors = vstruct_sectors(w->data, c->block_bits);

	extent_for_each_ptr(bkey_i_to_s_extent(&w->key), ptr) {
		ca = bch_dev_bkey_exists(c, ptr->dev);
		if (!percpu_ref_tryget(&ca->io_ref)) {
			/* XXX: fix this */
			bch_err(c, "missing device for journal write\n");
			continue;
		}

		this_cpu_add(ca->io_done->sectors[WRITE][BCH_DATA_journal],
			     sectors);

		bio = ca->journal.bio;
		bio_reset(bio, ca->disk_sb.bdev, REQ_OP_WRITE|REQ_SYNC|REQ_META);
		bio->bi_iter.bi_sector	= ptr->offset;
		bio->bi_end_io		= journal_write_endio;
		bio->bi_private		= ca;

		BUG_ON(bio->bi_iter.bi_sector == ca->prev_journal_sector);
		ca->prev_journal_sector = bio->bi_iter.bi_sector;

		if (!JSET_NO_FLUSH(w->data))
			bio->bi_opf    |= REQ_FUA;
		if (!JSET_NO_FLUSH(w->data) && !w->separate_flush)
			bio->bi_opf    |= REQ_PREFLUSH;

		bch2_bio_map(bio, w->data, sectors << 9);

		trace_and_count(c, journal_write, bio);
		closure_bio_submit(bio, cl);

		ca->journal.bucket_seq[ca->journal.cur_idx] =
			le64_to_cpu(w->data->seq);
	}

	continue_at(cl, journal_write_done, c->io_complete_wq);
}

static int bch2_journal_write_prep(struct journal *j, struct journal_buf *w)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct jset_entry *start, *end;
	struct jset *jset = w->data;
	struct journal_keys_to_wb wb = { NULL };
	unsigned sectors, bytes, u64s;
	unsigned long btree_roots_have = 0;
	bool validate_before_checksum = false;
	u64 seq = le64_to_cpu(jset->seq);
	int ret;

	/*
	 * Simple compaction, dropping empty jset_entries (from journal
	 * reservations that weren't fully used) and merging jset_entries that
	 * can be.
	 *
	 * If we wanted to be really fancy here, we could sort all the keys in
	 * the jset and drop keys that were overwritten - probably not worth it:
	 */
	vstruct_for_each(jset, i) {
		unsigned u64s = le16_to_cpu(i->u64s);

		/* Empty entry: */
		if (!u64s)
			continue;

		/*
		 * New btree roots are set by journalling them; when the journal
		 * entry gets written we have to propagate them to
		 * c->btree_roots
		 *
		 * But, every journal entry we write has to contain all the
		 * btree roots (at least for now); so after we copy btree roots
		 * to c->btree_roots we have to get any missing btree roots and
		 * add them to this journal entry:
		 */
		switch (i->type) {
		case BCH_JSET_ENTRY_btree_root:
			bch2_journal_entry_to_btree_root(c, i);
			__set_bit(i->btree_id, &btree_roots_have);
			break;
		case BCH_JSET_ENTRY_write_buffer_keys:
			EBUG_ON(!w->need_flush_to_write_buffer);

			if (!wb.wb)
				bch2_journal_keys_to_write_buffer_start(c, &wb, seq);

			struct bkey_i *k;
			jset_entry_for_each_key(i, k) {
				ret = bch2_journal_key_to_wb(c, &wb, i->btree_id, k);
				if (ret) {
					bch2_fs_fatal_error(c, "-ENOMEM flushing journal keys to btree write buffer");
					bch2_journal_keys_to_write_buffer_end(c, &wb);
					return ret;
				}
			}
			i->type = BCH_JSET_ENTRY_btree_keys;
			break;
		}
	}

	if (wb.wb)
		bch2_journal_keys_to_write_buffer_end(c, &wb);
	w->need_flush_to_write_buffer = false;

	start = end = vstruct_last(jset);

	end	= bch2_btree_roots_to_journal_entries(c, end, btree_roots_have);

	bch2_journal_super_entries_add_common(c, &end, seq);
	u64s	= (u64 *) end - (u64 *) start;
	BUG_ON(u64s > j->entry_u64s_reserved);

	le32_add_cpu(&jset->u64s, u64s);

	sectors = vstruct_sectors(jset, c->block_bits);
	bytes	= vstruct_bytes(jset);

	if (sectors > w->sectors) {
		bch2_fs_fatal_error(c, "aieeee! journal write overran available space, %zu > %u (extra %u reserved %u/%u)",
				    vstruct_bytes(jset), w->sectors << 9,
				    u64s, w->u64s_reserved, j->entry_u64s_reserved);
		return -EINVAL;
	}

	jset->magic		= cpu_to_le64(jset_magic(c));
	jset->version		= cpu_to_le32(c->sb.version);

	SET_JSET_BIG_ENDIAN(jset, CPU_BIG_ENDIAN);
	SET_JSET_CSUM_TYPE(jset, bch2_meta_checksum_type(c));

	if (!JSET_NO_FLUSH(jset) && journal_entry_empty(jset))
		j->last_empty_seq = seq;

	if (bch2_csum_type_is_encryption(JSET_CSUM_TYPE(jset)))
		validate_before_checksum = true;

	if (le32_to_cpu(jset->version) < bcachefs_metadata_version_current)
		validate_before_checksum = true;

	if (validate_before_checksum &&
	    (ret = jset_validate(c, NULL, jset, 0, WRITE)))
		return ret;

	ret = bch2_encrypt(c, JSET_CSUM_TYPE(jset), journal_nonce(jset),
		    jset->encrypted_start,
		    vstruct_end(jset) - (void *) jset->encrypted_start);
	if (bch2_fs_fatal_err_on(ret, c,
			"error decrypting journal entry: %i", ret))
		return ret;

	jset->csum = csum_vstruct(c, JSET_CSUM_TYPE(jset),
				  journal_nonce(jset), jset);

	if (!validate_before_checksum &&
	    (ret = jset_validate(c, NULL, jset, 0, WRITE)))
		return ret;

	memset((void *) jset + bytes, 0, (sectors << 9) - bytes);
	return 0;
}

static int bch2_journal_write_pick_flush(struct journal *j, struct journal_buf *w)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	int error = bch2_journal_error(j);

	/*
	 * If the journal is in an error state - we did an emergency shutdown -
	 * we prefer to continue doing journal writes. We just mark them as
	 * noflush so they'll never be used, but they'll still be visible by the
	 * list_journal tool - this helps in debugging.
	 *
	 * There's a caveat: the first journal write after marking the
	 * superblock dirty must always be a flush write, because on startup
	 * from a clean shutdown we didn't necessarily read the journal and the
	 * new journal write might overwrite whatever was in the journal
	 * previously - we can't leave the journal without any flush writes in
	 * it.
	 *
	 * So if we're in an error state, and we're still starting up, we don't
	 * write anything at all.
	 */
	if (error && test_bit(JOURNAL_NEED_FLUSH_WRITE, &j->flags))
		return -EIO;

	if (error ||
	    w->noflush ||
	    (!w->must_flush &&
	     (jiffies - j->last_flush_write) < msecs_to_jiffies(c->opts.journal_flush_delay) &&
	     test_bit(JOURNAL_MAY_SKIP_FLUSH, &j->flags))) {
		w->noflush = true;
		SET_JSET_NO_FLUSH(w->data, true);
		w->data->last_seq	= 0;
		w->last_seq		= 0;

		j->nr_noflush_writes++;
	} else {
		j->last_flush_write = jiffies;
		j->nr_flush_writes++;
		clear_bit(JOURNAL_NEED_FLUSH_WRITE, &j->flags);
	}

	return 0;
}

CLOSURE_CALLBACK(bch2_journal_write)
{
	closure_type(j, struct journal, io);
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *w = journal_last_unwritten_buf(j);
	struct bch_replicas_padded replicas;
	struct bio *bio;
	struct printbuf journal_debug_buf = PRINTBUF;
	unsigned nr_rw_members = 0;
	int ret;

	BUG_ON(BCH_SB_CLEAN(c->disk_sb.sb));

	j->write_start_time = local_clock();

	spin_lock(&j->lock);
	ret = bch2_journal_write_pick_flush(j, w);
	spin_unlock(&j->lock);
	if (ret)
		goto err;

	mutex_lock(&j->buf_lock);
	journal_buf_realloc(j, w);

	ret = bch2_journal_write_prep(j, w);
	mutex_unlock(&j->buf_lock);
	if (ret)
		goto err;

	j->entry_bytes_written += vstruct_bytes(w->data);

	while (1) {
		spin_lock(&j->lock);
		ret = journal_write_alloc(j, w);
		if (!ret || !j->can_discard)
			break;

		spin_unlock(&j->lock);
		bch2_journal_do_discards(j);
	}

	if (ret) {
		__bch2_journal_debug_to_text(&journal_debug_buf, j);
		spin_unlock(&j->lock);
		bch_err(c, "Unable to allocate journal write:\n%s",
			journal_debug_buf.buf);
		printbuf_exit(&journal_debug_buf);
		goto err;
	}

	/*
	 * write is allocated, no longer need to account for it in
	 * bch2_journal_space_available():
	 */
	w->sectors = 0;

	/*
	 * journal entry has been compacted and allocated, recalculate space
	 * available:
	 */
	bch2_journal_space_available(j);
	spin_unlock(&j->lock);

	w->devs_written = bch2_bkey_devs(bkey_i_to_s_c(&w->key));

	if (c->opts.nochanges)
		goto no_io;

	for_each_rw_member(c, ca)
		nr_rw_members++;

	if (nr_rw_members > 1)
		w->separate_flush = true;

	/*
	 * Mark journal replicas before we submit the write to guarantee
	 * recovery will find the journal entries after a crash.
	 */
	bch2_devlist_to_replicas(&replicas.e, BCH_DATA_journal,
				 w->devs_written);
	ret = bch2_mark_replicas(c, &replicas.e);
	if (ret)
		goto err;

	if (!JSET_NO_FLUSH(w->data) && w->separate_flush) {
		for_each_rw_member(c, ca) {
			percpu_ref_get(&ca->io_ref);

			bio = ca->journal.bio;
			bio_reset(bio, ca->disk_sb.bdev, REQ_OP_FLUSH);
			bio->bi_end_io		= journal_write_endio;
			bio->bi_private		= ca;
			closure_bio_submit(bio, cl);
		}
	}

	continue_at(cl, do_journal_write, c->io_complete_wq);
	return;
no_io:
	continue_at(cl, journal_write_done, c->io_complete_wq);
	return;
err:
	bch2_fatal_error(c);
	continue_at(cl, journal_write_done, c->io_complete_wq);
}
