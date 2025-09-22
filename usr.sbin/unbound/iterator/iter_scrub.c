/*
 * iterator/iter_scrub.c - scrubbing, normalization, sanitization of DNS msgs.
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
 * This file has routine(s) for cleaning up incoming DNS messages from 
 * possible useless or malicious junk in it.
 */
#include "config.h"
#include "iterator/iter_scrub.h"
#include "iterator/iterator.h"
#include "iterator/iter_priv.h"
#include "services/cache/rrset.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "util/config_file.h"
#include "util/module.h"
#include "util/data/msgparse.h"
#include "util/data/dname.h"
#include "util/data/msgreply.h"
#include "util/alloc.h"
#include "sldns/sbuffer.h"

/** RRset flag used during scrubbing. The RRset is OK. */
#define RRSET_SCRUB_OK	0x80

/** remove rrset, update loop variables */
static void
remove_rrset(const char* str, sldns_buffer* pkt, struct msg_parse* msg, 
	struct rrset_parse* prev, struct rrset_parse** rrset)
{
	if(verbosity >= VERB_QUERY && str
		&& (*rrset)->dname_len <= LDNS_MAX_DOMAINLEN) {
		uint8_t buf[LDNS_MAX_DOMAINLEN+1];
		dname_pkt_copy(pkt, buf, (*rrset)->dname);
		log_nametypeclass(VERB_QUERY, str, buf, 
			(*rrset)->type, ntohs((*rrset)->rrset_class));
	}
	if(prev)
		prev->rrset_all_next = (*rrset)->rrset_all_next;
	else	msg->rrset_first = (*rrset)->rrset_all_next;
	if(msg->rrset_last == *rrset)
		msg->rrset_last = prev;
	msg->rrset_count --;
	switch((*rrset)->section) {
		case LDNS_SECTION_ANSWER: msg->an_rrsets--; break;
		case LDNS_SECTION_AUTHORITY: msg->ns_rrsets--; break;
		case LDNS_SECTION_ADDITIONAL: msg->ar_rrsets--; break;
		default: log_assert(0);
	}
	msgparse_bucket_remove(msg, *rrset);
	*rrset = (*rrset)->rrset_all_next;
}

/** return true if rr type has additional names in it */
static int
has_additional(uint16_t t)
{
	switch(t) {
		case LDNS_RR_TYPE_MB:
		case LDNS_RR_TYPE_MD:
		case LDNS_RR_TYPE_MF:
		case LDNS_RR_TYPE_NS:
		case LDNS_RR_TYPE_MX:
		case LDNS_RR_TYPE_KX:
		case LDNS_RR_TYPE_SRV:
			return 1;
		case LDNS_RR_TYPE_NAPTR:
			/* TODO: NAPTR not supported, glue stripped off */
			return 0;
	}
	return 0;
}

/** get additional name from rrset RR, return false if no name present */
static int
get_additional_name(struct rrset_parse* rrset, struct rr_parse* rr, 
	uint8_t** nm, size_t* nmlen, sldns_buffer* pkt) 
{
	size_t offset = 0;
	size_t len, oldpos;
	switch(rrset->type) {
		case LDNS_RR_TYPE_MB:
		case LDNS_RR_TYPE_MD:
		case LDNS_RR_TYPE_MF:
		case LDNS_RR_TYPE_NS:
			offset = 0;
			break;
		case LDNS_RR_TYPE_MX:
		case LDNS_RR_TYPE_KX:
			offset = 2;
			break;
		case LDNS_RR_TYPE_SRV:
			offset = 6;
			break;
		case LDNS_RR_TYPE_NAPTR:
			/* TODO: NAPTR not supported, glue stripped off */
			return 0;
		default:
			return 0;
	}
	len = sldns_read_uint16(rr->ttl_data+sizeof(uint32_t));
	if(len < offset+1)
		return 0; /* rdata field too small */
	*nm = rr->ttl_data+sizeof(uint32_t)+sizeof(uint16_t)+offset;
	oldpos = sldns_buffer_position(pkt);
	sldns_buffer_set_position(pkt, (size_t)(*nm - sldns_buffer_begin(pkt)));
	*nmlen = pkt_dname_len(pkt);
	sldns_buffer_set_position(pkt, oldpos);
	if(*nmlen == 0)
		return 0;
	return 1;
}

/** Place mark on rrsets in additional section they are OK */
static void
mark_additional_rrset(sldns_buffer* pkt, struct msg_parse* msg, 
	struct rrset_parse* rrset)
{
	/* Mark A and AAAA for NS as appropriate additional section info. */
	uint8_t* nm = NULL;
	size_t nmlen = 0;
	struct rr_parse* rr;

