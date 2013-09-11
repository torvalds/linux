#include "../perf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "session.h"
#include "thread.h"
#include "util.h"
#include "debug.h"

struct thread *thread__new(pid_t pid, pid_t tid)
{
	struct thread *thread = zalloc(sizeof(*thread));

	if (thread != NULL) {
		map_groups__init(&thread->mg);
		thread->pid_ = pid;
		thread->tid = tid;
		thread->ppid = -1;
		thread->comm = malloc(32);
		if (thread->comm)
			snprintf(thread->comm, 32, ":%d", thread->tid);
	}

	return thread;
}

void thread__delete(struct thread *thread)
{
	map_groups__exit(&thread->mg);
	free(thread->comm);
	free(thread);
}

int thread__set_comm(struct thread *thread, const char *comm,
		     u64 timestamp __maybe_unused)
{
	int err;

	if (thread->comm)
		free(thread->comm);
	thread->comm = strdup(comm);
	err = thread->comm == NULL ? -ENOMEM : 0;
	if (!err) {
		thread->comm_set = true;
	}
	return err;
}

const char *thread__comm_str(const struct thread *thread)
{
	return thread->comm;
}

int thread__comm_len(struct thread *thread)
{
	if (!thread->comm_len) {
		if (!thread->comm)
			return 0;
		thread->comm_len = strlen(thread->comm);
	}

	return thread->comm_len;
}

size_t thread__fprintf(struct thread *thread, FILE *fp)
{
	return fprintf(fp, "Thread %d %s\n", thread->tid, thread__comm_str(thread)) +
	       map_groups__fprintf(&thread->mg, verbose, fp);
}

void thread__insert_map(struct thread *thread, struct map *map)
{
	map_groups__fixup_overlappings(&thread->mg, map, verbose, stderr);
	map_groups__insert(&thread->mg, map);
}

int thread__fork(struct thread *thread, struct thread *parent,
		 u64 timestamp __maybe_unused)
{
	int i;

	if (parent->comm_set) {
		if (thread->comm)
			free(thread->comm);
		thread->comm = strdup(parent->comm);
		if (!thread->comm)
			return -ENOMEM;
		thread->comm_set = true;
	}

	for (i = 0; i < MAP__NR_TYPES; ++i)
		if (map_groups__clone(&thread->mg, &parent->mg, i) < 0)
			return -ENOMEM;

	thread->ppid = parent->tid;

	return 0;
}
