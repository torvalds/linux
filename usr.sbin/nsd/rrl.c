
/* rrl.c - Response Rate Limiting for NSD.
 * By W.C.A. Wijngaards
 * Copyright 2012, NLnet Labs.
 * BSD, see LICENSE.
 */
#include "config.h"
#include <errno.h>
#include "rrl.h"
#include "util.h"
#include "lookup3.h"
#include "options.h"

#ifdef RATELIMIT

#ifdef HAVE_MMAP
#include <sys/mman.h>
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS   MAP_ANON
#endif
#endif /* HAVE_MMAP */


/**
 * The rate limiting data structure bucket, this represents one rate of
 * packets from a single source.
 * Smoothed average rates.
 */
struct rrl_bucket {
	/* the source netmask */
	uint64_t source;
	/* rate, in queries per second, which due to rate=r(t)+r(t-1)/2 is
	 * equal to double the queries per second */
	uint32_t rate;
	/* the full hash */
	uint32_t hash;
	/* counter for queries arrived in this second */
	uint32_t counter;
	/* timestamp, which time is the time of the counter, the rate is from
	 * one timestep before that. */
	int32_t stamp;
	/* flags for the source mask and type */
	uint16_t flags;
};

/* the (global) array of RRL buckets */
static struct rrl_bucket* rrl_array = NULL;
static size_t rrl_array_size = RRL_BUCKETS;
static uint32_t rrl_ratelimit = RRL_LIMIT; /* 2x qps */
static uint8_t rrl_slip_ratio = RRL_SLIP;
static uint8_t rrl_ipv4_prefixlen = RRL_IPV4_PREFIX_LENGTH;
static uint8_t rrl_ipv6_prefixlen = RRL_IPV6_PREFIX_LENGTH;
static uint64_t rrl_ipv6_mask; /* max prefixlen 64 */
static uint32_t rrl_whitelist_ratelimit = RRL_WLIST_LIMIT; /* 2x qps */

/* the array of mmaps for the children (saved between reloads) */
static void** rrl_maps = NULL;
static size_t rrl_maps_num = 0;

