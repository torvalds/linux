/*
 * query.c -- nsd(8) the resolver.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>

#include "answer.h"
#include "axfr.h"
#include "dns.h"
#include "dname.h"
#include "nsd.h"
#include "namedb.h"
#include "query.h"
#include "util.h"
#include "options.h"
#include "nsec3.h"
#include "tsig.h"

/* [Bug #253] Adding unnecessary NS RRset may lead to undesired truncation.
 * This function determines if the final response packet needs the NS RRset
 * included. Currently, it will only return negative if QTYPE == DNSKEY|DS.
 * This way, resolvers won't fallback to TCP unnecessarily when priming
 * trust anchors.
 */
static int answer_needs_ns(struct query  *query);

static int add_rrset(struct query  *query,
		     answer_type    *answer,
		     rr_section_type section,
		     domain_type    *owner,
		     rrset_type     *rrset);

static void answer_authoritative(struct nsd	  *nsd,
				 struct query     *q,
				 answer_type      *answer,
				 size_t            domain_number,
				 int               exact,
				 domain_type      *closest_match,
				 domain_type      *closest_encloser,
				 const dname_type *qname);

static void answer_lookup_zone(struct nsd *nsd, struct query *q,
			       answer_type *answer, size_t domain_number,
			       int exact, domain_type *closest_match,
			       domain_type *closest_encloser,
			       const dname_type *qname);

void
query_put_dname_offset(struct query *q, domain_type *domain, uint16_t offset)
{
	assert(q);
	assert(domain);
	assert(domain->number > 0);

	if (offset > MAX_COMPRESSION_OFFSET)
		return;
	if (q->compressed_dname_count >= MAX_COMPRESSED_DNAMES)
		return;

	q->compressed_dname_offsets[domain->number] = offset;
	q->compressed_dnames[q->compressed_dname_count] = domain;
	++q->compressed_dname_count;
}

void
query_clear_dname_offsets(struct query *q, size_t max_offset)
{
	while (q->compressed_dname_count > 0
	       && (q->compressed_dname_offsets[q->compressed_dnames[q->compressed_dname_count - 1]->number]
		   >= max_offset))
	{
		q->compressed_dname_offsets[q->compressed_dnames[q->compressed_dname_count - 1]->number] = 0;
		--q->compressed_dname_count;
	}
}

void
query_clear_compression_tables(struct query *q)
{
	uint16_t i;

	for (i = 0; i < q->compressed_dname_count; ++i) {
		assert(q->compressed_dnames);
		q->compressed_dname_offsets[q->compressed_dnames[i]->number] = 0;
	}
	q->compressed_dname_count = 0;
}

void
query_add_compression_domain(struct query *q, domain_type *domain, uint16_t offset)
{
	while (domain->parent) {
		DEBUG(DEBUG_NAME_COMPRESSION, 2,
		      (LOG_INFO, "query dname: %s, number: %lu, offset: %u\n",
		       domain_to_string(domain),
		       (unsigned long) domain->number,
		       offset));
		query_put_dname_offset(q, domain, offset);
		offset += label_length(dname_name(domain_dname(domain))) + 1;
		domain = domain->parent;
	}
}

/*
 * Generate an error response with the specified RCODE.
 */
query_state_type
query_error (struct query *q, nsd_rc_type rcode)
{
	if (rcode == NSD_RC_DISCARD) {
		return QUERY_DISCARDED;
	}

	buffer_clear(q->packet);

	QR_SET(q->packet);	   /* This is an answer.  */
	AD_CLR(q->packet);
	RCODE_SET(q->packet, (int) rcode); /* Error code.  */

	/* Truncate the question as well... */
	QDCOUNT_SET(q->packet, 0);
	ANCOUNT_SET(q->packet, 0);
	NSCOUNT_SET(q->packet, 0);
	ARCOUNT_SET(q->packet, 0);
	buffer_set_position(q->packet, QHEADERSZ);
	return QUERY_PROCESSED;
}

static int
query_ratelimit_err(nsd_type* nsd)
{
	time_t now = time(NULL);
	if(nsd->err_limit_time == now) {
		/* see if limit is exceeded for this second */
		if(nsd->err_limit_count++ > ERROR_RATELIMIT)
			return 1;
	} else {
		/* new second, new limits */
		nsd->err_limit_time = now;
		nsd->err_limit_count = 1;
	}
	return 0;
}

static query_state_type
query_formerr (struct query *query, nsd_type* nsd)
{
	int opcode = OPCODE(query->packet);
	if(query_ratelimit_err(nsd))
		return QUERY_DISCARDED;
	FLAGS_SET(query->packet, FLAGS(query->packet) & 0x0100U);
			/* Preserve the RD flag. Clear the rest. */
	OPCODE_SET(query->packet, opcode);
	return query_error(query, NSD_RC_FORMAT);
}

static void
query_cleanup(void *data)
{
	query_type *query = (query_type *) data;
	region_destroy(query->region);
}

query_type *
query_create(region_type *region, uint16_t *compressed_dname_offsets,
	size_t compressed_dname_size, domain_type **compressed_dnames)
{
	query_type *query
		= (query_type *) region_alloc_zero(region, sizeof(query_type));
	/* create region with large block size, because the initial chunk
	   saves many mallocs in the server */
	query->region = region_create_custom(xalloc, free, 16384, 16384/8, 32, 0);
	query->compressed_dname_offsets = compressed_dname_offsets;
	query->compressed_dnames = compressed_dnames;
	query->packet = buffer_create(region, QIOBUFSZ);
	region_add_cleanup(region, query_cleanup, query);
	query->compressed_dname_offsets_size = compressed_dname_size;
	tsig_create_record(&query->tsig, region);
	query->tsig_prepare_it = 1;
	query->tsig_update_it = 1;
	query->tsig_sign_it = 1;
	return query;
}

query_type *
query_create_with_buffer(region_type *region,
	uint16_t *compressed_dname_offsets, size_t compressed_dname_size,
	domain_type **compressed_dnames, struct buffer *buffer)
{
	query_type *query
		= (query_type *) region_alloc_zero(region, sizeof(query_type));
	/* create region with large block size, because the initial chunk
	   saves many mallocs in the server */
	query->region = region_create_custom(xalloc, free, 16384, 16384/8, 32, 0);
	region_add_cleanup(region, query_cleanup, query);
	query->compressed_dname_offsets = compressed_dname_offsets;
	query->compressed_dnames = compressed_dnames;
	query->packet = buffer;
	query->compressed_dname_offsets_size = compressed_dname_size;
	tsig_create_record(&query->tsig, region);
	query->tsig_prepare_it = 1;
	query->tsig_update_it = 1;
	query->tsig_sign_it = 1;
	return query;
}

void
query_set_buffer_data(query_type *q, void *data, size_t data_capacity)
{
	buffer_create_from(q->packet, data, data_capacity);
}

void
query_reset(query_type *q, size_t maxlen, int is_tcp)
{
	/*
	 * As long as less than 4Kb (region block size) has been used,
	 * this call to free_all is free, the block is saved for re-use,
	 * so no malloc() or free() calls are done.
	 * at present use of the region is for:
	 *   o query qname dname_type (255 max).
	 *   o wildcard expansion domain_type (7*ptr+u32+2bytes)+(5*ptr nsec3)
	 *   o wildcard expansion for additional section domain_type.
	 *   o nsec3 hashed name(s) (3 dnames for a nonexist_proof,
	 *     one proof per wildcard and for nx domain).
	 */
	region_free_all(q->region);
	q->remote_addrlen = (socklen_t)sizeof(q->remote_addr);
	q->client_addrlen = (socklen_t)sizeof(q->client_addr);
	q->is_proxied = 0;
	q->maxlen = maxlen;
	q->reserved_space = 0;
	buffer_clear(q->packet);
	edns_init_record(&q->edns);
	tsig_init_record(&q->tsig, NULL, NULL);
	q->tsig_prepare_it = 1;
	q->tsig_update_it = 1;
	q->tsig_sign_it = 1;
	q->tcp = is_tcp;
	q->qname = NULL;
	q->qtype = 0;
	q->qclass = 0;
	q->zone = NULL;
	q->opcode = 0;
	q->cname_count = 0;
	q->delegation_domain = NULL;
	q->delegation_rrset = NULL;
	q->compressed_dname_count = 0;
	q->number_temporary_domains = 0;

	q->axfr_is_done = 0;
	q->axfr_zone = NULL;
	q->axfr_current_domain = NULL;
	q->axfr_current_rrset = NULL;
	q->axfr_current_rr = 0;

	q->ixfr_is_done = 0;
	q->ixfr_data = NULL;
	q->ixfr_count_newsoa = 0;
	q->ixfr_count_oldsoa = 0;
	q->ixfr_count_del = 0;
	q->ixfr_count_add = 0;

#ifdef RATELIMIT
	q->wildcard_domain = NULL;
#endif
}

