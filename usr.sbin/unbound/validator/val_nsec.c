/*
 * validator/val_nsec.c - validator NSEC denial of existence functions.
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
 * This file contains helper functions for the validator module.
 * The functions help with NSEC checking, the different NSEC proofs
 * for denial of existence, and proofs for presence of types.
 */
#include "config.h"
#include "validator/val_nsec.h"
#include "validator/val_utils.h"
#include "util/data/msgreply.h"
#include "util/data/dname.h"
#include "util/net_help.h"
#include "util/module.h"
#include "services/cache/rrset.h"

/** get ttl of rrset */
static uint32_t 
rrset_get_ttl(struct ub_packed_rrset_key* k)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)k->entry.data;
	return d->ttl;
}

int
nsecbitmap_has_type_rdata(uint8_t* bitmap, size_t len, uint16_t type)
{
	/* Check type present in NSEC typemap with bitmap arg */
	/* bitmasks for determining type-lowerbits presence */
	uint8_t masks[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
	uint8_t type_window = type>>8;
	uint8_t type_low = type&0xff;
	uint8_t win, winlen;
	/* read each of the type bitmap windows and see if the searched
	 * type is amongst it */
	while(len > 0) {
		if(len < 3) /* bad window, at least window# winlen bitmap */
			return 0;
		win = *bitmap++;
		winlen = *bitmap++;
		len -= 2;
		if(len < winlen || winlen < 1 || winlen > 32) 
			return 0;	/* bad window length */
		if(win == type_window) {
			/* search window bitmap for the correct byte */
			/* mybyte is 0 if we need the first byte */
			size_t mybyte = type_low>>3;
			if(winlen <= mybyte)
				return 0; /* window too short */
			return (int)(bitmap[mybyte] & masks[type_low&0x7]);
		} else {
			/* not the window we are looking for */
			bitmap += winlen;
			len -= winlen;
		}
	}
	/* end of bitmap reached, no type found */
	return 0;
}

int
nsec_has_type(struct ub_packed_rrset_key* nsec, uint16_t type)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)nsec->
		entry.data;
	size_t len;
	if(!d || d->count == 0 || d->rr_len[0] < 2+1)
		return 0;
	len = dname_valid(d->rr_data[0]+2, d->rr_len[0]-2);
	if(!len)
		return 0;
	return nsecbitmap_has_type_rdata(d->rr_data[0]+2+len, 
		d->rr_len[0]-2-len, type);
}

/**
 * Get next owner name from nsec record
 * @param nsec: the nsec RRset.
 *	If there are multiple RRs, then this will only return one of them.
 * @param nm: the next name is returned.
 * @param ln: length of nm is returned.
 * @return false on a bad NSEC RR (too short, malformed dname).
 */
static int 
nsec_get_next(struct ub_packed_rrset_key* nsec, uint8_t** nm, size_t* ln)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)nsec->
		entry.data;
	if(!d || d->count == 0 || d->rr_len[0] < 2+1) {
		*nm = 0;
		*ln = 0;
		return 0;
	}
	*nm = d->rr_data[0]+2;
	*ln = dname_valid(*nm, d->rr_len[0]-2);
	if(!*ln) {
		*nm = 0;
		*ln = 0;
		return 0;
	}
	return 1;
}

/**
 * For an NSEC that matches the DS queried for, check absence of DS type.
 *
 * @param nsec: NSEC for proof, must be trusted.
 * @param qinfo: what is queried for.
 * @return if secure the nsec proves that no DS is present, or 
 *	insecure if it proves it is not a delegation point.
 *	or bogus if something was wrong.
 */
static enum sec_status 
val_nsec_proves_no_ds(struct ub_packed_rrset_key* nsec, 
	struct query_info* qinfo)
{
	log_assert(qinfo->qtype == LDNS_RR_TYPE_DS);
	log_assert(ntohs(nsec->rk.type) == LDNS_RR_TYPE_NSEC);

	if(nsec_has_type(nsec, LDNS_RR_TYPE_SOA) && qinfo->qname_len != 1) {
		/* SOA present means that this is the NSEC from the child, 
		 * not the parent (so it is the wrong one). */
		return sec_status_bogus;
	}
	if(nsec_has_type(nsec, LDNS_RR_TYPE_DS)) {
		/* DS present means that there should have been a positive 
		 * response to the DS query, so there is something wrong. */
		return sec_status_bogus;
	}

	if(!nsec_has_type(nsec, LDNS_RR_TYPE_NS)) {
		/* If there is no NS at this point at all, then this 
		 * doesn't prove anything one way or the other. */
		return sec_status_insecure;
	}
	/* Otherwise, this proves no DS. */
	return sec_status_secure;
}

