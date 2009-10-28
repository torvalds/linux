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

struct map *map__new(struct mmap_event *event, char *cwd, int cwdlen,
		     unsigned int sym_priv_size)
{
	struct map *self = malloc(sizeof(*self));

	if (self != NULL) {
		const char *filename = event->filename;
		char newfilename[PATH_MAX];
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

		self->start = event->start;
		self->end   = event->start + event->len;
		self->pgoff = event->pgoff;

		self->dso = dsos__findnew(filename, sym_priv_size);
		if (self->dso == NULL)
			goto out_delete;

		if (self->dso == vdso || anon)
			self->map_ip = self->unmap_ip = identity__map_ip;
		else {
			self->map_ip = map__map_ip;
			self->unmap_ip = map__unmap_ip;
		}
	}
	return self;
out_delete:
	free(self);
	return NULL;
}

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
			pr_warning("No symbols found in %s, maybe install a debug package?\n",
				   self->dso->long_name);
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
