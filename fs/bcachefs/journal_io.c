// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "buckets.h"
#include "checksum.h"
#include "error.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "replicas.h"
#include "trace.h"

struct journal_list {
	struct closure		cl;
	struct mutex		lock;
	struct list_head	*head;
	int			ret;
};

#define JOURNAL_ENTRY_ADD_OK		0
#define JOURNAL_ENTRY_ADD_OUT_OF_RANGE	5

/*
 * Given a journal entry we just read, add it to the list of journal entries to
 * be replayed:
 */
static int journal_entry_add(struct bch_fs *c, struct bch_dev *ca,
			     struct journal_list *jlist, struct jset *j)
{
	struct journal_replay *i, *pos;
	struct list_head *where;
	size_t bytes = vstruct_bytes(j);
	__le64 last_seq;
	int ret;

	last_seq = !list_empty(jlist->head)
		? list_last_entry(jlist->head, struct journal_replay,
				  list)->j.last_seq
		: 0;

	/* Is this entry older than the range we need? */
	if (le64_to_cpu(j->seq) < le64_to_cpu(last_seq)) {
		ret = JOURNAL_ENTRY_ADD_OUT_OF_RANGE;
		goto out;
	}

	/* Drop entries we don't need anymore */
	list_for_each_entry_safe(i, pos, jlist->head, list) {
		if (le64_to_cpu(i->j.seq) >= le64_to_cpu(j->last_seq))
			break;
		list_del(&i->list);
		kvpfree(i, offsetof(struct journal_replay, j) +
			vstruct_bytes(&i->j));
	}

	list_for_each_entry_reverse(i, jlist->head, list) {
		/* Duplicate? */
		if (le64_to_cpu(j->seq) == le64_to_cpu(i->j.seq)) {
			fsck_err_on(bytes != vstruct_bytes(&i->j) ||
				    memcmp(j, &i->j, bytes), c,
				    "found duplicate but non identical journal entries (seq %llu)",
				    le64_to_cpu(j->seq));
			goto found;
		}

		if (le64_to_cpu(j->seq) > le64_to_cpu(i->j.seq)) {
			where = &i->list;
			goto add;
		}
	}

	where = jlist->head;
add:
	i = kvpmalloc(offsetof(struct journal_replay, j) + bytes, GFP_KERNEL);
	if (!i) {
		ret = -ENOMEM;
		goto out;
	}

	list_add(&i->list, where);
	i->devs.nr = 0;
	unsafe_memcpy(&i->j, j, bytes, "embedded variable length struct");
found:
	if (!bch2_dev_list_has_dev(i->devs, ca->dev_idx))
		bch2_dev_list_add_dev(&i->devs, ca->dev_idx);
	else
		fsck_err_on(1, c, "duplicate journal entries on same device");
	ret = JOURNAL_ENTRY_ADD_OK;
out:
fsck_err:
	return ret;
}

