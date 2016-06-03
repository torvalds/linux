#include "unwind.h"
#include "thread.h"

struct unwind_libunwind_ops __weak *local_unwind_libunwind_ops;

static void unwind__register_ops(struct thread *thread,
			  struct unwind_libunwind_ops *ops)
{
	thread->unwind_libunwind_ops = ops;
}

int unwind__prepare_access(struct thread *thread)
{
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
