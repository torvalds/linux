/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#define NTLMSSP_SIGNATURE "NTLMSSP"
/* Message Types */
#define NtLmNegotiate     cpu_to_le32(1)
#define NtLmChallenge     cpu_to_le32(2)
#define NtLmAuthenticate  cpu_to_le32(3)
#define UnknownMessage    cpu_to_le32(8)

/* Negotiate Flags */
#define NTLMSSP_NEGOTIATE_UNICODE         0x01 /* Text strings are unicode */
#define NTLMSSP_NEGOTIATE_OEM             0x02 /* Text strings are in OEM */
#define NTLMSSP_REQUEST_TARGET            0x04 /* Srv returns its auth realm */
/* define reserved9                       0x08 */
#define NTLMSSP_NEGOTIATE_SIGN          0x0010 /* Request signing capability */
#define NTLMSSP_NEGOTIATE_SEAL          0x0020 /* Request confidentiality */
#define NTLMSSP_NEGOTIATE_DGRAM         0x0040
#define NTLMSSP_NEGOTIATE_LM_KEY        0x0080 /* Use LM session key */
/* defined reserved 8                   0x0100 */
#define NTLMSSP_NEGOTIATE_NTLM          0x0200 /* NTLM authentication */
#define NTLMSSP_NEGOTIATE_NT_ONLY       0x0400 /* Lanman not allowed */
#define NTLMSSP_ANONYMOUS               0x0800
#define NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED 0x1000 /* reserved6 */
#define NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED 0x2000
#define NTLMSSP_NEGOTIATE_LOCAL_CALL    0x4000 /* client/server same machine */
#define NTLMSSP_NEGOTIATE_ALWAYS_SIGN   0x8000 /* Sign. All security levels  */
#define NTLMSSP_TARGET_TYPE_DOMAIN     0x10000
#define NTLMSSP_TARGET_TYPE_SERVER     0x20000
#define NTLMSSP_TARGET_TYPE_SHARE      0x40000
#define NTLMSSP_NEGOTIATE_EXTENDED_SEC 0x80000 /* NB:not related to NTLMv2 pwd*/
/* #define NTLMSSP_REQUEST_INIT_RESP     0x100000 */
#define NTLMSSP_NEGOTIATE_IDENTIFY    0x100000
#define NTLMSSP_REQUEST_ACCEPT_RESP   0x200000 /* reserved5 */
#define NTLMSSP_REQUEST_NON_NT_KEY    0x400000
#define NTLMSSP_NEGOTIATE_TARGET_INFO 0x800000
/* #define reserved4                 0x1000000 */
#define NTLMSSP_NEGOTIATE_VERSION    0x2000000 /* we only set for SMB2+ */
/* #define reserved3                 0x4000000 */
/* #define reserved2                 0x8000000 */
/* #define reserved1                0x10000000 */
#define NTLMSSP_NEGOTIATE_128       0x20000000
#define NTLMSSP_NEGOTIATE_KEY_XCH   0x40000000
#define NTLMSSP_NEGOTIATE_56        0x80000000

/* Define AV Pair Field IDs */
enum av_field_type {
	NTLMSSP_AV_EOL = 0,
	NTLMSSP_AV_NB_COMPUTER_NAME,
	NTLMSSP_AV_NB_DOMAIN_NAME,
	NTLMSSP_AV_DNS_COMPUTER_NAME,
	NTLMSSP_AV_DNS_DOMAIN_NAME,
	NTLMSSP_AV_DNS_TREE_NAME,
	NTLMSSP_AV_FLAGS,
	NTLMSSP_AV_TIMESTAMP,
	NTLMSSP_AV_RESTRICTION,
	NTLMSSP_AV_TARGET_NAME,
	NTLMSSP_AV_CHANNEL_BINDINGS
};

/* Although typedefs are not commonly used for structure definitions */
/* in the Linux kernel, in this particular case they are useful      */
/* to more closely match the standards document for NTLMSSP from     */
/* OpenGroup and to make the code more closely match the standard in */
/* appearance */

