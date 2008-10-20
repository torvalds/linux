/*
 * Copyright (C) 2007 Casey Schaufler <casey@schaufler-ca.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, version 2.
 *
 * Author:
 *      Casey Schaufler <casey@schaufler-ca.com>
 *
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include "smack.h"

struct smack_known smack_known_unset = {
	.smk_next	= NULL,
	.smk_known	= "UNSET",
	.smk_secid	= 1,
	.smk_cipso	= NULL,
};

struct smack_known smack_known_huh = {
	.smk_next	= &smack_known_unset,
	.smk_known	= "?",
	.smk_secid	= 2,
	.smk_cipso	= NULL,
};

struct smack_known smack_known_hat = {
	.smk_next	= &smack_known_huh,
	.smk_known	= "^",
	.smk_secid	= 3,
	.smk_cipso	= NULL,
};

struct smack_known smack_known_star = {
	.smk_next	= &smack_known_hat,
	.smk_known	= "*",
	.smk_secid	= 4,
	.smk_cipso	= NULL,
};

struct smack_known smack_known_floor = {
	.smk_next	= &smack_known_star,
	.smk_known	= "_",
	.smk_secid	= 5,
	.smk_cipso	= NULL,
};

struct smack_known smack_known_invalid = {
	.smk_next	= &smack_known_floor,
	.smk_known	= "",
	.smk_secid	= 6,
	.smk_cipso	= NULL,
};

struct smack_known *smack_known = &smack_known_invalid;

/*
 * The initial value needs to be bigger than any of the
 * known values above.
 */
static u32 smack_next_secid = 10;

/**
 * smk_access - determine if a subject has a specific access to an object
 * @subject_label: a pointer to the subject's Smack label
 * @object_label: a pointer to the object's Smack label
 * @request: the access requested, in "MAY" format
 *
 * This function looks up the subject/object pair in the
 * access rule list and returns 0 if the access is permitted,
 * non zero otherwise.
 *
 * Even though Smack labels are usually shared on smack_list
 * labels that come in off the network can't be imported
 * and added to the list for locking reasons.
 *
 * Therefore, it is necessary to check the contents of the labels,
 * not just the pointer values. Of course, in most cases the labels
 * will be on the list, so checking the pointers may be a worthwhile
 * optimization.
 */
int smk_access(char *subject_label, char *object_label, int request)
{
	u32 may = MAY_NOT;
	struct smk_list_entry *sp;
	struct smack_rule *srp;

	/*
	 * Hardcoded comparisons.
	 *
	 * A star subject can't access any object.
	 */
	if (subject_label == smack_known_star.smk_known ||
	    strcmp(subject_label, smack_known_star.smk_known) == 0)
		return -EACCES;
	/*
	 * A star object can be accessed by any subject.
	 */
	if (object_label == smack_known_star.smk_known ||
	    strcmp(object_label, smack_known_star.smk_known) == 0)
		return 0;
	/*
	 * An object can be accessed in any way by a subject
	 * with the same label.
	 */
	if (subject_label == object_label ||
	    strcmp(subject_label, object_label) == 0)
		return 0;
	/*
	 * A hat subject can read any object.
	 * A floor object can be read by any subject.
	 */
	if ((request & MAY_ANYREAD) == request) {
		if (object_label == smack_known_floor.smk_known ||
		    strcmp(object_label, smack_known_floor.smk_known) == 0)
			return 0;
		if (subject_label == smack_known_hat.smk_known ||
		    strcmp(subject_label, smack_known_hat.smk_known) == 0)
			return 0;
	}
	/*
	 * Beyond here an explicit relationship is required.
	 * If the requested access is contained in the available
	 * access (e.g. read is included in readwrite) it's
	 * good.
	 */
	for (sp = smack_list; sp != NULL; sp = sp->smk_next) {
		srp = &sp->smk_rule;

		if (srp->smk_subject == subject_label ||
		    strcmp(srp->smk_subject, subject_label) == 0) {
			if (srp->smk_object == object_label ||
			    strcmp(srp->smk_object, object_label) == 0) {
				may = srp->smk_access;
				break;
			}
		}
	}
	/*
	 * This is a bit map operation.
	 */
	if ((request & may) == request)
		return 0;

	return -EACCES;
}

/**
 * smk_curacc - determine if current has a specific access to an object
 * @object_label: a pointer to the object's Smack label
 * @request: the access requested, in "MAY" format
 *
 * This function checks the current subject label/object label pair
 * in the access rule list and returns 0 if the access is permitted,
 * non zero otherwise. It allows that current may have the capability
 * to override the rules.
 */
int smk_curacc(char *obj_label, u32 mode)
{
	int rc;

	rc = smk_access(current->security, obj_label, mode);
	if (rc == 0)
		return 0;

	/*
	 * Return if a specific label has been designated as the
	 * only one that gets privilege and current does not
	 * have that label.
	 */
	if (smack_onlycap != NULL && smack_onlycap != current->security)
		return rc;

	if (capable(CAP_MAC_OVERRIDE))
		return 0;

	return rc;
}

static DEFINE_MUTEX(smack_known_lock);

