#ifndef __CGROUP_H__
#define __CGROUP_H__

#include <linux/atomic.h>

struct option;

struct cgroup_sel {
	char *name;
	int fd;
	atomic_t refcnt;
};


extern int nr_cgroups; /* number of explicit cgroups defined */
void close_cgroup(struct cgroup_sel *cgrp);
int parse_cgroups(const struct option *opt, const char *str, int unset);

#endif /* __CGROUP_H__ */
