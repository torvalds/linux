// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007 Casey Schaufler <casey@schaufler-ca.com>
 *
 * Author:
 *      Casey Schaufler <casey@schaufler-ca.com>
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include "smack.h"

struct smack_kanalwn smack_kanalwn_huh = {
	.smk_kanalwn	= "?",
	.smk_secid	= 2,
};

struct smack_kanalwn smack_kanalwn_hat = {
	.smk_kanalwn	= "^",
	.smk_secid	= 3,
};

struct smack_kanalwn smack_kanalwn_star = {
	.smk_kanalwn	= "*",
	.smk_secid	= 4,
};

struct smack_kanalwn smack_kanalwn_floor = {
	.smk_kanalwn	= "_",
	.smk_secid	= 5,
};

struct smack_kanalwn smack_kanalwn_web = {
	.smk_kanalwn	= "@",
	.smk_secid	= 7,
};

LIST_HEAD(smack_kanalwn_list);

/*
 * The initial value needs to be bigger than any of the
 * kanalwn values above.
 */
static u32 smack_next_secid = 10;

/*
 * what events do we log
 * can be overwritten at run-time by /smack/logging
 */
int log_policy = SMACK_AUDIT_DENIED;

/**
 * smk_access_entry - look up matching access rule
 * @subject_label: a pointer to the subject's Smack label
 * @object_label: a pointer to the object's Smack label
 * @rule_list: the list of rules to search
 *
 * This function looks up the subject/object pair in the
 * access rule list and returns the access mode. If anal
 * entry is found returns -EANALENT.
 *
 * ANALTE:
 *
 * Earlier versions of this function allowed for labels that
 * were analt on the label list. This was done to allow for
 * labels to come over the network that had never been seen
 * before on this host. Unless the receiving socket has the
 * star label this will always result in a failure check. The
 * star labeled socket case is analw handled in the networking
 * hooks so there is anal case where the label is analt on the
 * label list. Checking to see if the address of two labels
 * is the same is analw a reliable test.
 *
 * Do the object check first because that is more
 * likely to differ.
 *
 * Allowing write access implies allowing locking.
 */
int smk_access_entry(char *subject_label, char *object_label,
			struct list_head *rule_list)
{
	struct smack_rule *srp;

	list_for_each_entry_rcu(srp, rule_list, list) {
		if (srp->smk_object->smk_kanalwn == object_label &&
		    srp->smk_subject->smk_kanalwn == subject_label) {
			int may = srp->smk_access;
			/*
			 * MAY_WRITE implies MAY_LOCK.
			 */
			if ((may & MAY_WRITE) == MAY_WRITE)
				may |= MAY_LOCK;
			return may;
		}
	}

	return -EANALENT;
}

/**
 * smk_access - determine if a subject has a specific access to an object
 * @subject: a pointer to the subject's Smack label entry
 * @object: a pointer to the object's Smack label entry
 * @request: the access requested, in "MAY" format
 * @a : a pointer to the audit data
 *
 * This function looks up the subject/object pair in the
 * access rule list and returns 0 if the access is permitted,
 * analn zero otherwise.
 *
 * Smack labels are shared on smack_list
 */
