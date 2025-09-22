/*
 * edns.c -- EDNS definitions (RFC 2671).
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */


#include "config.h"

#include <string.h>
#ifdef HAVE_SSL
#include <openssl/opensslv.h>
#include <openssl/evp.h>
#endif

#include "dns.h"
#include "edns.h"
#include "nsd.h"
#include "query.h"

#if !defined(HAVE_SSL) || !defined(HAVE_CRYPTO_MEMCMP)
/* we need fixed time compare, pull it in from tsig.c */
#define CRYPTO_memcmp memcmp_fixedtime
int memcmp_fixedtime(const void *s1, const void *s2, size_t n);
#endif

void
edns_init_data(edns_data_type *data, uint16_t max_length)
{
	memset(data, 0, sizeof(edns_data_type));
	/* record type: OPT */
	data->ok[1] = (TYPE_OPT & 0xff00) >> 8;	/* type_hi */
	data->ok[2] = TYPE_OPT & 0x00ff;	/* type_lo */
	/* udp payload size */
	data->ok[3] = (max_length & 0xff00) >> 8; /* size_hi */
	data->ok[4] = max_length & 0x00ff;	  /* size_lo */

	data->error[1] = (TYPE_OPT & 0xff00) >> 8;	/* type_hi */
	data->error[2] = TYPE_OPT & 0x00ff;		/* type_lo */
	data->error[3] = (max_length & 0xff00) >> 8;	/* size_hi */
	data->error[4] = max_length & 0x00ff;		/* size_lo */
	data->error[5] = 1;	/* XXX Extended RCODE=BAD VERS */

	/* COOKIE OPT HDR */
	data->cookie[0] = (COOKIE_CODE & 0xff00) >> 8;
	data->cookie[1] = (COOKIE_CODE & 0x00ff);
	data->cookie[2] = (24 & 0xff00) >> 8;
	data->cookie[3] = (24 & 0x00ff);
}

void
edns_init_nsid(edns_data_type *data, uint16_t nsid_len)
{
       /* NSID OPT HDR */
       data->nsid[0] = (NSID_CODE & 0xff00) >> 8;
       data->nsid[1] = (NSID_CODE & 0x00ff);
       data->nsid[2] = (nsid_len & 0xff00) >> 8;
       data->nsid[3] = (nsid_len & 0x00ff);
}

void
edns_init_record(edns_record_type *edns)
{
	edns->status = EDNS_NOT_PRESENT;
	edns->position = 0;
	edns->maxlen = 0;
	edns->opt_reserved_space = 0;
	edns->dnssec_ok = 0;
	edns->nsid = 0;
	edns->zoneversion = 0;
	edns->cookie_status = COOKIE_NOT_PRESENT;
	edns->cookie_len = 0;
	edns->ede = -1; /* -1 means no Extended DNS Error */
	edns->ede_text = NULL;
	edns->ede_text_len = 0;
}

/** handle a single edns option in the query */
static int
edns_handle_option(uint16_t optcode, uint16_t optlen, buffer_type* packet,
	edns_record_type* edns, struct query* query, nsd_type* nsd)
{
	(void) query; /* in case edns options need the query structure */
	/* handle opt code and read the optlen bytes from the packet */
	switch(optcode) {
	case NSID_CODE:
		/* is NSID enabled? */
		if(nsd->nsid_len > 0) {
			edns->nsid = 1;
			/* we have to check optlen, and move the buffer along */
			buffer_skip(packet, optlen);
			/* in the reply we need space for optcode+optlen+nsid_bytes */
			edns->opt_reserved_space += OPT_HDR + nsd->nsid_len;
		} else {
			/* ignore option */
			buffer_skip(packet, optlen);
		}
		break;
	case COOKIE_CODE:
		/* Cookies enabled? */
		if(nsd->do_answer_cookie) {
			if (optlen == 8) 
				edns->cookie_status = COOKIE_INVALID;
			else if (optlen < 16 || optlen > 40)
				return 0; /* FORMERR */
			else
				edns->cookie_status = COOKIE_UNVERIFIED;

			edns->cookie_len = optlen;
			memcpy(edns->cookie, buffer_current(packet), optlen);
			buffer_skip(packet, optlen);
			edns->opt_reserved_space += OPT_HDR + 24;
		} else {
			buffer_skip(packet, optlen);
		}
		break;
	case ZONEVERSION_CODE:
		edns->zoneversion = 1;
		if(optlen > 0)
			return 0;
		break;
	default:
		buffer_skip(packet, optlen);
		break;
	}
	return 1;
}

