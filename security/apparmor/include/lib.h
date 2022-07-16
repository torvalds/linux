/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor lib definitions
 *
 * 2017 Canonical Ltd.
 */

#ifndef __AA_LIB_H
#define __AA_LIB_H

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/lsm_hooks.h>

#include "match.h"

/*
 * DEBUG remains global (no per profile flag) since it is mostly used in sysctl
 * which is not related to profile accesses.
 */

#define DEBUG_ON (aa_g_debug)
/*
 * split individual debug cases out in preparation for finer grained
 * debug controls in the future.
 */
#define AA_DEBUG_LABEL DEBUG_ON
#define dbg_printk(__fmt, __args...) pr_debug(__fmt, ##__args)
#define AA_DEBUG(fmt, args...)						\
	do {								\
		if (DEBUG_ON)						\
			pr_debug_ratelimited("AppArmor: " fmt, ##args);	\
	} while (0)

#define AA_WARN(X) WARN((X), "APPARMOR WARN %s: %s\n", __func__, #X)

#define AA_BUG(X, args...)						    \
	do {								    \
		_Pragma("GCC diagnostic ignored \"-Wformat-zero-length\""); \
		AA_BUG_FMT((X), "" args);				    \
		_Pragma("GCC diagnostic warning \"-Wformat-zero-length\""); \
	} while (0)
#ifdef CONFIG_SECURITY_APPARMOR_DEBUG_ASSERTS
#define AA_BUG_FMT(X, fmt, args...)					\
	WARN((X), "AppArmor WARN %s: (" #X "): " fmt, __func__, ##args)
#else
#define AA_BUG_FMT(X, fmt, args...) no_printk(fmt, ##args)
#endif

#define AA_ERROR(fmt, args...)						\
	pr_err_ratelimited("AppArmor: " fmt, ##args)

/* Flag indicating whether initialization completed */
extern int apparmor_initialized;

/* fn's in lib */
const char *skipn_spaces(const char *str, size_t n);
char *aa_split_fqname(char *args, char **ns_name);
const char *aa_splitn_fqname(const char *fqname, size_t n, const char **ns_name,
			     size_t *ns_len);
void aa_info_message(const char *str);

/* Security blob offsets */
extern struct lsm_blob_sizes apparmor_blob_sizes;

/**
 * aa_strneq - compare null terminated @str to a non null terminated substring
 * @str: a null terminated string
 * @sub: a substring, not necessarily null terminated
 * @len: length of @sub to compare
 *
 * The @str string must be full consumed for this to be considered a match
 */
static inline bool aa_strneq(const char *str, const char *sub, int len)
{
	return !strncmp(str, sub, len) && !str[len];
}

/**
 * aa_dfa_null_transition - step to next state after null character
 * @dfa: the dfa to match against
 * @start: the state of the dfa to start matching in
 *
 * aa_dfa_null_transition transitions to the next state after a null
 * character which is not used in standard matching and is only
 * used to separate pairs.
 */
static inline aa_state_t aa_dfa_null_transition(struct aa_dfa *dfa,
						aa_state_t start)
{
	/* the null transition only needs the string's null terminator byte */
	return aa_dfa_next(dfa, start, 0);
}

static inline bool path_mediated_fs(struct dentry *dentry)
{
	return !(dentry->d_sb->s_flags & SB_NOUSER);
}

struct aa_str_table {
	int size;
	char **table;
};

void aa_free_str_table(struct aa_str_table *table);

struct counted_str {
	struct kref count;
	char name[];
};

#define str_to_counted(str) \
	((struct counted_str *)(str - offsetof(struct counted_str, name)))

#define __counted	/* atm just a notation */

void aa_str_kref(struct kref *kref);
char *aa_str_alloc(int size, gfp_t gfp);


static inline __counted char *aa_get_str(__counted char *str)
{
	if (str)
		kref_get(&(str_to_counted(str)->count));

	return str;
}

static inline void aa_put_str(__counted char *str)
{
	if (str)
		kref_put(&str_to_counted(str)->count, aa_str_kref);
}


/* struct aa_policy - common part of both namespaces and profiles
 * @name: name of the object
 * @hname - The hierarchical name
 * @list: list policy object is on
 * @profiles: head of the profiles list contained in the object
 */
struct aa_policy {
	const char *name;
	__counted char *hname;
	struct list_head list;
	struct list_head profiles;
};

/**
 * basename - find the last component of an hname
 * @name: hname to find the base profile name component of  (NOT NULL)
 *
 * Returns: the tail (base profile name) name component of an hname
 */
static inline const char *basename(const char *hname)
{
	char *split;

	hname = strim((char *)hname);
	for (split = strstr(hname, "//"); split; split = strstr(hname, "//"))
		hname = split + 2;

	return hname;
}

/**
 * __policy_find - find a policy by @name on a policy list
 * @head: list to search  (NOT NULL)
 * @name: name to search for  (NOT NULL)
 *
 * Requires: rcu_read_lock be held
 *
 * Returns: unrefcounted policy that match @name or NULL if not found
 */
static inline struct aa_policy *__policy_find(struct list_head *head,
					      const char *name)
{
	struct aa_policy *policy;

	list_for_each_entry_rcu(policy, head, list) {
		if (!strcmp(policy->name, name))
			return policy;
	}
	return NULL;
}

/**
 * __policy_strn_find - find a policy that's name matches @len chars of @str
 * @head: list to search  (NOT NULL)
 * @str: string to search for  (NOT NULL)
 * @len: length of match required
 *
 * Requires: rcu_read_lock be held
 *
 * Returns: unrefcounted policy that match @str or NULL if not found
 *
 * if @len == strlen(@strlen) then this is equiv to __policy_find
 * other wise it allows searching for policy by a partial match of name
 */
static inline struct aa_policy *__policy_strn_find(struct list_head *head,
					    const char *str, int len)
{
	struct aa_policy *policy;

	list_for_each_entry_rcu(policy, head, list) {
		if (aa_strneq(policy->name, str, len))
			return policy;
	}

	return NULL;
}

bool aa_policy_init(struct aa_policy *policy, const char *prefix,
		    const char *name, gfp_t gfp);
void aa_policy_destroy(struct aa_policy *policy);


/*
 * fn_label_build - abstract out the build of a label transition
 * @L: label the transition is being computed for
 * @P: profile parameter derived from L by this macro, can be passed to FN
 * @GFP: memory allocation type to use
 * @FN: fn to call for each profile transition. @P is set to the profile
 *
 * Returns: new label on success
 *          ERR_PTR if build @FN fails
 *          NULL if label_build fails due to low memory conditions
 *
 * @FN must return a label or ERR_PTR on failure. NULL is not allowed
 */
#define fn_label_build(L, P, GFP, FN)					\
({									\
	__label__ __cleanup, __done;					\
	struct aa_label *__new_;					\
									\
	if ((L)->size > 1) {						\
		/* TODO: add cache of transitions already done */	\
		struct label_it __i;					\
		int __j, __k, __count;					\
		DEFINE_VEC(label, __lvec);				\
		DEFINE_VEC(profile, __pvec);				\
		if (vec_setup(label, __lvec, (L)->size, (GFP)))	{	\
			__new_ = NULL;					\
			goto __done;					\
		}							\
		__j = 0;						\
		label_for_each(__i, (L), (P)) {				\
			__new_ = (FN);					\
			AA_BUG(!__new_);				\
			if (IS_ERR(__new_))				\
				goto __cleanup;				\
			__lvec[__j++] = __new_;				\
		}							\
		for (__j = __count = 0; __j < (L)->size; __j++)		\
			__count += __lvec[__j]->size;			\
		if (!vec_setup(profile, __pvec, __count, (GFP))) {	\
			for (__j = __k = 0; __j < (L)->size; __j++) {	\
				label_for_each(__i, __lvec[__j], (P))	\
					__pvec[__k++] = aa_get_profile(P); \
			}						\
			__count -= aa_vec_unique(__pvec, __count, 0);	\
			if (__count > 1) {				\
				__new_ = aa_vec_find_or_create_label(__pvec,\
						     __count, (GFP));	\
				/* only fails if out of Mem */		\
				if (!__new_)				\
					__new_ = NULL;			\
			} else						\
				__new_ = aa_get_label(&__pvec[0]->label); \
			vec_cleanup(profile, __pvec, __count);		\
		} else							\
			__new_ = NULL;					\
__cleanup:								\
		vec_cleanup(label, __lvec, (L)->size);			\
	} else {							\
		(P) = labels_profile(L);				\
		__new_ = (FN);						\
	}								\
__done:									\
	if (!__new_)							\
		AA_DEBUG("label build failed\n");			\
	(__new_);							\
})


#define __fn_build_in_ns(NS, P, NS_FN, OTHER_FN)			\
({									\
	struct aa_label *__new;						\
	if ((P)->ns != (NS))						\
		__new = (OTHER_FN);					\
	else								\
		__new = (NS_FN);					\
	(__new);							\
})

#define fn_label_build_in_ns(L, P, GFP, NS_FN, OTHER_FN)		\
({									\
	fn_label_build((L), (P), (GFP),					\
		__fn_build_in_ns(labels_ns(L), (P), (NS_FN), (OTHER_FN))); \
})

#endif /* __AA_LIB_H */