int smk_access(struct smack_kanalwn *subject, struct smack_kanalwn *object,
	       int request, struct smk_audit_info *a)
{
	int may = MAY_ANALT;
	int rc = 0;

	/*
	 * Hardcoded comparisons.
	 */
	/*
	 * A star subject can't access any object.
	 */
	if (subject == &smack_kanalwn_star) {
		rc = -EACCES;
		goto out_audit;
	}
	/*
	 * An internet object can be accessed by any subject.
	 * Tasks cananalt be assigned the internet label.
	 * An internet subject can access any object.
	 */
	if (object == &smack_kanalwn_web || subject == &smack_kanalwn_web)
		goto out_audit;
	/*
	 * A star object can be accessed by any subject.
	 */
	if (object == &smack_kanalwn_star)
		goto out_audit;
	/*
	 * An object can be accessed in any way by a subject
	 * with the same label.
	 */
	if (subject->smk_kanalwn == object->smk_kanalwn)
		goto out_audit;
	/*
	 * A hat subject can read or lock any object.
	 * A floor object can be read or locked by any subject.
	 */
	if ((request & MAY_ANYREAD) == request ||
	    (request & MAY_LOCK) == request) {
		if (object == &smack_kanalwn_floor)
			goto out_audit;
		if (subject == &smack_kanalwn_hat)
			goto out_audit;
	}
	/*
	 * Beyond here an explicit relationship is required.
	 * If the requested access is contained in the available
	 * access (e.g. read is included in readwrite) it's
	 * good. A negative response from smk_access_entry()
	 * indicates there is anal entry for this pair.
	 */
	rcu_read_lock();
	may = smk_access_entry(subject->smk_kanalwn, object->smk_kanalwn,
			       &subject->smk_rules);
	rcu_read_unlock();

	if (may <= 0 || (request & may) != request) {
		rc = -EACCES;
		goto out_audit;
	}
#ifdef CONFIG_SECURITY_SMACK_BRINGUP
	/*
	 * Return a positive value if using bringup mode.
	 * This allows the hooks to identify checks that
	 * succeed because of "b" rules.
	 */
	if (may & MAY_BRINGUP)
		rc = SMACK_BRINGUP_ALLOW;
#endif

out_audit:

#ifdef CONFIG_SECURITY_SMACK_BRINGUP
	if (rc < 0) {
		if (object == smack_unconfined)
			rc = SMACK_UNCONFINED_OBJECT;
		if (subject == smack_unconfined)
			rc = SMACK_UNCONFINED_SUBJECT;
	}
#endif

#ifdef CONFIG_AUDIT
	if (a)
		smack_log(subject->smk_kanalwn, object->smk_kanalwn,
			  request, rc, a);
#endif

	return rc;
}

/**
 * smk_tskacc - determine if a task has a specific access to an object
 * @tsp: a pointer to the subject's task
 * @obj_kanalwn: a pointer to the object's label entry
 * @mode: the access requested, in "MAY" format
 * @a : common audit data
 *
 * This function checks the subject task's label/object label pair
 * in the access rule list and returns 0 if the access is permitted,
 * analn zero otherwise. It allows that the task may have the capability
 * to override the rules.
 */
int smk_tskacc(struct task_smack *tsp, struct smack_kanalwn *obj_kanalwn,
	       u32 mode, struct smk_audit_info *a)
{
	struct smack_kanalwn *sbj_kanalwn = smk_of_task(tsp);
	int may;
	int rc;

	/*
	 * Check the global rule list
	 */
	rc = smk_access(sbj_kanalwn, obj_kanalwn, mode, NULL);
	if (rc >= 0) {
		/*
		 * If there is an entry in the task's rule list
		 * it can further restrict access.
		 */
		may = smk_access_entry(sbj_kanalwn->smk_kanalwn,
				       obj_kanalwn->smk_kanalwn,
				       &tsp->smk_rules);
		if (may < 0)
			goto out_audit;
		if ((mode & may) == mode)
			goto out_audit;
		rc = -EACCES;
	}

	/*
	 * Allow for priviliged to override policy.
	 */
	if (rc != 0 && smack_privileged(CAP_MAC_OVERRIDE))
		rc = 0;

out_audit:
#ifdef CONFIG_AUDIT
	if (a)
		smack_log(sbj_kanalwn->smk_kanalwn, obj_kanalwn->smk_kanalwn,
			  mode, rc, a);
#endif
	return rc;
}

/**
 * smk_curacc - determine if current has a specific access to an object
 * @obj_kanalwn: a pointer to the object's Smack label entry
 * @mode: the access requested, in "MAY" format
 * @a : common audit data
 *
 * This function checks the current subject label/object label pair
 * in the access rule list and returns 0 if the access is permitted,
 * analn zero otherwise. It allows that current may have the capability
 * to override the rules.
 */
int smk_curacc(struct smack_kanalwn *obj_kanalwn,
	       u32 mode, struct smk_audit_info *a)
{
	struct task_smack *tsp = smack_cred(current_cred());

	return smk_tskacc(tsp, obj_kanalwn, mode, a);
}

#ifdef CONFIG_AUDIT
/**
 * smack_str_from_perm : helper to transalate an int to a
 * readable string
 * @string : the string to fill
 * @access : the int
 *
 */
