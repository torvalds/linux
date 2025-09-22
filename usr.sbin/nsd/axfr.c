/*
 * axfr.c -- generating AXFR responses.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include "axfr.h"
#include "dns.h"
#include "packet.h"
#include "options.h"
#include "ixfr.h"

/* draft-ietf-dnsop-rfc2845bis-06, section 5.3.1 says to sign every packet */
#define AXFR_TSIG_SIGN_EVERY_NTH	0	/* tsig sign every N packets. */

query_state_type
query_axfr(struct nsd *nsd, struct query *query, int wstats)
{
	domain_type *closest_match;
	domain_type *closest_encloser;
	int exact;
	int added;
	uint16_t total_added = 0;

	if (query->axfr_is_done)
		return QUERY_PROCESSED;

	if (query->maxlen > AXFR_MAX_MESSAGE_LEN)
		query->maxlen = AXFR_MAX_MESSAGE_LEN;

	assert(!query_overflow(query));
	/* only keep running values for most packets */
	query->tsig_prepare_it = 0;
	query->tsig_update_it = 1;
	if(query->tsig_sign_it) {
		/* prepare for next updates */
		query->tsig_prepare_it = 1;
		query->tsig_sign_it = 0;
	}

	if (query->axfr_zone == NULL) {
		domain_type* qdomain;
		/* Start AXFR.  */
		if(wstats) {
			STATUP(nsd, raxfr);
		}
		exact = namedb_lookup(nsd->db,
				      query->qname,
				      &closest_match,
				      &closest_encloser);

		qdomain = closest_encloser;
		query->axfr_zone = domain_find_zone(nsd->db, closest_encloser);

		if (!exact
		    || query->axfr_zone == NULL
		    || query->axfr_zone->apex != qdomain
		    || query->axfr_zone->soa_rrset == NULL)
		{
			/* No SOA no transfer */
			RCODE_SET(query->packet, RCODE_NOTAUTH);
			return QUERY_PROCESSED;
		}
		if(wstats) {
			ZTATUP(nsd, query->axfr_zone, raxfr);
		}

		query->axfr_current_domain = qdomain;
		query->axfr_current_rrset = NULL;
		query->axfr_current_rr = 0;
		if(query->tsig.status == TSIG_OK) {
			query->tsig_sign_it = 1; /* sign first packet in stream */
		}

		query_add_compression_domain(query, qdomain, QHEADERSZ);

		assert(query->axfr_zone->soa_rrset->rr_count == 1);
		added = packet_encode_rr(query,
					 query->axfr_zone->apex,
					 &query->axfr_zone->soa_rrset->rrs[0],
					 query->axfr_zone->soa_rrset->rrs[0].ttl);
		if (!added) {
			/* XXX: This should never happen... generate error code? */
			abort();
		}
		++total_added;
	} else {
		/*
		 * Query name and EDNS need not be repeated after the
		 * first response packet.
		 */
		query->edns.status = EDNS_NOT_PRESENT;
		buffer_set_limit(query->packet, QHEADERSZ);
		QDCOUNT_SET(query->packet, 0);
		query_prepare_response(query);
	}

	/* Add zone RRs until answer is full.  */
	while (query->axfr_current_domain != NULL &&
			domain_is_subdomain(query->axfr_current_domain,
					    query->axfr_zone->apex))
	{
		if (!query->axfr_current_rrset) {
			query->axfr_current_rrset = domain_find_any_rrset(
				query->axfr_current_domain,
				query->axfr_zone);
			query->axfr_current_rr = 0;
		}
		while (query->axfr_current_rrset) {
			if (query->axfr_current_rrset != query->axfr_zone->soa_rrset
			    && query->axfr_current_rrset->zone == query->axfr_zone)
			{
				while (query->axfr_current_rr < query->axfr_current_rrset->rr_count) {
					size_t oldmaxlen = query->maxlen;
					if(total_added == 0)
						/* RR > 16K can be first RR */
						query->maxlen = (query->tcp?TCP_MAX_MESSAGE_LEN:UDP_MAX_MESSAGE_LEN);
					added = packet_encode_rr(
						query,
						query->axfr_current_domain,
						&query->axfr_current_rrset->rrs[query->axfr_current_rr],
						query->axfr_current_rrset->rrs[query->axfr_current_rr].ttl);
					if(total_added == 0) {
						query->maxlen = oldmaxlen;
						if(query_overflow(query)) {
							if(added) {
								++total_added;
								++query->axfr_current_rr;
								goto return_answer;
							}
						}
					}
					if (!added)
						goto return_answer;
					++total_added;
					++query->axfr_current_rr;
				}
			}

			query->axfr_current_rrset = query->axfr_current_rrset->next;
			query->axfr_current_rr = 0;
		}
		assert(query->axfr_current_domain);
		query->axfr_current_domain
			= domain_next(query->axfr_current_domain);
	}

	/* Add terminating SOA RR.  */
	assert(query->axfr_zone->soa_rrset->rr_count == 1);
	added = packet_encode_rr(query,
				 query->axfr_zone->apex,
				 &query->axfr_zone->soa_rrset->rrs[0],
				 query->axfr_zone->soa_rrset->rrs[0].ttl);
	if (added) {
		++total_added;
		query->tsig_sign_it = 1; /* sign last packet */
		query->axfr_is_done = 1;
	}

return_answer:
	AA_SET(query->packet);
	ANCOUNT_SET(query->packet, total_added);
	NSCOUNT_SET(query->packet, 0);
	ARCOUNT_SET(query->packet, 0);

	/* check if it needs tsig signatures */
	if(query->tsig.status == TSIG_OK) {
#if AXFR_TSIG_SIGN_EVERY_NTH > 0
		if(query->tsig.updates_since_last_prepare >= AXFR_TSIG_SIGN_EVERY_NTH) {
#endif
			query->tsig_sign_it = 1;
#if AXFR_TSIG_SIGN_EVERY_NTH > 0
		}
#endif
	}
	query_clear_compression_tables(query);
	return QUERY_IN_AXFR;
}

