// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "super-io.h"
#include "sb-counters.h"

/* BCH_SB_FIELD_counters */

static const u8 counters_to_stable_map[] = {
#define x(n, id, ...)	[BCH_COUNTER_##n] = BCH_COUNTER_STABLE_##n,
	BCH_PERSISTENT_COUNTERS()
#undef x
};

const char * const bch2_counter_names[] = {
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
}

static int bch2_sb_counters_validate(struct bch_sb *sb, struct bch_sb_field *f,
				enum bch_validate_flags flags, struct printbuf *err)
{
	return 0;
}

static void bch2_sb_counters_to_text(struct printbuf *out, struct bch_sb *sb,
			      struct bch_sb_field *f)
{
	struct bch_sb_field_counters *ctrs = field_to_type(f, counters);
	unsigned int nr = bch2_sb_counter_nr_entries(ctrs);

	for (unsigned i = 0; i < BCH_COUNTER_NR; i++) {
		unsigned stable = counters_to_stable_map[i];
		if (stable < nr)
			prt_printf(out, "%s \t%llu\n",
				   bch2_counter_names[i],
				   le64_to_cpu(ctrs->d[stable]));
	}
}

int bch2_sb_counters_to_cpu(struct bch_fs *c)
{
	struct bch_sb_field_counters *ctrs = bch2_sb_field_get(c->disk_sb.sb, counters);
	unsigned int nr = bch2_sb_counter_nr_entries(ctrs);

	for (unsigned i = 0; i < BCH_COUNTER_NR; i++)
		c->counters_on_mount[i] = 0;

	for (unsigned i = 0; i < BCH_COUNTER_NR; i++) {
		unsigned stable = counters_to_stable_map[i];
		if (stable < nr) {
			u64 v = le64_to_cpu(ctrs->d[stable]);
			percpu_u64_set(&c->counters[i], v);
			c->counters_on_mount[i] = v;
		}
	}

	return 0;
}

int bch2_sb_counters_from_cpu(struct bch_fs *c)
{
	struct bch_sb_field_counters *ctrs = bch2_sb_field_get(c->disk_sb.sb, counters);
	struct bch_sb_field_counters *ret;
	unsigned int nr = bch2_sb_counter_nr_entries(ctrs);

	if (nr < BCH_COUNTER_NR) {
		ret = bch2_sb_field_resize(&c->disk_sb, counters,
					   sizeof(*ctrs) / sizeof(u64) + BCH_COUNTER_NR);
		if (ret) {
			ctrs = ret;
			nr = bch2_sb_counter_nr_entries(ctrs);
		}
	}

	for (unsigned i = 0; i < BCH_COUNTER_NR; i++) {
		unsigned stable = counters_to_stable_map[i];
		if (stable < nr)
			ctrs->d[stable] = cpu_to_le64(percpu_u64_get(&c->counters[i]));
	}

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

#ifndef NO_BCACHEFS_CHARDEV
long bch2_ioctl_query_counters(struct bch_fs *c,
			struct bch_ioctl_query_counters __user *user_arg)
{
	struct bch_ioctl_query_counters arg;
	int ret = copy_from_user_errcode(&arg, user_arg, sizeof(arg));
	if (ret)
		return ret;

	if ((arg.flags & ~BCH_IOCTL_QUERY_COUNTERS_MOUNT) ||
	    arg.pad)
		return -EINVAL;

	arg.nr = min(arg.nr, BCH_COUNTER_NR);
	ret = put_user(arg.nr, &user_arg->nr);
	if (ret)
		return ret;

	for (unsigned i = 0; i < BCH_COUNTER_NR; i++) {
		unsigned stable = counters_to_stable_map[i];

		if (stable < arg.nr) {
			u64 v = !(arg.flags & BCH_IOCTL_QUERY_COUNTERS_MOUNT)
				? percpu_u64_get(&c->counters[i])
				: c->counters_on_mount[i];

			ret = put_user(v, &user_arg->d[stable]);
			if (ret)
				return ret;
		}
	}

	return 0;
}
#endif
