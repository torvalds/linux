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
	{{1, 2, {0, 0, 0, 0, 0, 5}, {cpu_to_le32(32), cpu_to_le32(546), 0, 0, 0} }, "guest"}
};


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
int compare_sids(struct cifs_sid *ctsid, struct cifs_sid *cwsid)
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


static void parse_ace(struct cifs_ace *pace, char *end_of_acl)
{
	int num_subauth;

	/* validate that we do not go past end of acl */

	/* XXX this if statement can be removed
	if (end_of_acl < (char *)pace + sizeof(struct cifs_ace)) {
		cERROR(1, ("ACL too small to parse ACE"));
		return;
	} */

	num_subauth = pace->num_subauth;
	if (num_subauth) {
#ifdef CONFIG_CIFS_DEBUG2
		int i;
		cFYI(1, ("ACE revision %d num_subauth %d",
			pace->revision, pace->num_subauth));
		for (i = 0; i < num_subauth; ++i) {
			cFYI(1, ("ACE sub_auth[%d]: 0x%x", i,
				le32_to_cpu(pace->sub_auth[i])));
		}

		/* BB add length check to make sure that we do not have huge
			num auths and therefore go off the end */

		cFYI(1, ("RID %d", le32_to_cpu(pace->sub_auth[num_subauth-1])));
#endif
	}

	return;
}

static void parse_ntace(struct cifs_ntace *pntace, char *end_of_acl)
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



static void parse_dacl(struct cifs_acl *pdacl, char *end_of_acl,
		       struct cifs_sid *pownersid, struct cifs_sid *pgrpsid)
{
	int i;
	int num_aces = 0;
	int acl_size;
	char *acl_base;
	struct cifs_ntace **ppntace;
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
		ppntace = kmalloc(num_aces * sizeof(struct cifs_ntace *),
				GFP_KERNEL);
		ppace = kmalloc(num_aces * sizeof(struct cifs_ace *),
				GFP_KERNEL);

/*		cifscred->cecount = pdacl->num_aces;
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
			if (end_of_acl < ((char *)ppace[i] +
					(le16_to_cpu(ppntace[i]->size) -
					sizeof(struct cifs_ntace)))) {
				cERROR(1, ("ACL too small to parse ACE"));
				break;
			} else
				parse_ace(ppace[i], end_of_acl);

/*			memcpy((void *)(&(cifscred->ntaces[i])),
				(void *)ppntace[i],
				sizeof(struct cifs_ntace));
			memcpy((void *)(&(cifscred->aces[i])),
				(void *)ppace[i],
				sizeof(struct cifs_ace)); */

			acl_base = (char *)ppntace[i];
			acl_size = le16_to_cpu(ppntace[i]->size);
		}

		kfree(ppace);
		kfree(ppntace);
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
		cFYI(1, ("SID revision %d num_auth %d First subauth 0x%x",
			psid->revision, psid->num_subauth, psid->sub_auth[0]));

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
int parse_sec_desc(struct cifs_ntsd *pntsd, int acl_len)
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

	parse_dacl(dacl_ptr, end_of_acl, owner_sid_ptr, group_sid_ptr);

/*	cifscred->uid = owner_sid_ptr->rid;
	cifscred->gid = group_sid_ptr->rid;
	memcpy((void *)(&(cifscred->osid)), (void *)owner_sid_ptr,
			sizeof (struct cifs_sid));
	memcpy((void *)(&(cifscred->gsid)), (void *)group_sid_ptr,
			sizeof (struct cifs_sid)); */


	return (0);
}
#endif /* CONFIG_CIFS_EXPERIMENTAL */
