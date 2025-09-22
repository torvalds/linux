/*
 * iterator/iter_resptype.c - response type information and classification.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file defines the response type. DNS Responses can be classified as
 * one of the response types.
 */
#include "config.h"
#include "iterator/iter_resptype.h"
#include "iterator/iter_delegpt.h"
#include "iterator/iterator.h"
#include "services/cache/dns.h"
#include "util/net_help.h"
#include "util/data/dname.h"
#include "sldns/rrdef.h"
#include "sldns/pkthdr.h"

enum response_type 
response_type_from_cache(struct dns_msg* msg, 
	struct query_info* request)
{
	/* If the message is NXDOMAIN, then it is an ANSWER. */
	if(FLAGS_GET_RCODE(msg->rep->flags) == LDNS_RCODE_NXDOMAIN)
		return RESPONSE_TYPE_ANSWER;
	if(request->qtype == LDNS_RR_TYPE_ANY)
		return RESPONSE_TYPE_ANSWER;
	
	/* First we look at the answer section. This can tell us if this is
	 * CNAME or positive ANSWER. */
	if(msg->rep->an_numrrsets > 0) {
		/* Now look at the answer section first. 3 states: 
		 *	o our answer is there directly,
		 *	o our answer is there after a cname,
		 *	o or there is just a cname. */
		uint8_t* mname = request->qname;
		size_t mname_len = request->qname_len;
		size_t i;
		for(i=0; i<msg->rep->an_numrrsets; i++) {
			struct ub_packed_rrset_key* s = msg->rep->rrsets[i];

			/* If we have encountered an answer (before or 
			 * after a CNAME), then we are done! Note that 
			 * if qtype == CNAME then this will be noted as 
			 * an ANSWER before it gets treated as a CNAME, 
			 * as it should */
			if(ntohs(s->rk.type) == request->qtype &&
				ntohs(s->rk.rrset_class) == request->qclass &&
				query_dname_compare(mname, s->rk.dname) == 0) {
				return RESPONSE_TYPE_ANSWER;
			}

			/* If we have encountered a CNAME, make sure that 
			 * it is relevant. */
			if(ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME &&
				query_dname_compare(mname, s->rk.dname) == 0) {
				get_cname_target(s, &mname, &mname_len);
			}
		}

		/* if we encountered a CNAME (or a bunch of CNAMEs), and 
		 * still got to here, then it is a CNAME response. (i.e., 
		 * the CNAME chain didn't terminate in an answer rrset.) */
		if(mname != request->qname) {
			return RESPONSE_TYPE_CNAME;
		}
	}

	/* At this point, since we don't need to detect REFERRAL or LAME 
	 * messages, it can only be an ANSWER. */
	return RESPONSE_TYPE_ANSWER;
}

