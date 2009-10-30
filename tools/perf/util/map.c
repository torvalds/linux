#include "event.h"
#include "symbol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "debug.h"

static inline int is_anon_memory(const char *filename)
{
	return strcmp(filename, "//anon") == 0;
}

static int strcommon(const char *pathname, char *cwd, int cwdlen)
{
	int n = 0;

	while (n < cwdlen && pathname[n] == cwd[n])
		++n;

	return n;
}

void map__init(struct map *self, u64 start, u64 end, u64 pgoff,
	       struct dso *dso)
{
	self->start    = start;
	self->end      = end;
	self->pgoff    = pgoff;
	self->dso      = dso;
	self->map_ip   = map__map_ip;
	self->unmap_ip = map__unmap_ip;
	RB_CLEAR_NODE(&self->rb_node);
}

struct map *map__new(struct mmap_event *event, char *cwd, int cwdlen)
{
	struct map *self = malloc(sizeof(*self));

	if (self != NULL) {
		const char *filename = event->filename;
		char newfilename[PATH_MAX];
		struct dso *dso;
		int anon;

		if (cwd) {
			int n = strcommon(filename, cwd, cwdlen);

			if (n == cwdlen) {
				snprintf(newfilename, sizeof(newfilename),
					 ".%s", filename + n);
				filename = newfilename;
			}
		}

		anon = is_anon_memory(filename);

		if (anon) {
			snprintf(newfilename, sizeof(newfilename), "/tmp/perf-%d.map", event->pid);
			filename = newfilename;
		}

		dso = dsos__findnew(filename);
		if (dso == NULL)
			goto out_delete;

		map__init(self, event->start, event->start + event->len,
			  event->pgoff, dso);

		if (self->dso == vdso || anon)
			self->map_ip = self->unmap_ip = identity__map_ip;
	}
	return self;
out_delete:
	free(self);
	return NULL;
}

#define DSO__DELETED "(deleted)"

struct symbol *
map__find_symbol(struct map *self, u64 ip, symbol_filter_t filter)
{
	if (!self->dso->loaded) {
		int nr = dso__load(self->dso, self, filter);

		if (nr < 0) {
			pr_warning("Failed to open %s, continuing without symbols\n",
				   self->dso->long_name);
			return NULL;
		} else if (nr == 0) {
			const char *name = self->dso->long_name;
			const size_t len = strlen(name);
			const size_t real_len = len - sizeof(DSO__DELETED);

			if (len > sizeof(DSO__DELETED) &&
			    strcmp(name + real_len + 1, DSO__DELETED) == 0)
				pr_warning("%.*s was updated, restart the "
					   "long running apps that use it!\n",
					   real_len, name);
			else
				pr_warning("no symbols found in %s, maybe "
					   "install a debug package?\n", name);
			return NULL;
		}
	}

	return self->dso->find_symbol(self->dso, ip);
}

struct map *map__clone(struct map *self)
{
	struct map *map = malloc(sizeof(*self));

	if (!map)
		return NULL;

	memcpy(map, self, sizeof(*self));

	return map;
}

int map__overlap(struct map *l, struct map *r)
{
	if (l->start > r->start) {
		struct map *t = l;
		l = r;
		r = t;
	}

	if (l->end > r->start)
		return 1;

	return 0;
}

size_t map__fprintf(struct map *self, FILE *fp)
{
	return fprintf(fp, " %Lx-%Lx %Lx %s\n",
		       self->start, self->end, self->pgoff, self->dso->name);
}
