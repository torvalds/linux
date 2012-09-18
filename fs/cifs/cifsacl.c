/*
 *   fs/cifs/cifsacl.c
 *
 *   Copyright (C) International Business Machines  Corp., 2007,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Contains the routines for mapping CIFS/NTFS ACLs
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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

/* security id for everyone/world system group */
static const struct cifs_sid sid_everyone = {
	1, 1, {0, 0, 0, 0, 0, 1}, {0} };
/* security id for Authenticated Users system group */
static const struct cifs_sid sid_authusers = {
	1, 1, {0, 0, 0, 0, 0, 5}, {__constant_cpu_to_le32(11)} };
/* group users */
static const struct cifs_sid sid_user = {1, 2 , {0, 0, 0, 0, 0, 5}, {} };

const struct cred *root_cred;

static void
shrink_idmap_tree(struct rb_root *root, int nr_to_scan, int *nr_rem,
			int *nr_del)
{
	struct rb_node *node;
	struct rb_node *tmp;
	struct cifs_sid_id *psidid;

	node = rb_first(root);
	while (node) {
		tmp = node;
		node = rb_next(tmp);
		psidid = rb_entry(tmp, struct cifs_sid_id, rbnode);
		if (nr_to_scan == 0 || *nr_del == nr_to_scan)
			++(*nr_rem);
		else {
			if (time_after(jiffies, psidid->time + SID_MAP_EXPIRE)
						&& psidid->refcount == 0) {
				rb_erase(tmp, root);
				++(*nr_del);
			} else
				++(*nr_rem);
		}
	}
}

/*
 * Run idmap cache shrinker.
 */
static int
cifs_idmap_shrinker(struct shrinker *shrink, struct shrink_control *sc)
{
	int nr_to_scan = sc->nr_to_scan;
	int nr_del = 0;
	int nr_rem = 0;
	struct rb_root *root;

	root = &uidtree;
	spin_lock(&siduidlock);
	shrink_idmap_tree(root, nr_to_scan, &nr_rem, &nr_del);
	spin_unlock(&siduidlock);

	root = &gidtree;
	spin_lock(&sidgidlock);
	shrink_idmap_tree(root, nr_to_scan, &nr_rem, &nr_del);
	spin_unlock(&sidgidlock);

	root = &siduidtree;
	spin_lock(&uidsidlock);
	shrink_idmap_tree(root, nr_to_scan, &nr_rem, &nr_del);
	spin_unlock(&uidsidlock);

	root = &sidgidtree;
	spin_lock(&gidsidlock);
	shrink_idmap_tree(root, nr_to_scan, &nr_rem, &nr_del);
	spin_unlock(&gidsidlock);

	return nr_rem;
}

static void
sid_rb_insert(struct rb_root *root, unsigned long cid,
		struct cifs_sid_id **psidid, char *typestr)
{
	char *strptr;
	struct rb_node *node = root->rb_node;
	struct rb_node *parent = NULL;
	struct rb_node **linkto = &(root->rb_node);
	struct cifs_sid_id *lsidid;

	while (node) {
		lsidid = rb_entry(node, struct cifs_sid_id, rbnode);
		parent = node;
		if (cid > lsidid->id) {
			linkto = &(node->rb_left);
			node = node->rb_left;
		}
		if (cid < lsidid->id) {
			linkto = &(node->rb_right);
			node = node->rb_right;
		}
	}

	(*psidid)->id = cid;
	(*psidid)->time = jiffies - (SID_MAP_RETRY + 1);
	(*psidid)->refcount = 0;

	sprintf((*psidid)->sidstr, "%s", typestr);
	strptr = (*psidid)->sidstr + strlen((*psidid)->sidstr);
	sprintf(strptr, "%ld", cid);

	clear_bit(SID_ID_PENDING, &(*psidid)->state);
	clear_bit(SID_ID_MAPPED, &(*psidid)->state);

	rb_link_node(&(*psidid)->rbnode, parent, linkto);
	rb_insert_color(&(*psidid)->rbnode, root);
}

static struct cifs_sid_id *
sid_rb_search(struct rb_root *root, unsigned long cid)
{
	struct rb_node *node = root->rb_node;
	struct cifs_sid_id *lsidid;

	while (node) {
		lsidid = rb_entry(node, struct cifs_sid_id, rbnode);
		if (cid > lsidid->id)
			node = node->rb_left;
		else if (cid < lsidid->id)
			node = node->rb_right;
		else /* node found */
			return lsidid;
	}

	return NULL;
}

static struct shrinker cifs_shrinker = {
	.shrink = cifs_idmap_shrinker,
	.seeks = DEFAULT_SEEKS,
};

static int
cifs_idmap_key_instantiate(struct key *key, const void *data, size_t datalen)
{
	char *payload;

	payload = kmalloc(datalen, GFP_KERNEL);
	if (!payload)
		return -ENOMEM;

	memcpy(payload, data, datalen);
	key->payload.data = payload;
	key->datalen = datalen;
	return 0;
}

static inline void
cifs_idmap_key_destroy(struct key *key)
{
	kfree(key->payload.data);
}

struct key_type cifs_idmap_key_type = {
	.name        = "cifs.idmap",
	.instantiate = cifs_idmap_key_instantiate,
	.destroy     = cifs_idmap_key_destroy,
	.describe    = user_describe,
	.match       = user_match,
};

