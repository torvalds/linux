/*
 *   fs/cifs/cifsacl.c
 *
 *   Copyright (C) International Business Machines  Corp., 2007
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
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsacl.h"
#include "cifsproto.h"
#include "cifs_debug.h"


#ifdef CONFIG_CIFS_EXPERIMENTAL

static struct cifs_wksid wksidarr[NUM_WK_SIDS] = {
	{{1, 0, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} }, "null user"},
	{{1, 1, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0} }, "nobody"},
	{{1, 1, {0, 0, 0, 0, 0, 5}, {cpu_to_le32(11), 0, 0, 0, 0} }, "net-users"},
	{{1, 1, {0, 0, 0, 0, 0, 5}, {cpu_to_le32(18), 0, 0, 0, 0} }, "sys"},
	{{1, 2, {0, 0, 0, 0, 0, 5}, {cpu_to_le32(32), cpu_to_le32(544), 0, 0, 0} }, "root"},
	{{1, 2, {0, 0, 0, 0, 0, 5}, {cpu_to_le32(32), cpu_to_le32(545), 0, 0, 0} }, "users"},
	{{1, 2, {0, 0, 0, 0, 0, 5}, {cpu_to_le32(32), cpu_to_le32(546), 0, 0, 0} }, "guest"} }
;


/* security id for everyone */
static const struct cifs_sid sid_everyone =
		{1, 1, {0, 0, 0, 0, 0, 0}, {} };
/* group users */
static const struct cifs_sid sid_user =
		{1, 2 , {0, 0, 0, 0, 0, 5}, {} };


int match_sid(struct cifs_sid *ctsid)
{
	int i, j;
	int num_subauth, num_sat, num_saw;
	struct cifs_sid *cwsid;

	if (!ctsid)
		return (-1);

	for (i = 0; i < NUM_WK_SIDS; ++i) {
		cwsid = &(wksidarr[i].cifssid);

		/* compare the revision */
		if (ctsid->revision != cwsid->revision)
			continue;

		/* compare all of the six auth values */
		for (j = 0; j < 6; ++j) {
			if (ctsid->authority[j] != cwsid->authority[j])
				break;
		}
		if (j < 6)
			continue; /* all of the auth values did not match */

		/* compare all of the subauth values if any */
		num_sat = ctsid->num_subauth;
		num_saw = cwsid->num_subauth;
		num_subauth = num_sat < num_saw ? num_sat : num_saw;
		if (num_subauth) {
			for (j = 0; j < num_subauth; ++j) {
				if (ctsid->sub_auth[j] != cwsid->sub_auth[j])
					break;
			}
			if (j < num_subauth)
				continue; /* all sub_auth values do not match */
		}

		cFYI(1, ("matching sid: %s\n", wksidarr[i].sidname));
		return (0); /* sids compare/match */
	}

	cFYI(1, ("No matching sid"));
	return (-1);
}

/* if the two SIDs (roughly equivalent to a UUID for a user or group) are
   the same returns 1, if they do not match returns 0 */
int compare_sids(const struct cifs_sid *ctsid, const struct cifs_sid *cwsid)
{
	int i;
	int num_subauth, num_sat, num_saw;

	if ((!ctsid) || (!cwsid))
		return (0);

	/* compare the revision */
	if (ctsid->revision != cwsid->revision)
		return (0);

	/* compare all of the six auth values */
	for (i = 0; i < 6; ++i) {
		if (ctsid->authority[i] != cwsid->authority[i])
			return (0);
	}

	/* compare all of the subauth values if any */
	num_sat = ctsid->num_subauth;
	num_saw = cwsid->num_subauth;
	num_subauth = num_sat < num_saw ? num_sat : num_saw;
	if (num_subauth) {
		for (i = 0; i < num_subauth; ++i) {
			if (ctsid->sub_auth[i] != cwsid->sub_auth[i])
				return (0);
		}
	}

	return (1); /* sids compare/match */
}

