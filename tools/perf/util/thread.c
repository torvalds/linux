#include "../perf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "session.h"
#include "thread.h"
#include "thread-stack.h"
#include "util.h"
#include "debug.h"
#include "comm.h"
#include "unwind.h"

int thread__init_map_groups(struct thread *thread, struct machine *machine)
{
	struct thread *leader;
	pid_t pid = thread->pid_;

	if (pid == thread->tid || pid == -1) {
		thread->mg = map_groups__new(machine);
	} else {
		leader = machine__findnew_thread(machine, pid, pid);
		if (leader)
			thread->mg = map_groups__get(leader->mg);
	}

	return thread->mg ? 0 : -1;
}

struct thread *thread__new(pid_t pid, pid_t tid)
{
	char *comm_str;
	struct comm *comm;
	struct thread *thread = zalloc(sizeof(*thread));

	if (thread != NULL) {
		thread->pid_ = pid;
		thread->tid = tid;
		thread->ppid = -1;
		thread->cpu = -1;
		INIT_LIST_HEAD(&thread->comm_list);

		if (unwind__prepare_access(thread) < 0)
			goto err_thread;

		comm_str = malloc(32);
		if (!comm_str)
			goto err_thread;

		snprintf(comm_str, 32, ":%d", tid);
		comm = comm__new(comm_str, 0, false);
		free(comm_str);
		if (!comm)
			goto err_thread;

		list_add(&comm->list, &thread->comm_list);

	}

	return thread;

err_thread:
	free(thread);
	return NULL;
}

void thread__delete(struct thread *thread)
{
	struct comm *comm, *tmp;

	thread_stack__free(thread);

	if (thread->mg) {
		map_groups__put(thread->mg);
		thread->mg = NULL;
	}
	list_for_each_entry_safe(comm, tmp, &thread->comm_list, list) {
		list_del(&comm->list);
		comm__free(comm);
	}
	unwind__finish_access(thread);

	free(thread);
}

struct thread *thread__get(struct thread *thread)
{
	++thread->refcnt;
	return thread;
}

void thread__put(struct thread *thread)
{
	if (thread && --thread->refcnt == 0) {
		list_del_init(&thread->node);
		thread__delete(thread);
	}
}

struct comm *thread__comm(const struct thread *thread)
{
	if (list_empty(&thread->comm_list))
		return NULL;

	return list_first_entry(&thread->comm_list, struct comm, list);
}

struct comm *thread__exec_comm(const struct thread *thread)
{
	struct comm *comm, *last = NULL;

	list_for_each_entry(comm, &thread->comm_list, list) {
		if (comm->exec)
			return comm;
		last = comm;
	}

	return last;
}

int __thread__set_comm(struct thread *thread, const char *str, u64 timestamp,
		       bool exec)
{
	struct comm *new, *curr = thread__comm(thread);
	int err;

	/* Override the default :tid entry */
	if (!thread->comm_set) {
		err = comm__override(curr, str, timestamp, exec);
		if (err)
			return err;
	} else {
		new = comm__new(str, timestamp, exec);
		if (!new)
			return -ENOMEM;
		list_add(&new->list, &thread->comm_list);

		if (exec)
			unwind__flush_access(thread);
	}

	thread->comm_set = true;

	return 0;
}

const char *thread__comm_str(const struct thread *thread)
{
	const struct comm *comm = thread__comm(thread);

	if (!comm)
		return NULL;

	return comm__str(comm);
}

/* CHECKME: it should probably better return the max comm len from its comm list */
int thread__comm_len(struct thread *thread)
{
	if (!thread->comm_len) {
		const char *comm = thread__comm_str(thread);
		if (!comm)
			return 0;
		thread->comm_len = strlen(comm);
	}

	return thread->comm_len;
}

size_t thread__fprintf(struct thread *thread, FILE *fp)
{
	return fprintf(fp, "Thread %d %s\n", thread->tid, thread__comm_str(thread)) +
	       map_groups__fprintf(thread->mg, fp);
}

void thread__insert_map(struct thread *thread, struct map *map)
{
	map_groups__fixup_overlappings(thread->mg, map, stderr);
	map_groups__insert(thread->mg, map);
}

static int thread__clone_map_groups(struct thread *thread,
				    struct thread *parent)
{
	int i;

	/* This is new thread, we share map groups for process. */
	if (thread->pid_ == parent->pid_)
		return 0;

	/* But this one is new process, copy maps. */
	for (i = 0; i < MAP__NR_TYPES; ++i)
		if (map_groups__clone(thread->mg, parent->mg, i) < 0)
			return -ENOMEM;

	return 0;
}

int thread__fork(struct thread *thread, struct thread *parent, u64 timestamp)
{
	int err;

	if (parent->comm_set) {
		const char *comm = thread__comm_str(parent);
		if (!comm)
			return -ENOMEM;
		err = thread__set_comm(thread, comm, timestamp);
		if (err)
			return err;
	}

	thread->ppid = parent->tid;
	return thread__clone_map_groups(thread, parent);
}

void thread__find_cpumode_addr_location(struct thread *thread,
					enum map_type type, u64 addr,
					struct addr_location *al)
{
	size_t i;
	const u8 const cpumodes[] = {
		PERF_RECORD_MISC_USER,
		PERF_RECORD_MISC_KERNEL,
		PERF_RECORD_MISC_GUEST_USER,
		PERF_RECORD_MISC_GUEST_KERNEL
	};

	for (i = 0; i < ARRAY_SIZE(cpumodes); i++) {
		thread__find_addr_location(thread, cpumodes[i], type, addr, al);
		if (al->map)
			break;
	}
}
