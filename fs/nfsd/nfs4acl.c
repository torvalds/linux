/*
 *  fs/nfs4acl/acl.c
 *
 *  Common NFSv4 ACL handling code.
 *
 *  Copyright (c) 2002, 2003 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
 *  Jeff Sedlak <jsedlak@umich.edu>
 *  J. Bruce Fields <bfields@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/nfs_fs.h>
#include <linux/posix_acl.h>
#include <linux/nfs4.h>
#include <linux/nfs4_acl.h>


/* mode bit translations: */
#define NFS4_READ_MODE (NFS4_ACE_READ_DATA)
#define NFS4_WRITE_MODE (NFS4_ACE_WRITE_DATA | NFS4_ACE_APPEND_DATA)
#define NFS4_EXECUTE_MODE NFS4_ACE_EXECUTE
#define NFS4_ANYONE_MODE (NFS4_ACE_READ_ATTRIBUTES | NFS4_ACE_READ_ACL | NFS4_ACE_SYNCHRONIZE)
#define NFS4_OWNER_MODE (NFS4_ACE_WRITE_ATTRIBUTES | NFS4_ACE_WRITE_ACL)

/* We don't support these bits; insist they be neither allowed nor denied */
#define NFS4_MASK_UNSUPP (NFS4_ACE_DELETE | NFS4_ACE_WRITE_OWNER \
		| NFS4_ACE_READ_NAMED_ATTRS | NFS4_ACE_WRITE_NAMED_ATTRS)

/* flags used to simulate posix default ACLs */
#define NFS4_INHERITANCE_FLAGS (NFS4_ACE_FILE_INHERIT_ACE \
		| NFS4_ACE_DIRECTORY_INHERIT_ACE | NFS4_ACE_INHERIT_ONLY_ACE)

#define NFS4_SUPPORTED_FLAGS (NFS4_INHERITANCE_FLAGS | NFS4_ACE_IDENTIFIER_GROUP)

#define MASK_EQUAL(mask1, mask2) \
	( ((mask1) & NFS4_ACE_MASK_ALL) == ((mask2) & NFS4_ACE_MASK_ALL) )

static u32
mask_from_posix(unsigned short perm, unsigned int flags)
{
	int mask = NFS4_ANYONE_MODE;

	if (flags & NFS4_ACL_OWNER)
		mask |= NFS4_OWNER_MODE;
	if (perm & ACL_READ)
		mask |= NFS4_READ_MODE;
	if (perm & ACL_WRITE)
		mask |= NFS4_WRITE_MODE;
	if ((perm & ACL_WRITE) && (flags & NFS4_ACL_DIR))
		mask |= NFS4_ACE_DELETE_CHILD;
	if (perm & ACL_EXECUTE)
		mask |= NFS4_EXECUTE_MODE;
	return mask;
}

static u32
deny_mask(u32 allow_mask, unsigned int flags)
{
	u32 ret = ~allow_mask & ~NFS4_MASK_UNSUPP;
	if (!(flags & NFS4_ACL_DIR))
		ret &= ~NFS4_ACE_DELETE_CHILD;
	return ret;
}

/* XXX: modify functions to return NFS errors; they're only ever
 * used by nfs code, after all.... */

/* We only map from NFSv4 to POSIX ACLs when setting ACLs, when we err on the
 * side of being more restrictive, so the mode bit mapping below is
 * pessimistic.  An optimistic version would be needed to handle DENY's,
 * but we espect to coalesce all ALLOWs and DENYs before mapping to mode
 * bits. */

static void
low_mode_from_nfs4(u32 perm, unsigned short *mode, unsigned int flags)
{
	u32 write_mode = NFS4_WRITE_MODE;

	if (flags & NFS4_ACL_DIR)
		write_mode |= NFS4_ACE_DELETE_CHILD;
	*mode = 0;
	if ((perm & NFS4_READ_MODE) == NFS4_READ_MODE)
		*mode |= ACL_READ;
	if ((perm & write_mode) == write_mode)
		*mode |= ACL_WRITE;
	if ((perm & NFS4_EXECUTE_MODE) == NFS4_EXECUTE_MODE)
		*mode |= ACL_EXECUTE;
}

