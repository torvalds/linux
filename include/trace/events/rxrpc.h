/* AF_RXRPC tracepoints
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rxrpc

#if !defined(_TRACE_RXRPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RXRPC_H

#include <linux/tracepoint.h>

/*
 * Define enums for tracing information.
 *
 * These should all be kept sorted, making it easier to match the string
 * mapping tables further on.
 */
#ifndef __RXRPC_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __RXRPC_DECLARE_TRACE_ENUMS_ONCE_ONLY

enum rxrpc_skb_trace {
	rxrpc_skb_rx_cleaned,
	rxrpc_skb_rx_freed,
	rxrpc_skb_rx_got,
	rxrpc_skb_rx_lost,
	rxrpc_skb_rx_purged,
	rxrpc_skb_rx_received,
	rxrpc_skb_rx_rotated,
	rxrpc_skb_rx_seen,
	rxrpc_skb_tx_cleaned,
	rxrpc_skb_tx_freed,
	rxrpc_skb_tx_got,
	rxrpc_skb_tx_new,
	rxrpc_skb_tx_rotated,
	rxrpc_skb_tx_seen,
};

enum rxrpc_conn_trace {
	rxrpc_conn_got,
	rxrpc_conn_new_client,
	rxrpc_conn_new_service,
	rxrpc_conn_put_client,
	rxrpc_conn_put_service,
	rxrpc_conn_queued,
	rxrpc_conn_reap_service,
	rxrpc_conn_seen,
};

enum rxrpc_client_trace {
	rxrpc_client_activate_chans,
	rxrpc_client_alloc,
	rxrpc_client_chan_activate,
	rxrpc_client_chan_disconnect,
	rxrpc_client_chan_pass,
	rxrpc_client_chan_unstarted,
	rxrpc_client_cleanup,
	rxrpc_client_count,
	rxrpc_client_discard,
	rxrpc_client_duplicate,
	rxrpc_client_exposed,
	rxrpc_client_replace,
	rxrpc_client_to_active,
	rxrpc_client_to_culled,
	rxrpc_client_to_idle,
	rxrpc_client_to_inactive,
	rxrpc_client_to_upgrade,
	rxrpc_client_to_waiting,
	rxrpc_client_uncount,
};

enum rxrpc_call_trace {
	rxrpc_call_connected,
	rxrpc_call_error,
	rxrpc_call_got,
	rxrpc_call_got_kernel,
	rxrpc_call_got_userid,
	rxrpc_call_new_client,
	rxrpc_call_new_service,
	rxrpc_call_put,
	rxrpc_call_put_kernel,
	rxrpc_call_put_noqueue,
	rxrpc_call_put_userid,
	rxrpc_call_queued,
	rxrpc_call_queued_ref,
	rxrpc_call_release,
	rxrpc_call_seen,
};

enum rxrpc_transmit_trace {
	rxrpc_transmit_await_reply,
	rxrpc_transmit_end,
	rxrpc_transmit_queue,
	rxrpc_transmit_queue_last,
	rxrpc_transmit_rotate,
	rxrpc_transmit_rotate_last,
	rxrpc_transmit_wait,
};

enum rxrpc_receive_trace {
	rxrpc_receive_end,
	rxrpc_receive_front,
	rxrpc_receive_incoming,
	rxrpc_receive_queue,
	rxrpc_receive_queue_last,
	rxrpc_receive_rotate,
};

enum rxrpc_recvmsg_trace {
	rxrpc_recvmsg_cont,
	rxrpc_recvmsg_data_return,
	rxrpc_recvmsg_dequeue,
	rxrpc_recvmsg_enter,
	rxrpc_recvmsg_full,
	rxrpc_recvmsg_hole,
	rxrpc_recvmsg_next,
	rxrpc_recvmsg_requeue,
	rxrpc_recvmsg_return,
	rxrpc_recvmsg_terminal,
	rxrpc_recvmsg_to_be_accepted,
	rxrpc_recvmsg_wait,
};

enum rxrpc_rtt_tx_trace {
	rxrpc_rtt_tx_data,
	rxrpc_rtt_tx_ping,
};

enum rxrpc_rtt_rx_trace {
	rxrpc_rtt_rx_ping_response,
	rxrpc_rtt_rx_requested_ack,
};

enum rxrpc_timer_trace {
	rxrpc_timer_begin,
	rxrpc_timer_exp_ack,
	rxrpc_timer_exp_hard,
	rxrpc_timer_exp_idle,
	rxrpc_timer_exp_keepalive,
	rxrpc_timer_exp_lost_ack,
	rxrpc_timer_exp_normal,
	rxrpc_timer_exp_ping,
	rxrpc_timer_exp_resend,
	rxrpc_timer_expired,
	rxrpc_timer_init_for_reply,
	rxrpc_timer_init_for_send_reply,
	rxrpc_timer_restart,
	rxrpc_timer_set_for_ack,
	rxrpc_timer_set_for_hard,
	rxrpc_timer_set_for_idle,
	rxrpc_timer_set_for_keepalive,
	rxrpc_timer_set_for_lost_ack,
	rxrpc_timer_set_for_normal,
	rxrpc_timer_set_for_ping,
	rxrpc_timer_set_for_resend,
	rxrpc_timer_set_for_send,
};

