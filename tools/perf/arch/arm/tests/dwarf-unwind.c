#include <string.h>
#include "perf_regs.h"
#include "thread.h"
#include "map.h"
#include "event.h"
#include "debug.h"
#include "tests/tests.h"

#define STACK_SIZE 8192

static int sample_ustack(struct perf_sample *sample,
			 struct thread *thread, u64 *regs)
{
	struct stack_dump *stack = &sample->user_stack;
	struct map *map;
	unsigned long sp;
	u64 stack_size, *buf;

	buf = malloc(STACK_SIZE);
	if (!buf) {
		pr_debug("failed to allocate sample uregs data\n");
		return -1;
	}

	sp = (unsigned long) regs[PERF_REG_ARM_SP];

	map = map_groups__find(thread->mg, MAP__VARIABLE, (u64) sp);
	if (!map) {
		pr_debug("failed to get stack map\n");
		free(buf);
		return -1;
	}

	stack_size = map->end - sp;
	stack_size = stack_size > STACK_SIZE ? STACK_SIZE : stack_size;

	memcpy(buf, (void *) sp, stack_size);
	stack->data = (char *) buf;
	stack->size = stack_size;
	return 0;
}

int test__arch_unwind_sample(struct perf_sample *sample,
			     struct thread *thread)
{
	struct regs_dump *regs = &sample->user_regs;
	u64 *buf;

	buf = calloc(1, sizeof(u64) * PERF_REGS_MAX);
	if (!buf) {
		pr_debug("failed to allocate sample uregs data\n");
		return -1;
	}

	perf_regs_load(buf);
	regs->abi  = PERF_SAMPLE_REGS_ABI;
	regs->regs = buf;
	regs->mask = PERF_REGS_MASK;

	return sample_ustack(sample, thread, buf);
}
