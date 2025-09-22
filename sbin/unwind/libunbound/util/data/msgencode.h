/*
 * util/data/msgencode.h - encode compressed DNS messages.
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
 * This file contains temporary data structures and routines to create
 * compressed DNS messages.
 */

#ifndef UTIL_DATA_MSGENCODE_H
#define UTIL_DATA_MSGENCODE_H
struct sldns_buffer;
struct query_info;
struct reply_info;
struct regional;
struct edns_data;

/** 
 * Generate answer from reply_info.
 * @param qinf: query information that provides query section in packet.
 * @param rep: reply to fill in.
 * @param id: id word from the query.
 * @param qflags: flags word from the query.
 * @param dest: buffer to put message into; will truncate if it does not fit.
 * @param timenow: time to subtract.
 * @param cached: set true if a cached reply (so no AA bit).
 *	set false for the first reply.
 * @param region: where to allocate temp variables (for compression).
 * @param udpsize: size of the answer, 512, from EDNS, or 64k for TCP.
 * @param edns: EDNS data included in the answer, NULL for none.
 *	or if edns_present = 0, it is not included.
 * @param dnssec: if 0 DNSSEC records are omitted from the answer.
 * @param secure: if 1, the AD bit is set in the reply.
 * @return: 0 on error (server failure).
 */
int reply_info_answer_encode(struct query_info* qinf, struct reply_info* rep, 
	uint16_t id, uint16_t qflags, struct sldns_buffer* dest, time_t timenow,
	int cached, struct regional* region, uint16_t udpsize, 
	struct edns_data* edns, int dnssec, int secure);

/**
 * Regenerate the wireformat from the stored msg reply.
 * If the buffer is too small then the message is truncated at a whole
 * rrset and the TC bit set, or whole rrsets are left out of the additional
 * and the TC bit is not set.
 * @param qinfo: query info to store.
 * @param rep: reply to store.
 * @param id: id value to store, network order.
 * @param flags: flags value to store, host order.
 * @param buffer: buffer to store the packet into.
 * @param timenow: time now, to adjust ttl values.
 * @param region: to store temporary data in.
 * @param udpsize: size of the answer, 512, from EDNS, or 64k for TCP.
 * @param dnssec: if 0 DNSSEC records are omitted from the answer.
 * @param minimise: if true, the answer is a minimal response, with
 *   authority and additional removed if possible.
 * @return: nonzero is success, or 
 *	0 on error: malloc failure (no log_err has been done).
 */
int reply_info_encode(struct query_info* qinfo, struct reply_info* rep, 
	uint16_t id, uint16_t flags, struct sldns_buffer* buffer, time_t timenow, 
	struct regional* region, uint16_t udpsize, int dnssec, int minimise);

/**
 * Encode query packet. Assumes the buffer is large enough.
 * @param pkt: where to store the packet.
 * @param qinfo: query info.
 */
void qinfo_query_encode(struct sldns_buffer* pkt, struct query_info* qinfo);

/**
 * Estimate size of EDNS record in packet. EDNS record will be no larger.
 * @param edns: edns data or NULL.
 * @return octets to reserve for EDNS.
 */
uint16_t calc_edns_field_size(struct edns_data* edns);

/**
 * Calculate the size of a specific EDNS option in packet.
 * @param edns: edns data or NULL.
 * @param code: the opt code to get the size of.
 * @return octets the option will take up.
 */
uint16_t calc_edns_option_size(struct edns_data* edns, uint16_t code);

/**
 * Calculate the size of the EDE option(s) in packet. Also calculate seperately
 * the size of the EXTRA-TEXT field(s) in case we can trim them to fit.
 * In this case include any LDNS_EDE_OTHER options in their entirety since they
 * are useless without extra text.
 * @param edns: edns data or NULL.
 * @param txt_size: the size of the EXTRA-TEXT field(s); this includes
 *	LDNS_EDE_OTHER in their entirety since they are useless without
 *	extra text.
 * @return octets the option will take up.
 */
uint16_t calc_ede_option_size(struct edns_data* edns, uint16_t* txt_size);

/**
 * Attach EDNS record to buffer. Buffer has complete packet. There must
 * be enough room left for the EDNS record.
 * @param pkt: packet added to.
 * @param edns: if NULL or present=0, nothing is added to the packet.
 */
void attach_edns_record(struct sldns_buffer* pkt, struct edns_data* edns);

/**
 * Encode an error. With QR and RA set.
 *
 * @param pkt: where to store the packet.
 * @param r: RCODE value to encode (may contain extra flags).
 * @param qinfo: if not NULL, the query is included.
 * @param qid: query ID to set in packet. network order.
 * @param qflags: original query flags (to copy RD and CD bits). host order.
 * @param edns: if not NULL, this is the query edns info,
 * 	and an edns reply is attached. Only attached if EDNS record fits reply.
 */
void error_encode(struct sldns_buffer* pkt, int r, struct query_info* qinfo,
	uint16_t qid, uint16_t qflags, struct edns_data* edns);

/**
 * Encode an extended error. With QR and RA set.
 *
 * @param pkt: where to store the packet.
 * @param rcode: Extended RCODE value to encode.
 * @param qinfo: if not NULL, the query is included.
 * @param qid: query ID to set in packet. network order.
 * @param qflags: original query flags (to copy RD and CD bits). host order.
 * @param xflags: extra flags to set (such as for example BIT_AA and/or BIT_TC)
 * @param edns: if not NULL, this is the query edns info,
 * 	and an edns reply is attached. Only attached if EDNS record fits reply.
 * 	Without edns extended errors (i.e. > 15) will not be conveyed.
 */
void extended_error_encode(struct sldns_buffer* pkt, uint16_t rcode,
	struct query_info* qinfo, uint16_t qid, uint16_t qflags,
	uint16_t xflags, struct edns_data* edns);

#endif /* UTIL_DATA_MSGENCODE_H */
