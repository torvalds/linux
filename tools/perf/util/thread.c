#include "../perf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "session.h"
#include "thread.h"
#include "util.h"
#include "debug.h"

void map_groups__init(struct map_groups *self)
{
	int i;
	for (i = 0; i < MAP__NR_TYPES; ++i) {
		self->maps[i] = RB_ROOT;
		INIT_LIST_HEAD(&self->removed_maps[i]);
	}
}

static struct thread *thread__new(pid_t pid)
{
	struct thread *self = zalloc(sizeof(*self));

	if (self != NULL) {
		map_groups__init(&self->mg);
		self->pid = pid;
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
	if (self->comm == NULL)
		return -ENOMEM;
	self->comm_set = true;
	return 0;
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
	[MAP__VARIABLE] = "Variables",
};

static size_t __map_groups__fprintf_maps(struct map_groups *self,
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

size_t map_groups__fprintf_maps(struct map_groups *self, FILE *fp)
{
	size_t printed = 0, i;
	for (i = 0; i < MAP__NR_TYPES; ++i)
		printed += __map_groups__fprintf_maps(self, i, fp);
	return printed;
}

static size_t __map_groups__fprintf_removed_maps(struct map_groups *self,
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

static size_t map_groups__fprintf_removed_maps(struct map_groups *self, FILE *fp)
{
	size_t printed = 0, i;
	for (i = 0; i < MAP__NR_TYPES; ++i)
		printed += __map_groups__fprintf_removed_maps(self, i, fp);
	return printed;
}

static size_t map_groups__fprintf(struct map_groups *self, FILE *fp)
{
	size_t printed = map_groups__fprintf_maps(self, fp);
	printed += fprintf(fp, "Removed maps:\n");
	return printed + map_groups__fprintf_removed_maps(self, fp);
}

static size_t thread__fprintf(struct thread *self, FILE *fp)
{
	return fprintf(fp, "Thread %d %s\n", self->pid, self->comm) +
	       map_groups__fprintf(&self->mg, fp);
}

struct thread *perf_session__findnew(struct perf_session *self, pid_t pid)
{
	struct rb_node **p = &self->threads.rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	/*
	 * Font-end cache - PID lookups come in blocks,
	 * so most of the time we dont have to look up
	 * the full rbtree:
	 */
	if (self->last_match && self->last_match->pid == pid)
		return self->last_match;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid) {
			self->last_match = th;
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
		rb_insert_color(&th->rb_node, &self->threads);
		self->last_match = th;
	}

	return th;
}

static void map_groups__remove_overlappings(struct map_groups *self,
					    struct map *map)
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
	map_groups__remove_overlappings(&self->mg, map);
	map_groups__insert(&self->mg, map);
}

/*
 * XXX This should not really _copy_ te maps, but refcount them.
 */
static int map_groups__clone(struct map_groups *self,
			     struct map_groups *parent, enum map_type type)
{
	struct rb_node *nd;
	for (nd = rb_first(&parent->maps[type]); nd; nd = rb_next(nd)) {
		struct map *map = rb_entry(nd, struct map, rb_node);
		struct map *new = map__clone(map);
		if (new == NULL)
			return -ENOMEM;
		map_groups__insert(self, new);
	}
	return 0;
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
	return 0;
}

size_t perf_session__fprintf(struct perf_session *self, FILE *fp)
{
	size_t ret = 0;
	struct rb_node *nd;

	for (nd = rb_first(&self->threads); nd; nd = rb_next(nd)) {
		struct thread *pos = rb_entry(nd, struct thread, rb_node);

		ret += thread__fprintf(pos, fp);
	}

	return ret;
}

struct symbol *map_groups__find_symbol(struct map_groups *self,
				       enum map_type type, u64 addr,
				       symbol_filter_t filter)
{
	struct map *map = map_groups__find(self, type, addr);

	if (map != NULL)
		return map__find_symbol(map, map->map_ip(map, addr), filter);

	return NULL;
}
