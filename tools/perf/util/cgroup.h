/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CGROUP_H__
#define __CGROUP_H__

#include <linux/refcount.h>
#include <linux/rbtree.h>
#include "util/env.h"

struct option;

struct cgroup {
	struct rb_node		node;
	u64			id;
	char			*name;
	int			fd;
	refcount_t		refcnt;
};

extern int nr_cgroups; /* number of explicit cgroups defined */

struct cgroup *cgroup__get(struct cgroup *cgroup);
void cgroup__put(struct cgroup *cgroup);

struct evlist;
struct rblist;

struct cgroup *evlist__findnew_cgroup(struct evlist *evlist, const char *name);
int evlist__expand_cgroup(struct evlist *evlist, const char *cgroups,
			  struct rblist *metric_events, bool open_cgroup);

void evlist__set_default_cgroup(struct evlist *evlist, struct cgroup *cgroup);

int parse_cgroups(const struct option *opt, const char *str, int unset);

struct cgroup *cgroup__findnew(struct perf_env *env, uint64_t id,
			       const char *path);
struct cgroup *cgroup__find(struct perf_env *env, uint64_t id);

void perf_env__purge_cgroups(struct perf_env *env);

#endif /* __CGROUP_H__ */
