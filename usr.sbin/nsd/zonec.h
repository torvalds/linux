/*
 * zonec.h -- zone compiler.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef ZONEC_H
#define ZONEC_H

#include "namedb.h"

#define NSEC_WINDOW_COUNT     256
#define NSEC_WINDOW_BITS_COUNT 256
#define NSEC_WINDOW_BITS_SIZE  (NSEC_WINDOW_BITS_COUNT / 8)

#define IPSECKEY_NOGATEWAY      0       /* RFC 4025 */
#define IPSECKEY_IP4            1
#define IPSECKEY_IP6            2
#define IPSECKEY_DNAME          3

#define AMTRELAY_NOGATEWAY      0       /* RFC 8777 */
#define AMTRELAY_IP4            1
#define AMTRELAY_IP6            2
#define AMTRELAY_DNAME          3

#define LINEBUFSZ 1024

#define DEFAULT_TTL 3600

/* parse a zone into memory. name is origin. zonefile is file to read.
 * returns number of errors; failure may have read a partial zone */
unsigned int zonec_read(
	struct namedb *database,
	struct domain_table *domains,
	const char *name,
	const char *zonefile,
	struct zone *zone);

/** check SSHFP type for failures and emit warnings */
void check_sshfp(void);
void apex_rrset_checks(struct namedb* db, rrset_type* rrset,
	domain_type* domain);

#endif /* ZONEC_H */
