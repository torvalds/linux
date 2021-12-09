// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2007,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Contains the routines for mapping CIFS/NTFS ACLs
 *
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/keyctl.h>
#include <linux/key-type.h>
#include <keys/user-type.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsacl.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "fs_context.h"

/* security id for everyone/world system group */
static const struct cifs_sid sid_everyone = {
	1, 1, {0, 0, 0, 0, 0, 1}, {0} };
/* security id for Authenticated Users system group */
static const struct cifs_sid sid_authusers = {
	1, 1, {0, 0, 0, 0, 0, 5}, {cpu_to_le32(11)} };

/* S-1-22-1 Unmapped Unix users */
static const struct cifs_sid sid_unix_users = {1, 1, {0, 0, 0, 0, 0, 22},
		{cpu_to_le32(1), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };

/* S-1-22-2 Unmapped Unix groups */
static const struct cifs_sid sid_unix_groups = { 1, 1, {0, 0, 0, 0, 0, 22},
		{cpu_to_le32(2), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };

/*
 * See https://technet.microsoft.com/en-us/library/hh509017(v=ws.10).aspx
 */

/* S-1-5-88 MS NFS and Apple style UID/GID/mode */

/* S-1-5-88-1 Unix uid */
static const struct cifs_sid sid_unix_NFS_users = { 1, 2, {0, 0, 0, 0, 0, 5},
	{cpu_to_le32(88),
	 cpu_to_le32(1), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };

/* S-1-5-88-2 Unix gid */
static const struct cifs_sid sid_unix_NFS_groups = { 1, 2, {0, 0, 0, 0, 0, 5},
	{cpu_to_le32(88),
	 cpu_to_le32(2), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };

/* S-1-5-88-3 Unix mode */
static const struct cifs_sid sid_unix_NFS_mode = { 1, 2, {0, 0, 0, 0, 0, 5},
	{cpu_to_le32(88),
	 cpu_to_le32(3), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };

static const struct cred *root_cred;

static int
cifs_idmap_key_instantiate(struct key *key, struct key_preparsed_payload *prep)
{
	char *payload;

	/*
	 * If the payload is less than or equal to the size of a pointer, then
	 * an allocation here is wasteful. Just copy the data directly to the
	 * payload.value union member instead.
	 *
	 * With this however, you must check the datalen before trying to
	 * dereference payload.data!
	 */
	if (prep->datalen <= sizeof(key->payload)) {
		key->payload.data[0] = NULL;
		memcpy(&key->payload, prep->data, prep->datalen);
	} else {
		payload = kmemdup(prep->data, prep->datalen, GFP_KERNEL);
		if (!payload)
			return -ENOMEM;
		key->payload.data[0] = payload;
	}

	key->datalen = prep->datalen;
	return 0;
}

static inline void
cifs_idmap_key_destroy(struct key *key)
{
	if (key->datalen > sizeof(key->payload))
		kfree(key->payload.data[0]);
}

static struct key_type cifs_idmap_key_type = {
	.name        = "cifs.idmap",
	.instantiate = cifs_idmap_key_instantiate,
	.destroy     = cifs_idmap_key_destroy,
	.describe    = user_describe,
};

static char *
sid_to_key_str(struct cifs_sid *sidptr, unsigned int type)
{
	int i, len;
	unsigned int saval;
	char *sidstr, *strptr;
	unsigned long long id_auth_val;

	/* 3 bytes for prefix */
	sidstr = kmalloc(3 + SID_STRING_BASE_SIZE +
			 (SID_STRING_SUBAUTH_SIZE * sidptr->num_subauth),
			 GFP_KERNEL);
	if (!sidstr)
		return sidstr;

	strptr = sidstr;
	len = sprintf(strptr, "%cs:S-%hhu", type == SIDOWNER ? 'o' : 'g',
			sidptr->revision);
	strptr += len;

	/* The authority field is a single 48-bit number */
	id_auth_val = (unsigned long long)sidptr->authority[5];
	id_auth_val |= (unsigned long long)sidptr->authority[4] << 8;
	id_auth_val |= (unsigned long long)sidptr->authority[3] << 16;
	id_auth_val |= (unsigned long long)sidptr->authority[2] << 24;
	id_auth_val |= (unsigned long long)sidptr->authority[1] << 32;
	id_auth_val |= (unsigned long long)sidptr->authority[0] << 48;

	/*
	 * MS-DTYP states that if the authority is >= 2^32, then it should be
	 * expressed as a hex value.
	 */
	if (id_auth_val <= UINT_MAX)
		len = sprintf(strptr, "-%llu", id_auth_val);
	else
		len = sprintf(strptr, "-0x%llx", id_auth_val);

	strptr += len;

	for (i = 0; i < sidptr->num_subauth; ++i) {
		saval = le32_to_cpu(sidptr->sub_auth[i]);
		len = sprintf(strptr, "-%u", saval);
		strptr += len;
	}

	return sidstr;
}

/*
 * if the two SIDs (roughly equivalent to a UUID for a user or group) are
 * the same returns zero, if they do not match returns non-zero.
 */
static int
compare_sids(const struct cifs_sid *ctsid, const struct cifs_sid *cwsid)
{
	int i;
	int num_subauth, num_sat, num_saw;

	if ((!ctsid) || (!cwsid))
		return 1;

	/* compare the revision */
	if (ctsid->revision != cwsid->revision) {
		if (ctsid->revision > cwsid->revision)
			return 1;
		else
			return -1;
	}

	/* compare all of the six auth values */
	for (i = 0; i < NUM_AUTHS; ++i) {
		if (ctsid->authority[i] != cwsid->authority[i]) {
			if (ctsid->authority[i] > cwsid->authority[i])
				return 1;
			else
				return -1;
		}
	}

	/* compare all of the subauth values if any */
	num_sat = ctsid->num_subauth;
	num_saw = cwsid->num_subauth;
	num_subauth = num_sat < num_saw ? num_sat : num_saw;
	if (num_subauth) {
		for (i = 0; i < num_subauth; ++i) {
			if (ctsid->sub_auth[i] != cwsid->sub_auth[i]) {
				if (le32_to_cpu(ctsid->sub_auth[i]) >
					le32_to_cpu(cwsid->sub_auth[i]))
					return 1;
				else
					return -1;
			}
		}
	}

	return 0; /* sids compare/match */
}

static bool
is_well_known_sid(const struct cifs_sid *psid, uint32_t *puid, bool is_group)
{
	int i;
	int num_subauth;
	const struct cifs_sid *pwell_known_sid;

	if (!psid || (puid == NULL))
		return false;

	num_subauth = psid->num_subauth;

	/* check if Mac (or Windows NFS) vs. Samba format for Unix owner SID */
	if (num_subauth == 2) {
		if (is_group)
			pwell_known_sid = &sid_unix_groups;
		else
			pwell_known_sid = &sid_unix_users;
	} else if (num_subauth == 3) {
		if (is_group)
			pwell_known_sid = &sid_unix_NFS_groups;
		else
			pwell_known_sid = &sid_unix_NFS_users;
	} else
		return false;

	/* compare the revision */
	if (psid->revision != pwell_known_sid->revision)
		return false;

	/* compare all of the six auth values */
	for (i = 0; i < NUM_AUTHS; ++i) {
		if (psid->authority[i] != pwell_known_sid->authority[i]) {
			cifs_dbg(FYI, "auth %d did not match\n", i);
			return false;
		}
	}

	if (num_subauth == 2) {
		if (psid->sub_auth[0] != pwell_known_sid->sub_auth[0])
			return false;

		*puid = le32_to_cpu(psid->sub_auth[1]);
	} else /* 3 subauths, ie Windows/Mac style */ {
		*puid = le32_to_cpu(psid->sub_auth[0]);
		if ((psid->sub_auth[0] != pwell_known_sid->sub_auth[0]) ||
		    (psid->sub_auth[1] != pwell_known_sid->sub_auth[1]))
			return false;

		*puid = le32_to_cpu(psid->sub_auth[2]);
	}

	cifs_dbg(FYI, "Unix UID %d returned from SID\n", *puid);
	return true; /* well known sid found, uid returned */
}

static __u16
cifs_copy_sid(struct cifs_sid *dst, const struct cifs_sid *src)
{
	int i;
	__u16 size = 1 + 1 + 6;

	dst->revision = src->revision;
	dst->num_subauth = min_t(u8, src->num_subauth, SID_MAX_SUB_AUTHORITIES);
	for (i = 0; i < NUM_AUTHS; ++i)
		dst->authority[i] = src->authority[i];
	for (i = 0; i < dst->num_subauth; ++i)
		dst->sub_auth[i] = src->sub_auth[i];
	size += (dst->num_subauth * 4);

	return size;
}

static int
id_to_sid(unsigned int cid, uint sidtype, struct cifs_sid *ssid)
{
	int rc;
	struct key *sidkey;
	struct cifs_sid *ksid;
	unsigned int ksid_size;
	char desc[3 + 10 + 1]; /* 3 byte prefix + 10 bytes for value + NULL */
	const struct cred *saved_cred;

	rc = snprintf(desc, sizeof(desc), "%ci:%u",
			sidtype == SIDOWNER ? 'o' : 'g', cid);
	if (rc >= sizeof(desc))
		return -EINVAL;

	rc = 0;
	saved_cred = override_creds(root_cred);
	sidkey = request_key(&cifs_idmap_key_type, desc, "");
	if (IS_ERR(sidkey)) {
		rc = -EINVAL;
		cifs_dbg(FYI, "%s: Can't map %cid %u to a SID\n",
			 __func__, sidtype == SIDOWNER ? 'u' : 'g', cid);
		goto out_revert_creds;
	} else if (sidkey->datalen < CIFS_SID_BASE_SIZE) {
		rc = -EIO;
		cifs_dbg(FYI, "%s: Downcall contained malformed key (datalen=%hu)\n",
			 __func__, sidkey->datalen);
		goto invalidate_key;
	}

	/*
	 * A sid is usually too large to be embedded in payload.value, but if
	 * there are no subauthorities and the host has 8-byte pointers, then
	 * it could be.
	 */
	ksid = sidkey->datalen <= sizeof(sidkey->payload) ?
		(struct cifs_sid *)&sidkey->payload :
		(struct cifs_sid *)sidkey->payload.data[0];

	ksid_size = CIFS_SID_BASE_SIZE + (ksid->num_subauth * sizeof(__le32));
	if (ksid_size > sidkey->datalen) {
		rc = -EIO;
		cifs_dbg(FYI, "%s: Downcall contained malformed key (datalen=%hu, ksid_size=%u)\n",
			 __func__, sidkey->datalen, ksid_size);
		goto invalidate_key;
	}

	cifs_copy_sid(ssid, ksid);
out_key_put:
	key_put(sidkey);
out_revert_creds:
	revert_creds(saved_cred);
	return rc;

invalidate_key:
	key_invalidate(sidkey);
	goto out_key_put;
}

int
sid_to_id(struct cifs_sb_info *cifs_sb, struct cifs_sid *psid,
		struct cifs_fattr *fattr, uint sidtype)
{
	int rc = 0;
	struct key *sidkey;
	char *sidstr;
	const struct cred *saved_cred;
	kuid_t fuid = cifs_sb->ctx->linux_uid;
	kgid_t fgid = cifs_sb->ctx->linux_gid;

	/*
	 * If we have too many subauthorities, then something is really wrong.
	 * Just return an error.
	 */
	if (unlikely(psid->num_subauth > SID_MAX_SUB_AUTHORITIES)) {
		cifs_dbg(FYI, "%s: %u subauthorities is too many!\n",
			 __func__, psid->num_subauth);
		return -EIO;
	}

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UID_FROM_ACL) ||
	    (cifs_sb_master_tcon(cifs_sb)->posix_extensions)) {
		uint32_t unix_id;
		bool is_group;

		if (sidtype != SIDOWNER)
			is_group = true;
		else
			is_group = false;

		if (is_well_known_sid(psid, &unix_id, is_group) == false)
			goto try_upcall_to_get_id;

		if (is_group) {
			kgid_t gid;
			gid_t id;

			id = (gid_t)unix_id;
			gid = make_kgid(&init_user_ns, id);
			if (gid_valid(gid)) {
				fgid = gid;
				goto got_valid_id;
			}
		} else {
			kuid_t uid;
			uid_t id;

			id = (uid_t)unix_id;
			uid = make_kuid(&init_user_ns, id);
			if (uid_valid(uid)) {
				fuid = uid;
				goto got_valid_id;
			}
		}
		/* If unable to find uid/gid easily from SID try via upcall */
	}

try_upcall_to_get_id:
	sidstr = sid_to_key_str(psid, sidtype);
	if (!sidstr)
		return -ENOMEM;

	saved_cred = override_creds(root_cred);
	sidkey = request_key(&cifs_idmap_key_type, sidstr, "");
	if (IS_ERR(sidkey)) {
		cifs_dbg(FYI, "%s: Can't map SID %s to a %cid\n",
			 __func__, sidstr, sidtype == SIDOWNER ? 'u' : 'g');
		goto out_revert_creds;
	}

	/*
	 * FIXME: Here we assume that uid_t and gid_t are same size. It's
	 * probably a safe assumption but might be better to check based on
	 * sidtype.
	 */
	BUILD_BUG_ON(sizeof(uid_t) != sizeof(gid_t));
	if (sidkey->datalen != sizeof(uid_t)) {
		cifs_dbg(FYI, "%s: Downcall contained malformed key (datalen=%hu)\n",
			 __func__, sidkey->datalen);
		key_invalidate(sidkey);
		goto out_key_put;
	}

	if (sidtype == SIDOWNER) {
		kuid_t uid;
		uid_t id;
		memcpy(&id, &sidkey->payload.data[0], sizeof(uid_t));
		uid = make_kuid(&init_user_ns, id);
		if (uid_valid(uid))
			fuid = uid;
	} else {
		kgid_t gid;
		gid_t id;
		memcpy(&id, &sidkey->payload.data[0], sizeof(gid_t));
		gid = make_kgid(&init_user_ns, id);
		if (gid_valid(gid))
			fgid = gid;
	}

out_key_put:
	key_put(sidkey);
out_revert_creds:
	revert_creds(saved_cred);
	kfree(sidstr);

	/*
	 * Note that we return 0 here unconditionally. If the mapping
	 * fails then we just fall back to using the ctx->linux_uid/linux_gid.
	 */
got_valid_id:
	rc = 0;
	if (sidtype == SIDOWNER)
		fattr->cf_uid = fuid;
	else
		fattr->cf_gid = fgid;
	return rc;
}

int
init_cifs_idmap(void)
{
	struct cred *cred;
	struct key *keyring;
	int ret;

	cifs_dbg(FYI, "Registering the %s key type\n",
		 cifs_idmap_key_type.name);

	/* create an override credential set with a special thread keyring in
	 * which requests are cached
	 *
	 * this is used to prevent malicious redirections from being installed
	 * with add_key().
	 */
	cred = prepare_kernel_cred(NULL);
	if (!cred)
		return -ENOMEM;

	keyring = keyring_alloc(".cifs_idmap",
				GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, cred,
				(KEY_POS_ALL & ~KEY_POS_SETATTR) |
				KEY_USR_VIEW | KEY_USR_READ,
				KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto failed_put_cred;
	}

	ret = register_key_type(&cifs_idmap_key_type);
	if (ret < 0)
		goto failed_put_key;

	/* instruct request_key() to use this special keyring as a cache for
	 * the results it looks up */
	set_bit(KEY_FLAG_ROOT_CAN_CLEAR, &keyring->flags);
	cred->thread_keyring = keyring;
	cred->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
	root_cred = cred;

	cifs_dbg(FYI, "cifs idmap keyring: %d\n", key_serial(keyring));
	return 0;

failed_put_key:
	key_put(keyring);
failed_put_cred:
	put_cred(cred);
	return ret;
}

void
exit_cifs_idmap(void)
{
	key_revoke(root_cred->thread_keyring);
	unregister_key_type(&cifs_idmap_key_type);
	put_cred(root_cred);
	cifs_dbg(FYI, "Unregistered %s key type\n", cifs_idmap_key_type.name);
}

/* copy ntsd, owner sid, and group sid from a security descriptor to another */
static __u32 copy_sec_desc(const struct cifs_ntsd *pntsd,
				struct cifs_ntsd *pnntsd,
				__u32 sidsoffset,
				struct cifs_sid *pownersid,
				struct cifs_sid *pgrpsid)
{
	struct cifs_sid *owner_sid_ptr, *group_sid_ptr;
	struct cifs_sid *nowner_sid_ptr, *ngroup_sid_ptr;

	/* copy security descriptor control portion */
	pnntsd->revision = pntsd->revision;
	pnntsd->type = pntsd->type;
	pnntsd->dacloffset = cpu_to_le32(sizeof(struct cifs_ntsd));
	pnntsd->sacloffset = 0;
	pnntsd->osidoffset = cpu_to_le32(sidsoffset);
	pnntsd->gsidoffset = cpu_to_le32(sidsoffset + sizeof(struct cifs_sid));

	/* copy owner sid */
	if (pownersid)
		owner_sid_ptr = pownersid;
	else
		owner_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->osidoffset));
	nowner_sid_ptr = (struct cifs_sid *)((char *)pnntsd + sidsoffset);
	cifs_copy_sid(nowner_sid_ptr, owner_sid_ptr);

	/* copy group sid */
	if (pgrpsid)
		group_sid_ptr = pgrpsid;
	else
		group_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->gsidoffset));
	ngroup_sid_ptr = (struct cifs_sid *)((char *)pnntsd + sidsoffset +
					sizeof(struct cifs_sid));
	cifs_copy_sid(ngroup_sid_ptr, group_sid_ptr);

	return sidsoffset + (2 * sizeof(struct cifs_sid));
}