/* See if the query can be admitted. */
static int axfr_ixfr_can_admit_query(struct nsd* nsd, struct query* q)
{
	struct acl_options *acl = NULL;
	struct zone_options* zone_opt;

#ifdef HAVE_SSL
	/* tls-auth-xfr-only is set and this is not an authenticated TLS */
	if (nsd->options->tls_auth_xfr_only && !q->tls_auth) {
		if (verbosity >= 2) {
			char address[128], proxy[128];
			addr2str(&q->client_addr, address, sizeof(address));
			addr2str(&q->remote_addr, proxy, sizeof(proxy));
			VERBOSITY(2, (LOG_INFO, "%s for %s from %s refused tls-auth-xfr-only",
				(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
				dname_to_string(q->qname, NULL),
				address));
		}
		RCODE_SET(q->packet, RCODE_REFUSE);
		/* RFC8914 - Extended DNS Errors
		 * 4.19.  Extended DNS Error Code 18 - Prohibited */
		q->edns.ede = EDE_PROHIBITED;
		return 0;
	}
#endif

	zone_opt = zone_options_find(nsd->options, q->qname);
	if(zone_opt && q->is_proxied && acl_check_incoming_block_proxy(
		zone_opt->pattern->provide_xfr, q, &acl) == -1) {
		/* the proxy address is blocked */
		if (verbosity >= 2) {
			char address[128], proxy[128];
			addr2str(&q->client_addr, address, sizeof(address));
			addr2str(&q->remote_addr, proxy, sizeof(proxy));
			VERBOSITY(2, (LOG_INFO, "%s for %s from %s via proxy %s refused because of proxy, %s %s",
				(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
				dname_to_string(q->qname, NULL),
				address, proxy,
				(acl?acl->ip_address_spec:"."),
				(acl ? ( acl->nokey    ? "NOKEY"
				      : acl->blocked  ? "BLOCKED"
				      : acl->key_name )
				    : "no acl matches")));
		}
		RCODE_SET(q->packet, RCODE_REFUSE);
		/* RFC8914 - Extended DNS Errors
		 * 4.19.  Extended DNS Error Code 18 - Prohibited */
		q->edns.ede = EDE_PROHIBITED;
		return 0;
	}
	if(!zone_opt ||
	   acl_check_incoming(zone_opt->pattern->provide_xfr, q, &acl)==-1)
	{
		if (verbosity >= 2) {
			char a[128];
			addr2str(&q->client_addr, a, sizeof(a));
			VERBOSITY(2, (LOG_INFO, "%s for %s from %s refused, %s",
				(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
				dname_to_string(q->qname, NULL), a, acl?"blocked":"no acl matches"));
		}
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "%s refused, %s",
			(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
			acl?"blocked":"no acl matches"));
		if (!zone_opt) {
			RCODE_SET(q->packet, RCODE_NOTAUTH);
		} else {
			RCODE_SET(q->packet, RCODE_REFUSE);
			/* RFC8914 - Extended DNS Errors
			 * 4.19.  Extended DNS Error Code 18 - Prohibited */
			q->edns.ede = EDE_PROHIBITED;
		}
		return 0;
	}
#ifdef HAVE_SSL
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "%s admitted acl %s %s %s",
		(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
		acl->ip_address_spec, acl->key_name?acl->key_name:"NOKEY",
		(q->tls||q->tls_auth)?(q->tls?"tls":"tls-auth"):""));
#else
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "%s admitted acl %s %s",
		(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
		acl->ip_address_spec, acl->key_name?acl->key_name:"NOKEY"));
#endif
	if (verbosity >= 1) {
		char a[128];
		addr2str(&q->client_addr, a, sizeof(a));
#ifdef HAVE_SSL
		VERBOSITY(1, (LOG_INFO, "%s for %s from %s %s %s",
			(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
			dname_to_string(q->qname, NULL), a,
			(q->tls||q->tls_auth)?(q->tls?"tls":"tls-auth"):"",
			q->cert_cn?q->cert_cn:"not-verified"));
#else
		VERBOSITY(1, (LOG_INFO, "%s for %s from %s",
			(q->qtype==TYPE_AXFR?"axfr":"ixfr"),
			dname_to_string(q->qname, NULL), a));
#endif
	}
	return 1;
}

/*
 * Answer if this is an AXFR or IXFR query.
 */
query_state_type
answer_axfr_ixfr(struct nsd *nsd, struct query *q)
{
	/* Is it AXFR? */
	switch (q->qtype) {
	case TYPE_AXFR:
		if (q->tcp) {
			if(!axfr_ixfr_can_admit_query(nsd, q))
				return QUERY_PROCESSED;
			return query_axfr(nsd, q, 1);
		}
		/* AXFR over UDP queries are discarded. */
		RCODE_SET(q->packet, RCODE_IMPL);
		return QUERY_PROCESSED;
	case TYPE_IXFR:
		if(!axfr_ixfr_can_admit_query(nsd, q)) {
			/* get rid of authority section, if present */
			NSCOUNT_SET(q->packet, 0);
			ARCOUNT_SET(q->packet, 0);
			if(QDCOUNT(q->packet) > 0 && (size_t)QHEADERSZ+4+
				q->qname->name_size <= buffer_limit(q->packet)) {
				buffer_set_position(q->packet, QHEADERSZ+4+
					q->qname->name_size);
			}
			return QUERY_PROCESSED;
		}
		return query_ixfr(nsd, q);
	default:
		return QUERY_DISCARDED;
	}
}
