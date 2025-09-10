#include "util/map_symbol.h"
#include "util/branch.h"
#include <linux/kernel.h>

static bool cross_area(u64 addr1, u64 addr2, int size)
{
	u64 align1, align2;

	align1 = addr1 & ~(size - 1);
	align2 = addr2 & ~(size - 1);

	return (align1 != align2) ? true : false;
}

#define AREA_4K		4096
#define AREA_2M		(2 * 1024 * 1024)

void branch_type_count(struct branch_type_stat *st, struct branch_flags *flags,
		       u64 from, u64 to)
{
	if (flags->type == PERF_BR_UNKNOWN || from == 0)
		return;

	if (flags->type == PERF_BR_EXTEND_ABI)
		st->new_counts[flags->new_type]++;
	else
		st->counts[flags->type]++;

	if (flags->type == PERF_BR_COND) {
		if (to > from)
			st->cond_fwd++;
		else
			st->cond_bwd++;
	}

	if (cross_area(from, to, AREA_2M))
		st->cross_2m++;
	else if (cross_area(from, to, AREA_4K))
		st->cross_4k++;
}

const char *branch_new_type_name(int new_type)
{
	const char *branch_new_names[PERF_BR_NEW_MAX] = {
		"FAULT_ALGN",
		"FAULT_DATA",
		"FAULT_INST",
/*
 * TODO: This switch should happen on 'perf_session__env(session)->arch'
 * instead, because an arm64 platform perf recording could be
 * opened for analysis on other platforms as well.
 */
#ifdef __aarch64__
		"ARM64_FIQ",
		"ARM64_DEBUG_HALT",
		"ARM64_DEBUG_EXIT",
		"ARM64_DEBUG_INST",
		"ARM64_DEBUG_DATA"
#else
		"ARCH_1",
		"ARCH_2",
		"ARCH_3",
		"ARCH_4",
		"ARCH_5"
#endif
	};

	if (new_type >= 0 && new_type < PERF_BR_NEW_MAX)
		return branch_new_names[new_type];

	return NULL;
}

const char *branch_type_name(int type)
{
	const char *branch_names[PERF_BR_MAX] = {
		"N/A",
		"COND",
		"UNCOND",
		"IND",
		"CALL",
		"IND_CALL",
		"RET",
		"SYSCALL",
		"SYSRET",
		"COND_CALL",
		"COND_RET",
		"ERET",
		"IRQ",
		"SERROR",
		"NO_TX",
		"", // Needed for PERF_BR_EXTEND_ABI that ends up triggering some compiler warnings about NULL deref
	};

	if (type >= 0 && type < PERF_BR_MAX)
		return branch_names[type];

	return NULL;
}

const char *get_branch_type(struct branch_entry *e)
{
	if (e->flags.type == PERF_BR_UNKNOWN)
		return "";

	if (e->flags.type == PERF_BR_EXTEND_ABI)
		return branch_new_type_name(e->flags.new_type);

	return branch_type_name(e->flags.type);
}

void branch_type_stat_display(FILE *fp, const struct branch_type_stat *st)
{
	u64 total = 0;
	int i;

	for (i = 0; i < PERF_BR_MAX; i++)
		total += st->counts[i];

	if (total == 0)
		return;

	fprintf(fp, "\n#");
	fprintf(fp, "\n# Branch Statistics:");
	fprintf(fp, "\n#");

	if (st->cond_fwd > 0) {
		fprintf(fp, "\n%8s: %5.1f%%",
			"COND_FWD",
			100.0 * (double)st->cond_fwd / (double)total);
	}

	if (st->cond_bwd > 0) {
		fprintf(fp, "\n%8s: %5.1f%%",
			"COND_BWD",
			100.0 * (double)st->cond_bwd / (double)total);
	}

	if (st->cross_4k > 0) {
		fprintf(fp, "\n%8s: %5.1f%%",
			"CROSS_4K",
			100.0 * (double)st->cross_4k / (double)total);
	}

	if (st->cross_2m > 0) {
		fprintf(fp, "\n%8s: %5.1f%%",
			"CROSS_2M",
			100.0 * (double)st->cross_2m / (double)total);
	}

	for (i = 0; i < PERF_BR_MAX; i++) {
		if (st->counts[i] > 0)
			fprintf(fp, "\n%8s: %5.1f%%",
				branch_type_name(i),
				100.0 *
				(double)st->counts[i] / (double)total);
	}

	for (i = 0; i < PERF_BR_NEW_MAX; i++) {
		if (st->new_counts[i] > 0)
			fprintf(fp, "\n%8s: %5.1f%%",
				branch_new_type_name(i),
				100.0 *
				(double)st->new_counts[i] / (double)total);
	}

}

static int count_str_scnprintf(int idx, const char *str, char *bf, int size)
{
	return scnprintf(bf, size, "%s%s", (idx) ? " " : " (", str);
}

int branch_type_str(const struct branch_type_stat *st, char *bf, int size)
{
	int i, j = 0, printed = 0;
	u64 total = 0;

	for (i = 0; i < PERF_BR_MAX; i++)
		total += st->counts[i];

	for (i = 0; i < PERF_BR_NEW_MAX; i++)
		total += st->new_counts[i];

	if (total == 0)
		return 0;

	if (st->cond_fwd > 0)
		printed += count_str_scnprintf(j++, "COND_FWD", bf + printed, size - printed);

	if (st->cond_bwd > 0)
		printed += count_str_scnprintf(j++, "COND_BWD", bf + printed, size - printed);

	for (i = 0; i < PERF_BR_MAX; i++) {
		if (i == PERF_BR_COND)
			continue;

		if (st->counts[i] > 0)
			printed += count_str_scnprintf(j++, branch_type_name(i), bf + printed, size - printed);
	}

	for (i = 0; i < PERF_BR_NEW_MAX; i++) {
		if (st->new_counts[i] > 0)
			printed += count_str_scnprintf(j++, branch_new_type_name(i), bf + printed, size - printed);
	}

	if (st->cross_4k > 0)
		printed += count_str_scnprintf(j++, "CROSS_4K", bf + printed, size - printed);

	if (st->cross_2m > 0)
		printed += count_str_scnprintf(j++, "CROSS_2M", bf + printed, size - printed);

	return printed;
}

const char *branch_spec_desc(int spec)
{
	const char *branch_spec_outcomes[PERF_BR_SPEC_MAX] = {
		"N/A",
		"SPEC_WRONG_PATH",
		"NON_SPEC_CORRECT_PATH",
		"SPEC_CORRECT_PATH",
	};

	if (spec >= 0 && spec < PERF_BR_SPEC_MAX)
		return branch_spec_outcomes[spec];

	return NULL;
}