/*
   change posix mode to reflect permissions
   pmode is the existing mode (we only want to overwrite part of this
   bits to set can be: S_IRWXU, S_IRWXG or S_IRWXO ie 00700 or 00070 or 00007
*/
static void access_flags_to_mode(__le32 ace_flags, int type, umode_t *pmode,
				 umode_t *pdenied, umode_t mask)
{
	__u32 flags = le32_to_cpu(ace_flags);
	/*
	 * Do not assume "preferred" or "canonical" order.
	 * The first DENY or ALLOW ACE which matches perfectly is
	 * the permission to be used. Once allowed or denied, same
	 * permission in later ACEs do not matter.
	 */

	/* If not already allowed, deny these bits */
	if (type == ACCESS_DENIED) {
		if (flags & GENERIC_ALL &&
				!(*pmode & mask & 0777))
			*pdenied |= mask & 0777;

		if (((flags & GENERIC_WRITE) ||
				((flags & FILE_WRITE_RIGHTS) == FILE_WRITE_RIGHTS)) &&
				!(*pmode & mask & 0222))
			*pdenied |= mask & 0222;

		if (((flags & GENERIC_READ) ||
				((flags & FILE_READ_RIGHTS) == FILE_READ_RIGHTS)) &&
				!(*pmode & mask & 0444))
			*pdenied |= mask & 0444;

		if (((flags & GENERIC_EXECUTE) ||
				((flags & FILE_EXEC_RIGHTS) == FILE_EXEC_RIGHTS)) &&
				!(*pmode & mask & 0111))
			*pdenied |= mask & 0111;

		return;
	} else if (type != ACCESS_ALLOWED) {
		cifs_dbg(VFS, "unknown access control type %d\n", type);
		return;
	}
	/* else ACCESS_ALLOWED type */

	if ((flags & GENERIC_ALL) &&
			!(*pdenied & mask & 0777)) {
		*pmode |= mask & 0777;
		cifs_dbg(NOISY, "all perms\n");
		return;
	}

	if (((flags & GENERIC_WRITE) ||
			((flags & FILE_WRITE_RIGHTS) == FILE_WRITE_RIGHTS)) &&
			!(*pdenied & mask & 0222))
		*pmode |= mask & 0222;

	if (((flags & GENERIC_READ) ||
			((flags & FILE_READ_RIGHTS) == FILE_READ_RIGHTS)) &&
			!(*pdenied & mask & 0444))
		*pmode |= mask & 0444;

	if (((flags & GENERIC_EXECUTE) ||
			((flags & FILE_EXEC_RIGHTS) == FILE_EXEC_RIGHTS)) &&
			!(*pdenied & mask & 0111))
		*pmode |= mask & 0111;

	/* If DELETE_CHILD is set only on an owner ACE, set sticky bit */
	if (flags & FILE_DELETE_CHILD) {
		if (mask == ACL_OWNER_MASK) {
			if (!(*pdenied & 01000))
				*pmode |= 01000;
		} else if (!(*pdenied & 01000)) {
			*pmode &= ~01000;
			*pdenied |= 01000;
		}
	}

	cifs_dbg(NOISY, "access flags 0x%x mode now %04o\n", flags, *pmode);
	return;
}

