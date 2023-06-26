// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008 IBM Corporation
 *
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * File: ima_api.c
 *	Implements must_appraise_or_measure, collect_measurement,
 *	appraise_measurement, store_measurement and store_template.
 */
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/evm.h>
#include <linux/iversion.h>
#include <linux/fsverity.h>

#include "ima.h"

/*
 * ima_free_template_entry - free an existing template entry
 */
void ima_free_template_entry(struct ima_template_entry *entry)
{
	int i;

	for (i = 0; i < entry->template_desc->num_fields; i++)
		kfree(entry->template_data[i].data);

	kfree(entry->digests);
	kfree(entry);
}

/*
 * ima_alloc_init_template - create and initialize a new template entry
 */
int ima_alloc_init_template(struct ima_event_data *event_data,
			    struct ima_template_entry **entry,
			    struct ima_template_desc *desc)
{
	struct ima_template_desc *template_desc;
	struct tpm_digest *digests;
	int i, result = 0;

	if (desc)
		template_desc = desc;
	else
		template_desc = ima_template_desc_current();

	*entry = kzalloc(struct_size(*entry, template_data,
				     template_desc->num_fields), GFP_NOFS);
	if (!*entry)
		return -ENOMEM;

	digests = kcalloc(NR_BANKS(ima_tpm_chip) + ima_extra_slots,
			  sizeof(*digests), GFP_NOFS);
	if (!digests) {
		kfree(*entry);
		*entry = NULL;
		return -ENOMEM;
	}

	(*entry)->digests = digests;
	(*entry)->template_desc = template_desc;
	for (i = 0; i < template_desc->num_fields; i++) {
		const struct ima_template_field *field =
			template_desc->fields[i];
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

	if (!violation) {
		result = ima_calc_field_array_hash(&entry->template_data[0],
						   entry);
		if (result < 0) {
			integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode,
					    template_name, op,
					    audit_cause, result, 0);
			return result;
		}
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
	struct ima_event_data event_data = { .iint = iint,
					     .file = file,
					     .filename = filename,
					     .violation = cause };
	int violation = 1;
	int result;

	/* can overflow, only indicator */
	atomic_long_inc(&ima_htable.violations);

	result = ima_alloc_init_template(&event_data, &entry, NULL);
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
 * @idmap: idmap of the mount the inode was found from
 * @inode: pointer to the inode associated with the object being validated
 * @cred: pointer to credentials structure to validate
 * @secid: secid of the task being validated
 * @mask: contains the permission mask (MAY_READ, MAY_WRITE, MAY_EXEC,
 *        MAY_APPEND)
 * @func: caller identifier
 * @pcr: pointer filled in if matched measure policy sets pcr=
 * @template_desc: pointer filled in if matched measure policy sets template=
 * @func_data: func specific data, may be NULL
 * @allowed_algos: allowlist of hash algorithms for the IMA xattr
 *
 * The policy is defined in terms of keypairs:
 *		subj=, obj=, type=, func=, mask=, fsmagic=
 *	subj,obj, and type: are LSM specific.
 *	func: FILE_CHECK | BPRM_CHECK | CREDS_CHECK | MMAP_CHECK | MODULE_CHECK
 *	| KEXEC_CMDLINE | KEY_CHECK | CRITICAL_DATA | SETXATTR_CHECK
 *	| MMAP_CHECK_REQPROT
 *	mask: contains the permission mask
 *	fsmagic: hex value
 *
 * Returns IMA_MEASURE, IMA_APPRAISE mask.
 *
 */
int ima_get_action(struct mnt_idmap *idmap, struct inode *inode,
		   const struct cred *cred, u32 secid, int mask,
		   enum ima_hooks func, int *pcr,
		   struct ima_template_desc **template_desc,
		   const char *func_data, unsigned int *allowed_algos)
{
	int flags = IMA_MEASURE | IMA_AUDIT | IMA_APPRAISE | IMA_HASH;

	flags &= ima_policy_flag;

	return ima_match_policy(idmap, inode, cred, secid, func, mask,
				flags, pcr, template_desc, func_data,
				allowed_algos);
}

static bool ima_get_verity_digest(struct integrity_iint_cache *iint,
				  struct ima_max_digest_data *hash)
{
	enum hash_algo alg;
	int digest_len;

	/*
	 * On failure, 'measure' policy rules will result in a file data
	 * hash containing 0's.
	 */
	digest_len = fsverity_get_digest(iint->inode, hash->digest, NULL, &alg);
	if (digest_len == 0)
		return false;

	/*
	 * Unlike in the case of actually calculating the file hash, in
	 * the fsverity case regardless of the hash algorithm, return
	 * the verity digest to be included in the measurement list. A
	 * mismatch between the verity algorithm and the xattr signature
	 * algorithm, if one exists, will be detected later.
	 */
	hash->hdr.algo = alg;
	hash->hdr.length = digest_len;
	return true;
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
			    enum hash_algo algo, struct modsig *modsig)
{
	const char *audit_cause = "failed";
	struct inode *inode = file_inode(file);
	const char *filename = file->f_path.dentry->d_name.name;
	struct ima_max_digest_data hash;
	int result = 0;
	int length;
	void *tmpbuf;
	u64 i_version;

	/*
	 * Always collect the modsig, because IMA might have already collected
	 * the file digest without collecting the modsig in a previous
	 * measurement rule.
	 */
	if (modsig)
		ima_collect_modsig(modsig, buf, size);

	if (iint->flags & IMA_COLLECTED)
		goto out;

	/*
	 * Detecting file change is based on i_version. On filesystems
	 * which do not support i_version, support was originally limited
	 * to an initial measurement/appraisal/audit, but was modified to
	 * assume the file changed.
	 */
	i_version = inode_query_iversion(inode);
	hash.hdr.algo = algo;
	hash.hdr.length = hash_digest_size[algo];

	/* Initialize hash digest to 0's in case of failure */
	memset(&hash.digest, 0, sizeof(hash.digest));

	if (iint->flags & IMA_VERITY_REQUIRED) {
		if (!ima_get_verity_digest(iint, &hash)) {
			audit_cause = "no-verity-digest";
			result = -ENODATA;
		}
	} else if (buf) {
		result = ima_calc_buffer_hash(buf, size, &hash.hdr);
	} else {
		result = ima_calc_file_hash(file, &hash.hdr);
	}

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
			   int xattr_len, const struct modsig *modsig, int pcr,
			   struct ima_template_desc *template_desc)
{
	static const char op[] = "add_template_measure";
	static const char audit_cause[] = "ENOMEM";
	int result = -ENOMEM;
	struct inode *inode = file_inode(file);
	struct ima_template_entry *entry;
	struct ima_event_data event_data = { .iint = iint,
					     .file = file,
					     .filename = filename,
					     .xattr_value = xattr_value,
					     .xattr_len = xattr_len,
					     .modsig = modsig };
	int violation = 0;

	/*
	 * We still need to store the measurement in the case of MODSIG because
	 * we only have its contents to put in the list at the time of
	 * appraisal, but a file measurement from earlier might already exist in
	 * the measurement list.
	 */
	if (iint->measured_pcrs & (0x1 << pcr) && !modsig)
		return;

	result = ima_alloc_init_template(&event_data, &entry, template_desc);
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

	audit_log_task_info(ab);
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
		strscpy(namebuf, path->dentry->d_name.name, NAME_MAX);
		pathname = namebuf;
	}

	return pathname;
}
