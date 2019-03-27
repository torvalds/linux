/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2010 Edward Tomasz Napierała <trasz@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * ACL support routines specific to NFSv4 access control lists.  These are
 * utility routines for code common across file systems implementing NFSv4
 * ACLs.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/priv.h>
#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/acl.h>
#else
#include <errno.h>
#include <assert.h>
#include <sys/acl.h>
#include <sys/stat.h>
#define KASSERT(a, b) assert(a)
#define CTASSERT(a)

#endif /* !_KERNEL */

#ifdef _KERNEL

static void	acl_nfs4_trivial_from_mode(struct acl *aclp, mode_t mode);

static int	acl_nfs4_old_semantics = 0;

SYSCTL_INT(_vfs, OID_AUTO, acl_nfs4_old_semantics, CTLFLAG_RW,
    &acl_nfs4_old_semantics, 0, "Use pre-PSARC/2010/029 NFSv4 ACL semantics");

static struct {
	accmode_t accmode;
	int mask;
} accmode2mask[] = {{VREAD, ACL_READ_DATA},
		    {VWRITE, ACL_WRITE_DATA},
		    {VAPPEND, ACL_APPEND_DATA},
		    {VEXEC, ACL_EXECUTE},
		    {VREAD_NAMED_ATTRS, ACL_READ_NAMED_ATTRS},
		    {VWRITE_NAMED_ATTRS, ACL_WRITE_NAMED_ATTRS},
		    {VDELETE_CHILD, ACL_DELETE_CHILD},
		    {VREAD_ATTRIBUTES, ACL_READ_ATTRIBUTES},
		    {VWRITE_ATTRIBUTES, ACL_WRITE_ATTRIBUTES},
		    {VDELETE, ACL_DELETE},
		    {VREAD_ACL, ACL_READ_ACL},
		    {VWRITE_ACL, ACL_WRITE_ACL},
		    {VWRITE_OWNER, ACL_WRITE_OWNER},
		    {VSYNCHRONIZE, ACL_SYNCHRONIZE},
		    {0, 0}};

static int
_access_mask_from_accmode(accmode_t accmode)
{
	int access_mask = 0, i;

	for (i = 0; accmode2mask[i].accmode != 0; i++) {
		if (accmode & accmode2mask[i].accmode)
			access_mask |= accmode2mask[i].mask;
	}

	/*
	 * VAPPEND is just a modifier for VWRITE; if the caller asked
	 * for 'VAPPEND | VWRITE', we want to check for ACL_APPEND_DATA only.
	 */
	if (access_mask & ACL_APPEND_DATA)
		access_mask &= ~ACL_WRITE_DATA;

	return (access_mask);
}

/*
 * Return 0, iff access is allowed, 1 otherwise.
 */
static int
_acl_denies(const struct acl *aclp, int access_mask, struct ucred *cred,
    int file_uid, int file_gid, int *denied_explicitly)
{
	int i;
	const struct acl_entry *entry;

	if (denied_explicitly != NULL)
		*denied_explicitly = 0;

	KASSERT(aclp->acl_cnt <= ACL_MAX_ENTRIES,
	    ("aclp->acl_cnt <= ACL_MAX_ENTRIES"));

	for (i = 0; i < aclp->acl_cnt; i++) {
		entry = &(aclp->acl_entry[i]);

		if (entry->ae_entry_type != ACL_ENTRY_TYPE_ALLOW &&
		    entry->ae_entry_type != ACL_ENTRY_TYPE_DENY)
			continue;
		if (entry->ae_flags & ACL_ENTRY_INHERIT_ONLY)
			continue;
		switch (entry->ae_tag) {
		case ACL_USER_OBJ:
			if (file_uid != cred->cr_uid)
				continue;
			break;
		case ACL_USER:
			if (entry->ae_id != cred->cr_uid)
				continue;
			break;
		case ACL_GROUP_OBJ:
			if (!groupmember(file_gid, cred))
				continue;
			break;
		case ACL_GROUP:
			if (!groupmember(entry->ae_id, cred))
				continue;
			break;
		default:
			KASSERT(entry->ae_tag == ACL_EVERYONE,
			    ("entry->ae_tag == ACL_EVERYONE"));
		}

		if (entry->ae_entry_type == ACL_ENTRY_TYPE_DENY) {
			if (entry->ae_perm & access_mask) {
				if (denied_explicitly != NULL)
					*denied_explicitly = 1;
				return (1);
			}
		}

		access_mask &= ~(entry->ae_perm);
		if (access_mask == 0)
			return (0);
	}

	if (access_mask == 0)
		return (0);

	return (1);
}

int
vaccess_acl_nfs4(enum vtype type, uid_t file_uid, gid_t file_gid,
    struct acl *aclp, accmode_t accmode, struct ucred *cred, int *privused)
{
	accmode_t priv_granted = 0;
	int denied, explicitly_denied, access_mask, is_directory,
	    must_be_owner = 0;
	mode_t file_mode = 0;

	KASSERT((accmode & ~(VEXEC | VWRITE | VREAD | VADMIN | VAPPEND |
	    VEXPLICIT_DENY | VREAD_NAMED_ATTRS | VWRITE_NAMED_ATTRS |
	    VDELETE_CHILD | VREAD_ATTRIBUTES | VWRITE_ATTRIBUTES | VDELETE |
	    VREAD_ACL | VWRITE_ACL | VWRITE_OWNER | VSYNCHRONIZE)) == 0,
	    ("invalid bit in accmode"));
	KASSERT((accmode & VAPPEND) == 0 || (accmode & VWRITE),
	    	("VAPPEND without VWRITE"));

	if (privused != NULL)
		*privused = 0;

	if (accmode & VADMIN)
		must_be_owner = 1;

	/*
	 * Ignore VSYNCHRONIZE permission.
	 */
	accmode &= ~VSYNCHRONIZE;

	access_mask = _access_mask_from_accmode(accmode);

	if (type == VDIR)
		is_directory = 1;
	else
		is_directory = 0;

	/*
	 * File owner is always allowed to read and write the ACL
	 * and basic attributes.  This is to prevent a situation
	 * where user would change ACL in a way that prevents him
	 * from undoing the change.
	 */
	if (file_uid == cred->cr_uid)
		access_mask &= ~(ACL_READ_ACL | ACL_WRITE_ACL |
		    ACL_READ_ATTRIBUTES | ACL_WRITE_ATTRIBUTES);