typedef struct _SECURITY_BUFFER {
	__le16 Length;
	__le16 MaximumLength;
	__le32 BufferOffset;	/* offset to buffer */
} __attribute__((packed)) SECURITY_BUFFER;

typedef struct _NEGOTIATE_MESSAGE {
	__u8 Signature[sizeof(NTLMSSP_SIGNATURE)];
	__le32 MessageType;     /* NtLmNegotiate = 1 */
	__le32 NegotiateFlags;
	SECURITY_BUFFER DomainName;	/* RFC 1001 style and ASCII */
	SECURITY_BUFFER WorkstationName;	/* RFC 1001 and ASCII */
	/* SECURITY_BUFFER for version info not present since we
	   do not set the version is present flag */
	char DomainString[0];
	/* followed by WorkstationString */
} __attribute__((packed)) NEGOTIATE_MESSAGE, *PNEGOTIATE_MESSAGE;

#define NTLMSSP_REVISION_W2K3 0x0F

/* See MS-NLMP section 2.2.2.10 */
struct ntlmssp_version {
	__u8	ProductMajorVersion;
	__u8	ProductMinorVersion;
	__le16	ProductBuild; /* we send the cifs.ko module version here */
	__u8	Reserved[3];
	__u8	NTLMRevisionCurrent; /* currently 0x0F */
} __packed;

/* see MS-NLMP section 2.2.1.1 */
struct negotiate_message {
	__u8 Signature[sizeof(NTLMSSP_SIGNATURE)];
	__le32 MessageType;     /* NtLmNegotiate = 1 */
	__le32 NegotiateFlags;
	SECURITY_BUFFER DomainName;	/* RFC 1001 style and ASCII */
	SECURITY_BUFFER WorkstationName;	/* RFC 1001 and ASCII */
	struct	ntlmssp_version Version;
	/* SECURITY_BUFFER */
	char DomainString[0];
	/* followed by WorkstationString */
} __packed;

typedef struct _CHALLENGE_MESSAGE {
	__u8 Signature[sizeof(NTLMSSP_SIGNATURE)];
	__le32 MessageType;   /* NtLmChallenge = 2 */
	SECURITY_BUFFER TargetName;
	__le32 NegotiateFlags;
	__u8 Challenge[CIFS_CRYPTO_KEY_SIZE];
	__u8 Reserved[8];
	SECURITY_BUFFER TargetInfoArray;
	/* SECURITY_BUFFER for version info not present since we
	   do not set the version is present flag */
} __attribute__((packed)) CHALLENGE_MESSAGE, *PCHALLENGE_MESSAGE;

typedef struct _AUTHENTICATE_MESSAGE {
	__u8 Signature[sizeof(NTLMSSP_SIGNATURE)];
	__le32 MessageType;  /* NtLmsAuthenticate = 3 */
	SECURITY_BUFFER LmChallengeResponse;
	SECURITY_BUFFER NtChallengeResponse;
	SECURITY_BUFFER DomainName;
	SECURITY_BUFFER UserName;
	SECURITY_BUFFER WorkstationName;
	SECURITY_BUFFER SessionKey;
	__le32 NegotiateFlags;
	/* SECURITY_BUFFER for version info not present since we
	   do not set the version is present flag */
	char UserString[0];
} __attribute__((packed)) AUTHENTICATE_MESSAGE, *PAUTHENTICATE_MESSAGE;

/*
 * Size of the session key (crypto key encrypted with the password
 */

int decode_ntlmssp_challenge(char *bcc_ptr, int blob_len, struct cifs_ses *ses);
int build_ntlmssp_negotiate_blob(unsigned char **pbuffer, u16 *buflen,
				 struct cifs_ses *ses,
				 struct TCP_Server_Info *server,
				 const struct nls_table *nls_cp);
int build_ntlmssp_smb3_negotiate_blob(unsigned char **pbuffer, u16 *buflen,
				 struct cifs_ses *ses,
				 struct TCP_Server_Info *server,
				 const struct nls_table *nls_cp);
int build_ntlmssp_auth_blob(unsigned char **pbuffer, u16 *buflen,
			struct cifs_ses *ses,
			struct TCP_Server_Info *server,
			const struct nls_table *nls_cp);
