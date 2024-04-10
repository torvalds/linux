/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DSOS
#define __PERF_DSOS

#include <stdbool.h>
#include <stdio.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include "rwsem.h"

struct dso;
struct dso_id;
struct kmod_path;
struct machine;

/*
 * DSOs are put into both a list for fast iteration and rbtree for fast
 * long name lookup.
 */
struct dsos {
	struct list_head    head;
	struct rb_root	    root;	/* rbtree root sorted by long name */
	struct rw_semaphore lock;
};

void dsos__init(struct dsos *dsos);
void dsos__exit(struct dsos *dsos);

void __dsos__add(struct dsos *dsos, struct dso *dso);
void dsos__add(struct dsos *dsos, struct dso *dso);
struct dso *__dsos__addnew(struct dsos *dsos, const char *name);
struct dso *dsos__find(struct dsos *dsos, const char *name, bool cmp_short);

struct dso *dsos__findnew_id(struct dsos *dsos, const char *name, struct dso_id *id);
 
bool dsos__read_build_ids(struct dsos *dsos, bool with_hits);

struct dso *__dsos__findnew_link_by_longname_id(struct rb_root *root, struct dso *dso,
						const char *name, struct dso_id *id);

size_t dsos__fprintf_buildid(struct dsos *dsos, FILE *fp,
			       bool (skip)(struct dso *dso, int parm), int parm);
size_t dsos__fprintf(struct dsos *dsos, FILE *fp);

int dsos__hit_all(struct dsos *dsos);

struct dso *dsos__findnew_module_dso(struct dsos *dsos, struct machine *machine,
				     struct kmod_path *m, const char *filename);

struct dso *dsos__find_kernel_dso(struct dsos *dsos);

int dsos__for_each_dso(struct dsos *dsos, int (*cb)(struct dso *dso, void *data), void *data);

#endif /* __PERF_DSOS */
