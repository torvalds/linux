// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor functions for unpacking policy loaded from
 * userspace.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * AppArmor uses a serialized binary format for loading policy. To find
 * policy format documentation see Documentation/admin-guide/LSM/apparmor.rst
 * All policy is validated before it is used.
 */

#include <asm/unaligned.h>
#include <kunit/visibility.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/zstd.h>

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/cred.h"
#include "include/crypto.h"
#include "include/file.h"
#include "include/match.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/policy_unpack.h"
#include "include/policy_compat.h"

/* audit callback for unpack fields */
static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;

	if (aad(sa)->iface.ns) {
		audit_log_format(ab, " ns=");
		audit_log_untrustedstring(ab, aad(sa)->iface.ns);
	}
	if (aad(sa)->name) {
		audit_log_format(ab, " name=");
		audit_log_untrustedstring(ab, aad(sa)->name);
	}
	if (aad(sa)->iface.pos)
		audit_log_format(ab, " offset=%ld", aad(sa)->iface.pos);
}

/**
 * audit_iface - do audit message for policy unpacking/load/replace/remove
 * @new: profile if it has been allocated (MAYBE NULL)
 * @ns_name: name of the ns the profile is to be loaded to (MAY BE NULL)
 * @name: name of the profile being manipulated (MAYBE NULL)
 * @info: any extra info about the failure (MAYBE NULL)
 * @e: buffer position info
 * @error: error code
 *
 * Returns: %0 or error
 */
static int audit_iface(struct aa_profile *new, const char *ns_name,
		       const char *name, const char *info, struct aa_ext *e,
		       int error)
{
	struct aa_profile *profile = labels_profile(aa_current_raw_label());
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, AA_CLASS_NONE, NULL);
	if (e)
		aad(&sa)->iface.pos = e->pos - e->start;
	aad(&sa)->iface.ns = ns_name;
	if (new)
		aad(&sa)->name = new->base.hname;
	else
		aad(&sa)->name = name;
	aad(&sa)->info = info;
	aad(&sa)->error = error;

	return aa_audit(AUDIT_APPARMOR_STATUS, profile, &sa, audit_cb);
}

void __aa_loaddata_update(struct aa_loaddata *data, long revision)
{
	AA_BUG(!data);
	AA_BUG(!data->ns);
	AA_BUG(!mutex_is_locked(&data->ns->lock));
	AA_BUG(data->revision > revision);

	data->revision = revision;
	if ((data->dents[AAFS_LOADDATA_REVISION])) {
		d_inode(data->dents[AAFS_LOADDATA_DIR])->i_mtime =
			current_time(d_inode(data->dents[AAFS_LOADDATA_DIR]));
		d_inode(data->dents[AAFS_LOADDATA_REVISION])->i_mtime =
			current_time(d_inode(data->dents[AAFS_LOADDATA_REVISION]));
	}
}

bool aa_rawdata_eq(struct aa_loaddata *l, struct aa_loaddata *r)
{
	if (l->size != r->size)
		return false;
	if (l->compressed_size != r->compressed_size)
		return false;
	if (aa_g_hash_policy && memcmp(l->hash, r->hash, aa_hash_size()) != 0)
		return false;
	return memcmp(l->data, r->data, r->compressed_size ?: r->size) == 0;
}

/*
 * need to take the ns mutex lock which is NOT safe most places that
 * put_loaddata is called, so we have to delay freeing it
 */
static void do_loaddata_free(struct work_struct *work)
{
	struct aa_loaddata *d = container_of(work, struct aa_loaddata, work);
	struct aa_ns *ns = aa_get_ns(d->ns);

	if (ns) {
		mutex_lock_nested(&ns->lock, ns->level);
		__aa_fs_remove_rawdata(d);
		mutex_unlock(&ns->lock);
		aa_put_ns(ns);
	}

	kfree_sensitive(d->hash);
	kfree_sensitive(d->name);
	kvfree(d->data);
	kfree_sensitive(d);
}

void aa_loaddata_kref(struct kref *kref)
{
	struct aa_loaddata *d = container_of(kref, struct aa_loaddata, count);

	if (d) {
		INIT_WORK(&d->work, do_loaddata_free);
		schedule_work(&d->work);
	}
}

struct aa_loaddata *aa_loaddata_alloc(size_t size)
{
	struct aa_loaddata *d;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (d == NULL)
		return ERR_PTR(-ENOMEM);
	d->data = kvzalloc(size, GFP_KERNEL);
	if (!d->data) {
		kfree(d);
		return ERR_PTR(-ENOMEM);
	}
	kref_init(&d->count);
	INIT_LIST_HEAD(&d->list);

	return d;
}

/* test if read will be in packed data bounds */
VISIBLE_IF_KUNIT bool aa_inbounds(struct aa_ext *e, size_t size)
{
	return (size <= e->end - e->pos);
}
EXPORT_SYMBOL_IF_KUNIT(aa_inbounds);

/**
 * aa_unpack_u16_chunk - test and do bounds checking for a u16 size based chunk
 * @e: serialized data read head (NOT NULL)
 * @chunk: start address for chunk of data (NOT NULL)
 *
 * Returns: the size of chunk found with the read head at the end of the chunk.
 */
