/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2017 Hari Bathini, IBM Corporation
 */

#ifndef __PERF_NAMESPACES_H
#define __PERF_NAMESPACES_H

#include <sys/types.h>
#include <sys/stat.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <linux/refcount.h>
#include <linux/types.h>
#include <internal/rc_check.h>

#ifndef HAVE_SETNS_SUPPORT
int setns(int fd, int nstype);
#endif

struct perf_record_namespaces;

struct namespaces {
	struct list_head list;
	u64 end_time;
	struct perf_ns_link_info link_info[];
};

struct namespaces *namespaces__new(struct perf_record_namespaces *event);
void namespaces__free(struct namespaces *namespaces);

DECLARE_RC_STRUCT(nsinfo) {
	pid_t			pid;
	pid_t			tgid;
	pid_t			nstgid;
	bool			need_setns;
	bool			in_pidns;
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
struct nsinfo *nsinfo__copy(const struct nsinfo *nsi);

struct nsinfo *nsinfo__get(struct nsinfo *nsi);
void nsinfo__put(struct nsinfo *nsi);

bool nsinfo__need_setns(const struct nsinfo *nsi);
void nsinfo__clear_need_setns(struct nsinfo *nsi);
pid_t nsinfo__tgid(const struct nsinfo  *nsi);
pid_t nsinfo__nstgid(const struct nsinfo  *nsi);
pid_t nsinfo__pid(const struct nsinfo  *nsi);
bool nsinfo__in_pidns(const struct nsinfo  *nsi);
void nsinfo__set_in_pidns(struct nsinfo *nsi);

void nsinfo__mountns_enter(struct nsinfo *nsi, struct nscookie *nc);
void nsinfo__mountns_exit(struct nscookie *nc);

char *nsinfo__realpath(const char *path, struct nsinfo *nsi);
int nsinfo__stat(const char *filename, struct stat *st, struct nsinfo *nsi);

bool nsinfo__is_in_root_namespace(void);

static inline void __nsinfo__zput(struct nsinfo **nsip)
{
	if (nsip) {
		nsinfo__put(*nsip);
		*nsip = NULL;
	}
}

#define nsinfo__zput(nsi) __nsinfo__zput(&nsi)

const char *perf_ns__name(unsigned int id);

#endif  /* __PERF_NAMESPACES_H */