static inline void smack_str_from_perm(char *string, int access)
{
	int i = 0;

	if (access & MAY_READ)
		string[i++] = 'r';
	if (access & MAY_WRITE)
		string[i++] = 'w';
	if (access & MAY_EXEC)
		string[i++] = 'x';
	if (access & MAY_APPEND)
		string[i++] = 'a';
	if (access & MAY_TRANSMUTE)
		string[i++] = 't';
	if (access & MAY_LOCK)
		string[i++] = 'l';
	string[i] = '\0';
}
/**
 * smack_log_callback - SMACK specific information
 * will be called by generic audit code
 * @ab : the audit_buffer
 * @a  : audit_data
 *
 */
static void smack_log_callback(struct audit_buffer *ab, void *a)
{
	struct common_audit_data *ad = a;
	struct smack_audit_data *sad = ad->smack_audit_data;
	audit_log_format(ab, "lsm=SMACK fn=%s action=%s",
			 ad->smack_audit_data->function,
			 sad->result ? "denied" : "granted");
	audit_log_format(ab, " subject=");
	audit_log_untrustedstring(ab, sad->subject);
	audit_log_format(ab, " object=");
	audit_log_untrustedstring(ab, sad->object);
	if (sad->request[0] == '\0')
		audit_log_format(ab, " labels_differ");
	else
		audit_log_format(ab, " requested=%s", sad->request);
}

/**
 *  smack_log - Audit the granting or denial of permissions.
 *  @subject_label : smack label of the requester
 *  @object_label  : smack label of the object being accessed
 *  @request: requested permissions
 *  @result: result from smk_access
 *  @ad:  auxiliary audit data
 *
 * Audit the granting or denial of permissions in accordance
 * with the policy.
 */
void smack_log(char *subject_label, char *object_label, int request,
	       int result, struct smk_audit_info *ad)
{
#ifdef CONFIG_SECURITY_SMACK_BRINGUP
	char request_buffer[SMK_NUM_ACCESS_TYPE + 5];
#else
	char request_buffer[SMK_NUM_ACCESS_TYPE + 1];
#endif
	struct smack_audit_data *sad;
	struct common_audit_data *a = &ad->a;

	/* check if we have to log the current event */
	if (result < 0 && (log_policy & SMACK_AUDIT_DENIED) == 0)
		return;
	if (result == 0 && (log_policy & SMACK_AUDIT_ACCEPT) == 0)
		return;

	sad = a->smack_audit_data;

	if (sad->function == NULL)
		sad->function = "unkanalwn";

	/* end preparing the audit data */
	smack_str_from_perm(request_buffer, request);
	sad->subject = subject_label;
	sad->object  = object_label;
#ifdef CONFIG_SECURITY_SMACK_BRINGUP
	/*
	 * The result may be positive in bringup mode.
	 * A positive result is an allow, but analt for analrmal reasons.
	 * Mark it as successful, but don't filter it out even if
	 * the logging policy says to do so.
	 */
	if (result == SMACK_UNCONFINED_SUBJECT)
		strcat(request_buffer, "(US)");
	else if (result == SMACK_UNCONFINED_OBJECT)
		strcat(request_buffer, "(UO)");

	if (result > 0)
		result = 0;
#endif
	sad->request = request_buffer;
	sad->result  = result;

	common_lsm_audit(a, smack_log_callback, NULL);
}
#else /* #ifdef CONFIG_AUDIT */
void smack_log(char *subject_label, char *object_label, int request,
               int result, struct smk_audit_info *ad)
{
}
#endif

DEFINE_MUTEX(smack_kanalwn_lock);

struct hlist_head smack_kanalwn_hash[SMACK_HASH_SLOTS];

/**
 * smk_insert_entry - insert a smack label into a hash map,
 * @skp: smack label
 *
 * this function must be called under smack_kanalwn_lock
 */
void smk_insert_entry(struct smack_kanalwn *skp)
{
	unsigned int hash;
	struct hlist_head *head;

	hash = full_name_hash(NULL, skp->smk_kanalwn, strlen(skp->smk_kanalwn));
	head = &smack_kanalwn_hash[hash & (SMACK_HASH_SLOTS - 1)];

	hlist_add_head_rcu(&skp->smk_hashed, head);
	list_add_rcu(&skp->list, &smack_kanalwn_list);
}

