/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These are the state tables for the SCTP state machine.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/skbuff.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

static const sctp_sm_table_entry_t
primitive_event_table[SCTP_NUM_PRIMITIVE_TYPES][SCTP_STATE_NUM_STATES];
static const sctp_sm_table_entry_t
other_event_table[SCTP_NUM_OTHER_TYPES][SCTP_STATE_NUM_STATES];
static const sctp_sm_table_entry_t
timeout_event_table[SCTP_NUM_TIMEOUT_TYPES][SCTP_STATE_NUM_STATES];

static const sctp_sm_table_entry_t *sctp_chunk_event_lookup(sctp_cid_t cid,
							    sctp_state_t state);


static const sctp_sm_table_entry_t bug = {
	.fn = sctp_sf_bug,
	.name = "sctp_sf_bug"
};

#define DO_LOOKUP(_max, _type, _table) \
	if ((event_subtype._type > (_max))) { \
		printk(KERN_WARNING \
		       "sctp table %p possible attack:" \
		       " event %d exceeds max %d\n", \
		       _table, event_subtype._type, _max); \
		return &bug; \
	} \
	return &_table[event_subtype._type][(int)state];

const sctp_sm_table_entry_t *sctp_sm_lookup_event(sctp_event_t event_type,
						  sctp_state_t state,
						  sctp_subtype_t event_subtype)
{
	switch (event_type) {
	case SCTP_EVENT_T_CHUNK:
		return sctp_chunk_event_lookup(event_subtype.chunk, state);
		break;
	case SCTP_EVENT_T_TIMEOUT:
		DO_LOOKUP(SCTP_EVENT_TIMEOUT_MAX, timeout,
			  timeout_event_table);
		break;

	case SCTP_EVENT_T_OTHER:
		DO_LOOKUP(SCTP_EVENT_OTHER_MAX, other, other_event_table);
		break;

	case SCTP_EVENT_T_PRIMITIVE:
		DO_LOOKUP(SCTP_EVENT_PRIMITIVE_MAX, primitive,
			  primitive_event_table);
		break;

	default:
		/* Yikes!  We got an illegal event type.  */
		return &bug;
	}
}

#define TYPE_SCTP_FUNC(func) {.fn = func, .name = #func}

#define TYPE_SCTP_DATA { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_data_6_2), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_data_6_2), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_data_fast_4_4), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_DATA */

#define TYPE_SCTP_INIT { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_1B_init), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_1_siminit), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_1_siminit), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_2_dupinit), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_2_dupinit), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_2_dupinit), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_2_dupinit), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_reshutack), \
} /* TYPE_SCTP_INIT */

#define TYPE_SCTP_INIT_ACK { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_3_initack), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_1C_ack), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_INIT_ACK */

#define TYPE_SCTP_SACK { \
	/*  SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_sack_6_2), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_sack_6_2), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_sack_6_2), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_sack_6_2), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_SACK */

#define TYPE_SCTP_HEARTBEAT { \
	/*  SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_beat_8_3), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_beat_8_3), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_beat_8_3), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_beat_8_3), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_beat_8_3), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	/* This should not happen, but we are nice.  */ \
	TYPE_SCTP_FUNC(sctp_sf_beat_8_3), \
} /* TYPE_SCTP_HEARTBEAT */

#define TYPE_SCTP_HEARTBEAT_ACK { \
	/*  SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_violation), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_backbeat_8_3), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_backbeat_8_3), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_backbeat_8_3), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_backbeat_8_3), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_HEARTBEAT_ACK */

#define TYPE_SCTP_ABORT { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_pdiscard), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_wait_abort), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_echoed_abort), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_1_abort), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_shutdown_pending_abort), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_shutdown_sent_abort), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_1_abort), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_shutdown_ack_sent_abort), \
} /* TYPE_SCTP_ABORT */

#define TYPE_SCTP_SHUTDOWN { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_shutdown), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_shutdown_ack), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_SHUTDOWN */

#define TYPE_SCTP_SHUTDOWN_ACK { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_8_5_1_E_sa), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_8_5_1_E_sa), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_violation), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_violation), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_final), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_violation), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_final), \
} /* TYPE_SCTP_SHUTDOWN_ACK */

#define TYPE_SCTP_ERROR { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_echoed_err), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_operr_notify), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_operr_notify), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_operr_notify), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_ERROR */

#define TYPE_SCTP_COOKIE_ECHO { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_1D_ce), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_4_dupcook), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_4_dupcook), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_4_dupcook), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_4_dupcook), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_4_dupcook), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_4_dupcook), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_2_4_dupcook), \
} /* TYPE_SCTP_COOKIE_ECHO */

#define TYPE_SCTP_COOKIE_ACK { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_5_1E_ca), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_COOKIE_ACK */

#define TYPE_SCTP_ECN_ECNE { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecne), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecne), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecne), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecne), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecne), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_ECN_ECNE */

#define TYPE_SCTP_ECN_CWR { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecn_cwr), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecn_cwr), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_ecn_cwr), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_ECN_CWR */

