// SPDX-License-Identifier: GPL-2.0
#include "util.h"
#include "../perf.h"
#include <subcmd/parse-options.h>
#include "evsel.h"
#include "cgroup.h"
#include "evlist.h"
#include <linux/stringify.h>

int nr_cgroups;

static int
cgroupfs_find_mountpoint(char *buf, size_t maxlen)
{
	FILE *fp;
	char mountpoint[PATH_MAX + 1], tokens[PATH_MAX + 1], type[PATH_MAX + 1];
	char path_v1[PATH_MAX + 1], path_v2[PATH_MAX + 2], *path;
	char *token, *saved_ptr = NULL;

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return -1;

	/*
	 * in order to handle split hierarchy, we need to scan /proc/mounts
	 * and inspect every cgroupfs mount point to find one that has
	 * perf_event subsystem
	 */
	path_v1[0] = '\0';
	path_v2[0] = '\0';

	while (fscanf(fp, "%*s %"__stringify(PATH_MAX)"s %"__stringify(PATH_MAX)"s %"
				__stringify(PATH_MAX)"s %*d %*d\n",
				mountpoint, type, tokens) == 3) {

		if (!path_v1[0] && !strcmp(type, "cgroup")) {

			token = strtok_r(tokens, ",", &saved_ptr);

			while (token != NULL) {
				if (!strcmp(token, "perf_event")) {
					strcpy(path_v1, mountpoint);
					break;
				}
				token = strtok_r(NULL, ",", &saved_ptr);
			}
		}

		if (!path_v2[0] && !strcmp(type, "cgroup2"))
			strcpy(path_v2, mountpoint);

		if (path_v1[0] && path_v2[0])
			break;
	}
	fclose(fp);

	if (path_v1[0])
		path = path_v1;
	else if (path_v2[0])
		path = path_v2;
	else
		return -1;

	if (strlen(path) < maxlen) {
		strcpy(buf, path);
		return 0;
	}
	return -1;
}

static int open_cgroup(char *name)
{
	char path[PATH_MAX + 1];
	char mnt[PATH_MAX + 1];
	int fd;


	if (cgroupfs_find_mountpoint(mnt, PATH_MAX + 1))
		return -1;

	scnprintf(path, PATH_MAX, "%s/%s", mnt, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		fprintf(stderr, "no access to cgroup %s\n", path);

	return fd;
}

static int add_cgroup(struct perf_evlist *evlist, char *str)
{
	struct perf_evsel *counter;
	struct cgroup_sel *cgrp = NULL;
	int n;
	/*
	 * check if cgrp is already defined, if so we reuse it
	 */
	evlist__for_each_entry(evlist, counter) {
		cgrp = counter->cgrp;
		if (!cgrp)
			continue;
		if (!strcmp(cgrp->name, str)) {
			refcount_inc(&cgrp->refcnt);
			break;
		}

		cgrp = NULL;
	}

	if (!cgrp) {
		cgrp = zalloc(sizeof(*cgrp));
		if (!cgrp)
			return -1;

		cgrp->name = str;
		refcount_set(&cgrp->refcnt, 1);

		cgrp->fd = open_cgroup(str);
		if (cgrp->fd == -1) {
			free(cgrp);
			return -1;
		}
	}

	/*
	 * find corresponding event
	 * if add cgroup N, then need to find event N
	 */
	n = 0;
	evlist__for_each_entry(evlist, counter) {
		if (n == nr_cgroups)
			goto found;
		n++;
	}
	if (refcount_dec_and_test(&cgrp->refcnt))
		free(cgrp);

	return -1;
found:
	counter->cgrp = cgrp;
	return 0;
}

void close_cgroup(struct cgroup_sel *cgrp)
{
	if (cgrp && refcount_dec_and_test(&cgrp->refcnt)) {
		close(cgrp->fd);
		zfree(&cgrp->name);
		free(cgrp);
	}
}

int parse_cgroups(const struct option *opt __maybe_unused, const char *str,
		  int unset __maybe_unused)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;
	const char *p, *e, *eos = str + strlen(str);
	char *s;
	int ret;

	if (list_empty(&evlist->entries)) {
		fprintf(stderr, "must define events before cgroups\n");
		return -1;
	}

	for (;;) {
		p = strchr(str, ',');
		e = p ? p : eos;

		/* allow empty cgroups, i.e., skip */
		if (e - str) {
			/* termination added */
			s = strndup(str, e - str);
			if (!s)
				return -1;
			ret = add_cgroup(evlist, s);
			if (ret) {
				free(s);
				return -1;
			}
		}
		/* nr_cgroups is increased een for empty cgroups */
		nr_cgroups++;
		if (!p)
			break;
		str = p+1;
	}
	return 0;
}