int
edns_parse_record(edns_record_type *edns, buffer_type *packet,
	query_type* query, nsd_type* nsd)
{
	/* OPT record type... */
	uint8_t  opt_owner;
	uint16_t opt_type;
	uint16_t opt_class;
	uint8_t  opt_version;
	uint16_t opt_flags;
	uint16_t opt_rdlen;

	edns->position = buffer_position(packet);

	if (!buffer_available(packet, (OPT_LEN + OPT_RDATA)))
		return 0;

	opt_owner = buffer_read_u8(packet);
	opt_type = buffer_read_u16(packet);
	if (opt_owner != 0 || opt_type != TYPE_OPT) {
		/* Not EDNS.  */
		buffer_set_position(packet, edns->position);
		return 0;
	}

	opt_class = buffer_read_u16(packet);
	(void)buffer_read_u8(packet); /* opt_extended_rcode */
	opt_version = buffer_read_u8(packet);
	opt_flags = buffer_read_u16(packet);
	opt_rdlen = buffer_read_u16(packet);

	if (opt_version != 0) {
		/* The only error is VERSION not implemented */
		edns->status = EDNS_ERROR;
		return 1;
	}

	if (opt_rdlen > 0) {
		if(!buffer_available(packet, opt_rdlen))
			return 0;
		if(opt_rdlen > 65530)
			return 0;
		/* there is more to come, read opt code */
		while(opt_rdlen >= 4) {
			uint16_t optcode = buffer_read_u16(packet);
			uint16_t optlen = buffer_read_u16(packet);
			opt_rdlen -= 4;
			if(opt_rdlen < optlen)
				return 0; /* opt too long, formerr */
			opt_rdlen -= optlen;
			if(!edns_handle_option(optcode, optlen, packet,
				edns, query, nsd))
				return 0;
		}
		if(opt_rdlen != 0)
			return 0;
	}

	edns->status = EDNS_OK;
	edns->maxlen = opt_class;
	edns->dnssec_ok = opt_flags & DNSSEC_OK_MASK;
	return 1;
}

size_t
edns_reserved_space(edns_record_type *edns)
{
	/* MIEK; when a pkt is too large?? */
	return edns->status == EDNS_NOT_PRESENT ? 0
	     : (OPT_LEN + OPT_RDATA + edns->opt_reserved_space);
}

int siphash(const uint8_t *in, const size_t inlen,
                const uint8_t *k, uint8_t *out, const size_t outlen);

/** RFC 1982 comparison, uses unsigned integers, and tries to avoid
 * compiler optimization (eg. by avoiding a-b<0 comparisons),
 * this routine matches compare_serial(), for SOA serial number checks */
static int
compare_1982(uint32_t a, uint32_t b)
{
	/* for 32 bit values */
	const uint32_t cutoff = ((uint32_t) 1 << (32 - 1));

	if (a == b) {
		return 0;
	} else if ((a < b && b - a < cutoff) || (a > b && a - b > cutoff)) {
		return -1;
	} else {
		return 1;
	}
}

/** if we know that b is larger than a, return the difference between them,
 * that is the distance between them. in RFC1982 arith */
static uint32_t
subtract_1982(uint32_t a, uint32_t b)
{
	/* for 32 bit values */
	const uint32_t cutoff = ((uint32_t) 1 << (32 - 1));

	if(a == b)
		return 0;
	if(a < b && b - a < cutoff) {
		return b-a;
	}
	if(a > b && a - b > cutoff) {
		return ((uint32_t)0xffffffff) - (a-b-1);
	}
	/* wrong case, b smaller than a */
	return 0;
}

