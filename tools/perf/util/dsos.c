// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "dsos.h"
#include "dso.h"
#include "util.h"
#include "vdso.h"
#include "namespaces.h"
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <symbol.h> // filename__read_build_id
#include <unistd.h>

void dsos__init(struct dsos *dsos)
{
	INIT_LIST_HEAD(&dsos->head);
	dsos->root = RB_ROOT;
	init_rwsem(&dsos->lock);
}

static void dsos__purge(struct dsos *dsos)
{
	struct dso *pos, *n;

	down_write(&dsos->lock);

	list_for_each_entry_safe(pos, n, &dsos->head, node) {
		RB_CLEAR_NODE(&pos->rb_node);
		pos->root = NULL;
		list_del_init(&pos->node);
		dso__put(pos);
	}

	up_write(&dsos->lock);
}

void dsos__exit(struct dsos *dsos)
{
	dsos__purge(dsos);
	exit_rwsem(&dsos->lock);
}

bool __dsos__read_build_ids(struct dsos *dsos, bool with_hits)
{
	struct list_head *head = &dsos->head;
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
		} else if (errno == ENOENT && pos->nsinfo) {
			char *new_name = dso__filename_with_chroot(pos, pos->long_name);

			if (new_name && filename__read_build_id(new_name,
								&pos->bid) > 0) {
				have_build_id = true;
				pos->has_build_id = true;
			}
			free(new_name);
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
				return dso__get(this);	/* Find matching dso */
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
				return dso__get(pos);
		return NULL;
	}
	return __dsos__findnew_by_longname_id(&dsos->root, name, id);
}

struct dso *dsos__find(struct dsos *dsos, const char *name, bool cmp_short)
{
	struct dso *res;

	down_read(&dsos->lock);
	res = __dsos__find_id(dsos, name, NULL, cmp_short);
	up_read(&dsos->lock);
	return res;
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
	dso = __dsos__findnew_id(dsos, name, id);
	up_write(&dsos->lock);
	return dso;
}

size_t __dsos__fprintf_buildid(struct dsos *dsos, FILE *fp,
			       bool (skip)(struct dso *dso, int parm), int parm)
{
	struct list_head *head = &dsos->head;
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

size_t __dsos__fprintf(struct dsos *dsos, FILE *fp)
{
	struct list_head *head = &dsos->head;
	struct dso *pos;
	size_t ret = 0;

	list_for_each_entry(pos, head, node) {
		ret += dso__fprintf(pos, fp);
	}

	return ret;
}

int __dsos__hit_all(struct dsos *dsos)
{
	struct list_head *head = &dsos->head;
	struct dso *pos;

	list_for_each_entry(pos, head, node)
		pos->hit = true;

	return 0;
}

struct dso *dsos__findnew_module_dso(struct dsos *dsos,
				     struct machine *machine,
				     struct kmod_path *m,
				     const char *filename)
{
	struct dso *dso;

	down_write(&dsos->lock);

	dso = __dsos__find_id(dsos, m->name, NULL, /*cmp_short=*/true);
	if (!dso) {
		dso = __dsos__addnew(dsos, m->name);
		if (dso == NULL)
			goto out_unlock;

		dso__set_module_info(dso, m, machine);
		dso__set_long_name(dso, strdup(filename), true);
		dso->kernel = DSO_SPACE__KERNEL;
	}

out_unlock:
	up_write(&dsos->lock);
	return dso;
}

struct dso *dsos__find_kernel_dso(struct dsos *dsos)
{
	struct dso *dso, *res = NULL;

	down_read(&dsos->lock);
	list_for_each_entry(dso, &dsos->head, node) {
		/*
		 * The cpumode passed to is_kernel_module is not the cpumode of
		 * *this* event. If we insist on passing correct cpumode to
		 * is_kernel_module, we should record the cpumode when we adding
		 * this dso to the linked list.
		 *
		 * However we don't really need passing correct cpumode.  We
		 * know the correct cpumode must be kernel mode (if not, we
		 * should not link it onto kernel_dsos list).
		 *
		 * Therefore, we pass PERF_RECORD_MISC_CPUMODE_UNKNOWN.
		 * is_kernel_module() treats it as a kernel cpumode.
		 */
		if (!dso->kernel ||
		    is_kernel_module(dso->long_name,
				     PERF_RECORD_MISC_CPUMODE_UNKNOWN))
			continue;

		res = dso__get(dso);
		break;
	}
	up_read(&dsos->lock);
	return res;
}

int dsos__for_each_dso(struct dsos *dsos, int (*cb)(struct dso *dso, void *data), void *data)
{
	struct dso *dso;

	down_read(&dsos->lock);
	list_for_each_entry(dso, &dsos->head, node) {
		int err;

		err = cb(dso, data);
		if (err)
			return err;
	}
	up_read(&dsos->lock);
	return 0;
}
