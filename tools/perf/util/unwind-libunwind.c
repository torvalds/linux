// SPDX-License-Identifier: GPL-2.0
#include "unwind.h"
#include "dso.h"
#include "map.h"
#include "thread.h"
#include "session.h"
#include "debug.h"
#include "env.h"
#include "callchain.h"

struct unwind_libunwind_ops __weak *local_unwind_libunwind_ops;
struct unwind_libunwind_ops __weak *x86_32_unwind_libunwind_ops;
struct unwind_libunwind_ops __weak *arm64_unwind_libunwind_ops;

static void unwind__register_ops(struct maps *maps, struct unwind_libunwind_ops *ops)
{
	maps->unwind_libunwind_ops = ops;
}

int unwind__prepare_access(struct maps *maps, struct map *map, bool *initialized)
{
	const char *arch;
	enum dso_type dso_type;
	struct unwind_libunwind_ops *ops = local_unwind_libunwind_ops;
	int err;

	if (!dwarf_callchain_users)
		return 0;

	if (maps->addr_space) {
		pr_debug("unwind: thread map already set, dso=%s\n",
			 map->dso->name);
		if (initialized)
			*initialized = true;
		return 0;
	}

	/* env->arch is NULL for live-mode (i.e. perf top) */
	if (!maps->machine->env || !maps->machine->env->arch)
		goto out_register;

	dso_type = dso__type(map->dso, maps->machine);
	if (dso_type == DSO__TYPE_UNKNOWN)
		return 0;

	arch = perf_env__arch(maps->machine->env);

	if (!strcmp(arch, "x86")) {
		if (dso_type != DSO__TYPE_64BIT)
			ops = x86_32_unwind_libunwind_ops;
	} else if (!strcmp(arch, "arm64") || !strcmp(arch, "arm")) {
		if (dso_type == DSO__TYPE_64BIT)
			ops = arm64_unwind_libunwind_ops;
	}

	if (!ops) {
		pr_err("unwind: target platform=%s is not supported\n", arch);
		return 0;
	}
out_register:
	unwind__register_ops(maps, ops);

	err = maps->unwind_libunwind_ops->prepare_access(maps);
	if (initialized)
		*initialized = err ? false : true;
	return err;
}

void unwind__flush_access(struct maps *maps)
{
	if (maps->unwind_libunwind_ops)
		maps->unwind_libunwind_ops->flush_access(maps);
}

void unwind__finish_access(struct maps *maps)
{
	if (maps->unwind_libunwind_ops)
		maps->unwind_libunwind_ops->finish_access(maps);
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			 struct thread *thread,
			 struct perf_sample *data, int max_stack,
			 bool best_effort)
{
	if (thread->maps->unwind_libunwind_ops)
		return thread->maps->unwind_libunwind_ops->get_entries(cb, arg, thread, data,
								       max_stack, best_effort);
	return 0;
}
