#include "unwind.h"
#include "thread.h"
#include "session.h"
#include "debug.h"
#include "arch/common.h"

struct unwind_libunwind_ops __weak *local_unwind_libunwind_ops;

static void unwind__register_ops(struct thread *thread,
			  struct unwind_libunwind_ops *ops)
{
	thread->unwind_libunwind_ops = ops;
}

int unwind__prepare_access(struct thread *thread, struct map *map)
{
	const char *arch;
	enum dso_type dso_type;

	if (thread->addr_space) {
		pr_debug("unwind: thread map already set, dso=%s\n",
			 map->dso->name);
		return 0;
	}

	/* env->arch is NULL for live-mode (i.e. perf top) */
	if (!thread->mg->machine->env || !thread->mg->machine->env->arch)
		goto out_register;

	dso_type = dso__type(map->dso, thread->mg->machine);
	if (dso_type == DSO__TYPE_UNKNOWN)
		return 0;

	arch = normalize_arch(thread->mg->machine->env->arch);
	pr_debug("unwind: target platform=%s\n", arch);
out_register:
	unwind__register_ops(thread, local_unwind_libunwind_ops);

	return thread->unwind_libunwind_ops->prepare_access(thread);
}

void unwind__flush_access(struct thread *thread)
{
	if (thread->unwind_libunwind_ops)
		thread->unwind_libunwind_ops->flush_access(thread);
}

void unwind__finish_access(struct thread *thread)
{
	if (thread->unwind_libunwind_ops)
		thread->unwind_libunwind_ops->finish_access(thread);
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			 struct thread *thread,
			 struct perf_sample *data, int max_stack)
{
	if (thread->unwind_libunwind_ops)
		return thread->unwind_libunwind_ops->get_entries(cb, arg, thread, data, max_stack);
	return 0;
}
