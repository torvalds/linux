/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor label definitions
 *
 * Copyright 2017 Canonical Ltd.
 */

#ifndef __AA_LABEL_H
#define __AA_LABEL_H

#include <linux/atomic.h>
#include <linux/audit.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>

#include "apparmor.h"
#include "lib.h"

struct aa_ns;

#define LOCAL_VEC_ENTRIES 8
#define DEFINE_VEC(T, V)						\
	struct aa_ ## T *(_ ## V ## _localtmp)[LOCAL_VEC_ENTRIES];	\
	struct aa_ ## T **(V)

#define vec_setup(T, V, N, GFP)						\
({									\
	if ((N) <= LOCAL_VEC_ENTRIES) {					\
		typeof(N) i;						\
		(V) = (_ ## V ## _localtmp);				\
		for (i = 0; i < (N); i++)				\
			(V)[i] = NULL;					\
	} else								\
		(V) = kzalloc(sizeof(struct aa_ ## T *) * (N), (GFP));	\
	(V) ? 0 : -ENOMEM;						\
})

#define vec_cleanup(T, V, N)						\
do {									\
	int i;								\
	for (i = 0; i < (N); i++) {					\
		if (!IS_ERR_OR_NULL((V)[i]))				\
			aa_put_ ## T((V)[i]);				\
	}								\
	if ((V) != _ ## V ## _localtmp)					\
		kfree(V);						\
} while (0)

#define vec_last(VEC, SIZE) ((VEC)[(SIZE) - 1])
#define vec_ns(VEC, SIZE) (vec_last((VEC), (SIZE))->ns)
#define vec_labelset(VEC, SIZE) (&vec_ns((VEC), (SIZE))->labels)
#define cleanup_domain_vec(V, L) cleanup_label_vec((V), (L)->size)

struct aa_profile;
#define VEC_FLAG_TERMINATE 1
int aa_vec_unique(struct aa_profile **vec, int n, int flags);
struct aa_label *aa_vec_find_or_create_label(struct aa_profile **vec, int len,
					     gfp_t gfp);
#define aa_sort_and_merge_vec(N, V) \
	aa_sort_and_merge_profiles((N), (struct aa_profile **)(V))


/* struct aa_labelset - set of labels for a namespace
 *
 * Labels are reference counted; aa_labelset does not contribute to label
 * reference counts. Once a label's last refcount is put it is removed from
 * the set.
 */
struct aa_labelset {
	rwlock_t lock;

	struct rb_root root;
};

#define __labelset_for_each(LS, N) \
	for ((N) = rb_first(&(LS)->root); (N); (N) = rb_next(N))

enum label_flags {
	FLAG_HAT = 1,			/* profile is a hat */
	FLAG_UNCONFINED = 2,		/* label unconfined only if all */
	FLAG_NULL = 4,			/* profile is null learning profile */
	FLAG_IX_ON_NAME_ERROR = 8,	/* fallback to ix on name lookup fail */
	FLAG_IMMUTIBLE = 0x10,		/* don't allow changes/replacement */
	FLAG_USER_DEFINED = 0x20,	/* user based profile - lower privs */
	FLAG_NO_LIST_REF = 0x40,	/* list doesn't keep profile ref */
	FLAG_NS_COUNT = 0x80,		/* carries NS ref count */
	FLAG_IN_TREE = 0x100,		/* label is in tree */
	FLAG_PROFILE = 0x200,		/* label is a profile */
	FLAG_EXPLICIT = 0x400,		/* explicit static label */
	FLAG_STALE = 0x800,		/* replaced/removed */
	FLAG_RENAMED = 0x1000,		/* label has renaming in it */
	FLAG_REVOKED = 0x2000,		/* label has revocation in it */
	FLAG_DEBUG1 = 0x4000,
	FLAG_DEBUG2 = 0x8000,

	/* These flags must correspond with PATH_flags */
	/* TODO: add new path flags */
};

struct aa_label;
struct aa_proxy {
	struct kref count;
	struct aa_label __rcu *label;
};

struct label_it {
	int i, j;
};

/* struct aa_label - lazy labeling struct
 * @count: ref count of active users
 * @node: rbtree position
 * @rcu: rcu callback struct
 * @proxy: is set to the label that replaced this label
 * @hname: text representation of the label (MAYBE_NULL)
 * @flags: stale and other flags - values may change under label set lock
 * @secid: secid that references this label
 * @size: number of entries in @ent[]
 * @ent: set of profiles for label, actual size determined by @size
 */
struct aa_label {
	struct kref count;
	struct rb_node node;
	struct rcu_head rcu;
	struct aa_proxy *proxy;
	__counted char *hname;
	long flags;
	u32 secid;
	int size;
	struct aa_profile *vec[];
};

#define last_error(E, FN)				\
do {							\
	int __subE = (FN);				\
	if (__subE)					\
		(E) = __subE;				\
} while (0)

#define label_isprofile(X) ((X)->flags & FLAG_PROFILE)
#define label_unconfined(X) ((X)->flags & FLAG_UNCONFINED)
#define unconfined(X) label_unconfined(X)
#define label_is_stale(X) ((X)->flags & FLAG_STALE)
#define __label_make_stale(X) ((X)->flags |= FLAG_STALE)
#define labels_ns(X) (vec_ns(&((X)->vec[0]), (X)->size))
#define labels_set(X) (&labels_ns(X)->labels)
#define labels_view(X) labels_ns(X)
#define labels_profile(X) ((X)->vec[(X)->size - 1])


int aa_label_next_confined(struct aa_label *l, int i);

/* for each profile in a label */
#define label_for_each(I, L, P)						\
	for ((I).i = 0; ((P) = (L)->vec[(I).i]); ++((I).i))

/* assumes break/goto ended label_for_each */
#define label_for_each_cont(I, L, P)					\
	for (++((I).i); ((P) = (L)->vec[(I).i]); ++((I).i))

#define next_comb(I, L1, L2)						\
do {									\
	(I).j++;							\
	if ((I).j >= (L2)->size) {					\
		(I).i++;						\
		(I).j = 0;						\
	}								\
} while (0)


/* for each combination of P1 in L1, and P2 in L2 */
#define label_for_each_comb(I, L1, L2, P1, P2)				\
for ((I).i = (I).j = 0;							\
	((P1) = (L1)->vec[(I).i]) && ((P2) = (L2)->vec[(I).j]);		\
	(I) = next_comb(I, L1, L2))

#define fn_for_each_comb(L1, L2, P1, P2, FN)				\
({									\
	struct label_it i;						\
	int __E = 0;							\
	label_for_each_comb(i, (L1), (L2), (P1), (P2)) {		\
		last_error(__E, (FN));					\
	}								\
	__E;								\
})

/* for each profile that is enforcing confinement in a label */
#define label_for_each_confined(I, L, P)				\
	for ((I).i = aa_label_next_confined((L), 0);			\
	     ((P) = (L)->vec[(I).i]);					\
	     (I).i = aa_label_next_confined((L), (I).i + 1))

#define label_for_each_in_merge(I, A, B, P)				\
	for ((I).i = (I).j = 0;						\
	     ((P) = aa_label_next_in_merge(&(I), (A), (B)));		\
	     )

#define label_for_each_not_in_set(I, SET, SUB, P)			\
	for ((I).i = (I).j = 0;						\
	     ((P) = __aa_label_next_not_in_set(&(I), (SET), (SUB)));	\
	     )

#define next_in_ns(i, NS, L)						\
({									\
	typeof(i) ___i = (i);						\
	while ((L)->vec[___i] && (L)->vec[___i]->ns != (NS))		\
		(___i)++;						\
	(___i);								\
})

#define label_for_each_in_ns(I, NS, L, P)				\
	for ((I).i = next_in_ns(0, (NS), (L));				\
	     ((P) = (L)->vec[(I).i]);					\
	     (I).i = next_in_ns((I).i + 1, (NS), (L)))

#define fn_for_each_in_ns(L, P, FN)					\
({									\
	struct label_it __i;						\
	struct aa_ns *__ns = labels_ns(L);				\
	int __E = 0;							\
	label_for_each_in_ns(__i, __ns, (L), (P)) {			\
		last_error(__E, (FN));					\
	}								\
	__E;								\
})


#define fn_for_each_XXX(L, P, FN, ...)					\
({									\
	struct label_it i;						\
	int __E = 0;							\
	label_for_each ## __VA_ARGS__(i, (L), (P)) {			\
		last_error(__E, (FN));					\
	}								\
	__E;								\
})

#define fn_for_each(L, P, FN) fn_for_each_XXX(L, P, FN)
#define fn_for_each_confined(L, P, FN) fn_for_each_XXX(L, P, FN, _confined)

#define fn_for_each2_XXX(L1, L2, P, FN, ...)				\
({									\
	struct label_it i;						\
	int __E = 0;							\
	label_for_each ## __VA_ARGS__(i, (L1), (L2), (P)) {		\
		last_error(__E, (FN));					\
	}								\
	__E;								\
})

#define fn_for_each_in_merge(L1, L2, P, FN)				\
	fn_for_each2_XXX((L1), (L2), P, FN, _in_merge)
#define fn_for_each_not_in_set(L1, L2, P, FN)				\
	fn_for_each2_XXX((L1), (L2), P, FN, _not_in_set)

#define LABEL_MEDIATES(L, C)						\
({									\
	struct aa_profile *profile;					\
	struct label_it i;						\
	int ret = 0;							\
	label_for_each(i, (L), profile) {				\
		if (RULE_MEDIATES(&profile->rules, (C))) {		\
			ret = 1;					\
			break;						\
		}							\
	}								\
	ret;								\
})


void aa_labelset_destroy(struct aa_labelset *ls);
void aa_labelset_init(struct aa_labelset *ls);
void __aa_labelset_update_subtree(struct aa_ns *ns);

void aa_label_destroy(struct aa_label *label);
void aa_label_free(struct aa_label *label);
void aa_label_kref(struct kref *kref);
bool aa_label_init(struct aa_label *label, int size, gfp_t gfp);
struct aa_label *aa_label_alloc(int size, struct aa_proxy *proxy, gfp_t gfp);

bool aa_label_is_subset(struct aa_label *set, struct aa_label *sub);
bool aa_label_is_unconfined_subset(struct aa_label *set, struct aa_label *sub);
struct aa_profile *__aa_label_next_not_in_set(struct label_it *I,
					     struct aa_label *set,
					     struct aa_label *sub);
bool aa_label_remove(struct aa_label *label);
struct aa_label *aa_label_insert(struct aa_labelset *ls, struct aa_label *l);
bool aa_label_replace(struct aa_label *old, struct aa_label *new);
bool aa_label_make_newest(struct aa_labelset *ls, struct aa_label *old,
			  struct aa_label *new);

struct aa_label *aa_label_find(struct aa_label *l);

struct aa_profile *aa_label_next_in_merge(struct label_it *I,
					  struct aa_label *a,
					  struct aa_label *b);
struct aa_label *aa_label_find_merge(struct aa_label *a, struct aa_label *b);
struct aa_label *aa_label_merge(struct aa_label *a, struct aa_label *b,
				gfp_t gfp);


bool aa_update_label_name(struct aa_ns *ns, struct aa_label *label, gfp_t gfp);

#define FLAGS_NONE 0
#define FLAG_SHOW_MODE 1
#define FLAG_VIEW_SUBNS 2
#define FLAG_HIDDEN_UNCONFINED 4
#define FLAG_ABS_ROOT 8
int aa_label_snxprint(char *str, size_t size, struct aa_ns *view,
		      struct aa_label *label, int flags);
int aa_label_asxprint(char **strp, struct aa_ns *ns, struct aa_label *label,
		      int flags, gfp_t gfp);
int aa_label_acntsxprint(char __counted **strp, struct aa_ns *ns,
			 struct aa_label *label, int flags, gfp_t gfp);
void aa_label_xaudit(struct audit_buffer *ab, struct aa_ns *ns,
		     struct aa_label *label, int flags, gfp_t gfp);
void aa_label_seq_xprint(struct seq_file *f, struct aa_ns *ns,
			 struct aa_label *label, int flags, gfp_t gfp);
void aa_label_xprintk(struct aa_ns *ns, struct aa_label *label, int flags,
		      gfp_t gfp);
void aa_label_audit(struct audit_buffer *ab, struct aa_label *label, gfp_t gfp);
void aa_label_seq_print(struct seq_file *f, struct aa_label *label, gfp_t gfp);
void aa_label_printk(struct aa_label *label, gfp_t gfp);

struct aa_label *aa_label_strn_parse(struct aa_label *base, const char *str,
				     size_t n, gfp_t gfp, bool create,
				     bool force_stack);
struct aa_label *aa_label_parse(struct aa_label *base, const char *str,
				gfp_t gfp, bool create, bool force_stack);

static inline const char *aa_label_strn_split(const char *str, int n)
{
	const char *pos;
	aa_state_t state;

	state = aa_dfa_matchn_until(stacksplitdfa, DFA_START, str, n, &pos);
	if (!ACCEPT_TABLE(stacksplitdfa)[state])
		return NULL;

	return pos - 3;
}

static inline const char *aa_label_str_split(const char *str)
{
	const char *pos;
	aa_state_t state;

	state = aa_dfa_match_until(stacksplitdfa, DFA_START, str, &pos);
	if (!ACCEPT_TABLE(stacksplitdfa)[state])
		return NULL;

	return pos - 3;
}



struct aa_perms;
struct aa_ruleset;
int aa_label_match(struct aa_profile *profile, struct aa_ruleset *rules,
		   struct aa_label *label, aa_state_t state, bool subns,
		   u32 request, struct aa_perms *perms);


/**
 * __aa_get_label - get a reference count to uncounted label reference
 * @l: reference to get a count on
 *
 * Returns: pointer to reference OR NULL if race is lost and reference is
 *          being repeated.
 * Requires: lock held, and the return code MUST be checked
 */
static inline struct aa_label *__aa_get_label(struct aa_label *l)
{
	if (l && kref_get_unless_zero(&l->count))
		return l;

	return NULL;
}

static inline struct aa_label *aa_get_label(struct aa_label *l)
{
	if (l)
		kref_get(&(l->count));

	return l;
}


/**
 * aa_get_label_rcu - increment refcount on a label that can be replaced
 * @l: pointer to label that can be replaced (NOT NULL)
 *
 * Returns: pointer to a refcounted label.
 *     else NULL if no label
 */
static inline struct aa_label *aa_get_label_rcu(struct aa_label __rcu **l)
{
	struct aa_label *c;

	rcu_read_lock();
	do {
		c = rcu_dereference(*l);
	} while (c && !kref_get_unless_zero(&c->count));
	rcu_read_unlock();

	return c;
}

/**
 * aa_get_newest_label - find the newest version of @l
 * @l: the label to check for newer versions of
 *
 * Returns: refcounted newest version of @l taking into account
 *          replacement, renames and removals
 *          return @l.
 */
static inline struct aa_label *aa_get_newest_label(struct aa_label *l)
{
	if (!l)
		return NULL;

	if (label_is_stale(l)) {
		struct aa_label *tmp;

		AA_BUG(!l->proxy);
		AA_BUG(!l->proxy->label);
		/* BUG: only way this can happen is @l ref count and its
		 * replacement count have gone to 0 and are on their way
		 * to destruction. ie. we have a refcounting error
		 */
		tmp = aa_get_label_rcu(&l->proxy->label);
		AA_BUG(!tmp);

		return tmp;
	}

	return aa_get_label(l);
}

static inline void aa_put_label(struct aa_label *l)
{
	if (l)
		kref_put(&l->count, aa_label_kref);
}


struct aa_proxy *aa_alloc_proxy(struct aa_label *l, gfp_t gfp);
void aa_proxy_kref(struct kref *kref);

static inline struct aa_proxy *aa_get_proxy(struct aa_proxy *proxy)
{
	if (proxy)
		kref_get(&(proxy->count));

	return proxy;
}

static inline void aa_put_proxy(struct aa_proxy *proxy)
{
	if (proxy)
		kref_put(&proxy->count, aa_proxy_kref);
}

void __aa_proxy_redirect(struct aa_label *orig, struct aa_label *new);

#endif /* __AA_LABEL_H */
