// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "alloc.h"
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
				struct bkey_i *k, enum bkey_type key_type,
				const char *type, int write)
{
	void *next = vstruct_next(entry);
	const char *invalid;
	char buf[160];
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
		bch2_bkey_swab(key_type, NULL, bkey_to_packed(k));

	invalid = bch2_bkey_invalid(c, key_type, bkey_i_to_s_c(k));
	if (invalid) {
		bch2_bkey_val_to_text(c, key_type, buf, sizeof(buf),
				     bkey_i_to_s_c(k));
		mustfix_fsck_err(c, "invalid %s in journal: %s\n%s",
				 type, invalid, buf);

		le16_add_cpu(&entry->u64s, -k->k.u64s);
		memmove(k, bkey_next(k), next - (void *) bkey_next(k));
		journal_entry_null_range(vstruct_next(entry), next);
		return 0;
	}
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
				bkey_type(entry->level,
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
	}

	bl_entry = container_of(entry, struct jset_entry_blacklist_v2, entry);

	if (journal_entry_err_on(le64_to_cpu(bl_entry->start) >
				 le64_to_cpu(bl_entry->end), c,
		"invalid journal seq blacklist entry: start > end")) {
		journal_entry_null_range(entry, vstruct_next(entry));
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
	int ret = 0;

	if (entry->type >= BCH_JSET_ENTRY_NR) {
		journal_entry_err(c, "invalid journal entry type %u",
				  entry->type);
		journal_entry_null_range(entry, vstruct_next(entry));
		return 0;
	}

	ret = bch2_jset_entry_ops[entry->type].validate(c, jset, entry, write);
fsck_err:
	return ret;
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
	int ret = 0;

	if (le64_to_cpu(jset->magic) != jset_magic(c))
		return JOURNAL_ENTRY_NONE;

	if (le32_to_cpu(jset->version) != BCACHE_JSET_VERSION) {
		bch_err(c, "unknown journal entry version %u",
			le32_to_cpu(jset->version));
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
			       unsigned bucket, u64 *seq, bool *entries_found)
{
	struct bch_fs *c = ca->fs;
	struct journal_device *ja = &ca->journal;
	struct bio *bio = ja->bio;
	struct jset *j = NULL;
	unsigned sectors, sectors_read = 0;
	u64 offset = bucket_to_sector(ca, ja->buckets[bucket]),
	    end = offset + ca->mi.bucket_size;
	bool saw_bad = false;
	int ret = 0;

	pr_debug("reading %u", bucket);

	while (offset < end) {
		if (!sectors_read) {
reread:			sectors_read = min_t(unsigned,
				end - offset, buf->size >> 9);

			bio_reset(bio, ca->disk_sb.bdev, REQ_OP_READ);
			bio->bi_iter.bi_sector	= offset;
			bio->bi_iter.bi_size	= sectors_read << 9;
			bch2_bio_map(bio, buf->data);

			ret = submit_bio_wait(bio);

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
			*entries_found = true;
			break;
		case JOURNAL_ENTRY_ADD_OUT_OF_RANGE:
			break;
		default:
			return ret;
		}

		if (le64_to_cpu(j->seq) > *seq)
			*seq = le64_to_cpu(j->seq);

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
#define read_bucket(b)							\
	({								\
		bool entries_found = false;				\
		ret = journal_read_bucket(ca, &buf, jlist, b, &seq,	\
					  &entries_found);		\
		if (ret)						\
			goto err;					\
		__set_bit(b, bitmap);					\
		entries_found;						\
	 })

	struct journal_device *ja =
		container_of(cl, struct journal_device, read);
	struct bch_dev *ca = container_of(ja, struct bch_dev, journal);
	struct journal_list *jlist =
		container_of(cl->parent, struct journal_list, cl);
	struct request_queue *q = bdev_get_queue(ca->disk_sb.bdev);
	struct journal_read_buf buf = { NULL, 0 };
	unsigned long *bitmap;
	unsigned i, l, r;
	u64 seq = 0;
	int ret;

	if (!ja->nr)
		goto out;

	bitmap = kcalloc(BITS_TO_LONGS(ja->nr), ja->nr, GFP_KERNEL);
	if (!bitmap) {
		ret = -ENOMEM;
		goto err;
	}

	ret = journal_read_buf_realloc(&buf, PAGE_SIZE);
	if (ret)
		goto err;

	pr_debug("%u journal buckets", ja->nr);

	/*
	 * If the device supports discard but not secure discard, we can't do
	 * the fancy fibonacci hash/binary search because the live journal
	 * entries might not form a contiguous range:
	 */
	for (i = 0; i < ja->nr; i++)
		read_bucket(i);
	goto search_done;

	if (!blk_queue_nonrot(q))
		goto linear_scan;

	/*
	 * Read journal buckets ordered by golden ratio hash to quickly
	 * find a sequence of buckets with valid journal entries
	 */
	for (i = 0; i < ja->nr; i++) {
		l = (i * 2654435769U) % ja->nr;

		if (test_bit(l, bitmap))
			break;

		if (read_bucket(l))
			goto bsearch;
	}

	/*
	 * If that fails, check all the buckets we haven't checked
	 * already
	 */
	pr_debug("falling back to linear search");
linear_scan:
	for (l = find_first_zero_bit(bitmap, ja->nr);
	     l < ja->nr;
	     l = find_next_zero_bit(bitmap, ja->nr, l + 1))
		if (read_bucket(l))
			goto bsearch;

	/* no journal entries on this device? */
	if (l == ja->nr)
		goto out;
bsearch:
	/* Binary search */
	r = find_next_bit(bitmap, ja->nr, l + 1);
	pr_debug("starting binary search, l %u r %u", l, r);

	while (l + 1 < r) {
		unsigned m = (l + r) >> 1;
		u64 cur_seq = seq;

		read_bucket(m);

		if (cur_seq != seq)
			l = m;
		else
			r = m;
	}

search_done:
	/*
	 * Find the journal bucket with the highest sequence number:
	 *
	 * If there's duplicate journal entries in multiple buckets (which
	 * definitely isn't supposed to happen, but...) - make sure to start
	 * cur_idx at the last of those buckets, so we don't deadlock trying to
	 * allocate
	 */
	seq = 0;

	for (i = 0; i < ja->nr; i++)
		if (ja->bucket_seq[i] >= seq &&
		    ja->bucket_seq[i] != ja->bucket_seq[(i + 1) % ja->nr]) {
			/*
			 * When journal_next_bucket() goes to allocate for
			 * the first time, it'll use the bucket after
			 * ja->cur_idx
			 */
			ja->cur_idx = i;
			seq = ja->bucket_seq[i];
		}

	/*
	 * Set last_idx to indicate the entire journal is full and needs to be
	 * reclaimed - journal reclaim will immediately reclaim whatever isn't
	 * pinned when it first runs:
	 */
	ja->last_idx = (ja->cur_idx + 1) % ja->nr;

	/*
	 * Read buckets in reverse order until we stop finding more journal
	 * entries:
	 */
	for (i = (ja->cur_idx + ja->nr - 1) % ja->nr;
	     i != ja->cur_idx;
	     i = (i + ja->nr - 1) % ja->nr)
		if (!test_bit(i, bitmap) &&
		    !read_bucket(i))
			break;
out:
	kvpfree(buf.data, buf.size);
	kfree(bitmap);
	percpu_ref_put(&ca->io_ref);
	closure_return(cl);
	return;
err:
	mutex_lock(&jlist->lock);
	jlist->ret = ret;
	mutex_unlock(&jlist->lock);
	goto out;
#undef read_bucket
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
		if (!(bch2_dev_has_data(c, ca) & (1 << BCH_DATA_JOURNAL)))
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
		ret = jset_validate_entries(c, &i->j, READ);
		if (ret)
			goto fsck_err;

		/*
		 * If we're mounting in degraded mode - if we didn't read all
		 * the devices - this is wrong:
		 */

		if (!degraded &&
		    (test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) ||
		     fsck_err_on(!bch2_replicas_marked(c, BCH_DATA_JOURNAL,
						       i->devs), c,
				 "superblock not marked as containing replicas (type %u)",
				 BCH_DATA_JOURNAL))) {
			ret = bch2_mark_replicas(c, BCH_DATA_JOURNAL, i->devs);
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

int bch2_journal_mark(struct bch_fs *c, struct list_head *list)
{
	struct bkey_i *k, *n;
	struct jset_entry *j;
	struct journal_replay *r;
	int ret;

	list_for_each_entry(r, list, list)
		for_each_jset_key(k, n, j, &r->j) {
			enum bkey_type type = bkey_type(j->level, j->btree_id);
			struct bkey_s_c k_s_c = bkey_i_to_s_c(k);

			if (btree_type_has_ptrs(type)) {
				ret = bch2_btree_mark_key_initial(c, type, k_s_c);
				if (ret)
					return ret;
			}
		}

	return 0;
}

int bch2_journal_replay(struct bch_fs *c, struct list_head *list)
{
	struct journal *j = &c->journal;
	struct journal_entry_pin_list *pin_list;
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
				ret = bch2_alloc_replay_key(c, k->k.p);
			} else {
				/*
				 * We might cause compressed extents to be
				 * split, so we need to pass in a
				 * disk_reservation:
				 */
				struct disk_reservation disk_res =
					bch2_disk_reservation_init(c, 0);

				ret = bch2_btree_insert(c, entry->btree_id, k,
							&disk_res, NULL, NULL,
							BTREE_INSERT_NOFAIL|
							BTREE_INSERT_JOURNAL_REPLAY);
			}

			if (ret) {
				bch_err(c, "journal replay: error %d while replaying key",
					ret);
				goto err;
			}

			cond_resched();
		}

		pin_list = journal_seq_pin(j, j->replay_journal_seq);

		if (atomic_dec_and_test(&pin_list->count))
			journal_wake(j);
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

static void bch2_journal_add_btree_root(struct journal_buf *buf,
				       enum btree_id id, struct bkey_i *k,
				       unsigned level)
{
	struct jset_entry *entry;

	entry = bch2_journal_add_entry_noreservation(buf, k->k.u64s);
	entry->type	= BCH_JSET_ENTRY_btree_root;
	entry->btree_id = id;
	entry->level	= level;
	memcpy_u64s(entry->_data, k, k->k.u64s);
}

static unsigned journal_dev_buckets_available(struct journal *j,
					      struct bch_dev *ca)
{
	struct journal_device *ja = &ca->journal;
	unsigned next = (ja->cur_idx + 1) % ja->nr;
	unsigned available = (ja->last_idx + ja->nr - next) % ja->nr;

	/*
	 * Hack to avoid a deadlock during journal replay:
	 * journal replay might require setting a new btree
	 * root, which requires writing another journal entry -
	 * thus, if the journal is full (and this happens when
	 * replaying the first journal bucket's entries) we're
	 * screwed.
	 *
	 * So don't let the journal fill up unless we're in
	 * replay:
	 */
	if (test_bit(JOURNAL_REPLAY_DONE, &j->flags))
		available = max((int) available - 2, 0);

	/*
	 * Don't use the last bucket unless writing the new last_seq
	 * will make another bucket available:
	 */
	if (ja->bucket_seq[ja->last_idx] >= journal_last_seq(j))
		available = max((int) available - 1, 0);

	return available;
}

/* returns number of sectors available for next journal entry: */
int bch2_journal_entry_sectors(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	struct bkey_s_extent e = bkey_i_to_s_extent(&j->key);
	unsigned sectors_available = UINT_MAX;
	unsigned i, nr_online = 0, nr_devs = 0;

	lockdep_assert_held(&j->lock);

	rcu_read_lock();
	for_each_member_device_rcu(ca, c, i,
				   &c->rw_devs[BCH_DATA_JOURNAL]) {
		struct journal_device *ja = &ca->journal;
		unsigned buckets_required = 0;

		if (!ja->nr)
			continue;

		sectors_available = min_t(unsigned, sectors_available,
					  ca->mi.bucket_size);

		/*
		 * Note that we don't allocate the space for a journal entry
		 * until we write it out - thus, if we haven't started the write
		 * for the previous entry we have to make sure we have space for
		 * it too:
		 */
		if (bch2_extent_has_device(e.c, ca->dev_idx)) {
			if (j->prev_buf_sectors > ja->sectors_free)
				buckets_required++;

			if (j->prev_buf_sectors + sectors_available >
			    ja->sectors_free)
				buckets_required++;
		} else {
			if (j->prev_buf_sectors + sectors_available >
			    ca->mi.bucket_size)
				buckets_required++;

			buckets_required++;
		}

		if (journal_dev_buckets_available(j, ca) >= buckets_required)
			nr_devs++;
		nr_online++;
	}
	rcu_read_unlock();

	if (nr_online < c->opts.metadata_replicas_required)
		return -EROFS;

	if (nr_devs < min_t(unsigned, nr_online, c->opts.metadata_replicas))
		return 0;

	return sectors_available;
}

/**
 * journal_next_bucket - move on to the next journal bucket if possible
 */
static int journal_write_alloc(struct journal *j, struct journal_buf *w,
			       unsigned sectors)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bkey_s_extent e;
	struct bch_extent_ptr *ptr;
	struct journal_device *ja;
	struct bch_dev *ca;
	struct dev_alloc_list devs_sorted;
	unsigned i, replicas, replicas_want =
		READ_ONCE(c->opts.metadata_replicas);

	spin_lock(&j->lock);
	e = bkey_i_to_s_extent(&j->key);

	/*
	 * Drop any pointers to devices that have been removed, are no longer
	 * empty, or filled up their current journal bucket:
	 *
	 * Note that a device may have had a small amount of free space (perhaps
	 * one sector) that wasn't enough for the smallest possible journal
	 * entry - that's why we drop pointers to devices <= current free space,
	 * i.e. whichever device was limiting the current journal entry size.
	 */
	extent_for_each_ptr_backwards(e, ptr) {
		   ca = bch_dev_bkey_exists(c, ptr->dev);

		if (ca->mi.state != BCH_MEMBER_STATE_RW ||
		    ca->journal.sectors_free <= sectors)
			__bch2_extent_drop_ptr(e, ptr);
		else
			ca->journal.sectors_free -= sectors;
	}

	replicas = bch2_extent_nr_ptrs(e.c);

	rcu_read_lock();
	devs_sorted = bch2_wp_alloc_list(c, &j->wp,
					 &c->rw_devs[BCH_DATA_JOURNAL]);

	for (i = 0; i < devs_sorted.nr; i++) {
		ca = rcu_dereference(c->devs[devs_sorted.devs[i]]);
		if (!ca)
			continue;

		if (!ca->mi.durability)
			continue;

		ja = &ca->journal;
		if (!ja->nr)
			continue;

		if (replicas >= replicas_want)
			break;

		/*
		 * Check that we can use this device, and aren't already using
		 * it:
		 */
		if (bch2_extent_has_device(e.c, ca->dev_idx) ||
		    !journal_dev_buckets_available(j, ca) ||
		    sectors > ca->mi.bucket_size)
			continue;

		j->wp.next_alloc[ca->dev_idx] += U32_MAX;
		bch2_wp_rescale(c, ca, &j->wp);

		ja->sectors_free = ca->mi.bucket_size - sectors;
		ja->cur_idx = (ja->cur_idx + 1) % ja->nr;
		ja->bucket_seq[ja->cur_idx] = le64_to_cpu(w->data->seq);

		extent_ptr_append(bkey_i_to_extent(&j->key),
			(struct bch_extent_ptr) {
				  .offset = bucket_to_sector(ca,
					ja->buckets[ja->cur_idx]),
				  .dev = ca->dev_idx,
		});

		replicas += ca->mi.durability;
	}
	rcu_read_unlock();

	j->prev_buf_sectors = 0;

	bkey_copy(&w->key, &j->key);
	spin_unlock(&j->lock);

	if (replicas < c->opts.metadata_replicas_required)
		return -EROFS;

	BUG_ON(!replicas);

	return 0;
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

	if (buf->size >= new_size)
		return;

	new_buf = kvpmalloc(new_size, GFP_NOIO|__GFP_NOWARN);
	if (!new_buf)
		return;

	memcpy(new_buf, buf->data, buf->size);
	kvpfree(buf->data, buf->size);
	buf->data	= new_buf;
	buf->size	= new_size;
}

static void journal_write_done(struct closure *cl)
{
	struct journal *j = container_of(cl, struct journal, io);
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *w = journal_prev_buf(j);
	struct bch_devs_list devs =
		bch2_extent_devs(bkey_i_to_s_c_extent(&w->key));
	u64 seq = le64_to_cpu(w->data->seq);
	u64 last_seq = le64_to_cpu(w->data->last_seq);

	bch2_time_stats_update(j->write_time, j->write_start_time);

	if (!devs.nr) {
		bch_err(c, "unable to write journal to sufficient devices");
		goto err;
	}

	if (bch2_mark_replicas(c, BCH_DATA_JOURNAL, devs))
		goto err;

	spin_lock(&j->lock);
	j->seq_ondisk		= seq;
	j->last_seq_ondisk	= last_seq;

	if (seq >= j->pin.front)
		journal_seq_pin(j, seq)->devs = devs;

	/*
	 * Updating last_seq_ondisk may let bch2_journal_reclaim_work() discard
	 * more buckets:
	 *
	 * Must come before signaling write completion, for
	 * bch2_fs_journal_stop():
	 */
	mod_delayed_work(system_freezable_wq, &j->reclaim_work, 0);
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
		bch2_extent_drop_device(bkey_i_to_s_extent(&w->key), ca->dev_idx);
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
	struct jset *jset;
	struct bio *bio;
	struct bch_extent_ptr *ptr;
	unsigned i, sectors, bytes;

	journal_buf_realloc(j, w);
	jset = w->data;

	j->write_start_time = local_clock();
	mutex_lock(&c->btree_root_lock);
	for (i = 0; i < BTREE_ID_NR; i++) {
		struct btree_root *r = &c->btree_roots[i];

		if (r->alive)
			bch2_journal_add_btree_root(w, i, &r->key, r->level);
	}
	c->btree_roots_dirty = false;
	mutex_unlock(&c->btree_root_lock);

	journal_write_compact(jset);

	jset->read_clock	= cpu_to_le16(c->bucket_clock[READ].hand);
	jset->write_clock	= cpu_to_le16(c->bucket_clock[WRITE].hand);
	jset->magic		= cpu_to_le64(jset_magic(c));
	jset->version		= cpu_to_le32(BCACHE_JSET_VERSION);

	SET_JSET_BIG_ENDIAN(jset, CPU_BIG_ENDIAN);
	SET_JSET_CSUM_TYPE(jset, bch2_meta_checksum_type(c));

	if (bch2_csum_type_is_encryption(JSET_CSUM_TYPE(jset)) &&
	    jset_validate_entries(c, jset, WRITE))
		goto err;

	bch2_encrypt(c, JSET_CSUM_TYPE(jset), journal_nonce(jset),
		    jset->encrypted_start,
		    vstruct_end(jset) - (void *) jset->encrypted_start);

	jset->csum = csum_vstruct(c, JSET_CSUM_TYPE(jset),
				  journal_nonce(jset), jset);

	if (!bch2_csum_type_is_encryption(JSET_CSUM_TYPE(jset)) &&
	    jset_validate_entries(c, jset, WRITE))
		goto err;

	sectors = vstruct_sectors(jset, c->block_bits);
	BUG_ON(sectors > j->prev_buf_sectors);

	bytes = vstruct_bytes(w->data);
	memset((void *) w->data + bytes, 0, (sectors << 9) - bytes);

	if (journal_write_alloc(j, w, sectors)) {
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

		ca->journal.bucket_seq[ca->journal.cur_idx] = le64_to_cpu(w->data->seq);
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
	extent_for_each_ptr(bkey_i_to_s_extent(&j->key), ptr)
		ptr->offset += sectors;

	bch2_bucket_seq_cleanup(c);

	continue_at(cl, journal_write_done, system_highpri_wq);
	return;
err:
	bch2_inconsistent_error(c);
	continue_at(cl, journal_write_done, system_highpri_wq);
}