static void
sid_to_str(struct cifs_sid *sidptr, char *sidstr)
{
	int i;
	unsigned long saval;
	char *strptr;

	strptr = sidstr;

	sprintf(strptr, "%s", "S");
	strptr = sidstr + strlen(sidstr);

	sprintf(strptr, "-%d", sidptr->revision);
	strptr = sidstr + strlen(sidstr);

	for (i = 0; i < 6; ++i) {
		if (sidptr->authority[i]) {
			sprintf(strptr, "-%d", sidptr->authority[i]);
			strptr = sidstr + strlen(sidstr);
		}
	}

	for (i = 0; i < sidptr->num_subauth; ++i) {
		saval = le32_to_cpu(sidptr->sub_auth[i]);
		sprintf(strptr, "-%ld", saval);
		strptr = sidstr + strlen(sidstr);
	}
}

static void
id_rb_insert(struct rb_root *root, struct cifs_sid *sidptr,
		struct cifs_sid_id **psidid, char *typestr)
{
	int rc;
	char *strptr;
	struct rb_node *node = root->rb_node;
	struct rb_node *parent = NULL;
	struct rb_node **linkto = &(root->rb_node);
	struct cifs_sid_id *lsidid;

	while (node) {
		lsidid = rb_entry(node, struct cifs_sid_id, rbnode);
		parent = node;
		rc = compare_sids(sidptr, &((lsidid)->sid));
		if (rc > 0) {
			linkto = &(node->rb_left);
			node = node->rb_left;
		} else if (rc < 0) {
			linkto = &(node->rb_right);
			node = node->rb_right;
		}
	}

	memcpy(&(*psidid)->sid, sidptr, sizeof(struct cifs_sid));
	(*psidid)->time = jiffies - (SID_MAP_RETRY + 1);
	(*psidid)->refcount = 0;

	sprintf((*psidid)->sidstr, "%s", typestr);
	strptr = (*psidid)->sidstr + strlen((*psidid)->sidstr);
	sid_to_str(&(*psidid)->sid, strptr);

	clear_bit(SID_ID_PENDING, &(*psidid)->state);
	clear_bit(SID_ID_MAPPED, &(*psidid)->state);

	rb_link_node(&(*psidid)->rbnode, parent, linkto);
	rb_insert_color(&(*psidid)->rbnode, root);
}

static struct cifs_sid_id *
id_rb_search(struct rb_root *root, struct cifs_sid *sidptr)
{
	int rc;
	struct rb_node *node = root->rb_node;
	struct cifs_sid_id *lsidid;

	while (node) {
		lsidid = rb_entry(node, struct cifs_sid_id, rbnode);
		rc = compare_sids(sidptr, &((lsidid)->sid));
		if (rc > 0) {
			node = node->rb_left;
		} else if (rc < 0) {
			node = node->rb_right;
		} else /* node found */
			return lsidid;
	}

	return NULL;
}

static int
sidid_pending_wait(void *unused)
{
	schedule();
	return signal_pending(current) ? -ERESTARTSYS : 0;
}

static int
id_to_sid(unsigned long cid, uint sidtype, struct cifs_sid *ssid)
{
	int rc = 0;
	struct key *sidkey;
	const struct cred *saved_cred;
	struct cifs_sid *lsid;
	struct cifs_sid_id *psidid, *npsidid;
	struct rb_root *cidtree;
	spinlock_t *cidlock;

	if (sidtype == SIDOWNER) {
		cidlock = &siduidlock;
		cidtree = &uidtree;
	} else if (sidtype == SIDGROUP) {
		cidlock = &sidgidlock;
		cidtree = &gidtree;
	} else
		return -EINVAL;

	spin_lock(cidlock);
	psidid = sid_rb_search(cidtree, cid);

	if (!psidid) { /* node does not exist, allocate one & attempt adding */
		spin_unlock(cidlock);
		npsidid = kzalloc(sizeof(struct cifs_sid_id), GFP_KERNEL);
		if (!npsidid)
			return -ENOMEM;

		npsidid->sidstr = kmalloc(SIDLEN, GFP_KERNEL);
		if (!npsidid->sidstr) {
			kfree(npsidid);
			return -ENOMEM;
		}

		spin_lock(cidlock);
		psidid = sid_rb_search(cidtree, cid);
		if (psidid) { /* node happened to get inserted meanwhile */
			++psidid->refcount;
			spin_unlock(cidlock);
			kfree(npsidid->sidstr);
			kfree(npsidid);
		} else {
			psidid = npsidid;
			sid_rb_insert(cidtree, cid, &psidid,
					sidtype == SIDOWNER ? "oi:" : "gi:");
			++psidid->refcount;
			spin_unlock(cidlock);
		}
	} else {
		++psidid->refcount;
		spin_unlock(cidlock);
	}

	/*
	 * If we are here, it is safe to access psidid and its fields
	 * since a reference was taken earlier while holding the spinlock.
	 * A reference on the node is put without holding the spinlock
	 * and it is OK to do so in this case, shrinker will not erase
	 * this node until all references are put and we do not access
	 * any fields of the node after a reference is put .
	 */
	if (test_bit(SID_ID_MAPPED, &psidid->state)) {
		memcpy(ssid, &psidid->sid, sizeof(struct cifs_sid));
		psidid->time = jiffies; /* update ts for accessing */
		goto id_sid_out;
	}

	if (time_after(psidid->time + SID_MAP_RETRY, jiffies)) {
		rc = -EINVAL;
		goto id_sid_out;
	}

	if (!test_and_set_bit(SID_ID_PENDING, &psidid->state)) {
		saved_cred = override_creds(root_cred);
		sidkey = request_key(&cifs_idmap_key_type, psidid->sidstr, "");
		if (IS_ERR(sidkey)) {
			rc = -EINVAL;
			cFYI(1, "%s: Can't map and id to a SID", __func__);
		} else {
			lsid = (struct cifs_sid *)sidkey->payload.data;
			memcpy(&psidid->sid, lsid,
				sidkey->datalen < sizeof(struct cifs_sid) ?
				sidkey->datalen : sizeof(struct cifs_sid));
			memcpy(ssid, &psidid->sid,
				sidkey->datalen < sizeof(struct cifs_sid) ?
				sidkey->datalen : sizeof(struct cifs_sid));
			set_bit(SID_ID_MAPPED, &psidid->state);
			key_put(sidkey);
			kfree(psidid->sidstr);
		}
		psidid->time = jiffies; /* update ts for accessing */
		revert_creds(saved_cred);
		clear_bit(SID_ID_PENDING, &psidid->state);
		wake_up_bit(&psidid->state, SID_ID_PENDING);
	} else {
		rc = wait_on_bit(&psidid->state, SID_ID_PENDING,
				sidid_pending_wait, TASK_INTERRUPTIBLE);
		if (rc) {
			cFYI(1, "%s: sidid_pending_wait interrupted %d",
					__func__, rc);
			--psidid->refcount;
			return rc;
		}
		if (test_bit(SID_ID_MAPPED, &psidid->state))
			memcpy(ssid, &psidid->sid, sizeof(struct cifs_sid));
		else
			rc = -EINVAL;
	}
id_sid_out:
	--psidid->refcount;
	return rc;
}

