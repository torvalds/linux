/* SCTP kernel Implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (C) 1999-2001 Cisco, Motorola
 *
 * This file is part of the SCTP kernel implementation
 *
 * These are the definitions needed for the command object.
 *
 * This SCTP implementation  is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation  is distributed in the hope that it
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
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *   La Monte H.P. Yarroll <piggy@acm.org>
 *   Karl Knutson <karl@athena.chicago.il.us>
 *   Ardelle Fan <ardelle.fan@intel.com>
 *   Sridhar Samudrala <sri@us.ibm.com>
 */

#ifndef __net_sctp_command_h__
#define __net_sctp_command_h__

#include <net/sctp/constants.h>
#include <net/sctp/structs.h>


typedef enum {
	SCTP_CMD_NOP = 0,	/* Do nothing. */
	SCTP_CMD_NEW_ASOC,	/* Register a new association.  */
	SCTP_CMD_DELETE_TCB,	/* Delete the current association. */
	SCTP_CMD_NEW_STATE,	/* Enter a new state.  */
	SCTP_CMD_REPORT_TSN,	/* Record the arrival of a TSN.  */
	SCTP_CMD_GEN_SACK,	/* Send a Selective ACK (maybe).  */
	SCTP_CMD_PROCESS_SACK,	/* Process an inbound SACK.  */
	SCTP_CMD_GEN_INIT_ACK,	/* Generate an INIT ACK chunk.  */
	SCTP_CMD_PEER_INIT,	/* Process a INIT from the peer.  */
	SCTP_CMD_GEN_COOKIE_ECHO, /* Generate a COOKIE ECHO chunk. */
	SCTP_CMD_CHUNK_ULP,	/* Send a chunk to the sockets layer.  */
	SCTP_CMD_EVENT_ULP,	/* Send a notification to the sockets layer. */
	SCTP_CMD_REPLY,		/* Send a chunk to our peer.  */
	SCTP_CMD_SEND_PKT,	/* Send a full packet to our peer.  */
	SCTP_CMD_RETRAN,	/* Mark a transport for retransmission.  */
	SCTP_CMD_ECN_CE,        /* Do delayed CE processing.   */
	SCTP_CMD_ECN_ECNE,	/* Do delayed ECNE processing. */
	SCTP_CMD_ECN_CWR,	/* Do delayed CWR processing.  */
	SCTP_CMD_TIMER_START,	/* Start a timer.  */
	SCTP_CMD_TIMER_START_ONCE, /* Start a timer once */
	SCTP_CMD_TIMER_RESTART,	/* Restart a timer. */
	SCTP_CMD_TIMER_STOP,	/* Stop a timer. */
	SCTP_CMD_INIT_CHOOSE_TRANSPORT, /* Choose transport for an INIT. */
	SCTP_CMD_INIT_COUNTER_RESET, /* Reset init counter. */
	SCTP_CMD_INIT_COUNTER_INC,   /* Increment init counter. */
	SCTP_CMD_INIT_RESTART,  /* High level, do init timer work. */
	SCTP_CMD_COOKIEECHO_RESTART,  /* High level, do cookie-echo timer work. */
	SCTP_CMD_INIT_FAILED,   /* High level, do init failure work. */
	SCTP_CMD_REPORT_DUP,	/* Report a duplicate TSN.  */
	SCTP_CMD_STRIKE,	/* Mark a strike against a transport.  */
	SCTP_CMD_HB_TIMERS_START,    /* Start the heartbeat timers. */
	SCTP_CMD_HB_TIMER_UPDATE,    /* Update a heartbeat timers.  */
	SCTP_CMD_HB_TIMERS_STOP,     /* Stop the heartbeat timers.  */
	SCTP_CMD_TRANSPORT_HB_SENT,  /* Reset the status of a transport. */
	SCTP_CMD_TRANSPORT_IDLE,     /* Do manipulations on idle transport */
	SCTP_CMD_TRANSPORT_ON,       /* Mark the transport as active. */
	SCTP_CMD_REPORT_ERROR,   /* Pass this error back out of the sm. */
	SCTP_CMD_REPORT_BAD_TAG, /* Verification tags didn't match. */
	SCTP_CMD_PROCESS_CTSN,   /* Sideeffect from shutdown. */
	SCTP_CMD_ASSOC_FAILED,	 /* Handle association failure. */
	SCTP_CMD_DISCARD_PACKET, /* Discard the whole packet. */
	SCTP_CMD_GEN_SHUTDOWN,   /* Generate a SHUTDOWN chunk. */
	SCTP_CMD_UPDATE_ASSOC,   /* Update association information. */
	SCTP_CMD_PURGE_OUTQUEUE, /* Purge all data waiting to be sent. */
	SCTP_CMD_SETUP_T2,       /* Hi-level, setup T2-shutdown parms.  */
	SCTP_CMD_RTO_PENDING,	 /* Set transport's rto_pending. */
	SCTP_CMD_PART_DELIVER,	 /* Partial data delivery considerations. */
	SCTP_CMD_RENEGE,         /* Renege data on an association. */
	SCTP_CMD_SETUP_T4,	 /* ADDIP, setup T4 RTO timer parms. */
	SCTP_CMD_PROCESS_OPERR,  /* Process an ERROR chunk. */
	SCTP_CMD_REPORT_FWDTSN,	 /* Report new cumulative TSN Ack. */
	SCTP_CMD_PROCESS_FWDTSN, /* Skips were reported, so process further. */
	SCTP_CMD_CLEAR_INIT_TAG, /* Clears association peer's inittag. */
	SCTP_CMD_DEL_NON_PRIMARY, /* Removes non-primary peer transports. */
	SCTP_CMD_T3_RTX_TIMERS_STOP, /* Stops T3-rtx pending timers */
	SCTP_CMD_FORCE_PRIM_RETRAN,  /* Forces retrans. over primary path. */
	SCTP_CMD_SET_SK_ERR,	 /* Set sk_err */
	SCTP_CMD_ASSOC_CHANGE,	 /* generate and send assoc_change event */
	SCTP_CMD_ADAPTATION_IND, /* generate and send adaptation event */
	SCTP_CMD_ASSOC_SHKEY,    /* generate the association shared keys */
	SCTP_CMD_T1_RETRAN,	 /* Mark for retransmission after T1 timeout  */
	SCTP_CMD_UPDATE_INITTAG, /* Update peer inittag */
	SCTP_CMD_SEND_MSG,	 /* Send the whole use message */
	SCTP_CMD_SEND_NEXT_ASCONF, /* Send the next ASCONF after ACK */
	SCTP_CMD_PURGE_ASCONF_QUEUE, /* Purge all asconf queues.*/
	SCTP_CMD_SET_ASOC,	 /* Restore association context */
	SCTP_CMD_LAST
} sctp_verb_t;

