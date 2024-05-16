/* SPDX-License-Identifier: GPL-2.0 */
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
 * Author : Stephen Smalley, <stephen.smalley.work@gmail.com>
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
	u32 len;        /* length of string in bytes */
	struct mls_range range;
	char *str;	/* string representation if context cannot be mapped. */
};

static inline void mls_context_init(struct context *c)
{
	memset(&c->range, 0, sizeof(c->range));
}

static inline int mls_context_cpy(struct context *dst, const struct context *src)
{
	int rc;

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

/*
 * Sets both levels in the MLS range of 'dst' to the low level of 'src'.
 */
static inline int mls_context_cpy_low(struct context *dst, const struct context *src)
{
	int rc;

	dst->range.level[0].sens = src->range.level[0].sens;
	rc = ebitmap_cpy(&dst->range.level[0].cat, &src->range.level[0].cat);
	if (rc)
		goto out;

	dst->range.level[1].sens = src->range.level[0].sens;
	rc = ebitmap_cpy(&dst->range.level[1].cat, &src->range.level[0].cat);
	if (rc)
		ebitmap_destroy(&dst->range.level[0].cat);
out:
	return rc;
}

/*
 * Sets both levels in the MLS range of 'dst' to the high level of 'src'.
 */
static inline int mls_context_cpy_high(struct context *dst, const struct context *src)
{
	int rc;

	dst->range.level[0].sens = src->range.level[1].sens;
	rc = ebitmap_cpy(&dst->range.level[0].cat, &src->range.level[1].cat);
	if (rc)
		goto out;

	dst->range.level[1].sens = src->range.level[1].sens;
	rc = ebitmap_cpy(&dst->range.level[1].cat, &src->range.level[1].cat);
	if (rc)
		ebitmap_destroy(&dst->range.level[0].cat);
out:
	return rc;
}


static inline int mls_context_glblub(struct context *dst,
				     const struct context *c1, const struct context *c2)
{
	struct mls_range *dr = &dst->range;
	const struct mls_range *r1 = &c1->range, *r2 = &c2->range;
	int rc = 0;

	if (r1->level[1].sens < r2->level[0].sens ||
	    r2->level[1].sens < r1->level[0].sens)
		/* These ranges have no common sensitivities */
		return -EINVAL;

	/* Take the greatest of the low */
	dr->level[0].sens = max(r1->level[0].sens, r2->level[0].sens);

	/* Take the least of the high */
	dr->level[1].sens = min(r1->level[1].sens, r2->level[1].sens);

	rc = ebitmap_and(&dr->level[0].cat,
			 &r1->level[0].cat, &r2->level[0].cat);
	if (rc)
		goto out;

	rc = ebitmap_and(&dr->level[1].cat,
			 &r1->level[1].cat, &r2->level[1].cat);
	if (rc)
		goto out;

out:
	return rc;
}

static inline int mls_context_cmp(const struct context *c1, const struct context *c2)
{
	return ((c1->range.level[0].sens == c2->range.level[0].sens) &&
		ebitmap_cmp(&c1->range.level[0].cat, &c2->range.level[0].cat) &&
		(c1->range.level[1].sens == c2->range.level[1].sens) &&
		ebitmap_cmp(&c1->range.level[1].cat, &c2->range.level[1].cat));
}

static inline void mls_context_destroy(struct context *c)
{
	ebitmap_destroy(&c->range.level[0].cat);
	ebitmap_destroy(&c->range.level[1].cat);
	mls_context_init(c);
}

static inline void context_init(struct context *c)
{
	memset(c, 0, sizeof(*c));
}

static inline int context_cpy(struct context *dst, const struct context *src)
{
	int rc;

	dst->user = src->user;
	dst->role = src->role;
	dst->type = src->type;
	if (src->str) {
		dst->str = kstrdup(src->str, GFP_ATOMIC);
		if (!dst->str)
			return -ENOMEM;
		dst->len = src->len;
	} else {
		dst->str = NULL;
		dst->len = 0;
	}
	rc = mls_context_cpy(dst, src);
	if (rc) {
		kfree(dst->str);
		dst->str = NULL;
		dst->len = 0;
		return rc;
	}
	return 0;
}

static inline void context_destroy(struct context *c)
{
	c->user = c->role = c->type = 0;
	kfree(c->str);
	c->str = NULL;
	c->len = 0;
	mls_context_destroy(c);
}

static inline int context_cmp(const struct context *c1, const struct context *c2)
{
	if (c1->len && c2->len)
		return (c1->len == c2->len && !strcmp(c1->str, c2->str));
	if (c1->len || c2->len)
		return 0;
	return ((c1->user == c2->user) &&
		(c1->role == c2->role) &&
		(c1->type == c2->type) &&
		mls_context_cmp(c1, c2));
}

u32 context_compute_hash(const struct context *c);

#endif	/* _SS_CONTEXT_H_ */

