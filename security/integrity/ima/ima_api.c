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
#include <crypto/hash_info.h>
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
int ima_alloc_init_template(struct integrity_iint_cache *iint,
			    struct file *file, const unsigned char *filename,
			    struct evm_ima_xattr_data *xattr_value,
			    int xattr_len, struct ima_template_entry **entry)
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

		result = field->field_init(iint, file, filename,
					   xattr_value, xattr_len,
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
		       const unsigned char *filename)
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
		       const char *op, const char *cause)
{
	struct ima_template_entry *entry;
	struct inode *inode = file_inode(file);
	int violation = 1;
	int result;

	/* can overflow, only indicator */
	atomic_long_inc(&ima_htable.violations);

	result = ima_alloc_init_template(NULL, file, filename,
					 NULL, 0, &entry);
	if (result < 0) {
		result = -ENOMEM;
		goto err_out;
	}
	result = ima_store_template(entry, violation, inode, filename);
	if (result < 0)
		ima_free_template_entry(entry);
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
 *		subj=, obj=, type=, func=, mask=, fsmagic=
 *	subj,obj, and type: are LSM specific.
 *	func: FILE_CHECK | BPRM_CHECK | MMAP_CHECK | MODULE_CHECK
 *	mask: contains the permission mask
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
			    struct file *file,
			    struct evm_ima_xattr_data **xattr_value,
			    int *xattr_len)
{
	const char *audit_cause = "failed";
	struct inode *inode = file_inode(file);
	const char *filename = file->f_dentry->d_name.name;
	int result = 0;
	struct {
		struct ima_digest_data hdr;
		char digest[IMA_MAX_DIGEST_SIZE];
	} hash;

	if (xattr_value)
		*xattr_len = ima_read_xattr(file->f_dentry, xattr_value);

	if (!(iint->flags & IMA_COLLECTED)) {
		u64 i_version = file_inode(file)->i_version;

		if (file->f_flags & O_DIRECT) {
			audit_cause = "failed(directio)";
			result = -EACCES;
			goto out;
		}

		/* use default hash algorithm */
		hash.hdr.algo = ima_hash_algo;

		if (xattr_value)
			ima_get_hash_algo(*xattr_value, *xattr_len, &hash.hdr);

		result = ima_calc_file_hash(file, &hash.hdr);
		if (!result) {
			int length = sizeof(hash.hdr) + hash.hdr.length;
			void *tmpbuf = krealloc(iint->ima_hash, length,
						GFP_NOFS);
			if (tmpbuf) {
				iint->ima_hash = tmpbuf;
				memcpy(iint->ima_hash, &hash, length);
				iint->version = i_version;
				iint->flags |= IMA_COLLECTED;
			} else
				result = -ENOMEM;
		}
	}
out:
	if (result)
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode,
				    filename, "collect_data", audit_cause,
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
			   int xattr_len)
{
	static const char op[] = "add_template_measure";
	static const char audit_cause[] = "ENOMEM";
	int result = -ENOMEM;
	struct inode *inode = file_inode(file);
	struct ima_template_entry *entry;
	int violation = 0;

	if (iint->flags & IMA_MEASURED)
		return;

	result = ima_alloc_init_template(iint, file, filename,
					 xattr_value, xattr_len, &entry);
	if (result < 0) {
		integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode, filename,
				    op, audit_cause, result, 0);
		return;
	}

	result = ima_store_template(entry, violation, inode, filename);
	if (!result || result == -EEXIST)
		iint->flags |= IMA_MEASURED;
	if (result < 0)
		ima_free_template_entry(entry);
}

void ima_audit_measurement(struct integrity_iint_cache *iint,
			   const unsigned char *filename)
{
	struct audit_buffer *ab;
	char hash[(iint->ima_hash->length * 2) + 1];
	const char *algo_name = hash_algo_name[iint->ima_hash->algo];
	char algo_hash[sizeof(hash) + strlen(algo_name) + 2];
	int i;

	if (iint->flags & IMA_AUDITED)
		return;

	for (i = 0; i < iint->ima_hash->length; i++)
		hex_byte_pack(hash + (i * 2), iint->ima_hash->digest[i]);
	hash[i * 2] = '\0';

	ab = audit_log_start(current->audit_context, GFP_KERNEL,
			     AUDIT_INTEGRITY_RULE);
	if (!ab)
		return;

	audit_log_format(ab, "file=");
	audit_log_untrustedstring(ab, filename);
	audit_log_format(ab, " hash=");
	snprintf(algo_hash, sizeof(algo_hash), "%s:%s", algo_name, hash);
	audit_log_untrustedstring(ab, algo_hash);

	audit_log_task_info(ab, current);
	audit_log_end(ab);

	iint->flags |= IMA_AUDITED;
}

const char *ima_d_path(struct path *path, char **pathbuf)
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
	return pathname ?: (const char *)path->dentry->d_name.name;
}
