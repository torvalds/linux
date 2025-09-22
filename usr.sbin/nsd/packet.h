/*
 * packet.h -- low-level DNS packet encoding and decoding functions.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef PACKET_H
#define PACKET_H

#include <sys/types.h>

#include "dns.h"
#include "namedb.h"

struct query;

/*
 * Set of macro's to deal with the dns message header as specified
 * in RFC1035 in portable way.
 *
 */

/*
 *
 *                                    1  1  1  1  1  1
 *      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                      ID                       |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |QR|   Opcode  |AA|TC|RD|RA| Z|AD|CD|   RCODE   |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    QDCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    ANCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    NSCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    ARCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 */

/* The length of the header */
#define	QHEADERSZ	12

/* First octet of flags */
#define	RD_MASK		0x01U
#define	RD_SHIFT	0
#define	RD(packet)      (*buffer_at((packet), 2) & RD_MASK)
#define	RD_SET(packet)	(*buffer_at((packet), 2) |= RD_MASK)
#define	RD_CLR(packet)	(*buffer_at((packet), 2) &= ~RD_MASK)

#define TC_MASK		0x02U
#define TC_SHIFT	1
#define	TC(packet)	(*buffer_at((packet), 2) & TC_MASK)
#define	TC_SET(packet)	(*buffer_at((packet), 2) |= TC_MASK)
#define	TC_CLR(packet)	(*buffer_at((packet), 2) &= ~TC_MASK)

#define	AA_MASK		0x04U
#define	AA_SHIFT	2
#define	AA(packet)	(*buffer_at((packet), 2) & AA_MASK)
#define	AA_SET(packet)	(*buffer_at((packet), 2) |= AA_MASK)
#define	AA_CLR(packet)	(*buffer_at((packet), 2) &= ~AA_MASK)

#define	OPCODE_MASK	0x78U
#define	OPCODE_SHIFT	3
#define	OPCODE(packet)	((*buffer_at((packet), 2) & OPCODE_MASK) >> OPCODE_SHIFT)
#define	OPCODE_SET(packet, opcode) \
	(*buffer_at((packet), 2) = (*buffer_at((packet), 2) & ~OPCODE_MASK) | ((opcode) << OPCODE_SHIFT))

#define	QR_MASK		0x80U
#define	QR_SHIFT	7
#define	QR(packet)	(*buffer_at((packet), 2) & QR_MASK)
#define	QR_SET(packet)	(*buffer_at((packet), 2) |= QR_MASK)
#define	QR_CLR(packet)	(*buffer_at((packet), 2) &= ~QR_MASK)

/* Second octet of flags */
#define	RCODE_MASK	0x0fU
#define	RCODE_SHIFT	0
#define	RCODE(packet)	(*buffer_at((packet), 3) & RCODE_MASK)
#define	RCODE_SET(packet, rcode) \
	(*buffer_at((packet), 3) = (*buffer_at((packet), 3) & ~RCODE_MASK) | (rcode))

#define	CD_MASK		0x10U
#define	CD_SHIFT	4
#define	CD(packet)	(*buffer_at((packet), 3) & CD_MASK)
#define	CD_SET(packet)	(*buffer_at((packet), 3) |= CD_MASK)
#define	CD_CLR(packet)	(*buffer_at((packet), 3) &= ~CD_MASK)

#define	AD_MASK		0x20U
#define	AD_SHIFT	5
#define	AD(packet)	(*buffer_at((packet), 3) & AD_MASK)
#define	AD_SET(packet)	(*buffer_at((packet), 3) |= AD_MASK)
#define	AD_CLR(packet)	(*buffer_at((packet), 3) &= ~AD_MASK)

#define	Z_MASK		0x40U
#define	Z_SHIFT		6
#define	Z(packet)	(*buffer_at((packet), 3) & Z_MASK)
#define	Z_SET(packet)	(*buffer_at((packet), 3) |= Z_MASK)
#define	Z_CLR(packet)	(*buffer_at((packet), 3) &= ~Z_MASK)

