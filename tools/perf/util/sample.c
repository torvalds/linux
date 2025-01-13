/* SPDX-License-Identifier: GPL-2.0 */
#include "sample.h"
#include "debug.h"
#include <linux/zalloc.h>
#include <stdlib.h>
#include <string.h>

void perf_sample__init(struct perf_sample *sample, bool all)
{
	if (all) {
		memset(sample, 0, sizeof(*sample));
	} else {
		sample->user_regs = NULL;
		sample->intr_regs = NULL;
	}
}

void perf_sample__exit(struct perf_sample *sample)
{
	free(sample->user_regs);
	free(sample->intr_regs);
}

struct regs_dump *perf_sample__user_regs(struct perf_sample *sample)
{
	if (!sample->user_regs) {
		sample->user_regs = zalloc(sizeof(*sample->user_regs));
		if (!sample->user_regs)
			pr_err("Failure to allocate sample user_regs");
	}
	return sample->user_regs;
}


struct regs_dump *perf_sample__intr_regs(struct perf_sample *sample)
{
	if (!sample->intr_regs) {
		sample->intr_regs = zalloc(sizeof(*sample->intr_regs));
		if (!sample->intr_regs)
			pr_err("Failure to allocate sample intr_regs");
	}
	return sample->intr_regs;
}
