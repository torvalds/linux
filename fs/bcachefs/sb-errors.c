// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "sb-errors.h"
#include "super-io.h"

const char * const bch2_sb_error_strs[] = {
#define x(t, n, ...) [n] = #t,
	BCH_SB_ERRS()
	NULL
};

static void bch2_sb_error_id_to_text(struct printbuf *out, enum bch_sb_error_id id)
{
	if (id < BCH_SB_ERR_MAX)
		prt_str(out, bch2_sb_error_strs[id]);
	else
		prt_printf(out, "(unknown error %u)", id);
}

static inline unsigned bch2_sb_field_errors_nr_entries(struct bch_sb_field_errors *e)
{
	return bch2_sb_field_nr_entries(e);
}

static inline unsigned bch2_sb_field_errors_u64s(unsigned nr)
{
	return (sizeof(struct bch_sb_field_errors) +
		sizeof(struct bch_sb_field_error_entry) * nr) / sizeof(u64);
}

static int bch2_sb_errors_validate(struct bch_sb *sb, struct bch_sb_field *f,
				   enum bch_validate_flags flags, struct printbuf *err)
{
	struct bch_sb_field_errors *e = field_to_type(f, errors);
	unsigned i, nr = bch2_sb_field_errors_nr_entries(e);

	for (i = 0; i < nr; i++) {
		if (!BCH_SB_ERROR_ENTRY_NR(&e->entries[i])) {
			prt_printf(err, "entry with count 0 (id ");
			bch2_sb_error_id_to_text(err, BCH_SB_ERROR_ENTRY_ID(&e->entries[i]));
			prt_printf(err, ")");
			return -BCH_ERR_invalid_sb_errors;
		}

		if (i + 1 < nr &&
		    BCH_SB_ERROR_ENTRY_ID(&e->entries[i]) >=
		    BCH_SB_ERROR_ENTRY_ID(&e->entries[i + 1])) {
			prt_printf(err, "entries out of order");
			return -BCH_ERR_invalid_sb_errors;
		}
	}

	return 0;
}

static void bch2_sb_errors_to_text(struct printbuf *out, struct bch_sb *sb,
				   struct bch_sb_field *f)
{
	struct bch_sb_field_errors *e = field_to_type(f, errors);
	unsigned i, nr = bch2_sb_field_errors_nr_entries(e);

	if (out->nr_tabstops <= 1)
		printbuf_tabstop_push(out, 16);

	for (i = 0; i < nr; i++) {
		bch2_sb_error_id_to_text(out, BCH_SB_ERROR_ENTRY_ID(&e->entries[i]));
		prt_tab(out);
		prt_u64(out, BCH_SB_ERROR_ENTRY_NR(&e->entries[i]));
		prt_tab(out);
		bch2_prt_datetime(out, le64_to_cpu(e->entries[i].last_error_time));
		prt_newline(out);
	}
}

const struct bch_sb_field_ops bch_sb_field_ops_errors = {
	.validate	= bch2_sb_errors_validate,
	.to_text	= bch2_sb_errors_to_text,
};

void bch2_sb_error_count(struct bch_fs *c, enum bch_sb_error_id err)
{
	bch_sb_errors_cpu *e = &c->fsck_error_counts;
	struct bch_sb_error_entry_cpu n = {
		.id = err,
		.nr = 1,
		.last_error_time = ktime_get_real_seconds()
	};
	unsigned i;

	mutex_lock(&c->fsck_error_counts_lock);
	for (i = 0; i < e->nr; i++) {
		if (err == e->data[i].id) {
			e->data[i].nr++;
			e->data[i].last_error_time = n.last_error_time;
			goto out;
		}
		if (err < e->data[i].id)
			break;
	}

	if (darray_make_room(e, 1))
		goto out;

	darray_insert_item(e, i, n);
out:
	mutex_unlock(&c->fsck_error_counts_lock);
}

void bch2_sb_errors_from_cpu(struct bch_fs *c)
{
	bch_sb_errors_cpu *src = &c->fsck_error_counts;
	struct bch_sb_field_errors *dst;
	unsigned i;

	mutex_lock(&c->fsck_error_counts_lock);

	dst = bch2_sb_field_resize(&c->disk_sb, errors,
				   bch2_sb_field_errors_u64s(src->nr));

	if (!dst)
		goto err;

	for (i = 0; i < src->nr; i++) {
		SET_BCH_SB_ERROR_ENTRY_ID(&dst->entries[i], src->data[i].id);
		SET_BCH_SB_ERROR_ENTRY_NR(&dst->entries[i], src->data[i].nr);
		dst->entries[i].last_error_time = cpu_to_le64(src->data[i].last_error_time);
	}

err:
	mutex_unlock(&c->fsck_error_counts_lock);
}

static int bch2_sb_errors_to_cpu(struct bch_fs *c)
{
	struct bch_sb_field_errors *src = bch2_sb_field_get(c->disk_sb.sb, errors);
	bch_sb_errors_cpu *dst = &c->fsck_error_counts;
	unsigned i, nr = bch2_sb_field_errors_nr_entries(src);
	int ret;

	if (!nr)
		return 0;

	mutex_lock(&c->fsck_error_counts_lock);
	ret = darray_make_room(dst, nr);
	if (ret)
		goto err;

	dst->nr = nr;

	for (i = 0; i < nr; i++) {
		dst->data[i].id = BCH_SB_ERROR_ENTRY_ID(&src->entries[i]);
		dst->data[i].nr = BCH_SB_ERROR_ENTRY_NR(&src->entries[i]);
		dst->data[i].last_error_time = le64_to_cpu(src->entries[i].last_error_time);
	}
err:
	mutex_unlock(&c->fsck_error_counts_lock);

	return ret;
}

void bch2_fs_sb_errors_exit(struct bch_fs *c)
{
	darray_exit(&c->fsck_error_counts);
}

void bch2_fs_sb_errors_init_early(struct bch_fs *c)
{
	mutex_init(&c->fsck_error_counts_lock);
	darray_init(&c->fsck_error_counts);
}

int bch2_fs_sb_errors_init(struct bch_fs *c)
{
	return bch2_sb_errors_to_cpu(c);
}