/* How many commands can you put in an sctp_cmd_seq_t?
 * This is a rather arbitrary number, ideally derived from a careful
 * analysis of the state functions, but in reality just taken from
 * thin air in the hopes othat we don't trigger a kernel panic.
 */
#define SCTP_MAX_NUM_COMMANDS 14

typedef union {
	__s32 i32;
	__u32 u32;
	__be32 be32;
	__u16 u16;
	__u8 u8;
	int error;
	__be16 err;
	sctp_state_t state;
	sctp_event_timeout_t to;
	struct sctp_chunk *chunk;
	struct sctp_association *asoc;
	struct sctp_transport *transport;
	struct sctp_bind_addr *bp;
	sctp_init_chunk_t *init;
	struct sctp_ulpevent *ulpevent;
	struct sctp_packet *packet;
	sctp_sackhdr_t *sackh;
	struct sctp_datamsg *msg;
} sctp_arg_t;

/* We are simulating ML type constructors here.
 *
 * SCTP_ARG_CONSTRUCTOR(NAME, TYPE, ELT) builds a function called
 * SCTP_NAME() which takes an argument of type TYPE and returns an
 * sctp_arg_t.  It does this by inserting the sole argument into the
 * ELT union element of a local sctp_arg_t.
 *
 * E.g., SCTP_ARG_CONSTRUCTOR(I32, __s32, i32) builds SCTP_I32(arg),
 * which takes an __s32 and returns a sctp_arg_t containing the
 * __s32.  So, after foo = SCTP_I32(arg), foo.i32 == arg.
 */

