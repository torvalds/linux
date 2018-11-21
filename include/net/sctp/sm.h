/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * These are definitions needed by the state machine.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email addresses:
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Xingang Guo <xingang.guo@intel.com>
 *    Jon Grimm <jgrimm@us.ibm.com>
 *    Dajiang Zhang <dajiang.zhang@nokia.com>
 *    Sridhar Samudrala <sri@us.ibm.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *    Ardelle Fan <ardelle.fan@intel.com>
 *    Kevin Gao <kevin.gao@intel.com>
 */

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <net/sctp/command.h>
#include <net/sctp/sctp.h>

#ifndef __sctp_sm_h__
#define __sctp_sm_h__

/*
 * Possible values for the disposition are:
 */
enum sctp_disposition {
	SCTP_DISPOSITION_DISCARD,	 /* No further processing.  */
	SCTP_DISPOSITION_CONSUME,	 /* Process return values normally.  */
	SCTP_DISPOSITION_NOMEM,		 /* We ran out of memory--recover.  */
	SCTP_DISPOSITION_DELETE_TCB,	 /* Close the association.  */
	SCTP_DISPOSITION_ABORT,		 /* Close the association NOW.  */
	SCTP_DISPOSITION_VIOLATION,	 /* The peer is misbehaving.  */
	SCTP_DISPOSITION_NOT_IMPL,	 /* This entry is not implemented.  */
	SCTP_DISPOSITION_ERROR,		 /* This is plain old user error.  */
	SCTP_DISPOSITION_BUG,		 /* This is a bug.  */
};

typedef enum sctp_disposition (sctp_state_fn_t) (
					struct net *net,
					const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const union sctp_subtype type,
					void *arg,
					struct sctp_cmd_seq *commands);
typedef void (sctp_timer_event_t) (struct timer_list *);
struct sctp_sm_table_entry {
	sctp_state_fn_t *fn;
	const char *name;
};

/* A naming convention of "sctp_sf_xxx" applies to all the state functions
 * currently in use.
 */

/* Prototypes for generic state functions. */
sctp_state_fn_t sctp_sf_not_impl;
sctp_state_fn_t sctp_sf_bug;

/* Prototypes for gener timer state functions. */
sctp_state_fn_t sctp_sf_timer_ignore;

/* Prototypes for chunk state functions. */
sctp_state_fn_t sctp_sf_do_9_1_abort;
sctp_state_fn_t sctp_sf_cookie_wait_abort;
sctp_state_fn_t sctp_sf_cookie_echoed_abort;
sctp_state_fn_t sctp_sf_shutdown_pending_abort;
sctp_state_fn_t sctp_sf_shutdown_sent_abort;
sctp_state_fn_t sctp_sf_shutdown_ack_sent_abort;
sctp_state_fn_t sctp_sf_do_5_1B_init;
sctp_state_fn_t sctp_sf_do_5_1C_ack;
sctp_state_fn_t sctp_sf_do_5_1D_ce;
sctp_state_fn_t sctp_sf_do_5_1E_ca;
sctp_state_fn_t sctp_sf_do_4_C;
sctp_state_fn_t sctp_sf_eat_data_6_2;
sctp_state_fn_t sctp_sf_eat_data_fast_4_4;
sctp_state_fn_t sctp_sf_eat_sack_6_2;
sctp_state_fn_t sctp_sf_operr_notify;
sctp_state_fn_t sctp_sf_t1_init_timer_expire;
sctp_state_fn_t sctp_sf_t1_cookie_timer_expire;
sctp_state_fn_t sctp_sf_t2_timer_expire;
sctp_state_fn_t sctp_sf_t4_timer_expire;
sctp_state_fn_t sctp_sf_t5_timer_expire;
sctp_state_fn_t sctp_sf_sendbeat_8_3;
sctp_state_fn_t sctp_sf_beat_8_3;
sctp_state_fn_t sctp_sf_backbeat_8_3;
sctp_state_fn_t sctp_sf_do_9_2_final;
sctp_state_fn_t sctp_sf_do_9_2_shutdown;
sctp_state_fn_t sctp_sf_do_9_2_shut_ctsn;
sctp_state_fn_t sctp_sf_do_ecn_cwr;
sctp_state_fn_t sctp_sf_do_ecne;
sctp_state_fn_t sctp_sf_ootb;
sctp_state_fn_t sctp_sf_pdiscard;
sctp_state_fn_t sctp_sf_violation;
sctp_state_fn_t sctp_sf_discard_chunk;
sctp_state_fn_t sctp_sf_do_5_2_1_siminit;
sctp_state_fn_t sctp_sf_do_5_2_2_dupinit;
sctp_state_fn_t sctp_sf_do_5_2_3_initack;
sctp_state_fn_t sctp_sf_do_5_2_4_dupcook;
sctp_state_fn_t sctp_sf_unk_chunk;
sctp_state_fn_t sctp_sf_do_8_5_1_E_sa;
sctp_state_fn_t sctp_sf_cookie_echoed_err;
sctp_state_fn_t sctp_sf_do_asconf;
sctp_state_fn_t sctp_sf_do_asconf_ack;
sctp_state_fn_t sctp_sf_do_reconf;
sctp_state_fn_t sctp_sf_do_9_2_reshutack;
sctp_state_fn_t sctp_sf_eat_fwd_tsn;
sctp_state_fn_t sctp_sf_eat_fwd_tsn_fast;
sctp_state_fn_t sctp_sf_eat_auth;

