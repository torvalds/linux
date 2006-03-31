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

static int
mode_from_nfs4(u32 perm, unsigned short *mode, unsigned int flags)
{
	u32 ignore = 0;

	if (!(flags & NFS4_ACL_DIR))
		ignore |= NFS4_ACE_DELETE_CHILD; /* ignore it */
	perm |= ignore;
	*mode = 0;
	if ((perm & NFS4_READ_MODE) == NFS4_READ_MODE)
		*mode |= ACL_READ;
	if ((perm & NFS4_WRITE_MODE) == NFS4_WRITE_MODE)
		*mode |= ACL_WRITE;
	if ((perm & NFS4_EXECUTE_MODE) == NFS4_EXECUTE_MODE)
		*mode |= ACL_EXECUTE;
	if (!MASK_EQUAL(perm, ignore|mask_from_posix(*mode, flags)))
		return -EINVAL;
	return 0;
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

static int
write_pace(struct nfs4_ace *ace, struct posix_acl *pacl,
		struct posix_acl_entry **pace, short tag, unsigned int flags)
{
	struct posix_acl_entry *this = *pace;

	if (*pace == pacl->a_entries + pacl->a_count)
		return -EINVAL; /* fell off the end */
	(*pace)++;
	this->e_tag = tag;
	if (tag == ACL_USER_OBJ)
		flags |= NFS4_ACL_OWNER;
	if (mode_from_nfs4(ace->access_mask, &this->e_perm, flags))
		return -EINVAL;
	this->e_id = (tag == ACL_USER || tag == ACL_GROUP ?
			ace->who : ACL_UNDEFINED_ID);
	return 0;
}

static struct nfs4_ace *
get_next_v4_ace(struct list_head **p, struct list_head *head)
{
	struct nfs4_ace *ace;

	*p = (*p)->next;
	if (*p == head)
		return NULL;
	ace = list_entry(*p, struct nfs4_ace, l_ace);

	return ace;
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
	if (error < 0)
		goto out_acl;

	if (pacl != NULL) {
		if (acl->naces == 0) {
			error = -ENODATA;
			goto try_dpacl;
		}

		*pacl = _nfsv4_to_posix_one(acl, flags);
		if (IS_ERR(*pacl)) {
			error = PTR_ERR(*pacl);
			*pacl = NULL;
			goto out_acl;
		}
	}

try_dpacl:
	if (dpacl != NULL) {
		if (dacl->naces == 0) {
			if (pacl == NULL || *pacl == NULL)
				error = -ENODATA;
			goto out_acl;
		}

		error = 0;
		*dpacl = _nfsv4_to_posix_one(dacl, flags);
		if (IS_ERR(*dpacl)) {
			error = PTR_ERR(*dpacl);
			*dpacl = NULL;
			goto out_acl;
		}
	}

out_acl:
	if (error && pacl) {
		posix_acl_release(*pacl);
		*pacl = NULL;
	}
	nfs4_acl_free(dacl);
out:
	return error;
}

static int
same_who(struct nfs4_ace *a, struct nfs4_ace *b)
{
	return a->whotype == b->whotype &&
		(a->whotype != NFS4_ACL_WHO_NAMED || a->who == b->who);
}

static int
complementary_ace_pair(struct nfs4_ace *allow, struct nfs4_ace *deny,
		unsigned int flags)
{
	int ignore = 0;
	if (!(flags & NFS4_ACL_DIR))
		ignore |= NFS4_ACE_DELETE_CHILD;
	return MASK_EQUAL(ignore|deny_mask(allow->access_mask, flags),
			  ignore|deny->access_mask) &&
		allow->type == NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE &&
		deny->type == NFS4_ACE_ACCESS_DENIED_ACE_TYPE &&
		allow->flag == deny->flag &&
		same_who(allow, deny);
}

static inline int
user_obj_from_v4(struct nfs4_acl *n4acl, struct list_head **p,
		struct posix_acl *pacl, struct posix_acl_entry **pace,
		unsigned int flags)
{
	int error = -EINVAL;
	struct nfs4_ace *ace, *ace2;

	ace = get_next_v4_ace(p, &n4acl->ace_head);
	if (ace == NULL)
		goto out;
	if (ace2type(ace) != ACL_USER_OBJ)
		goto out;
	error = write_pace(ace, pacl, pace, ACL_USER_OBJ, flags);
	if (error < 0)
		goto out;
	error = -EINVAL;
	ace2 = get_next_v4_ace(p, &n4acl->ace_head);
	if (ace2 == NULL)
		goto out;
	if (!complementary_ace_pair(ace, ace2, flags))
		goto out;
	error = 0;
out:
	return error;
}

static inline int
users_from_v4(struct nfs4_acl *n4acl, struct list_head **p,
		struct nfs4_ace **mask_ace,
		struct posix_acl *pacl, struct posix_acl_entry **pace,
		unsigned int flags)
{
	int error = -EINVAL;
	struct nfs4_ace *ace, *ace2;

	ace = get_next_v4_ace(p, &n4acl->ace_head);
	if (ace == NULL)
		goto out;
	while (ace2type(ace) == ACL_USER) {
		if (ace->type != NFS4_ACE_ACCESS_DENIED_ACE_TYPE)
			goto out;
		if (*mask_ace &&
			!MASK_EQUAL(ace->access_mask, (*mask_ace)->access_mask))
			goto out;
		*mask_ace = ace;
		ace = get_next_v4_ace(p, &n4acl->ace_head);
		if (ace == NULL)
			goto out;
		if (ace->type != NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE)
			goto out;
		error = write_pace(ace, pacl, pace, ACL_USER, flags);
		if (error < 0)
			goto out;
		error = -EINVAL;
		ace2 = get_next_v4_ace(p, &n4acl->ace_head);
		if (ace2 == NULL)
			goto out;
		if (!complementary_ace_pair(ace, ace2, flags))
			goto out;
		if ((*mask_ace)->flag != ace2->flag ||
				!same_who(*mask_ace, ace2))
			goto out;
		ace = get_next_v4_ace(p, &n4acl->ace_head);
		if (ace == NULL)
			goto out;
	}
	error = 0;
out:
	return error;
}

static inline int
group_obj_and_groups_from_v4(struct nfs4_acl *n4acl, struct list_head **p,
		struct nfs4_ace **mask_ace,
		struct posix_acl *pacl, struct posix_acl_entry **pace,
		unsigned int flags)
{
	int error = -EINVAL;
	struct nfs4_ace *ace, *ace2;
	struct ace_container *ac;
	struct list_head group_l;

	INIT_LIST_HEAD(&group_l);
	ace = list_entry(*p, struct nfs4_ace, l_ace);

	/* group owner (mask and allow aces) */

	if (pacl->a_count != 3) {
		/* then the group owner should be preceded by mask */
		if (ace->type != NFS4_ACE_ACCESS_DENIED_ACE_TYPE)
			goto out;
		if (*mask_ace &&
			!MASK_EQUAL(ace->access_mask, (*mask_ace)->access_mask))
			goto out;
		*mask_ace = ace;
		ace = get_next_v4_ace(p, &n4acl->ace_head);
		if (ace == NULL)
			goto out;

		if ((*mask_ace)->flag != ace->flag || !same_who(*mask_ace, ace))
			goto out;
	}

	if (ace2type(ace) != ACL_GROUP_OBJ)
		goto out;

	ac = kmalloc(sizeof(*ac), GFP_KERNEL);
	error = -ENOMEM;
	if (ac == NULL)
		goto out;
	ac->ace = ace;
	list_add_tail(&ac->ace_l, &group_l);

	error = -EINVAL;
	if (ace->type != NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE)
		goto out;

	error = write_pace(ace, pacl, pace, ACL_GROUP_OBJ, flags);
	if (error < 0)
		goto out;

	error = -EINVAL;
	ace = get_next_v4_ace(p, &n4acl->ace_head);
	if (ace == NULL)
		goto out;

	/* groups (mask and allow aces) */

	while (ace2type(ace) == ACL_GROUP) {
		if (*mask_ace == NULL)
			goto out;

		if (ace->type != NFS4_ACE_ACCESS_DENIED_ACE_TYPE ||
			!MASK_EQUAL(ace->access_mask, (*mask_ace)->access_mask))
			goto out;
		*mask_ace = ace;

		ace = get_next_v4_ace(p, &n4acl->ace_head);
		if (ace == NULL)
			goto out;
		ac = kmalloc(sizeof(*ac), GFP_KERNEL);
		error = -ENOMEM;
		if (ac == NULL)
			goto out;
		error = -EINVAL;
		if (ace->type != NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE ||
				!same_who(ace, *mask_ace))
			goto out;

		ac->ace = ace;
		list_add_tail(&ac->ace_l, &group_l);

		error = write_pace(ace, pacl, pace, ACL_GROUP, flags);
		if (error < 0)
			goto out;
		error = -EINVAL;
		ace = get_next_v4_ace(p, &n4acl->ace_head);
		if (ace == NULL)
			goto out;
	}

	/* group owner (deny ace) */

	if (ace2type(ace) != ACL_GROUP_OBJ)
		goto out;
	ac = list_entry(group_l.next, struct ace_container, ace_l);
	ace2 = ac->ace;
	if (!complementary_ace_pair(ace2, ace, flags))
		goto out;
	list_del(group_l.next);
	kfree(ac);

	/* groups (deny aces) */

	while (!list_empty(&group_l)) {
		ace = get_next_v4_ace(p, &n4acl->ace_head);
		if (ace == NULL)
			goto out;
		if (ace2type(ace) != ACL_GROUP)
			goto out;
		ac = list_entry(group_l.next, struct ace_container, ace_l);
		ace2 = ac->ace;
		if (!complementary_ace_pair(ace2, ace, flags))
			goto out;
		list_del(group_l.next);
		kfree(ac);
	}

	ace = get_next_v4_ace(p, &n4acl->ace_head);
	if (ace == NULL)
		goto out;
	if (ace2type(ace) != ACL_OTHER)
		goto out;
	error = 0;
out:
	while (!list_empty(&group_l)) {
		ac = list_entry(group_l.next, struct ace_container, ace_l);
		list_del(group_l.next);
		kfree(ac);
	}
	return error;
}

static inline int
mask_from_v4(struct nfs4_acl *n4acl, struct list_head **p,
		struct nfs4_ace **mask_ace,
		struct posix_acl *pacl, struct posix_acl_entry **pace,
		unsigned int flags)
{
	int error = -EINVAL;
	struct nfs4_ace *ace;

	ace = list_entry(*p, struct nfs4_ace, l_ace);
	if (pacl->a_count != 3) {
		if (*mask_ace == NULL)
			goto out;
		(*mask_ace)->access_mask = deny_mask((*mask_ace)->access_mask, flags);
		write_pace(*mask_ace, pacl, pace, ACL_MASK, flags);
	}
	error = 0;
out:
	return error;
}

static inline int
other_from_v4(struct nfs4_acl *n4acl, struct list_head **p,
		struct posix_acl *pacl, struct posix_acl_entry **pace,
		unsigned int flags)
{
	int error = -EINVAL;
	struct nfs4_ace *ace, *ace2;

	ace = list_entry(*p, struct nfs4_ace, l_ace);
	if (ace->type != NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE)
		goto out;
	error = write_pace(ace, pacl, pace, ACL_OTHER, flags);
	if (error < 0)
		goto out;
	error = -EINVAL;
	ace2 = get_next_v4_ace(p, &n4acl->ace_head);
	if (ace2 == NULL)
		goto out;
	if (!complementary_ace_pair(ace, ace2, flags))
		goto out;
	error = 0;
out:
	return error;
}

static int
calculate_posix_ace_count(struct nfs4_acl *n4acl)
{
	if (n4acl->naces == 6) /* owner, owner group, and other only */
		return 3;
	else { /* Otherwise there must be a mask entry. */
		/* Also, the remaining entries are for named users and
		 * groups, and come in threes (mask, allow, deny): */
		if (n4acl->naces < 7)
			return -1;
		if ((n4acl->naces - 7) % 3)
			return -1;
		return 4 + (n4acl->naces - 7)/3;
	}
}


static struct posix_acl *
_nfsv4_to_posix_one(struct nfs4_acl *n4acl, unsigned int flags)
{
	struct posix_acl *pacl;
	int error = -EINVAL, nace = 0;
	struct list_head *p;
	struct nfs4_ace *mask_ace = NULL;
	struct posix_acl_entry *pace;

	nace = calculate_posix_ace_count(n4acl);
	if (nace < 0)
		goto out_err;

	pacl = posix_acl_alloc(nace, GFP_KERNEL);
	error = -ENOMEM;
	if (pacl == NULL)
		goto out_err;

	pace = &pacl->a_entries[0];
	p = &n4acl->ace_head;

	error = user_obj_from_v4(n4acl, &p, pacl, &pace, flags);
	if (error)
		goto out_acl;

	error = users_from_v4(n4acl, &p, &mask_ace, pacl, &pace, flags);
	if (error)
		goto out_acl;

	error = group_obj_and_groups_from_v4(n4acl, &p, &mask_ace, pacl, &pace,
						flags);
	if (error)
		goto out_acl;

	error = mask_from_v4(n4acl, &p, &mask_ace, pacl, &pace, flags);
	if (error)
		goto out_acl;
	error = other_from_v4(n4acl, &p, pacl, &pace, flags);
	if (error)
		goto out_acl;

	error = -EINVAL;
	if (p->next != &n4acl->ace_head)
		goto out_acl;
	if (pace != pacl->a_entries + pacl->a_count)
		goto out_acl;

	sort_pacl(pacl);

	return pacl;
out_acl:
	posix_acl_release(pacl);
out_err:
	pacl = ERR_PTR(error);
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

		if ((ace->flag & NFS4_INHERITANCE_FLAGS)
				!= NFS4_INHERITANCE_FLAGS)
			continue;

		error = nfs4_acl_add_ace(dacl, ace->type, ace->flag,
				ace->access_mask, ace->whotype, ace->who) == -1;
		if (error < 0)
			goto out;

		list_del(h);
		kfree(ace);
		acl->naces--;
	}

out:
	return error;
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
		return -1;

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

static inline int
match_who(struct nfs4_ace *ace, uid_t owner, gid_t group, uid_t who)
{
	switch (ace->whotype) {
		case NFS4_ACL_WHO_NAMED:
			return who == ace->who;
		case NFS4_ACL_WHO_OWNER:
			return who == owner;
		case NFS4_ACL_WHO_GROUP:
			return who == group;
		case NFS4_ACL_WHO_EVERYONE:
			return 1;
		default:
			return 0;
	}
}

EXPORT_SYMBOL(nfs4_acl_new);
EXPORT_SYMBOL(nfs4_acl_free);
EXPORT_SYMBOL(nfs4_acl_add_ace);
EXPORT_SYMBOL(nfs4_acl_get_whotype);
EXPORT_SYMBOL(nfs4_acl_write_who);
