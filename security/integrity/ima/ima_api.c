/*
 * Copyright (C) 2008 IBM Corporation
 *
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_api.c
 *	Implements must_appraise_or_measure, collect_measurement,
 *	appraise_measurement, store_measurement and store_template.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/evm.h>
#include "ima.h"

static const char *IMA_TEMPLATE_NAME = "ima";

/*
 * ima_store_template - store ima template measurements
 *
 * Calculate the hash of a template entry, add the template entry
 * to an ordered list of measurement entries maintained inside the kernel,
 * and also update the aggregate integrity value (maintained inside the
 * configured TPM PCR) over the hashes of the current list of measurement
 * entries.
 *
 * Applications retrieve the current kernel-held measurement list through
 * the securityfs entries in /sys/kernel/security/ima. The signed aggregate
 * TPM PCR (called quote) can be retrieved using a TPM user space library
 * and is used to validate the measurement list.
 *
 * Returns 0 on success, error code otherwise
 */
int ima_store_template(struct ima_template_entry *entry,
		       int violation, struct inode *inode)
{
	const char *op = "add_template_measure";
	const char *audit_cause = "hashing_error";
	int result;
	struct ima_digest_data hash;

	memset(entry->digest, 0, sizeof(entry->digest));
	entry->template_name = IMA_TEMPLATE_NAME;
	entry->template_len = sizeof(entry->template);

	if (!violation) {
		result = ima_calc_buffer_hash(&entry->template,
					      entry->template_len, &hash);
		if (result < 0) {
			integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode,
					    entry->template_name, op,
					    audit_cause, result, 0);
			return result;
		}
		memcpy(entry->digest, hash.digest, hash.length);
	}
	result = ima_add_template_entry(entry, violation, op, inode);
	return result;
}

/*
 * ima_add_violation - add violation to measurement list.
 *
 * Violations are flagged in the measurement list with zero hash values.
 * By extending the PCR with 0xFF's instead of with zeroes, the PCR
 * value is invalidated.
 */
void ima_add_violation(struct inode *inode, const unsigned char *filename,
		       const char *op, const char *cause)
{
	struct ima_template_entry *entry;
	int violation = 1;
	int result;

	/* can overflow, only indicator */
	atomic_long_inc(&ima_htable.violations);

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		result = -ENOMEM;
		goto err_out;
	}
	memset(&entry->template, 0, sizeof(entry->template));
	strncpy(entry->template.file_name, filename, IMA_EVENT_NAME_LEN_MAX);
	result = ima_store_template(entry, violation, inode);
	if (result < 0)
		kfree(entry);
err_out:
	integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode, filename,
			    op, cause, result, 0);
}

/**
 * ima_get_action - appraise & measure decision based on policy.
 * @inode: pointer to inode to measure
 * @mask: contains the permission mask (MAY_READ, MAY_WRITE, MAY_EXECUTE)
 * @function: calling function (FILE_CHECK, BPRM_CHECK, MMAP_CHECK, MODULE_CHECK)
 *
 * The policy is defined in terms of keypairs:
 * 		subj=, obj=, type=, func=, mask=, fsmagic=
 *	subj,obj, and type: are LSM specific.
 * 	func: FILE_CHECK | BPRM_CHECK | MMAP_CHECK | MODULE_CHECK
 * 	mask: contains the permission mask
 *	fsmagic: hex value
 *
 * Returns IMA_MEASURE, IMA_APPRAISE mask.
 *
 */
int ima_get_action(struct inode *inode, int mask, int function)
{
	int flags = IMA_MEASURE | IMA_AUDIT | IMA_APPRAISE;

	if (!ima_appraise)
		flags &= ~IMA_APPRAISE;

	return ima_match_policy(inode, function, mask, flags);
}

int ima_must_measure(struct inode *inode, int mask, int function)
{
	return ima_match_policy(inode, function, mask, IMA_MEASURE);
}

/*
 * ima_collect_measurement - collect file measurement
 *
 * Calculate the file hash, if it doesn't already exist,
 * storing the measurement and i_version in the iint.
 *
 * Must be called with iint->mutex held.
 *
 * Return 0 on success, error code otherwise
 */
