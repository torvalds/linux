/*
 * WAI protocol definitions
 * Copyright (c) 2009 Nanoradio
 */

#ifndef WAPI_COMMON_H
#define WAPI_COMMON_H


#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

/* WAPI Specification - 8.1.4.1 */

struct wapi_hdr {
	u16 version;
	u8  type;
	u8  subtype;
	u16 reserved;
	u16 length;
	u16 seq_num; /* Packet Sequence Number */
	u8  frag_seq_num; /* Fragment Sequence Number */
	u8  flag; /* bit0 is the more fragment field */
	/* followed by length octets of data */
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */

/* wapi_hdr version field */
#define WAPI_VERSION 1

/* wapi_hdr type field */
#define WAPI_TYPE 1

#define WAPI_NONCE_LEN 32

/* wapi_hdr subtype field */
enum { WAPI_SUBTYPE_PREAUTH_START = 1,
       WAPI_SUBTYPE_STAKEY_REQUEST = 2,
       WAPI_SUBTYPE_AUTH_ACTIVATION = 3,
       WAPI_SUBTYPE_AUTH_REQUEST = 4,
       WAPI_SUBTYPE_AUTH_RESPONSE = 5,
       WAPI_SUBTYPE_CERT_AUTH_REQUEST = 6,
       WAPI_SUBTYPE_CERT_AUTH_RESPONSE = 7,
       WAPI_SUBTYPE_UNICAST_KEY_REQUEST = 8,
       WAPI_SUBTYPE_UNICAST_KEY_RESPONSE = 9,
       WAPI_SUBTYPE_UNICAST_KEY_CONFIRM = 10,
       WAPI_SUBTYPE_MULTICAST_KEY_REQUEST = 11,
       WAPI_SYBTYPE_MULTICAST_KEY_RESPONSE = 12
};

#endif /* WAPI_COMMON_H */

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */

