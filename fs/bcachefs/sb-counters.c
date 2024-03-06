// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "super-io.h"
#include "sb-counters.h"

/* BCH_SB_FIELD_counters */

static const char * const bch2_counter_names[] = {
#define x(t, n, ...) (#t),
	BCH_PERSISTENT_COUNTERS()
#undef x
	NULL
};

static size_t bch2_sb_counter_nr_entries(struct bch_sb_field_counters *ctrs)
{
	if (!ctrs)
		return 0;

	return (__le64 *) vstruct_end(&ctrs->field) - &ctrs->d[0];
};

static int bch2_sb_counters_validate(struct bch_sb *sb,
				     struct bch_sb_field *f,
				     struct printbuf *err)
{
	return 0;
};

static void bch2_sb_counters_to_text(struct printbuf *out, struct bch_sb *sb,
			      struct bch_sb_field *f)
{
	struct bch_sb_field_counters *ctrs = field_to_type(f, counters);
	unsigned int i;
	unsigned int nr = bch2_sb_counter_nr_entries(ctrs);

	for (i = 0; i < nr; i++) {
		if (i < BCH_COUNTER_NR)
			prt_printf(out, "%s ", bch2_counter_names[i]);
		else
			prt_printf(out, "(unknown)");

		prt_tab(out);
		prt_printf(out, "%llu", le64_to_cpu(ctrs->d[i]));
		prt_newline(out);
	}
};

int bch2_sb_counters_to_cpu(struct bch_fs *c)
{
	struct bch_sb_field_counters *ctrs = bch2_sb_field_get(c->disk_sb.sb, counters);
	unsigned int i;
	unsigned int nr = bch2_sb_counter_nr_entries(ctrs);
	u64 val = 0;

	for (i = 0; i < BCH_COUNTER_NR; i++)
		c->counters_on_mount[i] = 0;

	for (i = 0; i < min_t(unsigned int, nr, BCH_COUNTER_NR); i++) {
		val = le64_to_cpu(ctrs->d[i]);
		percpu_u64_set(&c->counters[i], val);
		c->counters_on_mount[i] = val;
	}
	return 0;
};

int bch2_sb_counters_from_cpu(struct bch_fs *c)
{
	struct bch_sb_field_counters *ctrs = bch2_sb_field_get(c->disk_sb.sb, counters);
	struct bch_sb_field_counters *ret;
	unsigned int i;
	unsigned int nr = bch2_sb_counter_nr_entries(ctrs);

	if (nr < BCH_COUNTER_NR) {
		ret = bch2_sb_field_resize(&c->disk_sb, counters,
					       sizeof(*ctrs) / sizeof(u64) + BCH_COUNTER_NR);

		if (ret) {
			ctrs = ret;
			nr = bch2_sb_counter_nr_entries(ctrs);
		}
	}


	for (i = 0; i < min_t(unsigned int, nr, BCH_COUNTER_NR); i++)
		ctrs->d[i] = cpu_to_le64(percpu_u64_get(&c->counters[i]));
	return 0;
}

void bch2_fs_counters_exit(struct bch_fs *c)
{
	free_percpu(c->counters);
}

int bch2_fs_counters_init(struct bch_fs *c)
{
	c->counters = __alloc_percpu(sizeof(u64) * BCH_COUNTER_NR, sizeof(u64));
	if (!c->counters)
		return -BCH_ERR_ENOMEM_fs_counters_init;

	return bch2_sb_counters_to_cpu(c);
}

const struct bch_sb_field_ops bch_sb_field_ops_counters = {
	.validate	= bch2_sb_counters_validate,
	.to_text	= bch2_sb_counters_to_text,
};