static int
sid_to_id(struct cifs_sb_info *cifs_sb, struct cifs_sid *psid,
		struct cifs_fattr *fattr, uint sidtype)
{
	int rc;
	unsigned long cid;
	struct key *idkey;
	const struct cred *saved_cred;
	struct cifs_sid_id *psidid, *npsidid;
	struct rb_root *cidtree;
	spinlock_t *cidlock;

	if (sidtype == SIDOWNER) {
		cid = cifs_sb->mnt_uid; /* default uid, in case upcall fails */
		cidlock = &siduidlock;
		cidtree = &uidtree;
	} else if (sidtype == SIDGROUP) {
		cid = cifs_sb->mnt_gid; /* default gid, in case upcall fails */
		cidlock = &sidgidlock;
		cidtree = &gidtree;
	} else
		return -ENOENT;

	spin_lock(cidlock);
	psidid = id_rb_search(cidtree, psid);

	if (!psidid) { /* node does not exist, allocate one & attempt adding */
		spin_unlock(cidlock);
		npsidid = kzalloc(sizeof(struct cifs_sid_id), GFP_KERNEL);
		if (!npsidid)
			return -ENOMEM;

		npsidid->sidstr = kmalloc(SIDLEN, GFP_KERNEL);
		if (!npsidid->sidstr) {
			kfree(npsidid);
			return -ENOMEM;
		}

		spin_lock(cidlock);
		psidid = id_rb_search(cidtree, psid);
		if (psidid) { /* node happened to get inserted meanwhile */
			++psidid->refcount;
			spin_unlock(cidlock);
			kfree(npsidid->sidstr);
			kfree(npsidid);
		} else {
			psidid = npsidid;
			id_rb_insert(cidtree, psid, &psidid,
					sidtype == SIDOWNER ? "os:" : "gs:");
			++psidid->refcount;
			spin_unlock(cidlock);
		}
	} else {
		++psidid->refcount;
		spin_unlock(cidlock);
	}

	/*
	 * If we are here, it is safe to access psidid and its fields
	 * since a reference was taken earlier while holding the spinlock.
	 * A reference on the node is put without holding the spinlock
	 * and it is OK to do so in this case, shrinker will not erase
	 * this node until all references are put and we do not access
	 * any fields of the node after a reference is put .
	 */
	if (test_bit(SID_ID_MAPPED, &psidid->state)) {
		cid = psidid->id;
		psidid->time = jiffies; /* update ts for accessing */
		goto sid_to_id_out;
	}

	if (time_after(psidid->time + SID_MAP_RETRY, jiffies))
		goto sid_to_id_out;