	if(!has_additional(rrset->type))
		return;
	for(rr = rrset->rr_first; rr; rr = rr->next) {
		if(get_additional_name(rrset, rr, &nm, &nmlen, pkt)) {
			/* mark A */
			hashvalue_type h = pkt_hash_rrset(pkt, nm,
				LDNS_RR_TYPE_A, rrset->rrset_class, 0);
			struct rrset_parse* r = msgparse_hashtable_lookup(
				msg, pkt, h, 0, nm, nmlen, 
				LDNS_RR_TYPE_A, rrset->rrset_class);
			if(r && r->section == LDNS_SECTION_ADDITIONAL) {
				r->flags |= RRSET_SCRUB_OK;
			}
			
			/* mark AAAA */
			h = pkt_hash_rrset(pkt, nm, LDNS_RR_TYPE_AAAA, 
				rrset->rrset_class, 0);
			r = msgparse_hashtable_lookup(msg, pkt, h, 0, nm, 
				nmlen, LDNS_RR_TYPE_AAAA, rrset->rrset_class);
			if(r && r->section == LDNS_SECTION_ADDITIONAL) {
				r->flags |= RRSET_SCRUB_OK;
			}
		}
	}
}

/** Get target name of a CNAME */
static int
parse_get_cname_target(struct rrset_parse* rrset, uint8_t** sname, 
	size_t* snamelen, sldns_buffer* pkt)
{
	size_t oldpos, dlen;
	if(rrset->rr_count != 1) {
		struct rr_parse* sig;
		verbose(VERB_ALGO, "Found CNAME rrset with "
			"size > 1: %u", (unsigned)rrset->rr_count);
		/* use the first CNAME! */
		rrset->rr_count = 1;
		rrset->size = rrset->rr_first->size;
		for(sig=rrset->rrsig_first; sig; sig=sig->next)
			rrset->size += sig->size;
		rrset->rr_last = rrset->rr_first;
		rrset->rr_first->next = NULL;
	}
	if(rrset->rr_first->size < sizeof(uint16_t)+1)
		return 0; /* CNAME rdata too small */
	*sname = rrset->rr_first->ttl_data + sizeof(uint32_t)
		+ sizeof(uint16_t); /* skip ttl, rdatalen */
	*snamelen = rrset->rr_first->size - sizeof(uint16_t);

	if(rrset->rr_first->outside_packet) {
		if(!dname_valid(*sname, *snamelen))
			return 0;
		return 1;
	}
	oldpos = sldns_buffer_position(pkt);
	sldns_buffer_set_position(pkt, (size_t)(*sname - sldns_buffer_begin(pkt)));
	dlen = pkt_dname_len(pkt);
	sldns_buffer_set_position(pkt, oldpos);
	if(dlen == 0)
		return 0; /* parse fail on the rdata name */
	*snamelen = dlen;
	return 1;
}

/** Synthesize CNAME from DNAME, false if too long */
static int 
synth_cname(uint8_t* qname, size_t qnamelen, struct rrset_parse* dname_rrset, 
	uint8_t* alias, size_t* aliaslen, sldns_buffer* pkt)
{
	/* we already know that sname is a strict subdomain of DNAME owner */
	uint8_t* dtarg = NULL;
	size_t dtarglen;
	if(!parse_get_cname_target(dname_rrset, &dtarg, &dtarglen, pkt))
		return 0; 
	if(qnamelen <= dname_rrset->dname_len)
		return 0;
	if(qnamelen == 0)
		return 0;
	log_assert(qnamelen > dname_rrset->dname_len);
	/* DNAME from com. to net. with qname example.com. -> example.net. */
	/* so: \3com\0 to \3net\0 and qname \7example\3com\0 */
	*aliaslen = qnamelen + dtarglen - dname_rrset->dname_len;
	if(*aliaslen > LDNS_MAX_DOMAINLEN)
		return 0; /* should have been RCODE YXDOMAIN */
	/* decompress dnames into buffer, we know it fits */
	dname_pkt_copy(pkt, alias, qname);
	dname_pkt_copy(pkt, alias+(qnamelen-dname_rrset->dname_len), dtarg);
	return 1;
}

/** synthesize a CNAME rrset */
static struct rrset_parse*
synth_cname_rrset(uint8_t** sname, size_t* snamelen, uint8_t* alias, 
	size_t aliaslen, struct regional* region, struct msg_parse* msg, 
	struct rrset_parse* rrset, struct rrset_parse* prev,
	struct rrset_parse* nx, sldns_buffer* pkt)
{
	struct rrset_parse* cn = (struct rrset_parse*)regional_alloc(region,
		sizeof(struct rrset_parse));
	if(!cn)
		return NULL;
	memset(cn, 0, sizeof(*cn));
	cn->rr_first = (struct rr_parse*)regional_alloc(region, 
		sizeof(struct rr_parse));
	if(!cn->rr_first)
		return NULL;
	cn->rr_last = cn->rr_first;
	/* CNAME from sname to alias */
	cn->dname = (uint8_t*)regional_alloc(region, *snamelen);
	if(!cn->dname)
		return NULL;
	dname_pkt_copy(pkt, cn->dname, *sname);
	cn->dname_len = *snamelen;
	cn->type = LDNS_RR_TYPE_CNAME;
	cn->section = rrset->section;
	cn->rrset_class = rrset->rrset_class;
	cn->rr_count = 1;
	cn->size = sizeof(uint16_t) + aliaslen;
	cn->hash=pkt_hash_rrset(pkt, cn->dname, cn->type, cn->rrset_class, 0);
	/* allocate TTL + rdatalen + uncompressed dname */
	memset(cn->rr_first, 0, sizeof(struct rr_parse));
	cn->rr_first->outside_packet = 1;
	cn->rr_first->ttl_data = (uint8_t*)regional_alloc(region, 
		sizeof(uint32_t)+sizeof(uint16_t)+aliaslen);
	if(!cn->rr_first->ttl_data)
		return NULL;
	memmove(cn->rr_first->ttl_data, rrset->rr_first->ttl_data,
		sizeof(uint32_t)); /* RFC6672: synth CNAME TTL == DNAME TTL */
	sldns_write_uint16(cn->rr_first->ttl_data+4, aliaslen);
	memmove(cn->rr_first->ttl_data+6, alias, aliaslen);
	cn->rr_first->size = sizeof(uint16_t)+aliaslen;

