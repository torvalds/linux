/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2008, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_AUTH_H_
#define _NETINET_SCTP_AUTH_H_

#include <netinet/sctp_os.h>

/* digest lengths */
#define SCTP_AUTH_DIGEST_LEN_SHA1	20
#define SCTP_AUTH_DIGEST_LEN_SHA256	32
#define SCTP_AUTH_DIGEST_LEN_MAX	SCTP_AUTH_DIGEST_LEN_SHA256

/* random sizes */
#define SCTP_AUTH_RANDOM_SIZE_DEFAULT	32
#define SCTP_AUTH_RANDOM_SIZE_REQUIRED	32

/* union of all supported HMAC algorithm contexts */
typedef union sctp_hash_context {
	SCTP_SHA1_CTX sha1;
	SCTP_SHA256_CTX sha256;
} sctp_hash_context_t;

typedef struct sctp_key {
	uint32_t keylen;
	uint8_t key[];
} sctp_key_t;

typedef struct sctp_shared_key {
	LIST_ENTRY(sctp_shared_key) next;
	sctp_key_t *key;	/* key text */
	uint32_t refcount;	/* reference count */
	uint16_t keyid;		/* shared key ID */
	uint8_t deactivated;	/* key is deactivated */
} sctp_sharedkey_t;

LIST_HEAD(sctp_keyhead, sctp_shared_key);

/* authentication chunks list */
typedef struct sctp_auth_chklist {
	uint8_t chunks[256];
	uint8_t num_chunks;
} sctp_auth_chklist_t;

/* hmac algos supported list */
typedef struct sctp_hmaclist {
	uint16_t max_algo;	/* max algorithms allocated */
	uint16_t num_algo;	/* num algorithms used */
	uint16_t hmac[];
} sctp_hmaclist_t;

/* authentication info */
typedef struct sctp_authinformation {
	sctp_key_t *random;	/* local random key (concatenated) */
	uint32_t random_len;	/* local random number length for param */
	sctp_key_t *peer_random;	/* peer's random key (concatenated) */
	sctp_key_t *assoc_key;	/* cached concatenated send key */
	sctp_key_t *recv_key;	/* cached concatenated recv key */
	uint16_t active_keyid;	/* active send keyid */
	uint16_t assoc_keyid;	/* current send keyid (cached) */
	uint16_t recv_keyid;	/* last recv keyid (cached) */
} sctp_authinfo_t;



/*
 * Macros
 */
#define sctp_auth_is_required_chunk(chunk, list) ((list == NULL) ? (0) : (list->chunks[chunk] != 0))

/*
 * function prototypes
 */

/* socket option api functions */
extern sctp_auth_chklist_t *sctp_alloc_chunklist(void);
extern void sctp_free_chunklist(sctp_auth_chklist_t *chklist);
extern void sctp_clear_chunklist(sctp_auth_chklist_t *chklist);
extern sctp_auth_chklist_t *sctp_copy_chunklist(sctp_auth_chklist_t *chklist);
extern int sctp_auth_add_chunk(uint8_t chunk, sctp_auth_chklist_t *list);
extern int sctp_auth_delete_chunk(uint8_t chunk, sctp_auth_chklist_t *list);
extern size_t sctp_auth_get_chklist_size(const sctp_auth_chklist_t *list);
extern int
sctp_serialize_auth_chunks(const sctp_auth_chklist_t *list,
    uint8_t *ptr);
extern int
sctp_pack_auth_chunks(const sctp_auth_chklist_t *list,
    uint8_t *ptr);
extern int
sctp_unpack_auth_chunks(const uint8_t *ptr, uint8_t num_chunks,
    sctp_auth_chklist_t *list);

/* key handling */
extern sctp_key_t *sctp_alloc_key(uint32_t keylen);
extern void sctp_free_key(sctp_key_t *key);
extern void sctp_print_key(sctp_key_t *key, const char *str);
extern void sctp_show_key(sctp_key_t *key, const char *str);
extern sctp_key_t *sctp_generate_random_key(uint32_t keylen);
extern sctp_key_t *sctp_set_key(uint8_t *key, uint32_t keylen);
extern sctp_key_t *
sctp_compute_hashkey(sctp_key_t *key1, sctp_key_t *key2,
    sctp_key_t *shared);

/* shared key handling */
extern sctp_sharedkey_t *sctp_alloc_sharedkey(void);
extern void sctp_free_sharedkey(sctp_sharedkey_t *skey);
extern sctp_sharedkey_t *
sctp_find_sharedkey(struct sctp_keyhead *shared_keys,
    uint16_t key_id);