void rrl_mmap_init(int numch, size_t numbuck, size_t lm, size_t wlm, size_t sm,
	size_t plf, size_t pls)
{
#ifdef HAVE_MMAP
	size_t i;
#endif
	if(numbuck != 0)
		rrl_array_size = numbuck;
	rrl_ratelimit = lm*2;
	rrl_slip_ratio = sm;
	rrl_ipv4_prefixlen = plf;
	rrl_ipv6_prefixlen = pls;
	if (pls <= 32) {
		rrl_ipv6_mask = ((uint64_t) htonl(0xffffffff << (32-pls))) << 32;
	} else {
		rrl_ipv6_mask =  ((uint64_t) htonl(0xffffffff << (64-pls))) |
			(((uint64_t)0xffffffff)<<32);
	}
	rrl_whitelist_ratelimit = wlm*2;
#ifdef HAVE_MMAP
	/* allocate the ratelimit hashtable in a memory map so it is
	 * preserved across reforks (every child its own table) */
	rrl_maps_num = (size_t)numch;
	rrl_maps = (void**)xmallocarray(rrl_maps_num, sizeof(void*));
	for(i=0; i<rrl_maps_num; i++) {
		rrl_maps[i] = mmap(NULL,
			sizeof(struct rrl_bucket)*rrl_array_size,
			PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
		if(rrl_maps[i] == MAP_FAILED) {
			log_msg(LOG_ERR, "rrl: mmap failed: %s",
				strerror(errno));
			exit(1);
		}
		memset(rrl_maps[i], 0,
			sizeof(struct rrl_bucket)*rrl_array_size);
	}
#else
	(void)numch;
	rrl_maps_num = 0;
	rrl_maps = NULL;
#endif
}

void rrl_mmap_deinit(void)
{
#ifdef HAVE_MMAP
	size_t i;
	for(i=0; i<rrl_maps_num; i++) {
		munmap(rrl_maps[i], sizeof(struct rrl_bucket)*rrl_array_size);
		rrl_maps[i] = NULL;
	}
	free(rrl_maps);
	rrl_maps = NULL;
#endif
}

void rrl_mmap_deinit_keep_mmap(void)
{
#ifdef HAVE_MMAP
	free(rrl_maps);
	rrl_maps = NULL;
#endif
}

void rrl_set_limit(size_t lm, size_t wlm, size_t sm)
{
	rrl_ratelimit = lm*2;
	rrl_whitelist_ratelimit = wlm*2;
	rrl_slip_ratio = sm;
}

void rrl_init(size_t ch)
{
	if(!rrl_maps || ch >= rrl_maps_num)
	    rrl_array = xalloc_array_zero(sizeof(struct rrl_bucket),
	    	rrl_array_size);
#ifdef HAVE_MMAP
	else rrl_array = (struct rrl_bucket*)rrl_maps[ch];
#endif
}

void rrl_deinit(size_t ch)
{
	if(!rrl_maps || ch >= rrl_maps_num)
		free(rrl_array);
	rrl_array = NULL;
}

/** return the source netblock of the query, this is the genuine source
 * for genuine queries and the target for reflected packets */
static uint64_t rrl_get_source(query_type* query, uint16_t* c2)
{
	/* note there is an IPv6 subnet, that maps
	 * to the same buckets as IPv4 space, but there is a flag in c2
	 * that makes the hash different */
#ifdef INET6
	if( ((struct sockaddr_in*)&query->client_addr)->sin_family == AF_INET) {
		*c2 = 0;
		return ((struct sockaddr_in*)&query->client_addr)->
			sin_addr.s_addr & htonl(0xffffffff << (32-rrl_ipv4_prefixlen));
	} else {
		uint64_t s;
		*c2 = rrl_ip6;
		memmove(&s, &((struct sockaddr_in6*)&query->client_addr)->sin6_addr,
			sizeof(s));
		return s & rrl_ipv6_mask;
	}
#else
	*c2 = 0;
	return query->client_addr.sin_addr.s_addr & htonl(0xffffffff << (32-rrl_ipv4_prefixlen));
#endif
}

/** debug source to string */
static const char* rrlsource2str(uint64_t s, uint16_t c2)
{
	static char buf[64];
	struct in_addr a4;
#ifdef INET6
	if(c2) {
		/* IPv6 */
		struct in6_addr a6;
		memset(&a6, 0, sizeof(a6));
		memmove(&a6, &s, sizeof(s));
		if(!inet_ntop(AF_INET6, &a6, buf, sizeof(buf)))
			strlcpy(buf, "[ip6 ntop failed]", sizeof(buf));
		else {
			static char prefix[5];
			snprintf(prefix, sizeof(prefix), "/%d", rrl_ipv6_prefixlen);
			strlcat(buf, &prefix[0], sizeof(buf));
		}
		return buf;
	}
#else
	(void)c2;
#endif
	/* ipv4 */
	a4.s_addr = (uint32_t)s;
	if(!inet_ntop(AF_INET, &a4, buf, sizeof(buf)))
		strlcpy(buf, "[ip4 ntop failed]", sizeof(buf));
	else {
		static char prefix[5];
		snprintf(prefix, sizeof(prefix), "/%d", rrl_ipv4_prefixlen);
		strlcat(buf, &prefix[0], sizeof(buf));
	}
	return buf;
}

enum rrl_type rrlstr2type(const char* s)
{
	if(strcmp(s, "nxdomain")==0) return rrl_type_nxdomain;
	else if(strcmp(s, "error")==0) return rrl_type_error;
	else if(strcmp(s, "referral")==0) return rrl_type_referral;
	else if(strcmp(s, "any")==0) return rrl_type_any;
	else if(strcmp(s, "wildcard")==0) return rrl_type_wildcard;
	else if(strcmp(s, "nodata")==0) return rrl_type_nodata;
	else if(strcmp(s, "dnskey")==0) return rrl_type_dnskey;
	else if(strcmp(s, "positive")==0) return rrl_type_positive;
	else if(strcmp(s, "rrsig")==0) return rrl_type_rrsig;
	else if(strcmp(s, "all")==0) return rrl_type_all;
	return 0; /* unknown */
}

const char* rrltype2str(enum rrl_type c)
{
	switch(c & 0x0fff) {
		case rrl_type_nxdomain: return "nxdomain";
		case rrl_type_error: return "error";
		case rrl_type_referral: return "referral";
		case rrl_type_any: return "any";
		case rrl_type_wildcard: return "wildcard";
		case rrl_type_nodata: return "nodata";
		case rrl_type_dnskey: return "dnskey";
		case rrl_type_positive: return "positive";
		case rrl_type_rrsig: return "rrsig";
		case rrl_type_all: return "all";
	}
	return "unknown";
}

/** classify the query in a number of different types, each has separate
 * ratelimiting, so that positive queries are not impeded by others */
static uint16_t rrl_classify(query_type* query, const uint8_t** d,
	size_t* d_len)
{
	if(RCODE(query->packet) == RCODE_NXDOMAIN) {
		if(query->zone && query->zone->apex) {
			*d = dname_name(domain_dname(query->zone->apex));
			*d_len = domain_dname(query->zone->apex)->name_size;
		}
		return rrl_type_nxdomain;
	}
	if(RCODE(query->packet) != RCODE_OK) {
		if(query->zone && query->zone->apex) {
			*d = dname_name(domain_dname(query->zone->apex));
			*d_len = domain_dname(query->zone->apex)->name_size;
		}
		return rrl_type_error;
	}
	if(query->delegation_domain) {
		*d = dname_name(domain_dname(query->delegation_domain));
		*d_len = domain_dname(query->delegation_domain)->name_size;
		return rrl_type_referral;
	}
	if(query->qtype == TYPE_ANY) {
		if(query->qname) {
			*d = dname_name(query->qname);
			*d_len = query->qname->name_size;
		}
		return rrl_type_any;
	}
	if(query->qtype == TYPE_RRSIG) {
		if(query->qname) {
			*d = dname_name(query->qname);
			*d_len = query->qname->name_size;
		}
		return rrl_type_rrsig;
	}
	if(query->wildcard_domain) {
		*d = dname_name(domain_dname(query->wildcard_domain));
		*d_len = domain_dname(query->wildcard_domain)->name_size;
		return rrl_type_wildcard;
	}
	if(ANCOUNT(query->packet) == 0) {
		if(query->zone && query->zone->apex) {
			*d = dname_name(domain_dname(query->zone->apex));
			*d_len = domain_dname(query->zone->apex)->name_size;
		}
		return rrl_type_nodata;
	}
	if(query->qtype == TYPE_DNSKEY) {
		if(query->qname) {
			*d = dname_name(query->qname);
			*d_len = query->qname->name_size;
		}
		return rrl_type_dnskey;
	}
	/* positive */
	if(query->qname) {
		*d = dname_name(query->qname);
		*d_len = query->qname->name_size;
	}
	return rrl_type_positive;
}

/** Examine the query and return hash and source of netblock. */
static void examine_query(query_type* query, uint32_t* hash, uint64_t* source,
	uint16_t* flags, uint32_t* lm)
{
	/* compile a binary string representing the query */
	uint16_t c, c2;
	/* size with 16 bytes to spare */
	uint8_t buf[MAXDOMAINLEN + sizeof(*source) + sizeof(c) + 16];
	const uint8_t* dname = NULL; size_t dname_len = 0;
	uint32_t r = 0x267fcd16;

	*source = rrl_get_source(query, &c2);
	c = rrl_classify(query, &dname, &dname_len);
	if(query->zone && query->zone->opts &&
		(query->zone->opts->pattern->rrl_whitelist & c))
		*lm = rrl_whitelist_ratelimit;
	if(*lm == 0) return;
	c |= c2;
	*flags = c;
	memmove(buf, source, sizeof(*source));
	memmove(buf+sizeof(*source), &c, sizeof(c));

	DEBUG(DEBUG_QUERY, 1, (LOG_INFO, "rrl_examine type %s name %s", rrltype2str(c), dname?wiredname2str(dname):"NULL"));

	/* and hash it */
	if(dname && dname_len <= MAXDOMAINLEN) {
		memmove(buf+sizeof(*source)+sizeof(c), dname, dname_len);
		*hash = hashlittle(buf, sizeof(*source)+sizeof(c)+dname_len, r);
	} else
		*hash = hashlittle(buf, sizeof(*source)+sizeof(c), r);
}

/* age the bucket because elapsed time steps have gone by */
static void rrl_attenuate_bucket(struct rrl_bucket* b, int32_t elapsed)
{
	if(elapsed > 16) {
		b->rate = 0;
	} else {
		/* divide rate /2 for every elapsed time step, because
		 * the counters in the inbetween steps were 0 */
		/* r(t) = 0 + 0/2 + 0/4 + .. + oldrate/2^dt */
		b->rate >>= elapsed;
		/* we know that elapsed >= 2 */
		b->rate += (b->counter>>(elapsed-1));
	}
}

/** log a message about ratelimits */
static void
rrl_msg(query_type* query, const char* str)
{
	uint16_t c, c2, wl = 0;
	const uint8_t* d = NULL;
	size_t d_len;
	uint64_t s;
	char address[128];
	if(verbosity < 1) return;
	addr2str(&query->client_addr, address, sizeof(address));
	s = rrl_get_source(query, &c2);
	c = rrl_classify(query, &d, &d_len) | c2;
	if(query->zone && query->zone->opts &&
		(query->zone->opts->pattern->rrl_whitelist & c))
		wl = 1;
	log_msg(LOG_INFO, "ratelimit %s %s type %s%s target %s query %s %s",
		str, d?wiredname2str(d):"", rrltype2str(c),
		wl?"(whitelisted)":"", rrlsource2str(s, c2),
		address, rrtype_to_string(query->qtype));
}

/** true if the query used to be blocked by the ratelimit */
static int
used_to_block(uint32_t rate, uint32_t counter, uint32_t lm)
{
	return rate >= lm || counter+rate/2 >= lm;
}

/** update the rate in a ratelimit bucket, return actual rate */
uint32_t rrl_update(query_type* query, uint32_t hash, uint64_t source,
	uint16_t flags, int32_t now, uint32_t lm)
{
	struct rrl_bucket* b = &rrl_array[hash % rrl_array_size];

	DEBUG(DEBUG_QUERY, 1, (LOG_INFO, "source %llx hash %x oldrate %d oldcount %d stamp %d",
		(long long unsigned)source, hash, b->rate, b->counter, b->stamp));

	/* check if different source */
	if(b->source != source || b->flags != flags || b->hash != hash) {
		/* initialise */
		/* potentially the wrong limit here, used lower nonwhitelim */
		if(verbosity >= 1 &&
			used_to_block(b->rate, b->counter, rrl_ratelimit)) {
			char address[128];
			addr2str(&query->client_addr, address, sizeof(address));
			log_msg(LOG_INFO, "ratelimit unblock ~ type %s target %s query %s %s (%s collision)",
				rrltype2str(b->flags),
				rrlsource2str(b->source, b->flags),
				address, rrtype_to_string(query->qtype),
				(b->hash!=hash?"bucket":"hash"));
		}
		b->hash = hash;
		b->source = source;
		b->flags = flags;
		b->counter = 1;
		b->rate = 0;
		b->stamp = now;
		return 1;
	}
	/* this is the same source */

	/* check if old, zero or smooth it */
	/* circular arith for time */
	if(now - b->stamp == 1) {
		/* very busy bucket and time just stepped one step */
		int oldblock = used_to_block(b->rate, b->counter, lm);
		b->rate = b->rate/2 + b->counter;
		if(oldblock && b->rate < lm)
			rrl_msg(query, "unblock");
		b->counter = 1;
		b->stamp = now;
	} else if(now - b->stamp > 0) {
		/* older bucket */
		int olderblock = used_to_block(b->rate, b->counter, lm);
		rrl_attenuate_bucket(b, now - b->stamp);
		if(olderblock && b->rate < lm)
			rrl_msg(query, "unblock");
		b->counter = 1;
		b->stamp = now;
	} else if(now != b->stamp) {
		/* robust, timestamp from the future */
		if(used_to_block(b->rate, b->counter, lm))
			rrl_msg(query, "unblock");
		b->rate = 0;
		b->counter = 1;
		b->stamp = now;
	} else {
		/* bucket is from the current timestep, update counter */
		b->counter ++;

		/* log what is blocked for operational debugging */
		if(b->counter + b->rate/2 == lm && b->rate < lm)
			rrl_msg(query, "block");
	}

	/* return max from current rate and projected next-value for rate */
	/* so that if the rate increases suddenly very high, it is
	 * stopped halfway into the time step */
	if(b->counter > b->rate/2)
		return b->counter + b->rate/2;
	return b->rate;
}

int rrl_process_query(query_type* query)
{
	uint64_t source;
	uint32_t hash;
	/* we can use circular arithmetic here, so int32 works after 2038 */
	int32_t now = (int32_t)time(NULL);
	uint32_t lm = rrl_ratelimit;
	uint16_t flags;
	if(rrl_ratelimit == 0 && rrl_whitelist_ratelimit == 0)
		return 0;

	/* examine query */
	examine_query(query, &hash, &source, &flags, &lm);

	if(lm == 0)
		return 0; /* no limit for this */

	/* update rate */
	return (rrl_update(query, hash, source, flags, now, lm) >= lm);
}

query_state_type rrl_slip(query_type* query)
{
	/* discard number the packets, randomly */
#ifdef HAVE_ARC4RANDOM_UNIFORM
	if((rrl_slip_ratio > 0) && ((rrl_slip_ratio == 1) || ((arc4random_uniform(rrl_slip_ratio)) == 0))) {
#elif HAVE_ARC4RANDOM
	if((rrl_slip_ratio > 0) && ((rrl_slip_ratio == 1) || ((arc4random() % rrl_slip_ratio) == 0))) {
#else
	if((rrl_slip_ratio > 0) && ((rrl_slip_ratio == 1) || ((random() % rrl_slip_ratio) == 0))) {
#endif
		/* set TC on the rest */
		TC_SET(query->packet);
		ANCOUNT_SET(query->packet, 0);
		NSCOUNT_SET(query->packet, 0);
		ARCOUNT_SET(query->packet, 0);
		if(query->qname)
			/* header, type, class, qname */
			buffer_set_position(query->packet,
				QHEADERSZ+4+query->qname->name_size);
		else 	buffer_set_position(query->packet, QHEADERSZ);
		return QUERY_PROCESSED;
	}
	return QUERY_DISCARDED;
}

#endif /* RATELIMIT */