/* get a temporary domain number (or 0=failure) */
static domain_type*
query_get_tempdomain(struct query *q)
{
	static domain_type d[EXTRA_DOMAIN_NUMBERS];
	if(q->number_temporary_domains >= EXTRA_DOMAIN_NUMBERS)
		return 0;
	q->number_temporary_domains ++;
	memset(&d[q->number_temporary_domains-1], 0, sizeof(domain_type));
	d[q->number_temporary_domains-1].number = q->compressed_dname_offsets_size +
		q->number_temporary_domains - 1;
	return &d[q->number_temporary_domains-1];
}

static void
query_addtxt(struct query  *q,
	     const uint8_t *dname,
	     uint16_t       klass,
	     uint32_t       ttl,
	     const char    *txt)
{
	size_t txt_length = strlen(txt);
	uint8_t len = (uint8_t) txt_length;

	assert(txt_length <= UCHAR_MAX);

	/* Add the dname */
	if (dname >= buffer_begin(q->packet)
	    && dname <= buffer_current(q->packet))
	{
		buffer_write_u16(q->packet,
				 0xc000 | (dname - buffer_begin(q->packet)));
	} else {
		buffer_write(q->packet, dname + 1, *dname);
	}

	buffer_write_u16(q->packet, TYPE_TXT);
	buffer_write_u16(q->packet, klass);
	buffer_write_u32(q->packet, ttl);
	buffer_write_u16(q->packet, len + 1);
	buffer_write_u8(q->packet, len);
	buffer_write(q->packet, txt, len);
}

/*
 * Parse the question section of a query.  The normalized query name
 * is stored in QUERY->name, the class in QUERY->klass, and the type
 * in QUERY->type.
 */
static int
process_query_section(query_type *query)
{
	uint8_t qnamebuf[MAXDOMAINLEN];

	buffer_set_position(query->packet, QHEADERSZ);
	/* Lets parse the query name and convert it to lower case.  */
	if(!packet_read_query_section(query->packet, qnamebuf,
		&query->qtype, &query->qclass))
		return 0;
	query->qname = dname_make(query->region, qnamebuf, 1);
	return 1;
}


/*
 * Process an optional EDNS OPT record.  Sets QUERY->EDNS to 0 if
 * there was no EDNS record, to -1 if there was an invalid or
 * unsupported EDNS record, and to 1 otherwise.  Updates QUERY->MAXLEN
 * if the EDNS record specifies a maximum supported response length.
 *
 * Return NSD_RC_FORMAT on failure, NSD_RC_OK on success.
 */
static nsd_rc_type
process_edns(nsd_type* nsd, struct query *q)
{
	if (q->edns.status == EDNS_ERROR) {
		/* The only error is VERSION not implemented */
		return NSD_RC_FORMAT;
	}

	if (q->edns.status == EDNS_OK) {
		/* Only care about UDP size larger than normal... */
		if (!q->tcp && q->edns.maxlen > UDP_MAX_MESSAGE_LEN) {
			size_t edns_size;
#if defined(INET6)
			if (q->client_addr.ss_family == AF_INET6) {
				edns_size = nsd->ipv6_edns_size;
			} else
#endif
			edns_size = nsd->ipv4_edns_size;

			if (q->edns.maxlen < edns_size) {
				q->maxlen = q->edns.maxlen;
			} else {
				q->maxlen = edns_size;
			}

#if defined(INET6) && !defined(IPV6_USE_MIN_MTU) && !defined(IPV6_MTU)
			/*
			 * Use IPv6 minimum MTU to avoid sending
			 * packets that are too large for some links.
			 * IPv6 will not automatically fragment in
			 * this case (unlike IPv4).
			 */
			if (q->client_addr.ss_family == AF_INET6
			    && q->maxlen > IPV6_MIN_MTU)
			{
				q->maxlen = IPV6_MIN_MTU;
			}
#endif
		}

		/* Strip the OPT resource record off... */
		buffer_set_position(q->packet, q->edns.position);
		buffer_set_limit(q->packet, q->edns.position);
		ARCOUNT_SET(q->packet, ARCOUNT(q->packet) - 1);
	}
	return NSD_RC_OK;
}

/*
 * Processes TSIG.
 * Sets error when tsig does not verify on the query.
 */
static nsd_rc_type
process_tsig(struct query* q)
{
	if(q->tsig.status == TSIG_ERROR)
		return NSD_RC_FORMAT;
	if(q->tsig.status == TSIG_OK) {
		if(!tsig_from_query(&q->tsig)) {
			char a[128];
			addr2str(&q->client_addr, a, sizeof(a));
			log_msg(LOG_ERR, "query: bad tsig (%s) for key %s from %s",
				tsig_error(q->tsig.error_code),
				dname_to_string(q->tsig.key_name, NULL), a);
			return NSD_RC_NOTAUTH;
		}
		buffer_set_limit(q->packet, q->tsig.position);
		ARCOUNT_SET(q->packet, ARCOUNT(q->packet) - 1);
		tsig_prepare(&q->tsig);
		tsig_update(&q->tsig, q->packet, buffer_limit(q->packet));
		if(!tsig_verify(&q->tsig)) {
			char a[128];
			addr2str(&q->client_addr, a, sizeof(a));
			log_msg(LOG_ERR, "query: bad tsig signature for key %s from %s",
				dname_to_string(q->tsig.key->name, NULL), a);
			return NSD_RC_NOTAUTH;
		}
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "query good tsig signature for %s",
			dname_to_string(q->tsig.key->name, NULL)));
	}
	return NSD_RC_OK;
}

/*
 * Check notify acl and forward to xfrd (or return an error).
 */
static query_state_type
answer_notify(struct nsd* nsd, struct query *query)
{
	int acl_num, acl_num_xfr;
	struct acl_options *why;
	nsd_rc_type rc;

	struct zone_options* zone_opt;
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "got notify %s processing acl",
		dname_to_string(query->qname, NULL)));

	zone_opt = zone_options_find(nsd->options, query->qname);
	if(!zone_opt)
		return query_error(query, NSD_RC_NXDOMAIN);

	if(!nsd->this_child) /* we are in debug mode or something */
		return query_error(query, NSD_RC_SERVFAIL);

	if(!tsig_find_rr(&query->tsig, query->packet)) {
		DEBUG(DEBUG_XFRD,2, (LOG_ERR, "bad tsig RR format"));
		return query_error(query, NSD_RC_FORMAT);
	}
	rc = process_tsig(query);
	if(rc != NSD_RC_OK)
		return query_error(query, rc);

	/* check if it passes acl */
	if(query->is_proxied && acl_check_incoming_block_proxy(
		zone_opt->pattern->allow_notify, query, &why) == -1) {
		/* the proxy address is blocked */
		if (verbosity >= 2) {
			char address[128], proxy[128];
			addr2str(&query->client_addr, address, sizeof(address));
			addr2str(&query->remote_addr, proxy, sizeof(proxy));
			VERBOSITY(2, (LOG_INFO, "notify for %s from %s via proxy %s refused because of proxy, %s%s%s",
				dname_to_string(query->qname, NULL),
				address, proxy,
				(why?why->ip_address_spec:""),
				(why&&why->ip_address_spec[0]?" ":""),
				(why ? ( why->nokey    ? "NOKEY"
				       : why->blocked  ? "BLOCKED"
				       : why->key_name )
				     : "no acl matches")));
		}
		return query_error(query, NSD_RC_REFUSE);
	}
	if((acl_num = acl_check_incoming(zone_opt->pattern->allow_notify, query,
		&why)) != -1)
	{
		int s = nsd->serve2xfrd_fd_send[nsd->this_child->child_num];
		uint16_t sz;
		uint32_t acl_send = htonl(acl_num);
		uint32_t acl_xfr;
		size_t pos;

		/* Find priority candidate for request XFR. -1 if no match */
		acl_num_xfr = acl_check_incoming(
			zone_opt->pattern->request_xfr, query, NULL);

		acl_xfr = htonl(acl_num_xfr);

		assert(why);
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "got notify %s passed acl %s %s",
			dname_to_string(query->qname, NULL),
			why->ip_address_spec,
			why->nokey?"NOKEY":
			(why->blocked?"BLOCKED":why->key_name)));
		if(buffer_limit(query->packet) > MAX_PACKET_SIZE)
			return query_error(query, NSD_RC_SERVFAIL);
		/* forward to xfrd for processing
		   Note. Blocking IPC I/O, but acl is OK. */
		sz = buffer_limit(query->packet)
		   + sizeof(acl_send) + sizeof(acl_xfr);
		sz = htons(sz);
		if(!write_socket(s, &sz, sizeof(sz)) ||
			!write_socket(s, buffer_begin(query->packet),
				buffer_limit(query->packet)) ||
			!write_socket(s, &acl_send, sizeof(acl_send)) ||
			!write_socket(s, &acl_xfr, sizeof(acl_xfr))) {
			log_msg(LOG_ERR, "error in IPC notify server2main, %s",
				strerror(errno));
			return query_error(query, NSD_RC_SERVFAIL);
		}
		if(verbosity >= 1) {
			uint32_t serial = 0;
			char address[128];
			addr2str(&query->client_addr, address, sizeof(address));
			if(packet_find_notify_serial(query->packet, &serial))
			  VERBOSITY(1, (LOG_INFO, "notify for %s from %s serial %u",
				dname_to_string(query->qname, NULL), address,
				(unsigned)serial));
			else
			  VERBOSITY(1, (LOG_INFO, "notify for %s from %s",
				dname_to_string(query->qname, NULL), address));
		}

		/* create notify reply - keep same query contents */
		QR_SET(query->packet);         /* This is an answer.  */
		AA_SET(query->packet);	   /* we are authoritative. */
		ANCOUNT_SET(query->packet, 0);
		NSCOUNT_SET(query->packet, 0);
		ARCOUNT_SET(query->packet, 0);
		RCODE_SET(query->packet, RCODE_OK); /* Error code.  */
		/* position is right after the query */
		pos = buffer_position(query->packet);
		buffer_clear(query->packet);
		buffer_set_position(query->packet, pos);
		/* tsig is added in add_additional later (if needed) */
		return QUERY_PROCESSED;
	}

	if (verbosity >= 2) {
		char address[128];
		addr2str(&query->client_addr, address, sizeof(address));
		VERBOSITY(2, (LOG_INFO, "notify for %s from %s refused, %s%s%s",
			dname_to_string(query->qname, NULL),
			address,
			(why?why->ip_address_spec:""),
			(why&&why->ip_address_spec[0]?" ":""),
			(why ? ( why->nokey    ? "NOKEY"
			       : why->blocked  ? "BLOCKED"
			       : why->key_name )
			     : "no acl matches")));
	}

	return query_error(query, NSD_RC_REFUSE);
}