/*
   Generate access flags to reflect permissions mode is the existing mode.
   This function is called for every ACE in the DACL whose SID matches
   with either owner or group or everyone.
*/

static void mode_to_access_flags(umode_t mode, umode_t bits_to_use,
				__u32 *pace_flags)
{
	/* reset access mask */
	*pace_flags = 0x0;

	/* bits to use are either S_IRWXU or S_IRWXG or S_IRWXO */
	mode &= bits_to_use;

	/* check for R/W/X UGO since we do not know whose flags
	   is this but we have cleared all the bits sans RWX for
	   either user or group or other as per bits_to_use */
	if (mode & S_IRUGO)
		*pace_flags |= SET_FILE_READ_RIGHTS;
	if (mode & S_IWUGO)
		*pace_flags |= SET_FILE_WRITE_RIGHTS;
	if (mode & S_IXUGO)
		*pace_flags |= SET_FILE_EXEC_RIGHTS;

	cifs_dbg(NOISY, "mode: %04o, access flags now 0x%x\n",
		 mode, *pace_flags);
	return;
}

static __u16 cifs_copy_ace(struct cifs_ace *dst, struct cifs_ace *src, struct cifs_sid *psid)
{
	__u16 size = 1 + 1 + 2 + 4;

	dst->type = src->type;
	dst->flags = src->flags;
	dst->access_req = src->access_req;

	/* Check if there's a replacement sid specified */
	if (psid)
		size += cifs_copy_sid(&dst->sid, psid);
	else
		size += cifs_copy_sid(&dst->sid, &src->sid);

	dst->size = cpu_to_le16(size);

	return size;
}