VISIBLE_IF_KUNIT size_t aa_unpack_u16_chunk(struct aa_ext *e, char **chunk)
{
	size_t size = 0;
	void *pos = e->pos;

	if (!aa_inbounds(e, sizeof(u16)))
		goto fail;
	size = le16_to_cpu(get_unaligned((__le16 *) e->pos));
	e->pos += sizeof(__le16);
	if (!aa_inbounds(e, size))
		goto fail;
	*chunk = e->pos;
	e->pos += size;
	return size;

fail:
	e->pos = pos;
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_u16_chunk);

/* unpack control byte */
VISIBLE_IF_KUNIT bool aa_unpack_X(struct aa_ext *e, enum aa_code code)
{
	if (!aa_inbounds(e, 1))
		return false;
	if (*(u8 *) e->pos != code)
		return false;
	e->pos++;
	return true;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_X);

/**
 * aa_unpack_nameX - check is the next element is of type X with a name of @name
 * @e: serialized data extent information  (NOT NULL)
 * @code: type code
 * @name: name to match to the serialized element.  (MAYBE NULL)
 *
 * check that the next serialized data element is of type X and has a tag
 * name @name.  If @name is specified then there must be a matching
 * name element in the stream.  If @name is NULL any name element will be
 * skipped and only the typecode will be tested.
 *
 * Returns true on success (both type code and name tests match) and the read
 * head is advanced past the headers
 *
 * Returns: false if either match fails, the read head does not move
 */
VISIBLE_IF_KUNIT bool aa_unpack_nameX(struct aa_ext *e, enum aa_code code, const char *name)
{
	/*
	 * May need to reset pos if name or type doesn't match
	 */
	void *pos = e->pos;
	/*
	 * Check for presence of a tagname, and if present name size
	 * AA_NAME tag value is a u16.
	 */
	if (aa_unpack_X(e, AA_NAME)) {
		char *tag = NULL;
		size_t size = aa_unpack_u16_chunk(e, &tag);
		/* if a name is specified it must match. otherwise skip tag */
		if (name && (!size || tag[size-1] != '\0' || strcmp(name, tag)))
			goto fail;
	} else if (name) {
		/* if a name is specified and there is no name tag fail */
		goto fail;
	}

	/* now check if type code matches */
	if (aa_unpack_X(e, code))
		return true;

fail:
	e->pos = pos;
	return false;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_nameX);

static bool unpack_u8(struct aa_ext *e, u8 *data, const char *name)
{
	void *pos = e->pos;

	if (aa_unpack_nameX(e, AA_U8, name)) {
		if (!aa_inbounds(e, sizeof(u8)))
			goto fail;
		if (data)
			*data = *((u8 *)e->pos);
		e->pos += sizeof(u8);
		return true;
	}

fail:
	e->pos = pos;
	return false;
}

VISIBLE_IF_KUNIT bool aa_unpack_u32(struct aa_ext *e, u32 *data, const char *name)
{
	void *pos = e->pos;

	if (aa_unpack_nameX(e, AA_U32, name)) {
		if (!aa_inbounds(e, sizeof(u32)))
			goto fail;
		if (data)
			*data = le32_to_cpu(get_unaligned((__le32 *) e->pos));
		e->pos += sizeof(u32);
		return true;
	}

fail:
	e->pos = pos;
	return false;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_u32);

VISIBLE_IF_KUNIT bool aa_unpack_u64(struct aa_ext *e, u64 *data, const char *name)
{
	void *pos = e->pos;

	if (aa_unpack_nameX(e, AA_U64, name)) {
		if (!aa_inbounds(e, sizeof(u64)))
			goto fail;
		if (data)
			*data = le64_to_cpu(get_unaligned((__le64 *) e->pos));
		e->pos += sizeof(u64);
		return true;
	}

fail:
	e->pos = pos;
	return false;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_u64);

static bool aa_unpack_cap_low(struct aa_ext *e, kernel_cap_t *data, const char *name)
{
	u32 val;

	if (!aa_unpack_u32(e, &val, name))
		return false;
	data->val = val;
	return true;
}

static bool aa_unpack_cap_high(struct aa_ext *e, kernel_cap_t *data, const char *name)
{
	u32 val;

	if (!aa_unpack_u32(e, &val, name))
		return false;
	data->val = (u32)data->val | ((u64)val << 32);
	return true;
}

VISIBLE_IF_KUNIT bool aa_unpack_array(struct aa_ext *e, const char *name, u16 *size)
{
	void *pos = e->pos;

	if (aa_unpack_nameX(e, AA_ARRAY, name)) {
		if (!aa_inbounds(e, sizeof(u16)))
			goto fail;
		*size = le16_to_cpu(get_unaligned((__le16 *) e->pos));
		e->pos += sizeof(u16);
		return true;
	}

fail:
	e->pos = pos;
	return false;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_array);

VISIBLE_IF_KUNIT size_t aa_unpack_blob(struct aa_ext *e, char **blob, const char *name)
{
	void *pos = e->pos;

	if (aa_unpack_nameX(e, AA_BLOB, name)) {
		u32 size;
		if (!aa_inbounds(e, sizeof(u32)))
			goto fail;
		size = le32_to_cpu(get_unaligned((__le32 *) e->pos));
		e->pos += sizeof(u32);
		if (aa_inbounds(e, (size_t) size)) {
			*blob = e->pos;
			e->pos += size;
			return size;
		}
	}

fail:
	e->pos = pos;
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_blob);

VISIBLE_IF_KUNIT int aa_unpack_str(struct aa_ext *e, const char **string, const char *name)
{
	char *src_str;
	size_t size = 0;
	void *pos = e->pos;
	*string = NULL;
	if (aa_unpack_nameX(e, AA_STRING, name)) {
		size = aa_unpack_u16_chunk(e, &src_str);
		if (size) {
			/* strings are null terminated, length is size - 1 */
			if (src_str[size - 1] != 0)
				goto fail;
			*string = src_str;

			return size;
		}
	}

fail:
	e->pos = pos;
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_str);

