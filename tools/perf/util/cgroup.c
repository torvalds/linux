// SPDX-License-Identifier: GPL-2.0
#include <subcmd/parse-options.h>
#include "evsel.h"
#include "cgroup.h"
#include "evlist.h"
#include <linux/stringify.h>
#include <linux/zalloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

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

static int open_cgroup(const char *name)
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

static struct cgroup *evlist__find_cgroup(struct evlist *evlist, const char *str)
{
	struct evsel *counter;
	/*
	 * check if cgrp is already defined, if so we reuse it
	 */
	evlist__for_each_entry(evlist, counter) {
		if (!counter->cgrp)
			continue;
		if (!strcmp(counter->cgrp->name, str))
			return cgroup__get(counter->cgrp);
	}

	return NULL;
}

static struct cgroup *cgroup__new(const char *name)
{
	struct cgroup *cgroup = zalloc(sizeof(*cgroup));

	if (cgroup != NULL) {
		refcount_set(&cgroup->refcnt, 1);

		cgroup->name = strdup(name);
		if (!cgroup->name)
			goto out_err;
		cgroup->fd = open_cgroup(name);
		if (cgroup->fd == -1)
			goto out_free_name;
	}

	return cgroup;

out_free_name:
	zfree(&cgroup->name);
out_err:
	free(cgroup);
	return NULL;
}

struct cgroup *evlist__findnew_cgroup(struct evlist *evlist, const char *name)
{
	struct cgroup *cgroup = evlist__find_cgroup(evlist, name);

	return cgroup ?: cgroup__new(name);
}

static int add_cgroup(struct evlist *evlist, const char *str)
{
	struct evsel *counter;
	struct cgroup *cgrp = evlist__findnew_cgroup(evlist, str);
	int n;

	if (!cgrp)
		return -1;
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

	cgroup__put(cgrp);
	return -1;
found:
	counter->cgrp = cgrp;
	return 0;
}

static void cgroup__delete(struct cgroup *cgroup)
{
	close(cgroup->fd);
	zfree(&cgroup->name);
	free(cgroup);
}

void cgroup__put(struct cgroup *cgrp)
{
	if (cgrp && refcount_dec_and_test(&cgrp->refcnt)) {
		cgroup__delete(cgrp);
	}
}

struct cgroup *cgroup__get(struct cgroup *cgroup)
{
       if (cgroup)
		refcount_inc(&cgroup->refcnt);
       return cgroup;
}

static void evsel__set_default_cgroup(struct evsel *evsel, struct cgroup *cgroup)
{
	if (evsel->cgrp == NULL)
		evsel->cgrp = cgroup__get(cgroup);
}

void evlist__set_default_cgroup(struct evlist *evlist, struct cgroup *cgroup)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel)
		evsel__set_default_cgroup(evsel, cgroup);
}

int parse_cgroups(const struct option *opt, const char *str,
		  int unset __maybe_unused)
{
	struct evlist *evlist = *(struct evlist **)opt->value;
	struct evsel *counter;
	struct cgroup *cgrp = NULL;
	const char *p, *e, *eos = str + strlen(str);
	char *s;
	int ret, i;

	if (list_empty(&evlist->core.entries)) {
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
			free(s);
			if (ret)
				return -1;
		}
		/* nr_cgroups is increased een for empty cgroups */
		nr_cgroups++;
		if (!p)
			break;
		str = p+1;
	}
	/* for the case one cgroup combine to multiple events */
	i = 0;
	if (nr_cgroups == 1) {
		evlist__for_each_entry(evlist, counter) {
			if (i == 0)
				cgrp = counter->cgrp;
			else {
				counter->cgrp = cgrp;
				refcount_inc(&cgrp->refcnt);
			}
			i++;
		}
	}
	return 0;
}
