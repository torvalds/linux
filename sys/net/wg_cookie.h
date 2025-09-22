/*	$OpenBSD: wg_cookie.h,v 1.2 2020/12/09 05:53:33 tb Exp $ */
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

#ifndef __COOKIE_H__
#define __COOKIE_H__

#include <sys/types.h>
#include <sys/time.h>
#include <sys/rwlock.h>
#include <sys/queue.h>

#include <netinet/in.h>

#include <crypto/chachapoly.h>
#include <crypto/blake2s.h>
#include <crypto/siphash.h>

#define COOKIE_MAC_SIZE		16
#define COOKIE_KEY_SIZE		32
#define COOKIE_NONCE_SIZE	XCHACHA20POLY1305_NONCE_SIZE
#define COOKIE_COOKIE_SIZE	16
#define COOKIE_SECRET_SIZE	32
#define COOKIE_INPUT_SIZE	32
#define COOKIE_ENCRYPTED_SIZE	(COOKIE_COOKIE_SIZE + COOKIE_MAC_SIZE)

#define COOKIE_MAC1_KEY_LABEL	"mac1----"
#define COOKIE_COOKIE_KEY_LABEL	"cookie--"
#define COOKIE_SECRET_MAX_AGE	120
#define COOKIE_SECRET_LATENCY	5

/* Constants for initiation rate limiting */
#define RATELIMIT_SIZE		(1 << 13)
#define RATELIMIT_SIZE_MAX	(RATELIMIT_SIZE * 8)
#define NSEC_PER_SEC		1000000000LL
#define INITIATIONS_PER_SECOND	20
#define INITIATIONS_BURSTABLE	5
#define INITIATION_COST		(NSEC_PER_SEC / INITIATIONS_PER_SECOND)
#define TOKEN_MAX		(INITIATION_COST * INITIATIONS_BURSTABLE)
#define ELEMENT_TIMEOUT		1
#define IPV4_MASK_SIZE		4 /* Use all 4 bytes of IPv4 address */
#define IPV6_MASK_SIZE		8 /* Use top 8 bytes (/64) of IPv6 address */

struct cookie_macs {
	uint8_t	mac1[COOKIE_MAC_SIZE];
	uint8_t	mac2[COOKIE_MAC_SIZE];
};

struct ratelimit_entry {
	LIST_ENTRY(ratelimit_entry)	 r_entry;
	sa_family_t			 r_af;
	union {
		struct in_addr		 r_in;
#ifdef INET6
		struct in6_addr		 r_in6;
#endif
	};
	struct timespec			 r_last_time;	/* nanouptime */
	uint64_t			 r_tokens;
};

struct ratelimit {
	SIPHASH_KEY			 rl_secret;
	struct pool			*rl_pool;

	struct rwlock			 rl_lock;
	LIST_HEAD(, ratelimit_entry)	*rl_table;
	u_long				 rl_table_mask;
	size_t				 rl_table_num;
	struct timespec			 rl_last_gc;	/* nanouptime */
};

struct cookie_maker {
	uint8_t		cp_mac1_key[COOKIE_KEY_SIZE];
	uint8_t		cp_cookie_key[COOKIE_KEY_SIZE];

	struct rwlock	cp_lock;
	uint8_t		cp_cookie[COOKIE_COOKIE_SIZE];
	struct timespec	cp_birthdate;	/* nanouptime */
	int		cp_mac1_valid;
	uint8_t		cp_mac1_last[COOKIE_MAC_SIZE];
};

struct cookie_checker {
	struct ratelimit	cc_ratelimit_v4;
#ifdef INET6
	struct ratelimit	cc_ratelimit_v6;
#endif

	struct rwlock		cc_key_lock;
	uint8_t			cc_mac1_key[COOKIE_KEY_SIZE];
	uint8_t			cc_cookie_key[COOKIE_KEY_SIZE];

	struct rwlock		cc_secret_lock;
	struct timespec		cc_secret_birthdate;	/* nanouptime */
	uint8_t			cc_secret[COOKIE_SECRET_SIZE];
};

void	cookie_maker_init(struct cookie_maker *, uint8_t[COOKIE_INPUT_SIZE]);
int	cookie_checker_init(struct cookie_checker *, struct pool *);
void	cookie_checker_update(struct cookie_checker *,
	    uint8_t[COOKIE_INPUT_SIZE]);
void	cookie_checker_deinit(struct cookie_checker *);
void	cookie_checker_create_payload(struct cookie_checker *,
	    struct cookie_macs *cm, uint8_t[COOKIE_NONCE_SIZE],
	    uint8_t [COOKIE_ENCRYPTED_SIZE], struct sockaddr *);
int	cookie_maker_consume_payload(struct cookie_maker *,
	    uint8_t[COOKIE_NONCE_SIZE], uint8_t[COOKIE_ENCRYPTED_SIZE]);
void	cookie_maker_mac(struct cookie_maker *, struct cookie_macs *,
	    void *, size_t);
int	cookie_checker_validate_macs(struct cookie_checker *,
	    struct cookie_macs *, void *, size_t, int, struct sockaddr *);

#ifdef WGTEST
void	cookie_test();
#endif /* WGTEST */

#endif /* __COOKIE_H__ */
