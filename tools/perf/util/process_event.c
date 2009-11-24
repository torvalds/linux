#include "process_event.h"

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
process_comm_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread = threads__findnew(event->comm.pid);

	dump_printf("%p [%p]: PERF_RECORD_COMM: %s:%d\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->comm.comm, event->comm.pid);

	if (thread == NULL ||
	    thread__set_comm_adjust(thread, event->comm.comm)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
		return -1;
	}
	total_comm++;

	return 0;
}

