/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2009
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _CIFSPDU_H
#define _CIFSPDU_H

#include <linux/unaligned.h>
#include "../common/smb1pdu.h"

#define GETU16(var)  (*((__u16 *)var))	/* BB check for endian issues */
#define GETU32(var)  (*((__u32 *)var))	/* BB check for endian issues */

/* given a pointer to an smb_hdr, retrieve a void pointer to the ByteCount */
static inline void *
BCC(struct smb_hdr *smb)
{
	return (void *)smb + sizeof(*smb) + 2 * smb->WordCount;
}

/* given a pointer to an smb_hdr retrieve the pointer to the byte area */
#define pByteArea(smb_var) (BCC(smb_var) + 2)

/* get the unconverted ByteCount for a SMB packet and return it */
static inline __u16
get_bcc(struct smb_hdr *hdr)
{
	__le16 *bc_ptr = (__le16 *)BCC(hdr);

	return get_unaligned_le16(bc_ptr);
}

/* set the ByteCount for a SMB packet in little-endian */
static inline void
put_bcc(__u16 count, struct smb_hdr *hdr)
{
	__le16 *bc_ptr = (__le16 *)BCC(hdr);

	put_unaligned_le16(count, bc_ptr);
}

#endif /* _CIFSPDU_H */