enum rxrpc_propose_ack_trace {
	rxrpc_propose_ack_client_tx_end,
	rxrpc_propose_ack_input_data,
	rxrpc_propose_ack_ping_for_keepalive,
	rxrpc_propose_ack_ping_for_lost_ack,
	rxrpc_propose_ack_ping_for_lost_reply,
	rxrpc_propose_ack_ping_for_params,
	rxrpc_propose_ack_processing_op,
	rxrpc_propose_ack_respond_to_ack,
	rxrpc_propose_ack_respond_to_ping,
	rxrpc_propose_ack_retry_tx,
	rxrpc_propose_ack_rotate_rx,
	rxrpc_propose_ack_terminal_ack,
};

enum rxrpc_propose_ack_outcome {
	rxrpc_propose_ack_subsume,
	rxrpc_propose_ack_update,
	rxrpc_propose_ack_use,
};

enum rxrpc_congest_change {
	rxrpc_cong_begin_retransmission,
	rxrpc_cong_cleared_nacks,
	rxrpc_cong_new_low_nack,
	rxrpc_cong_no_change,
	rxrpc_cong_progress,
	rxrpc_cong_retransmit_again,
	rxrpc_cong_rtt_window_end,
	rxrpc_cong_saw_nack,
};

#endif /* end __RXRPC_DECLARE_TRACE_ENUMS_ONCE_ONLY */

/*
 * Declare tracing information enums and their string mappings for display.
 */
#define rxrpc_skb_traces \
	EM(rxrpc_skb_rx_cleaned,		"Rx CLN") \
	EM(rxrpc_skb_rx_freed,			"Rx FRE") \
	EM(rxrpc_skb_rx_got,			"Rx GOT") \
	EM(rxrpc_skb_rx_lost,			"Rx *L*") \
	EM(rxrpc_skb_rx_purged,			"Rx PUR") \
	EM(rxrpc_skb_rx_received,		"Rx RCV") \
	EM(rxrpc_skb_rx_rotated,		"Rx ROT") \
	EM(rxrpc_skb_rx_seen,			"Rx SEE") \
	EM(rxrpc_skb_tx_cleaned,		"Tx CLN") \
	EM(rxrpc_skb_tx_freed,			"Tx FRE") \
	EM(rxrpc_skb_tx_got,			"Tx GOT") \
	EM(rxrpc_skb_tx_new,			"Tx NEW") \
	EM(rxrpc_skb_tx_rotated,		"Tx ROT") \
	E_(rxrpc_skb_tx_seen,			"Tx SEE")

#define rxrpc_conn_traces \
	EM(rxrpc_conn_got,			"GOT") \
	EM(rxrpc_conn_new_client,		"NWc") \
	EM(rxrpc_conn_new_service,		"NWs") \
	EM(rxrpc_conn_put_client,		"PTc") \
	EM(rxrpc_conn_put_service,		"PTs") \
	EM(rxrpc_conn_queued,			"QUE") \
	EM(rxrpc_conn_reap_service,		"RPs") \
	E_(rxrpc_conn_seen,			"SEE")

#define rxrpc_client_traces \
	EM(rxrpc_client_activate_chans,		"Activa") \
	EM(rxrpc_client_alloc,			"Alloc ") \
	EM(rxrpc_client_chan_activate,		"ChActv") \
	EM(rxrpc_client_chan_disconnect,	"ChDisc") \
	EM(rxrpc_client_chan_pass,		"ChPass") \
	EM(rxrpc_client_chan_unstarted,		"ChUnst") \
	EM(rxrpc_client_cleanup,		"Clean ") \
	EM(rxrpc_client_count,			"Count ") \
	EM(rxrpc_client_discard,		"Discar") \
	EM(rxrpc_client_duplicate,		"Duplic") \
	EM(rxrpc_client_exposed,		"Expose") \
	EM(rxrpc_client_replace,		"Replac") \
	EM(rxrpc_client_to_active,		"->Actv") \
	EM(rxrpc_client_to_culled,		"->Cull") \
	EM(rxrpc_client_to_idle,		"->Idle") \
	EM(rxrpc_client_to_inactive,		"->Inac") \
	EM(rxrpc_client_to_upgrade,		"->Upgd") \
	EM(rxrpc_client_to_waiting,		"->Wait") \
	E_(rxrpc_client_uncount,		"Uncoun")

#define rxrpc_conn_cache_states \
	EM(RXRPC_CONN_CLIENT_INACTIVE,		"Inac") \
	EM(RXRPC_CONN_CLIENT_WAITING,		"Wait") \
	EM(RXRPC_CONN_CLIENT_ACTIVE,		"Actv") \
	EM(RXRPC_CONN_CLIENT_UPGRADE,		"Upgd") \
	EM(RXRPC_CONN_CLIENT_CULLED,		"Cull") \
	E_(RXRPC_CONN_CLIENT_IDLE,		"Idle") \