/*
 * Answer a query in the CHAOS class.
 */
static query_state_type
answer_chaos(struct nsd *nsd, query_type *q)
{
	AA_CLR(q->packet);
	switch (q->qtype) {
	case TYPE_ANY:
	case TYPE_TXT:
		if ((q->qname->name_size == 11
		     && memcmp(dname_name(q->qname), "\002id\006server", 11) == 0) ||
		    (q->qname->name_size ==  15
		     && memcmp(dname_name(q->qname), "\010hostname\004bind", 15) == 0))
		{
			if(!nsd->options->hide_identity) {
				/* Add ID */
				query_addtxt(q,
				     buffer_begin(q->packet) + QHEADERSZ,
				     CLASS_CH,
				     0,
				     nsd->identity);
				ANCOUNT_SET(q->packet, ANCOUNT(q->packet) + 1);
			} else {
				RCODE_SET(q->packet, RCODE_REFUSE);
				/* RFC8914 - Extended DNS Errors
				 * 4.19. Extended DNS Error Code 18 - Prohibited */
				q->edns.ede = EDE_PROHIBITED;
			}
		} else if ((q->qname->name_size == 16
			    && memcmp(dname_name(q->qname), "\007version\006server", 16) == 0) ||
			   (q->qname->name_size == 14
			    && memcmp(dname_name(q->qname), "\007version\004bind", 14) == 0))
		{
			if(!nsd->options->hide_version) {
				/* Add version */
				query_addtxt(q,
				     buffer_begin(q->packet) + QHEADERSZ,
				     CLASS_CH,
				     0,
				     nsd->version);
				ANCOUNT_SET(q->packet, ANCOUNT(q->packet) + 1);
			} else {
				RCODE_SET(q->packet, RCODE_REFUSE);
				/* RFC8914 - Extended DNS Errors
				 * 4.19. Extended DNS Error Code 18 - Prohibited */
				q->edns.ede = EDE_PROHIBITED;
			}
		} else {
			RCODE_SET(q->packet, RCODE_REFUSE);
			/* RFC8914 - Extended DNS Errors
			 * 4.22. Extended DNS Error Code 21 - Not Supported */
			q->edns.ede = EDE_NOT_SUPPORTED;

		}
		break;
	default:
		RCODE_SET(q->packet, RCODE_REFUSE);
		/* RFC8914 - Extended DNS Errors
		 * 4.22. Extended DNS Error Code 21 - Not Supported */
		q->edns.ede = EDE_NOT_SUPPORTED;
		break;
	}

	return QUERY_PROCESSED;
}


/*
 * Find the covering NSEC for a non-existent domain name.  Normally
 * the NSEC will be located at CLOSEST_MATCH, except when it is an
 * empty non-terminal.  In this case the NSEC may be located at the
 * previous domain name (in canonical ordering).
 */
static domain_type *
find_covering_nsec(domain_type *closest_match,
		   zone_type   *zone,
		   rrset_type **nsec_rrset)
{
	assert(closest_match);
	assert(nsec_rrset);

	/* loop away temporary created domains. For real ones it is &RBTREE_NULL */
#ifdef USE_RADIX_TREE
	while (closest_match->rnode == NULL)
#else
	while (closest_match->node.parent == NULL)
#endif
		closest_match = closest_match->parent;
	while (closest_match) {
		*nsec_rrset = domain_find_rrset(closest_match, zone, TYPE_NSEC);
		if (*nsec_rrset) {
			return closest_match;
		}
		if (closest_match == zone->apex) {
			/* Don't look outside the current zone.  */
			return NULL;
		}
		closest_match = domain_previous(closest_match);
	}
	return NULL;
}


struct additional_rr_types
{
	uint16_t        rr_type;
	rr_section_type rr_section;
};

struct additional_rr_types default_additional_rr_types[] = {
	{ TYPE_A, ADDITIONAL_A_SECTION },
	{ TYPE_AAAA, ADDITIONAL_AAAA_SECTION },
	{ 0, (rr_section_type) 0 }
};

struct additional_rr_types swap_aaaa_additional_rr_types[] = {
	{ TYPE_AAAA, ADDITIONAL_A_SECTION },
	{ TYPE_A, ADDITIONAL_AAAA_SECTION },
	{ 0, (rr_section_type) 0 }
};

struct additional_rr_types rt_additional_rr_types[] = {
	{ TYPE_A, ADDITIONAL_A_SECTION },
	{ TYPE_AAAA, ADDITIONAL_AAAA_SECTION },
	{ TYPE_X25, ADDITIONAL_OTHER_SECTION },
	{ TYPE_ISDN, ADDITIONAL_OTHER_SECTION },
	{ 0, (rr_section_type) 0 }
};

static void
add_additional_rrsets(struct query *query, answer_type *answer,
		      rrset_type *master_rrset, size_t rdata_index,
		      int allow_glue, struct additional_rr_types types[])
{
	size_t i;

	assert(query);
	assert(answer);
	assert(master_rrset);
	assert(rdata_atom_is_domain(rrset_rrtype(master_rrset), rdata_index));

	for (i = 0; i < master_rrset->rr_count; ++i) {
		int j;
		domain_type *additional = rdata_atom_domain(master_rrset->rrs[i].rdatas[rdata_index]);
		domain_type *match = additional;

		assert(additional);

		if (!allow_glue && domain_is_glue(match, query->zone))
			continue;

		/*
		 * Check to see if we need to generate the dependent
		 * based on a wildcard domain.
		 */
		while (!match->is_existing) {
			match = match->parent;
		}
		if (additional != match && domain_wildcard_child(match)) {
			domain_type *wildcard_child = domain_wildcard_child(match);
			domain_type *temp = (domain_type *) region_alloc(
				query->region, sizeof(domain_type));
#ifdef USE_RADIX_TREE
			temp->rnode = NULL;
			temp->dname = additional->dname;
#else
			memcpy(&temp->node, &additional->node, sizeof(rbnode_type));
			temp->node.parent = NULL;
#endif
			temp->number = additional->number;
			temp->parent = match;
			temp->wildcard_child_closest_match = temp;
			temp->rrsets = wildcard_child->rrsets;
			temp->is_existing = wildcard_child->is_existing;
			additional = temp;
		}

		for (j = 0; types[j].rr_type != 0; ++j) {
			rrset_type *rrset = domain_find_rrset(
				additional, query->zone, types[j].rr_type);
			if (rrset) {
				answer_add_rrset(answer, types[j].rr_section,
						 additional, rrset);
			}
		}
	}
}

