#include "../perf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "session.h"
#include "thread.h"
#include "util.h"
#include "debug.h"

struct thread *thread__new(pid_t pid)
{
	struct thread *self = zalloc(sizeof(*self));

	if (self != NULL) {
		map_groups__init(&self->mg);
		self->pid = pid;
		self->ppid = -1;
		self->comm = malloc(32);
		if (self->comm)
			snprintf(self->comm, 32, ":%d", self->pid);
	}

	return self;
}

void thread__delete(struct thread *self)
{
	map_groups__exit(&self->mg);
	free(self->comm);
	free(self);
}

int thread__set_comm(struct thread *self, const char *comm)
{
	int err;

	if (self->comm)
		free(self->comm);
	self->comm = strdup(comm);
	err = self->comm == NULL ? -ENOMEM : 0;
	if (!err) {
		self->comm_set = true;
	}
	return err;
}

int thread__comm_len(struct thread *self)
{
	if (!self->comm_len) {
		if (!self->comm)
			return 0;
		self->comm_len = strlen(self->comm);
	}

	return self->comm_len;
}

size_t thread__fprintf(struct thread *thread, FILE *fp)
{
	return fprintf(fp, "Thread %d %s\n", thread->pid, thread->comm) +
	       map_groups__fprintf(&thread->mg, verbose, fp);
}

void thread__insert_map(struct thread *self, struct map *map)
{
	map_groups__fixup_overlappings(&self->mg, map, verbose, stderr);
	map_groups__insert(&self->mg, map);
}

int thread__fork(struct thread *self, struct thread *parent)
{
	int i;

	if (parent->comm_set) {
		if (self->comm)
			free(self->comm);
		self->comm = strdup(parent->comm);
		if (!self->comm)
			return -ENOMEM;
		self->comm_set = true;
	}

	for (i = 0; i < MAP__NR_TYPES; ++i)
		if (map_groups__clone(&self->mg, &parent->mg, i) < 0)
			return -ENOMEM;

	self->ppid = parent->pid;

	return 0;
}
