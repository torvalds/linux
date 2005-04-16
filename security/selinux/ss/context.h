/*
 * A security context is a set of security attributes
 * associated with each subject and object controlled
 * by the security policy.  Security contexts are
  * externally represented as variable-length strings
 * that can be interpreted by a user or application
 * with an understanding of the security policy.
 * Internally, the security server uses a simple
 * structure.  This structure is private to the
 * security server and can be changed without affecting
 * clients of the security server.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _SS_CONTEXT_H_
#define _SS_CONTEXT_H_

#include "ebitmap.h"
#include "mls_types.h"
#include "security.h"

/*
 * A security context consists of an authenticated user
 * identity, a role, a type and a MLS range.
 */
struct context {
	u32 user;
	u32 role;
	u32 type;
	struct mls_range range;
};

static inline void mls_context_init(struct context *c)
{
	memset(&c->range, 0, sizeof(c->range));
}

static inline int mls_context_cpy(struct context *dst, struct context *src)
{
	int rc;

	if (!selinux_mls_enabled)
		return 0;

	dst->range.level[0].sens = src->range.level[0].sens;
	rc = ebitmap_cpy(&dst->range.level[0].cat, &src->range.level[0].cat);
	if (rc)
		goto out;

	dst->range.level[1].sens = src->range.level[1].sens;
	rc = ebitmap_cpy(&dst->range.level[1].cat, &src->range.level[1].cat);
	if (rc)
		ebitmap_destroy(&dst->range.level[0].cat);
out:
	return rc;
}

static inline int mls_context_cmp(struct context *c1, struct context *c2)
{
	if (!selinux_mls_enabled)
		return 1;

	return ((c1->range.level[0].sens == c2->range.level[0].sens) &&
		ebitmap_cmp(&c1->range.level[0].cat,&c2->range.level[0].cat) &&
		(c1->range.level[1].sens == c2->range.level[1].sens) &&
		ebitmap_cmp(&c1->range.level[1].cat,&c2->range.level[1].cat));
}

static inline void mls_context_destroy(struct context *c)
{
	if (!selinux_mls_enabled)
		return;

	ebitmap_destroy(&c->range.level[0].cat);
	ebitmap_destroy(&c->range.level[1].cat);
	mls_context_init(c);
}

static inline void context_init(struct context *c)
{
	memset(c, 0, sizeof(*c));
}

static inline int context_cpy(struct context *dst, struct context *src)
{
	dst->user = src->user;
	dst->role = src->role;
	dst->type = src->type;
	return mls_context_cpy(dst, src);
}

static inline void context_destroy(struct context *c)
{
	c->user = c->role = c->type = 0;
	mls_context_destroy(c);
}

static inline int context_cmp(struct context *c1, struct context *c2)
{
	return ((c1->user == c2->user) &&
		(c1->role == c2->role) &&
		(c1->type == c2->type) &&
		mls_context_cmp(c1, c2));
}

#endif	/* _SS_CONTEXT_H_ */