#define TYPE_SCTP_SHUTDOWN_COMPLETE { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_4_C), \
} /* TYPE_SCTP_SHUTDOWN_COMPLETE */

/* The primary index for this table is the chunk type.
 * The secondary index for this table is the state.
 *
 * For base protocol (RFC 2960).
 */
static const sctp_sm_table_entry_t chunk_event_table[SCTP_NUM_BASE_CHUNK_TYPES][SCTP_STATE_NUM_STATES] = {
	TYPE_SCTP_DATA,
	TYPE_SCTP_INIT,
	TYPE_SCTP_INIT_ACK,
	TYPE_SCTP_SACK,
	TYPE_SCTP_HEARTBEAT,
	TYPE_SCTP_HEARTBEAT_ACK,
	TYPE_SCTP_ABORT,
	TYPE_SCTP_SHUTDOWN,
	TYPE_SCTP_SHUTDOWN_ACK,
	TYPE_SCTP_ERROR,
	TYPE_SCTP_COOKIE_ECHO,
	TYPE_SCTP_COOKIE_ACK,
	TYPE_SCTP_ECN_ECNE,
	TYPE_SCTP_ECN_CWR,
	TYPE_SCTP_SHUTDOWN_COMPLETE,
}; /* state_fn_t chunk_event_table[][] */

#define TYPE_SCTP_ASCONF { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_asconf), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_ASCONF */

#define TYPE_SCTP_ASCONF_ACK { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_asconf_ack), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_ASCONF_ACK */

/* The primary index for this table is the chunk type.
 * The secondary index for this table is the state.
 */
static const sctp_sm_table_entry_t addip_chunk_event_table[SCTP_NUM_ADDIP_CHUNK_TYPES][SCTP_STATE_NUM_STATES] = {
	TYPE_SCTP_ASCONF,
	TYPE_SCTP_ASCONF_ACK,
}; /*state_fn_t addip_chunk_event_table[][] */

#define TYPE_SCTP_FWD_TSN { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_ootb), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_fwd_tsn), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_fwd_tsn), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_eat_fwd_tsn_fast), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_discard_chunk), \
} /* TYPE_SCTP_FWD_TSN */

/* The primary index for this table is the chunk type.
 * The secondary index for this table is the state.
 */
static const sctp_sm_table_entry_t prsctp_chunk_event_table[SCTP_NUM_PRSCTP_CHUNK_TYPES][SCTP_STATE_NUM_STATES] = {
	TYPE_SCTP_FWD_TSN,
}; /*state_fn_t prsctp_chunk_event_table[][] */

static const sctp_sm_table_entry_t
chunk_event_table_unknown[SCTP_STATE_NUM_STATES] = {
	/* SCTP_STATE_EMPTY */
	TYPE_SCTP_FUNC(sctp_sf_ootb),
	/* SCTP_STATE_CLOSED */
	TYPE_SCTP_FUNC(sctp_sf_tabort_8_4_8),
	/* SCTP_STATE_COOKIE_WAIT */
	TYPE_SCTP_FUNC(sctp_sf_unk_chunk),
	/* SCTP_STATE_COOKIE_ECHOED */
	TYPE_SCTP_FUNC(sctp_sf_unk_chunk),
	/* SCTP_STATE_ESTABLISHED */
	TYPE_SCTP_FUNC(sctp_sf_unk_chunk),
	/* SCTP_STATE_SHUTDOWN_PENDING */
	TYPE_SCTP_FUNC(sctp_sf_unk_chunk),
	/* SCTP_STATE_SHUTDOWN_SENT */
	TYPE_SCTP_FUNC(sctp_sf_unk_chunk),
	/* SCTP_STATE_SHUTDOWN_RECEIVED */
	TYPE_SCTP_FUNC(sctp_sf_unk_chunk),
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */
	TYPE_SCTP_FUNC(sctp_sf_unk_chunk),
};	/* chunk unknown */


#define TYPE_SCTP_PRIMITIVE_ASSOCIATE  { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_asoc), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_not_impl), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_not_impl), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_not_impl), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_not_impl), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_not_impl), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_not_impl), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_not_impl), \
} /* TYPE_SCTP_PRIMITIVE_ASSOCIATE */

#define TYPE_SCTP_PRIMITIVE_SHUTDOWN  { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_closed), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_wait_prm_shutdown), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_echoed_prm_shutdown),\
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_prm_shutdown), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_primitive), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_primitive), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_primitive), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_primitive), \
} /* TYPE_SCTP_PRIMITIVE_SHUTDOWN */

#define TYPE_SCTP_PRIMITIVE_ABORT  { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_closed), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_wait_prm_abort), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_echoed_prm_abort), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_1_prm_abort), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_shutdown_pending_prm_abort), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_shutdown_sent_prm_abort), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_1_prm_abort), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_shutdown_ack_sent_prm_abort), \
} /* TYPE_SCTP_PRIMITIVE_ABORT */

#define TYPE_SCTP_PRIMITIVE_SEND  { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_closed), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_send), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_send), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_send), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
} /* TYPE_SCTP_PRIMITIVE_SEND */