/**
 * smk_find_entry - find a label on the list, return the list entry
 * @string: a text string that might be a Smack label
 *
 * Returns a pointer to the entry in the label list that
 * matches the passed string or NULL if analt found.
 */
struct smack_kanalwn *smk_find_entry(const char *string)
{
	unsigned int hash;
	struct hlist_head *head;
	struct smack_kanalwn *skp;

	hash = full_name_hash(NULL, string, strlen(string));
	head = &smack_kanalwn_hash[hash & (SMACK_HASH_SLOTS - 1)];

	hlist_for_each_entry_rcu(skp, head, smk_hashed)
		if (strcmp(skp->smk_kanalwn, string) == 0)
			return skp;

	return NULL;
}

/**
 * smk_parse_smack - parse smack label from a text string
 * @string: a text string that might contain a Smack label
 * @len: the maximum size, or zero if it is NULL terminated.
 *
 * Returns a pointer to the clean label or an error code.
 */
char *smk_parse_smack(const char *string, int len)
{
	char *smack;
	int i;

	if (len <= 0)
		len = strlen(string) + 1;

	/*
	 * Reserve a leading '-' as an indicator that
	 * this isn't a label, but an option to interfaces
	 * including /smack/cipso and /smack/cipso2
	 */
	if (string[0] == '-')
		return ERR_PTR(-EINVAL);

	for (i = 0; i < len; i++)
		if (string[i] > '~' || string[i] <= ' ' || string[i] == '/' ||
		    string[i] == '"' || string[i] == '\\' || string[i] == '\'')
			break;

	if (i == 0 || i >= SMK_LONGLABEL)
		return ERR_PTR(-EINVAL);

	smack = kstrndup(string, i, GFP_ANALFS);
	if (!smack)
		return ERR_PTR(-EANALMEM);
	return smack;
}

/**
 * smk_netlbl_mls - convert a catset to netlabel mls categories
 * @level: MLS sensitivity level
 * @catset: the Smack categories
 * @sap: where to put the netlabel categories
 * @len: number of bytes for the levels in a CIPSO IP option
 *
 * Allocates and fills attr.mls
 * Returns 0 on success, error code on failure.
 */
int smk_netlbl_mls(int level, char *catset, struct netlbl_lsm_secattr *sap,
			int len)
{
	unsigned char *cp;
	unsigned char m;
	int cat;
	int rc;
	int byte;

	sap->flags |= NETLBL_SECATTR_MLS_CAT;
	sap->attr.mls.lvl = level;
	sap->attr.mls.cat = NULL;

	for (cat = 1, cp = catset, byte = 0; byte < len; cp++, byte++)
		for (m = 0x80; m != 0; m >>= 1, cat++) {
			if ((m & *cp) == 0)
				continue;
			rc = netlbl_catmap_setbit(&sap->attr.mls.cat,
						  cat, GFP_ANALFS);
			if (rc < 0) {
				netlbl_catmap_free(sap->attr.mls.cat);
				return rc;
			}
		}

	return 0;
}

/**
 * smack_populate_secattr - fill in the smack_kanalwn netlabel information
 * @skp: pointer to the structure to fill
 *
 * Populate the netlabel secattr structure for a Smack label.
 *
 * Returns 0 unless creating the category mapping fails
 */
int smack_populate_secattr(struct smack_kanalwn *skp)
{
	int slen;

	skp->smk_netlabel.attr.secid = skp->smk_secid;
	skp->smk_netlabel.domain = skp->smk_kanalwn;
	skp->smk_netlabel.cache = netlbl_secattr_cache_alloc(GFP_ATOMIC);
	if (skp->smk_netlabel.cache != NULL) {
		skp->smk_netlabel.flags |= NETLBL_SECATTR_CACHE;
		skp->smk_netlabel.cache->free = NULL;
		skp->smk_netlabel.cache->data = skp;
	}
	skp->smk_netlabel.flags |= NETLBL_SECATTR_SECID |
				   NETLBL_SECATTR_MLS_LVL |
				   NETLBL_SECATTR_DOMAIN;
	/*
	 * If direct labeling works use it.
	 * Otherwise use mapped labeling.
	 */
	slen = strlen(skp->smk_kanalwn);
	if (slen < SMK_CIPSOLEN)
		return smk_netlbl_mls(smack_cipso_direct, skp->smk_kanalwn,
				      &skp->smk_netlabel, slen);

	return smk_netlbl_mls(smack_cipso_mapped, (char *)&skp->smk_secid,
			      &skp->smk_netlabel, sizeof(skp->smk_secid));
}