static int
answer_needs_ns(struct query* query)
{
	assert(query);
	/* Currently, only troublesome for DNSKEY and DS,
         * cuz their RRSETs are quite large. */
	return (query->qtype != TYPE_DNSKEY && query->qtype != TYPE_DS
		&& query->qtype != TYPE_ANY);
}

static int
add_rrset(struct query   *query,
	  answer_type    *answer,
	  rr_section_type section,
	  domain_type    *owner,
	  rrset_type     *rrset)
{
	int result;

	assert(query);
	assert(answer);
	assert(owner);
	assert(rrset);
	assert(rrset_rrclass(rrset) == CLASS_IN);

	result = answer_add_rrset(answer, section, owner, rrset);
	if(minimal_responses && section != AUTHORITY_SECTION &&
		query->qtype != TYPE_NS)
		return result;
	switch (rrset_rrtype(rrset)) {
	case TYPE_NS:
#if defined(INET6)
		/* if query over IPv6, swap A and AAAA; put AAAA first */
		add_additional_rrsets(query, answer, rrset, 0, 1,
			(query->client_addr.ss_family == AF_INET6)?
			swap_aaaa_additional_rr_types:
			default_additional_rr_types);
#else
		add_additional_rrsets(query, answer, rrset, 0, 1,
				      default_additional_rr_types);
#endif
		break;
	case TYPE_MB:
		add_additional_rrsets(query, answer, rrset, 0, 0,
				      default_additional_rr_types);
		break;
	case TYPE_MX:
	case TYPE_KX:
		add_additional_rrsets(query, answer, rrset, 1, 0,
				      default_additional_rr_types);
		break;
	case TYPE_RT:
		add_additional_rrsets(query, answer, rrset, 1, 0,
				      rt_additional_rr_types);
		break;
	case TYPE_SRV:
		add_additional_rrsets(query, answer, rrset, 3, 0,
				      default_additional_rr_types);
		break;
	default:
		break;
	}

	return result;
}


/* returns 0 on error, or the domain number for to_name.
   from_name is changes to to_name by the DNAME rr.
   DNAME rr is from src to dest.
   closest encloser encloses the to_name. */
static size_t
query_synthesize_cname(struct query* q, struct answer* answer, const dname_type* from_name,
	const dname_type* to_name, domain_type* src, domain_type* to_closest_encloser,
	domain_type** to_closest_match, uint32_t ttl)
{
	/* add temporary domains for from_name and to_name and all
	   their (not allocated yet) parents */
	/* any domains below src are not_existing (because of DNAME at src) */
	int i;
	size_t j;
	domain_type* cname_domain;
	domain_type* cname_dest;
	rrset_type* rrset;

	domain_type* lastparent = src;
	assert(q && answer && from_name && to_name && src && to_closest_encloser);
	assert(to_closest_match);

	/* check for loop by duplicate CNAME rrset synthesized */
	for(j=0; j<answer->rrset_count; ++j) {
		if(answer->section[j] == ANSWER_SECTION &&
			answer->rrsets[j]->rr_count == 1 &&
			answer->rrsets[j]->rrs[0].type == TYPE_CNAME &&
			dname_compare(domain_dname(answer->rrsets[j]->rrs[0].owner), from_name) == 0 &&
			answer->rrsets[j]->rrs[0].rdata_count == 1 &&
			dname_compare(domain_dname(answer->rrsets[j]->rrs[0].rdatas->domain), to_name) == 0) {
			DEBUG(DEBUG_QUERY,2, (LOG_INFO, "loop for synthesized CNAME rrset for query %s", dname_to_string(q->qname, NULL)));
			return 0;
		}
	}

	/* allocate source part */
	for(i=0; i < from_name->label_count - domain_dname(src)->label_count; i++)
	{
		domain_type* newdom = query_get_tempdomain(q);
		if(!newdom)
			return 0;
		newdom->is_existing = 1;
		newdom->parent = lastparent;
#ifdef USE_RADIX_TREE
		newdom->dname
#else
		newdom->node.key
#endif
			= dname_partial_copy(q->region,
			from_name, domain_dname(src)->label_count + i + 1);
		if(dname_compare(domain_dname(newdom), q->qname) == 0) {
			/* 0 good for query name, otherwise new number */
			newdom->number = 0;
		}
		DEBUG(DEBUG_QUERY,2, (LOG_INFO, "created temp domain src %d. %s nr %d", i,
			domain_to_string(newdom), (int)newdom->number));
		lastparent = newdom;
	}
	cname_domain = lastparent;

	/* allocate dest part */
	lastparent = to_closest_encloser;
	for(i=0; i < to_name->label_count - domain_dname(to_closest_encloser)->label_count;
		i++)
	{
		domain_type* newdom = query_get_tempdomain(q);
		if(!newdom)
			return 0;
		newdom->is_existing = 0;
		newdom->parent = lastparent;
#ifdef USE_RADIX_TREE
		newdom->dname
#else
		newdom->node.key
#endif
			= dname_partial_copy(q->region,
			to_name, domain_dname(to_closest_encloser)->label_count + i + 1);
		DEBUG(DEBUG_QUERY,2, (LOG_INFO, "created temp domain dest %d. %s nr %d", i,
			domain_to_string(newdom), (int)newdom->number));
		lastparent = newdom;
	}
	cname_dest = lastparent;
	*to_closest_match = cname_dest;

	/* allocate the CNAME RR */
	rrset = (rrset_type*) region_alloc(q->region, sizeof(rrset_type));
	memset(rrset, 0, sizeof(rrset_type));
	rrset->zone = q->zone;
	rrset->rr_count = 1;
	rrset->rrs = (rr_type*) region_alloc(q->region, sizeof(rr_type));
	memset(rrset->rrs, 0, sizeof(rr_type));
	rrset->rrs->owner = cname_domain;
	rrset->rrs->ttl = ttl;
	rrset->rrs->type = TYPE_CNAME;
	rrset->rrs->klass = CLASS_IN;
	rrset->rrs->rdata_count = 1;
	rrset->rrs->rdatas = (rdata_atom_type*)region_alloc(q->region,
		sizeof(rdata_atom_type));
	rrset->rrs->rdatas->domain = cname_dest;

	if(!add_rrset(q, answer, ANSWER_SECTION, cname_domain, rrset)) {
		DEBUG(DEBUG_QUERY,2, (LOG_INFO, "could not add synthesized CNAME rrset to packet for query %s", dname_to_string(q->qname, NULL)));
		/* failure to add CNAME; likely is a loop, the same twice */
		return 0;
	}

	return cname_dest->number;
}

/*
 * Answer delegation information.
 *
 * DNSSEC: Include the DS RRset if present.  Otherwise include an NSEC
 * record proving the DS RRset does not exist.
 */
static void
answer_delegation(query_type *query, answer_type *answer)
{
	assert(answer);
	assert(query->delegation_domain);
	assert(query->delegation_rrset);

	if (query->cname_count == 0) {
		AA_CLR(query->packet);
	} else {
		AA_SET(query->packet);
	}

	add_rrset(query,
		  answer,
		  AUTHORITY_SECTION,
		  query->delegation_domain,
		  query->delegation_rrset);
	if (query->edns.dnssec_ok && zone_is_secure(query->zone)) {
		rrset_type *rrset;
		if ((rrset = domain_find_rrset(query->delegation_domain, query->zone, TYPE_DS))) {
			add_rrset(query, answer, AUTHORITY_SECTION,
				  query->delegation_domain, rrset);
#ifdef NSEC3
		} else if (query->zone->nsec3_param) {
			nsec3_answer_delegation(query, answer);
#endif
		} else if ((rrset = domain_find_rrset(query->delegation_domain, query->zone, TYPE_NSEC))) {
			add_rrset(query, answer, AUTHORITY_SECTION,
				  query->delegation_domain, rrset);
		}
	}
}


/*
 * Answer SOA information.
 */
static void
answer_soa(struct query *query, answer_type *answer)
{
	if (query->qclass != CLASS_ANY) {
		add_rrset(query, answer,
			  AUTHORITY_SECTION,
			  query->zone->apex,
			  query->zone->soa_nx_rrset);
	}
}


/*
 * Answer that the domain name exists but there is no RRset with the
 * requested type.
 *
 * DNSSEC: Include the correct NSEC record proving that the type does
 * not exist.  In the wildcard no data (3.1.3.4) case the wildcard IS
 * NOT expanded, so the ORIGINAL parameter must point to the original
 * wildcard entry, not to the generated entry.
 */
