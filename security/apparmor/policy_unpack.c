/*
 * AppArmor security module
 *
 * This file contains AppArmor functions for unpacking policy loaded from
 * userspace.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * AppArmor uses a serialized binary format for loading policy. To find
 * policy format documentation see Documentation/admin-guide/LSM/apparmor.rst
 * All policy is validated before it is used.
 */

#include <asm/unaligned.h>
#include <linux/ctype.h>
#include <linux/errno.h>

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/cred.h"
#include "include/crypto.h"
#include "include/match.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/policy_unpack.h"

#define K_ABI_MASK 0x3ff
#define FORCE_COMPLAIN_FLAG 0x800
#define VERSION_LT(X, Y) (((X) & K_ABI_MASK) < ((Y) & K_ABI_MASK))
#define VERSION_GT(X, Y) (((X) & K_ABI_MASK) > ((Y) & K_ABI_MASK))

#define v5	5	/* base version */
#define v6	6	/* per entry policydb mediation check */
#define v7	7
#define v8	8	/* full network masking */

/*
 * The AppArmor interface treats data as a type byte followed by the
 * actual data.  The interface has the notion of a a named entry
 * which has a name (AA_NAME typecode followed by name string) followed by
 * the entries typecode and data.  Named types allow for optional
 * elements and extensions to be added and tested for without breaking
 * backwards compatibility.
 */

enum aa_code {
	AA_U8,
	AA_U16,
	AA_U32,
	AA_U64,
	AA_NAME,		/* same as string except it is items name */
	AA_STRING,
	AA_BLOB,
	AA_STRUCT,
	AA_STRUCTEND,
	AA_LIST,
	AA_LISTEND,
	AA_ARRAY,
	AA_ARRAYEND,
};

/*
 * aa_ext is the read of the buffer containing the serialized profile.  The
 * data is copied into a kernel buffer in apparmorfs and then handed off to
 * the unpack routines.
 */
struct aa_ext {
	void *start;
	void *end;
	void *pos;		/* pointer to current position in the buffer */
	u32 version;
};

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
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, NULL);
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
	AA_BUG(!data->dents[AAFS_LOADDATA_REVISION]);
	AA_BUG(!mutex_is_locked(&data->ns->lock));
	AA_BUG(data->revision > revision);

	data->revision = revision;
	d_inode(data->dents[AAFS_LOADDATA_DIR])->i_mtime =
		current_time(d_inode(data->dents[AAFS_LOADDATA_DIR]));
	d_inode(data->dents[AAFS_LOADDATA_REVISION])->i_mtime =
		current_time(d_inode(data->dents[AAFS_LOADDATA_REVISION]));
}