struct ace_container {
	struct nfs4_ace  *ace;
	struct list_head  ace_l;
};

static short ace2type(struct nfs4_ace *);
static int _posix_to_nfsv4_one(struct posix_acl *, struct nfs4_acl *, unsigned int);
static struct posix_acl *_nfsv4_to_posix_one(struct nfs4_acl *, unsigned int);
int nfs4_acl_add_ace(struct nfs4_acl *, u32, u32, u32, int, uid_t);
static int nfs4_acl_split(struct nfs4_acl *, struct nfs4_acl *);

struct nfs4_acl *
nfs4_acl_posix_to_nfsv4(struct posix_acl *pacl, struct posix_acl *dpacl,
			unsigned int flags)
{
	struct nfs4_acl *acl;
	int error = -EINVAL;

	if ((pacl != NULL &&
		(posix_acl_valid(pacl) < 0 || pacl->a_count == 0)) ||
	    (dpacl != NULL &&
		(posix_acl_valid(dpacl) < 0 || dpacl->a_count == 0)))
		goto out_err;

	acl = nfs4_acl_new();
	if (acl == NULL) {
		error = -ENOMEM;
		goto out_err;
	}

	if (pacl != NULL) {
		error = _posix_to_nfsv4_one(pacl, acl,
						flags & ~NFS4_ACL_TYPE_DEFAULT);
		if (error < 0)
			goto out_acl;
	}

	if (dpacl != NULL) {
		error = _posix_to_nfsv4_one(dpacl, acl,
						flags | NFS4_ACL_TYPE_DEFAULT);
		if (error < 0)
			goto out_acl;
	}

	return acl;

out_acl:
	nfs4_acl_free(acl);
out_err:
	acl = ERR_PTR(error);

	return acl;
}

static int
nfs4_acl_add_pair(struct nfs4_acl *acl, int eflag, u32 mask, int whotype,
		uid_t owner, unsigned int flags)
{
	int error;

	error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE,
				 eflag, mask, whotype, owner);
	if (error < 0)
		return error;
	error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_DENIED_ACE_TYPE,
				eflag, deny_mask(mask, flags), whotype, owner);
	return error;
}