#define rxrpc_call_traces \
	EM(rxrpc_call_connected,		"CON") \
	EM(rxrpc_call_error,			"*E*") \
	EM(rxrpc_call_got,			"GOT") \
	EM(rxrpc_call_got_kernel,		"Gke") \
	EM(rxrpc_call_got_userid,		"Gus") \
	EM(rxrpc_call_new_client,		"NWc") \
	EM(rxrpc_call_new_service,		"NWs") \
	EM(rxrpc_call_put,			"PUT") \
	EM(rxrpc_call_put_kernel,		"Pke") \
	EM(rxrpc_call_put_noqueue,		"PNQ") \
	EM(rxrpc_call_put_userid,		"Pus") \
	EM(rxrpc_call_queued,			"QUE") \
	EM(rxrpc_call_queued_ref,		"QUR") \
	EM(rxrpc_call_release,			"RLS") \
	E_(rxrpc_call_seen,			"SEE")

#define rxrpc_transmit_traces \
	EM(rxrpc_transmit_await_reply,		"AWR") \
	EM(rxrpc_transmit_end,			"END") \
	EM(rxrpc_transmit_queue,		"QUE") \
	EM(rxrpc_transmit_queue_last,		"QLS") \
	EM(rxrpc_transmit_rotate,		"ROT") \
	EM(rxrpc_transmit_rotate_last,		"RLS") \
	E_(rxrpc_transmit_wait,			"WAI")

#define rxrpc_receive_traces \
	EM(rxrpc_receive_end,			"END") \
	EM(rxrpc_receive_front,			"FRN") \
	EM(rxrpc_receive_incoming,		"INC") \
	EM(rxrpc_receive_queue,			"QUE") \
	EM(rxrpc_receive_queue_last,		"QLS") \
	E_(rxrpc_receive_rotate,		"ROT")

#define rxrpc_recvmsg_traces \
	EM(rxrpc_recvmsg_cont,			"CONT") \
	EM(rxrpc_recvmsg_data_return,		"DATA") \
	EM(rxrpc_recvmsg_dequeue,		"DEQU") \
	EM(rxrpc_recvmsg_enter,			"ENTR") \
	EM(rxrpc_recvmsg_full,			"FULL") \
	EM(rxrpc_recvmsg_hole,			"HOLE") \
	EM(rxrpc_recvmsg_next,			"NEXT") \
	EM(rxrpc_recvmsg_requeue,		"REQU") \
	EM(rxrpc_recvmsg_return,		"RETN") \
	EM(rxrpc_recvmsg_terminal,		"TERM") \
	EM(rxrpc_recvmsg_to_be_accepted,	"TBAC") \
	E_(rxrpc_recvmsg_wait,			"WAIT")

#define rxrpc_rtt_tx_traces \
	EM(rxrpc_rtt_tx_data,			"DATA") \
	E_(rxrpc_rtt_tx_ping,			"PING")

#define rxrpc_rtt_rx_traces \
	EM(rxrpc_rtt_rx_ping_response,		"PONG") \
	E_(rxrpc_rtt_rx_requested_ack,		"RACK")

#define rxrpc_timer_traces \
	EM(rxrpc_timer_begin,			"Begin ") \
	EM(rxrpc_timer_expired,			"*EXPR*") \
	EM(rxrpc_timer_exp_ack,			"ExpAck") \
	EM(rxrpc_timer_exp_hard,		"ExpHrd") \
	EM(rxrpc_timer_exp_idle,		"ExpIdl") \
	EM(rxrpc_timer_exp_keepalive,		"ExpKA ") \
	EM(rxrpc_timer_exp_lost_ack,		"ExpLoA") \
	EM(rxrpc_timer_exp_normal,		"ExpNml") \
	EM(rxrpc_timer_exp_ping,		"ExpPng") \
	EM(rxrpc_timer_exp_resend,		"ExpRsn") \
	EM(rxrpc_timer_init_for_reply,		"IniRpl") \
	EM(rxrpc_timer_init_for_send_reply,	"SndRpl") \
	EM(rxrpc_timer_restart,			"Restrt") \
	EM(rxrpc_timer_set_for_ack,		"SetAck") \
	EM(rxrpc_timer_set_for_hard,		"SetHrd") \
	EM(rxrpc_timer_set_for_idle,		"SetIdl") \
	EM(rxrpc_timer_set_for_keepalive,	"KeepAl") \
	EM(rxrpc_timer_set_for_lost_ack,	"SetLoA") \
	EM(rxrpc_timer_set_for_normal,		"SetNml") \
	EM(rxrpc_timer_set_for_ping,		"SetPng") \
	EM(rxrpc_timer_set_for_resend,		"SetRTx") \
	E_(rxrpc_timer_set_for_send,		"SetSnd")

#define rxrpc_propose_ack_traces \
	EM(rxrpc_propose_ack_client_tx_end,	"ClTxEnd") \
	EM(rxrpc_propose_ack_input_data,	"DataIn ") \
	EM(rxrpc_propose_ack_ping_for_keepalive, "KeepAlv") \
	EM(rxrpc_propose_ack_ping_for_lost_ack,	"LostAck") \
	EM(rxrpc_propose_ack_ping_for_lost_reply, "LostRpl") \
	EM(rxrpc_propose_ack_ping_for_params,	"Params ") \
	EM(rxrpc_propose_ack_processing_op,	"ProcOp ") \
	EM(rxrpc_propose_ack_respond_to_ack,	"Rsp2Ack") \
	EM(rxrpc_propose_ack_respond_to_ping,	"Rsp2Png") \
	EM(rxrpc_propose_ack_retry_tx,		"RetryTx") \
	EM(rxrpc_propose_ack_rotate_rx,		"RxAck  ") \
	E_(rxrpc_propose_ack_terminal_ack,	"ClTerm ")