/* Prototypes for primitive event state functions.  */
sctp_state_fn_t sctp_sf_do_prm_asoc;
sctp_state_fn_t sctp_sf_do_prm_send;
sctp_state_fn_t sctp_sf_do_9_2_prm_shutdown;
sctp_state_fn_t sctp_sf_cookie_wait_prm_shutdown;
sctp_state_fn_t sctp_sf_cookie_echoed_prm_shutdown;
sctp_state_fn_t sctp_sf_do_9_1_prm_abort;
sctp_state_fn_t sctp_sf_cookie_wait_prm_abort;
sctp_state_fn_t sctp_sf_cookie_echoed_prm_abort;
sctp_state_fn_t sctp_sf_shutdown_pending_prm_abort;
sctp_state_fn_t sctp_sf_shutdown_sent_prm_abort;
sctp_state_fn_t sctp_sf_shutdown_ack_sent_prm_abort;
sctp_state_fn_t sctp_sf_error_closed;
sctp_state_fn_t sctp_sf_error_shutdown;
sctp_state_fn_t sctp_sf_ignore_primitive;
sctp_state_fn_t sctp_sf_do_prm_requestheartbeat;
sctp_state_fn_t sctp_sf_do_prm_asconf;
sctp_state_fn_t sctp_sf_do_prm_reconf;

/* Prototypes for other event state functions.  */
sctp_state_fn_t sctp_sf_do_no_pending_tsn;
sctp_state_fn_t sctp_sf_do_9_2_start_shutdown;
sctp_state_fn_t sctp_sf_do_9_2_shutdown_ack;
sctp_state_fn_t sctp_sf_ignore_other;
sctp_state_fn_t sctp_sf_cookie_wait_icmp_abort;

/* Prototypes for timeout event state functions.  */
sctp_state_fn_t sctp_sf_do_6_3_3_rtx;
sctp_state_fn_t sctp_sf_send_reconf;
sctp_state_fn_t sctp_sf_do_6_2_sack;
sctp_state_fn_t sctp_sf_autoclose_timer_expire;

/* Prototypes for utility support functions.  */
__u8 sctp_get_chunk_type(struct sctp_chunk *chunk);
const struct sctp_sm_table_entry *sctp_sm_lookup_event(
					struct net *net,
					enum sctp_event event_type,
					enum sctp_state state,
					union sctp_subtype event_subtype);
