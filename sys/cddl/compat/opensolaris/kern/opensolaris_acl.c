/*-
 * Copyright (c) 2008, 2009 Edward Tomasz Napierała <trasz@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/zfs_acl.h>
#include <sys/acl.h>

struct zfs2bsd {
	uint32_t	zb_zfs;
	int		zb_bsd;
};

struct zfs2bsd perms[] = {{ACE_READ_DATA, ACL_READ_DATA},
			{ACE_WRITE_DATA, ACL_WRITE_DATA},
			{ACE_EXECUTE, ACL_EXECUTE},
			{ACE_APPEND_DATA, ACL_APPEND_DATA},
			{ACE_DELETE_CHILD, ACL_DELETE_CHILD},
			{ACE_DELETE, ACL_DELETE},
			{ACE_READ_ATTRIBUTES, ACL_READ_ATTRIBUTES},
			{ACE_WRITE_ATTRIBUTES, ACL_WRITE_ATTRIBUTES},
			{ACE_READ_NAMED_ATTRS, ACL_READ_NAMED_ATTRS},
			{ACE_WRITE_NAMED_ATTRS, ACL_WRITE_NAMED_ATTRS},
			{ACE_READ_ACL, ACL_READ_ACL},
			{ACE_WRITE_ACL, ACL_WRITE_ACL},
			{ACE_WRITE_OWNER, ACL_WRITE_OWNER},
			{ACE_SYNCHRONIZE, ACL_SYNCHRONIZE},
			{0, 0}};

struct zfs2bsd flags[] = {{ACE_FILE_INHERIT_ACE,
			    ACL_ENTRY_FILE_INHERIT},
			{ACE_DIRECTORY_INHERIT_ACE,
			    ACL_ENTRY_DIRECTORY_INHERIT},
			{ACE_NO_PROPAGATE_INHERIT_ACE,
			    ACL_ENTRY_NO_PROPAGATE_INHERIT},
			{ACE_INHERIT_ONLY_ACE,
			    ACL_ENTRY_INHERIT_ONLY},
			{ACE_INHERITED_ACE,
			    ACL_ENTRY_INHERITED},
			{ACE_SUCCESSFUL_ACCESS_ACE_FLAG,
			    ACL_ENTRY_SUCCESSFUL_ACCESS},
			{ACE_FAILED_ACCESS_ACE_FLAG,
			    ACL_ENTRY_FAILED_ACCESS},
			{0, 0}};

static int
_bsd_from_zfs(uint32_t zfs, const struct zfs2bsd *table)
{
	const struct zfs2bsd *tmp;
	int bsd = 0;

	for (tmp = table; tmp->zb_zfs != 0; tmp++) {
		if (zfs & tmp->zb_zfs)
			bsd |= tmp->zb_bsd;
	}

	return (bsd);
}

static uint32_t
_zfs_from_bsd(int bsd, const struct zfs2bsd *table)
{
	const struct zfs2bsd *tmp;
	uint32_t zfs = 0;

	for (tmp = table; tmp->zb_bsd != 0; tmp++) {
		if (bsd & tmp->zb_bsd)
			zfs |= tmp->zb_zfs;
	}

	return (zfs);
}

int
acl_from_aces(struct acl *aclp, const ace_t *aces, int nentries)
{
	int i;
	struct acl_entry *entry;
	const ace_t *ace;

	if (nentries < 1) {
		printf("acl_from_aces: empty ZFS ACL; returning EINVAL.\n");
		return (EINVAL);
	}

	if (nentries > ACL_MAX_ENTRIES) {
		/*
		 * I believe it may happen only when moving a pool
		 * from SunOS to FreeBSD.
		 */
		printf("acl_from_aces: ZFS ACL too big to fit "
		    "into 'struct acl'; returning EINVAL.\n");
		return (EINVAL);
	}

	bzero(aclp, sizeof(*aclp));
	aclp->acl_maxcnt = ACL_MAX_ENTRIES;
	aclp->acl_cnt = nentries;

	for (i = 0; i < nentries; i++) {
		entry = &(aclp->acl_entry[i]);
		ace = &(aces[i]);

		if (ace->a_flags & ACE_OWNER)
			entry->ae_tag = ACL_USER_OBJ;
		else if (ace->a_flags & ACE_GROUP)
			entry->ae_tag = ACL_GROUP_OBJ;
		else if (ace->a_flags & ACE_EVERYONE)
			entry->ae_tag = ACL_EVERYONE;
		else if (ace->a_flags & ACE_IDENTIFIER_GROUP)
			entry->ae_tag = ACL_GROUP;
		else
			entry->ae_tag = ACL_USER;

		if (entry->ae_tag == ACL_USER || entry->ae_tag == ACL_GROUP)
			entry->ae_id = ace->a_who;
		else
			entry->ae_id = ACL_UNDEFINED_ID;

		entry->ae_perm = _bsd_from_zfs(ace->a_access_mask, perms);
		entry->ae_flags = _bsd_from_zfs(ace->a_flags, flags);

		switch (ace->a_type) {
		case ACE_ACCESS_ALLOWED_ACE_TYPE:
			entry->ae_entry_type = ACL_ENTRY_TYPE_ALLOW;
			break;
		case ACE_ACCESS_DENIED_ACE_TYPE:
			entry->ae_entry_type = ACL_ENTRY_TYPE_DENY;
			break;
		case ACE_SYSTEM_AUDIT_ACE_TYPE:
			entry->ae_entry_type = ACL_ENTRY_TYPE_AUDIT;
			break;
		case ACE_SYSTEM_ALARM_ACE_TYPE:
			entry->ae_entry_type = ACL_ENTRY_TYPE_ALARM;
			break;
		default:
			panic("acl_from_aces: a_type is 0x%x", ace->a_type);
		}
	}

	return (0);
}