/* We assume the acl has been verified with posix_acl_valid. */
static int
_posix_to_nfsv4_one(struct posix_acl *pacl, struct nfs4_acl *acl,
						unsigned int flags)
{
	struct posix_acl_entry *pa, *pe, *group_owner_entry;
	int error = -EINVAL;
	u32 mask, mask_mask;
	int eflag = ((flags & NFS4_ACL_TYPE_DEFAULT) ?
					NFS4_INHERITANCE_FLAGS : 0);

	BUG_ON(pacl->a_count < 3);
	pe = pacl->a_entries + pacl->a_count;
	pa = pe - 2; /* if mask entry exists, it's second from the last. */
	if (pa->e_tag == ACL_MASK)
		mask_mask = deny_mask(mask_from_posix(pa->e_perm, flags), flags);
	else
		mask_mask = 0;

	pa = pacl->a_entries;
	BUG_ON(pa->e_tag != ACL_USER_OBJ);
	mask = mask_from_posix(pa->e_perm, flags | NFS4_ACL_OWNER);
	error = nfs4_acl_add_pair(acl, eflag, mask, NFS4_ACL_WHO_OWNER, 0, flags);
	if (error < 0)
		goto out;
	pa++;

	while (pa->e_tag == ACL_USER) {
		mask = mask_from_posix(pa->e_perm, flags);
		error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_DENIED_ACE_TYPE,
				eflag,  mask_mask, NFS4_ACL_WHO_NAMED, pa->e_id);
		if (error < 0)
			goto out;


		error = nfs4_acl_add_pair(acl, eflag, mask,
				NFS4_ACL_WHO_NAMED, pa->e_id, flags);
		if (error < 0)
			goto out;
		pa++;
	}

	/* In the case of groups, we apply allow ACEs first, then deny ACEs,
	 * since a user can be in more than one group.  */

	/* allow ACEs */

	if (pacl->a_count > 3) {
		BUG_ON(pa->e_tag != ACL_GROUP_OBJ);
		error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_DENIED_ACE_TYPE,
				NFS4_ACE_IDENTIFIER_GROUP | eflag, mask_mask,
				NFS4_ACL_WHO_GROUP, 0);
		if (error < 0)
			goto out;
	}
	group_owner_entry = pa;
	mask = mask_from_posix(pa->e_perm, flags);
	error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE,
			NFS4_ACE_IDENTIFIER_GROUP | eflag, mask,
			NFS4_ACL_WHO_GROUP, 0);
	if (error < 0)
		goto out;
	pa++;

	while (pa->e_tag == ACL_GROUP) {
		mask = mask_from_posix(pa->e_perm, flags);
		error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_DENIED_ACE_TYPE,
				NFS4_ACE_IDENTIFIER_GROUP | eflag, mask_mask,
				NFS4_ACL_WHO_NAMED, pa->e_id);
		if (error < 0)
			goto out;

		error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE,
		    		NFS4_ACE_IDENTIFIER_GROUP | eflag, mask,
		    		NFS4_ACL_WHO_NAMED, pa->e_id);
		if (error < 0)
			goto out;
		pa++;
	}

	/* deny ACEs */

	pa = group_owner_entry;
	mask = mask_from_posix(pa->e_perm, flags);
	error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_DENIED_ACE_TYPE,
			NFS4_ACE_IDENTIFIER_GROUP | eflag,
			deny_mask(mask, flags), NFS4_ACL_WHO_GROUP, 0);
	if (error < 0)
		goto out;
	pa++;
	while (pa->e_tag == ACL_GROUP) {
		mask = mask_from_posix(pa->e_perm, flags);
		error = nfs4_acl_add_ace(acl, NFS4_ACE_ACCESS_DENIED_ACE_TYPE,
		    		NFS4_ACE_IDENTIFIER_GROUP | eflag,
		    		deny_mask(mask, flags), NFS4_ACL_WHO_NAMED, pa->e_id);
		if (error < 0)
			goto out;
		pa++;
	}

	if (pa->e_tag == ACL_MASK)
		pa++;
	BUG_ON(pa->e_tag != ACL_OTHER);
	mask = mask_from_posix(pa->e_perm, flags);
	error = nfs4_acl_add_pair(acl, eflag, mask, NFS4_ACL_WHO_EVERYONE, 0, flags);

out:
	return error;
}

static void
sort_pacl_range(struct posix_acl *pacl, int start, int end) {
	int sorted = 0, i;
	struct posix_acl_entry tmp;

	/* We just do a bubble sort; easy to do in place, and we're not
	 * expecting acl's to be long enough to justify anything more. */
	while (!sorted) {
		sorted = 1;
		for (i = start; i < end; i++) {
			if (pacl->a_entries[i].e_id
					> pacl->a_entries[i+1].e_id) {
				sorted = 0;
				tmp = pacl->a_entries[i];
				pacl->a_entries[i] = pacl->a_entries[i+1];
				pacl->a_entries[i+1] = tmp;
			}
		}
	}
}

static void
sort_pacl(struct posix_acl *pacl)
{
	/* posix_acl_valid requires that users and groups be in order
	 * by uid/gid. */
	int i, j;

	if (pacl->a_count <= 4)
		return; /* no users or groups */
	i = 1;
	while (pacl->a_entries[i].e_tag == ACL_USER)
		i++;
	sort_pacl_range(pacl, 1, i-1);

	BUG_ON(pacl->a_entries[i].e_tag != ACL_GROUP_OBJ);
	j = i++;
	while (pacl->a_entries[j].e_tag == ACL_GROUP)
		j++;
	sort_pacl_range(pacl, i, j-1);
	return;
}

