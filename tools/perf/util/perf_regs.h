/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_REGS_H
#define __PERF_REGS_H

#include <linux/types.h>
#include <linux/compiler.h>

struct regs_dump;

struct sample_reg {
	const char *name;
	uint64_t mask;
};
#define SMPL_REG(n, b) { .name = #n, .mask = 1ULL << (b) }
#define SMPL_REG2(n, b) { .name = #n, .mask = 3ULL << (b) }
#define SMPL_REG_END { .name = NULL }

enum {
	SDT_ARG_VALID = 0,
	SDT_ARG_SKIP,
};

int arch_sdt_arg_parse_op(char *old_op, char **new_op);
uint64_t arch__intr_reg_mask(void);
uint64_t arch__user_reg_mask(void);

#ifdef HAVE_PERF_REGS_SUPPORT
extern const struct sample_reg sample_reg_masks[];

#include <perf_regs.h>

#define DWARF_MINIMAL_REGS ((1ULL << PERF_REG_IP) | (1ULL << PERF_REG_SP))

int perf_reg_value(u64 *valp, struct regs_dump *regs, int id);

#else
#define PERF_REGS_MASK	0
#define PERF_REGS_MAX	0

#define DWARF_MINIMAL_REGS PERF_REGS_MASK

static inline const char *perf_reg_name(int id __maybe_unused)
{
	return NULL;
}

static inline int perf_reg_value(u64 *valp __maybe_unused,
				 struct regs_dump *regs __maybe_unused,
				 int id __maybe_unused)
{
	return 0;
}
#endif /* HAVE_PERF_REGS_SUPPORT */
#endif /* __PERF_REGS_H */
