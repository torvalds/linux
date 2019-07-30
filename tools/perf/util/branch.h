#ifndef _PERF_BRANCH_H
#define _PERF_BRANCH_H 1

#include <stdio.h>
#include <stdint.h>
#include <linux/perf_event.h>
#include <linux/types.h>

struct branch_flags {
	u64 mispred:1;
	u64 predicted:1;
	u64 in_tx:1;
	u64 abort:1;
	u64 cycles:16;
	u64 type:4;
	u64 reserved:40;
};

struct branch_entry {
	u64			from;
	u64			to;
	struct branch_flags	flags;
};

struct branch_stack {
	u64			nr;
	struct branch_entry	entries[0];
};

struct branch_type_stat {
	bool	branch_to;
	u64	counts[PERF_BR_MAX];
	u64	cond_fwd;
	u64	cond_bwd;
	u64	cross_4k;
	u64	cross_2m;
};

void branch_type_count(struct branch_type_stat *st, struct branch_flags *flags,
		       u64 from, u64 to);

const char *branch_type_name(int type);
void branch_type_stat_display(FILE *fp, struct branch_type_stat *st);
int branch_type_str(struct branch_type_stat *st, char *bf, int bfsize);

#endif /* _PERF_BRANCH_H */
