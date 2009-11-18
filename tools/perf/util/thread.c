#include "../perf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "thread.h"
#include "util.h"
#include "debug.h"

static struct thread *thread__new(pid_t pid)
{
	struct thread *self = calloc(1, sizeof(*self));

	if (self != NULL) {
		self->pid = pid;
		self->comm = malloc(32);
		if (self->comm)
			snprintf(self->comm, 32, ":%d", self->pid);
		INIT_LIST_HEAD(&self->maps);
	}

	return self;
}

int thread__set_comm(struct thread *self, const char *comm)
{
	if (self->comm)
		free(self->comm);
	self->comm = strdup(comm);
	return self->comm ? 0 : -ENOMEM;
}

static size_t thread__fprintf(struct thread *self, FILE *fp)
{
	struct map *pos;
	size_t ret = fprintf(fp, "Thread %d %s\n", self->pid, self->comm);

	list_for_each_entry(pos, &self->maps, node)
		ret += map__fprintf(pos, fp);

	return ret;
}

struct thread *
threads__findnew(pid_t pid, struct rb_root *threads, struct thread **last_match)
{
	struct rb_node **p = &threads->rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	/*
	 * Font-end cache - PID lookups come in blocks,
	 * so most of the time we dont have to look up
	 * the full rbtree:
	 */
	if (*last_match && (*last_match)->pid == pid)
		return *last_match;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid) {
			*last_match = th;
			return th;
		}

		if (pid < th->pid)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	th = thread__new(pid);
	if (th != NULL) {
		rb_link_node(&th->rb_node, parent, p);
		rb_insert_color(&th->rb_node, threads);
		*last_match = th;
	}

	return th;
}

struct thread *
register_idle_thread(struct rb_root *threads, struct thread **last_match)
{
	struct thread *thread = threads__findnew(0, threads, last_match);

	if (!thread || thread__set_comm(thread, "swapper")) {
		fprintf(stderr, "problem inserting idle task.\n");
		exit(-1);
	}

	return thread;
}

void thread__insert_map(struct thread *self, struct map *map)
{
	struct map *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &self->maps, node) {
		if (map__overlap(pos, map)) {
			if (verbose >= 2) {
				printf("overlapping maps:\n");
				map__fprintf(map, stdout);
				map__fprintf(pos, stdout);
			}

			if (map->start <= pos->start && map->end > pos->start)
				pos->start = map->end;

			if (map->end >= pos->end && map->start < pos->end)
				pos->end = map->start;

			if (verbose >= 2) {
				printf("after collision:\n");
				map__fprintf(pos, stdout);
			}

			if (pos->start >= pos->end) {
				list_del_init(&pos->node);
				free(pos);
			}
		}
	}

	list_add_tail(&map->node, &self->maps);
}

int thread__fork(struct thread *self, struct thread *parent)
{
	struct map *map;

	if (self->comm)
		free(self->comm);
	self->comm = strdup(parent->comm);
	if (!self->comm)
		return -ENOMEM;

	list_for_each_entry(map, &parent->maps, node) {
		struct map *new = map__clone(map);
		if (!new)
			return -ENOMEM;
		thread__insert_map(self, new);
	}

	return 0;
}

struct map *thread__find_map(struct thread *self, u64 ip)
{
	struct map *pos;

	if (self == NULL)
		return NULL;

	list_for_each_entry(pos, &self->maps, node)
		if (ip >= pos->start && ip <= pos->end)
			return pos;

	return NULL;
}

size_t threads__fprintf(FILE *fp, struct rb_root *threads)
{
	size_t ret = 0;
	struct rb_node *nd;

	for (nd = rb_first(threads); nd; nd = rb_next(nd)) {
		struct thread *pos = rb_entry(nd, struct thread, rb_node);

		ret += thread__fprintf(pos, fp);
	}

	return ret;
}
