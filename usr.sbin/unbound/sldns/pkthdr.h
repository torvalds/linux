/*
 * pkthdr.h - packet header from wire conversion routines
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file
 *
 * Contains functions that translate dns data from the wire format (as sent
 * by servers and clients) to the internal structures for the packet header.
 */
 
#ifndef LDNS_PKTHDR_H
#define LDNS_PKTHDR_H

#ifdef __cplusplus
extern "C" {
#endif

/* The length of the header */
#define	LDNS_HEADER_SIZE	12

/* First octet of flags */
#define	LDNS_RD_MASK		0x01U
#define	LDNS_RD_SHIFT	0
#define	LDNS_RD_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_RD_MASK)
#define	LDNS_RD_SET(wirebuf)	(*(wirebuf+2) |= LDNS_RD_MASK)
#define	LDNS_RD_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_RD_MASK)

#define LDNS_TC_MASK		0x02U
#define LDNS_TC_SHIFT	1
#define	LDNS_TC_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_TC_MASK)
#define	LDNS_TC_SET(wirebuf)	(*(wirebuf+2) |= LDNS_TC_MASK)
#define	LDNS_TC_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_TC_MASK)

#define	LDNS_AA_MASK		0x04U
#define	LDNS_AA_SHIFT	2
#define	LDNS_AA_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_AA_MASK)
#define	LDNS_AA_SET(wirebuf)	(*(wirebuf+2) |= LDNS_AA_MASK)
#define	LDNS_AA_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_AA_MASK)

#define	LDNS_OPCODE_MASK	0x78U
#define	LDNS_OPCODE_SHIFT	3
#define	LDNS_OPCODE_WIRE(wirebuf)	((*(wirebuf+2) & LDNS_OPCODE_MASK) >> LDNS_OPCODE_SHIFT)
#define	LDNS_OPCODE_SET(wirebuf, opcode) \
	(*(wirebuf+2) = ((*(wirebuf+2)) & ~LDNS_OPCODE_MASK) | ((opcode) << LDNS_OPCODE_SHIFT))

#define	LDNS_QR_MASK		0x80U
#define	LDNS_QR_SHIFT	7
#define	LDNS_QR_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_QR_MASK)
#define	LDNS_QR_SET(wirebuf)	(*(wirebuf+2) |= LDNS_QR_MASK)
#define	LDNS_QR_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_QR_MASK)

/* Second octet of flags */
#define	LDNS_RCODE_MASK	0x0fU
#define	LDNS_RCODE_SHIFT	0
#define	LDNS_RCODE_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_RCODE_MASK)
#define	LDNS_RCODE_SET(wirebuf, rcode) \
	(*(wirebuf+3) = ((*(wirebuf+3)) & ~LDNS_RCODE_MASK) | (rcode))

#define	LDNS_CD_MASK		0x10U
#define	LDNS_CD_SHIFT	4
#define	LDNS_CD_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_CD_MASK)
#define	LDNS_CD_SET(wirebuf)	(*(wirebuf+3) |= LDNS_CD_MASK)
#define	LDNS_CD_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_CD_MASK)

#define	LDNS_AD_MASK		0x20U
#define	LDNS_AD_SHIFT	5
#define	LDNS_AD_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_AD_MASK)
#define	LDNS_AD_SET(wirebuf)	(*(wirebuf+3) |= LDNS_AD_MASK)
#define	LDNS_AD_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_AD_MASK)

#define	LDNS_Z_MASK		0x40U
#define	LDNS_Z_SHIFT		6
#define	LDNS_Z_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_Z_MASK)
#define	LDNS_Z_SET(wirebuf)	(*(wirebuf+3) |= LDNS_Z_MASK)
#define	LDNS_Z_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_Z_MASK)

#define	LDNS_RA_MASK		0x80U
#define	LDNS_RA_SHIFT	7
#define	LDNS_RA_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_RA_MASK)
#define	LDNS_RA_SET(wirebuf)	(*(wirebuf+3) |= LDNS_RA_MASK)
#define	LDNS_RA_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_RA_MASK)

/* Query ID */
#define	LDNS_ID_WIRE(wirebuf)		(sldns_read_uint16(wirebuf))
#define	LDNS_ID_SET(wirebuf, id)	(sldns_write_uint16(wirebuf, id))

/* Counter of the question section */
#define LDNS_QDCOUNT_OFF		4
/*
#define	QDCOUNT(wirebuf)		(ntohs(*(uint16_t *)(wirebuf+QDCOUNT_OFF)))
*/
#define	LDNS_QDCOUNT(wirebuf)		(sldns_read_uint16(wirebuf+LDNS_QDCOUNT_OFF))
#define LDNS_QDCOUNT_SET(wirebuf, i)    (sldns_write_uint16(wirebuf+LDNS_QDCOUNT_OFF, i))

/* Counter of the answer section */
#define LDNS_ANCOUNT_OFF		6
#define	LDNS_ANCOUNT(wirebuf)		(sldns_read_uint16(wirebuf+LDNS_ANCOUNT_OFF))
#define LDNS_ANCOUNT_SET(wirebuf, i)    (sldns_write_uint16(wirebuf+LDNS_ANCOUNT_OFF, i))

/* Counter of the authority section */
#define LDNS_NSCOUNT_OFF		8
#define	LDNS_NSCOUNT(wirebuf)		(sldns_read_uint16(wirebuf+LDNS_NSCOUNT_OFF))
#define LDNS_NSCOUNT_SET(wirebuf, i)    (sldns_write_uint16(wirebuf+LDNS_NSCOUNT_OFF, i))

/* Counter of the additional section */
#define LDNS_ARCOUNT_OFF		10
#define	LDNS_ARCOUNT(wirebuf)		(sldns_read_uint16(wirebuf+LDNS_ARCOUNT_OFF))
#define LDNS_ARCOUNT_SET(wirebuf, i)    (sldns_write_uint16(wirebuf+LDNS_ARCOUNT_OFF, i))

/**
 * The sections of a packet
 */
enum sldns_enum_pkt_section {
        LDNS_SECTION_QUESTION = 0,
        LDNS_SECTION_ANSWER = 1,
        LDNS_SECTION_AUTHORITY = 2,
        LDNS_SECTION_ADDITIONAL = 3,
        /** bogus section, if not interested */
        LDNS_SECTION_ANY = 4,
        /** used to get all non-question rrs from a packet */
        LDNS_SECTION_ANY_NOQUESTION = 5
};
typedef enum sldns_enum_pkt_section sldns_pkt_section;

/* opcodes for pkt's */
enum sldns_enum_pkt_opcode {
        LDNS_PACKET_QUERY = 0,
        LDNS_PACKET_IQUERY = 1,
        LDNS_PACKET_STATUS = 2, /* there is no 3?? DNS is weird */
        LDNS_PACKET_NOTIFY = 4,
        LDNS_PACKET_UPDATE = 5
};
typedef enum sldns_enum_pkt_opcode sldns_pkt_opcode;

/* rcodes for pkts */
enum sldns_enum_pkt_rcode {
        LDNS_RCODE_NOERROR = 0,
        LDNS_RCODE_FORMERR = 1,
        LDNS_RCODE_SERVFAIL = 2,
        LDNS_RCODE_NXDOMAIN = 3,
        LDNS_RCODE_NOTIMPL = 4,
        LDNS_RCODE_REFUSED = 5,
        LDNS_RCODE_YXDOMAIN = 6,
        LDNS_RCODE_YXRRSET = 7,
        LDNS_RCODE_NXRRSET = 8,
        LDNS_RCODE_NOTAUTH = 9,
        LDNS_RCODE_NOTZONE = 10
};
typedef enum sldns_enum_pkt_rcode sldns_pkt_rcode;

#ifdef __cplusplus
}
#endif

#endif /* LDNS_PKTHDR_H */
