#ifndef _PERF_BRANCH_H
#define _PERF_BRANCH_H 1
/*
 * The linux/stddef.h isn't need here, but is needed for __always_inline used
 * in files included from uapi/linux/perf_event.h such as
 * /usr/include/linux/swab.h and /usr/include/linux/byteorder/little_endian.h,
 * detected in at least musl libc, used in Alpine Linux. -acme
 */
#include <stdio.h>
#include <linux/perf_event.h>
#include <linux/types.h>
#include "util/map_symbol.h"
#include "util/sample.h"

struct branch_flags {
	union {
		u64 value;
		struct {
			u64 mispred:1;
			u64 predicted:1;
			u64 in_tx:1;
			u64 abort:1;
			u64 cycles:16;
			u64 type:4;
			u64 new_type:4;
			u64 priv:3;
			u64 reserved:33;
		};
	};
};

struct branch_info {
	struct addr_map_symbol from;
	struct addr_map_symbol to;
	struct branch_flags    flags;
	char		       *srcline_from;
	char		       *srcline_to;
};

struct branch_entry {
	u64			from;
	u64			to;
	struct branch_flags	flags;
};

struct branch_stack {
	u64			nr;
	u64			hw_idx;
	struct branch_entry	entries[];
};

/*
 * The hw_idx is only available when PERF_SAMPLE_BRANCH_HW_INDEX is applied.
 * Otherwise, the output format of a sample with branch stack is
 * struct branch_stack {
 *	u64			nr;
 *	struct branch_entry	entries[0];
 * }
 * Check whether the hw_idx is available,
 * and return the corresponding pointer of entries[0].
 */
static inline struct branch_entry *perf_sample__branch_entries(struct perf_sample *sample)
{
	u64 *entry = (u64 *)sample->branch_stack;

	entry++;
	if (sample->no_hw_idx)
		return (struct branch_entry *)entry;
	return (struct branch_entry *)(++entry);
}

struct branch_type_stat {
	bool	branch_to;
	u64	counts[PERF_BR_MAX];
	u64	new_counts[PERF_BR_NEW_MAX];
	u64	cond_fwd;
	u64	cond_bwd;
	u64	cross_4k;
	u64	cross_2m;
};

void branch_type_count(struct branch_type_stat *st, struct branch_flags *flags,
		       u64 from, u64 to);

const char *branch_type_name(int type);
const char *branch_new_type_name(int new_type);
const char *get_branch_type(struct branch_entry *e);
void branch_type_stat_display(FILE *fp, struct branch_type_stat *st);
int branch_type_str(struct branch_type_stat *st, char *bf, int bfsize);

#endif /* _PERF_BRANCH_H */
