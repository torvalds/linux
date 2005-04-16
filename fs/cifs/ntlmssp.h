/*
 *   fs/cifs/ntlmssp.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002
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

#define NTLMSSP_SIGNATURE "NTLMSSP"
/* Message Types */
#define NtLmNegotiate     cpu_to_le32(1)
#define NtLmChallenge     cpu_to_le32(2)
#define NtLmAuthenticate  cpu_to_le32(3)
#define UnknownMessage    cpu_to_le32(8)

/* Negotiate Flags */
#define NTLMSSP_NEGOTIATE_UNICODE       0x01	// Text strings are in unicode
#define NTLMSSP_NEGOTIATE_OEM           0x02	// Text strings are in OEM
#define NTLMSSP_REQUEST_TARGET          0x04	// Server return its auth realm
#define NTLMSSP_NEGOTIATE_SIGN        0x0010	// Request signature capability
#define NTLMSSP_NEGOTIATE_SEAL        0x0020	// Request confidentiality
#define NTLMSSP_NEGOTIATE_DGRAM       0x0040
#define NTLMSSP_NEGOTIATE_LM_KEY      0x0080 // Use LM session key for sign/seal
#define NTLMSSP_NEGOTIATE_NTLM        0x0200	// NTLM authentication
#define NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED 0x1000
#define NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED 0x2000
#define NTLMSSP_NEGOTIATE_LOCAL_CALL  0x4000	// client/server on same machine
#define NTLMSSP_NEGOTIATE_ALWAYS_SIGN 0x8000	// Sign for all security levels
#define NTLMSSP_TARGET_TYPE_DOMAIN   0x10000
#define NTLMSSP_TARGET_TYPE_SERVER   0x20000
#define NTLMSSP_TARGET_TYPE_SHARE    0x40000
#define NTLMSSP_NEGOTIATE_NTLMV2     0x80000
#define NTLMSSP_REQUEST_INIT_RESP   0x100000
#define NTLMSSP_REQUEST_ACCEPT_RESP 0x200000
#define NTLMSSP_REQUEST_NOT_NT_KEY  0x400000
#define NTLMSSP_NEGOTIATE_TARGET_INFO 0x800000
#define NTLMSSP_NEGOTIATE_128     0x20000000
#define NTLMSSP_NEGOTIATE_KEY_XCH 0x40000000
#define NTLMSSP_NEGOTIATE_56      0x80000000

/* Although typedefs are not commonly used for structure definitions */
/* in the Linux kernel, in this particular case they are useful      */
/* to more closely match the standards document for NTLMSSP from     */
/* OpenGroup and to make the code more closely match the standard in */
/* appearance */

typedef struct _SECURITY_BUFFER {
	__le16 Length;
	__le16 MaximumLength;
	__le32 Buffer;		/* offset to buffer */
} SECURITY_BUFFER;

typedef struct _NEGOTIATE_MESSAGE {
	__u8 Signature[sizeof (NTLMSSP_SIGNATURE)];
	__le32 MessageType;     /* 1 */
	__le32 NegotiateFlags;
	SECURITY_BUFFER DomainName;	/* RFC 1001 style and ASCII */
	SECURITY_BUFFER WorkstationName;	/* RFC 1001 and ASCII */
	char DomainString[0];
	/* followed by WorkstationString */
} NEGOTIATE_MESSAGE, *PNEGOTIATE_MESSAGE;

typedef struct _CHALLENGE_MESSAGE {
	__u8 Signature[sizeof (NTLMSSP_SIGNATURE)];
	__le32 MessageType;   /* 2 */
	SECURITY_BUFFER TargetName;
	__le32 NegotiateFlags;
	__u8 Challenge[CIFS_CRYPTO_KEY_SIZE];
	__u8 Reserved[8];
	SECURITY_BUFFER TargetInfoArray;
} CHALLENGE_MESSAGE, *PCHALLENGE_MESSAGE;

typedef struct _AUTHENTICATE_MESSAGE {
	__u8 Signature[sizeof (NTLMSSP_SIGNATURE)];
	__le32 MessageType;  /* 3 */
	SECURITY_BUFFER LmChallengeResponse;
	SECURITY_BUFFER NtChallengeResponse;
	SECURITY_BUFFER DomainName;
	SECURITY_BUFFER UserName;
	SECURITY_BUFFER WorkstationName;
	SECURITY_BUFFER SessionKey;
	__le32 NegotiateFlags;
	char UserString[0];
} AUTHENTICATE_MESSAGE, *PAUTHENTICATE_MESSAGE;

#pragma pack()			/* resume default structure packing */
