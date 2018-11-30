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
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <asm/bug.h>

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

int nsinfo__init(struct nsinfo *nsi)
{
	char oldns[PATH_MAX];
	char spath[PATH_MAX];
	char *newns = NULL;
	char *statln = NULL;
	struct stat old_stat;
	struct stat new_stat;
	FILE *f = NULL;
	size_t linesz = 0;
	int rv = -1;

	if (snprintf(oldns, PATH_MAX, "/proc/self/ns/mnt") >= PATH_MAX)
		return rv;

	if (asprintf(&newns, "/proc/%d/ns/mnt", nsi->pid) == -1)
		return rv;

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

	/* If we're dealing with a process that is in a different PID namespace,
	 * attempt to work out the innermost tgid for the process.
	 */
	if (snprintf(spath, PATH_MAX, "/proc/%d/status", nsi->pid) >= PATH_MAX)
		goto out;

	f = fopen(spath, "r");
	if (f == NULL)
		goto out;

	while (getline(&statln, &linesz, f) != -1) {
		/* Use tgid if CONFIG_PID_NS is not defined. */
		if (strstr(statln, "Tgid:") != NULL) {
			nsi->tgid = (pid_t)strtol(strrchr(statln, '\t'),
						     NULL, 10);
			nsi->nstgid = nsi->tgid;
		}

		if (strstr(statln, "NStgid:") != NULL) {
			nsi->nstgid = (pid_t)strtol(strrchr(statln, '\t'),
						     NULL, 10);
			break;
		}
	}
	rv = 0;

out:
	if (f != NULL)
		(void) fclose(f);
	free(statln);
	free(newns);
	return rv;
}

struct nsinfo *nsinfo__new(pid_t pid)
{
	struct nsinfo *nsi;

	if (pid == 0)
		return NULL;

	nsi = calloc(1, sizeof(*nsi));
	if (nsi != NULL) {
		nsi->pid = pid;
		nsi->tgid = pid;
		nsi->nstgid = pid;
		nsi->need_setns = false;
		/* Init may fail if the process exits while we're trying to look
		 * at its proc information.  In that case, save the pid but
		 * don't try to enter the namespace.
		 */
		if (nsinfo__init(nsi) == -1)
			nsi->need_setns = false;

		refcount_set(&nsi->refcnt, 1);
	}

	return nsi;
}

struct nsinfo *nsinfo__copy(struct nsinfo *nsi)
{
	struct nsinfo *nnsi;

	if (nsi == NULL)
		return NULL;

	nnsi = calloc(1, sizeof(*nnsi));
	if (nnsi != NULL) {
		nnsi->pid = nsi->pid;
		nnsi->tgid = nsi->tgid;
		nnsi->nstgid = nsi->nstgid;
		nnsi->need_setns = nsi->need_setns;
		if (nsi->mntns_path) {
			nnsi->mntns_path = strdup(nsi->mntns_path);
			if (!nnsi->mntns_path) {
				free(nnsi);
				return NULL;
			}
		}
		refcount_set(&nnsi->refcnt, 1);
	}

	return nnsi;
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

void nsinfo__mountns_enter(struct nsinfo *nsi,
				  struct nscookie *nc)
{
	char curpath[PATH_MAX];
	int oldns = -1;
	int newns = -1;
	char *oldcwd = NULL;

	if (nc == NULL)
		return;

	nc->oldns = -1;
	nc->newns = -1;

	if (!nsi || !nsi->need_setns)
		return;

	if (snprintf(curpath, PATH_MAX, "/proc/self/ns/mnt") >= PATH_MAX)
		return;

	oldcwd = get_current_dir_name();
	if (!oldcwd)
		return;

	oldns = open(curpath, O_RDONLY);
	if (oldns < 0)
		goto errout;

	newns = open(nsi->mntns_path, O_RDONLY);
	if (newns < 0)
		goto errout;

	if (setns(newns, CLONE_NEWNS) < 0)
		goto errout;

	nc->oldcwd = oldcwd;
	nc->oldns = oldns;
	nc->newns = newns;
	return;

errout:
	free(oldcwd);
	if (oldns > -1)
		close(oldns);
	if (newns > -1)
		close(newns);
}

void nsinfo__mountns_exit(struct nscookie *nc)
{
	if (nc == NULL || nc->oldns == -1 || nc->newns == -1 || !nc->oldcwd)
		return;

	setns(nc->oldns, CLONE_NEWNS);

	if (nc->oldcwd) {
		WARN_ON_ONCE(chdir(nc->oldcwd));
		zfree(&nc->oldcwd);
	}

	if (nc->oldns > -1) {
		close(nc->oldns);
		nc->oldns = -1;
	}

	if (nc->newns > -1) {
		close(nc->newns);
		nc->newns = -1;
	}
}

char *nsinfo__realpath(const char *path, struct nsinfo *nsi)
{
	char *rpath;
	struct nscookie nsc;

	nsinfo__mountns_enter(nsi, &nsc);
	rpath = realpath(path, NULL);
	nsinfo__mountns_exit(&nsc);

	return rpath;
}
