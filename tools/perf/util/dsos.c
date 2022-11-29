// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "dsos.h"
#include "dso.h"
#include "vdso.h"
#include "namespaces.h"
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <symbol.h> // filename__read_build_id

static int __dso_id__cmp(struct dso_id *a, struct dso_id *b)
{
	if (a->maj > b->maj) return -1;
	if (a->maj < b->maj) return 1;

	if (a->min > b->min) return -1;
	if (a->min < b->min) return 1;

	if (a->ino > b->ino) return -1;
	if (a->ino < b->ino) return 1;

	/*
	 * Synthesized MMAP events have zero ino_generation, avoid comparing
	 * them with MMAP events with actual ino_generation.
	 *
	 * I found it harmful because the mismatch resulted in a new
	 * dso that did not have a build ID whereas the original dso did have a
	 * build ID. The build ID was essential because the object was not found
	 * otherwise. - Adrian
	 */
	if (a->ino_generation && b->ino_generation) {
		if (a->ino_generation > b->ino_generation) return -1;
		if (a->ino_generation < b->ino_generation) return 1;
	}

	return 0;
}

static bool dso_id__empty(struct dso_id *id)
{
	if (!id)
		return true;

	return !id->maj && !id->min && !id->ino && !id->ino_generation;
}

static void dso__inject_id(struct dso *dso, struct dso_id *id)
{
	dso->id.maj = id->maj;
	dso->id.min = id->min;
	dso->id.ino = id->ino;
	dso->id.ino_generation = id->ino_generation;
}

static int dso_id__cmp(struct dso_id *a, struct dso_id *b)
{
	/*
	 * The second is always dso->id, so zeroes if not set, assume passing
	 * NULL for a means a zeroed id
	 */
	if (dso_id__empty(a) || dso_id__empty(b))
		return 0;

	return __dso_id__cmp(a, b);
}

int dso__cmp_id(struct dso *a, struct dso *b)
{
	return __dso_id__cmp(&a->id, &b->id);
}

bool __dsos__read_build_ids(struct list_head *head, bool with_hits)
{
	bool have_build_id = false;
	struct dso *pos;
	struct nscookie nsc;

	list_for_each_entry(pos, head, node) {
		if (with_hits && !pos->hit && !dso__is_vdso(pos))
			continue;
		if (pos->has_build_id) {
			have_build_id = true;
			continue;
		}
		nsinfo__mountns_enter(pos->nsinfo, &nsc);
		if (filename__read_build_id(pos->long_name, &pos->bid) > 0) {
			have_build_id	  = true;
			pos->has_build_id = true;
		}
		nsinfo__mountns_exit(&nsc);
	}

	return have_build_id;
}

static int __dso__cmp_long_name(const char *long_name, struct dso_id *id, struct dso *b)
{
	int rc = strcmp(long_name, b->long_name);
	return rc ?: dso_id__cmp(id, &b->id);
}

static int __dso__cmp_short_name(const char *short_name, struct dso_id *id, struct dso *b)
{
	int rc = strcmp(short_name, b->short_name);
	return rc ?: dso_id__cmp(id, &b->id);
}

static int dso__cmp_short_name(struct dso *a, struct dso *b)
{
	return __dso__cmp_short_name(a->short_name, &a->id, b);
}

/*
 * Find a matching entry and/or link current entry to RB tree.
 * Either one of the dso or name parameter must be non-NULL or the
 * function will not work.
 */
struct dso *__dsos__findnew_link_by_longname_id(struct rb_root *root, struct dso *dso,
						const char *name, struct dso_id *id)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node  *parent = NULL;

	if (!name)
		name = dso->long_name;
	/*
	 * Find node with the matching name
	 */
	while (*p) {
		struct dso *this = rb_entry(*p, struct dso, rb_node);
		int rc = __dso__cmp_long_name(name, id, this);

		parent = *p;
		if (rc == 0) {
			/*
			 * In case the new DSO is a duplicate of an existing
			 * one, print a one-time warning & put the new entry
			 * at the end of the list of duplicates.
			 */
			if (!dso || (dso == this))
				return this;	/* Find matching dso */
			/*
			 * The core kernel DSOs may have duplicated long name.
			 * In this case, the short name should be different.
			 * Comparing the short names to differentiate the DSOs.
			 */
			rc = dso__cmp_short_name(dso, this);
			if (rc == 0) {
				pr_err("Duplicated dso name: %s\n", name);
				return NULL;
			}
		}
		if (rc < 0)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}
	if (dso) {
		/* Add new node and rebalance tree */
		rb_link_node(&dso->rb_node, parent, p);
		rb_insert_color(&dso->rb_node, root);
		dso->root = root;
	}
	return NULL;
}

