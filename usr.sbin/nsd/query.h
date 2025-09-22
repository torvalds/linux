/*
 * query.h -- manipulation with the queries
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef QUERY_H
#define QUERY_H

#include <assert.h>
#include <string.h>

#include "namedb.h"
#include "nsd.h"
#include "packet.h"
#include "tsig.h"
struct ixfr_data;

enum query_state {
	QUERY_PROCESSED,
	QUERY_DISCARDED,
	QUERY_IN_AXFR,
	QUERY_IN_IXFR
};
typedef enum query_state query_state_type;

/* Query as we pass it around */
typedef struct query query_type;
struct query {
	/*
	 * Memory region freed whenever the query is reset.
	 */
	region_type *region;

	/*
	 * The address the query was received from.
	 */
#ifdef INET6
	struct sockaddr_storage remote_addr;
#else
	struct sockaddr_in remote_addr;
#endif
	socklen_t remote_addrlen;

	/* if set, the request came through a proxy */
	int is_proxied;
	/* the client address
	 * the same as remote_addr if not proxied */
#ifdef INET6
	struct sockaddr_storage client_addr;
#else
	struct sockaddr_in client_addr;
#endif
	socklen_t client_addrlen;

	/*
	 * Maximum supported query size.
	 */
	size_t maxlen;

	/*
	 * Space reserved for optional records like EDNS.
	 */
	size_t reserved_space;

	/* EDNS information provided by the client.  */
	edns_record_type edns;

	/* TSIG record information and running hash for query-response */
	tsig_record_type tsig;
	/* tsig actions can be overridden, for axfr transfer. */
	int tsig_prepare_it, tsig_update_it, tsig_sign_it;

	int tcp;
	uint16_t tcplen;

	buffer_type *packet;

#ifdef HAVE_SSL
	/*
	 * TLS objects.
	*/
	SSL* tls;
	SSL* tls_auth;
	char* cert_cn;
#endif

	/* Normalized query domain name.  */
	const dname_type *qname;

	/* Query type and class in host byte order.  */
	uint16_t qtype;
	uint16_t qclass;

	/* The zone used to answer the query.  */
	zone_type *zone;

	/* The delegation domain, if any.  */
	domain_type *delegation_domain;

	/* The delegation NS rrset, if any.  */
	rrset_type *delegation_rrset;

	/* Original opcode.  */
	uint8_t opcode;

	/*
	 * The number of CNAMES followed.  After a CNAME is followed
	 * we no longer clear AA for a delegation and do not REFUSE
	 * or SERVFAIL if the destination zone of the CNAME does not exist,
	 * or is configured but not present.
	 * Also includes number of DNAMES followed.
	 */
	int cname_count;

	/* Used for dname compression.  */
	uint16_t     compressed_dname_count;
	domain_type **compressed_dnames;

	 /*
	  * Indexed by domain->number, index 0 is reserved for the
	  * query name when generated from a wildcard record.
	  */
	uint16_t    *compressed_dname_offsets;
	size_t compressed_dname_offsets_size;

	/* number of temporary domains used for the query */
	size_t number_temporary_domains;

	/*
	 * Used for AXFR processing.
	 */
	int          axfr_is_done;
	zone_type   *axfr_zone;
	domain_type *axfr_current_domain;
	rrset_type  *axfr_current_rrset;
	uint16_t     axfr_current_rr;

	/* Used for IXFR processing,
	 * indicates if the zone transfer is done, connection can close. */
	int ixfr_is_done;
	/* the ixfr data that is processed */
	struct ixfr_data* ixfr_data;
	/* the ixfr data that is the last segment */
	struct ixfr_data* ixfr_end_data;
	/* ixfr count of newsoa bytes added, 0 none, len means done */
	size_t ixfr_count_newsoa;
	/* ixfr count of oldsoa bytes added, 0 none, len means done */
	size_t ixfr_count_oldsoa;
	/* ixfr count of del bytes added, 0 none, len means done */
	size_t ixfr_count_del;
	/* ixfr count of add bytes added, 0 none, len means done */
	size_t ixfr_count_add;
	/* position for the end of SOA record, for UDP truncation */
	size_t ixfr_pos_of_newsoa;

#ifdef RATELIMIT
	/* if we encountered a wildcard, its domain */
	domain_type *wildcard_domain;
#endif
};


/* Check if the last write resulted in an overflow.  */
static inline int query_overflow(struct query *q);

/*
 * Store the offset of the specified domain in the dname compression
 * table.
 */
void query_put_dname_offset(struct query *query,
			    domain_type  *domain,
			    uint16_t      offset);
/*
 * Lookup the offset of the specified domain in the dname compression
 * table.  Offset 0 is used to indicate the domain is not yet in the
 * compression table.
 */
static inline
uint16_t query_get_dname_offset(struct query *query, domain_type *domain)
{
	return query->compressed_dname_offsets[domain->number];
}

/*
 * Remove all compressed dnames that have an offset that points beyond
 * the end of the current answer.  This must be done after some RRs
 * are truncated and before adding new RRs.  Otherwise dnames may be
 * compressed using truncated data!
 */
void query_clear_dname_offsets(struct query *query, size_t max_offset);

/*
 * Clear the compression tables.
 */
void query_clear_compression_tables(struct query *query);

/*
 * Enter the specified domain into the compression table starting at
 * the specified offset.
 */
void query_add_compression_domain(struct query *query,
				  domain_type  *domain,
				  uint16_t      offset);


/*
 * Create a new query structure.
 */
query_type *query_create(region_type *region,
			 uint16_t *compressed_dname_offsets,
			 size_t compressed_dname_size,
			 domain_type **compressed_dnames);

/*
 * Create a new query structure with buffer pointing to existing memory.
 */
query_type *query_create_with_buffer(region_type *region,
                                     uint16_t *compressed_dname_offsets,
                                     size_t compressed_dname_size,
                                     domain_type **compressed_dnames,
                                     struct buffer *buffer);

/*
 * Replace the query's buffer data.
 */
void query_set_buffer_data(query_type *q, void *data, size_t data_capacity);

/*
 * Reset a query structure so it is ready for receiving and processing
 * a new query.
 */
void query_reset(query_type *query, size_t maxlen, int is_tcp);

/*
 * Process a query and write the response in the query I/O buffer.
 */
query_state_type query_process(query_type *q, nsd_type *nsd, uint32_t *now_p);

/*
 * Prepare the query structure for writing the response. The packet
 * data up-to the current packet limit is preserved. This usually
 * includes the packet header and question section. Space is reserved
 * for the optional EDNS record, if required.
 */
void query_prepare_response(query_type *q);

/*
 * Add EDNS0 information to the response if required.
 */
void query_add_optional(query_type *q, nsd_type *nsd, uint32_t *now_p);

/*
 * Write an error response into the query structure with the indicated
 * RCODE.
 */
query_state_type query_error(query_type *q, nsd_rc_type rcode);

static inline int
query_overflow(query_type *q)
{
	return buffer_position(q->packet) > (q->maxlen - q->reserved_space);
}
#endif /* QUERY_H */