static __u16 fill_ace_for_sid(struct cifs_ace *pntace,
			const struct cifs_sid *psid, __u64 nmode,
			umode_t bits, __u8 access_type,
			bool allow_delete_child)
{
	int i;
	__u16 size = 0;
	__u32 access_req = 0;

	pntace->type = access_type;
	pntace->flags = 0x0;
	mode_to_access_flags(nmode, bits, &access_req);

	if (access_type == ACCESS_ALLOWED && allow_delete_child)
		access_req |= FILE_DELETE_CHILD;

	if (access_type == ACCESS_ALLOWED && !access_req)
		access_req = SET_MINIMUM_RIGHTS;
	else if (access_type == ACCESS_DENIED)
		access_req &= ~SET_MINIMUM_RIGHTS;

	pntace->access_req = cpu_to_le32(access_req);

	pntace->sid.revision = psid->revision;
	pntace->sid.num_subauth = psid->num_subauth;
	for (i = 0; i < NUM_AUTHS; i++)
		pntace->sid.authority[i] = psid->authority[i];
	for (i = 0; i < psid->num_subauth; i++)
		pntace->sid.sub_auth[i] = psid->sub_auth[i];

	size = 1 + 1 + 2 + 4 + 1 + 1 + 6 + (psid->num_subauth * 4);
	pntace->size = cpu_to_le16(size);

	return size;
}


#ifdef CONFIG_CIFS_DEBUG2
static void dump_ace(struct cifs_ace *pace, char *end_of_acl)
{
	int num_subauth;

	/* validate that we do not go past end of acl */

	if (le16_to_cpu(pace->size) < 16) {
		cifs_dbg(VFS, "ACE too small %d\n", le16_to_cpu(pace->size));
		return;
	}

	if (end_of_acl < (char *)pace + le16_to_cpu(pace->size)) {
		cifs_dbg(VFS, "ACL too small to parse ACE\n");
		return;
	}

	num_subauth = pace->sid.num_subauth;
	if (num_subauth) {
		int i;
		cifs_dbg(FYI, "ACE revision %d num_auth %d type %d flags %d size %d\n",
			 pace->sid.revision, pace->sid.num_subauth, pace->type,
			 pace->flags, le16_to_cpu(pace->size));
		for (i = 0; i < num_subauth; ++i) {
			cifs_dbg(FYI, "ACE sub_auth[%d]: 0x%x\n",
				 i, le32_to_cpu(pace->sid.sub_auth[i]));
		}

		/* BB add length check to make sure that we do not have huge
			num auths and therefore go off the end */
	}

	return;
}
#endif

static void parse_dacl(struct cifs_acl *pdacl, char *end_of_acl,
		       struct cifs_sid *pownersid, struct cifs_sid *pgrpsid,
		       struct cifs_fattr *fattr, bool mode_from_special_sid)
{
	int i;
	int num_aces = 0;
	int acl_size;
	char *acl_base;
	struct cifs_ace **ppace;

	/* BB need to add parm so we can store the SID BB */

	if (!pdacl) {
		/* no DACL in the security descriptor, set
		   all the permissions for user/group/other */
		fattr->cf_mode |= 0777;
		return;
	}

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)pdacl + le16_to_cpu(pdacl->size)) {
		cifs_dbg(VFS, "ACL too small to parse DACL\n");
		return;
	}

	cifs_dbg(NOISY, "DACL revision %d size %d num aces %d\n",
		 le16_to_cpu(pdacl->revision), le16_to_cpu(pdacl->size),
		 le32_to_cpu(pdacl->num_aces));

	/* reset rwx permissions for user/group/other.
	   Also, if num_aces is 0 i.e. DACL has no ACEs,
	   user/group/other have no permissions */
	fattr->cf_mode &= ~(0777);

	acl_base = (char *)pdacl;
	acl_size = sizeof(struct cifs_acl);

	num_aces = le32_to_cpu(pdacl->num_aces);
	if (num_aces > 0) {
		umode_t denied_mode = 0;

		if (num_aces > ULONG_MAX / sizeof(struct cifs_ace *))
			return;
		ppace = kmalloc_array(num_aces, sizeof(struct cifs_ace *),
				      GFP_KERNEL);
		if (!ppace)
			return;

		for (i = 0; i < num_aces; ++i) {
			ppace[i] = (struct cifs_ace *) (acl_base + acl_size);
#ifdef CONFIG_CIFS_DEBUG2
			dump_ace(ppace[i], end_of_acl);
#endif
			if (mode_from_special_sid &&
			    (compare_sids(&(ppace[i]->sid),
					  &sid_unix_NFS_mode) == 0)) {
				/*
				 * Full permissions are:
				 * 07777 = S_ISUID | S_ISGID | S_ISVTX |
				 *         S_IRWXU | S_IRWXG | S_IRWXO
				 */
				fattr->cf_mode &= ~07777;
				fattr->cf_mode |=
					le32_to_cpu(ppace[i]->sid.sub_auth[2]);
				break;
			} else {
				if (compare_sids(&(ppace[i]->sid), pownersid) == 0) {
					access_flags_to_mode(ppace[i]->access_req,
							ppace[i]->type,
							&fattr->cf_mode,
							&denied_mode,
							ACL_OWNER_MASK);
				} else if (compare_sids(&(ppace[i]->sid), pgrpsid) == 0) {
					access_flags_to_mode(ppace[i]->access_req,
							ppace[i]->type,
							&fattr->cf_mode,
							&denied_mode,
							ACL_GROUP_MASK);
				} else if ((compare_sids(&(ppace[i]->sid), &sid_everyone) == 0) ||
						(compare_sids(&(ppace[i]->sid), &sid_authusers) == 0)) {
					access_flags_to_mode(ppace[i]->access_req,
							ppace[i]->type,
							&fattr->cf_mode,
							&denied_mode,
							ACL_EVERYONE_MASK);
				}
			}


/*			memcpy((void *)(&(cifscred->aces[i])),
				(void *)ppace[i],
				sizeof(struct cifs_ace)); */

			acl_base = (char *)ppace[i];
			acl_size = le16_to_cpu(ppace[i]->size);
		}

		kfree(ppace);
	}

	return;
}

unsigned int setup_authusers_ACE(struct cifs_ace *pntace)
{
	int i;
	unsigned int ace_size = 20;

	pntace->type = ACCESS_ALLOWED_ACE_TYPE;
	pntace->flags = 0x0;
	pntace->access_req = cpu_to_le32(GENERIC_ALL);
	pntace->sid.num_subauth = 1;
	pntace->sid.revision = 1;
	for (i = 0; i < NUM_AUTHS; i++)
		pntace->sid.authority[i] =  sid_authusers.authority[i];

	pntace->sid.sub_auth[0] =  sid_authusers.sub_auth[0];

	/* size = 1 + 1 + 2 + 4 + 1 + 1 + 6 + (psid->num_subauth*4) */
	pntace->size = cpu_to_le16(ace_size);
	return ace_size;
}

/*
 * Fill in the special SID based on the mode. See
 * https://technet.microsoft.com/en-us/library/hh509017(v=ws.10).aspx
 */