void __dsos__add(struct dsos *dsos, struct dso *dso)
{
	list_add_tail(&dso->node, &dsos->head);
	__dsos__findnew_link_by_longname_id(&dsos->root, dso, NULL, &dso->id);
	/*
	 * It is now in the linked list, grab a reference, then garbage collect
	 * this when needing memory, by looking at LRU dso instances in the
	 * list with atomic_read(&dso->refcnt) == 1, i.e. no references
	 * anywhere besides the one for the list, do, under a lock for the
	 * list: remove it from the list, then a dso__put(), that probably will
	 * be the last and will then call dso__delete(), end of life.
	 *
	 * That, or at the end of the 'struct machine' lifetime, when all
	 * 'struct dso' instances will be removed from the list, in
	 * dsos__exit(), if they have no other reference from some other data
	 * structure.
	 *
	 * E.g.: after processing a 'perf.data' file and storing references
	 * to objects instantiated while processing events, we will have
	 * references to the 'thread', 'map', 'dso' structs all from 'struct
	 * hist_entry' instances, but we may not need anything not referenced,
	 * so we might as well call machines__exit()/machines__delete() and
	 * garbage collect it.
	 */
	dso__get(dso);
}

void dsos__add(struct dsos *dsos, struct dso *dso)
{
	down_write(&dsos->lock);
	__dsos__add(dsos, dso);
	up_write(&dsos->lock);
}

static struct dso *__dsos__findnew_by_longname_id(struct rb_root *root, const char *name, struct dso_id *id)
{
	return __dsos__findnew_link_by_longname_id(root, NULL, name, id);
}

static struct dso *__dsos__find_id(struct dsos *dsos, const char *name, struct dso_id *id, bool cmp_short)
{
	struct dso *pos;

	if (cmp_short) {
		list_for_each_entry(pos, &dsos->head, node)
			if (__dso__cmp_short_name(name, id, pos) == 0)
				return pos;
		return NULL;
	}
	return __dsos__findnew_by_longname_id(&dsos->root, name, id);
}

struct dso *__dsos__find(struct dsos *dsos, const char *name, bool cmp_short)
{
	return __dsos__find_id(dsos, name, NULL, cmp_short);
}

static void dso__set_basename(struct dso *dso)
{
	char *base, *lname;
	int tid;

	if (sscanf(dso->long_name, "/tmp/perf-%d.map", &tid) == 1) {
		if (asprintf(&base, "[JIT] tid %d", tid) < 0)
			return;
	} else {
	      /*
	       * basename() may modify path buffer, so we must pass
               * a copy.
               */
		lname = strdup(dso->long_name);
		if (!lname)
			return;

		/*
		 * basename() may return a pointer to internal
		 * storage which is reused in subsequent calls
		 * so copy the result.
		 */
		base = strdup(basename(lname));

		free(lname);

		if (!base)
			return;
	}
	dso__set_short_name(dso, base, true);
}

static struct dso *__dsos__addnew_id(struct dsos *dsos, const char *name, struct dso_id *id)
{
	struct dso *dso = dso__new_id(name, id);

	if (dso != NULL) {
		__dsos__add(dsos, dso);
		dso__set_basename(dso);
		/* Put dso here because __dsos_add already got it */
		dso__put(dso);
	}
	return dso;
}

struct dso *__dsos__addnew(struct dsos *dsos, const char *name)
{
	return __dsos__addnew_id(dsos, name, NULL);
}

static struct dso *__dsos__findnew_id(struct dsos *dsos, const char *name, struct dso_id *id)
{
	struct dso *dso = __dsos__find_id(dsos, name, id, false);

	if (dso && dso_id__empty(&dso->id) && !dso_id__empty(id))
		dso__inject_id(dso, id);

	return dso ? dso : __dsos__addnew_id(dsos, name, id);
}

struct dso *dsos__findnew_id(struct dsos *dsos, const char *name, struct dso_id *id)
{
	struct dso *dso;
	down_write(&dsos->lock);
	dso = dso__get(__dsos__findnew_id(dsos, name, id));
	up_write(&dsos->lock);
	return dso;
}

size_t __dsos__fprintf_buildid(struct list_head *head, FILE *fp,
			       bool (skip)(struct dso *dso, int parm), int parm)
{
	struct dso *pos;
	size_t ret = 0;

	list_for_each_entry(pos, head, node) {
		char sbuild_id[SBUILD_ID_SIZE];

		if (skip && skip(pos, parm))
			continue;
		build_id__sprintf(&pos->bid, sbuild_id);
		ret += fprintf(fp, "%-40s %s\n", sbuild_id, pos->long_name);
	}
	return ret;
}

size_t __dsos__fprintf(struct list_head *head, FILE *fp)
{
	struct dso *pos;
	size_t ret = 0;

	list_for_each_entry(pos, head, node) {
		ret += dso__fprintf(pos, fp);
	}

	return ret;
}
