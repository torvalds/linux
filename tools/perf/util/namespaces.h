/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2017 Hari Bathini, IBM Corporation
 */

#ifndef __PERF_NAMESPACES_H
#define __PERF_NAMESPACES_H

#include <sys/types.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <linux/refcount.h>
#include <linux/types.h>

#ifndef HAVE_SETNS_SUPPORT
int setns(int fd, int nstype);
#endif

struct namespaces_event;

struct namespaces {
	struct list_head list;
	u64 end_time;
	struct perf_ns_link_info link_info[];
};

struct namespaces *namespaces__new(struct namespaces_event *event);
void namespaces__free(struct namespaces *namespaces);

struct nsinfo {
	pid_t			pid;
	pid_t			tgid;
	pid_t			nstgid;
	bool			need_setns;
	char			*mntns_path;
	refcount_t		refcnt;
};

struct nscookie {
	int			oldns;
	int			newns;
	char			*oldcwd;
};

int nsinfo__init(struct nsinfo *nsi);
struct nsinfo *nsinfo__new(pid_t pid);
struct nsinfo *nsinfo__copy(struct nsinfo *nsi);
void nsinfo__delete(struct nsinfo *nsi);

struct nsinfo *nsinfo__get(struct nsinfo *nsi);
void nsinfo__put(struct nsinfo *nsi);

void nsinfo__mountns_enter(struct nsinfo *nsi, struct nscookie *nc);
void nsinfo__mountns_exit(struct nscookie *nc);

char *nsinfo__realpath(const char *path, struct nsinfo *nsi);

static inline void __nsinfo__zput(struct nsinfo **nsip)
{
	if (nsip) {
		nsinfo__put(*nsip);
		*nsip = NULL;
	}
}

#define nsinfo__zput(nsi) __nsinfo__zput(&nsi)

#endif  /* __PERF_NAMESPACES_H */