int sctp_chunk_iif(const struct sctp_chunk *);
struct sctp_association *sctp_make_temp_asoc(const struct sctp_endpoint *,
					     struct sctp_chunk *,
					     gfp_t gfp);
__u32 sctp_generate_verification_tag(void);
void sctp_populate_tie_tags(__u8 *cookie, __u32 curTag, __u32 hisTag);

/* Prototypes for chunk-building functions.  */
struct sctp_chunk *sctp_make_init(const struct sctp_association *asoc,
				  const struct sctp_bind_addr *bp,
				  gfp_t gfp, int vparam_len);
struct sctp_chunk *sctp_make_init_ack(const struct sctp_association *asoc,
				      const struct sctp_chunk *chunk,
				      const gfp_t gfp, const int unkparam_len);
struct sctp_chunk *sctp_make_cookie_echo(const struct sctp_association *asoc,
					 const struct sctp_chunk *chunk);
struct sctp_chunk *sctp_make_cookie_ack(const struct sctp_association *asoc,
					const struct sctp_chunk *chunk);
struct sctp_chunk *sctp_make_cwr(const struct sctp_association *asoc,
				 const __u32 lowest_tsn,
				 const struct sctp_chunk *chunk);
struct sctp_chunk *sctp_make_idata(const struct sctp_association *asoc,
				   __u8 flags, int paylen, gfp_t gfp);
struct sctp_chunk *sctp_make_ifwdtsn(const struct sctp_association *asoc,
				     __u32 new_cum_tsn, size_t nstreams,
				     struct sctp_ifwdtsn_skip *skiplist);
struct sctp_chunk *sctp_make_datafrag_empty(const struct sctp_association *asoc,
					    const struct sctp_sndrcvinfo *sinfo,
					    int len, __u8 flags, gfp_t gfp);
struct sctp_chunk *sctp_make_ecne(const struct sctp_association *asoc,
				  const __u32 lowest_tsn);
struct sctp_chunk *sctp_make_sack(const struct sctp_association *asoc);
struct sctp_chunk *sctp_make_shutdown(const struct sctp_association *asoc,
				      const struct sctp_chunk *chunk);
struct sctp_chunk *sctp_make_shutdown_ack(const struct sctp_association *asoc,
					  const struct sctp_chunk *chunk);
struct sctp_chunk *sctp_make_shutdown_complete(
					const struct sctp_association *asoc,
					const struct sctp_chunk *chunk);
void sctp_init_cause(struct sctp_chunk *chunk, __be16 cause, size_t paylen);
struct sctp_chunk *sctp_make_abort(const struct sctp_association *asoc,
				   const struct sctp_chunk *chunk,
				   const size_t hint);
struct sctp_chunk *sctp_make_abort_no_data(const struct sctp_association *asoc,
					   const struct sctp_chunk *chunk,
					   __u32 tsn);
struct sctp_chunk *sctp_make_abort_user(const struct sctp_association *asoc,
					struct msghdr *msg, size_t msg_len);
struct sctp_chunk *sctp_make_abort_violation(
					const struct sctp_association *asoc,
					const struct sctp_chunk *chunk,
					const __u8 *payload,
					const size_t paylen);
struct sctp_chunk *sctp_make_violation_paramlen(
					const struct sctp_association *asoc,
					const struct sctp_chunk *chunk,
					struct sctp_paramhdr *param);
struct sctp_chunk *sctp_make_violation_max_retrans(
					const struct sctp_association *asoc,
					const struct sctp_chunk *chunk);
struct sctp_chunk *sctp_make_heartbeat(const struct sctp_association *asoc,
				       const struct sctp_transport *transport);
struct sctp_chunk *sctp_make_heartbeat_ack(const struct sctp_association *asoc,
					   const struct sctp_chunk *chunk,
					   const void *payload,
					   const size_t paylen);