	if (!test_and_set_bit(SID_ID_PENDING, &psidid->state)) {
		saved_cred = override_creds(root_cred);
		idkey = request_key(&cifs_idmap_key_type, psidid->sidstr, "");
		if (IS_ERR(idkey))
			cFYI(1, "%s: Can't map SID to an id", __func__);
		else {
			cid = *(unsigned long *)idkey->payload.value;
			psidid->id = cid;
			set_bit(SID_ID_MAPPED, &psidid->state);
			key_put(idkey);
			kfree(psidid->sidstr);
		}
		revert_creds(saved_cred);
		psidid->time = jiffies; /* update ts for accessing */
		clear_bit(SID_ID_PENDING, &psidid->state);
		wake_up_bit(&psidid->state, SID_ID_PENDING);
	} else {
		rc = wait_on_bit(&psidid->state, SID_ID_PENDING,
				sidid_pending_wait, TASK_INTERRUPTIBLE);
		if (rc) {
			cFYI(1, "%s: sidid_pending_wait interrupted %d",
					__func__, rc);
			--psidid->refcount; /* decremented without spinlock */
			return rc;
		}
		if (test_bit(SID_ID_MAPPED, &psidid->state))
			cid = psidid->id;
	}

sid_to_id_out:
	--psidid->refcount; /* decremented without spinlock */
	if (sidtype == SIDOWNER)
		fattr->cf_uid = cid;
	else
		fattr->cf_gid = cid;

	return 0;
}

int
init_cifs_idmap(void)
{
	struct cred *cred;
	struct key *keyring;
	int ret;

	cFYI(1, "Registering the %s key type", cifs_idmap_key_type.name);

	/* create an override credential set with a special thread keyring in
	 * which requests are cached
	 *
	 * this is used to prevent malicious redirections from being installed
	 * with add_key().
	 */
	cred = prepare_kernel_cred(NULL);
	if (!cred)
		return -ENOMEM;

	keyring = key_alloc(&key_type_keyring, ".cifs_idmap", 0, 0, cred,
			    (KEY_POS_ALL & ~KEY_POS_SETATTR) |
			    KEY_USR_VIEW | KEY_USR_READ,
			    KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto failed_put_cred;
	}

	ret = key_instantiate_and_link(keyring, NULL, 0, NULL, NULL);
	if (ret < 0)
		goto failed_put_key;

	ret = register_key_type(&cifs_idmap_key_type);
	if (ret < 0)
		goto failed_put_key;

	/* instruct request_key() to use this special keyring as a cache for
	 * the results it looks up */
	set_bit(KEY_FLAG_ROOT_CAN_CLEAR, &keyring->flags);
	cred->thread_keyring = keyring;
	cred->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
	root_cred = cred;

	spin_lock_init(&siduidlock);
	uidtree = RB_ROOT;
	spin_lock_init(&sidgidlock);
	gidtree = RB_ROOT;

	spin_lock_init(&uidsidlock);
	siduidtree = RB_ROOT;
	spin_lock_init(&gidsidlock);
	sidgidtree = RB_ROOT;
	register_shrinker(&cifs_shrinker);

	cFYI(1, "cifs idmap keyring: %d", key_serial(keyring));
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
	unregister_shrinker(&cifs_shrinker);
	cFYI(1, "Unregistered %s key type", cifs_idmap_key_type.name);
}

void
cifs_destroy_idmaptrees(void)
{
	struct rb_root *root;
	struct rb_node *node;

	root = &uidtree;
	spin_lock(&siduidlock);
	while ((node = rb_first(root)))
		rb_erase(node, root);
	spin_unlock(&siduidlock);

	root = &gidtree;
	spin_lock(&sidgidlock);
	while ((node = rb_first(root)))
		rb_erase(node, root);
	spin_unlock(&sidgidlock);

	root = &siduidtree;
	spin_lock(&uidsidlock);
	while ((node = rb_first(root)))
		rb_erase(node, root);
	spin_unlock(&uidsidlock);

	root = &sidgidtree;
	spin_lock(&gidsidlock);
	while ((node = rb_first(root)))
		rb_erase(node, root);
	spin_unlock(&gidsidlock);
}

/* if the two SIDs (roughly equivalent to a UUID for a user or group) are
   the same returns 1, if they do not match returns 0 */
int compare_sids(const struct cifs_sid *ctsid, const struct cifs_sid *cwsid)
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
	for (i = 0; i < 6; ++i) {
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


/* copy ntsd, owner sid, and group sid from a security descriptor to another */
static void copy_sec_desc(const struct cifs_ntsd *pntsd,
				struct cifs_ntsd *pnntsd, __u32 sidsoffset)
{
	int i;

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
	owner_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->osidoffset));
	nowner_sid_ptr = (struct cifs_sid *)((char *)pnntsd + sidsoffset);

	nowner_sid_ptr->revision = owner_sid_ptr->revision;
	nowner_sid_ptr->num_subauth = owner_sid_ptr->num_subauth;
	for (i = 0; i < 6; i++)
		nowner_sid_ptr->authority[i] = owner_sid_ptr->authority[i];
	for (i = 0; i < 5; i++)
		nowner_sid_ptr->sub_auth[i] = owner_sid_ptr->sub_auth[i];

	/* copy group sid */
	group_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->gsidoffset));
	ngroup_sid_ptr = (struct cifs_sid *)((char *)pnntsd + sidsoffset +
					sizeof(struct cifs_sid));

	ngroup_sid_ptr->revision = group_sid_ptr->revision;
	ngroup_sid_ptr->num_subauth = group_sid_ptr->num_subauth;
	for (i = 0; i < 6; i++)
		ngroup_sid_ptr->authority[i] = group_sid_ptr->authority[i];
	for (i = 0; i < 5; i++)
		ngroup_sid_ptr->sub_auth[i] = group_sid_ptr->sub_auth[i];

	return;
}