/** check security status from cache or verify rrset, returns true if secure */
static int
nsec_verify_rrset(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key* nsec, struct key_entry_key* kkey,
	char** reason, sldns_ede_code* reason_bogus,
	struct module_qstate* qstate, char* reasonbuf, size_t reasonlen)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)
		nsec->entry.data;
	int verified = 0;
	if(!d) return 0;
	if(d->security == sec_status_secure)
		return 1;
	rrset_check_sec_status(env->rrset_cache, nsec, *env->now);
	if(d->security == sec_status_secure)
		return 1;
	d->security = val_verify_rrset_entry(env, ve, nsec, kkey, reason,
		reason_bogus, LDNS_SECTION_AUTHORITY, qstate, &verified,
		reasonbuf, reasonlen);
	if(d->security == sec_status_secure) {
		rrset_update_sec_status(env->rrset_cache, nsec, *env->now);
		return 1;
	}
	return 0;
}

enum sec_status 
val_nsec_prove_nodata_dsreply(struct module_env* env, struct val_env* ve, 
	struct query_info* qinfo, struct reply_info* rep, 
	struct key_entry_key* kkey, time_t* proof_ttl, char** reason,
	sldns_ede_code* reason_bogus, struct module_qstate* qstate,
	char* reasonbuf, size_t reasonlen)
{
	struct ub_packed_rrset_key* nsec = reply_find_rrset_section_ns(
		rep, qinfo->qname, qinfo->qname_len, LDNS_RR_TYPE_NSEC, 
		qinfo->qclass);
	enum sec_status sec;
	size_t i;
	uint8_t* wc = NULL, *ce = NULL;
	int valid_nsec = 0;
	struct ub_packed_rrset_key* wc_nsec = NULL;

	/* If we have a NSEC at the same name, it must prove one 
	 * of two things
	 * --
	 * 1) this is a delegation point and there is no DS
	 * 2) this is not a delegation point */
	if(nsec) {
		if(!nsec_verify_rrset(env, ve, nsec, kkey, reason,
			reason_bogus, qstate, reasonbuf, reasonlen)) {
			verbose(VERB_ALGO, "NSEC RRset for the "
				"referral did not verify.");
			return sec_status_bogus;
		}
		sec = val_nsec_proves_no_ds(nsec, qinfo);
		if(sec == sec_status_bogus) {
			/* something was wrong. */
			*reason = "NSEC does not prove absence of DS";
			*reason_bogus = LDNS_EDE_DNSSEC_BOGUS;
			return sec;
		} else if(sec == sec_status_insecure) {
			/* this wasn't a delegation point. */
			return sec;
		} else if(sec == sec_status_secure) {
			/* this proved no DS. */
			*proof_ttl = ub_packed_rrset_ttl(nsec);
			return sec;
		}
		/* if unchecked, fall through to next proof */
	}

	/* Otherwise, there is no NSEC at qname. This could be an ENT. 
	 * (ENT=empty non terminal). If not, this is broken. */
	
	/* verify NSEC rrsets in auth section */
	for(i=rep->an_numrrsets; i < rep->an_numrrsets+rep->ns_numrrsets; 
		i++) {
		if(rep->rrsets[i]->rk.type != htons(LDNS_RR_TYPE_NSEC))
			continue;
		if(!nsec_verify_rrset(env, ve, rep->rrsets[i], kkey, reason,
			reason_bogus, qstate, reasonbuf, reasonlen)) {
			verbose(VERB_ALGO, "NSEC for empty non-terminal "
				"did not verify.");
			*reason = "NSEC for empty non-terminal "
				"did not verify.";
			return sec_status_bogus;
		}
		if(nsec_proves_nodata(rep->rrsets[i], qinfo, &wc)) {
			verbose(VERB_ALGO, "NSEC for empty non-terminal "
				"proved no DS.");
			*proof_ttl = rrset_get_ttl(rep->rrsets[i]);
			if(wc && dname_is_wild(rep->rrsets[i]->rk.dname)) 
				wc_nsec = rep->rrsets[i];
			valid_nsec = 1;
		}
		if(val_nsec_proves_name_error(rep->rrsets[i], qinfo->qname)) {
			ce = nsec_closest_encloser(qinfo->qname, 
				rep->rrsets[i]);
		}
	}
	if(wc && !ce)
		valid_nsec = 0;
	else if(wc && ce) {
		/* ce and wc must match */
		if(query_dname_compare(wc, ce) != 0) 
			valid_nsec = 0;
		else if(!wc_nsec)
			valid_nsec = 0;
	}
	if(valid_nsec) {
		if(wc) {
			/* check if this is a delegation */
			*reason = "NSEC for wildcard does not prove absence of DS";
			return val_nsec_proves_no_ds(wc_nsec, qinfo);
		}
		/* valid nsec proves empty nonterminal */
		return sec_status_insecure;
	}

	/* NSEC proof did not conclusively point to DS or no DS */
	return sec_status_unchecked;
}

