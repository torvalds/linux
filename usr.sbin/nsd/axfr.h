/*
 * axfr.h -- generating AXFR responses.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef AXFR_H
#define AXFR_H

#include "nsd.h"
#include "query.h"

/*
 * For optimal compression AXFR response packets are limited in size
 * to MAX_COMPRESSION_OFFSET.
 */
#define AXFR_MAX_MESSAGE_LEN MAX_COMPRESSION_OFFSET

query_state_type answer_axfr_ixfr(struct nsd *nsd, struct query *q);
query_state_type query_axfr(struct nsd *nsd, struct query *query, int wstats);

#endif /* AXFR_H */