static void
answer_nodata(struct query *query, answer_type *answer, domain_type *original)
{
	answer_soa(query, answer);

#ifdef NSEC3
	if (query->edns.dnssec_ok && query->zone->nsec3_param) {
		nsec3_answer_nodata(query, answer, original);
	} else
#endif
	if (query->edns.dnssec_ok && zone_is_secure(query->zone)) {
		domain_type *nsec_domain;
		rrset_type *nsec_rrset;

		nsec_domain = find_covering_nsec(original, query->zone, &nsec_rrset);
		if (nsec_domain) {
			add_rrset(query, answer, AUTHORITY_SECTION, nsec_domain, nsec_rrset);
		}
	}
}

static void
answer_nxdomain(query_type *query, answer_type *answer)
{
	RCODE_SET(query->packet, RCODE_NXDOMAIN);
	answer_soa(query, answer);
}


/*
 * Answer domain information (or SOA if we do not have an RRset for
 * the type specified by the query).
 */
static void
answer_domain(struct nsd* nsd, struct query *q, answer_type *answer,
	      domain_type *domain, domain_type *original)
{
	rrset_type *rrset;

	if (q->qtype == TYPE_ANY) {
		rrset_type *preferred_rrset = NULL;
		rrset_type *normal_rrset = NULL;
		rrset_type *non_preferred_rrset = NULL;

		/*
		 * Minimize response size for ANY, with one RRset
		 * according to RFC 8482(4.1).
		 * Prefers popular and not large rtypes (A,AAAA,...)
		 * lowering large ones (DNSKEY,RRSIG,...).
		 */
		for (rrset = domain_find_any_rrset(domain, q->zone); rrset; rrset = rrset->next) {
			if (rrset->zone == q->zone
#ifdef NSEC3
				&& rrset_rrtype(rrset) != TYPE_NSEC3
#endif
			    /*
			     * Don't include the RRSIG RRset when
			     * DNSSEC is used, because it is added
			     * automatically on an per-RRset basis.
			     */
			    && !(q->edns.dnssec_ok
				 && zone_is_secure(q->zone)
				 && rrset_rrtype(rrset) == TYPE_RRSIG))
			{
				switch(rrset_rrtype(rrset)) {
					case TYPE_A:
					case TYPE_AAAA:
					case TYPE_SOA:
					case TYPE_MX:
					case TYPE_PTR:
						preferred_rrset = rrset;
						break;
					case TYPE_DNSKEY:
					case TYPE_RRSIG:
					case TYPE_NSEC:
						non_preferred_rrset = rrset;
						break;
					default:
						normal_rrset = rrset;
				}
				if (preferred_rrset) break;
			}
		}
		if (preferred_rrset) {
			add_rrset(q, answer, ANSWER_SECTION, domain, preferred_rrset);
		} else if (normal_rrset) {
			add_rrset(q, answer, ANSWER_SECTION, domain, normal_rrset);
		} else if (non_preferred_rrset) {
			add_rrset(q, answer, ANSWER_SECTION, domain, non_preferred_rrset);
		} else {
			answer_nodata(q, answer, original);
			return;
		}
#ifdef NSEC3
	} else if (q->qtype == TYPE_NSEC3) {
		answer_nodata(q, answer, original);
		return;
#endif
	} else if ((rrset = domain_find_rrset(domain, q->zone, q->qtype))) {
		add_rrset(q, answer, ANSWER_SECTION, domain, rrset);
	} else if ((rrset = domain_find_rrset(domain, q->zone, TYPE_CNAME))) {
		int added;

		/*
		 * If the CNAME is not added it is already in the
		 * answer, so we have a CNAME loop.  Don't follow the
		 * CNAME target in this case.
		 */
		added = add_rrset(q, answer, ANSWER_SECTION, domain, rrset);
		assert(rrset->rr_count > 0);
		if (added) {
			/* only process first CNAME record */
			domain_type *closest_match = rdata_atom_domain(rrset->rrs[0].rdatas[0]);
			domain_type *closest_encloser = closest_match;
			zone_type* origzone = q->zone;
			++q->cname_count;

			answer_lookup_zone(nsd, q, answer, closest_match->number,
					     closest_match == closest_encloser,
					     closest_match, closest_encloser,
					     domain_dname(closest_match));
			q->zone = origzone;
		}
		return;
	} else {
		answer_nodata(q, answer, original);
		return;
	}

	if (q->qclass != CLASS_ANY && q->zone->ns_rrset && answer_needs_ns(q)
		&& !minimal_responses) {
		add_rrset(q, answer, OPTIONAL_AUTHORITY_SECTION, q->zone->apex,
			  q->zone->ns_rrset);
	}
}


/*
 * Answer with authoritative data.  If a wildcard is matched the owner
 * name will be expanded to the domain name specified by
 * DOMAIN_NUMBER.  DOMAIN_NUMBER 0 (zero) is reserved for the original
 * query name.
 *
 * DNSSEC: Include the necessary NSEC records in case the request
 * domain name does not exist and/or a wildcard match does not exist.
 */
static void
answer_authoritative(struct nsd   *nsd,
		     struct query *q,
		     answer_type  *answer,
		     size_t        domain_number,
		     int           exact,
		     domain_type  *closest_match,
		     domain_type  *closest_encloser,
		     const dname_type *qname)
{
	domain_type *match;
	domain_type *original = closest_match;
	domain_type *dname_ce;
	domain_type *wildcard_child;
	rrset_type *rrset;

#ifdef NSEC3
	if(exact && domain_has_only_NSEC3(closest_match, q->zone)) {
		exact = 0; /* pretend it does not exist */
		if(closest_encloser->parent)
			closest_encloser = closest_encloser->parent;
	}
#endif /* NSEC3 */
	if((dname_ce = find_dname_above(closest_encloser, q->zone)) != NULL) {
		/* occlude the found data, the DNAME is closest_encloser */
		closest_encloser = dname_ce;
		exact = 0;
	}

	if (exact) {
		match = closest_match;
	} else if ((rrset=domain_find_rrset(closest_encloser, q->zone, TYPE_DNAME))) {
		/* process DNAME */
		const dname_type* name = qname;
		domain_type* src = closest_encloser;
		domain_type *dest = rdata_atom_domain(rrset->rrs[0].rdatas[0]);
		const dname_type* newname;
		size_t newnum = 0;
		zone_type* origzone = q->zone;
		assert(rrset->rr_count > 0);
		if(domain_number != 0) /* we followed CNAMEs or DNAMEs */
			name = domain_dname(closest_match);
		DEBUG(DEBUG_QUERY,2, (LOG_INFO, "expanding DNAME for q=%s", dname_to_string(name, NULL)));
		DEBUG(DEBUG_QUERY,2, (LOG_INFO, "->src is %s",
			domain_to_string(closest_encloser)));
		DEBUG(DEBUG_QUERY,2, (LOG_INFO, "->dest is %s",
			domain_to_string(dest)));
		if(!add_rrset(q, answer, ANSWER_SECTION, closest_encloser, rrset)) {
			/* stop if DNAME loops, when added second time */
			if(dname_is_subdomain(domain_dname(dest), domain_dname(src))) {
				return;
			}
		}
		newname = dname_replace(q->region, name,
			domain_dname(src), domain_dname(dest));
		++q->cname_count;
		if(!newname) { /* newname too long */
			RCODE_SET(q->packet, RCODE_YXDOMAIN);
			/* RFC 8914 - Extended DNS Errors
			 * 4.21. Extended DNS Error Code 0 - Other */
			ASSIGN_EDE_CODE_AND_STRING_LITERAL(q->edns.ede,
				EDE_OTHER, "DNAME expansion became too large");
			return;
		}
		DEBUG(DEBUG_QUERY,2, (LOG_INFO, "->result is %s", dname_to_string(newname, NULL)));
		/* follow the DNAME */
		(void)namedb_lookup(nsd->db, newname, &closest_match, &closest_encloser);
		/* synthesize CNAME record */
		newnum = query_synthesize_cname(q, answer, name, newname,
			src, closest_encloser, &closest_match, rrset->rrs[0].ttl);
		if(!newnum) {
			/* could not synthesize the CNAME. */
			/* return previous CNAMEs to make resolver recurse for us */
			return;
		}
		if(q->qtype == TYPE_CNAME) {
			/* The synthesized CNAME is the answer to
			 * that query, same as BIND does for query
			 * of type CNAME */
			return;
		}

		answer_lookup_zone(nsd, q, answer, newnum,
			closest_match == closest_encloser,
			closest_match, closest_encloser, newname);
		q->zone = origzone;
		return;
	} else if ((wildcard_child=domain_wildcard_child(closest_encloser))!=NULL &&
		wildcard_child->is_existing) {
		/* Generate the domain from the wildcard.  */
#ifdef RATELIMIT
		q->wildcard_domain = wildcard_child;
#endif

		match = (domain_type *) region_alloc(q->region,
						     sizeof(domain_type));
#ifdef USE_RADIX_TREE
		match->rnode = NULL;
		match->dname = wildcard_child->dname;
#else
		memcpy(&match->node, &wildcard_child->node, sizeof(rbnode_type));
		match->node.parent = NULL;
#endif
		match->parent = closest_encloser;
		match->wildcard_child_closest_match = match;
		match->number = domain_number;
		match->rrsets = wildcard_child->rrsets;
		match->is_existing = wildcard_child->is_existing;
#ifdef NSEC3
		match->nsec3 = wildcard_child->nsec3;
		/* copy over these entries:
		match->nsec3_is_exact = wildcard_child->nsec3_is_exact;
		match->nsec3_cover = wildcard_child->nsec3_cover;
		match->nsec3_wcard_child_cover = wildcard_child->nsec3_wcard_child_cover;
		match->nsec3_ds_parent_is_exact = wildcard_child->nsec3_ds_parent_is_exact;
		match->nsec3_ds_parent_cover = wildcard_child->nsec3_ds_parent_cover;
		*/

		if (q->edns.dnssec_ok && q->zone->nsec3_param) {
			/* Only add nsec3 wildcard data when do bit is set */
			nsec3_answer_wildcard(q, answer, wildcard_child, qname);
		}
#endif

		/*
		 * Remember the original domain in case a Wildcard No
		 * Data (3.1.3.4) response needs to be generated.  In
		 * this particular case the wildcard IS NOT
		 * expanded.
		 */
		original = wildcard_child;
	} else {
		match = NULL;
	}

	/* Authoritative zone.  */
#ifdef NSEC3
	if (q->edns.dnssec_ok && q->zone->nsec3_param) {
		nsec3_answer_authoritative(&match, q, answer,
			closest_encloser, qname);
	} else
#endif
	if (q->edns.dnssec_ok && zone_is_secure(q->zone)) {
		if (match != closest_encloser) {
			domain_type *nsec_domain;
			rrset_type *nsec_rrset;

			/*
			 * No match found or generated from wildcard,
			 * include NSEC record.
			 */
			nsec_domain = find_covering_nsec(closest_match, q->zone, &nsec_rrset);
			if (nsec_domain) {
				add_rrset(q, answer, AUTHORITY_SECTION, nsec_domain, nsec_rrset);
			}
		}
		if (!match) {
			domain_type *nsec_domain;
			rrset_type *nsec_rrset;

			/*
			 * No match and no wildcard.  Include NSEC
			 * proving there is no wildcard.
			 */
			if(closest_encloser && (nsec_domain =
				find_covering_nsec(closest_encloser->
					wildcard_child_closest_match, q->zone,
					&nsec_rrset)) != NULL) {
				add_rrset(q, answer, AUTHORITY_SECTION, nsec_domain, nsec_rrset);
			}
		}
	}

#ifdef NSEC3
	if (RCODE(q->packet)!=RCODE_OK) {
		return; /* nsec3 collision failure */
	}
#endif
	if (match) {
		answer_domain(nsd, q, answer, match, original);
	} else {
		answer_nxdomain(q, answer);
	}
}