int nsec_proves_nodata(struct ub_packed_rrset_key* nsec, 
	struct query_info* qinfo, uint8_t** wc)
{
	log_assert(wc);
	if(query_dname_compare(nsec->rk.dname, qinfo->qname) != 0) {
		uint8_t* nm;
		size_t ln;

		/* empty-non-terminal checking. 
		 * Done before wildcard, because this is an exact match,
		 * and would prevent a wildcard from matching. */

		/* If the nsec is proving that qname is an ENT, the nsec owner 
		 * will be less than qname, and the next name will be a child 
		 * domain of the qname. */
		if(!nsec_get_next(nsec, &nm, &ln))
			return 0; /* bad nsec */
		if(dname_strict_subdomain_c(nm, qinfo->qname) &&
			dname_canonical_compare(nsec->rk.dname, 
				qinfo->qname) < 0) {
			return 1; /* proves ENT */
		}

		/* wildcard checking. */

		/* If this is a wildcard NSEC, make sure that a) it was 
		 * possible to have generated qname from the wildcard and 
		 * b) the type map does not contain qtype. Note that this 
		 * does NOT prove that this wildcard was the applicable 
		 * wildcard. */
		if(dname_is_wild(nsec->rk.dname)) {
			/* the purported closest encloser. */
			uint8_t* ce = nsec->rk.dname;
			size_t ce_len = nsec->rk.dname_len;
			dname_remove_label(&ce, &ce_len);

			/* The qname must be a strict subdomain of the 
			 * closest encloser, for the wildcard to apply 
			 */
			if(dname_strict_subdomain_c(qinfo->qname, ce)) {
				/* here we have a matching NSEC for the qname,
				 * perform matching NSEC checks */
				if(nsec_has_type(nsec, LDNS_RR_TYPE_CNAME)) {
				   /* should have gotten the wildcard CNAME */
					return 0;
				}
				if(nsec_has_type(nsec, LDNS_RR_TYPE_NS) && 
				   !nsec_has_type(nsec, LDNS_RR_TYPE_SOA)) {
				   /* wrong parentside (wildcard) NSEC used */
					return 0;
				}
				if(nsec_has_type(nsec, qinfo->qtype)) {
					return 0;
				}
				*wc = ce;
				return 1;
			}
		} else {
			/* See if the next owner name covers a wildcard
			 * empty non-terminal. */
			while (dname_canonical_compare(nsec->rk.dname, nm) < 0) {
				/* wildcard does not apply if qname below
				 * the name that exists under the '*' */
				if (dname_subdomain_c(qinfo->qname, nm))
					break;
				/* but if it is a wildcard and qname is below
				 * it, then the wildcard applies. The wildcard
				 * is an empty nonterminal. nodata proven. */
				if (dname_is_wild(nm)) {
					size_t ce_len = ln;
					uint8_t* ce = nm;
					dname_remove_label(&ce, &ce_len);
					if(dname_strict_subdomain_c(qinfo->qname, ce)) {
						*wc = ce;
						return 1;
					}
				}
				dname_remove_label(&nm, &ln);
			}
		}

		/* Otherwise, this NSEC does not prove ENT and is not a 
		 * wildcard, so it does not prove NODATA. */
		return 0;
	}

	/* If the qtype exists, then we should have gotten it. */
	if(nsec_has_type(nsec, qinfo->qtype)) {
		return 0;
	}

	/* if the name is a CNAME node, then we should have gotten the CNAME*/
	if(nsec_has_type(nsec, LDNS_RR_TYPE_CNAME)) {
		return 0;
	}

	/* If an NS set exists at this name, and NOT a SOA (so this is a 
	 * zone cut, not a zone apex), then we should have gotten a 
	 * referral (or we just got the wrong NSEC). 
	 * The reverse of this check is used when qtype is DS, since that
	 * must use the NSEC from above the zone cut. */
	if(qinfo->qtype != LDNS_RR_TYPE_DS &&
		nsec_has_type(nsec, LDNS_RR_TYPE_NS) && 
		!nsec_has_type(nsec, LDNS_RR_TYPE_SOA)) {
		return 0;
	} else if(qinfo->qtype == LDNS_RR_TYPE_DS &&
		nsec_has_type(nsec, LDNS_RR_TYPE_SOA) &&
		!dname_is_root(qinfo->qname)) {
		return 0;
	}

	return 1;
}