VISIBLE_IF_KUNIT int aa_unpack_strdup(struct aa_ext *e, char **string, const char *name)
{
	const char *tmp;
	void *pos = e->pos;
	int res = aa_unpack_str(e, &tmp, name);
	*string = NULL;

	if (!res)
		return 0;

	*string = kmemdup(tmp, res, GFP_KERNEL);
	if (!*string) {
		e->pos = pos;
		return 0;
	}

	return res;
}
EXPORT_SYMBOL_IF_KUNIT(aa_unpack_strdup);


/**
 * unpack_dfa - unpack a file rule dfa
 * @e: serialized data extent information (NOT NULL)
 * @flags: dfa flags to check
 *
 * returns dfa or ERR_PTR or NULL if no dfa
 */
static struct aa_dfa *unpack_dfa(struct aa_ext *e, int flags)
{
	char *blob = NULL;
	size_t size;
	struct aa_dfa *dfa = NULL;

	size = aa_unpack_blob(e, &blob, "aadfa");
	if (size) {
		/*
		 * The dfa is aligned with in the blob to 8 bytes
		 * from the beginning of the stream.
		 * alignment adjust needed by dfa unpack
		 */
		size_t sz = blob - (char *) e->start -
			((e->pos - e->start) & 7);
		size_t pad = ALIGN(sz, 8) - sz;
		if (aa_g_paranoid_load)
			flags |= DFA_FLAG_VERIFY_STATES;
		dfa = aa_dfa_unpack(blob + pad, size - pad, flags);

		if (IS_ERR(dfa))
			return dfa;

	}

	return dfa;
}

/**
 * unpack_trans_table - unpack a profile transition table
 * @e: serialized data extent information  (NOT NULL)
 * @strs: str table to unpack to (NOT NULL)
 *
 * Returns: true if table successfully unpacked or not present
 */