void
aces_from_acl(ace_t *aces, int *nentries, const struct acl *aclp)
{
	int i;
	const struct acl_entry *entry;
	ace_t *ace;

	bzero(aces, sizeof(*aces) * aclp->acl_cnt);

	*nentries = aclp->acl_cnt;

	for (i = 0; i < aclp->acl_cnt; i++) {
		entry = &(aclp->acl_entry[i]);
		ace = &(aces[i]);

		ace->a_who = entry->ae_id;

		if (entry->ae_tag == ACL_USER_OBJ)
			ace->a_flags = ACE_OWNER;
		else if (entry->ae_tag == ACL_GROUP_OBJ)
			ace->a_flags = (ACE_GROUP | ACE_IDENTIFIER_GROUP);
		else if (entry->ae_tag == ACL_GROUP)
			ace->a_flags = ACE_IDENTIFIER_GROUP;
		else if (entry->ae_tag == ACL_EVERYONE)
			ace->a_flags = ACE_EVERYONE;
		else /* ACL_USER */
			ace->a_flags = 0;

		ace->a_access_mask = _zfs_from_bsd(entry->ae_perm, perms);
		ace->a_flags |= _zfs_from_bsd(entry->ae_flags, flags);

		switch (entry->ae_entry_type) {
		case ACL_ENTRY_TYPE_ALLOW:
			ace->a_type = ACE_ACCESS_ALLOWED_ACE_TYPE;
			break;
		case ACL_ENTRY_TYPE_DENY:
			ace->a_type = ACE_ACCESS_DENIED_ACE_TYPE;
			break;
		case ACL_ENTRY_TYPE_ALARM:
			ace->a_type = ACE_SYSTEM_ALARM_ACE_TYPE;
			break;
		case ACL_ENTRY_TYPE_AUDIT:
			ace->a_type = ACE_SYSTEM_AUDIT_ACE_TYPE;
			break;
		default:
			panic("aces_from_acl: ae_entry_type is 0x%x", entry->ae_entry_type);
		}
	}
}