struct sctp_chunk *sctp_make_op_error(const struct sctp_association *asoc,
				      const struct sctp_chunk *chunk,
				      __be16 cause_code, const void *payload,
				      size_t paylen, size_t reserve_tail);

struct sctp_chunk *sctp_make_asconf_update_ip(struct sctp_association *asoc,
					      union sctp_addr *laddr,
					      struct sockaddr *addrs,
					      int addrcnt, __be16 flags);
struct sctp_chunk *sctp_make_asconf_set_prim(struct sctp_association *asoc,
					     union sctp_addr *addr);
bool sctp_verify_asconf(const struct sctp_association *asoc,
			struct sctp_chunk *chunk, bool addr_param_needed,
			struct sctp_paramhdr **errp);
struct sctp_chunk *sctp_process_asconf(struct sctp_association *asoc,
				       struct sctp_chunk *asconf);
int sctp_process_asconf_ack(struct sctp_association *asoc,
			    struct sctp_chunk *asconf_ack);
struct sctp_chunk *sctp_make_fwdtsn(const struct sctp_association *asoc,
				    __u32 new_cum_tsn, size_t nstreams,
				    struct sctp_fwdtsn_skip *skiplist);
struct sctp_chunk *sctp_make_auth(const struct sctp_association *asoc,
				  __u16 key_id);
struct sctp_chunk *sctp_make_strreset_req(const struct sctp_association *asoc,
					  __u16 stream_num, __be16 *stream_list,
					  bool out, bool in);
struct sctp_chunk *sctp_make_strreset_tsnreq(
					const struct sctp_association *asoc);
struct sctp_chunk *sctp_make_strreset_addstrm(
					const struct sctp_association *asoc,
					__u16 out, __u16 in);
struct sctp_chunk *sctp_make_strreset_resp(const struct sctp_association *asoc,
					   __u32 result, __u32 sn);
struct sctp_chunk *sctp_make_strreset_tsnresp(struct sctp_association *asoc,
					      __u32 result, __u32 sn,
					      __u32 sender_tsn,
					      __u32 receiver_tsn);
bool sctp_verify_reconf(const struct sctp_association *asoc,
			struct sctp_chunk *chunk,
			struct sctp_paramhdr **errp);
void sctp_chunk_assign_tsn(struct sctp_chunk *chunk);
void sctp_chunk_assign_ssn(struct sctp_chunk *chunk);

/* Prototypes for stream-processing functions.  */
struct sctp_chunk *sctp_process_strreset_outreq(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp);
struct sctp_chunk *sctp_process_strreset_inreq(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp);
struct sctp_chunk *sctp_process_strreset_tsnreq(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp);
struct sctp_chunk *sctp_process_strreset_addstrm_out(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp);
struct sctp_chunk *sctp_process_strreset_addstrm_in(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp);
struct sctp_chunk *sctp_process_strreset_resp(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp);

/* Prototypes for statetable processing. */

int sctp_do_sm(struct net *net, enum sctp_event event_type,
	       union sctp_subtype subtype, enum sctp_state state,
	       struct sctp_endpoint *ep, struct sctp_association *asoc,
	       void *event_arg, gfp_t gfp);

/* 2nd level prototypes */
void sctp_generate_t3_rtx_event(struct timer_list *t);
void sctp_generate_heartbeat_event(struct timer_list *t);
void sctp_generate_reconf_event(struct timer_list *t);
void sctp_generate_proto_unreach_event(struct timer_list *t);

void sctp_ootb_pkt_free(struct sctp_packet *packet);

struct sctp_association *sctp_unpack_cookie(
					const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					struct sctp_chunk *chunk,
					gfp_t gfp, int *err,
					struct sctp_chunk **err_chk_p);

/* 3rd level prototypes */
__u32 sctp_generate_tag(const struct sctp_endpoint *ep);
__u32 sctp_generate_tsn(const struct sctp_endpoint *ep);