#define rxrpc_propose_ack_outcomes \
	EM(rxrpc_propose_ack_subsume,		" Subsume") \
	EM(rxrpc_propose_ack_update,		" Update") \
	E_(rxrpc_propose_ack_use,		"")

#define rxrpc_congest_modes \
	EM(RXRPC_CALL_CONGEST_AVOIDANCE,	"CongAvoid") \
	EM(RXRPC_CALL_FAST_RETRANSMIT,		"FastReTx ") \
	EM(RXRPC_CALL_PACKET_LOSS,		"PktLoss  ") \
	E_(RXRPC_CALL_SLOW_START,		"SlowStart")

#define rxrpc_congest_changes \
	EM(rxrpc_cong_begin_retransmission,	" Retrans") \
	EM(rxrpc_cong_cleared_nacks,		" Cleared") \
	EM(rxrpc_cong_new_low_nack,		" NewLowN") \
	EM(rxrpc_cong_no_change,		"") \
	EM(rxrpc_cong_progress,			" Progres") \
	EM(rxrpc_cong_retransmit_again,		" ReTxAgn") \
	EM(rxrpc_cong_rtt_window_end,		" RttWinE") \
	E_(rxrpc_cong_saw_nack,			" SawNack")

#define rxrpc_pkts \
	EM(0,					"?00") \
	EM(RXRPC_PACKET_TYPE_DATA,		"DATA") \
	EM(RXRPC_PACKET_TYPE_ACK,		"ACK") \
	EM(RXRPC_PACKET_TYPE_BUSY,		"BUSY") \
	EM(RXRPC_PACKET_TYPE_ABORT,		"ABORT") \
	EM(RXRPC_PACKET_TYPE_ACKALL,		"ACKALL") \
	EM(RXRPC_PACKET_TYPE_CHALLENGE,		"CHALL") \
	EM(RXRPC_PACKET_TYPE_RESPONSE,		"RESP") \
	EM(RXRPC_PACKET_TYPE_DEBUG,		"DEBUG") \
	EM(9,					"?09") \
	EM(10,					"?10") \
	EM(11,					"?11") \
	EM(12,					"?12") \
	EM(RXRPC_PACKET_TYPE_VERSION,		"VERSION") \
	EM(14,					"?14") \
	E_(15,					"?15")

#define rxrpc_ack_names \
	EM(0,					"-0-") \
	EM(RXRPC_ACK_REQUESTED,			"REQ") \
	EM(RXRPC_ACK_DUPLICATE,			"DUP") \
	EM(RXRPC_ACK_OUT_OF_SEQUENCE,		"OOS") \
	EM(RXRPC_ACK_EXCEEDS_WINDOW,		"WIN") \
	EM(RXRPC_ACK_NOSPACE,			"MEM") \
	EM(RXRPC_ACK_PING,			"PNG") \
	EM(RXRPC_ACK_PING_RESPONSE,		"PNR") \
	EM(RXRPC_ACK_DELAY,			"DLY") \
	EM(RXRPC_ACK_IDLE,			"IDL") \
	E_(RXRPC_ACK__INVALID,			"-?-")

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

rxrpc_skb_traces;
rxrpc_conn_traces;
rxrpc_client_traces;
rxrpc_call_traces;
rxrpc_transmit_traces;
rxrpc_receive_traces;
rxrpc_recvmsg_traces;
rxrpc_rtt_tx_traces;
rxrpc_rtt_rx_traces;
rxrpc_timer_traces;
rxrpc_propose_ack_traces;
rxrpc_propose_ack_outcomes;
rxrpc_congest_changes;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }

TRACE_EVENT(rxrpc_conn,
	    TP_PROTO(struct rxrpc_connection *conn, enum rxrpc_conn_trace op,
		     int usage, const void *where),

	    TP_ARGS(conn, op, usage, where),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_connection *,	conn		)
		    __field(int,			op		)
		    __field(int,			usage		)
		    __field(const void *,		where		)
			     ),

	    TP_fast_assign(
		    __entry->conn = conn;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->where = where;
			   ),

	    TP_printk("C=%p %s u=%d sp=%pSR",
		      __entry->conn,
		      __print_symbolic(__entry->op, rxrpc_conn_traces),
		      __entry->usage,
		      __entry->where)
	    );

TRACE_EVENT(rxrpc_client,
	    TP_PROTO(struct rxrpc_connection *conn, int channel,
		     enum rxrpc_client_trace op),

	    TP_ARGS(conn, channel, op),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_connection *,	conn		)
		    __field(u32,			cid		)
		    __field(int,			channel		)
		    __field(int,			usage		)
		    __field(enum rxrpc_client_trace,	op		)
		    __field(enum rxrpc_conn_cache_state, cs		)
			     ),

	    TP_fast_assign(
		    __entry->conn = conn;
		    __entry->channel = channel;
		    __entry->usage = atomic_read(&conn->usage);
		    __entry->op = op;
		    __entry->cid = conn->proto.cid;
		    __entry->cs = conn->cache_state;
			   ),

	    TP_printk("C=%p h=%2d %s %s i=%08x u=%d",
		      __entry->conn,
		      __entry->channel,
		      __print_symbolic(__entry->op, rxrpc_client_traces),
		      __print_symbolic(__entry->cs, rxrpc_conn_cache_states),
		      __entry->cid,
		      __entry->usage)
	    );

