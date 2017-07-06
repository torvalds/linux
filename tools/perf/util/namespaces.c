/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2017 Hari Bathini, IBM Corporation
 */

#include "namespaces.h"
#include "util.h"
#include "event.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct namespaces *namespaces__new(struct namespaces_event *event)
{
	struct namespaces *namespaces;
	u64 link_info_size = ((event ? event->nr_namespaces : NR_NAMESPACES) *
			      sizeof(struct perf_ns_link_info));

	namespaces = zalloc(sizeof(struct namespaces) + link_info_size);
	if (!namespaces)
		return NULL;

	namespaces->end_time = -1;

	if (event)
		memcpy(namespaces->link_info, event->link_info, link_info_size);

	return namespaces;
}

void namespaces__free(struct namespaces *namespaces)
{
	free(namespaces);
}

void nsinfo__init(struct nsinfo *nsi)
{
	char oldns[PATH_MAX];
	char *newns = NULL;
	struct stat old_stat;
	struct stat new_stat;

	if (snprintf(oldns, PATH_MAX, "/proc/self/ns/mnt") >= PATH_MAX)
		return;

	if (asprintf(&newns, "/proc/%d/ns/mnt", nsi->pid) == -1)
		return;

	if (stat(oldns, &old_stat) < 0)
		goto out;

	if (stat(newns, &new_stat) < 0)
		goto out;

	/* Check if the mount namespaces differ, if so then indicate that we
	 * want to switch as part of looking up dso/map data.
	 */
	if (old_stat.st_ino != new_stat.st_ino) {
		nsi->need_setns = true;
		nsi->mntns_path = newns;
		newns = NULL;
	}

out:
	free(newns);
}

struct nsinfo *nsinfo__new(pid_t pid)
{
	struct nsinfo *nsi = calloc(1, sizeof(*nsi));

	if (nsi != NULL) {
		nsi->pid = pid;
		nsi->need_setns = false;
		nsinfo__init(nsi);
		refcount_set(&nsi->refcnt, 1);
	}

	return nsi;
}

void nsinfo__delete(struct nsinfo *nsi)
{
	zfree(&nsi->mntns_path);
	free(nsi);
}

struct nsinfo *nsinfo__get(struct nsinfo *nsi)
{
	if (nsi)
		refcount_inc(&nsi->refcnt);
	return nsi;
}

void nsinfo__put(struct nsinfo *nsi)
{
	if (nsi && refcount_dec_and_test(&nsi->refcnt))
		nsinfo__delete(nsi);
}

void nsinfo__mountns_enter(struct nsinfo *nsi, struct nscookie *nc)
{
	char curpath[PATH_MAX];
	int oldns = -1;
	int newns = -1;

	if (nc == NULL)
		return;

	nc->oldns = -1;
	nc->newns = -1;

	if (!nsi || !nsi->need_setns)
		return;

	if (snprintf(curpath, PATH_MAX, "/proc/self/ns/mnt") >= PATH_MAX)
		return;

	oldns = open(curpath, O_RDONLY);
	if (oldns < 0)
		return;

	newns = open(nsi->mntns_path, O_RDONLY);
	if (newns < 0)
		goto errout;

	if (setns(newns, CLONE_NEWNS) < 0)
		goto errout;

	nc->oldns = oldns;
	nc->newns = newns;
	return;

errout:
	if (oldns > -1)
		close(oldns);
	if (newns > -1)
		close(newns);
}

void nsinfo__mountns_exit(struct nscookie *nc)
{
	if (nc == NULL || nc->oldns == -1 || nc->newns == -1)
		return;

	setns(nc->oldns, CLONE_NEWNS);

	if (nc->oldns > -1) {
		close(nc->oldns);
		nc->oldns = -1;
	}

	if (nc->newns > -1) {
		close(nc->newns);
		nc->newns = -1;
	}
}