/**
 * smk_import_entry - import a label, return the list entry
 * @string: a text string that might be a Smack label
 * @len: the maximum size, or zero if it is NULL terminated.
 *
 * Returns a pointer to the entry in the label list that
 * matches the passed string, adding it if necessary.
 */
struct smack_known *smk_import_entry(const char *string, int len)
{
	struct smack_known *skp;
	char smack[SMK_LABELLEN];
	int found;
	int i;

	if (len <= 0 || len > SMK_MAXLEN)
		len = SMK_MAXLEN;

	for (i = 0, found = 0; i < SMK_LABELLEN; i++) {
		if (found)
			smack[i] = '\0';
		else if (i >= len || string[i] > '~' || string[i] <= ' ' ||
			 string[i] == '/') {
			smack[i] = '\0';
			found = 1;
		} else
			smack[i] = string[i];
	}

	if (smack[0] == '\0')
		return NULL;

	mutex_lock(&smack_known_lock);

	for (skp = smack_known; skp != NULL; skp = skp->smk_next)
		if (strncmp(skp->smk_known, smack, SMK_MAXLEN) == 0)
			break;

	if (skp == NULL) {
		skp = kzalloc(sizeof(struct smack_known), GFP_KERNEL);
		if (skp != NULL) {
			skp->smk_next = smack_known;
			strncpy(skp->smk_known, smack, SMK_MAXLEN);
			skp->smk_secid = smack_next_secid++;
			skp->smk_cipso = NULL;
			spin_lock_init(&skp->smk_cipsolock);
			/*
			 * Make sure that the entry is actually
			 * filled before putting it on the list.
			 */
			smp_mb();
			smack_known = skp;
		}
	}

	mutex_unlock(&smack_known_lock);

	return skp;
}

/**
 * smk_import - import a smack label
 * @string: a text string that might be a Smack label
 * @len: the maximum size, or zero if it is NULL terminated.
 *
 * Returns a pointer to the label in the label list that
 * matches the passed string, adding it if necessary.
 */
char *smk_import(const char *string, int len)
{
	struct smack_known *skp;

	skp = smk_import_entry(string, len);
	if (skp == NULL)
		return NULL;
	return skp->smk_known;
}

/**
 * smack_from_secid - find the Smack label associated with a secid
 * @secid: an integer that might be associated with a Smack label
 *
 * Returns a pointer to the appropraite Smack label if there is one,
 * otherwise a pointer to the invalid Smack label.
 */
char *smack_from_secid(const u32 secid)
{
	struct smack_known *skp;

	for (skp = smack_known; skp != NULL; skp = skp->smk_next)
		if (skp->smk_secid == secid)
			return skp->smk_known;

	/*
	 * If we got this far someone asked for the translation
	 * of a secid that is not on the list.
	 */
	return smack_known_invalid.smk_known;
}

/**
 * smack_to_secid - find the secid associated with a Smack label
 * @smack: the Smack label
 *
 * Returns the appropriate secid if there is one,
 * otherwise 0
 */
u32 smack_to_secid(const char *smack)
{
	struct smack_known *skp;

	for (skp = smack_known; skp != NULL; skp = skp->smk_next)
		if (strncmp(skp->smk_known, smack, SMK_MAXLEN) == 0)
			return skp->smk_secid;
	return 0;
}

/**
 * smack_from_cipso - find the Smack label associated with a CIPSO option
 * @level: Bell & LaPadula level from the network
 * @cp: Bell & LaPadula categories from the network
 * @result: where to put the Smack value
 *
 * This is a simple lookup in the label table.
 *
 * This is an odd duck as far as smack handling goes in that
 * it sends back a copy of the smack label rather than a pointer
 * to the master list. This is done because it is possible for
 * a foreign host to send a smack label that is new to this
 * machine and hence not on the list. That would not be an
 * issue except that adding an entry to the master list can't
 * be done at that point.
 */
void smack_from_cipso(u32 level, char *cp, char *result)
{
	struct smack_known *kp;
	char *final = NULL;

	for (kp = smack_known; final == NULL && kp != NULL; kp = kp->smk_next) {
		if (kp->smk_cipso == NULL)
			continue;

		spin_lock_bh(&kp->smk_cipsolock);

		if (kp->smk_cipso->smk_level == level &&
		    memcmp(kp->smk_cipso->smk_catset, cp, SMK_LABELLEN) == 0)
			final = kp->smk_known;

		spin_unlock_bh(&kp->smk_cipsolock);
	}
	if (final == NULL)
		final = smack_known_huh.smk_known;
	strncpy(result, final, SMK_MAXLEN);
	return;
}

/**
 * smack_to_cipso - find the CIPSO option to go with a Smack label
 * @smack: a pointer to the smack label in question
 * @cp: where to put the result
 *
 * Returns zero if a value is available, non-zero otherwise.
 */
int smack_to_cipso(const char *smack, struct smack_cipso *cp)
{
	struct smack_known *kp;

	for (kp = smack_known; kp != NULL; kp = kp->smk_next)
		if (kp->smk_known == smack ||
		    strcmp(kp->smk_known, smack) == 0)
			break;

	if (kp == NULL || kp->smk_cipso == NULL)
		return -ENOENT;

	memcpy(cp, kp->smk_cipso, sizeof(struct smack_cipso));
	return 0;
}