	/* link it in */
	cn->rrset_all_next = nx;
	if(prev)
		prev->rrset_all_next = cn;
	else	msg->rrset_first = cn;
	if(nx == NULL)
		msg->rrset_last = cn;
	msg->rrset_count ++;
	msg->an_rrsets++;
	/* it is not inserted in the msg hashtable. */

	*sname = cn->rr_first->ttl_data + sizeof(uint32_t)+sizeof(uint16_t);
	*snamelen = aliaslen;
	return cn;
}

/** check if DNAME applies to a name */
static int
pkt_strict_sub(sldns_buffer* pkt, uint8_t* sname, uint8_t* dr)
{
	uint8_t buf1[LDNS_MAX_DOMAINLEN+1];
	uint8_t buf2[LDNS_MAX_DOMAINLEN+1];
	/* decompress names */
	dname_pkt_copy(pkt, buf1, sname);
	dname_pkt_copy(pkt, buf2, dr);
	return dname_strict_subdomain_c(buf1, buf2);
}

/** check subdomain with decompression */
static int
pkt_sub(sldns_buffer* pkt, uint8_t* comprname, uint8_t* zone)
{
	uint8_t buf[LDNS_MAX_DOMAINLEN+1];
	dname_pkt_copy(pkt, buf, comprname);
	return dname_subdomain_c(buf, zone);
}

/** check subdomain with decompression, compressed is parent */
static int
sub_of_pkt(sldns_buffer* pkt, uint8_t* zone, uint8_t* comprname)
{
	uint8_t buf[LDNS_MAX_DOMAINLEN+1];
	dname_pkt_copy(pkt, buf, comprname);
	return dname_subdomain_c(zone, buf);
}

/** Check if there are SOA records in the authority section (negative) */
static int
soa_in_auth(struct msg_parse* msg)
{
	struct rrset_parse* rrset;
	for(rrset = msg->rrset_first; rrset; rrset = rrset->rrset_all_next)
		if(rrset->type == LDNS_RR_TYPE_SOA &&
			rrset->section == LDNS_SECTION_AUTHORITY) 
			return 1;
	return 0;
}

/** Check if type is allowed in the authority section */
static int
type_allowed_in_authority_section(uint16_t tp)
{
	if(tp == LDNS_RR_TYPE_SOA || tp == LDNS_RR_TYPE_NS ||
		tp == LDNS_RR_TYPE_DS || tp == LDNS_RR_TYPE_NSEC ||
		tp == LDNS_RR_TYPE_NSEC3)
		return 1;
	return 0;
}

/** Check if type is allowed in the additional section */
static int
type_allowed_in_additional_section(uint16_t tp)
{
	if(tp == LDNS_RR_TYPE_A || tp == LDNS_RR_TYPE_AAAA)
		return 1;
	return 0;
}

/** Shorten RRset */
static void
shorten_rrset(sldns_buffer* pkt, struct rrset_parse* rrset, int count)
{
	/* The too large NS RRset is shortened. This is so that too large
	 * content does not overwhelm the cache. It may make the rrset
	 * bogus if it was signed, and then the domain is not resolved any
	 * more, that is okay, the NS RRset was too large. During a referral
	 * it can be shortened and then the first part of the list could
	 * be used to resolve. The scrub continues to disallow glue for the
	 * removed nameserver RRs and removes that too. Because the glue
	 * is not marked as okay, since the RRs have been removed here. */
	int i;
	struct rr_parse* rr = rrset->rr_first, *prev = NULL;
	if(!rr)
		return;
	for(i=0; i<count; i++) {
		prev = rr;
		rr = rr->next;
		if(!rr)
			return; /* The RRset is already short. */
	}
	if(verbosity >= VERB_QUERY
		&& rrset->dname_len <= LDNS_MAX_DOMAINLEN) {
		uint8_t buf[LDNS_MAX_DOMAINLEN+1];
		dname_pkt_copy(pkt, buf, rrset->dname);
		log_nametypeclass(VERB_QUERY, "normalize: shorten RRset:", buf,
			rrset->type, ntohs(rrset->rrset_class));
	}
	/* remove further rrs */
	rrset->rr_last = prev;
	rrset->rr_count = count;
	while(rr) {
		rrset->size -= rr->size;
		rr = rr->next;
	}
	if(rrset->rr_last)
		rrset->rr_last->next = NULL;
	else	rrset->rr_first = NULL;
}

/**
 * This routine normalizes a response. This includes removing "irrelevant"
 * records from the answer and additional sections and (re)synthesizing
 * CNAMEs from DNAMEs, if present.
 *
 * @param pkt: packet.
 * @param msg: msg to normalize.
 * @param qinfo: original query.
 * @param region: where to allocate synthesized CNAMEs.
 * @param env: module env with config options.
 * @return 0 on error.
 */