int
nfs4_acl_nfsv4_to_posix(struct nfs4_acl *acl, struct posix_acl **pacl,
		struct posix_acl **dpacl, unsigned int flags)
{
	struct nfs4_acl *dacl;
	int error = -ENOMEM;

	*pacl = NULL;
	*dpacl = NULL;

	dacl = nfs4_acl_new();
	if (dacl == NULL)
		goto out;

	error = nfs4_acl_split(acl, dacl);
	if (error)
		goto out_acl;

	*pacl = _nfsv4_to_posix_one(acl, flags);
	if (IS_ERR(*pacl)) {
		error = PTR_ERR(*pacl);
		*pacl = NULL;
		goto out_acl;
	}

	*dpacl = _nfsv4_to_posix_one(dacl, flags);
	if (IS_ERR(*dpacl)) {
		error = PTR_ERR(*dpacl);
		*dpacl = NULL;
	}
out_acl:
	if (error) {
		posix_acl_release(*pacl);
		*pacl = NULL;
	}
	nfs4_acl_free(dacl);
out:
	return error;
}

/*
 * While processing the NFSv4 ACE, this maintains bitmasks representing
 * which permission bits have been allowed and which denied to a given
 * entity: */
struct posix_ace_state {
	u32 allow;
	u32 deny;
};

struct posix_user_ace_state {
	uid_t uid;
	struct posix_ace_state perms;
};

struct posix_ace_state_array {
	int n;
	struct posix_user_ace_state aces[];
};

/*
 * While processing the NFSv4 ACE, this maintains the partial permissions
 * calculated so far: */

struct posix_acl_state {
	struct posix_ace_state owner;
	struct posix_ace_state group;
	struct posix_ace_state other;
	struct posix_ace_state everyone;
	struct posix_ace_state mask; /* Deny unused in this case */
	struct posix_ace_state_array *users;
	struct posix_ace_state_array *groups;
};

static int
init_state(struct posix_acl_state *state, int cnt)
{
	int alloc;

	memset(state, 0, sizeof(struct posix_acl_state));
	/*
	 * In the worst case, each individual acl could be for a distinct
	 * named user or group, but we don't no which, so we allocate
	 * enough space for either:
	 */
	alloc = sizeof(struct posix_ace_state_array)
		+ cnt*sizeof(struct posix_ace_state);
	state->users = kzalloc(alloc, GFP_KERNEL);
	if (!state->users)
		return -ENOMEM;
	state->groups = kzalloc(alloc, GFP_KERNEL);
	if (!state->groups) {
		kfree(state->users);
		return -ENOMEM;
	}
	return 0;
}

static void
free_state(struct posix_acl_state *state) {
	kfree(state->users);
	kfree(state->groups);
}

static inline void add_to_mask(struct posix_acl_state *state, struct posix_ace_state *astate)
{
	state->mask.allow |= astate->allow;
}

/*
 * Certain bits (SYNCHRONIZE, DELETE, WRITE_OWNER, READ/WRITE_NAMED_ATTRS,
 * READ_ATTRIBUTES, READ_ACL) are currently unenforceable and don't translate
 * to traditional read/write/execute permissions.
 *
 * It's problematic to reject acls that use certain mode bits, because it
 * places the burden on users to learn the rules about which bits one
 * particular server sets, without giving the user a lot of help--we return an
 * error that could mean any number of different things.  To make matters
 * worse, the problematic bits might be introduced by some application that's
 * automatically mapping from some other acl model.
 *
 * So wherever possible we accept anything, possibly erring on the side of
 * denying more permissions than necessary.
 *
 * However we do reject *explicit* DENY's of a few bits representing
 * permissions we could never deny:
 */

static inline int check_deny(u32 mask, int isowner)
{
	if (mask & (NFS4_ACE_READ_ATTRIBUTES | NFS4_ACE_READ_ACL))
		return -EINVAL;
	if (!isowner)
		return 0;
	if (mask & (NFS4_ACE_WRITE_ATTRIBUTES | NFS4_ACE_WRITE_ACL))
		return -EINVAL;
	return 0;
}

static struct posix_acl *
posix_state_to_acl(struct posix_acl_state *state, unsigned int flags)
{
	struct posix_acl_entry *pace;
	struct posix_acl *pacl;
	int nace;
	int i, error = 0;

	nace = 4 + state->users->n + state->groups->n;
	pacl = posix_acl_alloc(nace, GFP_KERNEL);
	if (!pacl)
		return ERR_PTR(-ENOMEM);

	pace = pacl->a_entries;
	pace->e_tag = ACL_USER_OBJ;
	error = check_deny(state->owner.deny, 1);
	if (error)
		goto out_err;
	low_mode_from_nfs4(state->owner.allow, &pace->e_perm, flags);
	pace->e_id = ACL_UNDEFINED_ID;

	for (i=0; i < state->users->n; i++) {
		pace++;
		pace->e_tag = ACL_USER;
		error = check_deny(state->users->aces[i].perms.deny, 0);
		if (error)
			goto out_err;
		low_mode_from_nfs4(state->users->aces[i].perms.allow,
					&pace->e_perm, flags);
		pace->e_id = state->users->aces[i].uid;
		add_to_mask(state, &state->users->aces[i].perms);
	}

	pace++;
	pace->e_tag = ACL_GROUP_OBJ;
	error = check_deny(state->group.deny, 0);
	if (error)
		goto out_err;
	low_mode_from_nfs4(state->group.allow, &pace->e_perm, flags);
	pace->e_id = ACL_UNDEFINED_ID;
	add_to_mask(state, &state->group);

	for (i=0; i < state->groups->n; i++) {
		pace++;
		pace->e_tag = ACL_GROUP;
		error = check_deny(state->groups->aces[i].perms.deny, 0);
		if (error)
			goto out_err;
		low_mode_from_nfs4(state->groups->aces[i].perms.allow,
					&pace->e_perm, flags);
		pace->e_id = state->groups->aces[i].uid;
		add_to_mask(state, &state->groups->aces[i].perms);
	}

	pace++;
	pace->e_tag = ACL_MASK;
	low_mode_from_nfs4(state->mask.allow, &pace->e_perm, flags);
	pace->e_id = ACL_UNDEFINED_ID;

	pace++;
	pace->e_tag = ACL_OTHER;
	error = check_deny(state->other.deny, 0);
	if (error)
		goto out_err;
	low_mode_from_nfs4(state->other.allow, &pace->e_perm, flags);
	pace->e_id = ACL_UNDEFINED_ID;

	return pacl;
out_err:
	posix_acl_release(pacl);
	return ERR_PTR(error);
}

static inline void allow_bits(struct posix_ace_state *astate, u32 mask)
{
	/* Allow all bits in the mask not already denied: */
	astate->allow |= mask & ~astate->deny;
}

static inline void deny_bits(struct posix_ace_state *astate, u32 mask)
{
	/* Deny all bits in the mask not already allowed: */
	astate->deny |= mask & ~astate->allow;
}

static int find_uid(struct posix_acl_state *state, struct posix_ace_state_array *a, uid_t uid)
{
	int i;

	for (i = 0; i < a->n; i++)
		if (a->aces[i].uid == uid)
			return i;
	/* Not found: */
	a->n++;
	a->aces[i].uid = uid;
	a->aces[i].perms.allow = state->everyone.allow;
	a->aces[i].perms.deny  = state->everyone.deny;

	return i;
}

static void deny_bits_array(struct posix_ace_state_array *a, u32 mask)
{
	int i;

	for (i=0; i < a->n; i++)
		deny_bits(&a->aces[i].perms, mask);
}

static void allow_bits_array(struct posix_ace_state_array *a, u32 mask)
{
	int i;

	for (i=0; i < a->n; i++)
		allow_bits(&a->aces[i].perms, mask);
}