int 
val_nsec_proves_name_error(struct ub_packed_rrset_key* nsec, uint8_t* qname)
{
	uint8_t* owner = nsec->rk.dname;
	uint8_t* next;
	size_t nlen;
	if(!nsec_get_next(nsec, &next, &nlen))
		return 0;

	/* If NSEC owner == qname, then this NSEC proves that qname exists. */
	if(query_dname_compare(qname, owner) == 0) {
		return 0;
	}

	/* If NSEC is a parent of qname, we need to check the type map
	 * If the parent name has a DNAME or is a delegation point, then 
	 * this NSEC is being misused. */
	if(dname_subdomain_c(qname, owner) && 
		(nsec_has_type(nsec, LDNS_RR_TYPE_DNAME) ||
		(nsec_has_type(nsec, LDNS_RR_TYPE_NS) 
			&& !nsec_has_type(nsec, LDNS_RR_TYPE_SOA))
		)) {
		return 0;
	}

	if(query_dname_compare(owner, next) == 0) {
		/* this nsec is the only nsec */
		/* zone.name NSEC zone.name, disproves everything else */
		/* but only for subdomains of that zone */
		if(dname_strict_subdomain_c(qname, next))
			return 1;
	}
	else if(dname_canonical_compare(owner, next) > 0) {
		/* this is the last nsec, ....(bigger) NSEC zonename(smaller) */
		/* the names after the last (owner) name do not exist 
		 * there are no names before the zone name in the zone 
		 * but the qname must be a subdomain of the zone name(next). */
		if(dname_canonical_compare(owner, qname) < 0 &&
			dname_strict_subdomain_c(qname, next))
			return 1;
	} else {
		/* regular NSEC, (smaller) NSEC (larger) */
		if(dname_canonical_compare(owner, qname) < 0 &&
		   dname_canonical_compare(qname, next) < 0) {
			return 1;
		}
	}
	return 0;
}

int val_nsec_proves_insecuredelegation(struct ub_packed_rrset_key* nsec, 
	struct query_info* qinfo)
{
	if(nsec_has_type(nsec, LDNS_RR_TYPE_NS) &&
		!nsec_has_type(nsec, LDNS_RR_TYPE_DS) &&
		!nsec_has_type(nsec, LDNS_RR_TYPE_SOA)) {
		/* see if nsec signals an insecure delegation */
		if(qinfo->qtype == LDNS_RR_TYPE_DS) {
			/* if type is DS and qname is equal to nsec, then it
			 * is an exact match nsec, result not insecure */
			if(dname_strict_subdomain_c(qinfo->qname,
				nsec->rk.dname))
				return 1;
		} else {
			if(dname_subdomain_c(qinfo->qname, nsec->rk.dname))
				return 1;
		}
	}
	return 0;
}

uint8_t* 
nsec_closest_encloser(uint8_t* qname, struct ub_packed_rrset_key* nsec)
{
	uint8_t* next;
	size_t nlen;
	uint8_t* common1, *common2;
	if(!nsec_get_next(nsec, &next, &nlen))
		return NULL;
	/* longest common with owner or next name */
	common1 = dname_get_shared_topdomain(nsec->rk.dname, qname);
	common2 = dname_get_shared_topdomain(next, qname);
	if(dname_count_labels(common1) > dname_count_labels(common2))
		return common1;
	return common2;
}

int val_nsec_proves_positive_wildcard(struct ub_packed_rrset_key* nsec, 
	struct query_info* qinf, uint8_t* wc)
{
	uint8_t* ce;
	/*  1) prove that qname doesn't exist and 
	 *  2) that the correct wildcard was used
	 *  nsec has been verified already. */
	if(!val_nsec_proves_name_error(nsec, qinf->qname))
		return 0;
	/* check wildcard name */
	ce = nsec_closest_encloser(qinf->qname, nsec);
	if(!ce)
		return 0;
	if(query_dname_compare(wc, ce) != 0) {
		return 0;
	}
	return 1;
}

int 
val_nsec_proves_no_wc(struct ub_packed_rrset_key* nsec, uint8_t* qname, 
	size_t qnamelen)
{
	/* Determine if a NSEC record proves the non-existence of a 
	 * wildcard that could have produced qname. */
	int labs;
	uint8_t* ce = nsec_closest_encloser(qname, nsec);
	uint8_t* strip;
	size_t striplen;
	uint8_t buf[LDNS_MAX_DOMAINLEN+3];
	if(!ce)
		return 0;
	/* we can subtract the closest encloser count - since that is the
	 * largest shared topdomain with owner and next NSEC name,
	 * because the NSEC is no proof for names shorter than the owner 
	 * and next names. */
	labs = dname_count_labels(qname) - dname_count_labels(ce);

	if(labs > 0) {
		/* i is number of labels to strip off qname, prepend * wild */
		strip = qname;
		striplen = qnamelen;
		dname_remove_labels(&strip, &striplen, labs);
		if(striplen > LDNS_MAX_DOMAINLEN-2)
			return 0; /* too long to prepend wildcard */
		buf[0] = 1;
		buf[1] = (uint8_t)'*';
		memmove(buf+2, strip, striplen);
		if(val_nsec_proves_name_error(nsec, buf)) {
			return 1;
		}
	}
	return 0;
}