TRACE_EVENT(rxrpc_call,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_call_trace op,
		     int usage, const void *where, const void *aux),

	    TP_ARGS(call, op, usage, where, aux),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(int,			op		)
		    __field(int,			usage		)
		    __field(const void *,		where		)
		    __field(const void *,		aux		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->where = where;
		    __entry->aux = aux;
			   ),

	    TP_printk("c=%p %s u=%d sp=%pSR a=%p",
		      __entry->call,
		      __print_symbolic(__entry->op, rxrpc_call_traces),
		      __entry->usage,
		      __entry->where,
		      __entry->aux)
	    );

TRACE_EVENT(rxrpc_skb,
	    TP_PROTO(struct sk_buff *skb, enum rxrpc_skb_trace op,
		     int usage, int mod_count, const void *where),

	    TP_ARGS(skb, op, usage, mod_count, where),

	    TP_STRUCT__entry(
		    __field(struct sk_buff *,		skb		)
		    __field(enum rxrpc_skb_trace,	op		)
		    __field(int,			usage		)
		    __field(int,			mod_count	)
		    __field(const void *,		where		)
			     ),

	    TP_fast_assign(
		    __entry->skb = skb;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->mod_count = mod_count;
		    __entry->where = where;
			   ),

	    TP_printk("s=%p %s u=%d m=%d p=%pSR",
		      __entry->skb,
		      __print_symbolic(__entry->op, rxrpc_skb_traces),
		      __entry->usage,
		      __entry->mod_count,
		      __entry->where)
	    );

TRACE_EVENT(rxrpc_rx_packet,
	    TP_PROTO(struct rxrpc_skb_priv *sp),

	    TP_ARGS(sp),

	    TP_STRUCT__entry(
		    __field_struct(struct rxrpc_host_header,	hdr		)
			     ),

	    TP_fast_assign(
		    memcpy(&__entry->hdr, &sp->hdr, sizeof(__entry->hdr));
			   ),

	    TP_printk("%08x:%08x:%08x:%04x %08x %08x %02x %02x %s",
		      __entry->hdr.epoch, __entry->hdr.cid,
		      __entry->hdr.callNumber, __entry->hdr.serviceId,
		      __entry->hdr.serial, __entry->hdr.seq,
		      __entry->hdr.type, __entry->hdr.flags,
		      __entry->hdr.type <= 15 ?
		      __print_symbolic(__entry->hdr.type, rxrpc_pkts) : "?UNK")
	    );

TRACE_EVENT(rxrpc_rx_done,
	    TP_PROTO(int result, int abort_code),

	    TP_ARGS(result, abort_code),

	    TP_STRUCT__entry(
		    __field(int,			result		)
		    __field(int,			abort_code	)
			     ),

	    TP_fast_assign(
		    __entry->result = result;
		    __entry->abort_code = abort_code;
			   ),

	    TP_printk("r=%d a=%d", __entry->result, __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_abort,
	    TP_PROTO(const char *why, u32 cid, u32 call_id, rxrpc_seq_t seq,
		     int abort_code, int error),

	    TP_ARGS(why, cid, call_id, seq, abort_code, error),

	    TP_STRUCT__entry(
		    __array(char,			why, 4		)
		    __field(u32,			cid		)
		    __field(u32,			call_id		)
		    __field(rxrpc_seq_t,		seq		)
		    __field(int,			abort_code	)
		    __field(int,			error		)
			     ),

	    TP_fast_assign(
		    memcpy(__entry->why, why, 4);
		    __entry->cid = cid;
		    __entry->call_id = call_id;
		    __entry->abort_code = abort_code;
		    __entry->error = error;
		    __entry->seq = seq;
			   ),

	    TP_printk("%08x:%08x s=%u a=%d e=%d %s",
		      __entry->cid, __entry->call_id, __entry->seq,
		      __entry->abort_code, __entry->error, __entry->why)
	    );

TRACE_EVENT(rxrpc_transmit,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_transmit_trace why),

	    TP_ARGS(call, why),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(enum rxrpc_transmit_trace,	why		)
		    __field(rxrpc_seq_t,		tx_hard_ack	)
		    __field(rxrpc_seq_t,		tx_top		)
		    __field(int,			tx_winsize	)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->why = why;
		    __entry->tx_hard_ack = call->tx_hard_ack;
		    __entry->tx_top = call->tx_top;
		    __entry->tx_winsize = call->tx_winsize;
			   ),

	    TP_printk("c=%p %s f=%08x n=%u/%u",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_transmit_traces),
		      __entry->tx_hard_ack + 1,
		      __entry->tx_top - __entry->tx_hard_ack,
		      __entry->tx_winsize)
	    );

