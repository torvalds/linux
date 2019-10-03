/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DSOS
#define __PERF_DSOS

#include <stdbool.h>
#include <stdio.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include "rwsem.h"

struct dso;

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
struct dso *dsos__find(struct dsos *dsos, const char *name, bool cmp_short);
struct dso *__dsos__findnew(struct dsos *dsos, const char *name);
struct dso *dsos__findnew(struct dsos *dsos, const char *name);

struct dso *__dsos__findnew_link_by_longname(struct rb_root *root, struct dso *dso, const char *name);

static inline struct dso *__dsos__findnew_by_longname(struct rb_root *root, const char *name)
{
	return __dsos__findnew_link_by_longname(root, NULL, name);
}

bool __dsos__read_build_ids(struct list_head *head, bool with_hits);

size_t __dsos__fprintf_buildid(struct list_head *head, FILE *fp,
			       bool (skip)(struct dso *dso, int parm), int parm);
size_t __dsos__fprintf(struct list_head *head, FILE *fp);

#endif /* __PERF_DSOS */