static int
scrub_normalize(sldns_buffer* pkt, struct msg_parse* msg, 
	struct query_info* qinfo, struct regional* region,
	struct module_env* env)
{
	uint8_t* sname = qinfo->qname;
	size_t snamelen = qinfo->qname_len;
	struct rrset_parse* rrset, *prev, *nsset=NULL;
	int cname_length = 0; /* number of CNAMEs, or DNAMEs */

	if(FLAGS_GET_RCODE(msg->flags) != LDNS_RCODE_NOERROR &&
		FLAGS_GET_RCODE(msg->flags) != LDNS_RCODE_NXDOMAIN)
		return 1;

	/* For the ANSWER section, remove all "irrelevant" records and add
	 * synthesized CNAMEs from DNAMEs
	 * This will strip out-of-order CNAMEs as well. */

	/* walk through the parse packet rrset list, keep track of previous
	 * for insert and delete ease, and examine every RRset */
	prev = NULL;
	rrset = msg->rrset_first;
	while(rrset && rrset->section == LDNS_SECTION_ANSWER) {
		if(cname_length > env->cfg->iter_scrub_cname) {
			/* Too many CNAMEs, or DNAMEs, from the authority
			 * server, scrub down the length to something
			 * shorter. This deletes everything after the limit
			 * is reached. The iterator is going to look up
			 * the content one by one anyway. */
			remove_rrset("normalize: removing because too many cnames:",
				pkt, msg, prev, &rrset);
			continue;
		}
		if(rrset->type == LDNS_RR_TYPE_DNAME && 
			pkt_strict_sub(pkt, sname, rrset->dname)) {
			/* check if next rrset is correct CNAME. else,
			 * synthesize a CNAME */
			struct rrset_parse* nx = rrset->rrset_all_next;
			uint8_t alias[LDNS_MAX_DOMAINLEN+1];
			size_t aliaslen = 0;
			if(rrset->rr_count != 1) {
				verbose(VERB_ALGO, "Found DNAME rrset with "
					"size > 1: %u", 
					(unsigned)rrset->rr_count);
				return 0;
			}
			if(!synth_cname(sname, snamelen, rrset, alias, 
				&aliaslen, pkt)) {
				verbose(VERB_ALGO, "synthesized CNAME "
					"too long");
				return 0;
			}
			cname_length++;
			if(nx && nx->type == LDNS_RR_TYPE_CNAME && 
			   dname_pkt_compare(pkt, sname, nx->dname) == 0) {
				/* check next cname */
				uint8_t* t = NULL;
				size_t tlen = 0;
				if(!parse_get_cname_target(nx, &t, &tlen, pkt))
					return 0;
				if(dname_pkt_compare(pkt, alias, t) == 0) {
					/* it's OK and better capitalized */
					prev = rrset;
					rrset = nx;
					continue;
				}
				/* synth ourselves */
			}
			/* synth a CNAME rrset */
			prev = synth_cname_rrset(&sname, &snamelen, alias, 
				aliaslen, region, msg, rrset, rrset, nx, pkt);
			if(!prev) {
				log_err("out of memory synthesizing CNAME");
				return 0;
			}
			/* FIXME: resolve the conflict between synthesized 
			 * CNAME ttls and the cache. */
			rrset = nx;
			continue;

		}

		/* The only records in the ANSWER section not allowed to */
		if(dname_pkt_compare(pkt, sname, rrset->dname) != 0) {
			remove_rrset("normalize: removing irrelevant RRset:", 
				pkt, msg, prev, &rrset);
			continue;
		}

		/* Follow the CNAME chain. */
		if(rrset->type == LDNS_RR_TYPE_CNAME) {
			struct rrset_parse* nx = rrset->rrset_all_next;
			uint8_t* oldsname = sname;
			cname_length++;
			/* see if the next one is a DNAME, if so, swap them */
			if(nx && nx->section == LDNS_SECTION_ANSWER &&
				nx->type == LDNS_RR_TYPE_DNAME &&
				nx->rr_count == 1 &&
				pkt_strict_sub(pkt, sname, nx->dname)) {
				/* there is a DNAME after this CNAME, it 
				 * is in the ANSWER section, and the DNAME
				 * applies to the name we cover */
				/* check if the alias of the DNAME equals
				 * this CNAME */
				uint8_t alias[LDNS_MAX_DOMAINLEN+1];
				size_t aliaslen = 0;
				uint8_t* t = NULL;
				size_t tlen = 0;
				if(synth_cname(sname, snamelen, nx, alias,
					&aliaslen, pkt) &&
					parse_get_cname_target(rrset, &t, &tlen, pkt) &&
			   		dname_pkt_compare(pkt, alias, t) == 0) {
					/* the synthesized CNAME equals the
					 * current CNAME.  This CNAME is the
					 * one that the DNAME creates, and this
					 * CNAME is better capitalised */
					verbose(VERB_ALGO, "normalize: re-order of DNAME and its CNAME");
					if(prev) prev->rrset_all_next = nx;
					else msg->rrset_first = nx;
					if(nx->rrset_all_next == NULL)
						msg->rrset_last = rrset;
					rrset->rrset_all_next =
						nx->rrset_all_next;
					nx->rrset_all_next = rrset;
					/* prev = nx; unused, enable if there
					 * is other rrset removal code after
					 * this */
				}
			}

			/* move to next name in CNAME chain */
			if(!parse_get_cname_target(rrset, &sname, &snamelen, pkt))
				return 0;
			prev = rrset;
			rrset = rrset->rrset_all_next;
			/* in CNAME ANY response, can have data after CNAME */
			if(qinfo->qtype == LDNS_RR_TYPE_ANY) {
				while(rrset && rrset->section ==
					LDNS_SECTION_ANSWER &&
					dname_pkt_compare(pkt, oldsname,
					rrset->dname) == 0) {
					if(rrset->type == LDNS_RR_TYPE_NS &&
						rrset->rr_count > env->cfg->iter_scrub_ns) {
						shorten_rrset(pkt, rrset, env->cfg->iter_scrub_ns);
					}
					prev = rrset;
					rrset = rrset->rrset_all_next;
				}
			}
			continue;
		}

		/* Otherwise, make sure that the RRset matches the qtype. */
		if(qinfo->qtype != LDNS_RR_TYPE_ANY && 
			qinfo->qtype != rrset->type) {
			remove_rrset("normalize: removing irrelevant RRset:", 
				pkt, msg, prev, &rrset);
			continue;
		}

		if(rrset->type == LDNS_RR_TYPE_NS &&
			rrset->rr_count > env->cfg->iter_scrub_ns) {
			shorten_rrset(pkt, rrset, env->cfg->iter_scrub_ns);
		}

		/* Mark the additional names from relevant rrset as OK. */
		/* only for RRsets that match the query name, other ones
		 * will be removed by sanitize, so no additional for them */
		if(dname_pkt_compare(pkt, qinfo->qname, rrset->dname) == 0)
			mark_additional_rrset(pkt, msg, rrset);
		
		prev = rrset;
		rrset = rrset->rrset_all_next;
	}

