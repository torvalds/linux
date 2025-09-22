/*
 * validator/autotrust.h - RFC5011 trust anchor management for unbound.
 *
 * Copyright (c) 2009, NLnet Labs. All rights reserved.
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
 * Contains autotrust definitions.
 */

#ifndef VALIDATOR_AUTOTRUST_H
#define VALIDATOR_AUTOTRUST_H
#include "util/rbtree.h"
#include "util/data/packed_rrset.h"
struct val_anchors;
struct trust_anchor;
struct ub_packed_rrset_key;
struct module_env;
struct module_qstate;
struct val_env;
struct sldns_buffer;

/** Autotrust anchor states */
typedef enum {
	AUTR_STATE_START   = 0,
	AUTR_STATE_ADDPEND = 1,
	AUTR_STATE_VALID   = 2,
	AUTR_STATE_MISSING = 3,
	AUTR_STATE_REVOKED = 4,
	AUTR_STATE_REMOVED = 5
} autr_state_type;

/** 
 * Autotrust metadata for one trust anchor key.
 */
struct autr_ta {
	/** next key */
	struct autr_ta* next;
	/** the RR */
	uint8_t* rr;
	/** length of rr */
	size_t rr_len, dname_len;
	/** last update of key state (new pending count keeps date the same) */
	time_t last_change;
	/** 5011 state */
	autr_state_type s;
	/** pending count */
	uint8_t pending_count;
	/** fresh TA was seen */
	uint8_t fetched;
	/** revoked TA was seen */
	uint8_t revoked;
};

/** 
 * Autotrust metadata for a trust point.
 * This is part of the struct trust_anchor data.
 */
struct autr_point_data {
	/** file to store the trust point in. chrootdir already applied. */
	char* file;
	/** rbtree node for probe sort, key is struct trust_anchor */
	rbnode_type pnode;

	/** the keys */
	struct autr_ta* keys;

	/** last queried DNSKEY set 
	 * Not all failures are captured in this entry.
	 * If the validator did not even start (e.g. timeout or localservfail),
	 * then the last_queried and query_failed values are not updated.
	 */
	time_t last_queried;
	/** last successful DNSKEY set */
	time_t last_success;
	/** next probe time */
	time_t next_probe_time;

	/** when to query if !failed */
	time_t query_interval;
	/** when to retry if failed */
	time_t retry_time;

	/** 
	 * How many times did it fail. diagnostic only (has no effect).
	 * Only updated if there was a dnskey rrset that failed to verify.
	 */
	uint8_t query_failed;
	/** true if the trust point has been revoked */
	uint8_t revoked;
};

/** 
 * Autotrust global metadata.
 */
struct autr_global_data {
	/** rbtree of autotrust anchors sorted by next probe time.
	 * When time is equal, sorted by anchor class, name. */
	rbtree_type probe;
};

/**
 * Create new global 5011 data structure.
 * @return new structure or NULL on malloc failure.
 */
struct autr_global_data* autr_global_create(void);

/**
 * Delete global 5011 data structure.
 * @param global: global autotrust state to delete.
 */
void autr_global_delete(struct autr_global_data* global);

/**
 * See if autotrust anchors are configured and how many.
 * @param anchors: the trust anchors structure.
 * @return number of autotrust trust anchors
 */
size_t autr_get_num_anchors(struct val_anchors* anchors);

/**
 * Process probe timer.  Add new probes if needed.
 * @param env: module environment with time, with anchors and with the mesh.
 * @return time of next probe (in seconds from now).
 * 	If 0, then there is no next probe anymore (trust points deleted).
 */
time_t autr_probe_timer(struct module_env* env);

/** probe tree compare function */
int probetree_cmp(const void* x, const void* y);

/**
 * Read autotrust file.
 * @param anchors: the anchors structure.
 * @param nm: name of the file (copied).
 * @return false on failure.
 */
int autr_read_file(struct val_anchors* anchors, const char* nm);

/**
 * Write autotrust file.
 * @param env: environment with scratch space.
 * @param tp: trust point to write.
 */
void autr_write_file(struct module_env* env, struct trust_anchor* tp);

/**
 * Delete autr anchor, deletes the autr data but does not do
 * unlinking from trees, caller does that.
 * @param tp: trust point to delete.
 */
void autr_point_delete(struct trust_anchor* tp);

/**
 * Perform autotrust processing.
 * @param env: qstate environment with the anchors structure.
 * @param ve: validator environment for verification of rrsigs.
 * @param tp: trust anchor to process.
 * @param dnskey_rrset: DNSKEY rrset probed (can be NULL if bad prime result).
 * 	allocated in a region. Has not been validated yet.
 * @param qstate: qstate with region.
 * @return false if trust anchor was revoked completely.
 * 	Otherwise logs errors to log, does not change return value.
 * 	On errors, likely the trust point has been unchanged.
 */
int autr_process_prime(struct module_env* env, struct val_env* ve,
	struct trust_anchor* tp, struct ub_packed_rrset_key* dnskey_rrset,
	struct module_qstate* qstate);

/**
 * Debug printout of rfc5011 tracked anchors
 * @param anchors: all the anchors.
 */
void autr_debug_print(struct val_anchors* anchors);

/** callback for query answer to 5011 probe */
void probe_answer_cb(void* arg, int rcode, struct sldns_buffer* buf, 
	enum sec_status sec, char* errinf, int was_ratelimited);

#endif /* VALIDATOR_AUTOTRUST_H */
