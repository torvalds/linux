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
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifsacl.h"

/* security id for everyone */
static const struct cifs_sid sid_everyone =
		{1, 1, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
/* group users */
static const struct cifs_sid sid_user =
		{1, 2 , {0, 0, 0, 0, 0, 5}, {32, 545, 0, 0}};

static int parse_sid(struct cifs_sid *psid, char *end_of_acl)
{
	/* BB need to add parm so we can store the SID BB */

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)psid + sizeof(struct cifs_sid)) {
		cERROR(1, ("ACL to small to parse SID"));
		return -EINVAL;
	}
#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("revision %d num_auth %d First subauth 0x%x",
		psid->revision, psid->num_auth, psid->sub_auth[0]));

	/* BB add length check to make sure that we do not have huge num auths
	      and therefore go off the end */
	cFYI(1, ("RID 0x%x", le32_to_cpu(psid->sub_auth[psid->num_auth])));
#endif
	return 0;
}

/* Convert CIFS ACL to POSIX form */
int parse_sec_desc(struct cifs_ntsd *pntsd, int acl_len)
{
	int i, rc;
	int num_aces = 0;
	int acl_size;
	struct cifs_sid *owner_sid_ptr, *group_sid_ptr;
	struct cifs_acl *dacl_ptr; /* no need for SACL ptr */
	struct cifs_ntace **ppntace;
	struct cifs_ace **ppace;
	char *acl_base;
	char *end_of_acl = ((char *)pntsd) + acl_len;

	owner_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				cpu_to_le32(pntsd->osidoffset));
	group_sid_ptr = (struct cifs_sid *)((char *)pntsd +
				cpu_to_le32(pntsd->gsidoffset));
	dacl_ptr = (struct cifs_acl *)((char *)pntsd +
				cpu_to_le32(pntsd->dacloffset));
#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("revision %d type 0x%x ooffset 0x%x goffset 0x%x "
		 "sacloffset 0x%x dacloffset 0x%x",
		 pntsd->revision, pntsd->type,
		 pntsd->osidoffset, pntsd->gsidoffset, pntsd->sacloffset,
		 pntsd->dacloffset));
#endif
	rc = parse_sid(owner_sid_ptr, end_of_acl);
	if (rc)
		return rc;

	rc = parse_sid(group_sid_ptr, end_of_acl);
	if (rc)
		return rc;

/*	cifscred->uid = owner_sid_ptr->rid;
	cifscred->gid = group_sid_ptr->rid;
	memcpy((void *)(&(cifscred->osid)), (void *)owner_sid_ptr,
			sizeof (struct cifs_sid));
	memcpy((void *)(&(cifscred->gsid)), (void *)group_sid_ptr,
			sizeof (struct cifs_sid)); */

	num_aces = cpu_to_le32(dacl_ptr->num_aces);
	cFYI(1, ("num aces %d", num_aces));
	if (num_aces  > 0) {
		ppntace = kmalloc(num_aces * sizeof(struct cifs_ntace *),
				GFP_KERNEL);
		ppace = kmalloc(num_aces * sizeof(struct cifs_ace *),
				GFP_KERNEL);

/*		cifscred->cecount = dacl_ptr->num_aces;
		cifscred->ntaces = kmalloc(num_aces *
				sizeof(struct cifs_ntace *), GFP_KERNEL);
		cifscred->aces = kmalloc(num_aces *
				sizeof(struct cifs_ace *), GFP_KERNEL);*/

		acl_base = (char *)dacl_ptr;
		acl_size = sizeof(struct cifs_acl);

		for (i = 0; i < num_aces; ++i) {
			ppntace[i] = (struct cifs_ntace *)
						(acl_base + acl_size);
			ppace[i] = (struct cifs_ace *)
					((char *)ppntace[i] +
					sizeof(struct cifs_ntace));

/*			memcpy((void *)(&(cifscred->ntaces[i])),
				(void *)ntace_ptrptr[i],
				sizeof(struct cifs_ntace));
			memcpy((void *)(&(cifscred->aces[i])),
				(void *)ace_ptrptr[i],
				sizeof(struct cifs_ace)); */

			acl_base = (char *)ppntace[i];
			acl_size = cpu_to_le32(ppntace[i]->size);
#ifdef CONFIG_CIFS_DEBUG2
			cFYI(1, ("ACE revision:%d", ppace[i]->revision));
#endif
		}
		kfree(ppace);
		kfree(ppntace);
	}

	return (0);
}