TRACE_EVENT(rxrpc_rx_data,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_seq_t seq,
		     rxrpc_serial_t serial, u8 flags, u8 anno),

	    TP_ARGS(call, seq, serial, flags, anno),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_seq_t,		seq		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(u8,				flags		)
		    __field(u8,				anno		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->seq = seq;
		    __entry->serial = serial;
		    __entry->flags = flags;
		    __entry->anno = anno;
			   ),

	    TP_printk("c=%p DATA %08x q=%08x fl=%02x a=%02x",
		      __entry->call,
		      __entry->serial,
		      __entry->seq,
		      __entry->flags,
		      __entry->anno)
	    );

TRACE_EVENT(rxrpc_rx_ack,
	    TP_PROTO(struct rxrpc_call *call,
		     rxrpc_serial_t serial, rxrpc_serial_t ack_serial,
		     rxrpc_seq_t first, rxrpc_seq_t prev, u8 reason, u8 n_acks),

	    TP_ARGS(call, serial, ack_serial, first, prev, reason, n_acks),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(rxrpc_serial_t,		ack_serial	)
		    __field(rxrpc_seq_t,		first		)
		    __field(rxrpc_seq_t,		prev		)
		    __field(u8,				reason		)
		    __field(u8,				n_acks		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->serial = serial;
		    __entry->ack_serial = ack_serial;
		    __entry->first = first;
		    __entry->prev = prev;
		    __entry->reason = reason;
		    __entry->n_acks = n_acks;
			   ),

	    TP_printk("c=%p %08x %s r=%08x f=%08x p=%08x n=%u",
		      __entry->call,
		      __entry->serial,
		      __print_symbolic(__entry->reason, rxrpc_ack_names),
		      __entry->ack_serial,
		      __entry->first,
		      __entry->prev,
		      __entry->n_acks)
	    );

TRACE_EVENT(rxrpc_rx_abort,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_serial_t serial,
		     u32 abort_code),

	    TP_ARGS(call, serial, abort_code),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(u32,			abort_code	)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->serial = serial;
		    __entry->abort_code = abort_code;
			   ),

	    TP_printk("c=%p ABORT %08x ac=%d",
		      __entry->call,
		      __entry->serial,
		      __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_rx_rwind_change,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_serial_t serial,
		     u32 rwind, bool wake),

	    TP_ARGS(call, serial, rwind, wake),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(u32,			rwind		)
		    __field(bool,			wake		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->serial = serial;
		    __entry->rwind = rwind;
		    __entry->wake = wake;
			   ),

	    TP_printk("c=%p %08x rw=%u%s",
		      __entry->call,
		      __entry->serial,
		      __entry->rwind,
		      __entry->wake ? " wake" : "")
	    );

TRACE_EVENT(rxrpc_tx_data,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_seq_t seq,
		     rxrpc_serial_t serial, u8 flags, bool retrans, bool lose),

	    TP_ARGS(call, seq, serial, flags, retrans, lose),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_seq_t,		seq		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(u8,				flags		)
		    __field(bool,			retrans		)
		    __field(bool,			lose		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->seq = seq;
		    __entry->serial = serial;
		    __entry->flags = flags;
		    __entry->retrans = retrans;
		    __entry->lose = lose;
			   ),

	    TP_printk("c=%p DATA %08x q=%08x fl=%02x%s%s",
		      __entry->call,
		      __entry->serial,
		      __entry->seq,
		      __entry->flags,
		      __entry->retrans ? " *RETRANS*" : "",
		      __entry->lose ? " *LOSE*" : "")
	    );

TRACE_EVENT(rxrpc_tx_ack,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_serial_t serial,
		     rxrpc_seq_t ack_first, rxrpc_serial_t ack_serial,
		     u8 reason, u8 n_acks),

	    TP_ARGS(call, serial, ack_first, ack_serial, reason, n_acks),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(rxrpc_seq_t,		ack_first	)
		    __field(rxrpc_serial_t,		ack_serial	)
		    __field(u8,				reason		)
		    __field(u8,				n_acks		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->serial = serial;
		    __entry->ack_first = ack_first;
		    __entry->ack_serial = ack_serial;
		    __entry->reason = reason;
		    __entry->n_acks = n_acks;
			   ),

	    TP_printk(" c=%p ACK  %08x %s f=%08x r=%08x n=%u",
		      __entry->call,
		      __entry->serial,
		      __print_symbolic(__entry->reason, rxrpc_ack_names),
		      __entry->ack_first,
		      __entry->ack_serial,
		      __entry->n_acks)
	    );

TRACE_EVENT(rxrpc_receive,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_receive_trace why,
		     rxrpc_serial_t serial, rxrpc_seq_t seq),

	    TP_ARGS(call, why, serial, seq),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(enum rxrpc_receive_trace,	why		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(rxrpc_seq_t,		seq		)
		    __field(rxrpc_seq_t,		hard_ack	)
		    __field(rxrpc_seq_t,		top		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->why = why;
		    __entry->serial = serial;
		    __entry->seq = seq;
		    __entry->hard_ack = call->rx_hard_ack;
		    __entry->top = call->rx_top;
			   ),

	    TP_printk("c=%p %s r=%08x q=%08x w=%08x-%08x",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_receive_traces),
		      __entry->serial,
		      __entry->seq,
		      __entry->hard_ack,
		      __entry->top)
	    );

