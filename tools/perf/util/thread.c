#include "../perf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "thread.h"
#include "util.h"
#include "debug.h"

static struct rb_root threads;
static struct thread *last_match;

void thread__init(struct thread *self, pid_t pid)
{
	int i;
	self->pid = pid;
	self->comm = NULL;
	for (i = 0; i < MAP__NR_TYPES; ++i) {
		self->maps[i] = RB_ROOT;
		INIT_LIST_HEAD(&self->removed_maps[i]);
	}
}

static struct thread *thread__new(pid_t pid)
{
	struct thread *self = zalloc(sizeof(*self));

	if (self != NULL) {
		thread__init(self, pid);
		self->comm = malloc(32);
		if (self->comm)
			snprintf(self->comm, 32, ":%d", self->pid);
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

int thread__comm_len(struct thread *self)
{
	if (!self->comm_len) {
		if (!self->comm)
			return 0;
		self->comm_len = strlen(self->comm);
	}

	return self->comm_len;
}

static const char *map_type__name[MAP__NR_TYPES] = {
	[MAP__FUNCTION] = "Functions",
};

static size_t __thread__fprintf_maps(struct thread *self,
				     enum map_type type, FILE *fp)
{
	size_t printed = fprintf(fp, "%s:\n", map_type__name[type]);
	struct rb_node *nd;

	for (nd = rb_first(&self->maps[type]); nd; nd = rb_next(nd)) {
		struct map *pos = rb_entry(nd, struct map, rb_node);
		printed += fprintf(fp, "Map:");
		printed += map__fprintf(pos, fp);
		if (verbose > 1) {
			printed += dso__fprintf(pos->dso, type, fp);
			printed += fprintf(fp, "--\n");
		}
	}

	return printed;
}

size_t thread__fprintf_maps(struct thread *self, FILE *fp)
{
	size_t printed = 0, i;
	for (i = 0; i < MAP__NR_TYPES; ++i)
		printed += __thread__fprintf_maps(self, i, fp);
	return printed;
}

static size_t __thread__fprintf_removed_maps(struct thread *self,
					     enum map_type type, FILE *fp)
{
	struct map *pos;
	size_t printed = 0;

	list_for_each_entry(pos, &self->removed_maps[type], node) {
		printed += fprintf(fp, "Map:");
		printed += map__fprintf(pos, fp);
		if (verbose > 1) {
			printed += dso__fprintf(pos->dso, type, fp);
			printed += fprintf(fp, "--\n");
		}
	}
	return printed;
}

static size_t thread__fprintf_removed_maps(struct thread *self, FILE *fp)
{
	size_t printed = 0, i;
	for (i = 0; i < MAP__NR_TYPES; ++i)
		printed += __thread__fprintf_removed_maps(self, i, fp);
	return printed;
}

static size_t thread__fprintf(struct thread *self, FILE *fp)
{
	size_t printed = fprintf(fp, "Thread %d %s\n", self->pid, self->comm);
	printed += thread__fprintf_removed_maps(self, fp);
	printed += fprintf(fp, "Removed maps:\n");
	return printed + thread__fprintf_removed_maps(self, fp);
}

struct thread *threads__findnew(pid_t pid)
{
	struct rb_node **p = &threads.rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	/*
	 * Font-end cache - PID lookups come in blocks,
	 * so most of the time we dont have to look up
	 * the full rbtree:
	 */
	if (last_match && last_match->pid == pid)
		return last_match;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid) {
			last_match = th;
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
		rb_insert_color(&th->rb_node, &threads);
		last_match = th;
	}

	return th;
}

struct thread *register_idle_thread(void)
{
	struct thread *thread = threads__findnew(0);

	if (!thread || thread__set_comm(thread, "swapper")) {
		fprintf(stderr, "problem inserting idle task.\n");
		exit(-1);
	}

	return thread;
}

static void thread__remove_overlappings(struct thread *self, struct map *map)
{
	struct rb_root *root = &self->maps[map->type];
	struct rb_node *next = rb_first(root);

	while (next) {
		struct map *pos = rb_entry(next, struct map, rb_node);
		next = rb_next(&pos->rb_node);

		if (!map__overlap(pos, map))
			continue;

		if (verbose >= 2) {
			fputs("overlapping maps:\n", stderr);
			map__fprintf(map, stderr);
			map__fprintf(pos, stderr);
		}

		rb_erase(&pos->rb_node, root);
		/*
		 * We may have references to this map, for instance in some
		 * hist_entry instances, so just move them to a separate
		 * list.
		 */
		list_add_tail(&pos->node, &self->removed_maps[map->type]);
	}
}

void maps__insert(struct rb_root *maps, struct map *map)
{
	struct rb_node **p = &maps->rb_node;
	struct rb_node *parent = NULL;
	const u64 ip = map->start;
	struct map *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map, rb_node);
		if (ip < m->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&map->rb_node, parent, p);
	rb_insert_color(&map->rb_node, maps);
}

struct map *maps__find(struct rb_root *maps, u64 ip)
{
	struct rb_node **p = &maps->rb_node;
	struct rb_node *parent = NULL;
	struct map *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map, rb_node);
		if (ip < m->start)
			p = &(*p)->rb_left;
		else if (ip > m->end)
			p = &(*p)->rb_right;
		else
			return m;
	}

	return NULL;
}

void thread__insert_map(struct thread *self, struct map *map)
{
	thread__remove_overlappings(self, map);
	maps__insert(&self->maps[map->type], map);
}

static int thread__clone_maps(struct thread *self, struct thread *parent,
			      enum map_type type)
{
	struct rb_node *nd;
	for (nd = rb_first(&parent->maps[type]); nd; nd = rb_next(nd)) {
		struct map *map = rb_entry(nd, struct map, rb_node);
		struct map *new = map__clone(map);
		if (new == NULL)
			return -ENOMEM;
		thread__insert_map(self, new);
	}
	return 0;
}

int thread__fork(struct thread *self, struct thread *parent)
{
	int i;

	if (self->comm)
		free(self->comm);
	self->comm = strdup(parent->comm);
	if (!self->comm)
		return -ENOMEM;

	for (i = 0; i < MAP__NR_TYPES; ++i)
		if (thread__clone_maps(self, parent, i) < 0)
			return -ENOMEM;
	return 0;
}

size_t threads__fprintf(FILE *fp)
{
	size_t ret = 0;
	struct rb_node *nd;

	for (nd = rb_first(&threads); nd; nd = rb_next(nd)) {
		struct thread *pos = rb_entry(nd, struct thread, rb_node);

		ret += thread__fprintf(pos, fp);
	}

	return ret;
}

struct symbol *thread__find_symbol(struct thread *self,
				   enum map_type type, u64 addr,
				   symbol_filter_t filter)
{
	struct map *map = thread__find_map(self, type, addr);

	if (map != NULL)
		return map__find_symbol(map, map->map_ip(map, addr), filter);

	return NULL;
}
