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
	RC_CHK_ACCESS(maps)->unwind_libunwind_ops = ops;
}

int unwind__prepare_access(struct maps *maps, struct map *map, bool *initialized)
{
	const char *arch;
	enum dso_type dso_type;
	struct unwind_libunwind_ops *ops = local_unwind_libunwind_ops;
	struct dso *dso = map__dso(map);
	struct machine *machine;
	int err;

	if (!dwarf_callchain_users)
		return 0;

	if (maps__addr_space(maps)) {
		pr_debug("unwind: thread map already set, dso=%s\n", dso->name);
		if (initialized)
			*initialized = true;
		return 0;
	}

	machine = maps__machine(maps);
	/* env->arch is NULL for live-mode (i.e. perf top) */
	if (!machine->env || !machine->env->arch)
		goto out_register;

	dso_type = dso__type(dso, machine);
	if (dso_type == DSO__TYPE_UNKNOWN)
		return 0;

	arch = perf_env__arch(machine->env);

	if (!strcmp(arch, "x86")) {
		if (dso_type != DSO__TYPE_64BIT)
			ops = x86_32_unwind_libunwind_ops;
	} else if (!strcmp(arch, "arm64") || !strcmp(arch, "arm")) {
		if (dso_type == DSO__TYPE_64BIT)
			ops = arm64_unwind_libunwind_ops;
	}

	if (!ops) {
		pr_warning_once("unwind: target platform=%s is not supported\n", arch);
		return 0;
	}
out_register:
	unwind__register_ops(maps, ops);

	err = maps__unwind_libunwind_ops(maps)->prepare_access(maps);
	if (initialized)
		*initialized = err ? false : true;
	return err;
}

void unwind__flush_access(struct maps *maps)
{
	const struct unwind_libunwind_ops *ops = maps__unwind_libunwind_ops(maps);

	if (ops)
		ops->flush_access(maps);
}

void unwind__finish_access(struct maps *maps)
{
	const struct unwind_libunwind_ops *ops = maps__unwind_libunwind_ops(maps);

	if (ops)
		ops->finish_access(maps);
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			 struct thread *thread,
			 struct perf_sample *data, int max_stack,
			 bool best_effort)
{
	const struct unwind_libunwind_ops *ops = maps__unwind_libunwind_ops(thread->maps);

	if (ops)
		return ops->get_entries(cb, arg, thread, data, max_stack, best_effort);
	return 0;
}