/*
   change posix mode to reflect permissions
   pmode is the existing mode (we only want to overwrite part of this
   bits to set can be: S_IRWXU, S_IRWXG or S_IRWXO ie 00700 or 00070 or 00007
*/
static void access_flags_to_mode(__le32 ace_flags, int type, umode_t *pmode,
				 umode_t *pbits_to_set)
{
	__u32 flags = le32_to_cpu(ace_flags);
	/* the order of ACEs is important.  The canonical order is to begin with
	   DENY entries followed by ALLOW, otherwise an allow entry could be
	   encountered first, making the subsequent deny entry like "dead code"
	   which would be superflous since Windows stops when a match is made
	   for the operation you are trying to perform for your user */

	/* For deny ACEs we change the mask so that subsequent allow access
	   control entries do not turn on the bits we are denying */
	if (type == ACCESS_DENIED) {
		if (flags & GENERIC_ALL)
			*pbits_to_set &= ~S_IRWXUGO;

		if ((flags & GENERIC_WRITE) ||
			((flags & FILE_WRITE_RIGHTS) == FILE_WRITE_RIGHTS))
			*pbits_to_set &= ~S_IWUGO;
		if ((flags & GENERIC_READ) ||
			((flags & FILE_READ_RIGHTS) == FILE_READ_RIGHTS))
			*pbits_to_set &= ~S_IRUGO;
		if ((flags & GENERIC_EXECUTE) ||
			((flags & FILE_EXEC_RIGHTS) == FILE_EXEC_RIGHTS))
			*pbits_to_set &= ~S_IXUGO;
		return;
	} else if (type != ACCESS_ALLOWED) {
		cERROR(1, "unknown access control type %d", type);
		return;
	}
	/* else ACCESS_ALLOWED type */

	if (flags & GENERIC_ALL) {
		*pmode |= (S_IRWXUGO & (*pbits_to_set));
		cFYI(DBG2, "all perms");
		return;
	}
	if ((flags & GENERIC_WRITE) ||
			((flags & FILE_WRITE_RIGHTS) == FILE_WRITE_RIGHTS))
		*pmode |= (S_IWUGO & (*pbits_to_set));
	if ((flags & GENERIC_READ) ||
			((flags & FILE_READ_RIGHTS) == FILE_READ_RIGHTS))
		*pmode |= (S_IRUGO & (*pbits_to_set));
	if ((flags & GENERIC_EXECUTE) ||
			((flags & FILE_EXEC_RIGHTS) == FILE_EXEC_RIGHTS))
		*pmode |= (S_IXUGO & (*pbits_to_set));

	cFYI(DBG2, "access flags 0x%x mode now 0x%x", flags, *pmode);
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

	cFYI(DBG2, "mode: 0x%x, access flags now 0x%x", mode, *pace_flags);
	return;
}

static __u16 fill_ace_for_sid(struct cifs_ace *pntace,
			const struct cifs_sid *psid, __u64 nmode, umode_t bits)
{
	int i;
	__u16 size = 0;
	__u32 access_req = 0;

	pntace->type = ACCESS_ALLOWED;
	pntace->flags = 0x0;
	mode_to_access_flags(nmode, bits, &access_req);
	if (!access_req)
		access_req = SET_MINIMUM_RIGHTS;
	pntace->access_req = cpu_to_le32(access_req);

	pntace->sid.revision = psid->revision;
	pntace->sid.num_subauth = psid->num_subauth;
	for (i = 0; i < 6; i++)
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
		cERROR(1, "ACE too small %d", le16_to_cpu(pace->size));
		return;
	}

	if (end_of_acl < (char *)pace + le16_to_cpu(pace->size)) {
		cERROR(1, "ACL too small to parse ACE");
		return;
	}

	num_subauth = pace->sid.num_subauth;
	if (num_subauth) {
		int i;
		cFYI(1, "ACE revision %d num_auth %d type %d flags %d size %d",
			pace->sid.revision, pace->sid.num_subauth, pace->type,
			pace->flags, le16_to_cpu(pace->size));
		for (i = 0; i < num_subauth; ++i) {
			cFYI(1, "ACE sub_auth[%d]: 0x%x", i,
				le32_to_cpu(pace->sid.sub_auth[i]));
		}

		/* BB add length check to make sure that we do not have huge
			num auths and therefore go off the end */
	}

	return;
}
#endif


