/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RxRPC key type
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _KEYS_RXRPC_TYPE_H
#define _KEYS_RXRPC_TYPE_H

#include <linux/key.h>

/*
 * key type for AF_RXRPC keys
 */
extern struct key_type key_type_rxrpc;

extern struct key *rxrpc_get_null_key(const char *);

/*
 * RxRPC key for Kerberos IV (type-2 security)
 */
struct rxkad_key {
	u32	vice_id;
	u32	start;			/* time at which ticket starts */
	u32	expiry;			/* time at which ticket expires */
	u32	kvno;			/* key version number */
	u8	primary_flag;		/* T if key for primary cell for this user */
	u16	ticket_len;		/* length of ticket[] */
	u8	session_key[8];		/* DES session key */
	u8	ticket[];		/* the encrypted ticket */
};

/*
 * list of tokens attached to an rxrpc key
 */
struct rxrpc_key_token {
	u16	security_index;		/* RxRPC header security index */
	bool	no_leak_key;		/* Don't copy the key to userspace */
	struct rxrpc_key_token *next;	/* the next token in the list */
	union {
		struct rxkad_key *kad;
	};
};

/*
 * structure of raw payloads passed to add_key() or instantiate key
 */
struct rxrpc_key_data_v1 {
	u16		security_index;
	u16		ticket_length;
	u32		expiry;			/* time_t */
	u32		kvno;
	u8		session_key[8];
	u8		ticket[];
};

/*
 * AF_RXRPC key payload derived from XDR format
 * - based on openafs-1.4.10/src/auth/afs_token.xg
 */
#define AFSTOKEN_LENGTH_MAX		16384	/* max payload size */
#define AFSTOKEN_STRING_MAX		256	/* max small string length */
#define AFSTOKEN_DATA_MAX		64	/* max small data length */
#define AFSTOKEN_CELL_MAX		64	/* max cellname length */
#define AFSTOKEN_MAX			8	/* max tokens per payload */
#define AFSTOKEN_BDATALN_MAX		16384	/* max big data length */
#define AFSTOKEN_RK_TIX_MAX		12000	/* max RxKAD ticket size */
#define AFSTOKEN_GK_KEY_MAX		64	/* max GSSAPI key size */
#define AFSTOKEN_GK_TOKEN_MAX		16384	/* max GSSAPI token size */

/*
 * Truncate a time64_t to the range from 1970 to 2106 as in the network
 * protocol.
 */
static inline u32 rxrpc_time64_to_u32(time64_t time)
{
	if (time < 0)
		return 0;

	if (time > UINT_MAX)
		return UINT_MAX;

	return (u32)time;
}

/*
 * Extend u32 back to time64_t using the same 1970-2106 range.
 */
static inline time64_t rxrpc_u32_to_time64(u32 time)
{
	return (time64_t)time;
}

#endif /* _KEYS_RXRPC_TYPE_H */
