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

static void comm_strs__remove_if_last(struct comm_str *cs);

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
	if (!cs)
		return;

	if (refcount_dec_and_test(comm_str__refcnt(cs))) {
		RC_CHK_FREE(cs);
	} else {
		if (refcount_read(comm_str__refcnt(cs)) == 1)
			comm_strs__remove_if_last(cs);

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

static int comm_str__search(const void *_key, const void *_member)
{
	const char *key = _key;
	const struct comm_str *member = *(const struct comm_str * const *)_member;

	return strcmp(key, comm_str__str(member));
}

static void comm_strs__remove_if_last(struct comm_str *cs)
{
	struct comm_strs *comm_strs = comm_strs__get();

	down_write(&comm_strs->lock);
	/*
	 * Are there only references from the array, if so remove the array
	 * reference under the write lock so that we don't race with findnew.
	 */
	if (refcount_read(comm_str__refcnt(cs)) == 1) {
		struct comm_str **entry;

		entry = bsearch(comm_str__str(cs), comm_strs->strs, comm_strs->num_strs,
				sizeof(struct comm_str *), comm_str__search);
		comm_str__put(*entry);
		for (int i = entry - comm_strs->strs; i < comm_strs->num_strs - 1; i++)
			comm_strs->strs[i] = comm_strs->strs[i + 1];
		comm_strs->num_strs--;
	}
	up_write(&comm_strs->lock);
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
			int low = 0, high = comm_strs->num_strs - 1;
			int insert = comm_strs->num_strs; /* Default to inserting at the end. */

			while (low <= high) {
				int mid = low + (high - low) / 2;
				int cmp = strcmp(comm_str__str(comm_strs->strs[mid]), str);

				if (cmp < 0) {
					low = mid + 1;
				} else {
					high = mid - 1;
					insert = mid;
				}
			}
			memmove(&comm_strs->strs[insert + 1], &comm_strs->strs[insert],
				(comm_strs->num_strs - insert) * sizeof(struct comm_str *));
			comm_strs->num_strs++;
			comm_strs->strs[insert] = result;
		}
	}
	up_write(&comm_strs->lock);
	return comm_str__get(result);
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
