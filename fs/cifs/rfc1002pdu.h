/*
 *   fs/cifs/rfc1002pdu.h
 *
 *   Protocol Data Unit definitions for RFC 1001/1002 support
 *
 *   Copyright (c) International Business Machines  Corp., 2004
 *   Author(s): Steve French (sfrench@us.ibm.com)
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

#pragma pack(1)

/* NB: unlike smb/cifs packets, the RFC1002 structures are big endian */

	/* RFC 1002 session packet types */
#define RFC1002_SESSION_MESASAGE 0x00
#define RFC1002_SESSION_REQUEST  0x81
#define RFC1002_POSITIVE_SESSION_RESPONSE 0x82
#define RFC1002_NEGATIVE_SESSION_RESPONSE 0x83
#define RFC1002_RETARGET_SESSION_RESPONSE 0x83
#define RFC1002_SESSION_KEEP_ALIVE 0x85

	/* RFC 1002 flags (only one defined */
#define RFC1002_LENGTH_EXTEND 0x80 /* high order bit of length (ie +64K) */

struct rfc1002_session_packet {
	__u8	type;
	__u8	flags;
	__u16	length;
	union {
		struct {
			__u8 called_len;
			__u8 called_name[32];
			__u8 scope1; /* null */
			__u8 calling_len;
			__u8 calling_name[32];
			__u8 scope2; /* null */
		} session_req;
		struct {
			__u32 retarget_ip_addr;
			__u16 port;
		} retarget_resp;
		__u8 neg_ses_resp_error_code;
		/* POSITIVE_SESSION_RESPONSE packet does not include trailer.
		SESSION_KEEP_ALIVE packet also does not include a trailer.
		Trailer for the SESSION_MESSAGE packet is SMB/CIFS header */
	} trailer;
};

/* Negative Session Response error codes */
#define RFC1002_NOT_LISTENING_CALLED  0x80 /* not listening on called name */
#define RFC1002_NOT_LISTENING_CALLING 0x81 /* not listening on calling name */
#define RFC1002_NOT_PRESENT           0x82 /* called name not present */
#define RFC1002_INSUFFICIENT_RESOURCE 0x83
#define RFC1002_UNSPECIFIED_ERROR     0x8F

/* RFC 1002 Datagram service packets are not defined here as they
are not needed for the network filesystem client unless we plan on
implementing broadcast resolution of the server ip address (from
server netbios name). Currently server names are resolved only via DNS
(tcp name) or ip address or an /etc/hosts equivalent mapping to ip address.*/

#define DEFAULT_CIFS_CALLED_NAME  "*SMBSERVER      "

#pragma pack()		/* resume default structure packing */
                                                             
