/*
 *   fs/cifs/smb2pdu.h
 *
 *   Copyright (c) International Business Machines  Corp., 2009, 2010
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
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

#ifndef _SMB2PDU_H
#define _SMB2PDU_H

#include <net/sock.h>

/*
 * Note that, due to trying to use names similar to the protocol specifications,
 * there are many mixed case field names in the structures below.  Although
 * this does not match typical Linux kernel style, it is necessary to be
 * be able to match against the protocol specfication.
 *
 * SMB2 commands
 * Some commands have minimal (wct=0,bcc=0), or uninteresting, responses
 * (ie no useful data other than the SMB error code itself) and are marked such.
 * Knowing this helps avoid response buffer allocations and copy in some cases.
 */

/* List of commands in host endian */
#define SMB2_NEGOTIATE_HE	0x0000
#define SMB2_SESSION_SETUP_HE	0x0001
#define SMB2_LOGOFF_HE		0x0002 /* trivial request/resp */
#define SMB2_TREE_CONNECT_HE	0x0003
#define SMB2_TREE_DISCONNECT_HE	0x0004 /* trivial req/resp */
#define SMB2_CREATE_HE		0x0005
#define SMB2_CLOSE_HE		0x0006
#define SMB2_FLUSH_HE		0x0007 /* trivial resp */
#define SMB2_READ_HE		0x0008
#define SMB2_WRITE_HE		0x0009
#define SMB2_LOCK_HE		0x000A
#define SMB2_IOCTL_HE		0x000B
#define SMB2_CANCEL_HE		0x000C
#define SMB2_ECHO_HE		0x000D
#define SMB2_QUERY_DIRECTORY_HE	0x000E
#define SMB2_CHANGE_NOTIFY_HE	0x000F
#define SMB2_QUERY_INFO_HE	0x0010
#define SMB2_SET_INFO_HE	0x0011
#define SMB2_OPLOCK_BREAK_HE	0x0012

/* The same list in little endian */
#define SMB2_NEGOTIATE		cpu_to_le16(SMB2_NEGOTIATE_HE)
#define SMB2_SESSION_SETUP	cpu_to_le16(SMB2_SESSION_SETUP_HE)
#define SMB2_LOGOFF		cpu_to_le16(SMB2_LOGOFF_HE)
#define SMB2_TREE_CONNECT	cpu_to_le16(SMB2_TREE_CONNECT_HE)
#define SMB2_TREE_DISCONNECT	cpu_to_le16(SMB2_TREE_DISCONNECT_HE)
#define SMB2_CREATE		cpu_to_le16(SMB2_CREATE_HE)
#define SMB2_CLOSE		cpu_to_le16(SMB2_CLOSE_HE)
#define SMB2_FLUSH		cpu_to_le16(SMB2_FLUSH_HE)
#define SMB2_READ		cpu_to_le16(SMB2_READ_HE)
#define SMB2_WRITE		cpu_to_le16(SMB2_WRITE_HE)
#define SMB2_LOCK		cpu_to_le16(SMB2_LOCK_HE)
#define SMB2_IOCTL		cpu_to_le16(SMB2_IOCTL_HE)
#define SMB2_CANCEL		cpu_to_le16(SMB2_CANCEL_HE)
#define SMB2_ECHO		cpu_to_le16(SMB2_ECHO_HE)
#define SMB2_QUERY_DIRECTORY	cpu_to_le16(SMB2_QUERY_DIRECTORY_HE)
#define SMB2_CHANGE_NOTIFY	cpu_to_le16(SMB2_CHANGE_NOTIFY_HE)
#define SMB2_QUERY_INFO		cpu_to_le16(SMB2_QUERY_INFO_HE)
#define SMB2_SET_INFO		cpu_to_le16(SMB2_SET_INFO_HE)
#define SMB2_OPLOCK_BREAK	cpu_to_le16(SMB2_OPLOCK_BREAK_HE)

#define NUMBER_OF_SMB2_COMMANDS	0x0013

/* BB FIXME - analyze following length BB */
#define MAX_SMB2_HDR_SIZE 0x78 /* 4 len + 64 hdr + (2*24 wct) + 2 bct + 2 pad */

#define SMB2_PROTO_NUMBER __constant_cpu_to_le32(0x424d53fe)

#define SMB2_HEADER_SIZE __constant_le16_to_cpu(64)

#define SMB2_ERROR_STRUCTURE_SIZE2 __constant_le16_to_cpu(9)

/*
 * SMB2 Header Definition
 *
 * "MBZ" :  Must be Zero
 * "BB"  :  BugBug, Something to check/review/analyze later
 * "PDU" :  "Protocol Data Unit" (ie a network "frame")
 *
 */
struct smb2_hdr {
	__be32 smb2_buf_length;	/* big endian on wire */
				/* length is only two or three bytes - with
				 one or two byte type preceding it that MBZ */
	__u8   ProtocolId[4];	/* 0xFE 'S' 'M' 'B' */
	__le16 StructureSize;	/* 64 */
	__le16 CreditCharge;	/* MBZ */
	__le32 Status;		/* Error from server */
	__le16 Command;
	__le16 CreditRequest;  /* CreditResponse */
	__le32 Flags;
	__le32 NextCommand;
	__u64  MessageId;	/* opaque - so can stay little endian */
	__le32 ProcessId;
	__u32  TreeId;		/* opaque - so do not make little endian */
	__u64  SessionId;	/* opaque - so do not make little endian */
	__u8   Signature[16];
} __packed;

struct smb2_pdu {
	struct smb2_hdr hdr;
	__le16 StructureSize2; /* size of wct area (varies, request specific) */
} __packed;

/*
 *	SMB2 flag definitions
 */
#define SMB2_FLAGS_SERVER_TO_REDIR	__constant_cpu_to_le32(0x00000001)
#define SMB2_FLAGS_ASYNC_COMMAND	__constant_cpu_to_le32(0x00000002)
#define SMB2_FLAGS_RELATED_OPERATIONS	__constant_cpu_to_le32(0x00000004)
#define SMB2_FLAGS_SIGNED		__constant_cpu_to_le32(0x00000008)
#define SMB2_FLAGS_DFS_OPERATIONS	__constant_cpu_to_le32(0x10000000)

/*
 *	Definitions for SMB2 Protocol Data Units (network frames)
 *
 *  See MS-SMB2.PDF specification for protocol details.
 *  The Naming convention is the lower case version of the SMB2
 *  command code name for the struct. Note that structures must be packed.
 *
 */
struct smb2_err_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;
	__le16 Reserved; /* MBZ */
	__le32 ByteCount;  /* even if zero, at least one byte follows */
	__u8   ErrorData[1];  /* variable length */
} __packed;

#endif				/* _SMB2PDU_H */