	/*
	 * Ignore append permission for regular files; use write
	 * permission instead.
	 */
	if (!is_directory && (access_mask & ACL_APPEND_DATA)) {
		access_mask &= ~ACL_APPEND_DATA;
		access_mask |= ACL_WRITE_DATA;
	}

	denied = _acl_denies(aclp, access_mask, cred, file_uid, file_gid,
	    &explicitly_denied);

	if (must_be_owner) {
		if (file_uid != cred->cr_uid)
			denied = EPERM;
	}

	/*
	 * For VEXEC, ensure that at least one execute bit is set for
	 * non-directories. We have to check the mode here to stay
	 * consistent with execve(2). See the test in
	 * exec_check_permissions().
	 */
	acl_nfs4_sync_mode_from_acl(&file_mode, aclp);
	if (!denied && !is_directory && (accmode & VEXEC) &&
	    (file_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
		denied = EACCES;

	if (!denied)
		return (0);

	/*
	 * Access failed.  Iff it was not denied explicitly and
	 * VEXPLICIT_DENY flag was specified, allow access.
	 */
	if ((accmode & VEXPLICIT_DENY) && explicitly_denied == 0)
		return (0);

	accmode &= ~VEXPLICIT_DENY;

	/*
	 * No match.  Try to use privileges, if there are any.
	 */
	if (is_directory) {
		if ((accmode & VEXEC) && !priv_check_cred(cred, PRIV_VFS_LOOKUP))
			priv_granted |= VEXEC;
	} else {
		/*
		 * Ensure that at least one execute bit is on. Otherwise,
		 * a privileged user will always succeed, and we don't want
		 * this to happen unless the file really is executable.
		 */
		if ((accmode & VEXEC) && (file_mode &
		    (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 &&
		    !priv_check_cred(cred, PRIV_VFS_EXEC))
			priv_granted |= VEXEC;
	}

	if ((accmode & VREAD) && !priv_check_cred(cred, PRIV_VFS_READ))
		priv_granted |= VREAD;

	if ((accmode & (VWRITE | VAPPEND | VDELETE_CHILD)) &&
	    !priv_check_cred(cred, PRIV_VFS_WRITE))
		priv_granted |= (VWRITE | VAPPEND | VDELETE_CHILD);

	if ((accmode & VADMIN_PERMS) &&
	    !priv_check_cred(cred, PRIV_VFS_ADMIN))
		priv_granted |= VADMIN_PERMS;

	if ((accmode & VSTAT_PERMS) &&
	    !priv_check_cred(cred, PRIV_VFS_STAT))
		priv_granted |= VSTAT_PERMS;

	if ((accmode & priv_granted) == accmode) {
		if (privused != NULL)
			*privused = 1;

		return (0);
	}

	if (accmode & (VADMIN_PERMS | VDELETE_CHILD | VDELETE))
		denied = EPERM;
	else
		denied = EACCES;

	return (denied);
}
#endif /* _KERNEL */

static int
_acl_entry_matches(struct acl_entry *entry, acl_tag_t tag, acl_perm_t perm,
    acl_entry_type_t entry_type)
{
	if (entry->ae_tag != tag)
		return (0);

	if (entry->ae_id != ACL_UNDEFINED_ID)
		return (0);

	if (entry->ae_perm != perm)
		return (0);

	if (entry->ae_entry_type != entry_type)
		return (0);

	if (entry->ae_flags != 0)
		return (0);

	return (1);
}

static struct acl_entry *
_acl_append(struct acl *aclp, acl_tag_t tag, acl_perm_t perm,
    acl_entry_type_t entry_type)
{
	struct acl_entry *entry;

	KASSERT(aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES,
	    ("aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES"));

	entry = &(aclp->acl_entry[aclp->acl_cnt]);
	aclp->acl_cnt++;

	entry->ae_tag = tag;
	entry->ae_id = ACL_UNDEFINED_ID;
	entry->ae_perm = perm;
	entry->ae_entry_type = entry_type;
	entry->ae_flags = 0;

	return (entry);
}

static struct acl_entry *
_acl_duplicate_entry(struct acl *aclp, int entry_index)
{
	int i;

	KASSERT(aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES,
	    ("aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES"));

	for (i = aclp->acl_cnt; i > entry_index; i--)
		aclp->acl_entry[i] = aclp->acl_entry[i - 1];

	aclp->acl_cnt++;

	return (&(aclp->acl_entry[entry_index + 1]));
}

static void
acl_nfs4_sync_acl_from_mode_draft(struct acl *aclp, mode_t mode,
    int file_owner_id)
{
	int i, meets, must_append;
	struct acl_entry *entry, *copy, *previous,
	    *a1, *a2, *a3, *a4, *a5, *a6;
	mode_t amode;
	const int READ = 04;
	const int WRITE = 02;
	const int EXEC = 01;

	KASSERT(aclp->acl_cnt <= ACL_MAX_ENTRIES,
	    ("aclp->acl_cnt <= ACL_MAX_ENTRIES"));

	/*
	 * NFSv4 Minor Version 1, draft-ietf-nfsv4-minorversion1-03.txt
	 *
	 * 3.16.6.3. Applying a Mode to an Existing ACL
	 */

	/*
	 * 1. For each ACE:
	 */
	for (i = 0; i < aclp->acl_cnt; i++) {
		entry = &(aclp->acl_entry[i]);

		/*
		 * 1.1. If the type is neither ALLOW or DENY - skip.
		 */
		if (entry->ae_entry_type != ACL_ENTRY_TYPE_ALLOW &&
		    entry->ae_entry_type != ACL_ENTRY_TYPE_DENY)
			continue;

		/*
		 * 1.2. If ACL_ENTRY_INHERIT_ONLY is set - skip.
		 */
		if (entry->ae_flags & ACL_ENTRY_INHERIT_ONLY)
			continue;

		/*
		 * 1.3. If ACL_ENTRY_FILE_INHERIT or ACL_ENTRY_DIRECTORY_INHERIT
		 *      are set:
		 */
		if (entry->ae_flags &
		    (ACL_ENTRY_FILE_INHERIT | ACL_ENTRY_DIRECTORY_INHERIT)) {
			/*
			 * 1.3.1. A copy of the current ACE is made, and placed
			 *        in the ACL immediately following the current
			 *        ACE.
			 */
			copy = _acl_duplicate_entry(aclp, i);

			/*
			 * 1.3.2. In the first ACE, the flag
			 *        ACL_ENTRY_INHERIT_ONLY is set.
			 */
			entry->ae_flags |= ACL_ENTRY_INHERIT_ONLY;

			/*
			 * 1.3.3. In the second ACE, the following flags
			 *        are cleared:
			 *        ACL_ENTRY_FILE_INHERIT,
			 *        ACL_ENTRY_DIRECTORY_INHERIT,
			 *        ACL_ENTRY_NO_PROPAGATE_INHERIT.
			 */
			copy->ae_flags &= ~(ACL_ENTRY_FILE_INHERIT |
			    ACL_ENTRY_DIRECTORY_INHERIT |
			    ACL_ENTRY_NO_PROPAGATE_INHERIT);

			/*
			 * The algorithm continues on with the second ACE.
			 */
			i++;
			entry = copy;
		}

		/*
		 * 1.4. If it's owner@, group@ or everyone@ entry, clear
		 *      ACL_READ_DATA, ACL_WRITE_DATA, ACL_APPEND_DATA
		 *      and ACL_EXECUTE.  Continue to the next entry.
		 */
		if (entry->ae_tag == ACL_USER_OBJ ||
		    entry->ae_tag == ACL_GROUP_OBJ ||
		    entry->ae_tag == ACL_EVERYONE) {
			entry->ae_perm &= ~(ACL_READ_DATA | ACL_WRITE_DATA |
			    ACL_APPEND_DATA | ACL_EXECUTE);
			continue;
		}

		/*
		 * 1.5. Otherwise, if the "who" field did not match one
		 *      of OWNER@, GROUP@, EVERYONE@:
		 *
		 * 1.5.1. If the type is ALLOW, check the preceding ACE.
		 *        If it does not meet all of the following criteria:
		 */
		if (entry->ae_entry_type != ACL_ENTRY_TYPE_ALLOW)
			continue;

		meets = 0;
		if (i > 0) {
			meets = 1;
			previous = &(aclp->acl_entry[i - 1]);

			/*
			 * 1.5.1.1. The type field is DENY,
			 */
			if (previous->ae_entry_type != ACL_ENTRY_TYPE_DENY)
				meets = 0;

			/*
			 * 1.5.1.2. The "who" field is the same as the current
			 *          ACE,
			 *
			 * 1.5.1.3. The flag bit ACE4_IDENTIFIER_GROUP
			 *          is the same as it is in the current ACE,
			 *          and no other flag bits are set,
			 */
			if (previous->ae_id != entry->ae_id ||
			    previous->ae_tag != entry->ae_tag)
				meets = 0;

			if (previous->ae_flags)
				meets = 0;

			/*
			 * 1.5.1.4. The mask bits are a subset of the mask bits
			 *          of the current ACE, and are also subset of
			 *          the following: ACL_READ_DATA,
			 *          ACL_WRITE_DATA, ACL_APPEND_DATA, ACL_EXECUTE
			 */
			if (previous->ae_perm & ~(entry->ae_perm))
				meets = 0;

			if (previous->ae_perm & ~(ACL_READ_DATA |
			    ACL_WRITE_DATA | ACL_APPEND_DATA | ACL_EXECUTE))
				meets = 0;
		}

		if (!meets) {
			/*
		 	 * Then the ACE of type DENY, with a who equal
			 * to the current ACE, flag bits equal to
			 * (<current ACE flags> & <ACE_IDENTIFIER_GROUP>)
			 * and no mask bits, is prepended.
			 */
			previous = entry;
			entry = _acl_duplicate_entry(aclp, i);

			/* Adjust counter, as we've just added an entry. */
			i++;

			previous->ae_tag = entry->ae_tag;
			previous->ae_id = entry->ae_id;
			previous->ae_flags = entry->ae_flags;
			previous->ae_perm = 0;
			previous->ae_entry_type = ACL_ENTRY_TYPE_DENY;
		}

		/*
		 * 1.5.2. The following modifications are made to the prepended
		 *        ACE.  The intent is to mask the following ACE
		 *        to disallow ACL_READ_DATA, ACL_WRITE_DATA,
		 *        ACL_APPEND_DATA, or ACL_EXECUTE, based upon the group
		 *        permissions of the new mode.  As a special case,
		 *        if the ACE matches the current owner of the file,
		 *        the owner bits are used, rather than the group bits.
		 *        This is reflected in the algorithm below.
		 */
		amode = mode >> 3;

		/*
		 * If ACE4_IDENTIFIER_GROUP is not set, and the "who" field
		 * in ACE matches the owner of the file, we shift amode three
		 * more bits, in order to have the owner permission bits
		 * placed in the three low order bits of amode.
		 */
		if (entry->ae_tag == ACL_USER && entry->ae_id == file_owner_id)
			amode = amode >> 3;

		if (entry->ae_perm & ACL_READ_DATA) {
			if (amode & READ)
				previous->ae_perm &= ~ACL_READ_DATA;
			else
				previous->ae_perm |= ACL_READ_DATA;
		}

		if (entry->ae_perm & ACL_WRITE_DATA) {
			if (amode & WRITE)
				previous->ae_perm &= ~ACL_WRITE_DATA;
			else
				previous->ae_perm |= ACL_WRITE_DATA;
		}

		if (entry->ae_perm & ACL_APPEND_DATA) {
			if (amode & WRITE)
				previous->ae_perm &= ~ACL_APPEND_DATA;
			else
				previous->ae_perm |= ACL_APPEND_DATA;
		}

		if (entry->ae_perm & ACL_EXECUTE) {
			if (amode & EXEC)
				previous->ae_perm &= ~ACL_EXECUTE;
			else
				previous->ae_perm |= ACL_EXECUTE;
		}

		/*
		 * 1.5.3. If ACE4_IDENTIFIER_GROUP is set in the flags
		 *        of the ALLOW ace:
		 *
		 * XXX: This point is not there in the Falkner's draft.
		 */
		if (entry->ae_tag == ACL_GROUP &&
		    entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW) {
			mode_t extramode, ownermode;
			extramode = (mode >> 3) & 07;
			ownermode = mode >> 6;
			extramode &= ~ownermode;

			if (extramode) {
				if (extramode & READ) {
					entry->ae_perm &= ~ACL_READ_DATA;
					previous->ae_perm &= ~ACL_READ_DATA;
				}

				if (extramode & WRITE) {
					entry->ae_perm &=
					    ~(ACL_WRITE_DATA | ACL_APPEND_DATA);
					previous->ae_perm &=
					    ~(ACL_WRITE_DATA | ACL_APPEND_DATA);
				}

				if (extramode & EXEC) {
					entry->ae_perm &= ~ACL_EXECUTE;
					previous->ae_perm &= ~ACL_EXECUTE;
				}
			}
		}
	}

	/*
	 * 2. If there at least six ACEs, the final six ACEs are examined.
	 *    If they are not equal to what we want, append six ACEs.
	 */
	must_append = 0;
	if (aclp->acl_cnt < 6) {
		must_append = 1;
	} else {
		a6 = &(aclp->acl_entry[aclp->acl_cnt - 1]);
		a5 = &(aclp->acl_entry[aclp->acl_cnt - 2]);
		a4 = &(aclp->acl_entry[aclp->acl_cnt - 3]);
		a3 = &(aclp->acl_entry[aclp->acl_cnt - 4]);
		a2 = &(aclp->acl_entry[aclp->acl_cnt - 5]);
		a1 = &(aclp->acl_entry[aclp->acl_cnt - 6]);

		if (!_acl_entry_matches(a1, ACL_USER_OBJ, 0,
		    ACL_ENTRY_TYPE_DENY))
			must_append = 1;
		if (!_acl_entry_matches(a2, ACL_USER_OBJ, ACL_WRITE_ACL |
		    ACL_WRITE_OWNER | ACL_WRITE_ATTRIBUTES |
		    ACL_WRITE_NAMED_ATTRS, ACL_ENTRY_TYPE_ALLOW))
			must_append = 1;
		if (!_acl_entry_matches(a3, ACL_GROUP_OBJ, 0,
		    ACL_ENTRY_TYPE_DENY))
			must_append = 1;
		if (!_acl_entry_matches(a4, ACL_GROUP_OBJ, 0,
		    ACL_ENTRY_TYPE_ALLOW))
			must_append = 1;
		if (!_acl_entry_matches(a5, ACL_EVERYONE, ACL_WRITE_ACL |
		    ACL_WRITE_OWNER | ACL_WRITE_ATTRIBUTES |
		    ACL_WRITE_NAMED_ATTRS, ACL_ENTRY_TYPE_DENY))
			must_append = 1;
		if (!_acl_entry_matches(a6, ACL_EVERYONE, ACL_READ_ACL |
		    ACL_READ_ATTRIBUTES | ACL_READ_NAMED_ATTRS |
		    ACL_SYNCHRONIZE, ACL_ENTRY_TYPE_ALLOW))
			must_append = 1;
	}

	if (must_append) {
		KASSERT(aclp->acl_cnt + 6 <= ACL_MAX_ENTRIES,
		    ("aclp->acl_cnt <= ACL_MAX_ENTRIES"));

		a1 = _acl_append(aclp, ACL_USER_OBJ, 0, ACL_ENTRY_TYPE_DENY);
		a2 = _acl_append(aclp, ACL_USER_OBJ, ACL_WRITE_ACL |
		    ACL_WRITE_OWNER | ACL_WRITE_ATTRIBUTES |
		    ACL_WRITE_NAMED_ATTRS, ACL_ENTRY_TYPE_ALLOW);
		a3 = _acl_append(aclp, ACL_GROUP_OBJ, 0, ACL_ENTRY_TYPE_DENY);
		a4 = _acl_append(aclp, ACL_GROUP_OBJ, 0, ACL_ENTRY_TYPE_ALLOW);
		a5 = _acl_append(aclp, ACL_EVERYONE, ACL_WRITE_ACL |
		    ACL_WRITE_OWNER | ACL_WRITE_ATTRIBUTES |
		    ACL_WRITE_NAMED_ATTRS, ACL_ENTRY_TYPE_DENY);
		a6 = _acl_append(aclp, ACL_EVERYONE, ACL_READ_ACL |
		    ACL_READ_ATTRIBUTES | ACL_READ_NAMED_ATTRS |
		    ACL_SYNCHRONIZE, ACL_ENTRY_TYPE_ALLOW);

		KASSERT(a1 != NULL && a2 != NULL && a3 != NULL && a4 != NULL &&
		    a5 != NULL && a6 != NULL, ("couldn't append to ACL."));
	}

	/*
	 * 3. The final six ACEs are adjusted according to the incoming mode.
	 */
	if (mode & S_IRUSR)
		a2->ae_perm |= ACL_READ_DATA;
	else
		a1->ae_perm |= ACL_READ_DATA;
	if (mode & S_IWUSR)
		a2->ae_perm |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	else
		a1->ae_perm |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	if (mode & S_IXUSR)
		a2->ae_perm |= ACL_EXECUTE;
	else
		a1->ae_perm |= ACL_EXECUTE;

	if (mode & S_IRGRP)
		a4->ae_perm |= ACL_READ_DATA;
	else
		a3->ae_perm |= ACL_READ_DATA;
	if (mode & S_IWGRP)
		a4->ae_perm |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	else
		a3->ae_perm |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	if (mode & S_IXGRP)
		a4->ae_perm |= ACL_EXECUTE;
	else
		a3->ae_perm |= ACL_EXECUTE;

	if (mode & S_IROTH)
		a6->ae_perm |= ACL_READ_DATA;
	else
		a5->ae_perm |= ACL_READ_DATA;
	if (mode & S_IWOTH)
		a6->ae_perm |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	else
		a5->ae_perm |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	if (mode & S_IXOTH)
		a6->ae_perm |= ACL_EXECUTE;
	else
		a5->ae_perm |= ACL_EXECUTE;
}

#ifdef _KERNEL
void
acl_nfs4_sync_acl_from_mode(struct acl *aclp, mode_t mode,
    int file_owner_id)
{

	if (acl_nfs4_old_semantics)
		acl_nfs4_sync_acl_from_mode_draft(aclp, mode, file_owner_id);
	else
		acl_nfs4_trivial_from_mode(aclp, mode);
}
#endif /* _KERNEL */

void
acl_nfs4_sync_mode_from_acl(mode_t *_mode, const struct acl *aclp)
{
	int i;
	mode_t old_mode = *_mode, mode = 0, seen = 0;
	const struct acl_entry *entry;

	KASSERT(aclp->acl_cnt <= ACL_MAX_ENTRIES,
	    ("aclp->acl_cnt <= ACL_MAX_ENTRIES"));

	/*
	 * NFSv4 Minor Version 1, draft-ietf-nfsv4-minorversion1-03.txt
	 *
	 * 3.16.6.1. Recomputing mode upon SETATTR of ACL
	 */

	for (i = 0; i < aclp->acl_cnt; i++) {
		entry = &(aclp->acl_entry[i]);

		if (entry->ae_entry_type != ACL_ENTRY_TYPE_ALLOW &&
		    entry->ae_entry_type != ACL_ENTRY_TYPE_DENY)
			continue;

		if (entry->ae_flags & ACL_ENTRY_INHERIT_ONLY)
			continue;

		if (entry->ae_tag == ACL_USER_OBJ) {
			if ((entry->ae_perm & ACL_READ_DATA) &&
			    ((seen & S_IRUSR) == 0)) {
				seen |= S_IRUSR;
				if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
					mode |= S_IRUSR;
			}
			if ((entry->ae_perm & ACL_WRITE_DATA) &&
			     ((seen & S_IWUSR) == 0)) {
				seen |= S_IWUSR;
				if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
					mode |= S_IWUSR;
			}
			if ((entry->ae_perm & ACL_EXECUTE) &&
			    ((seen & S_IXUSR) == 0)) {
				seen |= S_IXUSR;
				if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
					mode |= S_IXUSR;
			}
		} else if (entry->ae_tag == ACL_GROUP_OBJ) {
			if ((entry->ae_perm & ACL_READ_DATA) &&
			    ((seen & S_IRGRP) == 0)) {
				seen |= S_IRGRP;
				if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
					mode |= S_IRGRP;
			}
			if ((entry->ae_perm & ACL_WRITE_DATA) &&
			    ((seen & S_IWGRP) == 0)) {
				seen |= S_IWGRP;
				if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
					mode |= S_IWGRP;
			}
			if ((entry->ae_perm & ACL_EXECUTE) &&
			    ((seen & S_IXGRP) == 0)) {
				seen |= S_IXGRP;
				if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
					mode |= S_IXGRP;
			}
		} else if (entry->ae_tag == ACL_EVERYONE) {
			if (entry->ae_perm & ACL_READ_DATA) {
				if ((seen & S_IRUSR) == 0) {
					seen |= S_IRUSR;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IRUSR;
				}
				if ((seen & S_IRGRP) == 0) {
					seen |= S_IRGRP;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IRGRP;
				}
				if ((seen & S_IROTH) == 0) {
					seen |= S_IROTH;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IROTH;
				}
			}
			if (entry->ae_perm & ACL_WRITE_DATA) {
				if ((seen & S_IWUSR) == 0) {
					seen |= S_IWUSR;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IWUSR;
				}
				if ((seen & S_IWGRP) == 0) {
					seen |= S_IWGRP;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IWGRP;
				}
				if ((seen & S_IWOTH) == 0) {
					seen |= S_IWOTH;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IWOTH;
				}
			}
			if (entry->ae_perm & ACL_EXECUTE) {
				if ((seen & S_IXUSR) == 0) {
					seen |= S_IXUSR;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IXUSR;
				}
				if ((seen & S_IXGRP) == 0) {
					seen |= S_IXGRP;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IXGRP;
				}
				if ((seen & S_IXOTH) == 0) {
					seen |= S_IXOTH;
					if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
						mode |= S_IXOTH;
				}
			}
		}
	}

	*_mode = mode | (old_mode & ACL_PRESERVE_MASK);
}

#ifdef _KERNEL
/*
 * Calculate inherited ACL in a manner compatible with NFSv4 Minor Version 1,
 * draft-ietf-nfsv4-minorversion1-03.txt.
 */
static void		
acl_nfs4_compute_inherited_acl_draft(const struct acl *parent_aclp,
    struct acl *child_aclp, mode_t mode, int file_owner_id,
    int is_directory)
{
	int i, flags;
	const struct acl_entry *parent_entry;
	struct acl_entry *entry, *copy;

	KASSERT(child_aclp->acl_cnt == 0, ("child_aclp->acl_cnt == 0"));
	KASSERT(parent_aclp->acl_cnt <= ACL_MAX_ENTRIES,
	    ("parent_aclp->acl_cnt <= ACL_MAX_ENTRIES"));

	/*
	 * NFSv4 Minor Version 1, draft-ietf-nfsv4-minorversion1-03.txt
	 *
	 * 3.16.6.2. Applying the mode given to CREATE or OPEN
	 *           to an inherited ACL
	 */

	/*
	 * 1. Form an ACL that is the concatenation of all inheritable ACEs.
	 */
	for (i = 0; i < parent_aclp->acl_cnt; i++) {
		parent_entry = &(parent_aclp->acl_entry[i]);
		flags = parent_entry->ae_flags;

		/*
		 * Entry is not inheritable at all.
		 */
		if ((flags & (ACL_ENTRY_DIRECTORY_INHERIT |
		    ACL_ENTRY_FILE_INHERIT)) == 0)
			continue;

		/*
		 * We're creating a file, but entry is not inheritable
		 * by files.
		 */
		if (!is_directory && (flags & ACL_ENTRY_FILE_INHERIT) == 0)
			continue;

		/*
		 * Entry is inheritable only by files, but has NO_PROPAGATE
		 * flag set, and we're creating a directory, so it wouldn't
		 * propagate to any file in that directory anyway.
		 */
		if (is_directory &&
		    (flags & ACL_ENTRY_DIRECTORY_INHERIT) == 0 &&
		    (flags & ACL_ENTRY_NO_PROPAGATE_INHERIT))
			continue;

		KASSERT(child_aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES,
		    ("child_aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES"));
		child_aclp->acl_entry[child_aclp->acl_cnt] = *parent_entry;
		child_aclp->acl_cnt++;
	}

	/*
	 * 2. For each entry in the new ACL, adjust its flags, possibly
	 *    creating two entries in place of one.
	 */
	for (i = 0; i < child_aclp->acl_cnt; i++) {
		entry = &(child_aclp->acl_entry[i]);

		/*
		 * This is not in the specification, but SunOS
		 * apparently does that.
		 */
		if (((entry->ae_flags & ACL_ENTRY_NO_PROPAGATE_INHERIT) ||
		    !is_directory) &&
		    entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
			entry->ae_perm &= ~(ACL_WRITE_ACL | ACL_WRITE_OWNER);

		/*
		 * 2.A. If the ACL_ENTRY_NO_PROPAGATE_INHERIT is set, or if the object
		 *      being created is not a directory, then clear the
		 *      following flags: ACL_ENTRY_NO_PROPAGATE_INHERIT,
		 *      ACL_ENTRY_FILE_INHERIT, ACL_ENTRY_DIRECTORY_INHERIT,
		 *      ACL_ENTRY_INHERIT_ONLY.
		 */
		if (entry->ae_flags & ACL_ENTRY_NO_PROPAGATE_INHERIT ||
		    !is_directory) {
			entry->ae_flags &= ~(ACL_ENTRY_NO_PROPAGATE_INHERIT |
			ACL_ENTRY_FILE_INHERIT | ACL_ENTRY_DIRECTORY_INHERIT |
			ACL_ENTRY_INHERIT_ONLY);

			/*
			 * Continue on to the next ACE.
			 */
			continue;
		}

		/*
		 * 2.B. If the object is a directory and ACL_ENTRY_FILE_INHERIT
		 *      is set, but ACL_ENTRY_NO_PROPAGATE_INHERIT is not set, ensure
		 *      that ACL_ENTRY_INHERIT_ONLY is set.  Continue to the
		 *      next ACE.  Otherwise...
		 */
		/*
		 * XXX: Read it again and make sure what does the "otherwise"
		 *      apply to.
		 */
		if (is_directory &&
		    (entry->ae_flags & ACL_ENTRY_FILE_INHERIT) &&
		    ((entry->ae_flags & ACL_ENTRY_DIRECTORY_INHERIT) == 0)) {
			entry->ae_flags |= ACL_ENTRY_INHERIT_ONLY;
			continue;
		}

		/*
		 * 2.C. If the type of the ACE is neither ALLOW nor deny,
		 *      then continue.
		 */
		if (entry->ae_entry_type != ACL_ENTRY_TYPE_ALLOW &&
		    entry->ae_entry_type != ACL_ENTRY_TYPE_DENY)
			continue;

		/*
		 * 2.D. Copy the original ACE into a second, adjacent ACE.
		 */
		copy = _acl_duplicate_entry(child_aclp, i);

		/*
		 * 2.E. On the first ACE, ensure that ACL_ENTRY_INHERIT_ONLY
		 *      is set.
		 */
		entry->ae_flags |= ACL_ENTRY_INHERIT_ONLY;

		/*
		 * 2.F. On the second ACE, clear the following flags:
		 *      ACL_ENTRY_NO_PROPAGATE_INHERIT, ACL_ENTRY_FILE_INHERIT,
		 *      ACL_ENTRY_DIRECTORY_INHERIT, ACL_ENTRY_INHERIT_ONLY.
		 */
		copy->ae_flags &= ~(ACL_ENTRY_NO_PROPAGATE_INHERIT |
		    ACL_ENTRY_FILE_INHERIT | ACL_ENTRY_DIRECTORY_INHERIT |
		    ACL_ENTRY_INHERIT_ONLY);

		/*
		 * 2.G. On the second ACE, if the type is ALLOW,
		 *      an implementation MAY clear the following
		 *      mask bits: ACL_WRITE_ACL, ACL_WRITE_OWNER.
		 */
		if (copy->ae_entry_type == ACL_ENTRY_TYPE_ALLOW)
			copy->ae_perm &= ~(ACL_WRITE_ACL | ACL_WRITE_OWNER);

		/*
		 * Increment the counter to skip the copied entry.
		 */
		i++;
	}

	/*
	 * 3. To ensure that the mode is honored, apply the algorithm describe
	 *    in Section 2.16.6.3, using the mode that is to be used for file
	 *    creation.
	 */
	acl_nfs4_sync_acl_from_mode(child_aclp, mode, file_owner_id);
}
#endif /* _KERNEL */

/*
 * Populate the ACL with entries inherited from parent_aclp.
 */
static void		
acl_nfs4_inherit_entries(const struct acl *parent_aclp,
    struct acl *child_aclp, mode_t mode, int file_owner_id,
    int is_directory)
{
	int i, flags, tag;
	const struct acl_entry *parent_entry;
	struct acl_entry *entry;

	KASSERT(parent_aclp->acl_cnt <= ACL_MAX_ENTRIES,
	    ("parent_aclp->acl_cnt <= ACL_MAX_ENTRIES"));

	for (i = 0; i < parent_aclp->acl_cnt; i++) {
		parent_entry = &(parent_aclp->acl_entry[i]);
		flags = parent_entry->ae_flags;
		tag = parent_entry->ae_tag;

		/*
		 * Don't inherit owner@, group@, or everyone@ entries.
		 */
		if (tag == ACL_USER_OBJ || tag == ACL_GROUP_OBJ ||
		    tag == ACL_EVERYONE)
			continue;

		/*
		 * Entry is not inheritable at all.
		 */
		if ((flags & (ACL_ENTRY_DIRECTORY_INHERIT |
		    ACL_ENTRY_FILE_INHERIT)) == 0)
			continue;

		/*
		 * We're creating a file, but entry is not inheritable
		 * by files.
		 */
		if (!is_directory && (flags & ACL_ENTRY_FILE_INHERIT) == 0)
			continue;

		/*
		 * Entry is inheritable only by files, but has NO_PROPAGATE
		 * flag set, and we're creating a directory, so it wouldn't
		 * propagate to any file in that directory anyway.
		 */
		if (is_directory &&
		    (flags & ACL_ENTRY_DIRECTORY_INHERIT) == 0 &&
		    (flags & ACL_ENTRY_NO_PROPAGATE_INHERIT))
			continue;

		/*
		 * Entry qualifies for being inherited.
		 */
		KASSERT(child_aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES,
		    ("child_aclp->acl_cnt + 1 <= ACL_MAX_ENTRIES"));
		entry = &(child_aclp->acl_entry[child_aclp->acl_cnt]);
		*entry = *parent_entry;
		child_aclp->acl_cnt++;

		entry->ae_flags &= ~ACL_ENTRY_INHERIT_ONLY;
		entry->ae_flags |= ACL_ENTRY_INHERITED;

		/*
		 * If the type of the ACE is neither ALLOW nor DENY,
		 * then leave it as it is and proceed to the next one.
		 */
		if (entry->ae_entry_type != ACL_ENTRY_TYPE_ALLOW &&
		    entry->ae_entry_type != ACL_ENTRY_TYPE_DENY)
			continue;

		/*
		 * If the ACL_ENTRY_NO_PROPAGATE_INHERIT is set, or if
		 * the object being created is not a directory, then clear
		 * the following flags: ACL_ENTRY_NO_PROPAGATE_INHERIT,
		 * ACL_ENTRY_FILE_INHERIT, ACL_ENTRY_DIRECTORY_INHERIT,
		 * ACL_ENTRY_INHERIT_ONLY.
		 */
		if (entry->ae_flags & ACL_ENTRY_NO_PROPAGATE_INHERIT ||
		    !is_directory) {
			entry->ae_flags &= ~(ACL_ENTRY_NO_PROPAGATE_INHERIT |
			ACL_ENTRY_FILE_INHERIT | ACL_ENTRY_DIRECTORY_INHERIT |
			ACL_ENTRY_INHERIT_ONLY);
		}

		/*
		 * If the object is a directory and ACL_ENTRY_FILE_INHERIT
		 * is set, but ACL_ENTRY_DIRECTORY_INHERIT is not set, ensure
		 * that ACL_ENTRY_INHERIT_ONLY is set.
		 */
		if (is_directory &&
		    (entry->ae_flags & ACL_ENTRY_FILE_INHERIT) &&
		    ((entry->ae_flags & ACL_ENTRY_DIRECTORY_INHERIT) == 0)) {
			entry->ae_flags |= ACL_ENTRY_INHERIT_ONLY;
		}

		if (entry->ae_entry_type == ACL_ENTRY_TYPE_ALLOW &&
		    (entry->ae_flags & ACL_ENTRY_INHERIT_ONLY) == 0) {
			/*
			 * Some permissions must never be inherited.
			 */
			entry->ae_perm &= ~(ACL_WRITE_ACL | ACL_WRITE_OWNER |
			    ACL_WRITE_NAMED_ATTRS | ACL_WRITE_ATTRIBUTES);

			/*
			 * Others must be masked according to the file mode.
			 */
			if ((mode & S_IRGRP) == 0)
				entry->ae_perm &= ~ACL_READ_DATA;
			if ((mode & S_IWGRP) == 0)
				entry->ae_perm &=
				    ~(ACL_WRITE_DATA | ACL_APPEND_DATA);
			if ((mode & S_IXGRP) == 0)
				entry->ae_perm &= ~ACL_EXECUTE;
		}
	}
}

/*
 * Calculate inherited ACL in a manner compatible with PSARC/2010/029.
 * It's also being used to calculate a trivial ACL, by inheriting from
 * a NULL ACL.
 */
static void		
acl_nfs4_compute_inherited_acl_psarc(const struct acl *parent_aclp,
    struct acl *aclp, mode_t mode, int file_owner_id, int is_directory)
{
	acl_perm_t user_allow_first = 0, user_deny = 0, group_deny = 0;
	acl_perm_t user_allow, group_allow, everyone_allow;

	KASSERT(aclp->acl_cnt == 0, ("aclp->acl_cnt == 0"));

	user_allow = group_allow = everyone_allow = ACL_READ_ACL |
	    ACL_READ_ATTRIBUTES | ACL_READ_NAMED_ATTRS | ACL_SYNCHRONIZE;
	user_allow |= ACL_WRITE_ACL | ACL_WRITE_OWNER | ACL_WRITE_ATTRIBUTES |
	    ACL_WRITE_NAMED_ATTRS;

	if (mode & S_IRUSR)
		user_allow |= ACL_READ_DATA;
	if (mode & S_IWUSR)
		user_allow |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	if (mode & S_IXUSR)
		user_allow |= ACL_EXECUTE;

	if (mode & S_IRGRP)
		group_allow |= ACL_READ_DATA;
	if (mode & S_IWGRP)
		group_allow |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	if (mode & S_IXGRP)
		group_allow |= ACL_EXECUTE;

	if (mode & S_IROTH)
		everyone_allow |= ACL_READ_DATA;
	if (mode & S_IWOTH)
		everyone_allow |= (ACL_WRITE_DATA | ACL_APPEND_DATA);
	if (mode & S_IXOTH)
		everyone_allow |= ACL_EXECUTE;

	user_deny = ((group_allow | everyone_allow) & ~user_allow);
	group_deny = everyone_allow & ~group_allow;
	user_allow_first = group_deny & ~user_deny;

	if (user_allow_first != 0)
		_acl_append(aclp, ACL_USER_OBJ, user_allow_first,
		    ACL_ENTRY_TYPE_ALLOW);
	if (user_deny != 0)
		_acl_append(aclp, ACL_USER_OBJ, user_deny,
		    ACL_ENTRY_TYPE_DENY);
	if (group_deny != 0)
		_acl_append(aclp, ACL_GROUP_OBJ, group_deny,
		    ACL_ENTRY_TYPE_DENY);

	if (parent_aclp != NULL)
		acl_nfs4_inherit_entries(parent_aclp, aclp, mode,
		    file_owner_id, is_directory);

	_acl_append(aclp, ACL_USER_OBJ, user_allow, ACL_ENTRY_TYPE_ALLOW);
	_acl_append(aclp, ACL_GROUP_OBJ, group_allow, ACL_ENTRY_TYPE_ALLOW);
	_acl_append(aclp, ACL_EVERYONE, everyone_allow, ACL_ENTRY_TYPE_ALLOW);
}

#ifdef _KERNEL
void		
acl_nfs4_compute_inherited_acl(const struct acl *parent_aclp,
    struct acl *child_aclp, mode_t mode, int file_owner_id,
    int is_directory)
{

	if (acl_nfs4_old_semantics)
		acl_nfs4_compute_inherited_acl_draft(parent_aclp, child_aclp,
		    mode, file_owner_id, is_directory);
	else
		acl_nfs4_compute_inherited_acl_psarc(parent_aclp, child_aclp,
		    mode, file_owner_id, is_directory);
}
#endif /* _KERNEL */

/*
 * Calculate trivial ACL in a manner compatible with PSARC/2010/029.
 * Note that this results in an ACL different from (but semantically
 * equal to) the "canonical six" trivial ACL computed using algorithm
 * described in draft-ietf-nfsv4-minorversion1-03.txt, 3.16.6.2.
 */
static void
acl_nfs4_trivial_from_mode(struct acl *aclp, mode_t mode)
{

	aclp->acl_cnt = 0;
	acl_nfs4_compute_inherited_acl_psarc(NULL, aclp, mode, -1, -1);
}

#ifndef _KERNEL
/*
 * This routine is used by libc to implement acl_strip_np(3)
 * and acl_is_trivial_np(3).
 */
void
acl_nfs4_trivial_from_mode_libc(struct acl *aclp, int mode, int canonical_six)
{

	aclp->acl_cnt = 0;
	if (canonical_six)
		acl_nfs4_sync_acl_from_mode_draft(aclp, mode, -1);
	else
		acl_nfs4_trivial_from_mode(aclp, mode);
}
#endif /* !_KERNEL */

#ifdef _KERNEL
static int
_acls_are_equal(const struct acl *a, const struct acl *b)
{
	int i;
	const struct acl_entry *entrya, *entryb;

	if (a->acl_cnt != b->acl_cnt)
		return (0);

	for (i = 0; i < b->acl_cnt; i++) {
		entrya = &(a->acl_entry[i]);
		entryb = &(b->acl_entry[i]);

		if (entrya->ae_tag != entryb->ae_tag ||
		    entrya->ae_id != entryb->ae_id ||
		    entrya->ae_perm != entryb->ae_perm ||
		    entrya->ae_entry_type != entryb->ae_entry_type ||
		    entrya->ae_flags != entryb->ae_flags)
			return (0);
	}

	return (1);
}

/*
 * This routine is used to determine whether to remove extended attribute
 * that stores ACL contents.
 */
int
acl_nfs4_is_trivial(const struct acl *aclp, int file_owner_id)
{
	int trivial;
	mode_t tmpmode = 0;
	struct acl *tmpaclp;

	if (aclp->acl_cnt > 6)
		return (0);

	/*
	 * Compute the mode from the ACL, then compute new ACL from that mode.
	 * If the ACLs are identical, then the ACL is trivial.
	 *
	 * XXX: I guess there is a faster way to do this.  However, even
	 *      this slow implementation significantly speeds things up
	 *      for files that don't have non-trivial ACLs - it's critical
	 *      for performance to not use EA when they are not needed.
	 *
	 * First try the PSARC/2010/029 semantics.
	 */
	tmpaclp = acl_alloc(M_WAITOK | M_ZERO);
	acl_nfs4_sync_mode_from_acl(&tmpmode, aclp);
	acl_nfs4_trivial_from_mode(tmpaclp, tmpmode);
	trivial = _acls_are_equal(aclp, tmpaclp);
	if (trivial) {
		acl_free(tmpaclp);
		return (trivial);
	}

	/*
	 * Check if it's a draft-ietf-nfsv4-minorversion1-03.txt trivial ACL.
	 */
	tmpaclp->acl_cnt = 0;
	acl_nfs4_sync_acl_from_mode_draft(tmpaclp, tmpmode, file_owner_id);
	trivial = _acls_are_equal(aclp, tmpaclp);
	acl_free(tmpaclp);

	return (trivial);
}
#endif /* _KERNEL */

int
acl_nfs4_check(const struct acl *aclp, int is_directory)
{
	int i;
	const struct acl_entry *entry;

	/*
	 * The spec doesn't seem to say anything about ACL validity.
	 * It seems there is not much to do here.  There is even no need
	 * to count "owner@" or "everyone@" (ACL_USER_OBJ and ACL_EVERYONE)
	 * entries, as there can be several of them and that's perfectly
	 * valid.  There can be none of them too.  Really.
	 */

	if (aclp->acl_cnt > ACL_MAX_ENTRIES || aclp->acl_cnt <= 0)
		return (EINVAL);

	for (i = 0; i < aclp->acl_cnt; i++) {
		entry = &(aclp->acl_entry[i]);

		switch (entry->ae_tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_EVERYONE:
			if (entry->ae_id != ACL_UNDEFINED_ID)
				return (EINVAL);
			break;

		case ACL_USER:
		case ACL_GROUP:
			if (entry->ae_id == ACL_UNDEFINED_ID)
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}

		if ((entry->ae_perm | ACL_NFS4_PERM_BITS) != ACL_NFS4_PERM_BITS)
			return (EINVAL);

		/*
		 * Disallow ACL_ENTRY_TYPE_AUDIT and ACL_ENTRY_TYPE_ALARM for now.
		 */
		if (entry->ae_entry_type != ACL_ENTRY_TYPE_ALLOW &&
		    entry->ae_entry_type != ACL_ENTRY_TYPE_DENY)
			return (EINVAL);

		if ((entry->ae_flags | ACL_FLAGS_BITS) != ACL_FLAGS_BITS)
			return (EINVAL);

		/* Disallow unimplemented flags. */
		if (entry->ae_flags & (ACL_ENTRY_SUCCESSFUL_ACCESS |
		    ACL_ENTRY_FAILED_ACCESS))
			return (EINVAL);

		/* Disallow flags not allowed for ordinary files. */
		if (!is_directory) {
			if (entry->ae_flags & (ACL_ENTRY_FILE_INHERIT |
			    ACL_ENTRY_DIRECTORY_INHERIT |
			    ACL_ENTRY_NO_PROPAGATE_INHERIT | ACL_ENTRY_INHERIT_ONLY))
				return (EINVAL);
		}
	}

	return (0);
}

#ifdef	_KERNEL
static int
acl_nfs4_modload(module_t module, int what, void *arg)
{
	int ret;

	ret = 0;

	switch (what) {
	case MOD_LOAD:
	case MOD_SHUTDOWN:
		break;

	case MOD_QUIESCE:
		/* XXX TODO */
		ret = 0;
		break;

	case MOD_UNLOAD:
		/* XXX TODO */
		ret = 0;
		break;
	default:
		ret = EINVAL;
		break;
	}

	return (ret);
}

static moduledata_t acl_nfs4_mod = {
	"acl_nfs4",
	acl_nfs4_modload,
	NULL
};

/*
 * XXX TODO: which subsystem, order?
 */
DECLARE_MODULE(acl_nfs4, acl_nfs4_mod, SI_SUB_VFS, SI_ORDER_FIRST);
MODULE_VERSION(acl_nfs4, 1);
#endif	/* _KERNEL */