unsigned int setup_special_mode_ACE(struct cifs_ace *pntace, __u64 nmode)
{
	int i;
	unsigned int ace_size = 28;

	pntace->type = ACCESS_DENIED_ACE_TYPE;
	pntace->flags = 0x0;
	pntace->access_req = 0;
	pntace->sid.num_subauth = 3;
	pntace->sid.revision = 1;
	for (i = 0; i < NUM_AUTHS; i++)
		pntace->sid.authority[i] = sid_unix_NFS_mode.authority[i];

	pntace->sid.sub_auth[0] = sid_unix_NFS_mode.sub_auth[0];
	pntace->sid.sub_auth[1] = sid_unix_NFS_mode.sub_auth[1];
	pntace->sid.sub_auth[2] = cpu_to_le32(nmode & 07777);

	/* size = 1 + 1 + 2 + 4 + 1 + 1 + 6 + (psid->num_subauth*4) */
	pntace->size = cpu_to_le16(ace_size);
	return ace_size;
}

unsigned int setup_special_user_owner_ACE(struct cifs_ace *pntace)
{
	int i;
	unsigned int ace_size = 28;

	pntace->type = ACCESS_ALLOWED_ACE_TYPE;
	pntace->flags = 0x0;
	pntace->access_req = cpu_to_le32(GENERIC_ALL);
	pntace->sid.num_subauth = 3;
	pntace->sid.revision = 1;
	for (i = 0; i < NUM_AUTHS; i++)
		pntace->sid.authority[i] = sid_unix_NFS_users.authority[i];

	pntace->sid.sub_auth[0] = sid_unix_NFS_users.sub_auth[0];
	pntace->sid.sub_auth[1] = sid_unix_NFS_users.sub_auth[1];
	pntace->sid.sub_auth[2] = cpu_to_le32(current_fsgid().val);

	/* size = 1 + 1 + 2 + 4 + 1 + 1 + 6 + (psid->num_subauth*4) */
	pntace->size = cpu_to_le16(ace_size);
	return ace_size;
}

static void populate_new_aces(char *nacl_base,
		struct cifs_sid *pownersid,
		struct cifs_sid *pgrpsid,
		__u64 *pnmode, u32 *pnum_aces, u16 *pnsize,
		bool modefromsid)
{
	__u64 nmode;
	u32 num_aces = 0;
	u16 nsize = 0;
	__u64 user_mode;
	__u64 group_mode;
	__u64 other_mode;
	__u64 deny_user_mode = 0;
	__u64 deny_group_mode = 0;
	bool sticky_set = false;
	struct cifs_ace *pnntace = NULL;

	nmode = *pnmode;
	num_aces = *pnum_aces;
	nsize = *pnsize;

	if (modefromsid) {
		pnntace = (struct cifs_ace *) (nacl_base + nsize);
		nsize += setup_special_mode_ACE(pnntace, nmode);
		num_aces++;
		goto set_size;
	}

	/*
	 * We'll try to keep the mode as requested by the user.
	 * But in cases where we cannot meaningfully convert that
	 * into ACL, return back the updated mode, so that it is
	 * updated in the inode.
	 */

	if (!memcmp(pownersid, pgrpsid, sizeof(struct cifs_sid))) {
		/*
		 * Case when owner and group SIDs are the same.
		 * Set the more restrictive of the two modes.
		 */
		user_mode = nmode & (nmode << 3) & 0700;
		group_mode = nmode & (nmode >> 3) & 0070;
	} else {
		user_mode = nmode & 0700;
		group_mode = nmode & 0070;
	}

	other_mode = nmode & 0007;

	/* We need DENY ACE when the perm is more restrictive than the next sets. */
	deny_user_mode = ~(user_mode) & ((group_mode << 3) | (other_mode << 6)) & 0700;
	deny_group_mode = ~(group_mode) & (other_mode << 3) & 0070;

	*pnmode = user_mode | group_mode | other_mode | (nmode & ~0777);

	/* This tells if we should allow delete child for group and everyone. */
	if (nmode & 01000)
		sticky_set = true;

	if (deny_user_mode) {
		pnntace = (struct cifs_ace *) (nacl_base + nsize);
		nsize += fill_ace_for_sid(pnntace, pownersid, deny_user_mode,
				0700, ACCESS_DENIED, false);
		num_aces++;
	}

	/* Group DENY ACE does not conflict with owner ALLOW ACE. Keep in preferred order*/
	if (deny_group_mode && !(deny_group_mode & (user_mode >> 3))) {
		pnntace = (struct cifs_ace *) (nacl_base + nsize);
		nsize += fill_ace_for_sid(pnntace, pgrpsid, deny_group_mode,
				0070, ACCESS_DENIED, false);
		num_aces++;
	}

	pnntace = (struct cifs_ace *) (nacl_base + nsize);
	nsize += fill_ace_for_sid(pnntace, pownersid, user_mode,
			0700, ACCESS_ALLOWED, true);
	num_aces++;

	/* Group DENY ACE conflicts with owner ALLOW ACE. So keep it after. */
	if (deny_group_mode && (deny_group_mode & (user_mode >> 3))) {
		pnntace = (struct cifs_ace *) (nacl_base + nsize);
		nsize += fill_ace_for_sid(pnntace, pgrpsid, deny_group_mode,
				0070, ACCESS_DENIED, false);
		num_aces++;
	}

	pnntace = (struct cifs_ace *) (nacl_base + nsize);
	nsize += fill_ace_for_sid(pnntace, pgrpsid, group_mode,
			0070, ACCESS_ALLOWED, !sticky_set);
	num_aces++;

	pnntace = (struct cifs_ace *) (nacl_base + nsize);
	nsize += fill_ace_for_sid(pnntace, &sid_everyone, other_mode,
			0007, ACCESS_ALLOWED, !sticky_set);
	num_aces++;

set_size:
	*pnum_aces = num_aces;
	*pnsize = nsize;
}

static __u16 replace_sids_and_copy_aces(struct cifs_acl *pdacl, struct cifs_acl *pndacl,
		struct cifs_sid *pownersid, struct cifs_sid *pgrpsid,
		struct cifs_sid *pnownersid, struct cifs_sid *pngrpsid)
{
	int i;
	u16 size = 0;
	struct cifs_ace *pntace = NULL;
	char *acl_base = NULL;
	u32 src_num_aces = 0;
	u16 nsize = 0;
	struct cifs_ace *pnntace = NULL;
	char *nacl_base = NULL;
	u16 ace_size = 0;

	acl_base = (char *)pdacl;
	size = sizeof(struct cifs_acl);
	src_num_aces = le32_to_cpu(pdacl->num_aces);

	nacl_base = (char *)pndacl;
	nsize = sizeof(struct cifs_acl);

	/* Go through all the ACEs */
	for (i = 0; i < src_num_aces; ++i) {
		pntace = (struct cifs_ace *) (acl_base + size);
		pnntace = (struct cifs_ace *) (nacl_base + nsize);

		if (pnownersid && compare_sids(&pntace->sid, pownersid) == 0)
			ace_size = cifs_copy_ace(pnntace, pntace, pnownersid);
		else if (pngrpsid && compare_sids(&pntace->sid, pgrpsid) == 0)
			ace_size = cifs_copy_ace(pnntace, pntace, pngrpsid);
		else
			ace_size = cifs_copy_ace(pnntace, pntace, NULL);

		size += le16_to_cpu(pntace->size);
		nsize += ace_size;
	}

	return nsize;
}

