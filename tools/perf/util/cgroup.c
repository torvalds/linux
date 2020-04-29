// SPDX-License-Identifier: GPL-2.0
#include <subcmd/parse-options.h>
#include "evsel.h"
#include "cgroup.h"
#include "evlist.h"
#include <linux/zalloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <api/fs/fs.h>

int nr_cgroups;

static int open_cgroup(const char *name)
{
	char path[PATH_MAX + 1];
	char mnt[PATH_MAX + 1];
	int fd;


	if (cgroupfs_find_mountpoint(mnt, PATH_MAX + 1, "perf_event"))
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

static struct cgroup *__cgroup__findnew(struct rb_root *root, uint64_t id,
					bool create, const char *path)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct cgroup *cgrp;

	while (*p != NULL) {
		parent = *p;
		cgrp = rb_entry(parent, struct cgroup, node);

		if (cgrp->id == id)
			return cgrp;

		if (cgrp->id < id)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	if (!create)
		return NULL;

	cgrp = malloc(sizeof(*cgrp));
	if (cgrp == NULL)
		return NULL;

	cgrp->name = strdup(path);
	if (cgrp->name == NULL) {
		free(cgrp);
		return NULL;
	}

	cgrp->fd = -1;
	cgrp->id = id;
	refcount_set(&cgrp->refcnt, 1);

	rb_link_node(&cgrp->node, parent, p);
	rb_insert_color(&cgrp->node, root);

	return cgrp;
}

struct cgroup *cgroup__findnew(struct perf_env *env, uint64_t id,
			       const char *path)
{
	struct cgroup *cgrp;

	down_write(&env->cgroups.lock);
	cgrp = __cgroup__findnew(&env->cgroups.tree, id, true, path);
	up_write(&env->cgroups.lock);
	return cgrp;
}

struct cgroup *cgroup__find(struct perf_env *env, uint64_t id)
{
	struct cgroup *cgrp;

	down_read(&env->cgroups.lock);
	cgrp = __cgroup__findnew(&env->cgroups.tree, id, false, NULL);
	up_read(&env->cgroups.lock);
	return cgrp;
}

void perf_env__purge_cgroups(struct perf_env *env)
{
	struct rb_node *node;
	struct cgroup *cgrp;

	down_write(&env->cgroups.lock);
	while (!RB_EMPTY_ROOT(&env->cgroups.tree)) {
		node = rb_first(&env->cgroups.tree);
		cgrp = rb_entry(node, struct cgroup, node);

		rb_erase(node, &env->cgroups.tree);
		cgroup__put(cgrp);
	}
	up_write(&env->cgroups.lock);
}
