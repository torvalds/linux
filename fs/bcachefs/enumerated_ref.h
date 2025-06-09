/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ENUMERATED_REF_H
#define _BCACHEFS_ENUMERATED_REF_H

#include "enumerated_ref_types.h"

/*
 * A refcount where the users are enumerated: in debug mode, we create sepate
 * refcounts for each user, to make leaks and refcount errors easy to track
 * down:
 */

#ifdef ENUMERATED_REF_DEBUG
void enumerated_ref_get(struct enumerated_ref *, unsigned);
bool __enumerated_ref_tryget(struct enumerated_ref *, unsigned);
bool enumerated_ref_tryget(struct enumerated_ref *, unsigned);
void enumerated_ref_put(struct enumerated_ref *, unsigned);
#else

static inline void enumerated_ref_get(struct enumerated_ref *ref, unsigned idx)
{
	percpu_ref_get(&ref->ref);
}

static inline bool __enumerated_ref_tryget(struct enumerated_ref *ref, unsigned idx)
{
	return percpu_ref_tryget(&ref->ref);
}

static inline bool enumerated_ref_tryget(struct enumerated_ref *ref, unsigned idx)
{
	return percpu_ref_tryget_live(&ref->ref);
}

static inline void enumerated_ref_put(struct enumerated_ref *ref, unsigned idx)
{
	percpu_ref_put(&ref->ref);
}
#endif

static inline bool enumerated_ref_is_zero(struct enumerated_ref *ref)
{
#ifndef ENUMERATED_REF_DEBUG
	return percpu_ref_is_zero(&ref->ref);
#else
	for (unsigned i = 0; i < ref->nr; i++)
		if (atomic_long_read(&ref->refs[i]))
			return false;
	return true;
#endif
}

void enumerated_ref_stop_async(struct enumerated_ref *);
void enumerated_ref_stop(struct enumerated_ref *, const char * const[]);
void enumerated_ref_start(struct enumerated_ref *);

void enumerated_ref_exit(struct enumerated_ref *);
int enumerated_ref_init(struct enumerated_ref *, unsigned,
			void (*stop_fn)(struct enumerated_ref *));

struct printbuf;
void enumerated_ref_to_text(struct printbuf *,
			    struct enumerated_ref *,
			    const char * const[]);

#endif /* _BCACHEFS_ENUMERATED_REF_H */