static int set_chmod_dacl(struct cifs_acl *pdacl, struct cifs_acl *pndacl,
		struct cifs_sid *pownersid,	struct cifs_sid *pgrpsid,
		__u64 *pnmode, bool mode_from_sid)
{
	int i;
	u16 size = 0;
	struct cifs_ace *pntace = NULL;
	char *acl_base = NULL;
	u32 src_num_aces = 0;
	u16 nsize = 0;
	struct cifs_ace *pnntace = NULL;
	char *nacl_base = NULL;
	u32 num_aces = 0;
	bool new_aces_set = false;

	/* Assuming that pndacl and pnmode are never NULL */
	nacl_base = (char *)pndacl;
	nsize = sizeof(struct cifs_acl);

	/* If pdacl is NULL, we don't have a src. Simply populate new ACL. */
	if (!pdacl) {
		populate_new_aces(nacl_base,
				pownersid, pgrpsid,
				pnmode, &num_aces, &nsize,
				mode_from_sid);
		goto finalize_dacl;
	}

	acl_base = (char *)pdacl;
	size = sizeof(struct cifs_acl);
	src_num_aces = le32_to_cpu(pdacl->num_aces);

	/* Retain old ACEs which we can retain */
	for (i = 0; i < src_num_aces; ++i) {
		pntace = (struct cifs_ace *) (acl_base + size);

		if (!new_aces_set && (pntace->flags & INHERITED_ACE)) {
			/* Place the new ACEs in between existing explicit and inherited */
			populate_new_aces(nacl_base,
					pownersid, pgrpsid,
					pnmode, &num_aces, &nsize,
					mode_from_sid);

			new_aces_set = true;
		}

		/* If it's any one of the ACE we're replacing, skip! */
		if (((compare_sids(&pntace->sid, &sid_unix_NFS_mode) == 0) ||
				(compare_sids(&pntace->sid, pownersid) == 0) ||
				(compare_sids(&pntace->sid, pgrpsid) == 0) ||
				(compare_sids(&pntace->sid, &sid_everyone) == 0) ||
				(compare_sids(&pntace->sid, &sid_authusers) == 0))) {
			goto next_ace;
		}

		/* update the pointer to the next ACE to populate*/
		pnntace = (struct cifs_ace *) (nacl_base + nsize);

		nsize += cifs_copy_ace(pnntace, pntace, NULL);
		num_aces++;

next_ace:
		size += le16_to_cpu(pntace->size);
	}

	/* If inherited ACEs are not present, place the new ones at the tail */
	if (!new_aces_set) {
		populate_new_aces(nacl_base,
				pownersid, pgrpsid,
				pnmode, &num_aces, &nsize,
				mode_from_sid);

		new_aces_set = true;
	}

finalize_dacl:
	pndacl->num_aces = cpu_to_le32(num_aces);
	pndacl->size = cpu_to_le16(nsize);

	return 0;
}

static int parse_sid(struct cifs_sid *psid, char *end_of_acl)
{
	/* BB need to add parm so we can store the SID BB */

	/* validate that we do not go past end of ACL - sid must be at least 8
	   bytes long (assuming no sub-auths - e.g. the null SID */
	if (end_of_acl < (char *)psid + 8) {
		cifs_dbg(VFS, "ACL too small to parse SID %p\n", psid);
		return -EINVAL;
	}

#ifdef CONFIG_CIFS_DEBUG2
	if (psid->num_subauth) {
		int i;
		cifs_dbg(FYI, "SID revision %d num_auth %d\n",
			 psid->revision, psid->num_subauth);

		for (i = 0; i < psid->num_subauth; i++) {
			cifs_dbg(FYI, "SID sub_auth[%d]: 0x%x\n",
				 i, le32_to_cpu(psid->sub_auth[i]));
		}

		/* BB add length check to make sure that we do not have huge
			num auths and therefore go off the end */
		cifs_dbg(FYI, "RID 0x%x\n",
			 le32_to_cpu(psid->sub_auth[psid->num_subauth-1]));
	}
#endif

	return 0;
}


/* Convert CIFS ACL to POSIX form */
static int parse_sec_desc(struct cifs_sb_info *cifs_sb,
		struct cifs_ntsd *pntsd, int acl_len, struct cifs_fattr *fattr,
		bool get_mode_from_special_sid)
{
	int rc = 0;
	struct cifs_sid *owner_sid_ptr, *group_sid_ptr;
	struct cifs_acl *dacl_ptr; /* no need for SACL ptr */
	char *end_of_acl = ((char *)pntsd) + acl_len;
	__u32 dacloffset;

	if (pntsd == NULL)
		return -EIO;

	owner_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->osidoffset));
	group_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->gsidoffset));
	dacloffset = le32_to_cpu(pntsd->dacloffset);
	dacl_ptr = (struct cifs_acl *)((char *)pntsd + dacloffset);
	cifs_dbg(NOISY, "revision %d type 0x%x ooffset 0x%x goffset 0x%x sacloffset 0x%x dacloffset 0x%x\n",
		 pntsd->revision, pntsd->type, le32_to_cpu(pntsd->osidoffset),
		 le32_to_cpu(pntsd->gsidoffset),
		 le32_to_cpu(pntsd->sacloffset), dacloffset);
/*	cifs_dump_mem("owner_sid: ", owner_sid_ptr, 64); */
	rc = parse_sid(owner_sid_ptr, end_of_acl);
	if (rc) {
		cifs_dbg(FYI, "%s: Error %d parsing Owner SID\n", __func__, rc);
		return rc;
	}
	rc = sid_to_id(cifs_sb, owner_sid_ptr, fattr, SIDOWNER);
	if (rc) {
		cifs_dbg(FYI, "%s: Error %d mapping Owner SID to uid\n",
			 __func__, rc);
		return rc;
	}

	rc = parse_sid(group_sid_ptr, end_of_acl);
	if (rc) {
		cifs_dbg(FYI, "%s: Error %d mapping Owner SID to gid\n",
			 __func__, rc);
		return rc;
	}
	rc = sid_to_id(cifs_sb, group_sid_ptr, fattr, SIDGROUP);
	if (rc) {
		cifs_dbg(FYI, "%s: Error %d mapping Group SID to gid\n",
			 __func__, rc);
		return rc;
	}

	if (dacloffset)
		parse_dacl(dacl_ptr, end_of_acl, owner_sid_ptr,
			   group_sid_ptr, fattr, get_mode_from_special_sid);
	else
		cifs_dbg(FYI, "no ACL\n"); /* BB grant all or default perms? */

	return rc;
}