#define	RA_MASK		0x80U
#define	RA_SHIFT	7
#define	RA(packet)	(*buffer_at((packet), 3) & RA_MASK)
#define	RA_SET(packet)	(*buffer_at((packet), 3) |= RA_MASK)
#define	RA_CLR(packet)	(*buffer_at((packet), 3) &= ~RA_MASK)

/* Query ID */
#define	ID(packet)		(buffer_read_u16_at((packet), 0))
#define	ID_SET(packet, id)	(buffer_write_u16_at((packet), 0, (id)))

/* Flags, RCODE, and OPCODE. */
#define FLAGS(packet)		(buffer_read_u16_at((packet), 2))
#define FLAGS_SET(packet, f)	(buffer_write_u16_at((packet), 2, (f)))

/* Counter of the question section */
#define	QDCOUNT(packet)		(buffer_read_u16_at((packet), 4))
#define QDCOUNT_SET(packet, c)	(buffer_write_u16_at((packet), 4, (c)))

/* Counter of the answer section */
#define	ANCOUNT(packet)		(buffer_read_u16_at((packet), 6))
#define ANCOUNT_SET(packet, c)	(buffer_write_u16_at((packet), 6, (c)))

/* Counter of the authority section */
#define	NSCOUNT(packet)		(buffer_read_u16_at((packet), 8))
#define NSCOUNT_SET(packet, c)	(buffer_write_u16_at((packet), 8, (c)))

/* Counter of the additional section */
#define	ARCOUNT(packet)		(buffer_read_u16_at((packet), 10))
#define ARCOUNT_SET(packet, c)	(buffer_write_u16_at((packet), 10, (c)))

/* Miscellaneous limits */
#define MAX_PACKET_SIZE         65535   /* Maximum supported size of DNS packets.  */

#define	QIOBUFSZ		(MAX_PACKET_SIZE + MAX_RR_SIZE)

#define	MAXRRSPP		10240    /* Maximum number of rr's per packet */
#define MAX_COMPRESSED_DNAMES	MAXRRSPP /* Maximum number of compressed domains. */
#define MAX_COMPRESSION_OFFSET  16383	 /* Compression pointers are 14 bit. */
#define IPV4_MINIMAL_RESPONSE_SIZE 1232	 /* Recommended minimal edns size for IPv4 */
#define IPV6_MINIMAL_RESPONSE_SIZE 1220	 /* Recommended minimal edns size for IPv6 */

/* use round robin rotation */
extern int round_robin;
/* use minimal responses (more minimal, with additional only for referrals) */
extern int minimal_responses;

/*
 * Encode RR with OWNER as owner name into QUERY.  Returns the number
 * of RRs successfully encoded.
 */
int packet_encode_rr(struct query *query,
		     domain_type *owner,
		     rr_type *rr,
		     uint32_t ttl);

/*
 * Encode RRSET with OWNER as the owner name into QUERY.  Returns the
 * number of RRs successfully encoded.  If TRUNCATE_RRSET the entire
 * RRset is truncated in case an RR (or the RRsets signature) does not
 * fit.
 */
int packet_encode_rrset(struct query *query,
			domain_type *owner,
			rrset_type *rrset,
			int truncate_rrset,
			size_t minimal_respsize,
			int* done);

/*
 * Skip the RR at the current position in PACKET.
 */
int packet_skip_rr(buffer_type *packet, int question_section);

/*
 * Skip the dname at the current position in PACKET.
 */
int packet_skip_dname(buffer_type *packet);

/*
 * Read the RR at the current position in PACKET.
 */
rr_type *packet_read_rr(region_type *region,
			domain_table_type *owners,
			buffer_type *packet,
			int question_section);

/*
 * read a query entry from network packet given in buffer.
 * does not follow compression ptrs, checks for errors (returns 0).
 * Dest must be at least MAXDOMAINLEN long.
 */
int packet_read_query_section(buffer_type *packet,
			uint8_t* dest,
			uint16_t* qtype,
			uint16_t* qclass);

/* read notify SOA serial from packet. buffer position is unmodified on return.
 * returns false on no-serial found or parse failure. */
int packet_find_notify_serial(buffer_type *packet, uint32_t* serial);

#endif /* PACKET_H */