static void process_one_v4_ace(struct posix_acl_state *state,
				struct nfs4_ace *ace)
{
	u32 mask = ace->access_mask;
	int i;

	switch (ace2type(ace)) {
	case ACL_USER_OBJ:
		if (ace->type == NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE) {
			allow_bits(&state->owner, mask);
		} else {
			deny_bits(&state->owner, mask);
		}
		break;
	case ACL_USER:
		i = find_uid(state, state->users, ace->who);
		if (ace->type == NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE) {
			allow_bits(&state->users->aces[i].perms, mask);
		} else {
			deny_bits(&state->users->aces[i].perms, mask);
			mask = state->users->aces[i].perms.deny;
			deny_bits(&state->owner, mask);
		}
		break;
	case ACL_GROUP_OBJ:
		if (ace->type == NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE) {
			allow_bits(&state->group, mask);
		} else {
			deny_bits(&state->group, mask);
			mask = state->group.deny;
			deny_bits(&state->owner, mask);
			deny_bits(&state->everyone, mask);
			deny_bits_array(state->users, mask);
			deny_bits_array(state->groups, mask);
		}
		break;
	case ACL_GROUP:
		i = find_uid(state, state->groups, ace->who);
		if (ace->type == NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE) {
			allow_bits(&state->groups->aces[i].perms, mask);
		} else {
			deny_bits(&state->groups->aces[i].perms, mask);
			mask = state->groups->aces[i].perms.deny;
			deny_bits(&state->owner, mask);
			deny_bits(&state->group, mask);
			deny_bits(&state->everyone, mask);
			deny_bits_array(state->users, mask);
			deny_bits_array(state->groups, mask);
		}
		break;
	case ACL_OTHER:
		if (ace->type == NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE) {
			allow_bits(&state->owner, mask);
			allow_bits(&state->group, mask);
			allow_bits(&state->other, mask);
			allow_bits(&state->everyone, mask);
			allow_bits_array(state->users, mask);
			allow_bits_array(state->groups, mask);
		} else {
			deny_bits(&state->owner, mask);
			deny_bits(&state->group, mask);
			deny_bits(&state->other, mask);
			deny_bits(&state->everyone, mask);
			deny_bits_array(state->users, mask);
			deny_bits_array(state->groups, mask);
		}
	}
}

static struct posix_acl *
_nfsv4_to_posix_one(struct nfs4_acl *n4acl, unsigned int flags)
{
	struct posix_acl_state state;
	struct posix_acl *pacl;
	struct nfs4_ace *ace;
	int ret;

	ret = init_state(&state, n4acl->naces);
	if (ret)
		return ERR_PTR(ret);

	list_for_each_entry(ace, &n4acl->ace_head, l_ace)
		process_one_v4_ace(&state, ace);

	pacl = posix_state_to_acl(&state, flags);

	free_state(&state);

	if (!IS_ERR(pacl))
		sort_pacl(pacl);
	return pacl;
}

