// SPDX-License-Identifier: GPL-2.0
#include "comm.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/refcount.h>
#include <linux/rbtree.h>
#include <linux/zalloc.h>
#include "rwsem.h"

struct comm_str {
	char *str;
	struct rb_node rb_node;
	refcount_t refcnt;
};

/* Should perhaps be moved to struct machine */
static struct rb_root comm_str_root;
static struct rw_semaphore comm_str_lock = {.lock = PTHREAD_RWLOCK_INITIALIZER,};

static struct comm_str *comm_str__get(struct comm_str *cs)
{
	if (cs && refcount_inc_not_zero(&cs->refcnt))
		return cs;

	return NULL;
}

static void comm_str__put(struct comm_str *cs)
{
	if (cs && refcount_dec_and_test(&cs->refcnt)) {
		down_write(&comm_str_lock);
		rb_erase(&cs->rb_node, &comm_str_root);
		up_write(&comm_str_lock);
		zfree(&cs->str);
		free(cs);
	}
}

static struct comm_str *comm_str__alloc(const char *str)
{
	struct comm_str *cs;

	cs = zalloc(sizeof(*cs));
	if (!cs)
		return NULL;

	cs->str = strdup(str);
	if (!cs->str) {
		free(cs);
		return NULL;
	}

	refcount_set(&cs->refcnt, 1);

	return cs;
}

static
struct comm_str *__comm_str__findnew(const char *str, struct rb_root *root)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct comm_str *iter, *new;
	int cmp;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct comm_str, rb_node);

		/*
		 * If we race with comm_str__put, iter->refcnt is 0
		 * and it will be removed within comm_str__put call
		 * shortly, ignore it in this search.
		 */
		cmp = strcmp(str, iter->str);
		if (!cmp && comm_str__get(iter))
			return iter;

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	new = comm_str__alloc(str);
	if (!new)
		return NULL;

	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, root);

	return new;
}

static struct comm_str *comm_str__findnew(const char *str, struct rb_root *root)
{
	struct comm_str *cs;

	down_write(&comm_str_lock);
	cs = __comm_str__findnew(str, root);
	up_write(&comm_str_lock);

	return cs;
}

struct comm *comm__new(const char *str, u64 timestamp, bool exec)
{
	struct comm *comm = zalloc(sizeof(*comm));

	if (!comm)
		return NULL;

	comm->start = timestamp;
	comm->exec = exec;

	comm->comm_str = comm_str__findnew(str, &comm_str_root);
	if (!comm->comm_str) {
		free(comm);
		return NULL;
	}

	return comm;
}

int comm__override(struct comm *comm, const char *str, u64 timestamp, bool exec)
{
	struct comm_str *new, *old = comm->comm_str;

	new = comm_str__findnew(str, &comm_str_root);
	if (!new)
		return -ENOMEM;

	comm_str__put(old);
	comm->comm_str = new;
	comm->start = timestamp;
	if (exec)
		comm->exec = true;

	return 0;
}

void comm__free(struct comm *comm)
{
	comm_str__put(comm->comm_str);
	free(comm);
}

const char *comm__str(const struct comm *comm)
{
	return comm->comm_str->str;
}
