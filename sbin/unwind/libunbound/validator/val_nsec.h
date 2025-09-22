/*
 * validator/val_nsec.h - validator NSEC denial of existence functions.
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

#ifndef VALIDATOR_VAL_NSEC_H
#define VALIDATOR_VAL_NSEC_H
#include "util/data/packed_rrset.h"
#include "sldns/rrdef.h"
struct val_env;
struct module_env;
struct module_qstate;
struct ub_packed_rrset_key;
struct reply_info;
struct query_info;
struct key_entry_key;

/**
 * Check DS absence.
 * There is a NODATA reply to a DS that needs checking.
 * NSECs can prove this is not a delegation point, or successfully prove
 * that there is no DS. Or this fails.
 *
 * @param env: module env for rrsig verification routines.
 * @param ve: validator env for rrsig verification routines.
 * @param qinfo: the DS queried for.
 * @param rep: reply received.
 * @param kkey: key entry to use for verification of signatures.
 * @param proof_ttl: if secure, the TTL of how long this proof lasts.
 * @param reason: string explaining why bogus.
 * @param reason_bogus: relevant EDE code for validation failure.
 * @param qstate: qstate with region.
 * @param reasonbuf: buffer to use for fail reason string print.
 * @param reasonlen: length of reasonbuf.
 * @return security status.
 *	SECURE: proved absence of DS.
 *	INSECURE: proved that this was not a delegation point.
 *	BOGUS: crypto bad, or no absence of DS proven. 
 *	UNCHECKED: there was no way to prove anything (no NSECs, unknown algo).
 */
enum sec_status val_nsec_prove_nodata_dsreply(struct module_env* env,
	struct val_env* ve, struct query_info* qinfo, 
	struct reply_info* rep, struct key_entry_key* kkey,
	time_t* proof_ttl, char** reason, sldns_ede_code* reason_bogus,
	struct module_qstate* qstate, char* reasonbuf, size_t reasonlen);

/** 
 * nsec typemap check, takes an NSEC-type bitmap as argument, checks for type.
 * @param bitmap: pointer to the bitmap part of wireformat rdata.
 * @param len: length of the bitmap, in bytes.
 * @param type: the type (in host order) to check for.
 * @return true if the type bit was set in the bitmap. false if not, or
 * 	if the bitmap was malformed in some way.
 */
int nsecbitmap_has_type_rdata(uint8_t* bitmap, size_t len, uint16_t type);

/**
 * Check if type is present in the NSEC typemap
 * @param nsec: the nsec RRset.
 *	If there are multiple RRs, then each must have the same typemap,
 *	since the typemap represents the types at this domain node.
 * @param type: type to check for, host order.
 * @return true if present
 */
int nsec_has_type(struct ub_packed_rrset_key* nsec, uint16_t type);

/**
 * Determine if a NSEC proves the NOERROR/NODATA conditions. This will also
 * handle the empty non-terminal (ENT) case and partially handle the
 * wildcard case. If the ownername of 'nsec' is a wildcard, the validator
 * must still be provided proof that qname did not directly exist and that
 * the wildcard is, in fact, *.closest_encloser.
 *
 * @param nsec: the nsec record to check against.
 * @param qinfo: the query info.
 * @param wc: if the nodata is proven for a wildcard match, the wildcard
 * 	closest encloser is returned, else NULL (wc is unchanged).
 * 	This closest encloser must then match the nameerror given for the
 * 	nextcloser of qname.
 * @return true if NSEC proves this.
 */
int nsec_proves_nodata(struct ub_packed_rrset_key* nsec, 
	struct query_info* qinfo, uint8_t** wc);

/**
 * Determine if the given NSEC proves a NameError (NXDOMAIN) for a given
 * qname.
 *
 * @param nsec: the nsec to check
 * @param qname: what was queried.
 * @return true if proven.
 */
int val_nsec_proves_name_error(struct ub_packed_rrset_key* nsec, 
	uint8_t* qname);

/**
 * Determine if the given NSEC proves a positive wildcard response.
 * @param nsec: the nsec to check
 * @param qinf: what was queried.
 * @param wc: wildcard (without *. label)
 * @return true if proven.
 */
int val_nsec_proves_positive_wildcard(struct ub_packed_rrset_key* nsec, 
	struct query_info* qinf, uint8_t* wc);

/**
 * Determine closest encloser of a query name and the NSEC that covers it
 * (and thus disproved it). 
 * A name error must have been proven already, otherwise this will be invalid.
 * @param qname: the name queried for.
 * @param nsec: the nsec RRset.
 * @return closest encloser dname or NULL on error (bad nsec RRset).
 */
uint8_t* nsec_closest_encloser(uint8_t* qname, 
	struct ub_packed_rrset_key* nsec);

/**
 * Determine if the given NSEC proves that a wildcard match does not exist.
 *
 * @param nsec: the nsec RRset.
 * @param qname: the name queried for.
 * @param qnamelen: length of qname.
 * @return true if proven.
 */
int val_nsec_proves_no_wc(struct ub_packed_rrset_key* nsec, uint8_t* qname, 
	size_t qnamelen);

/**
 * Determine if an nsec proves an insecure delegation towards the qname.
 * @param nsec: nsec rrset.
 * @param qinfo: what was queries for.
 * @return 0 if not, 1 if an NSEC that signals an insecure delegation to
 * 	the qname.
 */
int val_nsec_proves_insecuredelegation(struct ub_packed_rrset_key* nsec,
        struct query_info* qinfo);

#endif /* VALIDATOR_VAL_NSEC_H */
