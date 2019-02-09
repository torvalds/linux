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
#include <linux/iversion.h>

#include "ima.h"

/*
 * ima_free_template_entry - free an existing template entry
 */
void ima_free_template_entry(struct ima_template_entry *entry)
{
	int i;

	for (i = 0; i < entry->template_desc->num_fields; i++)
		kfree(entry->template_data[i].data);

	kfree(entry);
}

/*
 * ima_alloc_init_template - create and initialize a new template entry
 */
int ima_alloc_init_template(struct ima_event_data *event_data,
			    struct ima_template_entry **entry)
{
	struct ima_template_desc *template_desc = ima_template_desc_current();
	int i, result = 0;

	*entry = kzalloc(sizeof(**entry) + template_desc->num_fields *
			 sizeof(struct ima_field_data), GFP_NOFS);
	if (!*entry)
		return -ENOMEM;

	(*entry)->template_desc = template_desc;
	for (i = 0; i < template_desc->num_fields; i++) {
		struct ima_template_field *field = template_desc->fields[i];
		u32 len;

		result = field->field_init(event_data,
					   &((*entry)->template_data[i]));
		if (result != 0)
			goto out;

		len = (*entry)->template_data[i].len;
		(*entry)->template_data_len += sizeof(len);
		(*entry)->template_data_len += len;
	}
	return 0;
out:
	ima_free_template_entry(*entry);
	*entry = NULL;
	return result;
}

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
		       int violation, struct inode *inode,
		       const unsigned char *filename, int pcr)
{
	static const char op[] = "add_template_measure";
	static const char audit_cause[] = "hashing_error";
	char *template_name = entry->template_desc->name;
	int result;
	struct {
		struct ima_digest_data hdr;
		char digest[TPM_DIGEST_SIZE];
	} hash;

	if (!violation) {
		int num_fields = entry->template_desc->num_fields;

		/* this function uses default algo */
		hash.hdr.algo = HASH_ALGO_SHA1;
		result = ima_calc_field_array_hash(&entry->template_data[0],
						   entry->template_desc,
						   num_fields, &hash.hdr);
		if (result < 0) {
			integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode,
					    template_name, op,
					    audit_cause, result, 0);
			return result;
		}
		memcpy(entry->digest, hash.hdr.digest, hash.hdr.length);
	}
	entry->pcr = pcr;
	result = ima_add_template_entry(entry, violation, op, inode, filename);
	return result;
}

/*
 * ima_add_violation - add violation to measurement list.
 *
 * Violations are flagged in the measurement list with zero hash values.
 * By extending the PCR with 0xFF's instead of with zeroes, the PCR
 * value is invalidated.
 */
void ima_add_violation(struct file *file, const unsigned char *filename,
		       struct integrity_iint_cache *iint,
		       const char *op, const char *cause)
{
	struct ima_template_entry *entry;
	struct inode *inode = file_inode(file);
	struct ima_event_data event_data = {iint, file, filename, NULL, 0,
					    cause};
	int violation = 1;
	int result;

	/* can overflow, only indicator */
	atomic_long_inc(&ima_htable.violations);

	result = ima_alloc_init_template(&event_data, &entry);
	if (result < 0) {
		result = -ENOMEM;
		goto err_out;
	}
	result = ima_store_template(entry, violation, inode,
				    filename, CONFIG_IMA_MEASURE_PCR_IDX);
	if (result < 0)
		ima_free_template_entry(entry);
err_out:
	integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode, filename,
			    op, cause, result, 0);
}

/**
 * ima_get_action - appraise & measure decision based on policy.
 * @inode: pointer to inode to measure
 * @cred: pointer to credentials structure to validate
 * @secid: secid of the task being validated
 * @mask: contains the permission mask (MAY_READ, MAY_WRITE, MAY_EXEC,
 *        MAY_APPEND)
 * @func: caller identifier
 * @pcr: pointer filled in if matched measure policy sets pcr=
 *
 * The policy is defined in terms of keypairs:
 *		subj=, obj=, type=, func=, mask=, fsmagic=
 *	subj,obj, and type: are LSM specific.
 *	func: FILE_CHECK | BPRM_CHECK | CREDS_CHECK | MMAP_CHECK | MODULE_CHECK
 *	mask: contains the permission mask
 *	fsmagic: hex value
 *
 * Returns IMA_MEASURE, IMA_APPRAISE mask.
 *
 */
int ima_get_action(struct inode *inode, const struct cred *cred, u32 secid,
		   int mask, enum ima_hooks func, int *pcr)
{
	int flags = IMA_MEASURE | IMA_AUDIT | IMA_APPRAISE | IMA_HASH;

	flags &= ima_policy_flag;

	return ima_match_policy(inode, cred, secid, func, mask, flags, pcr);
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
			    struct file *file, void *buf, loff_t size,
			    enum hash_algo algo)
{
	const char *audit_cause = "failed";
	struct inode *inode = file_inode(file);
	const char *filename = file->f_path.dentry->d_name.name;
	int result = 0;
	int length;
	void *tmpbuf;
	u64 i_version;
	struct {
		struct ima_digest_data hdr;
		char digest[IMA_MAX_DIGEST_SIZE];
	} hash;

