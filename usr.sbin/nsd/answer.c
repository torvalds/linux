/*
 * answer.c -- manipulating query answers and encoding them.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <string.h>

#include "answer.h"
#include "packet.h"
#include "query.h"

void
answer_init(answer_type *answer)
{
	answer->rrset_count = 0;
}

int
answer_add_rrset(answer_type *answer, rr_section_type section,
		 domain_type *domain, rrset_type *rrset)
{
	size_t i;

	assert(section >= ANSWER_SECTION && section < RR_SECTION_COUNT);
	assert(domain);
	assert(rrset);

	/* Don't add an RRset multiple times.  */
	for (i = 0; i < answer->rrset_count; ++i) {
		if (answer->rrsets[i] == rrset &&
			answer->domains[i]->number == domain->number) {
			if (section < answer->section[i]) {
				answer->section[i] = section;
				return 1;
			} else {
				return 0;
			}
		}
	}

	if (answer->rrset_count >= MAXRRSPP) {
		/* XXX: Generate warning/error? */
		return 0;
	}

	answer->section[answer->rrset_count] = section;
	answer->domains[answer->rrset_count] = domain;
	answer->rrsets[answer->rrset_count] = rrset;
	++answer->rrset_count;

	return 1;
}

void
encode_answer(query_type *q, const answer_type *answer)
{
	uint16_t counts[RR_SECTION_COUNT];
	rr_section_type section;
	size_t i;
	int minimal_respsize = IPV4_MINIMAL_RESPONSE_SIZE;
	int done = 0;

#if defined(INET6) && defined(MINIMAL_RESPONSES)
	if (q->client_addr.ss_family == AF_INET6)
		minimal_respsize = IPV6_MINIMAL_RESPONSE_SIZE;
#endif

	for (section = ANSWER_SECTION; section < RR_SECTION_COUNT; ++section) {
		counts[section] = 0;
	}

	for (section = ANSWER_SECTION;
	     !TC(q->packet) && section < RR_SECTION_COUNT;
	     ++section) {

		for (i = 0; !TC(q->packet) && i < answer->rrset_count; ++i) {
			if (answer->section[i] == section) {
				counts[section] += packet_encode_rrset(
					q,
					answer->domains[i],
					answer->rrsets[i],
					section, minimal_respsize, &done);
			}
		}
#ifdef MINIMAL_RESPONSES
		/**
		 * done is set prematurely, because the minimal response size
		 * has been reached. No need to try adding RRsets in following
		 * sections.
		 */
		if (done) {
			/* delegations should have a usable address in it */
			if(section == ADDITIONAL_A_SECTION &&
				counts[ADDITIONAL_A_SECTION] == 0 &&
				q->delegation_domain)
				TC_SET(q->packet);
			break;
		}
#endif
	}

	ANCOUNT_SET(q->packet, counts[ANSWER_SECTION]);
	NSCOUNT_SET(q->packet,
		    counts[AUTHORITY_SECTION]
		    + counts[OPTIONAL_AUTHORITY_SECTION]);
	ARCOUNT_SET(q->packet,
		    counts[ADDITIONAL_A_SECTION]
		    + counts[ADDITIONAL_AAAA_SECTION]
		    + counts[ADDITIONAL_OTHER_SECTION]);
}
