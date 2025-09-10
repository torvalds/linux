/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor contexts used to associate "labels" to objects.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#ifndef __AA_CONTEXT_H
#define __AA_CONTEXT_H

#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "label.h"
#include "policy_ns.h"
#include "task.h"

static inline struct aa_label *cred_label(const struct cred *cred)
{
	struct aa_label **blob = cred->security + apparmor_blob_sizes.lbs_cred;

	AA_BUG(!blob);
	return *blob;
}

static inline void set_cred_label(const struct cred *cred,
				  struct aa_label *label)
{
	struct aa_label **blob = cred->security + apparmor_blob_sizes.lbs_cred;

	AA_BUG(!blob);
	*blob = label;
}

/**
 * aa_cred_raw_label - obtain cred's label
 * @cred: cred to obtain label from  (NOT NULL)
 *
 * Returns: confining label
 *
 * does NOT increment reference count
 */
static inline struct aa_label *aa_cred_raw_label(const struct cred *cred)
{
	struct aa_label *label = cred_label(cred);

	AA_BUG(!label);
	return label;
}

/**
 * aa_get_newest_cred_label - obtain the newest label on a cred
 * @cred: cred to obtain label from (NOT NULL)
 *
 * Returns: newest version of confining label
 */
static inline struct aa_label *aa_get_newest_cred_label(const struct cred *cred)
{
	return aa_get_newest_label(aa_cred_raw_label(cred));
}

static inline struct aa_label *aa_get_newest_cred_label_condref(const struct cred *cred,
								bool *needput)
{
	struct aa_label *l = aa_cred_raw_label(cred);

	if (unlikely(label_is_stale(l))) {
		*needput = true;
		return aa_get_newest_label(l);
	}

	*needput = false;
	return l;
}

static inline void aa_put_label_condref(struct aa_label *l, bool needput)
{
	if (unlikely(needput))
		aa_put_label(l);
}

/**
 * aa_current_raw_label - find the current tasks confining label
 *
 * Returns: up to date confining label or the ns unconfined label (NOT NULL)
 *
 * This fn will not update the tasks cred to the most up to date version
 * of the label so it is safe to call when inside of locks.
 */
static inline struct aa_label *aa_current_raw_label(void)
{
	return aa_cred_raw_label(current_cred());
}

/**
 * aa_get_current_label - get the newest version of the current tasks label
 *
 * Returns: newest version of confining label (NOT NULL)
 *
 * This fn will not update the tasks cred, so it is safe inside of locks
 *
 * The returned reference must be put with aa_put_label()
 */
static inline struct aa_label *aa_get_current_label(void)
{
	struct aa_label *l = aa_current_raw_label();

	if (label_is_stale(l))
		return aa_get_newest_label(l);
	return aa_get_label(l);
}

/**
 * __end_current_label_crit_section - end crit section begun with __begin_...
 * @label: label obtained from __begin_current_label_crit_section
 * @needput: output: bool set by __begin_current_label_crit_section
 *
 * Returns: label to use for this crit section
 */
static inline void __end_current_label_crit_section(struct aa_label *label,
						    bool needput)
{
	if (unlikely(needput))
		aa_put_label(label);
}

/**
 * end_current_label_crit_section - put a reference found with begin_current_label..
 * @label: label reference to put
 *
 * Should only be used with a reference obtained with
 * begin_current_label_crit_section and never used in situations where the
 * task cred may be updated
 */
static inline void end_current_label_crit_section(struct aa_label *label)
{
	if (label != aa_current_raw_label())
		aa_put_label(label);
}

/**
 * __begin_current_label_crit_section - current's confining label
 * @needput: store whether the label needs to be put when ending crit section
 *
 * Returns: up to date confining label or the ns unconfined label (NOT NULL)
 *
 * safe to call inside locks
 *
 * The returned reference must be put with __end_current_label_crit_section()
 * This must NOT be used if the task cred could be updated within the
 * critical section between __begin_current_label_crit_section() ..
 * __end_current_label_crit_section()
 */
static inline struct aa_label *__begin_current_label_crit_section(bool *needput)
{
	struct aa_label *label = aa_current_raw_label();

	if (label_is_stale(label)) {
		*needput = true;
		return aa_get_newest_label(label);
	}

	*needput = false;
	return label;
}

/**
 * begin_current_label_crit_section - current's confining label and update it
 *
 * Returns: up to date confining label or the ns unconfined label (NOT NULL)
 *
 * Not safe to call inside locks
 *
 * The returned reference must be put with end_current_label_crit_section()
 * This must NOT be used if the task cred could be updated within the
 * critical section between begin_current_label_crit_section() ..
 * end_current_label_crit_section()
 */
static inline struct aa_label *begin_current_label_crit_section(void)
{
	struct aa_label *label = aa_current_raw_label();

	might_sleep();

	if (label_is_stale(label)) {
		label = aa_get_newest_label(label);
		if (aa_replace_current_label(label) == 0)
			/* task cred will keep the reference */
			aa_put_label(label);
	}

	return label;
}

static inline struct aa_ns *aa_get_current_ns(void)
{
	struct aa_label *label;
	struct aa_ns *ns;
	bool needput;

	label  = __begin_current_label_crit_section(&needput);
	ns = aa_get_ns(labels_ns(label));
	__end_current_label_crit_section(label, needput);

	return ns;
}

#endif /* __AA_CONTEXT_H */