void cookie_verify(query_type *q, struct nsd* nsd, uint32_t *now_p) {
	uint8_t hash[8], hash2verify[8];
	uint32_t cookie_time, now_uint32;
	size_t verify_size;
	int i;

	/* We support only draft-sury-toorop-dnsop-server-cookies sizes */
	if(q->edns.cookie_len != 24)
		return;

	if(q->edns.cookie[8] != 1)
		return;

	q->edns.cookie_status = COOKIE_INVALID;

	cookie_time = (q->edns.cookie[12] << 24)
	            | (q->edns.cookie[13] << 16)
	            | (q->edns.cookie[14] <<  8)
	            |  q->edns.cookie[15];
	
	now_uint32 = *now_p ? *now_p : (*now_p = (uint32_t)time(NULL));

	if(compare_1982(now_uint32, cookie_time) > 0) {
		/* ignore cookies > 1 hour in past */
		if (subtract_1982(cookie_time, now_uint32) > 3600)
			return;
	} else if (subtract_1982(now_uint32, cookie_time) > 300) {
		/* ignore cookies > 5 minutes in future */
		return;
	}

	memcpy(hash2verify, q->edns.cookie + 16, 8);

#ifdef INET6
	if(q->client_addr.ss_family == AF_INET6) {
		memcpy(q->edns.cookie + 16, &((struct sockaddr_in6 *)&q->client_addr)->sin6_addr, 16);
		verify_size = 32;
	} else {
		memcpy(q->edns.cookie + 16, &((struct sockaddr_in *)&q->client_addr)->sin_addr, 4);
		verify_size = 20;
	}
#else
	memcpy( q->edns.cookie + 16, &q->client_addr.sin_addr, 4);
	verify_size = 20;
#endif

	q->edns.cookie_status = COOKIE_INVALID;
	siphash(q->edns.cookie, verify_size,
		nsd->cookie_secrets[0].cookie_secret, hash, 8);
	if(CRYPTO_memcmp(hash2verify, hash, 8) == 0 ) {
		if (subtract_1982(cookie_time, now_uint32) < 1800) {
			q->edns.cookie_status = COOKIE_VALID_REUSE;
			memcpy(q->edns.cookie + 16, hash, 8);
		} else
			q->edns.cookie_status = COOKIE_VALID;
		return;
	}
	for(i = 1;
	    i < (int)nsd->cookie_count && i < NSD_COOKIE_HISTORY_SIZE;
	    i++) {
		siphash(q->edns.cookie, verify_size,
		        nsd->cookie_secrets[i].cookie_secret, hash, 8);
		if(CRYPTO_memcmp(hash2verify, hash, 8) == 0 ) {
			q->edns.cookie_status = COOKIE_VALID;
			return;
		}
	}
}

void cookie_create(query_type *q, struct nsd* nsd, uint32_t *now_p)
{
	uint8_t  hash[8];
	uint32_t now_uint32;
       
	if (q->edns.cookie_status == COOKIE_VALID_REUSE)
		return;

	now_uint32 = *now_p ? *now_p : (*now_p = (uint32_t)time(NULL));
	q->edns.cookie[ 8] = 1;
	q->edns.cookie[ 9] = 0;
	q->edns.cookie[10] = 0;
	q->edns.cookie[11] = 0;
	q->edns.cookie[12] = (now_uint32 & 0xFF000000) >> 24;
	q->edns.cookie[13] = (now_uint32 & 0x00FF0000) >> 16;
	q->edns.cookie[14] = (now_uint32 & 0x0000FF00) >>  8;
	q->edns.cookie[15] =  now_uint32 & 0x000000FF;
#ifdef INET6
	if (q->client_addr.ss_family == AF_INET6) {
		memcpy( q->edns.cookie + 16
		      , &((struct sockaddr_in6 *)&q->client_addr)->sin6_addr, 16);
		siphash(q->edns.cookie, 32, nsd->cookie_secrets[0].cookie_secret, hash, 8);
	} else {
		memcpy( q->edns.cookie + 16
		      , &((struct sockaddr_in *)&q->client_addr)->sin_addr, 4);
		siphash(q->edns.cookie, 20, nsd->cookie_secrets[0].cookie_secret, hash, 8);
	}
#else
	memcpy( q->edns.cookie + 16, &q->client_addr.sin_addr, 4);
	siphash(q->edns.cookie, 20, nsd->cookie_secrets[0].cookie_secret, hash, 8);
#endif
	memcpy(q->edns.cookie + 16, hash, 8);
}