/*
   change posix mode to reflect permissions
   pmode is the existing mode (we only want to overwrite part of this
   bits to set can be: S_IRWXU, S_IRWXG or S_IRWXO ie 00700 or 00070 or 00007
*/
static void access_flags_to_mode(__u32 ace_flags, umode_t *pmode,
				 umode_t bits_to_set)
{

	*pmode &= ~bits_to_set;

	if (ace_flags & GENERIC_ALL) {
		*pmode |= (S_IRWXUGO & bits_to_set);
#ifdef CONFIG_CIFS_DEBUG2
		cFYI(1, ("all perms"));
#endif
		return;
	}
	if ((ace_flags & GENERIC_WRITE) || (ace_flags & FILE_WRITE_RIGHTS))
		*pmode |= (S_IWUGO & bits_to_set);
	if ((ace_flags & GENERIC_READ) || (ace_flags & FILE_READ_RIGHTS))
		*pmode |= (S_IRUGO & bits_to_set);
	if ((ace_flags & GENERIC_EXECUTE) || (ace_flags & FILE_EXEC_RIGHTS))
		*pmode |= (S_IXUGO & bits_to_set);

#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("access flags 0x%x mode now 0x%x", ace_flags, *pmode);
#endif
	return;
}

/* Translate the CIFS ACL (simlar to NTFS ACL) for a file into mode bits */

void acl_to_uid_mode(struct inode *inode, const char *path)
{
	struct cifsFileInfo *open_file;
	int unlock_file = FALSE;
	int xid;
	int rc = -EIO;
	__u16 fid;
	struct super_block *sb;
	struct cifs_sb_info *cifs_sb;

	cFYI(1, ("get mode from ACL for %s", path));
	
	if (inode == NULL)
		return;

	xid = GetXid();
	open_file = find_readable_file(CIFS_I(inode));
	if (open_file) {
		unlock_file = TRUE;
		fid = open_file->netfid;
	} else {
		int oplock = FALSE;
		/* open file */
		sb = inode->i_sb;
		if (sb == NULL) {
			FreeXid(xid);
			return;
		}
		cifs_sb = CIFS_SB(sb);
		rc = CIFSSMBOpen(xid, cifs_sb->tcon, path, FILE_OPEN,
				GENERIC_READ, 0, &fid, &oplock, NULL,
				cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc != 0) {
			cERROR(1, ("Unable to open file to get ACL"));
			FreeXid(xid);
			return;
		}
	}

	/*   rc = CIFSSMBGetCIFSACL(xid, cifs_sb->tcon, fid, pntsd, acllen,
				    ACL_TYPE_ACCESS); */

	if (unlock_file == TRUE)
		atomic_dec(&open_file->wrtPending);
	else
		CIFSSMBClose(xid, cifs_sb->tcon, fid);

/* parse ACEs e.g.
	rc = parse_sec_desc(pntsd, acllen, inode);
*/

	FreeXid(xid);
	return;
}


static void parse_ace(struct cifs_ace *pace, char *end_of_acl)
{
	int num_subauth;

	/* validate that we do not go past end of acl */

	if (le16_to_cpu(pace->size) < 16) {
		cERROR(1, ("ACE too small, %d", le16_to_cpu(pace->size)));
		return;
	}

	if (end_of_acl < (char *)pace + le16_to_cpu(pace->size)) {
		cERROR(1, ("ACL too small to parse ACE"));
		return;
	}

	num_subauth = pace->sid.num_subauth;
	if (num_subauth) {
#ifdef CONFIG_CIFS_DEBUG2
		int i;
		cFYI(1, ("ACE revision %d num_auth %d type %d flags %d size %d",
			pace->sid.revision, pace->sid.num_subauth, pace->type,
			pace->flags, pace->size));
		for (i = 0; i < num_subauth; ++i) {
			cFYI(1, ("ACE sub_auth[%d]: 0x%x", i,
				le32_to_cpu(pace->sid.sub_auth[i])));
		}

		/* BB add length check to make sure that we do not have huge
			num auths and therefore go off the end */
#endif
	}

	return;
}