int ima_collect_measurement(struct integrity_iint_cache *iint,
			    struct file *file)
{
	struct inode *inode = file_inode(file);
	const char *filename = file->f_dentry->d_name.name;
	int result = 0;

	if (!(iint->flags & IMA_COLLECTED)) {
		u64 i_version = file_inode(file)->i_version;

		/* use default hash algorithm */
		iint->ima_hash.algo = ima_hash_algo;
		result = ima_calc_file_hash(file, &iint->ima_hash);
		if (!result) {
			iint->version = i_version;
			iint->flags |= IMA_COLLECTED;
		}
	}
	if (result)
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode,
				    filename, "collect_data", "failed",
				    result, 0);
	return result;
}

/*
 * ima_store_measurement - store file measurement
 *
 * Create an "ima" template and then store the template by calling
 * ima_store_template.
 *
 * We only get here if the inode has not already been measured,
 * but the measurement could already exist:
 * 	- multiple copies of the same file on either the same or
 *	  different filesystems.
 *	- the inode was previously flushed as well as the iint info,
 *	  containing the hashing info.
 *
 * Must be called with iint->mutex held.
 */
void ima_store_measurement(struct integrity_iint_cache *iint,
			   struct file *file, const unsigned char *filename)
{
	const char *op = "add_template_measure";
	const char *audit_cause = "ENOMEM";
	int result = -ENOMEM;
	struct inode *inode = file_inode(file);
	struct ima_template_entry *entry;
	int violation = 0;

	if (iint->flags & IMA_MEASURED)
		return;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode, filename,
				    op, audit_cause, result, 0);
		return;
	}
	memset(&entry->template, 0, sizeof(entry->template));
	if (iint->ima_hash.algo != ima_hash_algo) {
		struct ima_digest_data hash;

		hash.algo = ima_hash_algo;
		result = ima_calc_file_hash(file, &hash);
		if (result)
			integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode,
					    filename, "collect_data", "failed",
					    result, 0);
		else
			memcpy(entry->template.digest, hash.digest,
			       hash.length);
	} else
		memcpy(entry->template.digest, iint->ima_hash.digest,
		       iint->ima_hash.length);
	strcpy(entry->template.file_name,
	       (strlen(filename) > IMA_EVENT_NAME_LEN_MAX) ?
	       file->f_dentry->d_name.name : filename);

	result = ima_store_template(entry, violation, inode);
	if (!result || result == -EEXIST)
		iint->flags |= IMA_MEASURED;
	if (result < 0)
		kfree(entry);
}

void ima_audit_measurement(struct integrity_iint_cache *iint,
			   const unsigned char *filename)
{
	struct audit_buffer *ab;
	char hash[(iint->ima_hash.length * 2) + 1];
	int i;

	if (iint->flags & IMA_AUDITED)
		return;

	for (i = 0; i < iint->ima_hash.length; i++)
		hex_byte_pack(hash + (i * 2), iint->ima_hash.digest[i]);
	hash[i * 2] = '\0';

	ab = audit_log_start(current->audit_context, GFP_KERNEL,
			     AUDIT_INTEGRITY_RULE);
	if (!ab)
		return;

	audit_log_format(ab, "file=");
	audit_log_untrustedstring(ab, filename);
	audit_log_format(ab, " hash=");
	audit_log_untrustedstring(ab, hash);

	audit_log_task_info(ab, current);
	audit_log_end(ab);

	iint->flags |= IMA_AUDITED;
}

const char *ima_d_path(struct path *path, char **pathbuf)
{
	char *pathname = NULL;

	/* We will allow 11 spaces for ' (deleted)' to be appended */
	*pathbuf = kmalloc(PATH_MAX + 11, GFP_KERNEL);
	if (*pathbuf) {
		pathname = d_path(path, *pathbuf, PATH_MAX + 11);
		if (IS_ERR(pathname)) {
			kfree(*pathbuf);
			*pathbuf = NULL;
			pathname = NULL;
		}
	}
	return pathname;
}
