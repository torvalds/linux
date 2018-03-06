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
void close_cgroup(struct cgroup *cgrp);

int parse_cgroups(const struct option *opt, const char *str, int unset);

#endif /* __CGROUP_H__ */