/*
 * qname may be different after CNAMEs have been followed from query->qname.
 */
static void
answer_lookup_zone(struct nsd *nsd, struct query *q, answer_type *answer,
	size_t domain_number, int exact, domain_type *closest_match,
	domain_type *closest_encloser, const dname_type *qname)
{
	zone_type* origzone = q->zone;
	q->zone = domain_find_zone(nsd->db, closest_encloser);
	if (!q->zone) {
		/* no zone for this */
		if(q->cname_count == 0) {
			RCODE_SET(q->packet, RCODE_REFUSE);
			/* RFC 8914 - Extended DNS Errors
			 * 4.21. Extended DNS Error Code 20 - Not Authoritative */
			q->edns.ede = EDE_NOT_AUTHORITATIVE;
		}
		return;
	}
	assert(closest_encloser); /* otherwise, no q->zone would be found */
	if(q->zone->opts && q->zone->opts->pattern
	&& q->zone->opts->pattern->allow_query) {
		struct acl_options *why = NULL;

		/* check if it passes acl */
		if(q->is_proxied && acl_check_incoming_block_proxy(
			q->zone->opts->pattern->allow_query, q, &why) == -1) {
			/* the proxy address is blocked */
			if (verbosity >= 2) {
				char address[128], proxy[128];
				addr2str(&q->client_addr, address, sizeof(address));
				addr2str(&q->remote_addr, proxy, sizeof(proxy));
				VERBOSITY(2, (LOG_INFO, "query %s from %s via proxy %s refused because of proxy, %s%s%s",
					dname_to_string(q->qname, NULL),
					address, proxy,
					(why?why->ip_address_spec:""),
					(why&&why->ip_address_spec[0]?" ":""),
					(why ? ( why->nokey    ? "NOKEY"
					      : why->blocked  ? "BLOCKED"
					      : why->key_name )
					    : "no acl matches")));
			}
			/* no zone for this */
			if(q->cname_count == 0) {
				RCODE_SET(q->packet, RCODE_REFUSE);
				/* RFC8914 - Extended DNS Errors
				 * 4.19. Extended DNS Error Code 18 - Prohibited */
				q->edns.ede = EDE_PROHIBITED;
			}
			return;
		}
		if(acl_check_incoming(
		   q->zone->opts->pattern->allow_query, q, &why) != -1) {
			assert(why);
			DEBUG(DEBUG_QUERY,1, (LOG_INFO, "query %s passed acl %s %s",
				dname_to_string(q->qname, NULL),
				why->ip_address_spec,
				why->nokey?"NOKEY":
				(why->blocked?"BLOCKED":why->key_name)));
		} else if(q->qtype == TYPE_SOA
		       &&  0 == dname_compare(q->qname,
				(const dname_type*)q->zone->opts->node.key)
		       && -1 != acl_check_incoming(
				q->zone->opts->pattern->provide_xfr, q,&why)) {
			assert(why);
			DEBUG(DEBUG_QUERY,1, (LOG_INFO, "SOA apex query %s "
				"passed request-xfr acl %s %s",
				dname_to_string(q->qname, NULL),
				why->ip_address_spec,
				why->nokey?"NOKEY":
				(why->blocked?"BLOCKED":why->key_name)));
		} else {
			if (q->cname_count == 0 && verbosity >= 2) {
				char address[128];
				addr2str(&q->client_addr, address, sizeof(address));
				VERBOSITY(2, (LOG_INFO, "query %s from %s refused, %s%s%s",
					dname_to_string(q->qname, NULL),
					address,
					why ? ( why->nokey    ? "NOKEY"
					      : why->blocked  ? "BLOCKED"
					      : why->key_name )
					    : "no acl matches",
					(why&&why->ip_address_spec[0]?" ":""),
					why?why->ip_address_spec:""));
			}
			/* no zone for this */
			if(q->cname_count == 0) {
				RCODE_SET(q->packet, RCODE_REFUSE);
				/* RFC8914 - Extended DNS Errors
				 * 4.19. Extended DNS Error Code 18 - Prohibited */
				q->edns.ede = EDE_PROHIBITED;
			}
			return;
		}
	}
	if(!q->zone->apex || !q->zone->soa_rrset) {
		/* zone is configured but not loaded */
		if(q->cname_count == 0) {
			RCODE_SET(q->packet, RCODE_SERVFAIL);
			/* RFC 8914 - Extended DNS Errors
			 * 4.15. Extended DNS Error Code 14 - Not Ready */
			q->edns.ede = EDE_NOT_READY;
			ASSIGN_EDE_CODE_AND_STRING_LITERAL(q->edns.ede,
			    EDE_NOT_READY, "Zone is configured but not loaded");
		}
		return;
	}

	/*
	 * If confine-to-zone is set to yes do not return additional
	 * information for a zone with a different apex from the query zone.
	*/
	if (nsd->options->confine_to_zone &&
	   (origzone != NULL && dname_compare(domain_dname(origzone->apex), domain_dname(q->zone->apex)) != 0)) {
		return;
	}

	/* now move up the closest encloser until it exists, previous
	 * (possibly empty) closest encloser was useful to finding the zone
	 * (for empty zones too), but now we want actual data nodes */
	if (closest_encloser && !closest_encloser->is_existing) {
		exact = 0;
		while (closest_encloser != NULL && !closest_encloser->is_existing)
			closest_encloser = closest_encloser->parent;
	}

	/*
	 * See RFC 4035 (DNSSEC protocol) section 3.1.4.1 Responding
	 * to Queries for DS RRs.
	 */
	if (exact && q->qtype == TYPE_DS && closest_encloser == q->zone->apex) {
		/*
		 * Type DS query at a zone cut, use the responsible
		 * parent zone to generate the answer if we are
		 * authoritative for the parent zone.
		 */
		zone_type *zone = domain_find_parent_zone(nsd->db, q->zone);
		if (zone) {
			q->zone = zone;
			if(!q->zone->apex || !q->zone->soa_rrset) {
				/* zone is configured but not loaded */
				if(q->cname_count == 0) {
					RCODE_SET(q->packet, RCODE_SERVFAIL);
					/* RFC 8914 - Extended DNS Errors
					 * 4.15. Extended DNS Error Code 14 - Not Ready */
					ASSIGN_EDE_CODE_AND_STRING_LITERAL(
					   q->edns.ede, EDE_NOT_READY,
					   "Zone is configured but not loaded");
				}
				return;
			}
		}
	}

	/* see if the zone has expired (for secondary zones) */
	if(q->zone && q->zone->opts && q->zone->opts->pattern &&
		q->zone->opts->pattern->request_xfr != 0 && !q->zone->is_ok) {
		if(q->cname_count == 0) {
			RCODE_SET(q->packet, RCODE_SERVFAIL);
			/* RFC 8914 - Extended DNS Errors
			 * 4.25. Extended DNS Error Code 24 - Invalid Data */
			ASSIGN_EDE_CODE_AND_STRING_LITERAL(q->edns.ede,
				EDE_INVALID_DATA, "Zone has expired");
		}
		return;
	}

	if (exact && q->qtype == TYPE_DS && closest_encloser == q->zone->apex) {
		/*
		 * Type DS query at the zone apex (and the server is
		 * not authoritative for the parent zone).
		 */
		if (q->qclass == CLASS_ANY) {
			AA_CLR(q->packet);
		} else {
			AA_SET(q->packet);
		}
		answer_nodata(q, answer, closest_encloser);
	} else {
		q->delegation_domain = domain_find_ns_rrsets(
			closest_encloser, q->zone, &q->delegation_rrset);
		if(q->delegation_domain && find_dname_above(q->delegation_domain, q->zone)) {
			q->delegation_domain = NULL; /* use higher DNAME */
		}

		if (!q->delegation_domain
		    || !q->delegation_rrset
		    || (exact && q->qtype == TYPE_DS && closest_encloser == q->delegation_domain))
		{
			if (q->qclass == CLASS_ANY) {
				AA_CLR(q->packet);
			} else {
				AA_SET(q->packet);
			}
			answer_authoritative(nsd, q, answer, domain_number, exact,
					     closest_match, closest_encloser, qname);
		}
		else {
			answer_delegation(q, answer);
		}
	}
}

