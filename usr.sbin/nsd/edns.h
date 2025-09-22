/*
 * edns.h -- EDNS definitions (RFC 2671).
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef EDNS_H
#define EDNS_H

#include "buffer.h"
struct nsd;
struct query;

#define OPT_LEN 9U                      /* Length of the NSD EDNS response record minus 2 */
#define OPT_RDATA 2                     /* holds the rdata length comes after OPT_LEN */
#define OPT_HDR 4U                      /* NSID opt header length */
#define NSID_CODE       3               /* nsid option code */
#define COOKIE_CODE    10               /* COOKIE option code */
#define EDE_CODE       15               /* Extended DNS Errors option code */
#define ZONEVERSION_CODE 19             /* ZONEVERSION option code */
#define DNSSEC_OK_MASK  0x8000U         /* do bit mask */

/* https://iana.org/assignments/dns-parameters/#zoneversion-type-values */
#define ZONEVERSION_SOA_SERIAL 0

struct edns_data
{
	unsigned char ok[OPT_LEN];
	unsigned char error[OPT_LEN];
	unsigned char rdata_none[OPT_RDATA];
	unsigned char nsid[OPT_HDR];
	unsigned char cookie[OPT_HDR];
};
typedef struct edns_data edns_data_type;

enum edns_status
{
	EDNS_NOT_PRESENT,
	EDNS_OK,
	/* EDNS states may be extended in the future */
	EDNS_ERROR
};
typedef enum edns_status edns_status_type;

enum cookie_status
{
	COOKIE_NOT_PRESENT,
	COOKIE_UNVERIFIED,
	COOKIE_VALID,
	COOKIE_VALID_REUSE,
	COOKIE_INVALID
};
typedef enum cookie_status cookie_status_type;

struct edns_record
{
	edns_status_type   status;
	size_t             position;
	size_t             maxlen;
	size_t		   opt_reserved_space;
	int                dnssec_ok;
	int                nsid;
	int                zoneversion;
	cookie_status_type cookie_status;
	size_t             cookie_len;
	uint8_t            cookie[40];
	int                ede; /* RFC 8914 - Extended DNS Errors */
	char*              ede_text; /* RFC 8914 - Extended DNS Errors text*/
	uint16_t           ede_text_len;
};
typedef struct edns_record edns_record_type;

/* The Extended DNS Error codes (RFC8914) we use */
#define EDE_OTHER              0
#define EDE_NOT_READY         14
#define EDE_PROHIBITED        18
#define EDE_NOT_AUTHORITATIVE 20
#define EDE_NOT_SUPPORTED     21
#define EDE_INVALID_DATA      24

/* ASSIGN_EDE_CODE_AND_STRING_LITERAL may only be used with string literals.
 * This is guaranteed by concatenating and empty string to LITERAL, which
 * will make compilation fail if this macro is used with variables.
 */
#define ASSIGN_EDE_CODE_AND_STRING_LITERAL(EDE, CODE, LITERAL)	\
	do {							\
		EDE = (CODE);					\
		EDE ## _text = (LITERAL "");			\
		EDE ## _text_len = sizeof(LITERAL) - 1;		\
	} while (0)

void edns_init_data(edns_data_type *data, uint16_t max_length);
void edns_init_record(edns_record_type *data);
int edns_parse_record(edns_record_type *data, buffer_type *packet,
	struct query* q, struct nsd* nsd);

/*
 * The amount of space to reserve in the response for the EDNS data
 * (if required).
 */
size_t edns_reserved_space(edns_record_type *data);

void edns_init_nsid(edns_data_type *data, uint16_t nsid_len);

void cookie_verify(struct query *q, struct nsd* nsd, uint32_t *now_p);
void cookie_create(struct query *q, struct nsd* nsd, uint32_t *now_p);

#endif /* EDNS_H */
