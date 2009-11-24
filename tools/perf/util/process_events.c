#include "process_events.h"

char	*cwd;
int	cwdlen;

int
process_mmap_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct map *map = map__new(&event->mmap, cwd, cwdlen);
	struct thread *thread = threads__findnew(event->mmap.pid);

	dump_printf("%p [%p]: PERF_RECORD_MMAP %d/%d: [%p(%p) @ %p]: %s\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->mmap.pid,
		event->mmap.tid,
		(void *)(long)event->mmap.start,
		(void *)(long)event->mmap.len,
		(void *)(long)event->mmap.pgoff,
		event->mmap.filename);

	if (thread == NULL || map == NULL) {
		dump_printf("problem processing PERF_RECORD_MMAP, skipping event.\n");
		return 0;
	}

	thread__insert_map(thread, map);
	total_mmap++;

	return 0;
}

int
process_task_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread = threads__findnew(event->fork.pid);
	struct thread *parent = threads__findnew(event->fork.ppid);

	dump_printf("%p [%p]: PERF_RECORD_%s: (%d:%d):(%d:%d)\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->header.type == PERF_RECORD_FORK ? "FORK" : "EXIT",
		event->fork.pid, event->fork.tid,
		event->fork.ppid, event->fork.ptid);

	/*
	 * A thread clone will have the same PID for both
	 * parent and child.
	 */
	if (thread == parent)
		return 0;

	if (event->header.type == PERF_RECORD_EXIT)
		return 0;

	if (!thread || !parent || thread__fork(thread, parent)) {
		dump_printf("problem processing PERF_RECORD_FORK, skipping event.\n");
		return -1;
	}
	total_fork++;

	return 0;
}

