/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CGROUP_H__
#define __CGROUP_H__

#include <linux/refcount.h>

struct option;

struct cgroup {
	char *name;
	int fd;
	refcount_t refcnt;
};


extern int nr_cgroups; /* number of explicit cgroups defined */

struct cgroup *cgroup__get(struct cgroup *cgroup);
void cgroup__put(struct cgroup *cgroup);

struct evlist;

struct cgroup *evlist__findnew_cgroup(struct evlist *evlist, const char *name);

void evlist__set_default_cgroup(struct evlist *evlist, struct cgroup *cgroup);

int parse_cgroups(const struct option *opt, const char *str, int unset);

#endif /* __CGROUP_H__ */