	if (iint->flags & IMA_COLLECTED)
		goto out;

	/*
	 * Dectecting file change is based on i_version. On filesystems
	 * which do not support i_version, support is limited to an initial
	 * measurement/appraisal/audit.
	 */
	i_version = inode_query_iversion(inode);
	hash.hdr.algo = algo;

	/* Initialize hash digest to 0's in case of failure */
	memset(&hash.digest, 0, sizeof(hash.digest));

	if (buf)
		result = ima_calc_buffer_hash(buf, size, &hash.hdr);
	else
		result = ima_calc_file_hash(file, &hash.hdr);

	if (result && result != -EBADF && result != -EINVAL)
		goto out;

	length = sizeof(hash.hdr) + hash.hdr.length;
	tmpbuf = krealloc(iint->ima_hash, length, GFP_NOFS);
	if (!tmpbuf) {
		result = -ENOMEM;
		goto out;
	}

	iint->ima_hash = tmpbuf;
	memcpy(iint->ima_hash, &hash, length);
	iint->version = i_version;

	/* Possibly temporary failure due to type of read (eg. O_DIRECT) */
	if (!result)
		iint->flags |= IMA_COLLECTED;
out:
	if (result) {
		if (file->f_flags & O_DIRECT)
			audit_cause = "failed(directio)";

		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode,
				    filename, "collect_data", audit_cause,
				    result, 0);
	}
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
 *	- multiple copies of the same file on either the same or
 *	  different filesystems.
 *	- the inode was previously flushed as well as the iint info,
 *	  containing the hashing info.
 *
 * Must be called with iint->mutex held.
 */
void ima_store_measurement(struct integrity_iint_cache *iint,
			   struct file *file, const unsigned char *filename,
			   struct evm_ima_xattr_data *xattr_value,
			   int xattr_len, int pcr)
{
	static const char op[] = "add_template_measure";
	static const char audit_cause[] = "ENOMEM";
	int result = -ENOMEM;
	struct inode *inode = file_inode(file);
	struct ima_template_entry *entry;
	struct ima_event_data event_data = {iint, file, filename, xattr_value,
					    xattr_len, NULL};
	int violation = 0;

	if (iint->measured_pcrs & (0x1 << pcr))
		return;

	result = ima_alloc_init_template(&event_data, &entry);
	if (result < 0) {
		integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode, filename,
				    op, audit_cause, result, 0);
		return;
	}

	result = ima_store_template(entry, violation, inode, filename, pcr);
	if ((!result || result == -EEXIST) && !(file->f_flags & O_DIRECT)) {
		iint->flags |= IMA_MEASURED;
		iint->measured_pcrs |= (0x1 << pcr);
	}
	if (result < 0)
		ima_free_template_entry(entry);
}

void ima_audit_measurement(struct integrity_iint_cache *iint,
			   const unsigned char *filename)
{
	struct audit_buffer *ab;
	char *hash;
	const char *algo_name = hash_algo_name[iint->ima_hash->algo];
	int i;

	if (iint->flags & IMA_AUDITED)
		return;

	hash = kzalloc((iint->ima_hash->length * 2) + 1, GFP_KERNEL);
	if (!hash)
		return;

	for (i = 0; i < iint->ima_hash->length; i++)
		hex_byte_pack(hash + (i * 2), iint->ima_hash->digest[i]);
	hash[i * 2] = '\0';

	ab = audit_log_start(audit_context(), GFP_KERNEL,
			     AUDIT_INTEGRITY_RULE);
	if (!ab)
		goto out;

	audit_log_format(ab, "file=");
	audit_log_untrustedstring(ab, filename);
	audit_log_format(ab, " hash=\"%s:%s\"", algo_name, hash);

	audit_log_task_info(ab, current);
	audit_log_end(ab);

	iint->flags |= IMA_AUDITED;
out:
	kfree(hash);
	return;
}

/*
 * ima_d_path - return a pointer to the full pathname
 *
 * Attempt to return a pointer to the full pathname for use in the
 * IMA measurement list, IMA audit records, and auditing logs.
 *
 * On failure, return a pointer to a copy of the filename, not dname.
 * Returning a pointer to dname, could result in using the pointer
 * after the memory has been freed.
 */
const char *ima_d_path(const struct path *path, char **pathbuf, char *namebuf)
{
	char *pathname = NULL;

	*pathbuf = __getname();
	if (*pathbuf) {
		pathname = d_absolute_path(path, *pathbuf, PATH_MAX);
		if (IS_ERR(pathname)) {
			__putname(*pathbuf);
			*pathbuf = NULL;
			pathname = NULL;
		}
	}

	if (!pathname) {
		strlcpy(namebuf, path->dentry->d_name.name, NAME_MAX);
		pathname = namebuf;
	}

	return pathname;
}