enum response_type 
response_type_from_server(int rdset,
	struct dns_msg* msg, struct query_info* request, struct delegpt* dp,
	int* empty_nodata_found)
{
	uint8_t* origzone = (uint8_t*)"\000"; /* the default */
	struct ub_packed_rrset_key* s;
	size_t i;

	if(!msg || !request)
		return RESPONSE_TYPE_THROWAWAY;
	/* If the TC flag is set, the response is incomplete. Too large to
	 * fit even in TCP or so. Discard it, it cannot be retrieved here. */
	if((msg->rep->flags & BIT_TC))
		return RESPONSE_TYPE_THROWAWAY;

	/* If the message is NXDOMAIN, then it answers the question. */
	if(FLAGS_GET_RCODE(msg->rep->flags) == LDNS_RCODE_NXDOMAIN) {
		/* make sure its not recursive when we don't want it to */
		if( (msg->rep->flags&BIT_RA) &&
			!(msg->rep->flags&BIT_AA) && !rdset)
				return RESPONSE_TYPE_REC_LAME;
		/* it could be a CNAME with NXDOMAIN rcode */
		for(i=0; i<msg->rep->an_numrrsets; i++) {
			s = msg->rep->rrsets[i];
			if(ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME &&
				query_dname_compare(request->qname,
				s->rk.dname) == 0) {
				return RESPONSE_TYPE_CNAME;
			}
		}
		return RESPONSE_TYPE_ANSWER;
	}
	
	/* Other response codes mean (so far) to throw the response away as
	 * meaningless and move on to the next nameserver. */
	if(FLAGS_GET_RCODE(msg->rep->flags) != LDNS_RCODE_NOERROR)
		return RESPONSE_TYPE_THROWAWAY;

	/* Note: TC bit has already been handled */

	if(dp) {
		origzone = dp->name;
	}

	/* First we look at the answer section. This can tell us if this is a
	 * CNAME or ANSWER or (provisional) ANSWER. */
	if(msg->rep->an_numrrsets > 0) {
		uint8_t* mname = request->qname;
		size_t mname_len = request->qname_len;

		/* Now look at the answer section first. 3 states: our 
		 * answer is there directly, our answer is there after 
		 * a cname, or there is just a cname. */
		for(i=0; i<msg->rep->an_numrrsets; i++) {
			s = msg->rep->rrsets[i];
			
			/* if the answer section has NS rrset, and qtype ANY 
		 	 * and the delegation is lower, and no CNAMEs followed,
		 	 * this is a referral where the NS went to AN section */
			if((request->qtype == LDNS_RR_TYPE_ANY ||
				request->qtype == LDNS_RR_TYPE_NS) &&
				ntohs(s->rk.type) == LDNS_RR_TYPE_NS &&
				ntohs(s->rk.rrset_class) == request->qclass &&
				dname_strict_subdomain_c(s->rk.dname, 
				origzone)) {
				if((msg->rep->flags&BIT_AA))
					return RESPONSE_TYPE_ANSWER;
				return RESPONSE_TYPE_REFERRAL;
			}

			/* If we have encountered an answer (before or 
			 * after a CNAME), then we are done! Note that 
			 * if qtype == CNAME then this will be noted as an
			 * ANSWER before it gets treated as a CNAME, as 
			 * it should. */
			if(ntohs(s->rk.type) == request->qtype &&
				ntohs(s->rk.rrset_class) == request->qclass &&
				query_dname_compare(mname, s->rk.dname) == 0) {
				if((msg->rep->flags&BIT_AA))
					return RESPONSE_TYPE_ANSWER;
				/* If the AA bit isn't on, and we've seen 
				 * the answer, we only provisionally say 
				 * 'ANSWER' -- it very well could be a 
				 * REFERRAL. */
				break;
			}

			/* If we have encountered a CNAME, make sure that 
			 * it is relevant. */
			if(ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME &&
				query_dname_compare(mname, s->rk.dname) == 0) {
				get_cname_target(s, &mname, &mname_len);
			}
		}
		/* not a referral, and qtype any, thus an answer */
		if(request->qtype == LDNS_RR_TYPE_ANY)
			return RESPONSE_TYPE_ANSWER;
		/* if we encountered a CNAME (or a bunch of CNAMEs), and 
		 * still got to here, then it is a CNAME response. 
		 * (This is regardless of the AA bit at this point) */
		if(mname != request->qname) {
			return RESPONSE_TYPE_CNAME;
		}
	}

	/* Looking at the authority section, we just look and see if 
	 * there is a SOA record, that means a NOERROR/NODATA */
	for(i = msg->rep->an_numrrsets; i < (msg->rep->an_numrrsets +
		msg->rep->ns_numrrsets); i++) {
		s = msg->rep->rrsets[i];

		/* The normal way of detecting NOERROR/NODATA. */
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_SOA &&
			dname_subdomain_c(request->qname, s->rk.dname)) {
			/* we do our own recursion, thank you */
			if( (msg->rep->flags&BIT_RA) &&
				!(msg->rep->flags&BIT_AA) && !rdset)
				return RESPONSE_TYPE_REC_LAME;
			return RESPONSE_TYPE_ANSWER;
		}
	}
	/* Looking at the authority section, we just look and see if 
	 * there is a delegation NS set, turning it into a delegation. 
	 * Otherwise, we will have to conclude ANSWER (either it is 
	 * NOERROR/NODATA, or an non-authoritative answer). */
	for(i = msg->rep->an_numrrsets; i < (msg->rep->an_numrrsets +
		msg->rep->ns_numrrsets); i++) {
		s = msg->rep->rrsets[i];

		/* Detect REFERRAL/LAME/ANSWER based on the relationship 
		 * of the NS set to the originating zone name. */
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NS) {
			/* If we are getting an NS set for the zone we 
			 * thought we were contacting, then it is an answer.*/
			if(query_dname_compare(s->rk.dname, origzone) == 0) {
				/* see if mistakenly a recursive server was
				 * deployed and is responding nonAA */
				if( (msg->rep->flags&BIT_RA) &&
					!(msg->rep->flags&BIT_AA) && !rdset)
					return RESPONSE_TYPE_REC_LAME;
				/* Or if a lame server is deployed,
				 * which gives ns==zone delegation from cache 
				 * without AA bit as well, with nodata nosoa*/
				/* real answer must be +AA and SOA RFC(2308),
				 * so this is wrong, and we SERVFAIL it if
				 * this is the only possible reply, if it
				 * is misdeployed the THROWAWAY makes us pick
				 * the next server from the selection */
				if(msg->rep->an_numrrsets==0 &&
					!(msg->rep->flags&BIT_AA) && !rdset)
					return RESPONSE_TYPE_THROWAWAY;
				return RESPONSE_TYPE_ANSWER;
			}
			/* If we are getting a referral upwards (or to 
			 * the same zone), then the server is 'lame'. */
			if(dname_subdomain_c(origzone, s->rk.dname)) {
				if(rdset) /* forward or reclame not LAME */
					return RESPONSE_TYPE_THROWAWAY;
				return RESPONSE_TYPE_LAME;
			}
			/* If the NS set is below the delegation point we 
			 * are on, and it is non-authoritative, then it is 
			 * a referral, otherwise it is an answer. */
			if(dname_subdomain_c(s->rk.dname, origzone)) {
				/* NOTE: I no longer remember in what case 
				 * we would like this to be an answer. 
				 * NODATA should have a SOA or nothing, 
				 * not an NS rrset. 
				 * True, referrals should not have the AA 
				 * bit set, but... */
				 
				/* if((msg->rep->flags&BIT_AA))
					return RESPONSE_TYPE_ANSWER; */
				return RESPONSE_TYPE_REFERRAL;
			}
			/* Otherwise, the NS set is irrelevant. */
		}
	}

	/* If we've gotten this far, this is NOERROR/NODATA (which could 
	 * be an entirely empty message) */
	/* For entirely empty messages, try again, at first, then accept
	 * it it happens more. A regular noerror/nodata response has a soa
	 * negative ttl value in the authority section. This makes it try
	 * again at another authority. And decides between storing a 5 second
	 * empty message or a 5 second servfail response. */
	if(msg->rep->an_numrrsets == 0 && msg->rep->ns_numrrsets == 0 &&
		msg->rep->ar_numrrsets == 0) {
		if(empty_nodata_found) {
			/* detect as throwaway at first, but accept later. */
			(*empty_nodata_found)++;
			if(*empty_nodata_found < EMPTY_NODATA_RETRY_COUNT)
				return RESPONSE_TYPE_THROWAWAY;
			return RESPONSE_TYPE_ANSWER;
		}
		return RESPONSE_TYPE_ANSWER;
	}
	/* check if recursive answer; saying it has empty cache */
	if( (msg->rep->flags&BIT_RA) && !(msg->rep->flags&BIT_AA) && !rdset)
		return RESPONSE_TYPE_REC_LAME;
	return RESPONSE_TYPE_ANSWER;
}
