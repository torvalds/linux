/*	$OpenBSD: wg_noise.h,v 1.3 2024/03/05 17:48:01 mvs Exp $ */
/*
 * Copyright (C) 2015-2020 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2020 Matt Dunwoodie <ncon@noconroy.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __NOISE_H__
#define __NOISE_H__

#include <sys/types.h>
#include <sys/time.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>

#include <crypto/blake2s.h>
#include <crypto/chachapoly.h>
#include <crypto/curve25519.h>

#define NOISE_PUBLIC_KEY_LEN	CURVE25519_KEY_SIZE
#define NOISE_SYMMETRIC_KEY_LEN	CHACHA20POLY1305_KEY_SIZE
#define NOISE_TIMESTAMP_LEN	(sizeof(uint64_t) + sizeof(uint32_t))
#define NOISE_AUTHTAG_LEN	CHACHA20POLY1305_AUTHTAG_SIZE
#define NOISE_HASH_LEN		BLAKE2S_HASH_SIZE

/* Protocol string constants */
#define NOISE_HANDSHAKE_NAME	"Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s"
#define NOISE_IDENTIFIER_NAME	"WireGuard v1 zx2c4 Jason@zx2c4.com"

/* Constants for the counter */
#define COUNTER_BITS_TOTAL	8192
#define COUNTER_BITS		(sizeof(unsigned long) * 8)
#define COUNTER_NUM		(COUNTER_BITS_TOTAL / COUNTER_BITS)
#define COUNTER_WINDOW_SIZE	(COUNTER_BITS_TOTAL - COUNTER_BITS)

/* Constants for the keypair */
#define REKEY_AFTER_MESSAGES	(1ull << 60)
#define REJECT_AFTER_MESSAGES	(UINT64_MAX - COUNTER_WINDOW_SIZE - 1)
#define REKEY_AFTER_TIME	120
#define REKEY_AFTER_TIME_RECV	165
#define REJECT_AFTER_TIME	180
#define REJECT_INTERVAL		(1000000000 / 50) /* fifty times per sec */
/* 24 = floor(log2(REJECT_INTERVAL)) */
#define REJECT_INTERVAL_MASK	(~((1ull<<24)-1))

enum noise_state_hs {
	HS_ZEROED = 0,
	CREATED_INITIATION,
	CONSUMED_INITIATION,
	CREATED_RESPONSE,
	CONSUMED_RESPONSE,
};

struct noise_handshake {
	enum noise_state_hs	 hs_state;
	uint32_t		 hs_local_index;
	uint32_t		 hs_remote_index;
	uint8_t		 	 hs_e[NOISE_PUBLIC_KEY_LEN];
	uint8_t		 	 hs_hash[NOISE_HASH_LEN];
	uint8_t		 	 hs_ck[NOISE_HASH_LEN];
};

struct noise_counter {
	struct mutex		 c_mtx;
	uint64_t		 c_send;
	uint64_t		 c_recv;
	unsigned long		 c_backtrack[COUNTER_NUM];
};

struct noise_keypair {
	SLIST_ENTRY(noise_keypair)	kp_entry;
	int				kp_valid;
	int				kp_is_initiator;
	uint32_t			kp_local_index;
	uint32_t			kp_remote_index;
	uint8_t				kp_send[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t				kp_recv[NOISE_SYMMETRIC_KEY_LEN];
	struct timespec			kp_birthdate; /* nanouptime */
	struct noise_counter		kp_ctr;
};

struct noise_remote {
	uint8_t				 r_public[NOISE_PUBLIC_KEY_LEN];
	struct noise_local		*r_local;
	uint8_t		 		 r_ss[NOISE_PUBLIC_KEY_LEN];

	struct rwlock			 r_handshake_lock;
	struct noise_handshake		 r_handshake;
	uint8_t				 r_psk[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t				 r_timestamp[NOISE_TIMESTAMP_LEN];
	struct timespec			 r_last_init; /* nanouptime */

	struct mutex			 r_keypair_mtx;
	SLIST_HEAD(,noise_keypair)	 r_unused_keypairs;
	struct noise_keypair		*r_next, *r_current, *r_previous;
	struct noise_keypair		 r_keypair[3]; /* 3: next, current, previous. */

};

struct noise_local {
	struct rwlock		l_identity_lock;
	int			l_has_identity;
	uint8_t			l_public[NOISE_PUBLIC_KEY_LEN];
	uint8_t			l_private[NOISE_PUBLIC_KEY_LEN];

	struct noise_upcall {
		void	 *u_arg;
		struct noise_remote *
			(*u_remote_get)(void *, uint8_t[NOISE_PUBLIC_KEY_LEN]);
		uint32_t
			(*u_index_set)(void *, struct noise_remote *);
		void	(*u_index_drop)(void *, uint32_t);
	}			l_upcall;
};

/* Set/Get noise parameters */
void	noise_local_init(struct noise_local *, struct noise_upcall *);
void	noise_local_lock_identity(struct noise_local *);
void	noise_local_unlock_identity(struct noise_local *);
int	noise_local_set_private(struct noise_local *, uint8_t[NOISE_PUBLIC_KEY_LEN]);
int	noise_local_keys(struct noise_local *, uint8_t[NOISE_PUBLIC_KEY_LEN],
	    uint8_t[NOISE_PUBLIC_KEY_LEN]);

void	noise_remote_init(struct noise_remote *, uint8_t[NOISE_PUBLIC_KEY_LEN],
	    struct noise_local *);
int	noise_remote_set_psk(struct noise_remote *, uint8_t[NOISE_SYMMETRIC_KEY_LEN]);
int	noise_remote_keys(struct noise_remote *, uint8_t[NOISE_PUBLIC_KEY_LEN],
	    uint8_t[NOISE_SYMMETRIC_KEY_LEN]);

/* Should be called anytime noise_local_set_private is called */
void	noise_remote_precompute(struct noise_remote *);

/* Cryptographic functions */
int	noise_create_initiation(
	    struct noise_remote *,
	    uint32_t *s_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
	    uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN]);

int	noise_consume_initiation(
	    struct noise_local *,
	    struct noise_remote **,
	    uint32_t s_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
	    uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN]);

int	noise_create_response(
	    struct noise_remote *,
	    uint32_t *s_idx,
	    uint32_t *r_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t en[0 + NOISE_AUTHTAG_LEN]);

int	noise_consume_response(
	    struct noise_remote *,
	    uint32_t s_idx,
	    uint32_t r_idx,
	    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
	    uint8_t en[0 + NOISE_AUTHTAG_LEN]);

int	noise_remote_begin_session(struct noise_remote *);
void	noise_remote_clear(struct noise_remote *);
void	noise_remote_expire_current(struct noise_remote *);

int	noise_remote_ready(struct noise_remote *);

int	noise_remote_encrypt(
	    struct noise_remote *,
	    uint32_t *r_idx,
	    uint64_t *nonce,
	    uint8_t *buf,
	    size_t buflen);
int	noise_remote_decrypt(
	    struct noise_remote *,
	    uint32_t r_idx,
	    uint64_t nonce,
	    uint8_t *buf,
	    size_t buflen);

#ifdef WGTEST
void	noise_test();
#endif /* WGTEST */

#endif /* __NOISE_H__ */