static void parse_dacl(struct cifs_acl *pdacl, char *end_of_acl,
		       struct cifs_sid *pownersid, struct cifs_sid *pgrpsid,
		       struct cifs_fattr *fattr)
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
		fattr->cf_mode |= S_IRWXUGO;
		return;
	}

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)pdacl + le16_to_cpu(pdacl->size)) {
		cERROR(1, "ACL too small to parse DACL");
		return;
	}

	cFYI(DBG2, "DACL revision %d size %d num aces %d",
		le16_to_cpu(pdacl->revision), le16_to_cpu(pdacl->size),
		le32_to_cpu(pdacl->num_aces));

	/* reset rwx permissions for user/group/other.
	   Also, if num_aces is 0 i.e. DACL has no ACEs,
	   user/group/other have no permissions */
	fattr->cf_mode &= ~(S_IRWXUGO);

	acl_base = (char *)pdacl;
	acl_size = sizeof(struct cifs_acl);

	num_aces = le32_to_cpu(pdacl->num_aces);
	if (num_aces > 0) {
		umode_t user_mask = S_IRWXU;
		umode_t group_mask = S_IRWXG;
		umode_t other_mask = S_IRWXU | S_IRWXG | S_IRWXO;

		if (num_aces > ULONG_MAX / sizeof(struct cifs_ace *))
			return;
		ppace = kmalloc(num_aces * sizeof(struct cifs_ace *),
				GFP_KERNEL);
		if (!ppace) {
			cERROR(1, "DACL memory allocation error");
			return;
		}

		for (i = 0; i < num_aces; ++i) {
			ppace[i] = (struct cifs_ace *) (acl_base + acl_size);
#ifdef CONFIG_CIFS_DEBUG2
			dump_ace(ppace[i], end_of_acl);
#endif
			if (compare_sids(&(ppace[i]->sid), pownersid) == 0)
				access_flags_to_mode(ppace[i]->access_req,
						     ppace[i]->type,
						     &fattr->cf_mode,
						     &user_mask);
			if (compare_sids(&(ppace[i]->sid), pgrpsid) == 0)
				access_flags_to_mode(ppace[i]->access_req,
						     ppace[i]->type,
						     &fattr->cf_mode,
						     &group_mask);
			if (compare_sids(&(ppace[i]->sid), &sid_everyone) == 0)
				access_flags_to_mode(ppace[i]->access_req,
						     ppace[i]->type,
						     &fattr->cf_mode,
						     &other_mask);
			if (compare_sids(&(ppace[i]->sid), &sid_authusers) == 0)
				access_flags_to_mode(ppace[i]->access_req,
						     ppace[i]->type,
						     &fattr->cf_mode,
						     &other_mask);


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


static int set_chmod_dacl(struct cifs_acl *pndacl, struct cifs_sid *pownersid,
			struct cifs_sid *pgrpsid, __u64 nmode)
{
	u16 size = 0;
	struct cifs_acl *pnndacl;

	pnndacl = (struct cifs_acl *)((char *)pndacl + sizeof(struct cifs_acl));

	size += fill_ace_for_sid((struct cifs_ace *) ((char *)pnndacl + size),
					pownersid, nmode, S_IRWXU);
	size += fill_ace_for_sid((struct cifs_ace *)((char *)pnndacl + size),
					pgrpsid, nmode, S_IRWXG);
	size += fill_ace_for_sid((struct cifs_ace *)((char *)pnndacl + size),
					 &sid_everyone, nmode, S_IRWXO);

	pndacl->size = cpu_to_le16(size + sizeof(struct cifs_acl));
	pndacl->num_aces = cpu_to_le32(3);

	return 0;
}


static int parse_sid(struct cifs_sid *psid, char *end_of_acl)
{
	/* BB need to add parm so we can store the SID BB */

	/* validate that we do not go past end of ACL - sid must be at least 8
	   bytes long (assuming no sub-auths - e.g. the null SID */
	if (end_of_acl < (char *)psid + 8) {
		cERROR(1, "ACL too small to parse SID %p", psid);
		return -EINVAL;
	}

	if (psid->num_subauth) {
#ifdef CONFIG_CIFS_DEBUG2
		int i;
		cFYI(1, "SID revision %d num_auth %d",
			psid->revision, psid->num_subauth);

		for (i = 0; i < psid->num_subauth; i++) {
			cFYI(1, "SID sub_auth[%d]: 0x%x ", i,
				le32_to_cpu(psid->sub_auth[i]));
		}

		/* BB add length check to make sure that we do not have huge
			num auths and therefore go off the end */
		cFYI(1, "RID 0x%x",
			le32_to_cpu(psid->sub_auth[psid->num_subauth-1]));
#endif
	}

	return 0;
}


/* Convert CIFS ACL to POSIX form */
static int parse_sec_desc(struct cifs_sb_info *cifs_sb,
		struct cifs_ntsd *pntsd, int acl_len, struct cifs_fattr *fattr)
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
	cFYI(DBG2, "revision %d type 0x%x ooffset 0x%x goffset 0x%x "
		 "sacloffset 0x%x dacloffset 0x%x",
		 pntsd->revision, pntsd->type, le32_to_cpu(pntsd->osidoffset),
		 le32_to_cpu(pntsd->gsidoffset),
		 le32_to_cpu(pntsd->sacloffset), dacloffset);
/*	cifs_dump_mem("owner_sid: ", owner_sid_ptr, 64); */
	rc = parse_sid(owner_sid_ptr, end_of_acl);
	if (rc) {
		cFYI(1, "%s: Error %d parsing Owner SID", __func__, rc);
		return rc;
	}
	rc = sid_to_id(cifs_sb, owner_sid_ptr, fattr, SIDOWNER);
	if (rc) {
		cFYI(1, "%s: Error %d mapping Owner SID to uid", __func__, rc);
		return rc;
	}

	rc = parse_sid(group_sid_ptr, end_of_acl);
	if (rc) {
		cFYI(1, "%s: Error %d mapping Owner SID to gid", __func__, rc);
		return rc;
	}
	rc = sid_to_id(cifs_sb, group_sid_ptr, fattr, SIDGROUP);
	if (rc) {
		cFYI(1, "%s: Error %d mapping Group SID to gid", __func__, rc);
		return rc;
	}

	if (dacloffset)
		parse_dacl(dacl_ptr, end_of_acl, owner_sid_ptr,
			   group_sid_ptr, fattr);
	else
		cFYI(1, "no ACL"); /* BB grant all or default perms? */

	return rc;
}

/* Convert permission bits from mode to equivalent CIFS ACL */
static int build_sec_desc(struct cifs_ntsd *pntsd, struct cifs_ntsd *pnntsd,
	__u32 secdesclen, __u64 nmode, uid_t uid, gid_t gid, int *aclflag)
{
	int rc = 0;
	__u32 dacloffset;
	__u32 ndacloffset;
	__u32 sidsoffset;
	struct cifs_sid *owner_sid_ptr, *group_sid_ptr;
	struct cifs_sid *nowner_sid_ptr, *ngroup_sid_ptr;
	struct cifs_acl *dacl_ptr = NULL;  /* no need for SACL ptr */
	struct cifs_acl *ndacl_ptr = NULL; /* no need for SACL ptr */

	if (nmode != NO_CHANGE_64) { /* chmod */
		owner_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->osidoffset));
		group_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->gsidoffset));
		dacloffset = le32_to_cpu(pntsd->dacloffset);
		dacl_ptr = (struct cifs_acl *)((char *)pntsd + dacloffset);
		ndacloffset = sizeof(struct cifs_ntsd);
		ndacl_ptr = (struct cifs_acl *)((char *)pnntsd + ndacloffset);
		ndacl_ptr->revision = dacl_ptr->revision;
		ndacl_ptr->size = 0;
		ndacl_ptr->num_aces = 0;

		rc = set_chmod_dacl(ndacl_ptr, owner_sid_ptr, group_sid_ptr,
					nmode);
		sidsoffset = ndacloffset + le16_to_cpu(ndacl_ptr->size);
		/* copy sec desc control portion & owner and group sids */
		copy_sec_desc(pntsd, pnntsd, sidsoffset);
		*aclflag = CIFS_ACL_DACL;
	} else {
		memcpy(pnntsd, pntsd, secdesclen);
		if (uid != NO_CHANGE_32) { /* chown */
			owner_sid_ptr = (struct cifs_sid *)((char *)pnntsd +
					le32_to_cpu(pnntsd->osidoffset));
			nowner_sid_ptr = kmalloc(sizeof(struct cifs_sid),
								GFP_KERNEL);
			if (!nowner_sid_ptr)
				return -ENOMEM;
			rc = id_to_sid(uid, SIDOWNER, nowner_sid_ptr);
			if (rc) {
				cFYI(1, "%s: Mapping error %d for owner id %d",
						__func__, rc, uid);
				kfree(nowner_sid_ptr);
				return rc;
			}
			memcpy(owner_sid_ptr, nowner_sid_ptr,
					sizeof(struct cifs_sid));
			kfree(nowner_sid_ptr);
			*aclflag = CIFS_ACL_OWNER;
		}
		if (gid != NO_CHANGE_32) { /* chgrp */
			group_sid_ptr = (struct cifs_sid *)((char *)pnntsd +
					le32_to_cpu(pnntsd->gsidoffset));
			ngroup_sid_ptr = kmalloc(sizeof(struct cifs_sid),
								GFP_KERNEL);
			if (!ngroup_sid_ptr)
				return -ENOMEM;
			rc = id_to_sid(gid, SIDGROUP, ngroup_sid_ptr);
			if (rc) {
				cFYI(1, "%s: Mapping error %d for group id %d",
						__func__, rc, gid);
				kfree(ngroup_sid_ptr);
				return rc;
			}
			memcpy(group_sid_ptr, ngroup_sid_ptr,
					sizeof(struct cifs_sid));
			kfree(ngroup_sid_ptr);
			*aclflag = CIFS_ACL_GROUP;
		}
	}

	return rc;
}