extern int
sctp_insert_sharedkey(struct sctp_keyhead *shared_keys,
    sctp_sharedkey_t *new_skey);
extern int
sctp_copy_skeylist(const struct sctp_keyhead *src,
    struct sctp_keyhead *dest);

/* ref counts on shared keys, by key id */
extern void sctp_auth_key_acquire(struct sctp_tcb *stcb, uint16_t keyid);
extern void
sctp_auth_key_release(struct sctp_tcb *stcb, uint16_t keyid,
    int so_locked);


/* hmac list handling */
extern sctp_hmaclist_t *sctp_alloc_hmaclist(uint16_t num_hmacs);
extern void sctp_free_hmaclist(sctp_hmaclist_t *list);
extern int sctp_auth_add_hmacid(sctp_hmaclist_t *list, uint16_t hmac_id);
extern sctp_hmaclist_t *sctp_copy_hmaclist(sctp_hmaclist_t *list);
extern sctp_hmaclist_t *sctp_default_supported_hmaclist(void);
extern uint16_t
sctp_negotiate_hmacid(sctp_hmaclist_t *peer,
    sctp_hmaclist_t *local);
extern int sctp_serialize_hmaclist(sctp_hmaclist_t *list, uint8_t *ptr);
extern int
sctp_verify_hmac_param(struct sctp_auth_hmac_algo *hmacs,
    uint32_t num_hmacs);

extern sctp_authinfo_t *sctp_alloc_authinfo(void);
extern void sctp_free_authinfo(sctp_authinfo_t *authinfo);

/* keyed-HMAC functions */
extern uint32_t sctp_get_auth_chunk_len(uint16_t hmac_algo);
extern uint32_t sctp_get_hmac_digest_len(uint16_t hmac_algo);
extern uint32_t
sctp_hmac(uint16_t hmac_algo, uint8_t *key, uint32_t keylen,
    uint8_t *text, uint32_t textlen, uint8_t *digest);
extern uint32_t
sctp_compute_hmac(uint16_t hmac_algo, sctp_key_t *key,
    uint8_t *text, uint32_t textlen, uint8_t *digest);
extern int sctp_auth_is_supported_hmac(sctp_hmaclist_t *list, uint16_t id);

/* mbuf versions */
extern uint32_t
sctp_hmac_m(uint16_t hmac_algo, uint8_t *key, uint32_t keylen,
    struct mbuf *m, uint32_t m_offset, uint8_t *digest, uint32_t trailer);
extern uint32_t
sctp_compute_hmac_m(uint16_t hmac_algo, sctp_key_t *key,
    struct mbuf *m, uint32_t m_offset, uint8_t *digest);

/*
 * authentication routines
 */
extern void sctp_clear_cachedkeys(struct sctp_tcb *stcb, uint16_t keyid);
extern void sctp_clear_cachedkeys_ep(struct sctp_inpcb *inp, uint16_t keyid);
extern int sctp_delete_sharedkey(struct sctp_tcb *stcb, uint16_t keyid);
extern int sctp_delete_sharedkey_ep(struct sctp_inpcb *inp, uint16_t keyid);
extern int sctp_auth_setactivekey(struct sctp_tcb *stcb, uint16_t keyid);
extern int sctp_auth_setactivekey_ep(struct sctp_inpcb *inp, uint16_t keyid);
extern int sctp_deact_sharedkey(struct sctp_tcb *stcb, uint16_t keyid);
extern int sctp_deact_sharedkey_ep(struct sctp_inpcb *inp, uint16_t keyid);

extern void
sctp_auth_get_cookie_params(struct sctp_tcb *stcb, struct mbuf *m,
    uint32_t offset, uint32_t length);
extern void
sctp_fill_hmac_digest_m(struct mbuf *m, uint32_t auth_offset,
    struct sctp_auth_chunk *auth, struct sctp_tcb *stcb, uint16_t key_id);
extern struct mbuf *
sctp_add_auth_chunk(struct mbuf *m, struct mbuf **m_end,
    struct sctp_auth_chunk **auth_ret, uint32_t *offset,
    struct sctp_tcb *stcb, uint8_t chunk);
extern int
sctp_handle_auth(struct sctp_tcb *stcb, struct sctp_auth_chunk *ch,
    struct mbuf *m, uint32_t offset);
extern void
sctp_notify_authentication(struct sctp_tcb *stcb,
    uint32_t indication, uint16_t keyid, uint16_t alt_keyid, int so_locked);
extern int
sctp_validate_init_auth_params(struct mbuf *m, int offset,
    int limit);
extern void
sctp_initialize_auth_params(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb);

/* test functions */
#endif				/* __SCTP_AUTH_H__ */