static void
answer_query(struct nsd *nsd, struct query *q)
{
	domain_type *closest_match;
	domain_type *closest_encloser;
	int exact;
	uint16_t offset;
	answer_type answer;

	answer_init(&answer);

	exact = namedb_lookup(nsd->db, q->qname, &closest_match, &closest_encloser);

	answer_lookup_zone(nsd, q, &answer, 0, exact, closest_match,
		closest_encloser, q->qname);
	ZTATUP2(nsd, q->zone, opcode, q->opcode);
	ZTATUP2(nsd, q->zone, qtype, q->qtype);
	ZTATUP2(nsd, q->zone, qclass, q->qclass);

	offset = dname_label_offsets(q->qname)[domain_dname(closest_encloser)->label_count - 1] + QHEADERSZ;
	query_add_compression_domain(q, closest_encloser, offset);
	encode_answer(q, &answer);
	query_clear_compression_tables(q);
}

void
query_prepare_response(query_type *q)
{
	uint16_t flags;

	/*
	 * Preserve the data up-to the current packet's limit.
	 */
	buffer_set_position(q->packet, buffer_limit(q->packet));
	buffer_set_limit(q->packet, buffer_capacity(q->packet));

	/*
	 * Reserve space for the EDNS records if required.
	 */
	q->reserved_space = edns_reserved_space(&q->edns);
	q->reserved_space += tsig_reserved_space(&q->tsig);

	/* Update the flags.  */
	flags = FLAGS(q->packet);
	flags &= 0x0100U;	/* Preserve the RD flag.  */
				/* CD flag must be cleared for auth answers */
	flags |= 0x8000U;	/* Set the QR flag.  */
	FLAGS_SET(q->packet, flags);
}

/*
 * Processes the query.
 *
 */
query_state_type
query_process(query_type *q, nsd_type *nsd, uint32_t *now_p)
{
	/* The query... */
	nsd_rc_type rc;
	query_state_type query_state;
	uint16_t arcount;

	/* Sanity checks */
	if (buffer_limit(q->packet) < QHEADERSZ) {
		/* packet too small to contain DNS header.
		Now packet investigation macros will work without problems. */
		return QUERY_DISCARDED;
	}
	if (QR(q->packet)) {
		/* Not a query? Drop it on the floor. */
		return QUERY_DISCARDED;
	}

	/* check opcode early on, because new opcodes may have different
	 * specification of the meaning of the rest of the packet */
	q->opcode = OPCODE(q->packet);
	if(q->opcode != OPCODE_QUERY && q->opcode != OPCODE_NOTIFY) {
		if(query_ratelimit_err(nsd))
			return QUERY_DISCARDED;
		if(nsd->options->drop_updates && q->opcode == OPCODE_UPDATE)
			return QUERY_DISCARDED;
		return query_error(q, NSD_RC_IMPL);
	}

	if (RCODE(q->packet) != RCODE_OK || !process_query_section(q)) {
		return query_formerr(q, nsd);
	}

	/* Update statistics.  */
	STATUP2(nsd, opcode, q->opcode);
	STATUP2(nsd, qtype, q->qtype);
	STATUP2(nsd, qclass, q->qclass);

	if (q->opcode != OPCODE_QUERY) {
		if (q->opcode == OPCODE_NOTIFY) {
			return answer_notify(nsd, q);
		} else {
			if(query_ratelimit_err(nsd))
				return QUERY_DISCARDED;
			return query_error(q, NSD_RC_IMPL);
		}
	}

	/* Dont bother to answer more than one question at once... */
	if (QDCOUNT(q->packet) != 1) {
		if(QDCOUNT(q->packet) == 0 && ANCOUNT(q->packet) == 0 &&
			NSCOUNT(q->packet) == 0 && ARCOUNT(q->packet) == 1 &&
			buffer_limit(q->packet) >= QHEADERSZ+OPT_LEN+
			OPT_RDATA) {
			/* add edns section to answer */
			buffer_set_position(q->packet, QHEADERSZ);
			if (edns_parse_record(&q->edns, q->packet, q, nsd)) {
				if(process_edns(nsd, q) == NSD_RC_OK) {
					int opcode = OPCODE(q->packet);
					(void)query_error(q, NSD_RC_FORMAT);
					query_add_optional(q, nsd, now_p);
					FLAGS_SET(q->packet, FLAGS(q->packet) & 0x0100U);
						/* Preserve the RD flag. Clear the rest. */
					OPCODE_SET(q->packet, opcode);
					QR_SET(q->packet);
					return QUERY_PROCESSED;
				}
			}
		}
		FLAGS_SET(q->packet, 0);
		return query_formerr(q, nsd);
	}
	/* Ignore settings of flags */

	/* Dont allow any records in the answer or authority section...
	   except for IXFR queries. */
	if (ANCOUNT(q->packet) != 0 ||
		(q->qtype!=TYPE_IXFR && NSCOUNT(q->packet) != 0)) {
		return query_formerr(q, nsd);
	}
	if(q->qtype==TYPE_IXFR && NSCOUNT(q->packet) > 0) {
		unsigned int i; /* skip ixfr soa information data here */
		unsigned int nscount = (unsigned)NSCOUNT(q->packet);
		/* define a bound on the number of extraneous records allowed,
		 * we expect 1, a SOA serial record, and no more.
		 * perhaps RRSIGs (but not needed), otherwise we do not
		 * understand what this means.  We do not want too many
		 * because the high iteration counts slow down. */
		if(nscount > 64) return query_formerr(q, nsd);
		for(i=0; i< nscount; i++)
			if(!packet_skip_rr(q->packet, 0))
				return query_formerr(q, nsd);
	}

	arcount = ARCOUNT(q->packet);
	/* A TSIG RR is not allowed before the EDNS OPT RR.
	 * In RFC6891 (about EDNS) it says:
	 * "The placement flexibility for the OPT RR does not
	 * override the need for the TSIG or SIG(0) RRs to be
	 * the last in the additional section whenever they are
	 * present."
	 * And in RFC8945 (about TSIG) it says:
	 * "If multiple TSIG records are detected or a TSIG record is
	 * present in any other position, the DNS message is dropped
	 * and a response with RCODE 1 (FORMERR) MUST be returned."
	 */
	/* See if there is an OPT RR. */
	if (arcount > 0) {
		if (edns_parse_record(&q->edns, q->packet, q, nsd))
			--arcount;
	}
	/* See if there is a TSIG RR. */
	if (arcount > 0 && q->tsig.status == TSIG_NOT_PRESENT) {
		/* see if tsig is after the edns record */
		if (!tsig_parse_rr(&q->tsig, q->packet))
			return query_formerr(q, nsd);
		if(q->tsig.status != TSIG_NOT_PRESENT)
			--arcount;
	}
	/* If more RRs left in Add. Section, FORMERR. */
	if (arcount > 0) {
		return query_formerr(q, nsd);
	}

	/* Do we have any trailing garbage? */
#ifdef	STRICT_MESSAGE_PARSE
	if (buffer_remaining(q->packet) > 0) {
		/* If we're strict.... */
		return query_formerr(q, nsd);
	}
#endif
	/* Remove trailing garbage.  */
	buffer_set_limit(q->packet, buffer_position(q->packet));

	rc = process_tsig(q);
	if (rc != NSD_RC_OK) {
		return query_error(q, rc);
	}
	rc = process_edns(nsd, q);
	if (rc != NSD_RC_OK) {
		/* We should not return FORMERR, but BADVERS (=16).
		 * BADVERS is created with Ext. RCODE, followed by RCODE.
		 * Ext. RCODE is set to 1, RCODE must be 0 (getting 0x10 = 16).
		 * Thus RCODE = NOERROR = NSD_RC_OK. */
		RCODE_SET(q->packet, NSD_RC_OK);
		buffer_clear(q->packet);
		buffer_set_position(q->packet,
			QHEADERSZ + 4 + q->qname->name_size);
		QR_SET(q->packet);
		AD_CLR(q->packet);
		QDCOUNT_SET(q->packet, 1);
		ANCOUNT_SET(q->packet, 0);
		NSCOUNT_SET(q->packet, 0);
		ARCOUNT_SET(q->packet, 0);
		return QUERY_PROCESSED;
	}

	if (q->edns.cookie_status == COOKIE_UNVERIFIED)
		cookie_verify(q, nsd, now_p);

	query_prepare_response(q);

	if (q->qclass != CLASS_IN && q->qclass != CLASS_ANY) {
		if (q->qclass == CLASS_CH) {
			return answer_chaos(nsd, q);
		} else {
			/* RFC8914 - Extended DNS Errors
			 * 4.22. Extended DNS Error Code 21 - Not Supported */
			q->edns.ede = EDE_NOT_SUPPORTED;
			return query_error(q, RCODE_REFUSE);
		}
	}
	query_state = answer_axfr_ixfr(nsd, q);
	if (query_state == QUERY_PROCESSED || query_state == QUERY_IN_AXFR
		|| query_state == QUERY_IN_IXFR) {
		return query_state;
	}
	if(q->qtype == TYPE_ANY && nsd->options->refuse_any && !q->tcp) {
		TC_SET(q->packet);
		return query_error(q, NSD_RC_OK);
	}

	answer_query(nsd, q);

	return QUERY_PROCESSED;
}