	/* Mark additional names from AUTHORITY */
	while(rrset && rrset->section == LDNS_SECTION_AUTHORITY) {
		/* protect internals of recursor by making sure to del these */
		if(rrset->type==LDNS_RR_TYPE_DNAME ||
			rrset->type==LDNS_RR_TYPE_CNAME ||
			rrset->type==LDNS_RR_TYPE_A ||
			rrset->type==LDNS_RR_TYPE_AAAA) {
			remove_rrset("normalize: removing irrelevant "
				"RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		/* Allowed list of types in the authority section */
		if(env->cfg->harden_unknown_additional &&
			!type_allowed_in_authority_section(rrset->type)) {
			remove_rrset("normalize: removing irrelevant "
				"RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		/* only one NS set allowed in authority section */
		if(rrset->type==LDNS_RR_TYPE_NS) {
			/* NS set must be pertinent to the query */
			if(!sub_of_pkt(pkt, qinfo->qname, rrset->dname)) {
				remove_rrset("normalize: removing irrelevant "
					"RRset:", pkt, msg, prev, &rrset);
				continue;
			}
			/* we don't want NS sets for NXDOMAIN answers,
			 * because they could contain poisonous contents,
			 * from. eg. fragmentation attacks, inserted after
			 * long RRSIGs in the packet get to the packet
			 * border and such */
			/* also for NODATA answers */
			if(FLAGS_GET_RCODE(msg->flags) == LDNS_RCODE_NXDOMAIN ||
			   (FLAGS_GET_RCODE(msg->flags) == LDNS_RCODE_NOERROR
			    && soa_in_auth(msg) && msg->an_rrsets == 0)) {
				remove_rrset("normalize: removing irrelevant "
					"RRset:", pkt, msg, prev, &rrset);
				continue;
			}
			if(nsset == NULL) {
				nsset = rrset;
			} else {
				remove_rrset("normalize: removing irrelevant "
					"RRset:", pkt, msg, prev, &rrset);
				continue;
			}
			if(rrset->rr_count > env->cfg->iter_scrub_ns) {
				/* If this is not a referral, and the NS RRset
				 * is signed, then remove it entirely, so
				 * that when it becomes bogus it does not
				 * make the message that is otherwise fine
				 * into a bogus message. */
				if(!(msg->an_rrsets == 0 &&
					FLAGS_GET_RCODE(msg->flags) ==
					LDNS_RCODE_NOERROR &&
					!soa_in_auth(msg) &&
					!(msg->flags & BIT_AA)) &&
					rrset->rrsig_count != 0) {
					remove_rrset("normalize: removing too large NS "
						"RRset:", pkt, msg, prev, &rrset);
					continue;
				} else {
					shorten_rrset(pkt, rrset, env->cfg->iter_scrub_ns);
				}
			}
		}
		/* if this is type DS and we query for type DS we just got
		 * a referral answer for our type DS query, fix packet */
		if(rrset->type==LDNS_RR_TYPE_DS &&
			qinfo->qtype == LDNS_RR_TYPE_DS &&
			dname_pkt_compare(pkt, qinfo->qname, rrset->dname) == 0) {
			rrset->section = LDNS_SECTION_ANSWER;
			msg->ancount = rrset->rr_count + rrset->rrsig_count;
			msg->nscount = 0;
			msg->arcount = 0;
			msg->an_rrsets = 1;
			msg->ns_rrsets = 0;
			msg->ar_rrsets = 0;
			msg->rrset_count = 1;
			msg->rrset_first = rrset;
			msg->rrset_last = rrset;
			rrset->rrset_all_next = NULL;
			return 1;
		}
		mark_additional_rrset(pkt, msg, rrset);
		prev = rrset;
		rrset = rrset->rrset_all_next;
	}

	/* For each record in the additional section, remove it if it is an
	 * address record and not in the collection of additional names 
	 * found in ANSWER and AUTHORITY. */
	/* These records have not been marked OK previously */
	while(rrset && rrset->section == LDNS_SECTION_ADDITIONAL) {
		if(rrset->type==LDNS_RR_TYPE_A || 
			rrset->type==LDNS_RR_TYPE_AAAA) 
		{
			if((rrset->flags & RRSET_SCRUB_OK)) {
				/* remove flag to clean up flags variable */
				rrset->flags &= ~RRSET_SCRUB_OK;
			} else {
				remove_rrset("normalize: removing irrelevant "
					"RRset:", pkt, msg, prev, &rrset);
				continue;
			}
		}
		/* protect internals of recursor by making sure to del these */
		if(rrset->type==LDNS_RR_TYPE_DNAME || 
			rrset->type==LDNS_RR_TYPE_CNAME ||
			rrset->type==LDNS_RR_TYPE_NS) {
			remove_rrset("normalize: removing irrelevant "
				"RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		/* Allowed list of types in the additional section */
		if(env->cfg->harden_unknown_additional &&
			!type_allowed_in_additional_section(rrset->type)) {
			remove_rrset("normalize: removing irrelevant "
				"RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		prev = rrset;
		rrset = rrset->rrset_all_next;
	}
	
	return 1;
}

/**
 * Store potential poison in the cache (only if hardening disabled).
 * The rrset is stored in the cache but removed from the message.
 * So that it will be used for infrastructure purposes, but not be 
 * returned to the client.
 * @param pkt: packet
 * @param msg: message parsed
 * @param env: environment with cache
 * @param rrset: to store.
 */
static void
store_rrset(sldns_buffer* pkt, struct msg_parse* msg, struct module_env* env,
	struct rrset_parse* rrset)
{
	struct ub_packed_rrset_key* k;
	struct packed_rrset_data* d;
	struct rrset_ref ref;
	time_t now = *env->now;

	k = alloc_special_obtain(env->alloc);
	if(!k)
		return;
	k->entry.data = NULL;
	if(!parse_copy_decompress_rrset(pkt, msg, rrset, NULL, k)) {
		alloc_special_release(env->alloc, k);
		return;
	}
	d = (struct packed_rrset_data*)k->entry.data;
	packed_rrset_ttl_add(d, now);
	ref.key = k;
	ref.id = k->id;
	/*ignore ret: it was in the cache, ref updated */
	(void)rrset_cache_update(env->rrset_cache, &ref, env->alloc, now);
}

/**
 * Check if right hand name in NSEC is within zone
 * @param pkt: the packet buffer for decompression.
 * @param rrset: the NSEC rrset
 * @param zonename: the zone name.
 * @return true if BAD.
 */
static int sanitize_nsec_is_overreach(sldns_buffer* pkt,
	struct rrset_parse* rrset, uint8_t* zonename)
{
	struct rr_parse* rr;
	uint8_t* rhs;
	size_t len;
	log_assert(rrset->type == LDNS_RR_TYPE_NSEC);
	for(rr = rrset->rr_first; rr; rr = rr->next) {
		size_t pos = sldns_buffer_position(pkt);
		size_t rhspos;
		rhs = rr->ttl_data+4+2;
		len = sldns_read_uint16(rr->ttl_data+4);
		rhspos = rhs-sldns_buffer_begin(pkt);
		sldns_buffer_set_position(pkt, rhspos);
		if(pkt_dname_len(pkt) == 0) {
			/* malformed */
			sldns_buffer_set_position(pkt, pos);
			return 1;
		}
		if(sldns_buffer_position(pkt)-rhspos > len) {
			/* outside of rdata boundaries */
			sldns_buffer_set_position(pkt, pos);
			return 1;
		}
		sldns_buffer_set_position(pkt, pos);
		if(!pkt_sub(pkt, rhs, zonename)) {
			/* overreaching */
			return 1;
		}
	}
	/* all NSEC RRs OK */
	return 0;
}

/** Remove individual RRs, if the length is wrong. Returns true if the RRset
 * has been removed. */
static int
scrub_sanitize_rr_length(sldns_buffer* pkt, struct msg_parse* msg,
	struct rrset_parse* prev, struct rrset_parse** rrset, int* added_ede,
	struct module_qstate* qstate)
{
	struct rr_parse* rr, *rr_prev = NULL;
	for(rr = (*rrset)->rr_first; rr; rr = rr->next) {

		/* Sanity check for length of records
		 * An A record should be 6 bytes only
		 * (2 bytes for length and 4 for IPv4 addr)*/
		if((*rrset)->type == LDNS_RR_TYPE_A && rr->size != 6 ) {
			if(!*added_ede) {
				*added_ede = 1;
				errinf_ede(qstate, "sanitize: records of inappropriate length have been removed.",
					LDNS_EDE_OTHER);
			}
			if(msgparse_rrset_remove_rr("sanitize: removing type A RR of inappropriate length:",
				pkt, *rrset, rr_prev, rr, NULL, 0)) {
				remove_rrset("sanitize: removing type A RRset of inappropriate length:",
					pkt, msg, prev, rrset);
				return 1;
			}
			continue;
		}

		/* Sanity check for length of records
		 * An AAAA record should be 18 bytes only
		 * (2 bytes for length and 16 for IPv6 addr)*/
		if((*rrset)->type == LDNS_RR_TYPE_AAAA && rr->size != 18 ) {
			if(!*added_ede) {
				*added_ede = 1;
				errinf_ede(qstate, "sanitize: records of inappropriate length have been removed.",
					LDNS_EDE_OTHER);
			}
			if(msgparse_rrset_remove_rr("sanitize: removing type AAAA RR of inappropriate length:",
				pkt, *rrset, rr_prev, rr, NULL, 0)) {
				remove_rrset("sanitize: removing type AAAA RRset of inappropriate length:",
					pkt, msg, prev, rrset);
				return 1;
			}
			continue;
		}
		rr_prev = rr;
	}
	return 0;
}

/**
 * Given a response event, remove suspect RRsets from the response.
 * "Suspect" rrsets are potentially poison. Note that this routine expects
 * the response to be in a "normalized" state -- that is, all "irrelevant"
 * RRsets have already been removed, CNAMEs are in order, etc.
 *
 * @param pkt: packet.
 * @param msg: msg to normalize.
 * @param qinfo: the question originally asked.
 * @param zonename: name of server zone.
 * @param env: module environment with config and cache.
 * @param ie: iterator environment with private address data.
 * @param qstate: for setting errinf for EDE error messages.
 * @return 0 on error.
 */
static int
scrub_sanitize(sldns_buffer* pkt, struct msg_parse* msg, 
	struct query_info* qinfo, uint8_t* zonename, struct module_env* env,
	struct iter_env* ie, struct module_qstate* qstate)
{
	int del_addi = 0; /* if additional-holding rrsets are deleted, we
		do not trust the normalized additional-A-AAAA any more */
	uint8_t* ns_rrset_dname = NULL;
	int added_rrlen_ede = 0;
	struct rrset_parse* rrset, *prev;
	prev = NULL;
	rrset = msg->rrset_first;

	/* the first DNAME is allowed to stay. It needs checking before
	 * it can be used from the cache. After normalization, an initial 
	 * DNAME will have a correctly synthesized CNAME after it. */
	if(rrset && rrset->type == LDNS_RR_TYPE_DNAME && 
		rrset->section == LDNS_SECTION_ANSWER &&
		pkt_strict_sub(pkt, qinfo->qname, rrset->dname) &&
		pkt_sub(pkt, rrset->dname, zonename)) {
		prev = rrset; /* DNAME allowed to stay in answer section */
		rrset = rrset->rrset_all_next;
	}
	
	/* remove all records from the answer section that are 
	 * not the same domain name as the query domain name.
	 * The answer section should contain rrsets with the same name
	 * as the question. For DNAMEs a CNAME has been synthesized.
	 * Wildcards have the query name in answer section.
	 * ANY queries get query name in answer section.
	 * Remainders of CNAME chains are cut off and resolved by iterator. */
	while(rrset && rrset->section == LDNS_SECTION_ANSWER) {
		if(dname_pkt_compare(pkt, qinfo->qname, rrset->dname) != 0) {
			if(has_additional(rrset->type)) del_addi = 1;
			remove_rrset("sanitize: removing extraneous answer "
				"RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		prev = rrset;
		rrset = rrset->rrset_all_next;
	}

	/* At this point, we brutally remove ALL rrsets that aren't 
	 * children of the originating zone. The idea here is that, 
	 * as far as we know, the server that we contacted is ONLY 
	 * authoritative for the originating zone. It, of course, MAY 
	 * be authoritative for any other zones, and of course, MAY 
	 * NOT be authoritative for some subdomains of the originating 
	 * zone. */
	prev = NULL;
	rrset = msg->rrset_first;
	while(rrset) {

		/* Sanity check for length of records */
		if(rrset->type == LDNS_RR_TYPE_A ||
			rrset->type == LDNS_RR_TYPE_AAAA) {
			if(scrub_sanitize_rr_length(pkt, msg, prev, &rrset,
				&added_rrlen_ede, qstate))
				continue;
		}

		/* remove private addresses */
		if( (rrset->type == LDNS_RR_TYPE_A || 
			rrset->type == LDNS_RR_TYPE_AAAA)) {

			/* do not set servfail since this leads to too
			 * many drops of other people using rfc1918 space */
			/* also do not remove entire rrset, unless all records
			 * in it are bad */
			if(priv_rrset_bad(ie->priv, pkt, rrset)) {
				remove_rrset(NULL, pkt, msg, prev, &rrset);
				continue;
			}
		}
		
		/* skip DNAME records -- they will always be followed by a 
		 * synthesized CNAME, which will be relevant.
		 * FIXME: should this do something differently with DNAME 
		 * rrsets NOT in Section.ANSWER? */
		/* But since DNAME records are also subdomains of the zone,
		 * same check can be used */

		if(!pkt_sub(pkt, rrset->dname, zonename)) {
			if(msg->an_rrsets == 0 && 
				rrset->type == LDNS_RR_TYPE_NS && 
				rrset->section == LDNS_SECTION_AUTHORITY &&
				FLAGS_GET_RCODE(msg->flags) == 
				LDNS_RCODE_NOERROR && !soa_in_auth(msg) &&
				sub_of_pkt(pkt, zonename, rrset->dname)) {
				/* noerror, nodata and this NS rrset is above
				 * the zone. This is LAME! 
				 * Leave in the NS for lame classification. */
				/* remove everything from the additional
				 * (we dont want its glue that was approved
				 * during the normalize action) */
				del_addi = 1;
			} else if(!env->cfg->harden_glue && (
				rrset->type == LDNS_RR_TYPE_A ||
				rrset->type == LDNS_RR_TYPE_AAAA)) {
				/* store in cache! Since it is relevant
				 * (from normalize) it will be picked up 
				 * from the cache to be used later */
				store_rrset(pkt, msg, env, rrset);
				remove_rrset("sanitize: storing potential "
				"poison RRset:", pkt, msg, prev, &rrset);
				continue;
			} else {
				if(has_additional(rrset->type)) del_addi = 1;
				remove_rrset("sanitize: removing potential "
				"poison RRset:", pkt, msg, prev, &rrset);
				continue;
			}
		}
		if(rrset->type == LDNS_RR_TYPE_NS &&
			(rrset->section == LDNS_SECTION_AUTHORITY ||
			rrset->section == LDNS_SECTION_ANSWER)) {
			/* If the type is NS, and we're in the
			 * answer or authority section, then
			 * store the dname so we can check
			 * against the glue records
			 * further down	*/
			ns_rrset_dname = rrset->dname;
		}
		if(del_addi && rrset->section == LDNS_SECTION_ADDITIONAL) {
			remove_rrset("sanitize: removing potential "
			"poison reference RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		/* check if right hand side of NSEC is within zone */
		if(rrset->type == LDNS_RR_TYPE_NSEC &&
			sanitize_nsec_is_overreach(pkt, rrset, zonename)) {
			remove_rrset("sanitize: removing overreaching NSEC "
				"RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		if(env->cfg->harden_unverified_glue && ns_rrset_dname &&
			rrset->section == LDNS_SECTION_ADDITIONAL &&
			(rrset->type == LDNS_RR_TYPE_A || rrset->type == LDNS_RR_TYPE_AAAA) &&
			!pkt_strict_sub(pkt, rrset->dname, ns_rrset_dname)) {
			/* We're in the additional section, looking
			 * at an A/AAAA rrset, have a previous
			 * delegation point and we notice that
			 * the glue records are NOT for strict
			 * subdomains of the delegation. So set a
			 * flag, recompute the hash for the rrset
			 * and write the A/AAAA record to cache.
			 * It'll be retrieved if we can't separately
			 * resolve the glue	*/
			rrset->flags = PACKED_RRSET_UNVERIFIED_GLUE;
			rrset->hash = pkt_hash_rrset(pkt, rrset->dname, rrset->type, rrset->rrset_class, rrset->flags);
			store_rrset(pkt, msg, env, rrset);
			remove_rrset("sanitize: storing potential "
			"unverified glue reference RRset:", pkt, msg, prev, &rrset);
			continue;
		}
		prev = rrset;
		rrset = rrset->rrset_all_next;
	}
	return 1;
}

int 
scrub_message(sldns_buffer* pkt, struct msg_parse* msg, 
	struct query_info* qinfo, uint8_t* zonename, struct regional* region,
	struct module_env* env, struct module_qstate* qstate,
	struct iter_env* ie)
{
	/* basic sanity checks */
	log_nametypeclass(VERB_ALGO, "scrub for", zonename, LDNS_RR_TYPE_NS, 
		qinfo->qclass);
	if(msg->qdcount > 1)
		return 0;
	if( !(msg->flags&BIT_QR) )
		return 0;
	msg->flags &= ~(BIT_AD|BIT_Z); /* force off bit AD and Z */
	
	/* make sure that a query is echoed back when NOERROR or NXDOMAIN */
	/* this is not required for basic operation but is a forgery 
	 * resistance (security) feature */
	if((FLAGS_GET_RCODE(msg->flags) == LDNS_RCODE_NOERROR ||
		FLAGS_GET_RCODE(msg->flags) == LDNS_RCODE_NXDOMAIN) &&
		msg->qdcount == 0)
		return 0;

	/* if a query is echoed back, make sure it is correct. Otherwise,
	 * this may be not a reply to our query. */
	if(msg->qdcount == 1) {
		if(dname_pkt_compare(pkt, msg->qname, qinfo->qname) != 0)
			return 0;
		if(msg->qtype != qinfo->qtype || msg->qclass != qinfo->qclass)
			return 0;
	}

	/* normalize the response, this cleans up the additional.  */
	if(!scrub_normalize(pkt, msg, qinfo, region, env))
		return 0;
	/* delete all out-of-zone information */
	if(!scrub_sanitize(pkt, msg, qinfo, zonename, env, ie, qstate))
		return 0;
	return 1;
}