#define TYPE_SCTP_PRIMITIVE_REQUESTHEARTBEAT  { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_closed), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_requestheartbeat),          \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_requestheartbeat),          \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_requestheartbeat),          \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_requestheartbeat),          \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_requestheartbeat),          \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_requestheartbeat),          \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_requestheartbeat),          \
} /* TYPE_SCTP_PRIMITIVE_REQUESTHEARTBEAT */

#define TYPE_SCTP_PRIMITIVE_ASCONF { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_closed), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_error_closed), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_closed), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_prm_asconf), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_error_shutdown), \
} /* TYPE_SCTP_PRIMITIVE_REQUESTHEARTBEAT */

/* The primary index for this table is the primitive type.
 * The secondary index for this table is the state.
 */
static const sctp_sm_table_entry_t primitive_event_table[SCTP_NUM_PRIMITIVE_TYPES][SCTP_STATE_NUM_STATES] = {
	TYPE_SCTP_PRIMITIVE_ASSOCIATE,
	TYPE_SCTP_PRIMITIVE_SHUTDOWN,
	TYPE_SCTP_PRIMITIVE_ABORT,
	TYPE_SCTP_PRIMITIVE_SEND,
	TYPE_SCTP_PRIMITIVE_REQUESTHEARTBEAT,
	TYPE_SCTP_PRIMITIVE_ASCONF,
};

#define TYPE_SCTP_OTHER_NO_PENDING_TSN  { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_start_shutdown), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_9_2_shutdown_ack), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
}

#define TYPE_SCTP_OTHER_ICMP_PROTO_UNREACH  { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_cookie_wait_icmp_abort), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_ignore_other), \
}

static const sctp_sm_table_entry_t other_event_table[SCTP_NUM_OTHER_TYPES][SCTP_STATE_NUM_STATES] = {
	TYPE_SCTP_OTHER_NO_PENDING_TSN,
	TYPE_SCTP_OTHER_ICMP_PROTO_UNREACH,
};

#define TYPE_SCTP_EVENT_TIMEOUT_NONE { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_T1_COOKIE { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_t1_cookie_timer_expire), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_T1_INIT { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_t1_init_timer_expire), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_T2_SHUTDOWN { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_t2_timer_expire), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_t2_timer_expire), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_T3_RTX { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_6_3_3_rtx), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_6_3_3_rtx), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_6_3_3_rtx), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_6_3_3_rtx), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_T4_RTO { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_t4_timer_expire), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_t5_timer_expire), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_t5_timer_expire), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_HEARTBEAT { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_sendbeat_8_3), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_sendbeat_8_3), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_sendbeat_8_3), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_SACK { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_bug), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_do_6_2_sack), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_do_6_2_sack), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_do_6_2_sack), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

#define TYPE_SCTP_EVENT_TIMEOUT_AUTOCLOSE { \
	/* SCTP_STATE_EMPTY */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_CLOSED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_WAIT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_COOKIE_ECHOED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_ESTABLISHED */ \
	TYPE_SCTP_FUNC(sctp_sf_autoclose_timer_expire), \
	/* SCTP_STATE_SHUTDOWN_PENDING */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_RECEIVED */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
	/* SCTP_STATE_SHUTDOWN_ACK_SENT */ \
	TYPE_SCTP_FUNC(sctp_sf_timer_ignore), \
}

static const sctp_sm_table_entry_t timeout_event_table[SCTP_NUM_TIMEOUT_TYPES][SCTP_STATE_NUM_STATES] = {
	TYPE_SCTP_EVENT_TIMEOUT_NONE,
	TYPE_SCTP_EVENT_TIMEOUT_T1_COOKIE,
	TYPE_SCTP_EVENT_TIMEOUT_T1_INIT,
	TYPE_SCTP_EVENT_TIMEOUT_T2_SHUTDOWN,
	TYPE_SCTP_EVENT_TIMEOUT_T3_RTX,
	TYPE_SCTP_EVENT_TIMEOUT_T4_RTO,
	TYPE_SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD,
	TYPE_SCTP_EVENT_TIMEOUT_HEARTBEAT,
	TYPE_SCTP_EVENT_TIMEOUT_SACK,
	TYPE_SCTP_EVENT_TIMEOUT_AUTOCLOSE,
};

static const sctp_sm_table_entry_t *sctp_chunk_event_lookup(sctp_cid_t cid,
							    sctp_state_t state)
{
	if (state > SCTP_STATE_MAX)
		return &bug;

	if (cid >= 0 && cid <= SCTP_CID_BASE_MAX)
		return &chunk_event_table[cid][state];

	if (sctp_prsctp_enable) {
		if (cid == SCTP_CID_FWD_TSN)
			return &prsctp_chunk_event_table[0][state];
	}

	if (sctp_addip_enable) {
		if (cid == SCTP_CID_ASCONF)
			return &addip_chunk_event_table[0][state];

		if (cid == SCTP_CID_ASCONF_ACK)
			return &addip_chunk_event_table[1][state];
	}

	return &chunk_event_table_unknown[state];
}
