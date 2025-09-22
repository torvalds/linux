/* rrl.h - Response Rate Limiting for NSD.
 * By W.C.A. Wijngaards
 * Copyright 2012, NLnet Labs.
 * BSD, see LICENSE.
 */
#ifndef RRL_H
#define RRL_H
#include "query.h"

/** the classification types for the rrl */
enum rrl_type {
	/* classification types */
	rrl_type_nxdomain	= 0x01,
	rrl_type_error		= 0x02,
	rrl_type_referral	= 0x04,
	rrl_type_any		= 0x08,
	rrl_type_wildcard	= 0x10,
	rrl_type_nodata		= 0x20,
	rrl_type_dnskey		= 0x40,
	rrl_type_positive	= 0x80,
	rrl_type_rrsig		= 0x100,

	/* all classification types */
	rrl_type_all		= 0x1ff,
	/* to distinguish between ip4 and ip6 netblocks, used in code */
	rrl_ip6			= 0x8000
};

/** Number of buckets */
#define RRL_BUCKETS 1000000
/** default rrl limit, in 2x qps , the default is 200 qps */
#define RRL_LIMIT 400
/** default slip */
#define RRL_SLIP 2
/** default prefix lengths */
#define RRL_IPV4_PREFIX_LENGTH 24
#define RRL_IPV6_PREFIX_LENGTH 64
/** default whitelist rrl limit, in 2x qps, default is thus 2000 qps */
#define RRL_WLIST_LIMIT 4000

/**
 * Initialize for n children (optional, otherwise no mmaps used)
 * ratelimits lm and wlm are in qps (this routines x2s them for internal use).
 * plf and pls are in prefix lengths.
 */
void rrl_mmap_init(int numch, size_t numbuck, size_t lm, size_t wlm, size_t sm,
	size_t plf, size_t pls);

/**
 * Initialize rate limiting (for this child server process)
 */
void rrl_init(size_t ch);

/** deinit (for this child server process) */
void rrl_deinit(size_t ch);

/** deinit mmaps for n children */
void rrl_mmap_deinit(void);
/** frees memory but keeps mmap in place (for other processes) */
void rrl_mmap_deinit_keep_mmap(void);

/**
 * Process query that happens, the query structure contains the
 * information about the query and the answer.
 * returns true if the query is ratelimited.
 */
int rrl_process_query(query_type* query);

/**
 * Deny the query, with slip.
 * Returns DISCARD or PROCESSED(with TC flag).
 */
query_state_type rrl_slip(query_type* query);

/** convert classification type to string */
const char* rrltype2str(enum rrl_type c);
/** convert string to classification type */
enum rrl_type rrlstr2type(const char* s);

/** for unit test, update rrl bucket; return rate */
uint32_t rrl_update(query_type* query, uint32_t hash, uint64_t source,
	uint16_t flags, int32_t now, uint32_t lm);
/** set the rate limit counters, pass variables in qps */
void rrl_set_limit(size_t lm, size_t wlm, size_t sm);

#endif /* RRL_H */