/* Convert permission bits from mode to equivalent CIFS ACL */
static int build_sec_desc(struct cifs_ntsd *pntsd, struct cifs_ntsd *pnntsd,
	__u32 secdesclen, __u32 *pnsecdesclen, __u64 *pnmode, kuid_t uid, kgid_t gid,
	bool mode_from_sid, bool id_from_sid, int *aclflag)
{
	int rc = 0;
	__u32 dacloffset;
	__u32 ndacloffset;
	__u32 sidsoffset;
	struct cifs_sid *owner_sid_ptr, *group_sid_ptr;
	struct cifs_sid *nowner_sid_ptr = NULL, *ngroup_sid_ptr = NULL;
	struct cifs_acl *dacl_ptr = NULL;  /* no need for SACL ptr */
	struct cifs_acl *ndacl_ptr = NULL; /* no need for SACL ptr */
	char *end_of_acl = ((char *)pntsd) + secdesclen;
	u16 size = 0;

	dacloffset = le32_to_cpu(pntsd->dacloffset);
	if (dacloffset) {
		dacl_ptr = (struct cifs_acl *)((char *)pntsd + dacloffset);
		if (end_of_acl < (char *)dacl_ptr + le16_to_cpu(dacl_ptr->size)) {
			cifs_dbg(VFS, "Server returned illegal ACL size\n");
			return -EINVAL;
		}
	}

	owner_sid_ptr = (struct cifs_sid *)((char *)pntsd +
			le32_to_cpu(pntsd->osidoffset));
	group_sid_ptr = (struct cifs_sid *)((char *)pntsd +
			le32_to_cpu(pntsd->gsidoffset));

	if (pnmode && *pnmode != NO_CHANGE_64) { /* chmod */
		ndacloffset = sizeof(struct cifs_ntsd);
		ndacl_ptr = (struct cifs_acl *)((char *)pnntsd + ndacloffset);
		ndacl_ptr->revision =
			dacloffset ? dacl_ptr->revision : cpu_to_le16(ACL_REVISION);

		ndacl_ptr->size = cpu_to_le16(0);
		ndacl_ptr->num_aces = cpu_to_le32(0);

		rc = set_chmod_dacl(dacl_ptr, ndacl_ptr, owner_sid_ptr, group_sid_ptr,
				    pnmode, mode_from_sid);

		sidsoffset = ndacloffset + le16_to_cpu(ndacl_ptr->size);
		/* copy the non-dacl portion of secdesc */
		*pnsecdesclen = copy_sec_desc(pntsd, pnntsd, sidsoffset,
				NULL, NULL);

		*aclflag |= CIFS_ACL_DACL;
	} else {
		ndacloffset = sizeof(struct cifs_ntsd);
		ndacl_ptr = (struct cifs_acl *)((char *)pnntsd + ndacloffset);
		ndacl_ptr->revision =
			dacloffset ? dacl_ptr->revision : cpu_to_le16(ACL_REVISION);
		ndacl_ptr->num_aces = dacl_ptr ? dacl_ptr->num_aces : 0;

		if (uid_valid(uid)) { /* chown */
			uid_t id;
			nowner_sid_ptr = kmalloc(sizeof(struct cifs_sid),
								GFP_KERNEL);
			if (!nowner_sid_ptr) {
				rc = -ENOMEM;
				goto chown_chgrp_exit;
			}
			id = from_kuid(&init_user_ns, uid);
			if (id_from_sid) {
				struct owner_sid *osid = (struct owner_sid *)nowner_sid_ptr;
				/* Populate the user ownership fields S-1-5-88-1 */
				osid->Revision = 1;
				osid->NumAuth = 3;
				osid->Authority[5] = 5;
				osid->SubAuthorities[0] = cpu_to_le32(88);
				osid->SubAuthorities[1] = cpu_to_le32(1);
				osid->SubAuthorities[2] = cpu_to_le32(id);

			} else { /* lookup sid with upcall */
				rc = id_to_sid(id, SIDOWNER, nowner_sid_ptr);
				if (rc) {
					cifs_dbg(FYI, "%s: Mapping error %d for owner id %d\n",
						 __func__, rc, id);
					goto chown_chgrp_exit;
				}
			}
			*aclflag |= CIFS_ACL_OWNER;
		}
		if (gid_valid(gid)) { /* chgrp */
			gid_t id;
			ngroup_sid_ptr = kmalloc(sizeof(struct cifs_sid),
								GFP_KERNEL);
			if (!ngroup_sid_ptr) {
				rc = -ENOMEM;
				goto chown_chgrp_exit;
			}
			id = from_kgid(&init_user_ns, gid);
			if (id_from_sid) {
				struct owner_sid *gsid = (struct owner_sid *)ngroup_sid_ptr;
				/* Populate the group ownership fields S-1-5-88-2 */
				gsid->Revision = 1;
				gsid->NumAuth = 3;
				gsid->Authority[5] = 5;
				gsid->SubAuthorities[0] = cpu_to_le32(88);
				gsid->SubAuthorities[1] = cpu_to_le32(2);
				gsid->SubAuthorities[2] = cpu_to_le32(id);

			} else { /* lookup sid with upcall */
				rc = id_to_sid(id, SIDGROUP, ngroup_sid_ptr);
				if (rc) {
					cifs_dbg(FYI, "%s: Mapping error %d for group id %d\n",
						 __func__, rc, id);
					goto chown_chgrp_exit;
				}
			}
			*aclflag |= CIFS_ACL_GROUP;
		}

		if (dacloffset) {
			/* Replace ACEs for old owner with new one */
			size = replace_sids_and_copy_aces(dacl_ptr, ndacl_ptr,
					owner_sid_ptr, group_sid_ptr,
					nowner_sid_ptr, ngroup_sid_ptr);
			ndacl_ptr->size = cpu_to_le16(size);
		}

		sidsoffset = ndacloffset + le16_to_cpu(ndacl_ptr->size);
		/* copy the non-dacl portion of secdesc */
		*pnsecdesclen = copy_sec_desc(pntsd, pnntsd, sidsoffset,
				nowner_sid_ptr, ngroup_sid_ptr);

chown_chgrp_exit:
		/* errors could jump here. So make sure we return soon after this */
		kfree(nowner_sid_ptr);
		kfree(ngroup_sid_ptr);
	}

	return rc;
}

struct cifs_ntsd *get_cifs_acl_by_fid(struct cifs_sb_info *cifs_sb,
				      const struct cifs_fid *cifsfid, u32 *pacllen,
				      u32 __maybe_unused unused)
{
	struct cifs_ntsd *pntsd = NULL;
	unsigned int xid;
	int rc;
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);

	if (IS_ERR(tlink))
		return ERR_CAST(tlink);

	xid = get_xid();
	rc = CIFSSMBGetCIFSACL(xid, tlink_tcon(tlink), cifsfid->netfid, &pntsd,
				pacllen);
	free_xid(xid);

	cifs_put_tlink(tlink);

	cifs_dbg(FYI, "%s: rc = %d ACL len %d\n", __func__, rc, *pacllen);
	if (rc)
		return ERR_PTR(rc);
	return pntsd;
}

static struct cifs_ntsd *get_cifs_acl_by_path(struct cifs_sb_info *cifs_sb,
		const char *path, u32 *pacllen)
{
	struct cifs_ntsd *pntsd = NULL;
	int oplock = 0;
	unsigned int xid;
	int rc;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);
	struct cifs_fid fid;
	struct cifs_open_parms oparms;

	if (IS_ERR(tlink))
		return ERR_CAST(tlink);

	tcon = tlink_tcon(tlink);
	xid = get_xid();

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	oparms.desired_access = READ_CONTROL;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.disposition = FILE_OPEN;
	oparms.path = path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = CIFS_open(xid, &oparms, &oplock, NULL);
	if (!rc) {
		rc = CIFSSMBGetCIFSACL(xid, tcon, fid.netfid, &pntsd, pacllen);
		CIFSSMBClose(xid, tcon, fid.netfid);
	}

	cifs_put_tlink(tlink);
	free_xid(xid);

	cifs_dbg(FYI, "%s: rc = %d ACL len %d\n", __func__, rc, *pacllen);
	if (rc)
		return ERR_PTR(rc);
	return pntsd;
}

