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

static void unwind__register_ops(struct map_groups *mg,
			  struct unwind_libunwind_ops *ops)
{
	mg->unwind_libunwind_ops = ops;
}

int unwind__prepare_access(struct map_groups *mg, struct map *map,
			   bool *initialized)
{
	const char *arch;
	enum dso_type dso_type;
	struct unwind_libunwind_ops *ops = local_unwind_libunwind_ops;
	int err;

	if (!dwarf_callchain_users)
		return 0;

	if (mg->addr_space) {
		pr_debug("unwind: thread map already set, dso=%s\n",
			 map->dso->name);
		if (initialized)
			*initialized = true;
		return 0;
	}

	/* env->arch is NULL for live-mode (i.e. perf top) */
	if (!mg->machine->env || !mg->machine->env->arch)
		goto out_register;

	dso_type = dso__type(map->dso, mg->machine);
	if (dso_type == DSO__TYPE_UNKNOWN)
		return 0;

	arch = perf_env__arch(mg->machine->env);

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
	unwind__register_ops(mg, ops);

	err = mg->unwind_libunwind_ops->prepare_access(mg);
	if (initialized)
		*initialized = err ? false : true;
	return err;
}

void unwind__flush_access(struct map_groups *mg)
{
	if (mg->unwind_libunwind_ops)
		mg->unwind_libunwind_ops->flush_access(mg);
}

void unwind__finish_access(struct map_groups *mg)
{
	if (mg->unwind_libunwind_ops)
		mg->unwind_libunwind_ops->finish_access(mg);
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			 struct thread *thread,
			 struct perf_sample *data, int max_stack)
{
	if (thread->mg->unwind_libunwind_ops)
		return thread->mg->unwind_libunwind_ops->get_entries(cb, arg, thread, data, max_stack);
	return 0;
}
