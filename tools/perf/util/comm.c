#include "comm.h"
#include "util.h"
#include <stdlib.h>
#include <stdio.h>

struct comm_str {
	char *str;
	struct rb_node rb_node;
	int ref;
};

/* Should perhaps be moved to struct machine */
static struct rb_root comm_str_root;

static void comm_str__get(struct comm_str *cs)
{
	cs->ref++;
}

static void comm_str__put(struct comm_str *cs)
{
	if (!--cs->ref) {
		rb_erase(&cs->rb_node, &comm_str_root);
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

	return cs;
}

static struct comm_str *comm_str__findnew(const char *str, struct rb_root *root)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct comm_str *iter, *new;
	int cmp;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct comm_str, rb_node);

		cmp = strcmp(str, iter->str);
		if (!cmp)
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

	comm_str__get(comm->comm_str);

	return comm;
}

int comm__override(struct comm *comm, const char *str, u64 timestamp, bool exec)
{
	struct comm_str *new, *old = comm->comm_str;

	new = comm_str__findnew(str, &comm_str_root);
	if (!new)
		return -ENOMEM;

	comm_str__get(new);
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