/* Extern declarations for major data structures.  */
extern sctp_timer_event_t *sctp_timer_events[SCTP_NUM_TIMEOUT_TYPES];


/* Get the size of a DATA chunk payload. */
static inline __u16 sctp_data_size(struct sctp_chunk *chunk)
{
	__u16 size;

	size = ntohs(chunk->chunk_hdr->length);
	size -= sctp_datahdr_len(&chunk->asoc->stream);

	return size;
}

/* Compare two TSNs */
#define TSN_lt(a,b)	\
	(typecheck(__u32, a) && \
	 typecheck(__u32, b) && \
	 ((__s32)((a) - (b)) < 0))

#define TSN_lte(a,b)	\
	(typecheck(__u32, a) && \
	 typecheck(__u32, b) && \
	 ((__s32)((a) - (b)) <= 0))

/* Compare two MIDs */
#define MID_lt(a, b)	\
	(typecheck(__u32, a) && \
	 typecheck(__u32, b) && \
	 ((__s32)((a) - (b)) < 0))

/* Compare two SSNs */
#define SSN_lt(a,b)		\
	(typecheck(__u16, a) && \
	 typecheck(__u16, b) && \
	 ((__s16)((a) - (b)) < 0))

/* ADDIP 3.1.1 */
#define ADDIP_SERIAL_gte(a,b)	\
	(typecheck(__u32, a) && \
	 typecheck(__u32, b) && \
	 ((__s32)((b) - (a)) <= 0))

/* Check VTAG of the packet matches the sender's own tag. */
static inline int
sctp_vtag_verify(const struct sctp_chunk *chunk,
		 const struct sctp_association *asoc)
{
	/* RFC 2960 Sec 8.5 When receiving an SCTP packet, the endpoint
	 * MUST ensure that the value in the Verification Tag field of
	 * the received SCTP packet matches its own Tag. If the received
	 * Verification Tag value does not match the receiver's own
	 * tag value, the receiver shall silently discard the packet...
	 */
        if (ntohl(chunk->sctp_hdr->vtag) == asoc->c.my_vtag)
                return 1;

	return 0;
}

/* Check VTAG of the packet matches the sender's own tag and the T bit is
 * not set, OR its peer's tag and the T bit is set in the Chunk Flags.
 */
static inline int
sctp_vtag_verify_either(const struct sctp_chunk *chunk,
			const struct sctp_association *asoc)
{
        /* RFC 2960 Section 8.5.1, sctpimpguide Section 2.41
	 *
	 * B) The receiver of a ABORT MUST accept the packet
	 *    if the Verification Tag field of the packet matches its own tag
	 *    and the T bit is not set
	 *    OR
	 *    it is set to its peer's tag and the T bit is set in the Chunk
	 *    Flags.
	 *    Otherwise, the receiver MUST silently discard the packet
	 *    and take no further action.
	 *
	 * C) The receiver of a SHUTDOWN COMPLETE shall accept the packet
	 *    if the Verification Tag field of the packet matches its own tag
	 *    and the T bit is not set
	 *    OR
	 *    it is set to its peer's tag and the T bit is set in the Chunk
	 *    Flags.
	 *    Otherwise, the receiver MUST silently discard the packet
	 *    and take no further action.  An endpoint MUST ignore the
	 *    SHUTDOWN COMPLETE if it is not in the SHUTDOWN-ACK-SENT state.
	 */
        if ((!sctp_test_T_bit(chunk) &&
             (ntohl(chunk->sctp_hdr->vtag) == asoc->c.my_vtag)) ||
	    (sctp_test_T_bit(chunk) && asoc->c.peer_vtag &&
	     (ntohl(chunk->sctp_hdr->vtag) == asoc->c.peer_vtag))) {
                return 1;
	}

	return 0;
}

#endif /* __sctp_sm_h__ */