static struct nonce journal_nonce(const struct jset *jset)
{
	return (struct nonce) {{
		[0] = 0,
		[1] = ((__le32 *) &jset->seq)[0],
		[2] = ((__le32 *) &jset->seq)[1],
		[3] = BCH_NONCE_JOURNAL,
	}};
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

#define journal_entry_err(c, msg, ...)					\
({									\
	switch (write) {						\
	case READ:							\
		mustfix_fsck_err(c, msg, ##__VA_ARGS__);		\
		break;							\
	case WRITE:							\
		bch_err(c, "corrupt metadata before write:\n"		\
			msg, ##__VA_ARGS__);				\
		if (bch2_fs_inconsistent(c)) {				\
			ret = BCH_FSCK_ERRORS_NOT_FIXED;		\
			goto fsck_err;					\
		}							\
		break;							\
	}								\
	true;								\
})

#define journal_entry_err_on(cond, c, msg, ...)				\
	((cond) ? journal_entry_err(c, msg, ##__VA_ARGS__) : false)

static int journal_validate_key(struct bch_fs *c, struct jset *jset,
				struct jset_entry *entry,
				struct bkey_i *k, enum btree_node_type key_type,
				const char *type, int write)
{
	void *next = vstruct_next(entry);
	const char *invalid;
	unsigned version = le32_to_cpu(jset->version);
	int ret = 0;

	if (journal_entry_err_on(!k->k.u64s, c,
			"invalid %s in journal: k->u64s 0", type)) {
		entry->u64s = cpu_to_le16((u64 *) k - entry->_data);
		journal_entry_null_range(vstruct_next(entry), next);
		return 0;
	}

	if (journal_entry_err_on((void *) bkey_next(k) >
				(void *) vstruct_next(entry), c,
			"invalid %s in journal: extends past end of journal entry",
			type)) {
		entry->u64s = cpu_to_le16((u64 *) k - entry->_data);
		journal_entry_null_range(vstruct_next(entry), next);
		return 0;
	}

	if (journal_entry_err_on(k->k.format != KEY_FORMAT_CURRENT, c,
			"invalid %s in journal: bad format %u",
			type, k->k.format)) {
		le16_add_cpu(&entry->u64s, -k->k.u64s);
		memmove(k, bkey_next(k), next - (void *) bkey_next(k));
		journal_entry_null_range(vstruct_next(entry), next);
		return 0;
	}

	if (JSET_BIG_ENDIAN(jset) != CPU_BIG_ENDIAN)
		bch2_bkey_swab(NULL, bkey_to_packed(k));

	if (!write &&
	    version < bcachefs_metadata_version_bkey_renumber)
		bch2_bkey_renumber(key_type, bkey_to_packed(k), write);

	invalid = bch2_bkey_invalid(c, bkey_i_to_s_c(k), key_type);
	if (invalid) {
		char buf[160];

		bch2_bkey_val_to_text(&PBUF(buf), c, bkey_i_to_s_c(k));
		mustfix_fsck_err(c, "invalid %s in journal: %s\n%s",
				 type, invalid, buf);

		le16_add_cpu(&entry->u64s, -k->k.u64s);
		memmove(k, bkey_next(k), next - (void *) bkey_next(k));
		journal_entry_null_range(vstruct_next(entry), next);
		return 0;
	}

	if (write &&
	    version < bcachefs_metadata_version_bkey_renumber)
		bch2_bkey_renumber(key_type, bkey_to_packed(k), write);
fsck_err:
	return ret;
}

static int journal_entry_validate_btree_keys(struct bch_fs *c,
					     struct jset *jset,
					     struct jset_entry *entry,
					     int write)
{
	struct bkey_i *k;

	vstruct_for_each(entry, k) {
		int ret = journal_validate_key(c, jset, entry, k,
				__btree_node_type(entry->level,
						  entry->btree_id),
				"key", write);
		if (ret)
			return ret;
	}

	return 0;
}

static int journal_entry_validate_btree_root(struct bch_fs *c,
					     struct jset *jset,
					     struct jset_entry *entry,
					     int write)
{
	struct bkey_i *k = entry->start;
	int ret = 0;

	if (journal_entry_err_on(!entry->u64s ||
				 le16_to_cpu(entry->u64s) != k->k.u64s, c,
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

	return journal_validate_key(c, jset, entry, k, BKEY_TYPE_BTREE,
				    "btree root", write);
fsck_err:
	return ret;
}

static int journal_entry_validate_prio_ptrs(struct bch_fs *c,
					    struct jset *jset,
					    struct jset_entry *entry,
					    int write)
{
	/* obsolete, don't care: */
	return 0;
}

static int journal_entry_validate_blacklist(struct bch_fs *c,
					    struct jset *jset,
					    struct jset_entry *entry,
					    int write)
{
	int ret = 0;

	if (journal_entry_err_on(le16_to_cpu(entry->u64s) != 1, c,
		"invalid journal seq blacklist entry: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
	}
fsck_err:
	return ret;
}

static int journal_entry_validate_blacklist_v2(struct bch_fs *c,
					       struct jset *jset,
					       struct jset_entry *entry,
					       int write)
{
	struct jset_entry_blacklist_v2 *bl_entry;
	int ret = 0;

	if (journal_entry_err_on(le16_to_cpu(entry->u64s) != 2, c,
		"invalid journal seq blacklist entry: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		goto out;
	}

	bl_entry = container_of(entry, struct jset_entry_blacklist_v2, entry);

	if (journal_entry_err_on(le64_to_cpu(bl_entry->start) >
				 le64_to_cpu(bl_entry->end), c,
		"invalid journal seq blacklist entry: start > end")) {
		journal_entry_null_range(entry, vstruct_next(entry));
	}
out:
fsck_err:
	return ret;
}

static int journal_entry_validate_usage(struct bch_fs *c,
					struct jset *jset,
					struct jset_entry *entry,
					int write)
{
	struct jset_entry_usage *u =
		container_of(entry, struct jset_entry_usage, entry);
	unsigned bytes = jset_u64s(le16_to_cpu(entry->u64s)) * sizeof(u64);
	int ret = 0;

	if (journal_entry_err_on(bytes < sizeof(*u),
				 c,
				 "invalid journal entry usage: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

fsck_err:
	return ret;
}

static int journal_entry_validate_data_usage(struct bch_fs *c,
					struct jset *jset,
					struct jset_entry *entry,
					int write)
{
	struct jset_entry_data_usage *u =
		container_of(entry, struct jset_entry_data_usage, entry);
	unsigned bytes = jset_u64s(le16_to_cpu(entry->u64s)) * sizeof(u64);
	int ret = 0;

	if (journal_entry_err_on(bytes < sizeof(*u) ||
				 bytes < sizeof(*u) + u->r.nr_devs,
				 c,
				 "invalid journal entry usage: bad size")) {
		journal_entry_null_range(entry, vstruct_next(entry));
		return ret;
	}

fsck_err:
	return ret;
}

struct jset_entry_ops {
	int (*validate)(struct bch_fs *, struct jset *,
			struct jset_entry *, int);
};

static const struct jset_entry_ops bch2_jset_entry_ops[] = {
#define x(f, nr)						\
	[BCH_JSET_ENTRY_##f]	= (struct jset_entry_ops) {	\
		.validate	= journal_entry_validate_##f,	\
	},
	BCH_JSET_ENTRY_TYPES()
#undef x
};

static int journal_entry_validate(struct bch_fs *c, struct jset *jset,
				  struct jset_entry *entry, int write)
{
	return entry->type < BCH_JSET_ENTRY_NR
		? bch2_jset_entry_ops[entry->type].validate(c, jset,
							    entry, write)
		: 0;
}

static int jset_validate_entries(struct bch_fs *c, struct jset *jset,
				 int write)
{
	struct jset_entry *entry;
	int ret = 0;

	vstruct_for_each(jset, entry) {
		if (journal_entry_err_on(vstruct_next(entry) >
					 vstruct_last(jset), c,
				"journal entry extends past end of jset")) {
			jset->u64s = cpu_to_le32((u64 *) entry - jset->_data);
			break;
		}

		ret = journal_entry_validate(c, jset, entry, write);
		if (ret)
			break;
	}
fsck_err:
	return ret;
}

static int jset_validate(struct bch_fs *c,
			 struct jset *jset, u64 sector,
			 unsigned bucket_sectors_left,
			 unsigned sectors_read,
			 int write)
{
	size_t bytes = vstruct_bytes(jset);
	struct bch_csum csum;
	unsigned version;
	int ret = 0;

	if (le64_to_cpu(jset->magic) != jset_magic(c))
		return JOURNAL_ENTRY_NONE;

	version = le32_to_cpu(jset->version);
	if ((version != BCH_JSET_VERSION_OLD &&
	     version < bcachefs_metadata_version_min) ||
	    version >= bcachefs_metadata_version_max) {
		bch_err(c, "unknown journal entry version %u", jset->version);
		return BCH_FSCK_UNKNOWN_VERSION;
	}

	if (journal_entry_err_on(bytes > bucket_sectors_left << 9, c,
				 "journal entry too big (%zu bytes), sector %lluu",
				 bytes, sector)) {
		/* XXX: note we might have missing journal entries */
		return JOURNAL_ENTRY_BAD;
	}

	if (bytes > sectors_read << 9)
		return JOURNAL_ENTRY_REREAD;

	if (fsck_err_on(!bch2_checksum_type_valid(c, JSET_CSUM_TYPE(jset)), c,
			"journal entry with unknown csum type %llu sector %lluu",
			JSET_CSUM_TYPE(jset), sector))
		return JOURNAL_ENTRY_BAD;

	csum = csum_vstruct(c, JSET_CSUM_TYPE(jset), journal_nonce(jset), jset);
	if (journal_entry_err_on(bch2_crc_cmp(csum, jset->csum), c,
				 "journal checksum bad, sector %llu", sector)) {
		/* XXX: retry IO, when we start retrying checksum errors */
		/* XXX: note we might have missing journal entries */
		return JOURNAL_ENTRY_BAD;
	}

	bch2_encrypt(c, JSET_CSUM_TYPE(jset), journal_nonce(jset),
		     jset->encrypted_start,
		     vstruct_end(jset) - (void *) jset->encrypted_start);

	if (journal_entry_err_on(le64_to_cpu(jset->last_seq) > le64_to_cpu(jset->seq), c,
				 "invalid journal entry: last_seq > seq"))
		jset->last_seq = jset->seq;

	return 0;
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
		return -ENOMEM;

	new_size = roundup_pow_of_two(new_size);
	n = kvpmalloc(new_size, GFP_KERNEL);
	if (!n)
		return -ENOMEM;

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
	bool saw_bad = false;
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

			bio->bi_iter.bi_sector	= offset;
			bio->bi_iter.bi_size	= sectors_read << 9;
			bch2_bio_map(bio, buf->data);

			ret = submit_bio_wait(bio);
			kfree(bio);

			if (bch2_dev_io_err_on(ret, ca,
					       "journal read from sector %llu",
					       offset) ||
			    bch2_meta_read_fault("journal"))
				return -EIO;

			j = buf->data;
		}

		ret = jset_validate(c, j, offset,
				    end - offset, sectors_read,
				    READ);
		switch (ret) {
		case BCH_FSCK_OK:
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
			sectors = c->opts.block_size;
			goto next_block;
		case JOURNAL_ENTRY_BAD:
			saw_bad = true;
			sectors = c->opts.block_size;
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

		mutex_lock(&jlist->lock);
		ret = journal_entry_add(c, ca, jlist, j);
		mutex_unlock(&jlist->lock);

		switch (ret) {
		case JOURNAL_ENTRY_ADD_OK:
			break;
		case JOURNAL_ENTRY_ADD_OUT_OF_RANGE:
			break;
		default:
			return ret;
		}

		sectors = vstruct_sectors(j, c->block_bits);
next_block:
		pr_debug("next");
		offset		+= sectors;
		sectors_read	-= sectors;
		j = ((void *) j) + (sectors << 9);
	}

	return 0;
}

static void bch2_journal_read_device(struct closure *cl)
{
	struct journal_device *ja =
		container_of(cl, struct journal_device, read);
	struct bch_dev *ca = container_of(ja, struct bch_dev, journal);
	struct journal_list *jlist =
		container_of(cl->parent, struct journal_list, cl);
	struct journal_read_buf buf = { NULL, 0 };
	u64 min_seq = U64_MAX;
	unsigned i;
	int ret;

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

	/* Find the journal bucket with the highest sequence number: */
	for (i = 0; i < ja->nr; i++) {
		if (ja->bucket_seq[i] > ja->bucket_seq[ja->cur_idx])
			ja->cur_idx = i;

		min_seq = min(ja->bucket_seq[i], min_seq);
	}

	/*
	 * If there's duplicate journal entries in multiple buckets (which
	 * definitely isn't supposed to happen, but...) - make sure to start
	 * cur_idx at the last of those buckets, so we don't deadlock trying to
	 * allocate
	 */
	while (ja->bucket_seq[ja->cur_idx] > min_seq &&
	       ja->bucket_seq[ja->cur_idx] >
	       ja->bucket_seq[(ja->cur_idx + 1) % ja->nr])
		ja->cur_idx = (ja->cur_idx + 1) % ja->nr;

	ja->sectors_free = 0;

	/*
	 * Set dirty_idx to indicate the entire journal is full and needs to be
	 * reclaimed - journal reclaim will immediately reclaim whatever isn't
	 * pinned when it first runs:
	 */
	ja->discard_idx = ja->dirty_idx_ondisk =
		ja->dirty_idx = (ja->cur_idx + 1) % ja->nr;
out:
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

void bch2_journal_entries_free(struct list_head *list)
{

	while (!list_empty(list)) {
		struct journal_replay *i =
			list_first_entry(list, struct journal_replay, list);
		list_del(&i->list);
		kvpfree(i, offsetof(struct journal_replay, j) +
			vstruct_bytes(&i->j));
	}
}

int bch2_journal_set_seq(struct bch_fs *c, u64 last_seq, u64 end_seq)
{
	struct journal *j = &c->journal;
	struct journal_entry_pin_list *p;
	u64 seq, nr = end_seq - last_seq + 1;

	if (nr > j->pin.size) {
		free_fifo(&j->pin);
		init_fifo(&j->pin, roundup_pow_of_two(nr), GFP_KERNEL);
		if (!j->pin.data) {
			bch_err(c, "error reallocating journal fifo (%llu open entries)", nr);
			return -ENOMEM;
		}
	}

	atomic64_set(&j->seq, end_seq);
	j->last_seq_ondisk = last_seq;

	j->pin.front	= last_seq;
	j->pin.back	= end_seq + 1;

	fifo_for_each_entry_ptr(p, &j->pin, seq) {
		INIT_LIST_HEAD(&p->list);
		INIT_LIST_HEAD(&p->flushed);
		atomic_set(&p->count, 0);
		p->devs.nr = 0;
	}

	return 0;
}

int bch2_journal_read(struct bch_fs *c, struct list_head *list)
{
	struct journal *j = &c->journal;
	struct journal_list jlist;
	struct journal_replay *i;
	struct journal_entry_pin_list *p;
	struct bch_dev *ca;
	u64 cur_seq, end_seq;
	unsigned iter;
	size_t keys = 0, entries = 0;
	bool degraded = false;
	int ret = 0;

	closure_init_stack(&jlist.cl);
	mutex_init(&jlist.lock);
	jlist.head = list;
	jlist.ret = 0;

	for_each_member_device(ca, c, iter) {
		if (!test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) &&
		    !(bch2_dev_has_data(c, ca) & (1 << BCH_DATA_JOURNAL)))
			continue;

		if ((ca->mi.state == BCH_MEMBER_STATE_RW ||
		     ca->mi.state == BCH_MEMBER_STATE_RO) &&
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

	if (list_empty(list)){
		bch_err(c, "no journal entries found");
		return BCH_FSCK_REPAIR_IMPOSSIBLE;
	}

	list_for_each_entry(i, list, list) {
		struct bch_replicas_padded replicas;
		char buf[80];

		bch2_devlist_to_replicas(&replicas.e, BCH_DATA_JOURNAL, i->devs);

		ret = jset_validate_entries(c, &i->j, READ);
		if (ret)
			goto fsck_err;

		/*
		 * If we're mounting in degraded mode - if we didn't read all
		 * the devices - this is wrong:
		 */

		if (!degraded &&
		    (test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) ||
		     fsck_err_on(!bch2_replicas_marked(c, &replicas.e, false), c,
				 "superblock not marked as containing replicas %s",
				 (bch2_replicas_entry_to_text(&PBUF(buf),
							      &replicas.e), buf)))) {
			ret = bch2_mark_replicas(c, &replicas.e);
			if (ret)
				return ret;
		}
	}

	i = list_last_entry(list, struct journal_replay, list);

	ret = bch2_journal_set_seq(c,
				   le64_to_cpu(i->j.last_seq),
				   le64_to_cpu(i->j.seq));
	if (ret)
		return ret;

	mutex_lock(&j->blacklist_lock);

	list_for_each_entry(i, list, list) {
		p = journal_seq_pin(j, le64_to_cpu(i->j.seq));

		atomic_set(&p->count, 1);
		p->devs = i->devs;

		if (bch2_journal_seq_blacklist_read(j, i)) {
			mutex_unlock(&j->blacklist_lock);
			return -ENOMEM;
		}
	}

	mutex_unlock(&j->blacklist_lock);

	cur_seq = journal_last_seq(j);
	end_seq = le64_to_cpu(list_last_entry(list,
				struct journal_replay, list)->j.seq);

	list_for_each_entry(i, list, list) {
		struct jset_entry *entry;
		struct bkey_i *k, *_n;
		bool blacklisted;

		mutex_lock(&j->blacklist_lock);
		while (cur_seq < le64_to_cpu(i->j.seq) &&
		       bch2_journal_seq_blacklist_find(j, cur_seq))
			cur_seq++;

		blacklisted = bch2_journal_seq_blacklist_find(j,
							 le64_to_cpu(i->j.seq));
		mutex_unlock(&j->blacklist_lock);

		fsck_err_on(blacklisted, c,
			    "found blacklisted journal entry %llu",
			    le64_to_cpu(i->j.seq));

		fsck_err_on(le64_to_cpu(i->j.seq) != cur_seq, c,
			"journal entries %llu-%llu missing! (replaying %llu-%llu)",
			cur_seq, le64_to_cpu(i->j.seq) - 1,
			journal_last_seq(j), end_seq);

		cur_seq = le64_to_cpu(i->j.seq) + 1;

		for_each_jset_key(k, _n, entry, &i->j)
			keys++;
		entries++;
	}

	bch_info(c, "journal read done, %zu keys in %zu entries, seq %llu",
		 keys, entries, journal_cur_seq(j));
fsck_err:
	return ret;
}

/* journal replay: */

int bch2_journal_replay(struct bch_fs *c, struct list_head *list)
{
	struct journal *j = &c->journal;
	struct bkey_i *k, *_n;
	struct jset_entry *entry;
	struct journal_replay *i, *n;
	int ret = 0;

	list_for_each_entry_safe(i, n, list, list) {
		j->replay_journal_seq = le64_to_cpu(i->j.seq);

		for_each_jset_key(k, _n, entry, &i->j) {

			if (entry->btree_id == BTREE_ID_ALLOC) {
				/*
				 * allocation code handles replay for
				 * BTREE_ID_ALLOC keys:
				 */
				ret = bch2_alloc_replay_key(c, k);
			} else {
				/*
				 * We might cause compressed extents to be
				 * split, so we need to pass in a
				 * disk_reservation:
				 */
				struct disk_reservation disk_res =
					bch2_disk_reservation_init(c, 0);

				ret = bch2_btree_insert(c, entry->btree_id, k,
						&disk_res, NULL,
						BTREE_INSERT_NOFAIL|
						BTREE_INSERT_JOURNAL_REPLAY|
						BTREE_INSERT_NOMARK);
			}

			if (ret) {
				bch_err(c, "journal replay: error %d while replaying key",
					ret);
				goto err;
			}

			cond_resched();
		}

		bch2_journal_pin_put(j, j->replay_journal_seq);
	}

	j->replay_journal_seq = 0;

	bch2_journal_set_replay_done(j);
	bch2_journal_flush_all_pins(j);
	ret = bch2_journal_error(j);
err:
	bch2_journal_entries_free(list);
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
		    ca->mi.state != BCH_MEMBER_STATE_RW ||
		    !ja->nr ||
		    bch2_bkey_has_device(bkey_i_to_s_c(&w->key),
					 ca->dev_idx) ||
		    sectors > ja->sectors_free)
			continue;

		bch2_dev_stripe_increment(c, ca, &j->wp.stripe);

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
 * journal_next_bucket - move on to the next journal bucket if possible
 */
static int journal_write_alloc(struct journal *j, struct journal_buf *w,
			       unsigned sectors)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_device *ja;
	struct bch_dev *ca;
	struct dev_alloc_list devs_sorted;
	unsigned i, replicas = 0, replicas_want =
		READ_ONCE(c->opts.metadata_replicas);

	rcu_read_lock();

	devs_sorted = bch2_dev_alloc_list(c, &j->wp.stripe,
					  &c->rw_devs[BCH_DATA_JOURNAL]);

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
		}
	}

	__journal_write_alloc(j, w, &devs_sorted,
			      sectors, &replicas, replicas_want);
done:
	rcu_read_unlock();

	return replicas >= c->opts.metadata_replicas_required ? 0 : -EROFS;
}

static void journal_write_compact(struct jset *jset)
{
	struct jset_entry *i, *next, *prev = NULL;

	/*
	 * Simple compaction, dropping empty jset_entries (from journal
	 * reservations that weren't fully used) and merging jset_entries that
	 * can be.
	 *
	 * If we wanted to be really fancy here, we could sort all the keys in
	 * the jset and drop keys that were overwritten - probably not worth it:
	 */
	vstruct_for_each_safe(jset, i, next) {
		unsigned u64s = le16_to_cpu(i->u64s);

		/* Empty entry: */
		if (!u64s)
			continue;

		/* Can we merge with previous entry? */
		if (prev &&
		    i->btree_id == prev->btree_id &&
		    i->level	== prev->level &&
		    i->type	== prev->type &&
		    i->type	== BCH_JSET_ENTRY_btree_keys &&
		    le16_to_cpu(prev->u64s) + u64s <= U16_MAX) {
			memmove_u64s_down(vstruct_next(prev),
					  i->_data,
					  u64s);
			le16_add_cpu(&prev->u64s, u64s);
			continue;
		}

		/* Couldn't merge, move i into new position (after prev): */
		prev = prev ? vstruct_next(prev) : jset->start;
		if (i != prev)
			memmove_u64s_down(prev, i, jset_u64s(u64s));
	}

	prev = prev ? vstruct_next(prev) : jset->start;
	jset->u64s = cpu_to_le32((u64 *) prev - jset->_data);
}

static void journal_buf_realloc(struct journal *j, struct journal_buf *buf)
{
	/* we aren't holding j->lock: */
	unsigned new_size = READ_ONCE(j->buf_size_want);
	void *new_buf;

	if (buf->buf_size >= new_size)
		return;

	new_buf = kvpmalloc(new_size, GFP_NOIO|__GFP_NOWARN);
	if (!new_buf)
		return;

	memcpy(new_buf, buf->data, buf->buf_size);
	kvpfree(buf->data, buf->buf_size);
	buf->data	= new_buf;
	buf->buf_size	= new_size;
}

static void journal_write_done(struct closure *cl)
{
	struct journal *j = container_of(cl, struct journal, io);
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *w = journal_prev_buf(j);
	struct bch_devs_list devs =
		bch2_bkey_devs(bkey_i_to_s_c(&w->key));
	struct bch_replicas_padded replicas;
	u64 seq = le64_to_cpu(w->data->seq);
	u64 last_seq = le64_to_cpu(w->data->last_seq);

	bch2_time_stats_update(j->write_time, j->write_start_time);

	if (!devs.nr) {
		bch_err(c, "unable to write journal to sufficient devices");
		goto err;
	}

	bch2_devlist_to_replicas(&replicas.e, BCH_DATA_JOURNAL, devs);

	if (bch2_mark_replicas(c, &replicas.e))
		goto err;

	spin_lock(&j->lock);
	if (seq >= j->pin.front)
		journal_seq_pin(j, seq)->devs = devs;

	j->seq_ondisk		= seq;
	j->last_seq_ondisk	= last_seq;
	bch2_journal_space_available(j);

	/*
	 * Updating last_seq_ondisk may let bch2_journal_reclaim_work() discard
	 * more buckets:
	 *
	 * Must come before signaling write completion, for
	 * bch2_fs_journal_stop():
	 */
	mod_delayed_work(c->journal_reclaim_wq, &j->reclaim_work, 0);
out:
	/* also must come before signalling write completion: */
	closure_debug_destroy(cl);

	BUG_ON(!j->reservations.prev_buf_unwritten);
	atomic64_sub(((union journal_res_state) { .prev_buf_unwritten = 1 }).v,
		     &j->reservations.counter);

	closure_wake_up(&w->wait);
	journal_wake(j);

	if (test_bit(JOURNAL_NEED_WRITE, &j->flags))
		mod_delayed_work(system_freezable_wq, &j->write_work, 0);
	spin_unlock(&j->lock);
	return;
err:
	bch2_fatal_error(c);
	bch2_journal_halt(j);
	spin_lock(&j->lock);
	goto out;
}

static void journal_write_endio(struct bio *bio)
{
	struct bch_dev *ca = bio->bi_private;
	struct journal *j = &ca->fs->journal;

	if (bch2_dev_io_err_on(bio->bi_status, ca, "journal write") ||
	    bch2_meta_write_fault("journal")) {
		struct journal_buf *w = journal_prev_buf(j);
		unsigned long flags;

		spin_lock_irqsave(&j->err_lock, flags);
		bch2_bkey_drop_device(bkey_i_to_s(&w->key), ca->dev_idx);
		spin_unlock_irqrestore(&j->err_lock, flags);
	}

	closure_put(&j->io);
	percpu_ref_put(&ca->io_ref);
}

void bch2_journal_write(struct closure *cl)
{
	struct journal *j = container_of(cl, struct journal, io);
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	struct journal_buf *w = journal_prev_buf(j);
	struct jset_entry *start, *end;
	struct jset *jset;
	struct bio *bio;
	struct bch_extent_ptr *ptr;
	bool validate_before_checksum = false;
	unsigned i, sectors, bytes, u64s;
	int ret;

	bch2_journal_pin_put(j, le64_to_cpu(w->data->seq));

	journal_buf_realloc(j, w);
	jset = w->data;

	j->write_start_time = local_clock();

	start	= vstruct_last(jset);
	end	= bch2_journal_super_entries_add_common(c, start,
						le64_to_cpu(jset->seq));
	u64s	= (u64 *) end - (u64 *) start;
	BUG_ON(u64s > j->entry_u64s_reserved);

	le32_add_cpu(&jset->u64s, u64s);
	BUG_ON(vstruct_sectors(jset, c->block_bits) > w->sectors);

	journal_write_compact(jset);

	jset->read_clock	= cpu_to_le16(c->bucket_clock[READ].hand);
	jset->write_clock	= cpu_to_le16(c->bucket_clock[WRITE].hand);
	jset->magic		= cpu_to_le64(jset_magic(c));

	jset->version		= c->sb.version < bcachefs_metadata_version_new_versioning
		? cpu_to_le32(BCH_JSET_VERSION_OLD)
		: cpu_to_le32(c->sb.version);

	SET_JSET_BIG_ENDIAN(jset, CPU_BIG_ENDIAN);
	SET_JSET_CSUM_TYPE(jset, bch2_meta_checksum_type(c));

	if (bch2_csum_type_is_encryption(JSET_CSUM_TYPE(jset)))
		validate_before_checksum = true;

	if (le32_to_cpu(jset->version) <
	    bcachefs_metadata_version_bkey_renumber)
		validate_before_checksum = true;

	if (validate_before_checksum &&
	    jset_validate_entries(c, jset, WRITE))
		goto err;

	bch2_encrypt(c, JSET_CSUM_TYPE(jset), journal_nonce(jset),
		    jset->encrypted_start,
		    vstruct_end(jset) - (void *) jset->encrypted_start);

	jset->csum = csum_vstruct(c, JSET_CSUM_TYPE(jset),
				  journal_nonce(jset), jset);

	if (!validate_before_checksum &&
	    jset_validate_entries(c, jset, WRITE))
		goto err;

	sectors = vstruct_sectors(jset, c->block_bits);
	BUG_ON(sectors > w->sectors);

	bytes = vstruct_bytes(jset);
	memset((void *) jset + bytes, 0, (sectors << 9) - bytes);

	spin_lock(&j->lock);
	ret = journal_write_alloc(j, w, sectors);

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

	if (ret) {
		bch2_journal_halt(j);
		bch_err(c, "Unable to allocate journal write");
		bch2_fatal_error(c);
		continue_at(cl, journal_write_done, system_highpri_wq);
		return;
	}

	/*
	 * XXX: we really should just disable the entire journal in nochanges
	 * mode
	 */
	if (c->opts.nochanges)
		goto no_io;

	extent_for_each_ptr(bkey_i_to_s_extent(&w->key), ptr) {
		ca = bch_dev_bkey_exists(c, ptr->dev);
		if (!percpu_ref_tryget(&ca->io_ref)) {
			/* XXX: fix this */
			bch_err(c, "missing device for journal write\n");
			continue;
		}

		this_cpu_add(ca->io_done->sectors[WRITE][BCH_DATA_JOURNAL],
			     sectors);

		bio = ca->journal.bio;
		bio_reset(bio, ca->disk_sb.bdev,
			  REQ_OP_WRITE|REQ_SYNC|REQ_META|REQ_PREFLUSH|REQ_FUA);
		bio->bi_iter.bi_sector	= ptr->offset;
		bio->bi_iter.bi_size	= sectors << 9;
		bio->bi_end_io		= journal_write_endio;
		bio->bi_private		= ca;
		bch2_bio_map(bio, jset);

		trace_journal_write(bio);
		closure_bio_submit(bio, cl);

		ca->journal.bucket_seq[ca->journal.cur_idx] = le64_to_cpu(jset->seq);
	}

	for_each_rw_member(ca, c, i)
		if (journal_flushes_device(ca) &&
		    !bch2_extent_has_device(bkey_i_to_s_c_extent(&w->key), i)) {
			percpu_ref_get(&ca->io_ref);

			bio = ca->journal.bio;
			bio_reset(bio, ca->disk_sb.bdev, REQ_OP_FLUSH);
			bio->bi_end_io		= journal_write_endio;
			bio->bi_private		= ca;
			closure_bio_submit(bio, cl);
		}

no_io:
	bch2_bucket_seq_cleanup(c);

	continue_at(cl, journal_write_done, system_highpri_wq);
	return;
err:
	bch2_inconsistent_error(c);
	continue_at(cl, journal_write_done, system_highpri_wq);
}
