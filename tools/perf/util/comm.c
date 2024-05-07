// SPDX-License-Identifier: GPL-2.0
#include "comm.h"
#include <errno.h>
#include <string.h>
#include <internal/rc_check.h>
#include <linux/refcount.h>
#include <linux/zalloc.h>
#include "rwsem.h"

DECLARE_RC_STRUCT(comm_str) {
	refcount_t refcnt;
	char str[];
};

static struct comm_strs {
	struct rw_semaphore lock;
	struct comm_str **strs;
	int num_strs;
	int capacity;
} _comm_strs;

static void comm_strs__init(void)
{
	init_rwsem(&_comm_strs.lock);
	_comm_strs.capacity = 16;
	_comm_strs.num_strs = 0;
	_comm_strs.strs = calloc(16, sizeof(*_comm_strs.strs));
}

static struct comm_strs *comm_strs__get(void)
{
	static pthread_once_t comm_strs_type_once = PTHREAD_ONCE_INIT;

	pthread_once(&comm_strs_type_once, comm_strs__init);

	return &_comm_strs;
}

static refcount_t *comm_str__refcnt(struct comm_str *cs)
{
	return &RC_CHK_ACCESS(cs)->refcnt;
}

static const char *comm_str__str(const struct comm_str *cs)
{
	return &RC_CHK_ACCESS(cs)->str[0];
}

static struct comm_str *comm_str__get(struct comm_str *cs)
{
	struct comm_str *result;

	if (RC_CHK_GET(result, cs))
		refcount_inc_not_zero(comm_str__refcnt(cs));

	return result;
}

static void comm_str__put(struct comm_str *cs)
{
	if (cs && refcount_dec_and_test(comm_str__refcnt(cs))) {
		struct comm_strs *comm_strs = comm_strs__get();
		int i;

		down_write(&comm_strs->lock);
		for (i = 0; i < comm_strs->num_strs; i++) {
			if (comm_strs->strs[i] == cs)
				break;
		}
		for (; i < comm_strs->num_strs - 1; i++)
			comm_strs->strs[i] = comm_strs->strs[i + 1];

		comm_strs->num_strs--;
		up_write(&comm_strs->lock);
		RC_CHK_FREE(cs);
	} else {
		RC_CHK_PUT(cs);
	}
}

static struct comm_str *comm_str__new(const char *str)
{
	struct comm_str *result = NULL;
	RC_STRUCT(comm_str) *cs;

	cs = malloc(sizeof(*cs) + strlen(str) + 1);
	if (ADD_RC_CHK(result, cs)) {
		refcount_set(comm_str__refcnt(result), 1);
		strcpy(&cs->str[0], str);
	}
	return result;
}

static int comm_str__cmp(const void *_lhs, const void *_rhs)
{
	const struct comm_str *lhs = *(const struct comm_str * const *)_lhs;
	const struct comm_str *rhs = *(const struct comm_str * const *)_rhs;

	return strcmp(comm_str__str(lhs), comm_str__str(rhs));
}

static int comm_str__search(const void *_key, const void *_member)
{
	const char *key = _key;
	const struct comm_str *member = *(const struct comm_str * const *)_member;

	return strcmp(key, comm_str__str(member));
}

static struct comm_str *__comm_strs__find(struct comm_strs *comm_strs, const char *str)
{
	struct comm_str **result;

	result = bsearch(str, comm_strs->strs, comm_strs->num_strs, sizeof(struct comm_str *),
			 comm_str__search);

	if (!result)
		return NULL;

	return comm_str__get(*result);
}

static struct comm_str *comm_strs__findnew(const char *str)
{
	struct comm_strs *comm_strs = comm_strs__get();
	struct comm_str *result;

	if (!comm_strs)
		return NULL;

	down_read(&comm_strs->lock);
	result = __comm_strs__find(comm_strs, str);
	up_read(&comm_strs->lock);
	if (result)
		return result;

	down_write(&comm_strs->lock);
	result = __comm_strs__find(comm_strs, str);
	if (!result) {
		if (comm_strs->num_strs == comm_strs->capacity) {
			struct comm_str **tmp;

			tmp = reallocarray(comm_strs->strs,
					   comm_strs->capacity + 16,
					   sizeof(*comm_strs->strs));
			if (!tmp) {
				up_write(&comm_strs->lock);
				return NULL;
			}
			comm_strs->strs = tmp;
			comm_strs->capacity += 16;
		}
		result = comm_str__new(str);
		if (result) {
			comm_strs->strs[comm_strs->num_strs++] = result;
			qsort(comm_strs->strs, comm_strs->num_strs, sizeof(struct comm_str *),
			      comm_str__cmp);
		}
	}
	up_write(&comm_strs->lock);
	return result;
}

struct comm *comm__new(const char *str, u64 timestamp, bool exec)
{
	struct comm *comm = zalloc(sizeof(*comm));

	if (!comm)
		return NULL;

	comm->start = timestamp;
	comm->exec = exec;

	comm->comm_str = comm_strs__findnew(str);
	if (!comm->comm_str) {
		free(comm);
		return NULL;
	}

	return comm;
}

int comm__override(struct comm *comm, const char *str, u64 timestamp, bool exec)
{
	struct comm_str *new, *old = comm->comm_str;

	new = comm_strs__findnew(str);
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
	return comm_str__str(comm->comm_str);
}