static bool unpack_trans_table(struct aa_ext *e, struct aa_str_table *strs)
{
	void *saved_pos = e->pos;
	char **table = NULL;

	/* exec table is optional */
	if (aa_unpack_nameX(e, AA_STRUCT, "xtable")) {
		u16 size;
		int i;

		if (!aa_unpack_array(e, NULL, &size))
			/*
			 * Note: index into trans table array is a max
			 * of 2^24, but unpack array can only unpack
			 * an array of 2^16 in size atm so no need
			 * for size check here
			 */
			goto fail;
		table = kcalloc(size, sizeof(char *), GFP_KERNEL);
		if (!table)
			goto fail;

		for (i = 0; i < size; i++) {
			char *str;
			int c, j, pos, size2 = aa_unpack_strdup(e, &str, NULL);
			/* aa_unpack_strdup verifies that the last character is
			 * null termination byte.
			 */
			if (!size2)
				goto fail;
			table[i] = str;
			/* verify that name doesn't start with space */
			if (isspace(*str))
				goto fail;

			/* count internal #  of internal \0 */
			for (c = j = 0; j < size2 - 1; j++) {
				if (!str[j]) {
					pos = j;
					c++;
				}
			}
			if (*str == ':') {
				/* first character after : must be valid */
				if (!str[1])
					goto fail;
				/* beginning with : requires an embedded \0,
				 * verify that exactly 1 internal \0 exists
				 * trailing \0 already verified by aa_unpack_strdup
				 *
				 * convert \0 back to : for label_parse
				 */
				if (c == 1)
					str[pos] = ':';
				else if (c > 1)
					goto fail;
			} else if (c)
				/* fail - all other cases with embedded \0 */
				goto fail;
		}
		if (!aa_unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;

		strs->table = table;
		strs->size = size;
	}
	return true;

fail:
	kfree_sensitive(table);
	e->pos = saved_pos;
	return false;
}

static bool unpack_xattrs(struct aa_ext *e, struct aa_profile *profile)
{
	void *pos = e->pos;

	if (aa_unpack_nameX(e, AA_STRUCT, "xattrs")) {
		u16 size;
		int i;

		if (!aa_unpack_array(e, NULL, &size))
			goto fail;
		profile->attach.xattr_count = size;
		profile->attach.xattrs = kcalloc(size, sizeof(char *), GFP_KERNEL);
		if (!profile->attach.xattrs)
			goto fail;
		for (i = 0; i < size; i++) {
			if (!aa_unpack_strdup(e, &profile->attach.xattrs[i], NULL))
				goto fail;
		}
		if (!aa_unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	return true;

fail:
	e->pos = pos;
	return false;
}

static bool unpack_secmark(struct aa_ext *e, struct aa_ruleset *rules)
{
	void *pos = e->pos;
	u16 size;
	int i;

	if (aa_unpack_nameX(e, AA_STRUCT, "secmark")) {
		if (!aa_unpack_array(e, NULL, &size))
			goto fail;

		rules->secmark = kcalloc(size, sizeof(struct aa_secmark),
					   GFP_KERNEL);
		if (!rules->secmark)
			goto fail;

		rules->secmark_count = size;

		for (i = 0; i < size; i++) {
			if (!unpack_u8(e, &rules->secmark[i].audit, NULL))
				goto fail;
			if (!unpack_u8(e, &rules->secmark[i].deny, NULL))
				goto fail;
			if (!aa_unpack_strdup(e, &rules->secmark[i].label, NULL))
				goto fail;
		}
		if (!aa_unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	return true;

fail:
	if (rules->secmark) {
		for (i = 0; i < size; i++)
			kfree(rules->secmark[i].label);
		kfree(rules->secmark);
		rules->secmark_count = 0;
		rules->secmark = NULL;
	}

	e->pos = pos;
	return false;
}

static bool unpack_rlimits(struct aa_ext *e, struct aa_ruleset *rules)
{
	void *pos = e->pos;

	/* rlimits are optional */
	if (aa_unpack_nameX(e, AA_STRUCT, "rlimits")) {
		u16 size;
		int i;
		u32 tmp = 0;
		if (!aa_unpack_u32(e, &tmp, NULL))
			goto fail;
		rules->rlimits.mask = tmp;

		if (!aa_unpack_array(e, NULL, &size) ||
		    size > RLIM_NLIMITS)
			goto fail;
		for (i = 0; i < size; i++) {
			u64 tmp2 = 0;
			int a = aa_map_resource(i);
			if (!aa_unpack_u64(e, &tmp2, NULL))
				goto fail;
			rules->rlimits.limits[a].rlim_max = tmp2;
		}
		if (!aa_unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}
	return true;

fail:
	e->pos = pos;
	return false;
}

static bool unpack_perm(struct aa_ext *e, u32 version, struct aa_perms *perm)
{
	if (version != 1)
		return false;

	return	aa_unpack_u32(e, &perm->allow, NULL) &&
		aa_unpack_u32(e, &perm->allow, NULL) &&
		aa_unpack_u32(e, &perm->deny, NULL) &&
		aa_unpack_u32(e, &perm->subtree, NULL) &&
		aa_unpack_u32(e, &perm->cond, NULL) &&
		aa_unpack_u32(e, &perm->kill, NULL) &&
		aa_unpack_u32(e, &perm->complain, NULL) &&
		aa_unpack_u32(e, &perm->prompt, NULL) &&
		aa_unpack_u32(e, &perm->audit, NULL) &&
		aa_unpack_u32(e, &perm->quiet, NULL) &&
		aa_unpack_u32(e, &perm->hide, NULL) &&
		aa_unpack_u32(e, &perm->xindex, NULL) &&
		aa_unpack_u32(e, &perm->tag, NULL) &&
		aa_unpack_u32(e, &perm->label, NULL);
}

static ssize_t unpack_perms_table(struct aa_ext *e, struct aa_perms **perms)
{
	void *pos = e->pos;
	u16 size = 0;

	AA_BUG(!perms);
	/*
	 * policy perms are optional, in which case perms are embedded
	 * in the dfa accept table
	 */
	if (aa_unpack_nameX(e, AA_STRUCT, "perms")) {
		int i;
		u32 version;

		if (!aa_unpack_u32(e, &version, "version"))
			goto fail_reset;
		if (!aa_unpack_array(e, NULL, &size))
			goto fail_reset;
		*perms = kcalloc(size, sizeof(struct aa_perms), GFP_KERNEL);
		if (!*perms)
			goto fail_reset;
		for (i = 0; i < size; i++) {
			if (!unpack_perm(e, version, &(*perms)[i]))
				goto fail;
		}
		if (!aa_unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	} else
		*perms = NULL;

	return size;

fail:
	kfree(*perms);
fail_reset:
	e->pos = pos;
	return -EPROTO;
}

static int unpack_pdb(struct aa_ext *e, struct aa_policydb *policy,
		      bool required_dfa, bool required_trans,
		      const char **info)
{
	void *pos = e->pos;
	int i, flags, error = -EPROTO;
	ssize_t size;

	size = unpack_perms_table(e, &policy->perms);
	if (size < 0) {
		error = size;
		policy->perms = NULL;
		*info = "failed to unpack - perms";
		goto fail;
	}
	policy->size = size;

	if (policy->perms) {
		/* perms table present accept is index */
		flags = TO_ACCEPT1_FLAG(YYTD_DATA32);
	} else {
		/* packed perms in accept1 and accept2 */
		flags = TO_ACCEPT1_FLAG(YYTD_DATA32) |
			TO_ACCEPT2_FLAG(YYTD_DATA32);
	}

	policy->dfa = unpack_dfa(e, flags);
	if (IS_ERR(policy->dfa)) {
		error = PTR_ERR(policy->dfa);
		policy->dfa = NULL;
		*info = "failed to unpack - dfa";
		goto fail;
	} else if (!policy->dfa) {
		if (required_dfa) {
			*info = "missing required dfa";
			goto fail;
		}
		goto out;
	}

	/*
	 * only unpack the following if a dfa is present
	 *
	 * sadly start was given different names for file and policydb
	 * but since it is optional we can try both
	 */
	if (!aa_unpack_u32(e, &policy->start[0], "start"))
		/* default start state */
		policy->start[0] = DFA_START;
	if (!aa_unpack_u32(e, &policy->start[AA_CLASS_FILE], "dfa_start")) {
		/* default start state for xmatch and file dfa */
		policy->start[AA_CLASS_FILE] = DFA_START;
	}	/* setup class index */
	for (i = AA_CLASS_FILE + 1; i <= AA_CLASS_LAST; i++) {
		policy->start[i] = aa_dfa_next(policy->dfa, policy->start[0],
					       i);
	}
	if (!unpack_trans_table(e, &policy->trans) && required_trans) {
		*info = "failed to unpack profile transition table";
		goto fail;
	}

	/* TODO: move compat mapping here, requires dfa merging first */
	/* TODO: move verify here, it has to be done after compat mappings */
out:
	return 0;

fail:
	e->pos = pos;
	return error;
}

static u32 strhash(const void *data, u32 len, u32 seed)
{
	const char * const *key = data;

	return jhash(*key, strlen(*key), seed);
}

static int datacmp(struct rhashtable_compare_arg *arg, const void *obj)
{
	const struct aa_data *data = obj;
	const char * const *key = arg->key;

	return strcmp(data->key, *key);
}

/**
 * unpack_profile - unpack a serialized profile
 * @e: serialized data extent information (NOT NULL)
 * @ns_name: pointer of newly allocated copy of %NULL in case of error
 *
 * NOTE: unpack profile sets audit struct if there is a failure
 */
static struct aa_profile *unpack_profile(struct aa_ext *e, char **ns_name)
{
	struct aa_ruleset *rules;
	struct aa_profile *profile = NULL;
	const char *tmpname, *tmpns = NULL, *name = NULL;
	const char *info = "failed to unpack profile";
	size_t ns_len;
	struct rhashtable_params params = { 0 };
	char *key = NULL;
	struct aa_data *data;
	int error = -EPROTO;
	kernel_cap_t tmpcap;
	u32 tmp;

	*ns_name = NULL;

	/* check that we have the right struct being passed */
	if (!aa_unpack_nameX(e, AA_STRUCT, "profile"))
		goto fail;
	if (!aa_unpack_str(e, &name, NULL))
		goto fail;
	if (*name == '\0')
		goto fail;

	tmpname = aa_splitn_fqname(name, strlen(name), &tmpns, &ns_len);
	if (tmpns) {
		*ns_name = kstrndup(tmpns, ns_len, GFP_KERNEL);
		if (!*ns_name) {
			info = "out of memory";
			error = -ENOMEM;
			goto fail;
		}
		name = tmpname;
	}

	profile = aa_alloc_profile(name, NULL, GFP_KERNEL);
	if (!profile) {
		info = "out of memory";
		error = -ENOMEM;
		goto fail;
	}
	rules = list_first_entry(&profile->rules, typeof(*rules), list);

	/* profile renaming is optional */
	(void) aa_unpack_str(e, &profile->rename, "rename");

	/* attachment string is optional */
	(void) aa_unpack_str(e, &profile->attach.xmatch_str, "attach");

	/* xmatch is optional and may be NULL */
	error = unpack_pdb(e, &profile->attach.xmatch, false, false, &info);
	if (error) {
		info = "bad xmatch";
		goto fail;
	}

	/* neither xmatch_len not xmatch_perms are optional if xmatch is set */
	if (profile->attach.xmatch.dfa) {
		if (!aa_unpack_u32(e, &tmp, NULL)) {
			info = "missing xmatch len";
			goto fail;
		}
		profile->attach.xmatch_len = tmp;
		profile->attach.xmatch.start[AA_CLASS_XMATCH] = DFA_START;
		if (!profile->attach.xmatch.perms) {
			error = aa_compat_map_xmatch(&profile->attach.xmatch);
			if (error) {
				info = "failed to convert xmatch permission table";
				goto fail;
			}
		}
	}

	/* disconnected attachment string is optional */
	(void) aa_unpack_str(e, &profile->disconnected, "disconnected");

	/* per profile debug flags (complain, audit) */
	if (!aa_unpack_nameX(e, AA_STRUCT, "flags")) {
		info = "profile missing flags";
		goto fail;
	}
	info = "failed to unpack profile flags";
	if (!aa_unpack_u32(e, &tmp, NULL))
		goto fail;
	if (tmp & PACKED_FLAG_HAT)
		profile->label.flags |= FLAG_HAT;
	if (tmp & PACKED_FLAG_DEBUG1)
		profile->label.flags |= FLAG_DEBUG1;
	if (tmp & PACKED_FLAG_DEBUG2)
		profile->label.flags |= FLAG_DEBUG2;
	if (!aa_unpack_u32(e, &tmp, NULL))
		goto fail;
	if (tmp == PACKED_MODE_COMPLAIN || (e->version & FORCE_COMPLAIN_FLAG)) {
		profile->mode = APPARMOR_COMPLAIN;
	} else if (tmp == PACKED_MODE_ENFORCE) {
		profile->mode = APPARMOR_ENFORCE;
	} else if (tmp == PACKED_MODE_KILL) {
		profile->mode = APPARMOR_KILL;
	} else if (tmp == PACKED_MODE_UNCONFINED) {
		profile->mode = APPARMOR_UNCONFINED;
		profile->label.flags |= FLAG_UNCONFINED;
	} else if (tmp == PACKED_MODE_USER) {
		profile->mode = APPARMOR_USER;
	} else {
		goto fail;
	}
	if (!aa_unpack_u32(e, &tmp, NULL))
		goto fail;
	if (tmp)
		profile->audit = AUDIT_ALL;

	if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
		goto fail;

	/* path_flags is optional */
	if (aa_unpack_u32(e, &profile->path_flags, "path_flags"))
		profile->path_flags |= profile->label.flags &
			PATH_MEDIATE_DELETED;
	else
		/* set a default value if path_flags field is not present */
		profile->path_flags = PATH_MEDIATE_DELETED;

	info = "failed to unpack profile capabilities";
	if (!aa_unpack_cap_low(e, &rules->caps.allow, NULL))
		goto fail;
	if (!aa_unpack_cap_low(e, &rules->caps.audit, NULL))
		goto fail;
	if (!aa_unpack_cap_low(e, &rules->caps.quiet, NULL))
		goto fail;
	if (!aa_unpack_cap_low(e, &tmpcap, NULL))
		goto fail;

	info = "failed to unpack upper profile capabilities";
	if (aa_unpack_nameX(e, AA_STRUCT, "caps64")) {
		/* optional upper half of 64 bit caps */
		if (!aa_unpack_cap_high(e, &rules->caps.allow, NULL))
			goto fail;
		if (!aa_unpack_cap_high(e, &rules->caps.audit, NULL))
			goto fail;
		if (!aa_unpack_cap_high(e, &rules->caps.quiet, NULL))
			goto fail;
		if (!aa_unpack_cap_high(e, &tmpcap, NULL))
			goto fail;
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	info = "failed to unpack extended profile capabilities";
	if (aa_unpack_nameX(e, AA_STRUCT, "capsx")) {
		/* optional extended caps mediation mask */
		if (!aa_unpack_cap_low(e, &rules->caps.extended, NULL))
			goto fail;
		if (!aa_unpack_cap_high(e, &rules->caps.extended, NULL))
			goto fail;
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	if (!unpack_xattrs(e, profile)) {
		info = "failed to unpack profile xattrs";
		goto fail;
	}

	if (!unpack_rlimits(e, rules)) {
		info = "failed to unpack profile rlimits";
		goto fail;
	}

	if (!unpack_secmark(e, rules)) {
		info = "failed to unpack profile secmark rules";
		goto fail;
	}

	if (aa_unpack_nameX(e, AA_STRUCT, "policydb")) {
		/* generic policy dfa - optional and may be NULL */
		info = "failed to unpack policydb";
		error = unpack_pdb(e, &rules->policy, true, false,
				   &info);
		if (error)
			goto fail;
		/* Fixup: drop when we get rid of start array */
		if (aa_dfa_next(rules->policy.dfa, rules->policy.start[0],
				AA_CLASS_FILE))
			rules->policy.start[AA_CLASS_FILE] =
			  aa_dfa_next(rules->policy.dfa,
				      rules->policy.start[0],
				      AA_CLASS_FILE);
		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
		if (!rules->policy.perms) {
			error = aa_compat_map_policy(&rules->policy,
						     e->version);
			if (error) {
				info = "failed to remap policydb permission table";
				goto fail;
			}
		}
	} else {
		rules->policy.dfa = aa_get_dfa(nulldfa);
		rules->policy.perms = kcalloc(2, sizeof(struct aa_perms),
					      GFP_KERNEL);
		if (!rules->policy.perms)
			goto fail;
		rules->policy.size = 2;
	}
	/* get file rules */
	error = unpack_pdb(e, &rules->file, false, true, &info);
	if (error) {
		goto fail;
	} else if (rules->file.dfa) {
		if (!rules->file.perms) {
			error = aa_compat_map_file(&rules->file);
			if (error) {
				info = "failed to remap file permission table";
				goto fail;
			}
		}
	} else if (rules->policy.dfa &&
		   rules->policy.start[AA_CLASS_FILE]) {
		rules->file.dfa = aa_get_dfa(rules->policy.dfa);
		rules->file.start[AA_CLASS_FILE] = rules->policy.start[AA_CLASS_FILE];
		rules->file.perms = kcalloc(rules->policy.size,
					    sizeof(struct aa_perms),
					    GFP_KERNEL);
		if (!rules->file.perms)
			goto fail;
		memcpy(rules->file.perms, rules->policy.perms,
		       rules->policy.size * sizeof(struct aa_perms));
		rules->file.size = rules->policy.size;
	} else {
		rules->file.dfa = aa_get_dfa(nulldfa);
		rules->file.perms = kcalloc(2, sizeof(struct aa_perms),
					    GFP_KERNEL);
		if (!rules->file.perms)
			goto fail;
		rules->file.size = 2;
	}
	error = -EPROTO;
	if (aa_unpack_nameX(e, AA_STRUCT, "data")) {
		info = "out of memory";
		profile->data = kzalloc(sizeof(*profile->data), GFP_KERNEL);
		if (!profile->data) {
			error = -ENOMEM;
			goto fail;
		}
		params.nelem_hint = 3;
		params.key_len = sizeof(void *);
		params.key_offset = offsetof(struct aa_data, key);
		params.head_offset = offsetof(struct aa_data, head);
		params.hashfn = strhash;
		params.obj_cmpfn = datacmp;

		if (rhashtable_init(profile->data, &params)) {
			info = "failed to init key, value hash table";
			goto fail;
		}

		while (aa_unpack_strdup(e, &key, NULL)) {
			data = kzalloc(sizeof(*data), GFP_KERNEL);
			if (!data) {
				kfree_sensitive(key);
				error = -ENOMEM;
				goto fail;
			}

			data->key = key;
			data->size = aa_unpack_blob(e, &data->data, NULL);
			data->data = kvmemdup(data->data, data->size, GFP_KERNEL);
			if (data->size && !data->data) {
				kfree_sensitive(data->key);
				kfree_sensitive(data);
				error = -ENOMEM;
				goto fail;
			}

			if (rhashtable_insert_fast(profile->data, &data->head,
						   profile->data->p)) {
				kfree_sensitive(data->key);
				kfree_sensitive(data);
				info = "failed to insert data to table";
				goto fail;
			}
		}

		if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL)) {
			info = "failed to unpack end of key, value data table";
			goto fail;
		}
	}

	if (!aa_unpack_nameX(e, AA_STRUCTEND, NULL)) {
		info = "failed to unpack end of profile";
		goto fail;
	}

	return profile;

fail:
	if (error == 0)
		/* default error covers most cases */
		error = -EPROTO;
	if (*ns_name) {
		kfree(*ns_name);
		*ns_name = NULL;
	}
	if (profile)
		name = NULL;
	else if (!name)
		name = "unknown";
	audit_iface(profile, NULL, name, info, e, error);
	aa_free_profile(profile);

	return ERR_PTR(error);
}

/**
 * verify_header - unpack serialized stream header
 * @e: serialized data read head (NOT NULL)
 * @required: whether the header is required or optional
 * @ns: Returns - namespace if one is specified else NULL (NOT NULL)
 *
 * Returns: error or 0 if header is good
 */
static int verify_header(struct aa_ext *e, int required, const char **ns)
{
	int error = -EPROTONOSUPPORT;
	const char *name = NULL;
	*ns = NULL;

	/* get the interface version */
	if (!aa_unpack_u32(e, &e->version, "version")) {
		if (required) {
			audit_iface(NULL, NULL, NULL, "invalid profile format",
				    e, error);
			return error;
		}
	}

	/* Check that the interface version is currently supported.
	 * if not specified use previous version
	 * Mask off everything that is not kernel abi version
	 */
	if (VERSION_LT(e->version, v5) || VERSION_GT(e->version, v9)) {
		audit_iface(NULL, NULL, NULL, "unsupported interface version",
			    e, error);
		return error;
	}

	/* read the namespace if present */
	if (aa_unpack_str(e, &name, "namespace")) {
		if (*name == '\0') {
			audit_iface(NULL, NULL, NULL, "invalid namespace name",
				    e, error);
			return error;
		}
		if (*ns && strcmp(*ns, name)) {
			audit_iface(NULL, NULL, NULL, "invalid ns change", e,
				    error);
		} else if (!*ns) {
			*ns = kstrdup(name, GFP_KERNEL);
			if (!*ns)
				return -ENOMEM;
		}
	}

	return 0;
}

/**
 * verify_dfa_accept_index - verify accept indexes are in range of perms table
 * @dfa: the dfa to check accept indexes are in range
 * table_size: the permission table size the indexes should be within
 */
static bool verify_dfa_accept_index(struct aa_dfa *dfa, int table_size)
{
	int i;
	for (i = 0; i < dfa->tables[YYTD_ID_ACCEPT]->td_lolen; i++) {
		if (ACCEPT_TABLE(dfa)[i] >= table_size)
			return false;
	}
	return true;
}

static bool verify_perm(struct aa_perms *perm)
{
	/* TODO: allow option to just force the perms into a valid state */
	if (perm->allow & perm->deny)
		return false;
	if (perm->subtree & ~perm->allow)
		return false;
	if (perm->cond & (perm->allow | perm->deny))
		return false;
	if (perm->kill & perm->allow)
		return false;
	if (perm->complain & (perm->allow | perm->deny))
		return false;
	if (perm->prompt & (perm->allow | perm->deny))
		return false;
	if (perm->complain & perm->prompt)
		return false;
	if (perm->hide & perm->allow)
		return false;

	return true;
}

static bool verify_perms(struct aa_policydb *pdb)
{
	int i;

	for (i = 0; i < pdb->size; i++) {
		if (!verify_perm(&pdb->perms[i]))
			return false;
		/* verify indexes into str table */
		if ((pdb->perms[i].xindex & AA_X_TYPE_MASK) == AA_X_TABLE &&
		    (pdb->perms[i].xindex & AA_X_INDEX_MASK) >= pdb->trans.size)
			return false;
		if (pdb->perms[i].tag && pdb->perms[i].tag >= pdb->trans.size)
			return false;
		if (pdb->perms[i].label &&
		    pdb->perms[i].label >= pdb->trans.size)
			return false;
	}

	return true;
}

/**
 * verify_profile - Do post unpack analysis to verify profile consistency
 * @profile: profile to verify (NOT NULL)
 *
 * Returns: 0 if passes verification else error
 *
 * This verification is post any unpack mapping or changes
 */
static int verify_profile(struct aa_profile *profile)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	if (!rules)
		return 0;

	if ((rules->file.dfa && !verify_dfa_accept_index(rules->file.dfa,
							 rules->file.size)) ||
	    (rules->policy.dfa &&
	     !verify_dfa_accept_index(rules->policy.dfa, rules->policy.size))) {
		audit_iface(profile, NULL, NULL,
			    "Unpack: Invalid named transition", NULL, -EPROTO);
		return -EPROTO;
	}

	if (!verify_perms(&rules->file)) {
		audit_iface(profile, NULL, NULL,
			    "Unpack: Invalid perm index", NULL, -EPROTO);
		return -EPROTO;
	}
	if (!verify_perms(&rules->policy)) {
		audit_iface(profile, NULL, NULL,
			    "Unpack: Invalid perm index", NULL, -EPROTO);
		return -EPROTO;
	}
	if (!verify_perms(&profile->attach.xmatch)) {
		audit_iface(profile, NULL, NULL,
			    "Unpack: Invalid perm index", NULL, -EPROTO);
		return -EPROTO;
	}

	return 0;
}

void aa_load_ent_free(struct aa_load_ent *ent)
{
	if (ent) {
		aa_put_profile(ent->rename);
		aa_put_profile(ent->old);
		aa_put_profile(ent->new);
		kfree(ent->ns_name);
		kfree_sensitive(ent);
	}
}

struct aa_load_ent *aa_load_ent_alloc(void)
{
	struct aa_load_ent *ent = kzalloc(sizeof(*ent), GFP_KERNEL);
	if (ent)
		INIT_LIST_HEAD(&ent->list);
	return ent;
}

static int compress_zstd(const char *src, size_t slen, char **dst, size_t *dlen)
{
#ifdef CONFIG_SECURITY_APPARMOR_EXPORT_BINARY
	const zstd_parameters params =
		zstd_get_params(aa_g_rawdata_compression_level, slen);
	const size_t wksp_len = zstd_cctx_workspace_bound(&params.cParams);
	void *wksp = NULL;
	zstd_cctx *ctx = NULL;
	size_t out_len = zstd_compress_bound(slen);
	void *out = NULL;
	int ret = 0;

	out = kvzalloc(out_len, GFP_KERNEL);
	if (!out) {
		ret = -ENOMEM;
		goto cleanup;
	}

	wksp = kvzalloc(wksp_len, GFP_KERNEL);
	if (!wksp) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ctx = zstd_init_cctx(wksp, wksp_len);
	if (!ctx) {
		ret = -EINVAL;
		goto cleanup;
	}

	out_len = zstd_compress_cctx(ctx, out, out_len, src, slen, &params);
	if (zstd_is_error(out_len) || out_len >= slen) {
		ret = -EINVAL;
		goto cleanup;
	}

	if (is_vmalloc_addr(out)) {
		*dst = kvzalloc(out_len, GFP_KERNEL);
		if (*dst) {
			memcpy(*dst, out, out_len);
			kvfree(out);
			out = NULL;
		}
	} else {
		/*
		 * If the staging buffer was kmalloc'd, then using krealloc is
		 * probably going to be faster. The destination buffer will
		 * always be smaller, so it's just shrunk, avoiding a memcpy
		 */
		*dst = krealloc(out, out_len, GFP_KERNEL);
	}

	if (!*dst) {
		ret = -ENOMEM;
		goto cleanup;
	}

	*dlen = out_len;

cleanup:
	if (ret) {
		kvfree(out);
		*dst = NULL;
	}

	kvfree(wksp);
	return ret;
#else
	*dlen = slen;
	return 0;
#endif
}

static int compress_loaddata(struct aa_loaddata *data)
{
	AA_BUG(data->compressed_size > 0);

	/*
	 * Shortcut the no compression case, else we increase the amount of
	 * storage required by a small amount
	 */
	if (aa_g_rawdata_compression_level != 0) {
		void *udata = data->data;
		int error = compress_zstd(udata, data->size, &data->data,
					  &data->compressed_size);
		if (error) {
			data->compressed_size = data->size;
			return error;
		}
		if (udata != data->data)
			kvfree(udata);
	} else
		data->compressed_size = data->size;

	return 0;
}

/**
 * aa_unpack - unpack packed binary profile(s) data loaded from user space
 * @udata: user data copied to kmem  (NOT NULL)
 * @lh: list to place unpacked profiles in a aa_repl_ws
 * @ns: Returns namespace profile is in if specified else NULL (NOT NULL)
 *
 * Unpack user data and return refcounted allocated profile(s) stored in
 * @lh in order of discovery, with the list chain stored in base.list
 * or error
 *
 * Returns: profile(s) on @lh else error pointer if fails to unpack
 */
int aa_unpack(struct aa_loaddata *udata, struct list_head *lh,
	      const char **ns)
{
	struct aa_load_ent *tmp, *ent;
	struct aa_profile *profile = NULL;
	char *ns_name = NULL;
	int error;
	struct aa_ext e = {
		.start = udata->data,
		.end = udata->data + udata->size,
		.pos = udata->data,
	};

	*ns = NULL;
	while (e.pos < e.end) {
		void *start;
		error = verify_header(&e, e.pos == e.start, ns);
		if (error)
			goto fail;

		start = e.pos;
		profile = unpack_profile(&e, &ns_name);
		if (IS_ERR(profile)) {
			error = PTR_ERR(profile);
			goto fail;
		}

		error = verify_profile(profile);
		if (error)
			goto fail_profile;

		if (aa_g_hash_policy)
			error = aa_calc_profile_hash(profile, e.version, start,
						     e.pos - start);
		if (error)
			goto fail_profile;

		ent = aa_load_ent_alloc();
		if (!ent) {
			error = -ENOMEM;
			goto fail_profile;
		}

		ent->new = profile;
		ent->ns_name = ns_name;
		ns_name = NULL;
		list_add_tail(&ent->list, lh);
	}
	udata->abi = e.version & K_ABI_MASK;
	if (aa_g_hash_policy) {
		udata->hash = aa_calc_hash(udata->data, udata->size);
		if (IS_ERR(udata->hash)) {
			error = PTR_ERR(udata->hash);
			udata->hash = NULL;
			goto fail;
		}
	}

	if (aa_g_export_binary) {
		error = compress_loaddata(udata);
		if (error)
			goto fail;
	}
	return 0;

fail_profile:
	kfree(ns_name);
	aa_put_profile(profile);

fail:
	list_for_each_entry_safe(ent, tmp, lh, list) {
		list_del_init(&ent->list);
		aa_load_ent_free(ent);
	}

	return error;
}
