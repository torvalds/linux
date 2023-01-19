/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor basic permission sets definitions.
 *
 * Copyright 2017 Canonical Ltd.
 */

#ifndef __AA_PERM_H
#define __AA_PERM_H

#include <linux/fs.h>
#include "label.h"

#define AA_MAY_EXEC		MAY_EXEC
#define AA_MAY_WRITE		MAY_WRITE
#define AA_MAY_READ		MAY_READ
#define AA_MAY_APPEND		MAY_APPEND

#define AA_MAY_CREATE		0x0010
#define AA_MAY_DELETE		0x0020
#define AA_MAY_OPEN		0x0040
#define AA_MAY_RENAME		0x0080		/* pair */

#define AA_MAY_SETATTR		0x0100		/* meta write */
#define AA_MAY_GETATTR		0x0200		/* meta read */
#define AA_MAY_SETCRED		0x0400		/* security cred/attr */
#define AA_MAY_GETCRED		0x0800

#define AA_MAY_CHMOD		0x1000		/* pair */
#define AA_MAY_CHOWN		0x2000		/* pair */
#define AA_MAY_CHGRP		0x4000		/* pair */
#define AA_MAY_LOCK		0x8000		/* LINK_SUBSET overlaid */

#define AA_EXEC_MMAP		0x00010000
#define AA_MAY_MPROT		0x00020000	/* extend conditions */
#define AA_MAY_LINK		0x00040000	/* pair */
#define AA_MAY_SNAPSHOT		0x00080000	/* pair */

#define AA_MAY_DELEGATE
#define AA_CONT_MATCH		0x08000000

#define AA_MAY_STACK		0x10000000
#define AA_MAY_ONEXEC		0x20000000 /* either stack or change_profile */
#define AA_MAY_CHANGE_PROFILE	0x40000000
#define AA_MAY_CHANGEHAT	0x80000000

#define AA_LINK_SUBSET		AA_MAY_LOCK	/* overlaid */


#define PERMS_CHRS_MASK (MAY_READ | MAY_WRITE | AA_MAY_CREATE |		\
			 AA_MAY_DELETE | AA_MAY_LINK | AA_MAY_LOCK |	\
			 AA_MAY_EXEC | AA_EXEC_MMAP | AA_MAY_APPEND)

#define PERMS_NAMES_MASK (PERMS_CHRS_MASK | AA_MAY_OPEN | AA_MAY_RENAME |     \
			  AA_MAY_SETATTR | AA_MAY_GETATTR | AA_MAY_SETCRED | \
			  AA_MAY_GETCRED | AA_MAY_CHMOD | AA_MAY_CHOWN | \
			  AA_MAY_CHGRP | AA_MAY_MPROT | AA_MAY_SNAPSHOT | \
			  AA_MAY_STACK | AA_MAY_ONEXEC |		\
			  AA_MAY_CHANGE_PROFILE | AA_MAY_CHANGEHAT)

extern const char aa_file_perm_chrs[];
extern const char *aa_file_perm_names[];

struct aa_perms {
	u32 allow;
	u32 deny;	/* explicit deny, or conflict if allow also set */

	u32 subtree;	/* allow perm on full subtree only when allow is set */
	u32 cond;	/* set only when ~allow and ~deny */

	u32 kill;	/* set only when ~allow | deny */
	u32 complain;	/* accumulates only used when ~allow & ~deny */
	u32 prompt;	/* accumulates only used when ~allow & ~deny */

	u32 audit;	/* set only when allow is set */
	u32 quiet;	/* set only when ~allow | deny */
	u32 hide;	/* set only when  ~allow | deny */


	u32 xindex;
	u32 tag;	/* tag string index, if present */
	u32 label;	/* label string index, if present */
};

/*
 * Indexes are broken into a 24 bit index and 8 bit flag.
 * For the index to be valid there must be a value in the flag
 */
#define AA_INDEX_MASK			0x00ffffff
#define AA_INDEX_FLAG_MASK		0xff000000
#define AA_INDEX_NONE			0

#define ALL_PERMS_MASK 0xffffffff
extern struct aa_perms nullperms;
extern struct aa_perms allperms;

/**
 * aa_perms_accum_raw - accumulate perms with out masking off overlapping perms
 * @accum - perms struct to accumulate into
 * @addend - perms struct to add to @accum
 */
