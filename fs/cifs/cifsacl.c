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

/* security id for everyone */
static const struct cifs_sid sid_everyone =
		{1, 1, {0, 0, 0, 0, 0, 0}, {}};
/* group users */
static const struct cifs_sid sid_user =
		{1, 2 , {0, 0, 0, 0, 0, 5}, {}};

static void parse_ace(struct cifs_ace * pace, char * end_of_acl)
{
	int i;
	int num_subauth;
	 __u32 *psub_auth;

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)pace + sizeof(struct cifs_ace)) {
		cERROR(1, ("ACL too small to parse ACE"));
		return;
	}

	num_subauth = cpu_to_le32(pace->num_subauth);
	if (num_subauth) {
		psub_auth = (__u32 *)((char *)pace + sizeof(struct cifs_ace));
#ifdef CONFIG_CIFS_DEBUG2 
		cFYI(1, ("ACE revision %d num_subauth %d", 
			pace->revision, pace->num_subauth)); 
		for (i = 0; i < num_subauth; ++i) { 
			cFYI(1, ("ACE sub_auth[%d]: 0x%x", i, 
				le32_to_cpu(psub_auth[i]))); 
		} 

		/* BB add length check to make sure that we do not have huge 
			num auths and therefore go off the end */ 

		cFYI(1, ("RID %d", le32_to_cpu(psub_auth[num_subauth-1]))); 
#endif 
	} 

	return; 
} 

static void parse_ntace(struct cifs_ntace * pntace, char * end_of_acl) 
{ 
	/* validate that we do not go past end of acl */ 
	if (end_of_acl < (char *)pntace + sizeof(struct cifs_ntace)) {
		cERROR(1, ("ACL too small to parse NT ACE"));
		return;
	}

#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("NTACE type %d flags 0x%x size %d, access Req 0x%x",
		pntace->type, pntace->flags, pntace->size,
		pntace->access_req));
#endif
	return;
}



static void parse_dacl(struct cifs_acl * pdacl, char * end_of_acl)
{
	int i;
	int num_aces = 0;
	int acl_size;
	char *acl_base;
	struct cifs_ntace **ppntace;
	struct cifs_ace **ppace;

	/* BB need to add parm so we can store the SID BB */

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)pdacl + pdacl->size) {
		cERROR(1, ("ACL too small to parse DACL"));
		return;
	}

#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("DACL revision %d size %d num aces %d",
		pdacl->revision, pdacl->size, pdacl->num_aces));
#endif

	acl_base = (char *)pdacl;
	acl_size = sizeof(struct cifs_acl);

	num_aces = cpu_to_le32(pdacl->num_aces);
	if (num_aces  > 0) {
		ppntace = kmalloc(num_aces * sizeof(struct cifs_ntace *),
				GFP_KERNEL);
		ppace = kmalloc(num_aces * sizeof(struct cifs_ace *),
				GFP_KERNEL);

/*              cifscred->cecount = pdacl->num_aces;
                cifscred->ntaces = kmalloc(num_aces *
                        sizeof(struct cifs_ntace *), GFP_KERNEL);
                cifscred->aces = kmalloc(num_aces *
                        sizeof(struct cifs_ace *), GFP_KERNEL);*/


		for (i = 0; i < num_aces; ++i) {
			ppntace[i] = (struct cifs_ntace *)
					(acl_base + acl_size);
			ppace[i] = (struct cifs_ace *) ((char *)ppntace[i] +
					sizeof(struct cifs_ntace));

			parse_ntace(ppntace[i], end_of_acl);
			parse_ace(ppace[i], end_of_acl);

/*                      memcpy((void *)(&(cifscred->ntaces[i])),
                                (void *)ppntace[i],
                                sizeof(struct cifs_ntace));
                        memcpy((void *)(&(cifscred->aces[i])),
                                (void *)ppace[i],
                                sizeof(struct cifs_ace)); */

			acl_base = (char *)ppntace[i];
			acl_size = cpu_to_le32(ppntace[i]->size);
		}

		kfree(ppace);
		kfree(ppntace);
	}

	return;
}


static int parse_sid(struct cifs_sid *psid, char *end_of_acl)
{
	int i;
	int num_subauth;
	__u32 *psub_auth;

	/* BB need to add parm so we can store the SID BB */

	/* validate that we do not go past end of acl */
	if (end_of_acl < (char *)psid + sizeof(struct cifs_sid)) {
		cERROR(1, ("ACL too small to parse SID"));
		return -EINVAL;
	}

	num_subauth = cpu_to_le32(psid->num_subauth);
	if (num_subauth) {
		psub_auth = (__u32 *)((char *)psid + sizeof(struct cifs_sid));
#ifdef CONFIG_CIFS_DEBUG2
		cFYI(1, ("SID revision %d num_auth %d First subauth 0x%x",
			psid->revision, psid->num_subauth, psid->sub_auth[0]));

		for (i = 0; i < num_subauth; ++i) {
			cFYI(1, ("SID sub_auth[%d]: 0x%x ", i,
			le32_to_cpu(psub_auth[i])));
		}

		/* BB add length check to make sure that we do not have huge 
			num auths and therefore go off the end */
		cFYI(1, ("RID 0x%x", 
			le32_to_cpu(psid->sub_auth[psid->num_subauth])));
#endif
	}

	return 0;
}


/* Convert CIFS ACL to POSIX form */
int parse_sec_desc(struct cifs_ntsd *pntsd, int acl_len)
{
	int rc;
	struct cifs_sid *owner_sid_ptr, *group_sid_ptr;
	struct cifs_acl *dacl_ptr; /* no need for SACL ptr */
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

	parse_dacl(dacl_ptr, end_of_acl);

/*	cifscred->uid = owner_sid_ptr->rid;
	cifscred->gid = group_sid_ptr->rid;
	memcpy((void *)(&(cifscred->osid)), (void *)owner_sid_ptr,
			sizeof (struct cifs_sid));
	memcpy((void *)(&(cifscred->gsid)), (void *)group_sid_ptr,
			sizeof (struct cifs_sid)); */

	return (0);
}