bool aa_rawdata_eq(struct aa_loaddata *l, struct aa_loaddata *r)
{
	if (l->size != r->size)
		return false;
	if (aa_g_hash_policy && memcmp(l->hash, r->hash, aa_hash_size()) != 0)
		return false;
	return memcmp(l->data, r->data, r->size) == 0;
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

	kzfree(d->hash);
	kzfree(d->name);
	kvfree(d->data);
	kzfree(d);
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
static bool inbounds(struct aa_ext *e, size_t size)
{
	return (size <= e->end - e->pos);
}

static void *kvmemdup(const void *src, size_t len)
{
	void *p = kvmalloc(len, GFP_KERNEL);

	if (p)
		memcpy(p, src, len);
	return p;
}

/**
 * aa_u16_chunck - test and do bounds checking for a u16 size based chunk
 * @e: serialized data read head (NOT NULL)
 * @chunk: start address for chunk of data (NOT NULL)
 *
 * Returns: the size of chunk found with the read head at the end of the chunk.
 */
static size_t unpack_u16_chunk(struct aa_ext *e, char **chunk)
{
	size_t size = 0;

	if (!inbounds(e, sizeof(u16)))
		return 0;
	size = le16_to_cpu(get_unaligned((__le16 *) e->pos));
	e->pos += sizeof(__le16);
	if (!inbounds(e, size))
		return 0;
	*chunk = e->pos;
	e->pos += size;
	return size;
}

/* unpack control byte */
static bool unpack_X(struct aa_ext *e, enum aa_code code)
{
	if (!inbounds(e, 1))
		return 0;
	if (*(u8 *) e->pos != code)
		return 0;
	e->pos++;
	return 1;
}

/**
 * unpack_nameX - check is the next element is of type X with a name of @name
 * @e: serialized data extent information  (NOT NULL)
 * @code: type code
 * @name: name to match to the serialized element.  (MAYBE NULL)
 *
 * check that the next serialized data element is of type X and has a tag
 * name @name.  If @name is specified then there must be a matching
 * name element in the stream.  If @name is NULL any name element will be
 * skipped and only the typecode will be tested.
 *
 * Returns 1 on success (both type code and name tests match) and the read
 * head is advanced past the headers
 *
 * Returns: 0 if either match fails, the read head does not move
 */
static bool unpack_nameX(struct aa_ext *e, enum aa_code code, const char *name)
{
	/*
	 * May need to reset pos if name or type doesn't match
	 */
	void *pos = e->pos;
	/*
	 * Check for presence of a tagname, and if present name size
	 * AA_NAME tag value is a u16.
	 */
	if (unpack_X(e, AA_NAME)) {
		char *tag = NULL;
		size_t size = unpack_u16_chunk(e, &tag);
		/* if a name is specified it must match. otherwise skip tag */
		if (name && (!size || strcmp(name, tag)))
			goto fail;
	} else if (name) {
		/* if a name is specified and there is no name tag fail */
		goto fail;
	}

	/* now check if type code matches */
	if (unpack_X(e, code))
		return 1;

fail:
	e->pos = pos;
	return 0;
}

static bool unpack_u8(struct aa_ext *e, u8 *data, const char *name)
{
	if (unpack_nameX(e, AA_U8, name)) {
		if (!inbounds(e, sizeof(u8)))
			return 0;
		if (data)
			*data = get_unaligned((u8 *)e->pos);
		e->pos += sizeof(u8);
		return 1;
	}
	return 0;
}

static bool unpack_u32(struct aa_ext *e, u32 *data, const char *name)
{
	if (unpack_nameX(e, AA_U32, name)) {
		if (!inbounds(e, sizeof(u32)))
			return 0;
		if (data)
			*data = le32_to_cpu(get_unaligned((__le32 *) e->pos));
		e->pos += sizeof(u32);
		return 1;
	}
	return 0;
}

static bool unpack_u64(struct aa_ext *e, u64 *data, const char *name)
{
	if (unpack_nameX(e, AA_U64, name)) {
		if (!inbounds(e, sizeof(u64)))
			return 0;
		if (data)
			*data = le64_to_cpu(get_unaligned((__le64 *) e->pos));
		e->pos += sizeof(u64);
		return 1;
	}
	return 0;
}

static size_t unpack_array(struct aa_ext *e, const char *name)
{
	if (unpack_nameX(e, AA_ARRAY, name)) {
		int size;
		if (!inbounds(e, sizeof(u16)))
			return 0;
		size = (int)le16_to_cpu(get_unaligned((__le16 *) e->pos));
		e->pos += sizeof(u16);
		return size;
	}
	return 0;
}

static size_t unpack_blob(struct aa_ext *e, char **blob, const char *name)
{
	if (unpack_nameX(e, AA_BLOB, name)) {
		u32 size;
		if (!inbounds(e, sizeof(u32)))
			return 0;
		size = le32_to_cpu(get_unaligned((__le32 *) e->pos));
		e->pos += sizeof(u32);
		if (inbounds(e, (size_t) size)) {
			*blob = e->pos;
			e->pos += size;
			return size;
		}
	}
	return 0;
}

static int unpack_str(struct aa_ext *e, const char **string, const char *name)
{
	char *src_str;
	size_t size = 0;
	void *pos = e->pos;
	*string = NULL;
	if (unpack_nameX(e, AA_STRING, name)) {
		size = unpack_u16_chunk(e, &src_str);
		if (size) {
			/* strings are null terminated, length is size - 1 */
			if (src_str[size - 1] != 0)
				goto fail;
			*string = src_str;
		}
	}
	return size;

fail:
	e->pos = pos;
	return 0;
}

static int unpack_strdup(struct aa_ext *e, char **string, const char *name)
{
	const char *tmp;
	void *pos = e->pos;
	int res = unpack_str(e, &tmp, name);
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


/**
 * unpack_dfa - unpack a file rule dfa
 * @e: serialized data extent information (NOT NULL)
 *
 * returns dfa or ERR_PTR or NULL if no dfa
 */
static struct aa_dfa *unpack_dfa(struct aa_ext *e)
{
	char *blob = NULL;
	size_t size;
	struct aa_dfa *dfa = NULL;

	size = unpack_blob(e, &blob, "aadfa");
	if (size) {
		/*
		 * The dfa is aligned with in the blob to 8 bytes
		 * from the beginning of the stream.
		 * alignment adjust needed by dfa unpack
		 */
		size_t sz = blob - (char *) e->start -
			((e->pos - e->start) & 7);
		size_t pad = ALIGN(sz, 8) - sz;
		int flags = TO_ACCEPT1_FLAG(YYTD_DATA32) |
			TO_ACCEPT2_FLAG(YYTD_DATA32) | DFA_FLAG_VERIFY_STATES;
		dfa = aa_dfa_unpack(blob + pad, size - pad, flags);

		if (IS_ERR(dfa))
			return dfa;

	}

	return dfa;
}

/**
 * unpack_trans_table - unpack a profile transition table
 * @e: serialized data extent information  (NOT NULL)
 * @profile: profile to add the accept table to (NOT NULL)
 *
 * Returns: 1 if table successfully unpacked
 */
static bool unpack_trans_table(struct aa_ext *e, struct aa_profile *profile)
{
	void *saved_pos = e->pos;

	/* exec table is optional */
	if (unpack_nameX(e, AA_STRUCT, "xtable")) {
		int i, size;

		size = unpack_array(e, NULL);
		/* currently 4 exec bits and entries 0-3 are reserved iupcx */
		if (size > 16 - 4)
			goto fail;
		profile->file.trans.table = kcalloc(size, sizeof(char *),
						    GFP_KERNEL);
		if (!profile->file.trans.table)
			goto fail;

		profile->file.trans.size = size;
		for (i = 0; i < size; i++) {
			char *str;
			int c, j, pos, size2 = unpack_strdup(e, &str, NULL);
			/* unpack_strdup verifies that the last character is
			 * null termination byte.
			 */
			if (!size2)
				goto fail;
			profile->file.trans.table[i] = str;
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
				 * trailing \0 already verified by unpack_strdup
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
		if (!unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}
	return 1;

fail:
	aa_free_domain_entries(&profile->file.trans);
	e->pos = saved_pos;
	return 0;
}

static bool unpack_xattrs(struct aa_ext *e, struct aa_profile *profile)
{
	void *pos = e->pos;

	if (unpack_nameX(e, AA_STRUCT, "xattrs")) {
		int i, size;

		size = unpack_array(e, NULL);
		profile->xattr_count = size;
		profile->xattrs = kcalloc(size, sizeof(char *), GFP_KERNEL);
		if (!profile->xattrs)
			goto fail;
		for (i = 0; i < size; i++) {
			if (!unpack_strdup(e, &profile->xattrs[i], NULL))
				goto fail;
		}
		if (!unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	return 1;

fail:
	e->pos = pos;
	return 0;
}

static bool unpack_secmark(struct aa_ext *e, struct aa_profile *profile)
{
	void *pos = e->pos;
	int i, size;

	if (unpack_nameX(e, AA_STRUCT, "secmark")) {
		size = unpack_array(e, NULL);

		profile->secmark = kcalloc(size, sizeof(struct aa_secmark),
					   GFP_KERNEL);
		if (!profile->secmark)
			goto fail;

		profile->secmark_count = size;

		for (i = 0; i < size; i++) {
			if (!unpack_u8(e, &profile->secmark[i].audit, NULL))
				goto fail;
			if (!unpack_u8(e, &profile->secmark[i].deny, NULL))
				goto fail;
			if (!unpack_strdup(e, &profile->secmark[i].label, NULL))
				goto fail;
		}
		if (!unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	return 1;

fail:
	if (profile->secmark) {
		for (i = 0; i < size; i++)
			kfree(profile->secmark[i].label);
		kfree(profile->secmark);
		profile->secmark_count = 0;
		profile->secmark = NULL;
	}

	e->pos = pos;
	return 0;
}

static bool unpack_rlimits(struct aa_ext *e, struct aa_profile *profile)
{
	void *pos = e->pos;

	/* rlimits are optional */
	if (unpack_nameX(e, AA_STRUCT, "rlimits")) {
		int i, size;
		u32 tmp = 0;
		if (!unpack_u32(e, &tmp, NULL))
			goto fail;
		profile->rlimits.mask = tmp;

		size = unpack_array(e, NULL);
		if (size > RLIM_NLIMITS)
			goto fail;
		for (i = 0; i < size; i++) {
			u64 tmp2 = 0;
			int a = aa_map_resource(i);
			if (!unpack_u64(e, &tmp2, NULL))
				goto fail;
			profile->rlimits.limits[a].rlim_max = tmp2;
		}
		if (!unpack_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}
	return 1;

fail:
	e->pos = pos;
	return 0;
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
 *
 * NOTE: unpack profile sets audit struct if there is a failure
 */
static struct aa_profile *unpack_profile(struct aa_ext *e, char **ns_name)
{
	struct aa_profile *profile = NULL;
	const char *tmpname, *tmpns = NULL, *name = NULL;
	const char *info = "failed to unpack profile";
	size_t ns_len;
	struct rhashtable_params params = { 0 };
	char *key = NULL;
	struct aa_data *data;
	int i, error = -EPROTO;
	kernel_cap_t tmpcap;
	u32 tmp;

	*ns_name = NULL;

	/* check that we have the right struct being passed */
	if (!unpack_nameX(e, AA_STRUCT, "profile"))
		goto fail;
	if (!unpack_str(e, &name, NULL))
		goto fail;
	if (*name == '\0')
		goto fail;

	tmpname = aa_splitn_fqname(name, strlen(name), &tmpns, &ns_len);
	if (tmpns) {
		*ns_name = kstrndup(tmpns, ns_len, GFP_KERNEL);
		if (!*ns_name) {
			info = "out of memory";
			goto fail;
		}
		name = tmpname;
	}

	profile = aa_alloc_profile(name, NULL, GFP_KERNEL);
	if (!profile)
		return ERR_PTR(-ENOMEM);

	/* profile renaming is optional */
	(void) unpack_str(e, &profile->rename, "rename");

	/* attachment string is optional */
	(void) unpack_str(e, &profile->attach, "attach");

	/* xmatch is optional and may be NULL */
	profile->xmatch = unpack_dfa(e);
	if (IS_ERR(profile->xmatch)) {
		error = PTR_ERR(profile->xmatch);
		profile->xmatch = NULL;
		info = "bad xmatch";
		goto fail;
	}
	/* xmatch_len is not optional if xmatch is set */
	if (profile->xmatch) {
		if (!unpack_u32(e, &tmp, NULL)) {
			info = "missing xmatch len";
			goto fail;
		}
		profile->xmatch_len = tmp;
	}

	/* disconnected attachment string is optional */
	(void) unpack_str(e, &profile->disconnected, "disconnected");

	/* per profile debug flags (complain, audit) */
	if (!unpack_nameX(e, AA_STRUCT, "flags")) {
		info = "profile missing flags";
		goto fail;
	}
	info = "failed to unpack profile flags";
	if (!unpack_u32(e, &tmp, NULL))
		goto fail;
	if (tmp & PACKED_FLAG_HAT)
		profile->label.flags |= FLAG_HAT;
	if (!unpack_u32(e, &tmp, NULL))
		goto fail;
	if (tmp == PACKED_MODE_COMPLAIN || (e->version & FORCE_COMPLAIN_FLAG))
		profile->mode = APPARMOR_COMPLAIN;
	else if (tmp == PACKED_MODE_KILL)
		profile->mode = APPARMOR_KILL;
	else if (tmp == PACKED_MODE_UNCONFINED)
		profile->mode = APPARMOR_UNCONFINED;
	if (!unpack_u32(e, &tmp, NULL))
		goto fail;
	if (tmp)
		profile->audit = AUDIT_ALL;

	if (!unpack_nameX(e, AA_STRUCTEND, NULL))
		goto fail;

	/* path_flags is optional */
	if (unpack_u32(e, &profile->path_flags, "path_flags"))
		profile->path_flags |= profile->label.flags &
			PATH_MEDIATE_DELETED;
	else
		/* set a default value if path_flags field is not present */
		profile->path_flags = PATH_MEDIATE_DELETED;

	info = "failed to unpack profile capabilities";
	if (!unpack_u32(e, &(profile->caps.allow.cap[0]), NULL))
		goto fail;
	if (!unpack_u32(e, &(profile->caps.audit.cap[0]), NULL))
		goto fail;
	if (!unpack_u32(e, &(profile->caps.quiet.cap[0]), NULL))
		goto fail;
	if (!unpack_u32(e, &tmpcap.cap[0], NULL))
		goto fail;

	info = "failed to unpack upper profile capabilities";
	if (unpack_nameX(e, AA_STRUCT, "caps64")) {
		/* optional upper half of 64 bit caps */
		if (!unpack_u32(e, &(profile->caps.allow.cap[1]), NULL))
			goto fail;
		if (!unpack_u32(e, &(profile->caps.audit.cap[1]), NULL))
			goto fail;
		if (!unpack_u32(e, &(profile->caps.quiet.cap[1]), NULL))
			goto fail;
		if (!unpack_u32(e, &(tmpcap.cap[1]), NULL))
			goto fail;
		if (!unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	info = "failed to unpack extended profile capabilities";
	if (unpack_nameX(e, AA_STRUCT, "capsx")) {
		/* optional extended caps mediation mask */
		if (!unpack_u32(e, &(profile->caps.extended.cap[0]), NULL))
			goto fail;
		if (!unpack_u32(e, &(profile->caps.extended.cap[1]), NULL))
			goto fail;
		if (!unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	if (!unpack_xattrs(e, profile)) {
		info = "failed to unpack profile xattrs";
		goto fail;
	}

	if (!unpack_rlimits(e, profile)) {
		info = "failed to unpack profile rlimits";
		goto fail;
	}

	if (!unpack_secmark(e, profile)) {
		info = "failed to unpack profile secmark rules";
		goto fail;
	}

	if (unpack_nameX(e, AA_STRUCT, "policydb")) {
		/* generic policy dfa - optional and may be NULL */
		info = "failed to unpack policydb";
		profile->policy.dfa = unpack_dfa(e);
		if (IS_ERR(profile->policy.dfa)) {
			error = PTR_ERR(profile->policy.dfa);
			profile->policy.dfa = NULL;
			goto fail;
		} else if (!profile->policy.dfa) {
			error = -EPROTO;
			goto fail;
		}
		if (!unpack_u32(e, &profile->policy.start[0], "start"))
			/* default start state */
			profile->policy.start[0] = DFA_START;
		/* setup class index */
		for (i = AA_CLASS_FILE; i <= AA_CLASS_LAST; i++) {
			profile->policy.start[i] =
				aa_dfa_next(profile->policy.dfa,
					    profile->policy.start[0],
					    i);
		}
		if (!unpack_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	} else
		profile->policy.dfa = aa_get_dfa(nulldfa);

	/* get file rules */
	profile->file.dfa = unpack_dfa(e);
	if (IS_ERR(profile->file.dfa)) {
		error = PTR_ERR(profile->file.dfa);
		profile->file.dfa = NULL;
		info = "failed to unpack profile file rules";
		goto fail;
	} else if (profile->file.dfa) {
		if (!unpack_u32(e, &profile->file.start, "dfa_start"))
			/* default start state */
			profile->file.start = DFA_START;
	} else if (profile->policy.dfa &&
		   profile->policy.start[AA_CLASS_FILE]) {
		profile->file.dfa = aa_get_dfa(profile->policy.dfa);
		profile->file.start = profile->policy.start[AA_CLASS_FILE];
	} else
		profile->file.dfa = aa_get_dfa(nulldfa);

	if (!unpack_trans_table(e, profile)) {
		info = "failed to unpack profile transition table";
		goto fail;
	}

	if (unpack_nameX(e, AA_STRUCT, "data")) {
		info = "out of memory";
		profile->data = kzalloc(sizeof(*profile->data), GFP_KERNEL);
		if (!profile->data)
			goto fail;

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

		while (unpack_strdup(e, &key, NULL)) {
			data = kzalloc(sizeof(*data), GFP_KERNEL);
			if (!data) {
				kzfree(key);
				goto fail;
			}

			data->key = key;
			data->size = unpack_blob(e, &data->data, NULL);
			data->data = kvmemdup(data->data, data->size);
			if (data->size && !data->data) {
				kzfree(data->key);
				kzfree(data);
				goto fail;
			}

			rhashtable_insert_fast(profile->data, &data->head,
					       profile->data->p);
		}

		if (!unpack_nameX(e, AA_STRUCTEND, NULL)) {
			info = "failed to unpack end of key, value data table";
			goto fail;
		}
	}

	if (!unpack_nameX(e, AA_STRUCTEND, NULL)) {
		info = "failed to unpack end of profile";
		goto fail;
	}

	return profile;

fail:
	if (profile)
		name = NULL;
	else if (!name)
		name = "unknown";
	audit_iface(profile, NULL, name, info, e, error);
	aa_free_profile(profile);

	return ERR_PTR(error);
}

/**
 * verify_head - unpack serialized stream header
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
	if (!unpack_u32(e, &e->version, "version")) {
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
	if (VERSION_LT(e->version, v5) || VERSION_GT(e->version, v7)) {
		audit_iface(NULL, NULL, NULL, "unsupported interface version",
			    e, error);
		return error;
	}

	/* read the namespace if present */
	if (unpack_str(e, &name, "namespace")) {
		if (*name == '\0') {
			audit_iface(NULL, NULL, NULL, "invalid namespace name",
				    e, error);
			return error;
		}
		if (*ns && strcmp(*ns, name))
			audit_iface(NULL, NULL, NULL, "invalid ns change", e,
				    error);
		else if (!*ns)
			*ns = name;
	}

	return 0;
}

static bool verify_xindex(int xindex, int table_size)
{
	int index, xtype;
	xtype = xindex & AA_X_TYPE_MASK;
	index = xindex & AA_X_INDEX_MASK;
	if (xtype == AA_X_TABLE && index >= table_size)
		return 0;
	return 1;
}

/* verify dfa xindexes are in range of transition tables */
static bool verify_dfa_xindex(struct aa_dfa *dfa, int table_size)
{
	int i;
	for (i = 0; i < dfa->tables[YYTD_ID_ACCEPT]->td_lolen; i++) {
		if (!verify_xindex(dfa_user_xindex(dfa, i), table_size))
			return 0;
		if (!verify_xindex(dfa_other_xindex(dfa, i), table_size))
			return 0;
	}
	return 1;
}

/**
 * verify_profile - Do post unpack analysis to verify profile consistency
 * @profile: profile to verify (NOT NULL)
 *
 * Returns: 0 if passes verification else error
 */
static int verify_profile(struct aa_profile *profile)
{
	if (profile->file.dfa &&
	    !verify_dfa_xindex(profile->file.dfa,
			       profile->file.trans.size)) {
		audit_iface(profile, NULL, NULL, "Invalid named transition",
			    NULL, -EPROTO);
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
		kzfree(ent);
	}
}

struct aa_load_ent *aa_load_ent_alloc(void)
{
	struct aa_load_ent *ent = kzalloc(sizeof(*ent), GFP_KERNEL);
	if (ent)
		INIT_LIST_HEAD(&ent->list);
	return ent;
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
	int error;
	struct aa_ext e = {
		.start = udata->data,
		.end = udata->data + udata->size,
		.pos = udata->data,
	};

	*ns = NULL;
	while (e.pos < e.end) {
		char *ns_name = NULL;
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
	return 0;

fail_profile:
	aa_put_profile(profile);

fail:
	list_for_each_entry_safe(ent, tmp, lh, list) {
		list_del_init(&ent->list);
		aa_load_ent_free(ent);
	}

	return error;
}
