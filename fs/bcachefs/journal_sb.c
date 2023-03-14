// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "journal_sb.h"
#include "darray.h"

#include <linux/sort.h>

/* BCH_SB_FIELD_journal: */

static int u64_cmp(const void *_l, const void *_r)
{
	const u64 *l = _l;
	const u64 *r = _r;

	return cmp_int(*l, *r);
}

static int bch2_sb_journal_validate(struct bch_sb *sb,
				    struct bch_sb_field *f,
				    struct printbuf *err)
{
	struct bch_sb_field_journal *journal = field_to_type(f, journal);
	struct bch_member *m = bch2_sb_get_members(sb)->members + sb->dev_idx;
	int ret = -BCH_ERR_invalid_sb_journal;
	unsigned nr;
	unsigned i;
	u64 *b;

	nr = bch2_nr_journal_buckets(journal);
	if (!nr)
		return 0;

	b = kmalloc_array(nr, sizeof(u64), GFP_KERNEL);
	if (!b)
		return -BCH_ERR_ENOMEM_sb_journal_validate;

	for (i = 0; i < nr; i++)
		b[i] = le64_to_cpu(journal->buckets[i]);

	sort(b, nr, sizeof(u64), u64_cmp, NULL);

	if (!b[0]) {
		prt_printf(err, "journal bucket at sector 0");
		goto err;
	}

	if (b[0] < le16_to_cpu(m->first_bucket)) {
		prt_printf(err, "journal bucket %llu before first bucket %u",
		       b[0], le16_to_cpu(m->first_bucket));
		goto err;
	}

	if (b[nr - 1] >= le64_to_cpu(m->nbuckets)) {
		prt_printf(err, "journal bucket %llu past end of device (nbuckets %llu)",
		       b[nr - 1], le64_to_cpu(m->nbuckets));
		goto err;
	}

	for (i = 0; i + 1 < nr; i++)
		if (b[i] == b[i + 1]) {
			prt_printf(err, "duplicate journal buckets %llu", b[i]);
			goto err;
		}

	ret = 0;
err:
	kfree(b);
	return ret;
}

static void bch2_sb_journal_to_text(struct printbuf *out, struct bch_sb *sb,
				    struct bch_sb_field *f)
{
	struct bch_sb_field_journal *journal = field_to_type(f, journal);
	unsigned i, nr = bch2_nr_journal_buckets(journal);

	prt_printf(out, "Buckets: ");
	for (i = 0; i < nr; i++)
		prt_printf(out, " %llu", le64_to_cpu(journal->buckets[i]));
	prt_newline(out);
}

const struct bch_sb_field_ops bch_sb_field_ops_journal = {
	.validate	= bch2_sb_journal_validate,
	.to_text	= bch2_sb_journal_to_text,
};

struct u64_range {
	u64	start;
	u64	end;
};

static int u64_range_cmp(const void *_l, const void *_r)
{
	const struct u64_range *l = _l;
	const struct u64_range *r = _r;

	return cmp_int(l->start, r->start);
}

static int bch2_sb_journal_v2_validate(struct bch_sb *sb,
				    struct bch_sb_field *f,
				    struct printbuf *err)
{
	struct bch_sb_field_journal_v2 *journal = field_to_type(f, journal_v2);
	struct bch_member *m = bch2_sb_get_members(sb)->members + sb->dev_idx;
	int ret = -BCH_ERR_invalid_sb_journal;
	unsigned nr;
	unsigned i;
	struct u64_range *b;

	nr = bch2_sb_field_journal_v2_nr_entries(journal);
	if (!nr)
		return 0;

	b = kmalloc_array(nr, sizeof(*b), GFP_KERNEL);
	if (!b)
		return -BCH_ERR_ENOMEM_sb_journal_v2_validate;

	for (i = 0; i < nr; i++) {
		b[i].start = le64_to_cpu(journal->d[i].start);
		b[i].end = b[i].start + le64_to_cpu(journal->d[i].nr);
	}

	sort(b, nr, sizeof(*b), u64_range_cmp, NULL);

	if (!b[0].start) {
		prt_printf(err, "journal bucket at sector 0");
		goto err;
	}

	if (b[0].start < le16_to_cpu(m->first_bucket)) {
		prt_printf(err, "journal bucket %llu before first bucket %u",
		       b[0].start, le16_to_cpu(m->first_bucket));
		goto err;
	}

	if (b[nr - 1].end > le64_to_cpu(m->nbuckets)) {
		prt_printf(err, "journal bucket %llu past end of device (nbuckets %llu)",
		       b[nr - 1].end - 1, le64_to_cpu(m->nbuckets));
		goto err;
	}

	for (i = 0; i + 1 < nr; i++) {
		if (b[i].end > b[i + 1].start) {
			prt_printf(err, "duplicate journal buckets in ranges %llu-%llu, %llu-%llu",
			       b[i].start, b[i].end, b[i + 1].start, b[i + 1].end);
			goto err;
		}
	}

	ret = 0;
err:
	kfree(b);
	return ret;
}

static void bch2_sb_journal_v2_to_text(struct printbuf *out, struct bch_sb *sb,
				    struct bch_sb_field *f)
{
	struct bch_sb_field_journal_v2 *journal = field_to_type(f, journal_v2);
	unsigned i, nr = bch2_sb_field_journal_v2_nr_entries(journal);

	prt_printf(out, "Buckets: ");
	for (i = 0; i < nr; i++)
		prt_printf(out, " %llu-%llu",
		       le64_to_cpu(journal->d[i].start),
		       le64_to_cpu(journal->d[i].start) + le64_to_cpu(journal->d[i].nr));
	prt_newline(out);
}

const struct bch_sb_field_ops bch_sb_field_ops_journal_v2 = {
	.validate	= bch2_sb_journal_v2_validate,
	.to_text	= bch2_sb_journal_v2_to_text,
};

int bch2_journal_buckets_to_sb(struct bch_fs *c, struct bch_dev *ca,
			       u64 *buckets, unsigned nr)
{
	struct bch_sb_field_journal_v2 *j;
	unsigned i, dst = 0, nr_compacted = 1;

	if (c)
		lockdep_assert_held(&c->sb_lock);

	if (!nr) {
		bch2_sb_field_delete(&ca->disk_sb, BCH_SB_FIELD_journal);
		bch2_sb_field_delete(&ca->disk_sb, BCH_SB_FIELD_journal_v2);
		return 0;
	}

	for (i = 0; i + 1 < nr; i++)
		if (buckets[i] + 1 != buckets[i + 1])
			nr_compacted++;

	j = bch2_sb_resize_journal_v2(&ca->disk_sb,
			 (sizeof(*j) + sizeof(j->d[0]) * nr_compacted) / sizeof(u64));
	if (!j)
		return -BCH_ERR_ENOSPC_sb_journal;

	bch2_sb_field_delete(&ca->disk_sb, BCH_SB_FIELD_journal);

	j->d[dst].start = le64_to_cpu(buckets[0]);
	j->d[dst].nr	= le64_to_cpu(1);

	for (i = 1; i < nr; i++) {
		if (buckets[i] == buckets[i - 1] + 1) {
			le64_add_cpu(&j->d[dst].nr, 1);
		} else {
			dst++;
			j->d[dst].start = le64_to_cpu(buckets[i]);
			j->d[dst].nr	= le64_to_cpu(1);
		}
	}

	BUG_ON(dst + 1 != nr_compacted);
	return 0;
}