static void parse_dacl(struct cifs_acl *pdacl, char *end_of_acl,
		       struct cifs_sid *pownersid, struct cifs_sid *pgrpsid,
		       struct inode *inode)
{
	int i;
	int num_aces = 0;
	int acl_size;
	char *acl_base;
	struct cifs_ace **ppace;

	/* BB need to add parm so we can store the SID BB */

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)pdacl + le16_to_cpu(pdacl->size)) {
		cERROR(1, ("ACL too small to parse DACL"));
		return;
	}

#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("DACL revision %d size %d num aces %d",
		le16_to_cpu(pdacl->revision), le16_to_cpu(pdacl->size),
		le32_to_cpu(pdacl->num_aces)));
#endif

	acl_base = (char *)pdacl;
	acl_size = sizeof(struct cifs_acl);

	num_aces = le32_to_cpu(pdacl->num_aces);
	if (num_aces  > 0) {
		ppace = kmalloc(num_aces * sizeof(struct cifs_ace *),
				GFP_KERNEL);

/*		cifscred->cecount = pdacl->num_aces;
		cifscred->aces = kmalloc(num_aces *
			sizeof(struct cifs_ace *), GFP_KERNEL);*/

		for (i = 0; i < num_aces; ++i) {
			ppace[i] = (struct cifs_ace *) (acl_base + acl_size);

			parse_ace(ppace[i], end_of_acl);

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


static int parse_sid(struct cifs_sid *psid, char *end_of_acl)
{

	/* BB need to add parm so we can store the SID BB */

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)psid + sizeof(struct cifs_sid)) {
		cERROR(1, ("ACL too small to parse SID"));
		return -EINVAL;
	}

	if (psid->num_subauth) {
#ifdef CONFIG_CIFS_DEBUG2
		int i;
		cFYI(1, ("SID revision %d num_auth %d",
			psid->revision, psid->num_subauth));

		for (i = 0; i < psid->num_subauth; i++) {
			cFYI(1, ("SID sub_auth[%d]: 0x%x ", i,
				le32_to_cpu(psid->sub_auth[i])));
		}

		/* BB add length check to make sure that we do not have huge
			num auths and therefore go off the end */
		cFYI(1, ("RID 0x%x",
			le32_to_cpu(psid->sub_auth[psid->num_subauth-1])));
#endif
	}

	return 0;
}


/* Convert CIFS ACL to POSIX form */
static int parse_sec_desc(struct cifs_ntsd *pntsd, int acl_len,
			  struct inode *inode)
{
	int rc;
	struct cifs_sid *owner_sid_ptr, *group_sid_ptr;
	struct cifs_acl *dacl_ptr; /* no need for SACL ptr */
	char *end_of_acl = ((char *)pntsd) + acl_len;

	owner_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->osidoffset));
	group_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				le32_to_cpu(pntsd->gsidoffset));
	dacl_ptr = (struct cifs_acl *)((char *)pntsd +
				le32_to_cpu(pntsd->dacloffset));
#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("revision %d type 0x%x ooffset 0x%x goffset 0x%x "
		 "sacloffset 0x%x dacloffset 0x%x",
		 pntsd->revision, pntsd->type, le32_to_cpu(pntsd->osidoffset),
		 le32_to_cpu(pntsd->gsidoffset),
		 le32_to_cpu(pntsd->sacloffset),
		 le32_to_cpu(pntsd->dacloffset)));
#endif
	rc = parse_sid(owner_sid_ptr, end_of_acl);
	if (rc)
		return rc;

	rc = parse_sid(group_sid_ptr, end_of_acl);
	if (rc)
		return rc;

	parse_dacl(dacl_ptr, end_of_acl, owner_sid_ptr, group_sid_ptr, inode);

/*	cifscred->uid = owner_sid_ptr->rid;
	cifscred->gid = group_sid_ptr->rid;
	memcpy((void *)(&(cifscred->osid)), (void *)owner_sid_ptr,
			sizeof(struct cifs_sid));
	memcpy((void *)(&(cifscred->gsid)), (void *)group_sid_ptr,
			sizeof(struct cifs_sid)); */


	return (0);
}
#endif /* CONFIG_CIFS_EXPERIMENTAL */