#define SCTP_ARG_CONSTRUCTOR(name, type, elt) \
static inline sctp_arg_t	\
SCTP_## name (type arg)		\
{ sctp_arg_t retval;\
  memset(&retval, 0, sizeof(sctp_arg_t));\
  retval.elt = arg;\
  return retval;\
}

SCTP_ARG_CONSTRUCTOR(I32,	__s32, i32)
SCTP_ARG_CONSTRUCTOR(U32,	__u32, u32)
SCTP_ARG_CONSTRUCTOR(BE32,	__be32, be32)
SCTP_ARG_CONSTRUCTOR(U16,	__u16, u16)
SCTP_ARG_CONSTRUCTOR(U8,	__u8, u8)
SCTP_ARG_CONSTRUCTOR(ERROR,     int, error)
SCTP_ARG_CONSTRUCTOR(PERR,      __be16, err)	/* protocol error */
SCTP_ARG_CONSTRUCTOR(STATE,	sctp_state_t, state)
SCTP_ARG_CONSTRUCTOR(TO,	sctp_event_timeout_t, to)
SCTP_ARG_CONSTRUCTOR(CHUNK,	struct sctp_chunk *, chunk)
SCTP_ARG_CONSTRUCTOR(ASOC,	struct sctp_association *, asoc)
SCTP_ARG_CONSTRUCTOR(TRANSPORT,	struct sctp_transport *, transport)
SCTP_ARG_CONSTRUCTOR(BA,	struct sctp_bind_addr *, bp)
SCTP_ARG_CONSTRUCTOR(PEER_INIT,	sctp_init_chunk_t *, init)
SCTP_ARG_CONSTRUCTOR(ULPEVENT,  struct sctp_ulpevent *, ulpevent)
SCTP_ARG_CONSTRUCTOR(PACKET,	struct sctp_packet *, packet)
SCTP_ARG_CONSTRUCTOR(SACKH,	sctp_sackhdr_t *, sackh)
SCTP_ARG_CONSTRUCTOR(DATAMSG,	struct sctp_datamsg *, msg)

static inline sctp_arg_t SCTP_FORCE(void)
{
	return SCTP_I32(1);
}

static inline sctp_arg_t SCTP_NOFORCE(void)
{
	return SCTP_I32(0);
}

static inline sctp_arg_t SCTP_NULL(void)
{
	sctp_arg_t retval;
	memset(&retval, 0, sizeof(sctp_arg_t));
	return retval;
}

typedef struct {
	sctp_arg_t obj;
	sctp_verb_t verb;
} sctp_cmd_t;

typedef struct {
	sctp_cmd_t cmds[SCTP_MAX_NUM_COMMANDS];
	__u8 next_free_slot;
	__u8 next_cmd;
} sctp_cmd_seq_t;


/* Initialize a block of memory as a command sequence.
 * Return 0 if the initialization fails.
 */
int sctp_init_cmd_seq(sctp_cmd_seq_t *seq);

/* Add a command to an sctp_cmd_seq_t.
 *
 * Use the SCTP_* constructors defined by SCTP_ARG_CONSTRUCTOR() above
 * to wrap data which goes in the obj argument.
 */
void sctp_add_cmd_sf(sctp_cmd_seq_t *seq, sctp_verb_t verb, sctp_arg_t obj);

/* Return the next command structure in an sctp_cmd_seq.
 * Return NULL at the end of the sequence.
 */
sctp_cmd_t *sctp_next_cmd(sctp_cmd_seq_t *seq);

#endif /* __net_sctp_command_h__ */

