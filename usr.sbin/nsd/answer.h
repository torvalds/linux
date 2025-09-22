/*
 * answer.h -- manipulating query answers and encoding them.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef ANSWER_H
#define ANSWER_H

#include <sys/types.h>

#include "dns.h"
#include "namedb.h"
#include "packet.h"
#include "query.h"

/*
 * Structure used to keep track of RRsets that need to be stored in
 * the answer packet.
 */
typedef struct answer answer_type;
struct answer {
	size_t rrset_count;
	rrset_type *rrsets[MAXRRSPP];
	domain_type *domains[MAXRRSPP];
	rr_section_type section[MAXRRSPP];
};


void encode_answer(query_type *q, const answer_type *answer);


void answer_init(answer_type *answer);

/*
 * Add the specified RRset to the answer in the specified section.  If
 * the RRset is already present and in the same (or "higher") section
 * return 0, otherwise return 1.
 */
int answer_add_rrset(answer_type *answer, rr_section_type section,
		     domain_type *domain, rrset_type *rrset);


#endif /* ANSWER_H */