static struct cifs_ntsd *get_cifs_acl_by_fid(struct cifs_sb_info *cifs_sb,
		__u16 fid, u32 *pacllen)
{
	struct cifs_ntsd *pntsd = NULL;
	unsigned int xid;
	int rc;
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);

	if (IS_ERR(tlink))
		return ERR_CAST(tlink);

	xid = get_xid();
	rc = CIFSSMBGetCIFSACL(xid, tlink_tcon(tlink), fid, &pntsd, pacllen);
	free_xid(xid);

	cifs_put_tlink(tlink);

	cFYI(1, "%s: rc = %d ACL len %d", __func__, rc, *pacllen);
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
	int rc, create_options = 0;
	__u16 fid;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);

	if (IS_ERR(tlink))
		return ERR_CAST(tlink);

	tcon = tlink_tcon(tlink);
	xid = get_xid();

	if (backup_cred(cifs_sb))
		create_options |= CREATE_OPEN_BACKUP_INTENT;

	rc = CIFSSMBOpen(xid, tcon, path, FILE_OPEN, READ_CONTROL,
			create_options, &fid, &oplock, NULL, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (!rc) {
		rc = CIFSSMBGetCIFSACL(xid, tcon, fid, &pntsd, pacllen);
		CIFSSMBClose(xid, tcon, fid);
	}

	cifs_put_tlink(tlink);
	free_xid(xid);

	cFYI(1, "%s: rc = %d ACL len %d", __func__, rc, *pacllen);
	if (rc)
		return ERR_PTR(rc);
	return pntsd;
}

