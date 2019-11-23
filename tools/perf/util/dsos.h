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

/*
 * DSOs are put into both a list for fast iteration and rbtree for fast
 * long name lookup.
 */
struct dsos {
	struct list_head    head;
	struct rb_root	    root;	/* rbtree root sorted by long name */
	struct rw_semaphore lock;
};

void __dsos__add(struct dsos *dsos, struct dso *dso);
void dsos__add(struct dsos *dsos, struct dso *dso);
struct dso *__dsos__addnew(struct dsos *dsos, const char *name);
struct dso *__dsos__find(struct dsos *dsos, const char *name, bool cmp_short);

struct dso *dsos__findnew_id(struct dsos *dsos, const char *name, struct dso_id *id);
 
struct dso *__dsos__findnew_link_by_longname_id(struct rb_root *root, struct dso *dso,
						const char *name, struct dso_id *id);

bool __dsos__read_build_ids(struct list_head *head, bool with_hits);

size_t __dsos__fprintf_buildid(struct list_head *head, FILE *fp,
			       bool (skip)(struct dso *dso, int parm), int parm);
size_t __dsos__fprintf(struct list_head *head, FILE *fp);

#endif /* __PERF_DSOS */