/* Retrieve an ACL from the server */
struct cifs_ntsd *get_cifs_acl(struct cifs_sb_info *cifs_sb,
				      struct inode *inode, const char *path,
			       u32 *pacllen, u32 info)
{
	struct cifs_ntsd *pntsd = NULL;
	struct cifsFileInfo *open_file = NULL;

	if (inode)
		open_file = find_readable_file(CIFS_I(inode), true);
	if (!open_file)
		return get_cifs_acl_by_path(cifs_sb, path, pacllen);

	pntsd = get_cifs_acl_by_fid(cifs_sb, &open_file->fid, pacllen, info);
	cifsFileInfo_put(open_file);
	return pntsd;
}

 /* Set an ACL on the server */
int set_cifs_acl(struct cifs_ntsd *pnntsd, __u32 acllen,
			struct inode *inode, const char *path, int aclflag)
{
	int oplock = 0;
	unsigned int xid;
	int rc, access_flags;
	struct cifs_tcon *tcon;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);
	struct cifs_fid fid;
	struct cifs_open_parms oparms;

	if (IS_ERR(tlink))
		return PTR_ERR(tlink);

	tcon = tlink_tcon(tlink);
	xid = get_xid();

	if (aclflag == CIFS_ACL_OWNER || aclflag == CIFS_ACL_GROUP)
		access_flags = WRITE_OWNER;
	else
		access_flags = WRITE_DAC;

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	oparms.desired_access = access_flags;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.disposition = FILE_OPEN;
	oparms.path = path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = CIFS_open(xid, &oparms, &oplock, NULL);
	if (rc) {
		cifs_dbg(VFS, "Unable to open file to set ACL\n");
		goto out;
	}

	rc = CIFSSMBSetCIFSACL(xid, tcon, fid.netfid, pnntsd, acllen, aclflag);
	cifs_dbg(NOISY, "SetCIFSACL rc = %d\n", rc);

	CIFSSMBClose(xid, tcon, fid.netfid);
out:
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

/* Translate the CIFS ACL (similar to NTFS ACL) for a file into mode bits */
int
cifs_acl_to_fattr(struct cifs_sb_info *cifs_sb, struct cifs_fattr *fattr,
		  struct inode *inode, bool mode_from_special_sid,
		  const char *path, const struct cifs_fid *pfid)
{
	struct cifs_ntsd *pntsd = NULL;
	u32 acllen = 0;
	int rc = 0;
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);
	struct smb_version_operations *ops;
	const u32 info = 0;

	cifs_dbg(NOISY, "converting ACL to mode for %s\n", path);

	if (IS_ERR(tlink))
		return PTR_ERR(tlink);

	ops = tlink_tcon(tlink)->ses->server->ops;

	if (pfid && (ops->get_acl_by_fid))
		pntsd = ops->get_acl_by_fid(cifs_sb, pfid, &acllen, info);
	else if (ops->get_acl)
		pntsd = ops->get_acl(cifs_sb, inode, path, &acllen, info);
	else {
		cifs_put_tlink(tlink);
		return -EOPNOTSUPP;
	}
	/* if we can retrieve the ACL, now parse Access Control Entries, ACEs */
	if (IS_ERR(pntsd)) {
		rc = PTR_ERR(pntsd);
		cifs_dbg(VFS, "%s: error %d getting sec desc\n", __func__, rc);
	} else if (mode_from_special_sid) {
		rc = parse_sec_desc(cifs_sb, pntsd, acllen, fattr, true);
		kfree(pntsd);
	} else {
		/* get approximated mode from ACL */
		rc = parse_sec_desc(cifs_sb, pntsd, acllen, fattr, false);
		kfree(pntsd);
		if (rc)
			cifs_dbg(VFS, "parse sec desc failed rc = %d\n", rc);
	}

	cifs_put_tlink(tlink);

	return rc;
}

/* Convert mode bits to an ACL so we can update the ACL on the server */
int
id_mode_to_cifs_acl(struct inode *inode, const char *path, __u64 *pnmode,
			kuid_t uid, kgid_t gid)
{
	int rc = 0;
	int aclflag = CIFS_ACL_DACL; /* default flag to set */
	__u32 secdesclen = 0;
	__u32 nsecdesclen = 0;
	__u32 dacloffset = 0;
	struct cifs_acl *dacl_ptr = NULL;
	struct cifs_ntsd *pntsd = NULL; /* acl obtained from server */
	struct cifs_ntsd *pnntsd = NULL; /* modified acl to be sent to server */
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);
	struct smb_version_operations *ops;
	bool mode_from_sid, id_from_sid;
	const u32 info = 0;

	if (IS_ERR(tlink))
		return PTR_ERR(tlink);

	ops = tlink_tcon(tlink)->ses->server->ops;

	cifs_dbg(NOISY, "set ACL from mode for %s\n", path);

	/* Get the security descriptor */

	if (ops->get_acl == NULL) {
		cifs_put_tlink(tlink);
		return -EOPNOTSUPP;
	}

	pntsd = ops->get_acl(cifs_sb, inode, path, &secdesclen, info);
	if (IS_ERR(pntsd)) {
		rc = PTR_ERR(pntsd);
		cifs_dbg(VFS, "%s: error %d getting sec desc\n", __func__, rc);
		cifs_put_tlink(tlink);
		return rc;
	}

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MODE_FROM_SID)
		mode_from_sid = true;
	else
		mode_from_sid = false;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UID_FROM_ACL)
		id_from_sid = true;
	else
		id_from_sid = false;

	/* Potentially, five new ACEs can be added to the ACL for U,G,O mapping */
	nsecdesclen = secdesclen;
	if (pnmode && *pnmode != NO_CHANGE_64) { /* chmod */
		if (mode_from_sid)
			nsecdesclen += sizeof(struct cifs_ace);
		else /* cifsacl */
			nsecdesclen += 5 * sizeof(struct cifs_ace);
	} else { /* chown */
		/* When ownership changes, changes new owner sid length could be different */
		nsecdesclen = sizeof(struct cifs_ntsd) + (sizeof(struct cifs_sid) * 2);
		dacloffset = le32_to_cpu(pntsd->dacloffset);
		if (dacloffset) {
			dacl_ptr = (struct cifs_acl *)((char *)pntsd + dacloffset);
			if (mode_from_sid)
				nsecdesclen +=
					le32_to_cpu(dacl_ptr->num_aces) * sizeof(struct cifs_ace);
			else /* cifsacl */
				nsecdesclen += le16_to_cpu(dacl_ptr->size);
		}
	}

	/*
	 * Add three ACEs for owner, group, everyone getting rid of other ACEs
	 * as chmod disables ACEs and set the security descriptor. Allocate
	 * memory for the smb header, set security descriptor request security
	 * descriptor parameters, and security descriptor itself
	 */
	nsecdesclen = max_t(u32, nsecdesclen, DEFAULT_SEC_DESC_LEN);
	pnntsd = kmalloc(nsecdesclen, GFP_KERNEL);
	if (!pnntsd) {
		kfree(pntsd);
		cifs_put_tlink(tlink);
		return -ENOMEM;
	}

	rc = build_sec_desc(pntsd, pnntsd, secdesclen, &nsecdesclen, pnmode, uid, gid,
			    mode_from_sid, id_from_sid, &aclflag);

	cifs_dbg(NOISY, "build_sec_desc rc: %d\n", rc);

	if (ops->set_acl == NULL)
		rc = -EOPNOTSUPP;

	if (!rc) {
		/* Set the security descriptor */
		rc = ops->set_acl(pnntsd, nsecdesclen, inode, path, aclflag);
		cifs_dbg(NOISY, "set_cifs_acl rc: %d\n", rc);
	}
	cifs_put_tlink(tlink);

	kfree(pnntsd);
	kfree(pntsd);
	return rc;
}