static int
nfs4_acl_split(struct nfs4_acl *acl, struct nfs4_acl *dacl)
{
	struct list_head *h, *n;
	struct nfs4_ace *ace;
	int error = 0;

	list_for_each_safe(h, n, &acl->ace_head) {
		ace = list_entry(h, struct nfs4_ace, l_ace);

		if (ace->type != NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE &&
		    ace->type != NFS4_ACE_ACCESS_DENIED_ACE_TYPE)
			return -EINVAL;

		if (ace->flag & ~NFS4_SUPPORTED_FLAGS)
			return -EINVAL;

		switch (ace->flag & NFS4_INHERITANCE_FLAGS) {
		case 0:
			/* Leave this ace in the effective acl: */
			continue;
		case NFS4_INHERITANCE_FLAGS:
			/* Add this ace to the default acl and remove it
			 * from the effective acl: */
			error = nfs4_acl_add_ace(dacl, ace->type, ace->flag,
				ace->access_mask, ace->whotype, ace->who);
			if (error)
				return error;
			list_del(h);
			kfree(ace);
			acl->naces--;
			break;
		case NFS4_INHERITANCE_FLAGS & ~NFS4_ACE_INHERIT_ONLY_ACE:
			/* Add this ace to the default, but leave it in
			 * the effective acl as well: */
			error = nfs4_acl_add_ace(dacl, ace->type, ace->flag,
				ace->access_mask, ace->whotype, ace->who);
			if (error)
				return error;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static short
ace2type(struct nfs4_ace *ace)
{
	switch (ace->whotype) {
		case NFS4_ACL_WHO_NAMED:
			return (ace->flag & NFS4_ACE_IDENTIFIER_GROUP ?
					ACL_GROUP : ACL_USER);
		case NFS4_ACL_WHO_OWNER:
			return ACL_USER_OBJ;
		case NFS4_ACL_WHO_GROUP:
			return ACL_GROUP_OBJ;
		case NFS4_ACL_WHO_EVERYONE:
			return ACL_OTHER;
	}
	BUG();
	return -1;
}

EXPORT_SYMBOL(nfs4_acl_posix_to_nfsv4);
EXPORT_SYMBOL(nfs4_acl_nfsv4_to_posix);

struct nfs4_acl *
nfs4_acl_new(void)
{
	struct nfs4_acl *acl;

	if ((acl = kmalloc(sizeof(*acl), GFP_KERNEL)) == NULL)
		return NULL;

	acl->naces = 0;
	INIT_LIST_HEAD(&acl->ace_head);

	return acl;
}

void
nfs4_acl_free(struct nfs4_acl *acl)
{
	struct list_head *h;
	struct nfs4_ace *ace;

	if (!acl)
		return;

	while (!list_empty(&acl->ace_head)) {
		h = acl->ace_head.next;
		list_del(h);
		ace = list_entry(h, struct nfs4_ace, l_ace);
		kfree(ace);
	}

	kfree(acl);

	return;
}

int
nfs4_acl_add_ace(struct nfs4_acl *acl, u32 type, u32 flag, u32 access_mask,
		int whotype, uid_t who)
{
	struct nfs4_ace *ace;

	if ((ace = kmalloc(sizeof(*ace), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	ace->type = type;
	ace->flag = flag;
	ace->access_mask = access_mask;
	ace->whotype = whotype;
	ace->who = who;

	list_add_tail(&ace->l_ace, &acl->ace_head);
	acl->naces++;

	return 0;
}

static struct {
	char *string;
	int   stringlen;
	int type;
} s2t_map[] = {
	{
		.string    = "OWNER@",
		.stringlen = sizeof("OWNER@") - 1,
		.type      = NFS4_ACL_WHO_OWNER,
	},
	{
		.string    = "GROUP@",
		.stringlen = sizeof("GROUP@") - 1,
		.type      = NFS4_ACL_WHO_GROUP,
	},
	{
		.string    = "EVERYONE@",
		.stringlen = sizeof("EVERYONE@") - 1,
		.type      = NFS4_ACL_WHO_EVERYONE,
	},
};

int
nfs4_acl_get_whotype(char *p, u32 len)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s2t_map); i++) {
		if (s2t_map[i].stringlen == len &&
				0 == memcmp(s2t_map[i].string, p, len))
			return s2t_map[i].type;
	}
	return NFS4_ACL_WHO_NAMED;
}

int
nfs4_acl_write_who(int who, char *p)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s2t_map); i++) {
		if (s2t_map[i].type == who) {
			memcpy(p, s2t_map[i].string, s2t_map[i].stringlen);
			return s2t_map[i].stringlen;
		}
	}
	BUG();
	return -1;
}

EXPORT_SYMBOL(nfs4_acl_new);
EXPORT_SYMBOL(nfs4_acl_free);
EXPORT_SYMBOL(nfs4_acl_add_ace);
EXPORT_SYMBOL(nfs4_acl_get_whotype);
EXPORT_SYMBOL(nfs4_acl_write_who);