TRACE_EVENT(rxrpc_recvmsg,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_recvmsg_trace why,
		     rxrpc_seq_t seq, unsigned int offset, unsigned int len,
		     int ret),

	    TP_ARGS(call, why, seq, offset, len, ret),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(enum rxrpc_recvmsg_trace,	why		)
		    __field(rxrpc_seq_t,		seq		)
		    __field(unsigned int,		offset		)
		    __field(unsigned int,		len		)
		    __field(int,			ret		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->why = why;
		    __entry->seq = seq;
		    __entry->offset = offset;
		    __entry->len = len;
		    __entry->ret = ret;
			   ),

	    TP_printk("c=%p %s q=%08x o=%u l=%u ret=%d",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_recvmsg_traces),
		      __entry->seq,
		      __entry->offset,
		      __entry->len,
		      __entry->ret)
	    );

TRACE_EVENT(rxrpc_rtt_tx,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_rtt_tx_trace why,
		     rxrpc_serial_t send_serial),

	    TP_ARGS(call, why, send_serial),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(enum rxrpc_rtt_tx_trace,	why		)
		    __field(rxrpc_serial_t,		send_serial	)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->why = why;
		    __entry->send_serial = send_serial;
			   ),

	    TP_printk("c=%p %s sr=%08x",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_rtt_tx_traces),
		      __entry->send_serial)
	    );

TRACE_EVENT(rxrpc_rtt_rx,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_rtt_rx_trace why,
		     rxrpc_serial_t send_serial, rxrpc_serial_t resp_serial,
		     s64 rtt, u8 nr, s64 avg),

	    TP_ARGS(call, why, send_serial, resp_serial, rtt, nr, avg),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(enum rxrpc_rtt_rx_trace,	why		)
		    __field(u8,				nr		)
		    __field(rxrpc_serial_t,		send_serial	)
		    __field(rxrpc_serial_t,		resp_serial	)
		    __field(s64,			rtt		)
		    __field(u64,			avg		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->why = why;
		    __entry->send_serial = send_serial;
		    __entry->resp_serial = resp_serial;
		    __entry->rtt = rtt;
		    __entry->nr = nr;
		    __entry->avg = avg;
			   ),

	    TP_printk("c=%p %s sr=%08x rr=%08x rtt=%lld nr=%u avg=%lld",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_rtt_rx_traces),
		      __entry->send_serial,
		      __entry->resp_serial,
		      __entry->rtt,
		      __entry->nr,
		      __entry->avg)
	    );

TRACE_EVENT(rxrpc_timer,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_timer_trace why,
		     unsigned long now),

	    TP_ARGS(call, why, now),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,		call		)
		    __field(enum rxrpc_timer_trace,		why		)
		    __field(long,				now		)
		    __field(long,				ack_at		)
		    __field(long,				ack_lost_at	)
		    __field(long,				resend_at	)
		    __field(long,				ping_at		)
		    __field(long,				expect_rx_by	)
		    __field(long,				expect_req_by	)
		    __field(long,				expect_term_by	)
		    __field(long,				timer		)
			     ),

	    TP_fast_assign(
		    __entry->call		= call;
		    __entry->why		= why;
		    __entry->now		= now;
		    __entry->ack_at		= call->ack_at;
		    __entry->ack_lost_at	= call->ack_lost_at;
		    __entry->resend_at		= call->resend_at;
		    __entry->expect_rx_by	= call->expect_rx_by;
		    __entry->expect_req_by	= call->expect_req_by;
		    __entry->expect_term_by	= call->expect_term_by;
		    __entry->timer		= call->timer.expires;
			   ),

	    TP_printk("c=%p %s a=%ld la=%ld r=%ld xr=%ld xq=%ld xt=%ld t=%ld",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_timer_traces),
		      __entry->ack_at - __entry->now,
		      __entry->ack_lost_at - __entry->now,
		      __entry->resend_at - __entry->now,
		      __entry->expect_rx_by - __entry->now,
		      __entry->expect_req_by - __entry->now,
		      __entry->expect_term_by - __entry->now,
		      __entry->timer - __entry->now)
	    );

TRACE_EVENT(rxrpc_rx_lose,
	    TP_PROTO(struct rxrpc_skb_priv *sp),

	    TP_ARGS(sp),

	    TP_STRUCT__entry(
		    __field_struct(struct rxrpc_host_header,	hdr		)
			     ),

	    TP_fast_assign(
		    memcpy(&__entry->hdr, &sp->hdr, sizeof(__entry->hdr));
			   ),

	    TP_printk("%08x:%08x:%08x:%04x %08x %08x %02x %02x %s *LOSE*",
		      __entry->hdr.epoch, __entry->hdr.cid,
		      __entry->hdr.callNumber, __entry->hdr.serviceId,
		      __entry->hdr.serial, __entry->hdr.seq,
		      __entry->hdr.type, __entry->hdr.flags,
		      __entry->hdr.type <= 15 ?
		      __print_symbolic(__entry->hdr.type, rxrpc_pkts) : "?UNK")
	    );