/* Retrieve an ACL from the server */
struct cifs_ntsd *get_cifs_acl(struct cifs_sb_info *cifs_sb,
				      struct inode *inode, const char *path,
				      u32 *pacllen)
{
	struct cifs_ntsd *pntsd = NULL;
	struct cifsFileInfo *open_file = NULL;

	if (inode)
		open_file = find_readable_file(CIFS_I(inode), true);
	if (!open_file)
		return get_cifs_acl_by_path(cifs_sb, path, pacllen);

	pntsd = get_cifs_acl_by_fid(cifs_sb, open_file->fid.netfid, pacllen);
	cifsFileInfo_put(open_file);
	return pntsd;
}

 /* Set an ACL on the server */
int set_cifs_acl(struct cifs_ntsd *pnntsd, __u32 acllen,
			struct inode *inode, const char *path, int aclflag)
{
	int oplock = 0;
	unsigned int xid;
	int rc, access_flags, create_options = 0;
	__u16 fid;
	struct cifs_tcon *tcon;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);

	if (IS_ERR(tlink))
		return PTR_ERR(tlink);

	tcon = tlink_tcon(tlink);
	xid = get_xid();

	if (backup_cred(cifs_sb))
		create_options |= CREATE_OPEN_BACKUP_INTENT;

	if (aclflag == CIFS_ACL_OWNER || aclflag == CIFS_ACL_GROUP)
		access_flags = WRITE_OWNER;
	else
		access_flags = WRITE_DAC;

	rc = CIFSSMBOpen(xid, tcon, path, FILE_OPEN, access_flags,
			create_options, &fid, &oplock, NULL, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc) {
		cERROR(1, "Unable to open file to set ACL");
		goto out;
	}

	rc = CIFSSMBSetCIFSACL(xid, tcon, fid, pnntsd, acllen, aclflag);
	cFYI(DBG2, "SetCIFSACL rc = %d", rc);

	CIFSSMBClose(xid, tcon, fid);
out:
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

/* Translate the CIFS ACL (simlar to NTFS ACL) for a file into mode bits */
int
cifs_acl_to_fattr(struct cifs_sb_info *cifs_sb, struct cifs_fattr *fattr,
		  struct inode *inode, const char *path, const __u16 *pfid)
{
	struct cifs_ntsd *pntsd = NULL;
	u32 acllen = 0;
	int rc = 0;

	cFYI(DBG2, "converting ACL to mode for %s", path);

	if (pfid)
		pntsd = get_cifs_acl_by_fid(cifs_sb, *pfid, &acllen);
	else
		pntsd = get_cifs_acl(cifs_sb, inode, path, &acllen);

	/* if we can retrieve the ACL, now parse Access Control Entries, ACEs */
	if (IS_ERR(pntsd)) {
		rc = PTR_ERR(pntsd);
		cERROR(1, "%s: error %d getting sec desc", __func__, rc);
	} else {
		rc = parse_sec_desc(cifs_sb, pntsd, acllen, fattr);
		kfree(pntsd);
		if (rc)
			cERROR(1, "parse sec desc failed rc = %d", rc);
	}

	return rc;
}

/* Convert mode bits to an ACL so we can update the ACL on the server */
int
id_mode_to_cifs_acl(struct inode *inode, const char *path, __u64 nmode,
			uid_t uid, gid_t gid)
{
	int rc = 0;
	int aclflag = CIFS_ACL_DACL; /* default flag to set */
	__u32 secdesclen = 0;
	struct cifs_ntsd *pntsd = NULL; /* acl obtained from server */
	struct cifs_ntsd *pnntsd = NULL; /* modified acl to be sent to server */

	cFYI(DBG2, "set ACL from mode for %s", path);

	/* Get the security descriptor */
	pntsd = get_cifs_acl(CIFS_SB(inode->i_sb), inode, path, &secdesclen);

	/* Add three ACEs for owner, group, everyone getting rid of
	   other ACEs as chmod disables ACEs and set the security descriptor */

	if (IS_ERR(pntsd)) {
		rc = PTR_ERR(pntsd);
		cERROR(1, "%s: error %d getting sec desc", __func__, rc);
	} else {
		/* allocate memory for the smb header,
		   set security descriptor request security descriptor
		   parameters, and secuirty descriptor itself */

		secdesclen = secdesclen < DEFSECDESCLEN ?
					DEFSECDESCLEN : secdesclen;
		pnntsd = kmalloc(secdesclen, GFP_KERNEL);
		if (!pnntsd) {
			cERROR(1, "Unable to allocate security descriptor");
			kfree(pntsd);
			return -ENOMEM;
		}

		rc = build_sec_desc(pntsd, pnntsd, secdesclen, nmode, uid, gid,
					&aclflag);

		cFYI(DBG2, "build_sec_desc rc: %d", rc);

		if (!rc) {
			/* Set the security descriptor */
			rc = set_cifs_acl(pnntsd, secdesclen, inode,
						path, aclflag);
			cFYI(DBG2, "set_cifs_acl rc: %d", rc);
		}

		kfree(pnntsd);
		kfree(pntsd);
	}

	return rc;
}