static inline void aa_perms_accum_raw(struct aa_perms *accum,
				      struct aa_perms *addend)
{
	accum->deny |= addend->deny;
	accum->allow &= addend->allow & ~addend->deny;
	accum->audit |= addend->audit & addend->allow;
	accum->quiet &= addend->quiet & ~addend->allow;
	accum->kill |= addend->kill & ~addend->allow;
	accum->complain |= addend->complain & ~addend->allow & ~addend->deny;
	accum->cond |= addend->cond & ~addend->allow & ~addend->deny;
	accum->hide &= addend->hide & ~addend->allow;
	accum->prompt |= addend->prompt & ~addend->allow & ~addend->deny;
	accum->subtree |= addend->subtree & ~addend->deny;

	if (!accum->xindex)
		accum->xindex = addend->xindex;
	if (!accum->tag)
		accum->tag = addend->tag;
	if (!accum->label)
		accum->label = addend->label;
}

/**
 * aa_perms_accum - accumulate perms, masking off overlapping perms
 * @accum - perms struct to accumulate into
 * @addend - perms struct to add to @accum
 */
static inline void aa_perms_accum(struct aa_perms *accum,
				  struct aa_perms *addend)
{
	accum->deny |= addend->deny;
	accum->allow &= addend->allow & ~accum->deny;
	accum->audit |= addend->audit & accum->allow;
	accum->quiet &= addend->quiet & ~accum->allow;
	accum->kill |= addend->kill & ~accum->allow;
	accum->complain |= addend->complain & ~accum->allow & ~accum->deny;
	accum->cond |= addend->cond & ~accum->allow & ~accum->deny;
	accum->hide &= addend->hide & ~accum->allow;
	accum->prompt |= addend->prompt & ~accum->allow & ~accum->deny;
	accum->subtree &= addend->subtree & ~accum->deny;

	if (!accum->xindex)
		accum->xindex = addend->xindex;
	if (!accum->tag)
		accum->tag = addend->tag;
	if (!accum->label)
		accum->label = addend->label;
}

#define xcheck(FN1, FN2)	\
({				\
	int e, error = FN1;	\
	e = FN2;		\
	if (e)			\
		error = e;	\
	error;			\
})


/*
 * TODO: update for labels pointing to labels instead of profiles
 * TODO: optimize the walk, currently does subwalk of L2 for each P in L1
 * gah this doesn't allow for label compound check!!!!
 */
#define xcheck_ns_profile_profile(P1, P2, FN, args...)		\
({								\
	int ____e = 0;						\
	if (P1->ns == P2->ns)					\
		____e = FN((P1), (P2), args);			\
	(____e);						\
})

#define xcheck_ns_profile_label(P, L, FN, args...)		\
({								\
	struct aa_profile *__p2;				\
	fn_for_each((L), __p2,					\
		    xcheck_ns_profile_profile((P), __p2, (FN), args));	\
})

#define xcheck_ns_labels(L1, L2, FN, args...)			\
({								\
	struct aa_profile *__p1;				\
	fn_for_each((L1), __p1, FN(__p1, (L2), args));		\
})

/* Do the cross check but applying FN at the profiles level */
#define xcheck_labels_profiles(L1, L2, FN, args...)		\
	xcheck_ns_labels((L1), (L2), xcheck_ns_profile_label, (FN), args)

#define xcheck_labels(L1, L2, P, FN1, FN2)			\
	xcheck(fn_for_each((L1), (P), (FN1)), fn_for_each((L2), (P), (FN2)))


extern struct aa_perms default_perms;


void aa_perm_mask_to_str(char *str, size_t str_size, const char *chrs,
			 u32 mask);
void aa_audit_perm_names(struct audit_buffer *ab, const char * const *names,
			 u32 mask);
void aa_audit_perm_mask(struct audit_buffer *ab, u32 mask, const char *chrs,
			u32 chrsmask, const char * const *names, u32 namesmask);
void aa_apply_modes_to_perms(struct aa_profile *profile,
			     struct aa_perms *perms);
void aa_perms_accum(struct aa_perms *accum, struct aa_perms *addend);
void aa_perms_accum_raw(struct aa_perms *accum, struct aa_perms *addend);
void aa_profile_match_label(struct aa_profile *profile,
			    struct aa_ruleset *rules, struct aa_label *label,
			    int type, u32 request, struct aa_perms *perms);
int aa_profile_label_perm(struct aa_profile *profile, struct aa_profile *target,
			  u32 request, int type, u32 *deny,
			  struct common_audit_data *sa);
int aa_check_perms(struct aa_profile *profile, struct aa_perms *perms,
		   u32 request, struct common_audit_data *sa,
		   void (*cb)(struct audit_buffer *, void *));
#endif /* __AA_PERM_H */