TRACE_EVENT(rxrpc_propose_ack,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_propose_ack_trace why,
		     u8 ack_reason, rxrpc_serial_t serial, bool immediate,
		     bool background, enum rxrpc_propose_ack_outcome outcome),

	    TP_ARGS(call, why, ack_reason, serial, immediate, background,
		    outcome),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,		call		)
		    __field(enum rxrpc_propose_ack_trace,	why		)
		    __field(rxrpc_serial_t,			serial		)
		    __field(u8,					ack_reason	)
		    __field(bool,				immediate	)
		    __field(bool,				background	)
		    __field(enum rxrpc_propose_ack_outcome,	outcome		)
			     ),

	    TP_fast_assign(
		    __entry->call	= call;
		    __entry->why	= why;
		    __entry->serial	= serial;
		    __entry->ack_reason	= ack_reason;
		    __entry->immediate	= immediate;
		    __entry->background	= background;
		    __entry->outcome	= outcome;
			   ),

	    TP_printk("c=%p %s %s r=%08x i=%u b=%u%s",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_propose_ack_traces),
		      __print_symbolic(__entry->ack_reason, rxrpc_ack_names),
		      __entry->serial,
		      __entry->immediate,
		      __entry->background,
		      __print_symbolic(__entry->outcome, rxrpc_propose_ack_outcomes))
	    );

TRACE_EVENT(rxrpc_retransmit,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_seq_t seq, u8 annotation,
		     s64 expiry),

	    TP_ARGS(call, seq, annotation, expiry),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_seq_t,		seq		)
		    __field(u8,				annotation	)
		    __field(s64,			expiry		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->seq = seq;
		    __entry->annotation = annotation;
		    __entry->expiry = expiry;
			   ),

	    TP_printk("c=%p q=%x a=%02x xp=%lld",
		      __entry->call,
		      __entry->seq,
		      __entry->annotation,
		      __entry->expiry)
	    );

TRACE_EVENT(rxrpc_congest,
	    TP_PROTO(struct rxrpc_call *call, struct rxrpc_ack_summary *summary,
		     rxrpc_serial_t ack_serial, enum rxrpc_congest_change change),

	    TP_ARGS(call, summary, ack_serial, change),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,		call		)
		    __field(enum rxrpc_congest_change,		change		)
		    __field(rxrpc_seq_t,			hard_ack	)
		    __field(rxrpc_seq_t,			top		)
		    __field(rxrpc_seq_t,			lowest_nak	)
		    __field(rxrpc_serial_t,			ack_serial	)
		    __field_struct(struct rxrpc_ack_summary,	sum		)
			     ),

	    TP_fast_assign(
		    __entry->call	= call;
		    __entry->change	= change;
		    __entry->hard_ack	= call->tx_hard_ack;
		    __entry->top	= call->tx_top;
		    __entry->lowest_nak	= call->acks_lowest_nak;
		    __entry->ack_serial	= ack_serial;
		    memcpy(&__entry->sum, summary, sizeof(__entry->sum));
			   ),

	    TP_printk("c=%p r=%08x %s q=%08x %s cw=%u ss=%u nr=%u,%u nw=%u,%u r=%u b=%u u=%u d=%u l=%x%s%s%s",
		      __entry->call,
		      __entry->ack_serial,
		      __print_symbolic(__entry->sum.ack_reason, rxrpc_ack_names),
		      __entry->hard_ack,
		      __print_symbolic(__entry->sum.mode, rxrpc_congest_modes),
		      __entry->sum.cwnd,
		      __entry->sum.ssthresh,
		      __entry->sum.nr_acks, __entry->sum.nr_nacks,
		      __entry->sum.nr_new_acks, __entry->sum.nr_new_nacks,
		      __entry->sum.nr_rot_new_acks,
		      __entry->top - __entry->hard_ack,
		      __entry->sum.cumulative_acks,
		      __entry->sum.dup_acks,
		      __entry->lowest_nak, __entry->sum.new_low_nack ? "!" : "",
		      __print_symbolic(__entry->change, rxrpc_congest_changes),
		      __entry->sum.retrans_timeo ? " rTxTo" : "")
	    );

TRACE_EVENT(rxrpc_disconnect_call,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(u32,			abort_code	)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->abort_code = call->abort_code;
			   ),

	    TP_printk("c=%p ab=%08x",
		      __entry->call,
		      __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_improper_term,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(u32,			abort_code	)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->abort_code = call->abort_code;
			   ),

	    TP_printk("c=%p ab=%08x",
		      __entry->call,
		      __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_rx_eproto,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_serial_t serial,
		     const char *why),

	    TP_ARGS(call, serial, why),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(rxrpc_serial_t,		serial		)
		    __field(const char *,		why		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->serial = serial;
		    __entry->why = why;
			   ),

	    TP_printk("c=%p EPROTO %08x %s",
		      __entry->call,
		      __entry->serial,
		      __entry->why)
	    );

TRACE_EVENT(rxrpc_connect_call,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(unsigned long,		user_call_ID	)
		    __field(u32,			cid		)
		    __field(u32,			call_id		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->user_call_ID = call->user_call_ID;
		    __entry->cid = call->cid;
		    __entry->call_id = call->call_id;
			   ),

	    TP_printk("c=%p u=%p %08x:%08x",
		      __entry->call,
		      (void *)__entry->user_call_ID,
		      __entry->cid,
		      __entry->call_id)
	    );

#endif /* _TRACE_RXRPC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
