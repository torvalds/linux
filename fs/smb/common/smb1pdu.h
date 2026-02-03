/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2009
 *                 2018 Samsung Electronics Co., Ltd.
 *   Author(s): Steve French <sfrench@us.ibm.com>
 *              Namjae Jeon <linkinjeon@kernel.org>
 *
 */

#ifndef _COMMON_SMB1_PDU_H
#define _COMMON_SMB1_PDU_H

#define SMB1_PROTO_NUMBER		cpu_to_le32(0x424d53ff)

/*
 * See MS-CIFS 2.2.3.1
 *     MS-SMB 2.2.3.1
 */
struct smb_hdr {
	__u8 Protocol[4];
	__u8 Command;
	union {
		struct {
			__u8 ErrorClass;
			__u8 Reserved;
			__le16 Error;
		} __packed DosError;
		__le32 CifsError;
	} __packed Status;
	__u8 Flags;
	__le16 Flags2;		/* note: le */
	__le16 PidHigh;
	union {
		struct {
			__le32 SequenceNumber;  /* le */
			__u32 Reserved; /* zero */
		} __packed Sequence;
		__u8 SecuritySignature[8];	/* le */
	} __packed Signature;
	__u8 pad[2];
	__u16 Tid;
	__le16 Pid;
	__u16 Uid;
	__le16 Mid;
	__u8 WordCount;
} __packed;

/* See MS-CIFS 2.2.4.52.1 */
typedef struct smb_negotiate_req {
	struct smb_hdr hdr;	/* wct = 0 */
	__le16 ByteCount;
	unsigned char DialectsArray[];
} __packed SMB_NEGOTIATE_REQ;

#endif /* _COMMON_SMB1_PDU_H */