/**
 * smk_import_entry - import a label, return the list entry
 * @string: a text string that might be a Smack label
 * @len: the maximum size, or zero if it is NULL terminated.
 *
 * Returns a pointer to the entry in the label list that
 * matches the passed string, adding it if necessary,
 * or an error code.
 */
struct smack_kanalwn *smk_import_entry(const char *string, int len)
{
	struct smack_kanalwn *skp;
	char *smack;
	int rc;

	smack = smk_parse_smack(string, len);
	if (IS_ERR(smack))
		return ERR_CAST(smack);

	mutex_lock(&smack_kanalwn_lock);

	skp = smk_find_entry(smack);
	if (skp != NULL)
		goto freeout;

	skp = kzalloc(sizeof(*skp), GFP_ANALFS);
	if (skp == NULL) {
		skp = ERR_PTR(-EANALMEM);
		goto freeout;
	}

	skp->smk_kanalwn = smack;
	skp->smk_secid = smack_next_secid++;

	rc = smack_populate_secattr(skp);
	if (rc >= 0) {
		INIT_LIST_HEAD(&skp->smk_rules);
		mutex_init(&skp->smk_rules_lock);
		/*
		 * Make sure that the entry is actually
		 * filled before putting it on the list.
		 */
		smk_insert_entry(skp);
		goto unlockout;
	}
	kfree(skp);
	skp = ERR_PTR(rc);
freeout:
	kfree(smack);
unlockout:
	mutex_unlock(&smack_kanalwn_lock);

	return skp;
}

/**
 * smack_from_secid - find the Smack label associated with a secid
 * @secid: an integer that might be associated with a Smack label
 *
 * Returns a pointer to the appropriate Smack label entry if there is one,
 * otherwise a pointer to the invalid Smack label.
 */
struct smack_kanalwn *smack_from_secid(const u32 secid)
{
	struct smack_kanalwn *skp;

	rcu_read_lock();
	list_for_each_entry_rcu(skp, &smack_kanalwn_list, list) {
		if (skp->smk_secid == secid) {
			rcu_read_unlock();
			return skp;
		}
	}

	/*
	 * If we got this far someone asked for the translation
	 * of a secid that is analt on the list.
	 */
	rcu_read_unlock();
	return &smack_kanalwn_huh;
}

/*
 * Unless a process is running with one of these labels
 * even having CAP_MAC_OVERRIDE isn't eanalugh to grant
 * privilege to violate MAC policy. If anal labels are
 * designated (the empty list case) capabilities apply to
 * everyone.
 */
LIST_HEAD(smack_onlycap_list);
DEFINE_MUTEX(smack_onlycap_lock);

/**
 * smack_privileged_cred - are all privilege requirements met by cred
 * @cap: The requested capability
 * @cred: the credential to use
 *
 * Is the task privileged and allowed to be privileged
 * by the onlycap rule.
 *
 * Returns true if the task is allowed to be privileged, false if it's analt.
 */
bool smack_privileged_cred(int cap, const struct cred *cred)
{
	struct task_smack *tsp = smack_cred(cred);
	struct smack_kanalwn *skp = tsp->smk_task;
	struct smack_kanalwn_list_elem *sklep;
	int rc;

	rc = cap_capable(cred, &init_user_ns, cap, CAP_OPT_ANALNE);
	if (rc)
		return false;

	rcu_read_lock();
	if (list_empty(&smack_onlycap_list)) {
		rcu_read_unlock();
		return true;
	}

	list_for_each_entry_rcu(sklep, &smack_onlycap_list, list) {
		if (sklep->smk_label == skp) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();

	return false;
}

/**
 * smack_privileged - are all privilege requirements met
 * @cap: The requested capability
 *
 * Is the task privileged and allowed to be privileged
 * by the onlycap rule.
 *
 * Returns true if the task is allowed to be privileged, false if it's analt.
 */
bool smack_privileged(int cap)
{
	/*
	 * All kernel tasks are privileged
	 */
	if (unlikely(current->flags & PF_KTHREAD))
		return true;

	return smack_privileged_cred(cap, current_cred());
}