void
query_add_optional(query_type *q, nsd_type *nsd, uint32_t *now_p)
{
	struct edns_data *edns = &nsd->edns_ipv4;
#if defined(INET6)
	if (q->client_addr.ss_family == AF_INET6) {
		edns = &nsd->edns_ipv6;
	}
#endif
	if (RCODE(q->packet) == RCODE_FORMAT) {
		return;
	}
	switch (q->edns.status) {
	case EDNS_NOT_PRESENT:
		break;
	case EDNS_OK:
		if (q->edns.dnssec_ok)	edns->ok[7] = 0x80;
		else			edns->ok[7] = 0x00;
		buffer_write(q->packet, edns->ok, OPT_LEN);

		/* Add Extended DNS Error (RFC8914)
		 * to verify that we stay in bounds */
		if (q->edns.ede >= 0)
			q->edns.opt_reserved_space +=
				6 + ( q->edns.ede_text_len
			            ? q->edns.ede_text_len : 0);

		if(q->edns.zoneversion
		&& q->zone
		&& q->zone->soa_rrset
		&& q->zone->soa_rrset->rrs
		&& q->zone->soa_rrset->rrs->rdata_count >= 3)
			q->edns.opt_reserved_space += sizeof(uint16_t)
			                           +  sizeof(uint16_t)
			                           +  sizeof(uint8_t)
			                           +  sizeof(uint8_t)
			                           +  sizeof(uint32_t);

		if(q->edns.opt_reserved_space == 0 || !buffer_available(
			q->packet, 2+q->edns.opt_reserved_space)) {
			/* fill with NULLs */
			buffer_write(q->packet, edns->rdata_none, OPT_RDATA);
		} else {
			/* rdata length */
			buffer_write_u16(q->packet, q->edns.opt_reserved_space);
			/* edns options */
			if(q->edns.nsid) {
				/* nsid opt header */
				buffer_write(q->packet, edns->nsid, OPT_HDR);
				/* nsid payload */
				buffer_write(q->packet, nsd->nsid, nsd->nsid_len);
			}
			if(q->edns.zoneversion
			&& q->zone
			&& q->zone->soa_rrset
			&& q->zone->soa_rrset->rrs
			&& q->zone->soa_rrset->rrs->rdata_count >= 3) {
				buffer_write_u16(q->packet, ZONEVERSION_CODE);
				buffer_write_u16( q->packet
				                , sizeof(uint8_t)
						+ sizeof(uint8_t)
						+ sizeof(uint32_t));
				buffer_write_u8(q->packet,
				    domain_dname(q->zone->apex)->label_count - 1);
				buffer_write_u8( q->packet
				               , ZONEVERSION_SOA_SERIAL);
				buffer_write_u32(q->packet,
				    read_uint32(rdata_atom_data(
				    q->zone->soa_rrset->rrs->rdatas[2])));
			}
			if(q->edns.cookie_status != COOKIE_NOT_PRESENT) {
				/* cookie opt header */
				buffer_write(q->packet, edns->cookie, OPT_HDR);
				/* cookie payload */
				cookie_create(q, nsd, now_p);
				buffer_write(q->packet, q->edns.cookie, 24);
			}
			/* Append Extended DNS Error (RFC8914) option if needed */
			if (q->edns.ede >= 0) { /* < 0 means no EDE */
				/* OPTION-CODE */
				buffer_write_u16(q->packet, EDE_CODE);
				/* OPTION-LENGTH */
				buffer_write_u16(q->packet,
					2 + ( q->edns.ede_text_len
					    ? q->edns.ede_text_len : 0));
				/* INFO-CODE */
				buffer_write_u16(q->packet, q->edns.ede);
				/* EXTRA-TEXT */
				if (q->edns.ede_text_len)
					buffer_write(q->packet,
							q->edns.ede_text,
							q->edns.ede_text_len);
			}
		}
		ARCOUNT_SET(q->packet, ARCOUNT(q->packet) + 1);
		STATUP(nsd, edns);
		ZTATUP(nsd, q->zone, edns);
		break;
	case EDNS_ERROR:
		if (q->edns.dnssec_ok)	edns->error[7] = 0x80;
		else			edns->error[7] = 0x00;
		buffer_write(q->packet, edns->error, OPT_LEN);
		buffer_write(q->packet, edns->rdata_none, OPT_RDATA);
		ARCOUNT_SET(q->packet, ARCOUNT(q->packet) + 1);
		STATUP(nsd, ednserr);
		ZTATUP(nsd, q->zone, ednserr);
		break;
	}

	if (q->tsig.status != TSIG_NOT_PRESENT) {
		if (q->tsig.status == TSIG_ERROR ||
			q->tsig.error_code != TSIG_ERROR_NOERROR) {
			tsig_error_reply(&q->tsig);
			tsig_append_rr(&q->tsig, q->packet);
			ARCOUNT_SET(q->packet, ARCOUNT(q->packet) + 1);
		} else if(q->tsig.status == TSIG_OK &&
			q->tsig.error_code == TSIG_ERROR_NOERROR)
		{
			if(q->tsig_prepare_it)
				tsig_prepare(&q->tsig);
			if(q->tsig_update_it)
				tsig_update(&q->tsig, q->packet, buffer_position(q->packet));
			if(q->tsig_sign_it) {
				tsig_sign(&q->tsig);
				tsig_append_rr(&q->tsig, q->packet);
				ARCOUNT_SET(q->packet, ARCOUNT(q->packet) + 1);
			}
		}
	}
}
