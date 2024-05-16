/* SPDX-License-Identifier: GPL-2.0-or-later */
/* AF_RXRPC tracepoints
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rxrpc

#if !defined(_TRACE_RXRPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RXRPC_H

#include <linux/tracepoint.h>
#include <linux/errqueue.h>

/*
 * Declare tracing information enums and their string mappings for display.
 */
#define rxrpc_abort_reasons \
	/* AFS errors */						\
	EM(afs_abort_general_error,		"afs-error")		\
	EM(afs_abort_interrupted,		"afs-intr")		\
	EM(afs_abort_oom,			"afs-oom")		\
	EM(afs_abort_op_not_supported,		"afs-op-notsupp")	\
	EM(afs_abort_probeuuid_negative,	"afs-probeuuid-neg")	\
	EM(afs_abort_send_data_error,		"afs-send-data")	\
	EM(afs_abort_unmarshal_error,		"afs-unmarshal")	\
	/* rxperf errors */						\
	EM(rxperf_abort_general_error,		"rxperf-error")		\
	EM(rxperf_abort_oom,			"rxperf-oom")		\
	EM(rxperf_abort_op_not_supported,	"rxperf-op-notsupp")	\
	EM(rxperf_abort_unmarshal_error,	"rxperf-unmarshal")	\
	/* RxKAD security errors */					\
	EM(rxkad_abort_1_short_check,		"rxkad1-short-check")	\
	EM(rxkad_abort_1_short_data,		"rxkad1-short-data")	\
	EM(rxkad_abort_1_short_encdata,		"rxkad1-short-encdata")	\
	EM(rxkad_abort_1_short_header,		"rxkad1-short-hdr")	\
	EM(rxkad_abort_2_short_check,		"rxkad2-short-check")	\
	EM(rxkad_abort_2_short_data,		"rxkad2-short-data")	\
	EM(rxkad_abort_2_short_header,		"rxkad2-short-hdr")	\
	EM(rxkad_abort_2_short_len,		"rxkad2-short-len")	\
	EM(rxkad_abort_bad_checksum,		"rxkad2-bad-cksum")	\
	EM(rxkad_abort_chall_key_expired,	"rxkad-chall-key-exp")	\
	EM(rxkad_abort_chall_level,		"rxkad-chall-level")	\
	EM(rxkad_abort_chall_no_key,		"rxkad-chall-nokey")	\
	EM(rxkad_abort_chall_short,		"rxkad-chall-short")	\
	EM(rxkad_abort_chall_version,		"rxkad-chall-version")	\
	EM(rxkad_abort_resp_bad_callid,		"rxkad-resp-bad-callid") \
	EM(rxkad_abort_resp_bad_checksum,	"rxkad-resp-bad-cksum")	\
	EM(rxkad_abort_resp_bad_param,		"rxkad-resp-bad-param")	\
	EM(rxkad_abort_resp_call_ctr,		"rxkad-resp-call-ctr") \
	EM(rxkad_abort_resp_call_state,		"rxkad-resp-call-state") \
	EM(rxkad_abort_resp_key_expired,	"rxkad-resp-key-exp")	\
	EM(rxkad_abort_resp_key_rejected,	"rxkad-resp-key-rej")	\
	EM(rxkad_abort_resp_level,		"rxkad-resp-level")	\
	EM(rxkad_abort_resp_nokey,		"rxkad-resp-nokey")	\
	EM(rxkad_abort_resp_ooseq,		"rxkad-resp-ooseq")	\
	EM(rxkad_abort_resp_short,		"rxkad-resp-short")	\
	EM(rxkad_abort_resp_short_tkt,		"rxkad-resp-short-tkt")	\
	EM(rxkad_abort_resp_tkt_aname,		"rxkad-resp-tk-aname")	\
	EM(rxkad_abort_resp_tkt_expired,	"rxkad-resp-tk-exp")	\
	EM(rxkad_abort_resp_tkt_future,		"rxkad-resp-tk-future")	\
	EM(rxkad_abort_resp_tkt_inst,		"rxkad-resp-tk-inst")	\
	EM(rxkad_abort_resp_tkt_len,		"rxkad-resp-tk-len")	\
	EM(rxkad_abort_resp_tkt_realm,		"rxkad-resp-tk-realm")	\
	EM(rxkad_abort_resp_tkt_short,		"rxkad-resp-tk-short")	\
	EM(rxkad_abort_resp_tkt_sinst,		"rxkad-resp-tk-sinst")	\
	EM(rxkad_abort_resp_tkt_sname,		"rxkad-resp-tk-sname")	\
	EM(rxkad_abort_resp_unknown_tkt,	"rxkad-resp-unknown-tkt") \
	EM(rxkad_abort_resp_version,		"rxkad-resp-version")	\
	/* rxrpc errors */						\
	EM(rxrpc_abort_call_improper_term,	"call-improper-term")	\
	EM(rxrpc_abort_call_reset,		"call-reset")		\
	EM(rxrpc_abort_call_sendmsg,		"call-sendmsg")		\
	EM(rxrpc_abort_call_sock_release,	"call-sock-rel")	\
	EM(rxrpc_abort_call_sock_release_tba,	"call-sock-rel-tba")	\
	EM(rxrpc_abort_call_timeout,		"call-timeout")		\
	EM(rxrpc_abort_no_service_key,		"no-serv-key")		\
	EM(rxrpc_abort_nomem,			"nomem")		\
	EM(rxrpc_abort_service_not_offered,	"serv-not-offered")	\
	EM(rxrpc_abort_shut_down,		"shut-down")		\
	EM(rxrpc_abort_unsupported_security,	"unsup-sec")		\
	EM(rxrpc_badmsg_bad_abort,		"bad-abort")		\
	EM(rxrpc_badmsg_bad_jumbo,		"bad-jumbo")		\
	EM(rxrpc_badmsg_short_ack,		"short-ack")		\
	EM(rxrpc_badmsg_short_ack_trailer,	"short-ack-trailer")	\
	EM(rxrpc_badmsg_short_hdr,		"short-hdr")		\
	EM(rxrpc_badmsg_unsupported_packet,	"unsup-pkt")		\
	EM(rxrpc_badmsg_zero_call,		"zero-call")		\
	EM(rxrpc_badmsg_zero_seq,		"zero-seq")		\
	EM(rxrpc_badmsg_zero_service,		"zero-service")		\
	EM(rxrpc_eproto_ackr_outside_window,	"ackr-out-win")		\
	EM(rxrpc_eproto_ackr_sack_overflow,	"ackr-sack-over")	\
	EM(rxrpc_eproto_ackr_short_sack,	"ackr-short-sack")	\
	EM(rxrpc_eproto_ackr_zero,		"ackr-zero")		\
	EM(rxrpc_eproto_bad_upgrade,		"bad-upgrade")		\
	EM(rxrpc_eproto_data_after_last,	"data-after-last")	\
	EM(rxrpc_eproto_different_last,		"diff-last")		\
	EM(rxrpc_eproto_early_reply,		"early-reply")		\
	EM(rxrpc_eproto_improper_term,		"improper-term")	\
	EM(rxrpc_eproto_no_client_call,		"no-cl-call")		\
	EM(rxrpc_eproto_no_client_conn,		"no-cl-conn")		\
	EM(rxrpc_eproto_no_service_call,	"no-sv-call")		\
	EM(rxrpc_eproto_reupgrade,		"re-upgrade")		\
	EM(rxrpc_eproto_rxnull_challenge,	"rxnull-chall")		\
	EM(rxrpc_eproto_rxnull_response,	"rxnull-resp")		\
	EM(rxrpc_eproto_tx_rot_last,		"tx-rot-last")		\
	EM(rxrpc_eproto_unexpected_ack,		"unex-ack")		\
	EM(rxrpc_eproto_unexpected_ackall,	"unex-ackall")		\
	EM(rxrpc_eproto_unexpected_implicit_end, "unex-impl-end")	\
	EM(rxrpc_eproto_unexpected_reply,	"unex-reply")		\
	EM(rxrpc_eproto_wrong_security,		"wrong-sec")		\
	EM(rxrpc_recvmsg_excess_data,		"recvmsg-excess")	\
	EM(rxrpc_recvmsg_short_data,		"recvmsg-short")	\
	E_(rxrpc_sendmsg_late_send,		"sendmsg-late")

#define rxrpc_call_poke_traces \
	EM(rxrpc_call_poke_abort,		"Abort")	\
	EM(rxrpc_call_poke_complete,		"Compl")	\
	EM(rxrpc_call_poke_error,		"Error")	\
	EM(rxrpc_call_poke_idle,		"Idle")		\
	EM(rxrpc_call_poke_set_timeout,		"Set-timo")	\
	EM(rxrpc_call_poke_start,		"Start")	\
	EM(rxrpc_call_poke_timer,		"Timer")	\
	E_(rxrpc_call_poke_timer_now,		"Timer-now")

#define rxrpc_skb_traces \
	EM(rxrpc_skb_eaten_by_unshare,		"ETN unshare  ") \
	EM(rxrpc_skb_eaten_by_unshare_nomem,	"ETN unshar-nm") \
	EM(rxrpc_skb_get_conn_secured,		"GET conn-secd") \
	EM(rxrpc_skb_get_conn_work,		"GET conn-work") \
	EM(rxrpc_skb_get_last_nack,		"GET last-nack") \
	EM(rxrpc_skb_get_local_work,		"GET locl-work") \
	EM(rxrpc_skb_get_reject_work,		"GET rej-work ") \
	EM(rxrpc_skb_get_to_recvmsg,		"GET to-recv  ") \
	EM(rxrpc_skb_get_to_recvmsg_oos,	"GET to-recv-o") \
	EM(rxrpc_skb_new_encap_rcv,		"NEW encap-rcv") \
	EM(rxrpc_skb_new_error_report,		"NEW error-rpt") \
	EM(rxrpc_skb_new_jumbo_subpacket,	"NEW jumbo-sub") \
	EM(rxrpc_skb_new_unshared,		"NEW unshared ") \
	EM(rxrpc_skb_put_conn_secured,		"PUT conn-secd") \
	EM(rxrpc_skb_put_conn_work,		"PUT conn-work") \
	EM(rxrpc_skb_put_error_report,		"PUT error-rep") \
	EM(rxrpc_skb_put_input,			"PUT input    ") \
	EM(rxrpc_skb_put_jumbo_subpacket,	"PUT jumbo-sub") \
	EM(rxrpc_skb_put_last_nack,		"PUT last-nack") \
	EM(rxrpc_skb_put_purge,			"PUT purge    ") \
	EM(rxrpc_skb_put_rotate,		"PUT rotate   ") \
	EM(rxrpc_skb_put_unknown,		"PUT unknown  ") \
	EM(rxrpc_skb_see_conn_work,		"SEE conn-work") \
	EM(rxrpc_skb_see_recvmsg,		"SEE recvmsg  ") \
	EM(rxrpc_skb_see_reject,		"SEE reject   ") \
	EM(rxrpc_skb_see_rotate,		"SEE rotate   ") \
	E_(rxrpc_skb_see_version,		"SEE version  ")

#define rxrpc_local_traces \
	EM(rxrpc_local_free,			"FREE        ") \
	EM(rxrpc_local_get_call,		"GET call    ") \
	EM(rxrpc_local_get_client_conn,		"GET conn-cln") \
	EM(rxrpc_local_get_for_use,		"GET for-use ") \
	EM(rxrpc_local_get_peer,		"GET peer    ") \
	EM(rxrpc_local_get_prealloc_conn,	"GET conn-pre") \
	EM(rxrpc_local_new,			"NEW         ") \
	EM(rxrpc_local_put_bind,		"PUT bind    ") \
	EM(rxrpc_local_put_call,		"PUT call    ") \
	EM(rxrpc_local_put_for_use,		"PUT for-use ") \
	EM(rxrpc_local_put_kill_conn,		"PUT conn-kil") \
	EM(rxrpc_local_put_peer,		"PUT peer    ") \
	EM(rxrpc_local_put_prealloc_peer,	"PUT peer-pre") \
	EM(rxrpc_local_put_release_sock,	"PUT rel-sock") \
	EM(rxrpc_local_stop,			"STOP        ") \
	EM(rxrpc_local_stopped,			"STOPPED     ") \
	EM(rxrpc_local_unuse_bind,		"UNU bind    ") \
	EM(rxrpc_local_unuse_conn_work,		"UNU conn-wrk") \
	EM(rxrpc_local_unuse_peer_keepalive,	"UNU peer-kpa") \
	EM(rxrpc_local_unuse_release_sock,	"UNU rel-sock") \
	EM(rxrpc_local_use_conn_work,		"USE conn-wrk") \
	EM(rxrpc_local_use_lookup,		"USE lookup  ") \
	E_(rxrpc_local_use_peer_keepalive,	"USE peer-kpa")

#define rxrpc_peer_traces \
	EM(rxrpc_peer_free,			"FREE        ") \
	EM(rxrpc_peer_get_accept,		"GET accept  ") \
	EM(rxrpc_peer_get_application,		"GET app     ") \
	EM(rxrpc_peer_get_bundle,		"GET bundle  ") \
	EM(rxrpc_peer_get_call,			"GET call    ") \
	EM(rxrpc_peer_get_client_conn,		"GET cln-conn") \
	EM(rxrpc_peer_get_input,		"GET input   ") \
	EM(rxrpc_peer_get_input_error,		"GET inpt-err") \
	EM(rxrpc_peer_get_keepalive,		"GET keepaliv") \
	EM(rxrpc_peer_get_lookup_client,	"GET look-cln") \
	EM(rxrpc_peer_get_service_conn,		"GET srv-conn") \
	EM(rxrpc_peer_new_client,		"NEW client  ") \
	EM(rxrpc_peer_new_prealloc,		"NEW prealloc") \
	EM(rxrpc_peer_put_application,		"PUT app     ") \
	EM(rxrpc_peer_put_bundle,		"PUT bundle  ") \
	EM(rxrpc_peer_put_call,			"PUT call    ") \
	EM(rxrpc_peer_put_conn,			"PUT conn    ") \
	EM(rxrpc_peer_put_input,		"PUT input   ") \
	EM(rxrpc_peer_put_input_error,		"PUT inpt-err") \
	E_(rxrpc_peer_put_keepalive,		"PUT keepaliv")

#define rxrpc_bundle_traces \
	EM(rxrpc_bundle_free,			"FREE        ") \
	EM(rxrpc_bundle_get_client_call,	"GET clt-call") \
	EM(rxrpc_bundle_get_client_conn,	"GET clt-conn") \
	EM(rxrpc_bundle_get_service_conn,	"GET svc-conn") \
	EM(rxrpc_bundle_put_call,		"PUT call    ") \
	EM(rxrpc_bundle_put_conn,		"PUT conn    ") \
	EM(rxrpc_bundle_put_discard,		"PUT discard ") \
	E_(rxrpc_bundle_new,			"NEW         ")

#define rxrpc_conn_traces \
	EM(rxrpc_conn_free,			"FREE        ") \
	EM(rxrpc_conn_get_activate_call,	"GET act-call") \
	EM(rxrpc_conn_get_call_input,		"GET inp-call") \
	EM(rxrpc_conn_get_conn_input,		"GET inp-conn") \
	EM(rxrpc_conn_get_idle,			"GET idle    ") \
	EM(rxrpc_conn_get_poke_abort,		"GET pk-abort") \
	EM(rxrpc_conn_get_poke_timer,		"GET poke    ") \
	EM(rxrpc_conn_get_service_conn,		"GET svc-conn") \
	EM(rxrpc_conn_new_client,		"NEW client  ") \
	EM(rxrpc_conn_new_service,		"NEW service ") \
	EM(rxrpc_conn_put_call,			"PUT call    ") \
	EM(rxrpc_conn_put_call_input,		"PUT inp-call") \
	EM(rxrpc_conn_put_conn_input,		"PUT inp-conn") \
	EM(rxrpc_conn_put_discard_idle,		"PUT disc-idl") \
	EM(rxrpc_conn_put_local_dead,		"PUT loc-dead") \
	EM(rxrpc_conn_put_noreuse,		"PUT noreuse ") \
	EM(rxrpc_conn_put_poke,			"PUT poke    ") \
	EM(rxrpc_conn_put_service_reaped,	"PUT svc-reap") \
	EM(rxrpc_conn_put_unbundle,		"PUT unbundle") \
	EM(rxrpc_conn_put_unidle,		"PUT unidle  ") \
	EM(rxrpc_conn_put_work,			"PUT work    ") \
	EM(rxrpc_conn_queue_challenge,		"QUE chall   ") \
	EM(rxrpc_conn_queue_retry_work,		"QUE retry-wk") \
	EM(rxrpc_conn_queue_rx_work,		"QUE rx-work ") \
	EM(rxrpc_conn_see_new_service_conn,	"SEE new-svc ") \
	EM(rxrpc_conn_see_reap_service,		"SEE reap-svc") \
	E_(rxrpc_conn_see_work,			"SEE work    ")

#define rxrpc_client_traces \
	EM(rxrpc_client_activate_chans,		"Activa") \
	EM(rxrpc_client_alloc,			"Alloc ") \
	EM(rxrpc_client_chan_activate,		"ChActv") \
	EM(rxrpc_client_chan_disconnect,	"ChDisc") \
	EM(rxrpc_client_chan_pass,		"ChPass") \
	EM(rxrpc_client_cleanup,		"Clean ") \
	EM(rxrpc_client_discard,		"Discar") \
	EM(rxrpc_client_exposed,		"Expose") \
	EM(rxrpc_client_replace,		"Replac") \
	EM(rxrpc_client_queue_new_call,		"Q-Call") \
	EM(rxrpc_client_to_active,		"->Actv") \
	E_(rxrpc_client_to_idle,		"->Idle")

#define rxrpc_call_traces \
	EM(rxrpc_call_get_io_thread,		"GET iothread") \
	EM(rxrpc_call_get_input,		"GET input   ") \
	EM(rxrpc_call_get_kernel_service,	"GET krnl-srv") \
	EM(rxrpc_call_get_notify_socket,	"GET notify  ") \
	EM(rxrpc_call_get_poke,			"GET poke    ") \
	EM(rxrpc_call_get_recvmsg,		"GET recvmsg ") \
	EM(rxrpc_call_get_release_sock,		"GET rel-sock") \
	EM(rxrpc_call_get_sendmsg,		"GET sendmsg ") \
	EM(rxrpc_call_get_userid,		"GET user-id ") \
	EM(rxrpc_call_new_client,		"NEW client  ") \
	EM(rxrpc_call_new_prealloc_service,	"NEW prealloc") \
	EM(rxrpc_call_put_discard_prealloc,	"PUT disc-pre") \
	EM(rxrpc_call_put_discard_error,	"PUT disc-err") \
	EM(rxrpc_call_put_io_thread,		"PUT iothread") \
	EM(rxrpc_call_put_input,		"PUT input   ") \
	EM(rxrpc_call_put_kernel,		"PUT kernel  ") \
	EM(rxrpc_call_put_poke,			"PUT poke    ") \
	EM(rxrpc_call_put_recvmsg,		"PUT recvmsg ") \
	EM(rxrpc_call_put_release_sock,		"PUT rls-sock") \
	EM(rxrpc_call_put_release_sock_tba,	"PUT rls-sk-a") \
	EM(rxrpc_call_put_sendmsg,		"PUT sendmsg ") \
	EM(rxrpc_call_put_unnotify,		"PUT unnotify") \
	EM(rxrpc_call_put_userid_exists,	"PUT u-exists") \
	EM(rxrpc_call_put_userid,		"PUT user-id ") \
	EM(rxrpc_call_see_accept,		"SEE accept  ") \
	EM(rxrpc_call_see_activate_client,	"SEE act-clnt") \
	EM(rxrpc_call_see_connect_failed,	"SEE con-fail") \
	EM(rxrpc_call_see_connected,		"SEE connect ") \
	EM(rxrpc_call_see_disconnected,		"SEE disconn ") \
	EM(rxrpc_call_see_distribute_error,	"SEE dist-err") \
	EM(rxrpc_call_see_input,		"SEE input   ") \
	EM(rxrpc_call_see_release,		"SEE release ") \
	EM(rxrpc_call_see_userid_exists,	"SEE u-exists") \
	E_(rxrpc_call_see_zap,			"SEE zap     ")

#define rxrpc_txqueue_traces \
	EM(rxrpc_txqueue_await_reply,		"AWR") \
	EM(rxrpc_txqueue_dequeue,		"DEQ") \
	EM(rxrpc_txqueue_end,			"END") \
	EM(rxrpc_txqueue_queue,			"QUE") \
	EM(rxrpc_txqueue_queue_last,		"QLS") \
	EM(rxrpc_txqueue_rotate,		"ROT") \
	EM(rxrpc_txqueue_rotate_last,		"RLS") \
	E_(rxrpc_txqueue_wait,			"WAI")

#define rxrpc_receive_traces \
	EM(rxrpc_receive_end,			"END") \
	EM(rxrpc_receive_front,			"FRN") \
	EM(rxrpc_receive_incoming,		"INC") \
	EM(rxrpc_receive_queue,			"QUE") \
	EM(rxrpc_receive_queue_last,		"QLS") \
	EM(rxrpc_receive_queue_oos,		"QUO") \
	EM(rxrpc_receive_queue_oos_last,	"QOL") \
	EM(rxrpc_receive_oos,			"OOS") \
	EM(rxrpc_receive_oos_last,		"OSL") \
	EM(rxrpc_receive_rotate,		"ROT") \
	E_(rxrpc_receive_rotate_last,		"RLS")

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
	EM(rxrpc_recvmsg_unqueue,		"UNQU") \
	E_(rxrpc_recvmsg_wait,			"WAIT")

#define rxrpc_rtt_tx_traces \
	EM(rxrpc_rtt_tx_cancel,			"CNCE") \
	EM(rxrpc_rtt_tx_data,			"DATA") \
	EM(rxrpc_rtt_tx_no_slot,		"FULL") \
	E_(rxrpc_rtt_tx_ping,			"PING")

#define rxrpc_rtt_rx_traces \
	EM(rxrpc_rtt_rx_other_ack,		"OACK") \
	EM(rxrpc_rtt_rx_obsolete,		"OBSL") \
	EM(rxrpc_rtt_rx_lost,			"LOST") \
	EM(rxrpc_rtt_rx_ping_response,		"PONG") \
	E_(rxrpc_rtt_rx_requested_ack,		"RACK")

#define rxrpc_timer_traces \
	EM(rxrpc_timer_trace_delayed_ack,	"DelayAck ") \
	EM(rxrpc_timer_trace_expect_rx,		"ExpectRx ") \
	EM(rxrpc_timer_trace_hard,		"HardLimit") \
	EM(rxrpc_timer_trace_idle,		"IdleLimit") \
	EM(rxrpc_timer_trace_keepalive,		"KeepAlive") \
	EM(rxrpc_timer_trace_lost_ack,		"LostAck  ") \
	EM(rxrpc_timer_trace_ping,		"DelayPing") \
	EM(rxrpc_timer_trace_resend,		"Resend   ") \
	EM(rxrpc_timer_trace_resend_reset,	"ResendRst") \
	E_(rxrpc_timer_trace_resend_tx,		"ResendTx ")

#define rxrpc_propose_ack_traces \
	EM(rxrpc_propose_ack_client_tx_end,	"ClTxEnd") \
	EM(rxrpc_propose_ack_delayed_ack,	"DlydAck") \
	EM(rxrpc_propose_ack_input_data,	"DataIn ") \
	EM(rxrpc_propose_ack_input_data_hole,	"DataInH") \
	EM(rxrpc_propose_ack_ping_for_keepalive, "KeepAlv") \
	EM(rxrpc_propose_ack_ping_for_lost_ack,	"LostAck") \
	EM(rxrpc_propose_ack_ping_for_lost_reply, "LostRpl") \
	EM(rxrpc_propose_ack_ping_for_0_retrans, "0-Retrn") \
	EM(rxrpc_propose_ack_ping_for_old_rtt,	"OldRtt ") \
	EM(rxrpc_propose_ack_ping_for_params,	"Params ") \
	EM(rxrpc_propose_ack_ping_for_rtt,	"Rtt    ") \
	EM(rxrpc_propose_ack_processing_op,	"ProcOp ") \
	EM(rxrpc_propose_ack_respond_to_ack,	"Rsp2Ack") \
	EM(rxrpc_propose_ack_respond_to_ping,	"Rsp2Png") \
	EM(rxrpc_propose_ack_retry_tx,		"RetryTx") \
	EM(rxrpc_propose_ack_rotate_rx,		"RxAck  ") \
	EM(rxrpc_propose_ack_rx_idle,		"RxIdle ") \
	E_(rxrpc_propose_ack_terminal_ack,	"ClTerm ")

#define rxrpc_congest_modes \
	EM(RXRPC_CALL_CONGEST_AVOIDANCE,	"CongAvoid") \
	EM(RXRPC_CALL_FAST_RETRANSMIT,		"FastReTx ") \
	EM(RXRPC_CALL_PACKET_LOSS,		"PktLoss  ") \
	E_(RXRPC_CALL_SLOW_START,		"SlowStart")

#define rxrpc_congest_changes \
	EM(rxrpc_cong_begin_retransmission,	" Retrans") \
	EM(rxrpc_cong_cleared_nacks,		" Cleared") \
	EM(rxrpc_cong_new_low_nack,		" NewLowN") \
	EM(rxrpc_cong_no_change,		" -") \
	EM(rxrpc_cong_progress,			" Progres") \
	EM(rxrpc_cong_idle_reset,		" IdleRes") \
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

#define rxrpc_sack_traces \
	EM(rxrpc_sack_advance,			"ADV")	\
	EM(rxrpc_sack_fill,			"FIL")	\
	EM(rxrpc_sack_nack,			"NAK")	\
	EM(rxrpc_sack_none,			"---")	\
	E_(rxrpc_sack_oos,			"OOS")

#define rxrpc_completions \
	EM(RXRPC_CALL_SUCCEEDED,		"Succeeded") \
	EM(RXRPC_CALL_REMOTELY_ABORTED,		"RemoteAbort") \
	EM(RXRPC_CALL_LOCALLY_ABORTED,		"LocalAbort") \
	EM(RXRPC_CALL_LOCAL_ERROR,		"LocalError") \
	E_(RXRPC_CALL_NETWORK_ERROR,		"NetError")

#define rxrpc_tx_points \
	EM(rxrpc_tx_point_call_abort,		"CallAbort") \
	EM(rxrpc_tx_point_call_ack,		"CallAck") \
	EM(rxrpc_tx_point_call_data_frag,	"CallDataFrag") \
	EM(rxrpc_tx_point_call_data_nofrag,	"CallDataNofrag") \
	EM(rxrpc_tx_point_call_final_resend,	"CallFinalResend") \
	EM(rxrpc_tx_point_conn_abort,		"ConnAbort") \
	EM(rxrpc_tx_point_reject,		"Reject") \
	EM(rxrpc_tx_point_rxkad_challenge,	"RxkadChall") \
	EM(rxrpc_tx_point_rxkad_response,	"RxkadResp") \
	EM(rxrpc_tx_point_version_keepalive,	"VerKeepalive") \
	E_(rxrpc_tx_point_version_reply,	"VerReply")

#define rxrpc_req_ack_traces \
	EM(rxrpc_reqack_ack_lost,		"ACK-LOST  ")	\
	EM(rxrpc_reqack_already_on,		"ALREADY-ON")	\
	EM(rxrpc_reqack_more_rtt,		"MORE-RTT  ")	\
	EM(rxrpc_reqack_no_srv_last,		"NO-SRVLAST")	\
	EM(rxrpc_reqack_old_rtt,		"OLD-RTT   ")	\
	EM(rxrpc_reqack_retrans,		"RETRANS   ")	\
	EM(rxrpc_reqack_slow_start,		"SLOW-START")	\
	E_(rxrpc_reqack_small_txwin,		"SMALL-TXWN")
/* ---- Must update size of stat_why_req_ack[] if more are added! */

#define rxrpc_txbuf_traces \
	EM(rxrpc_txbuf_alloc_ack,		"ALLOC ACK  ")	\
	EM(rxrpc_txbuf_alloc_data,		"ALLOC DATA ")	\
	EM(rxrpc_txbuf_free,			"FREE       ")	\
	EM(rxrpc_txbuf_get_buffer,		"GET BUFFER ")	\
	EM(rxrpc_txbuf_get_trans,		"GET TRANS  ")	\
	EM(rxrpc_txbuf_get_retrans,		"GET RETRANS")	\
	EM(rxrpc_txbuf_put_ack_tx,		"PUT ACK TX ")	\
	EM(rxrpc_txbuf_put_cleaned,		"PUT CLEANED")	\
	EM(rxrpc_txbuf_put_nomem,		"PUT NOMEM  ")	\
	EM(rxrpc_txbuf_put_rotated,		"PUT ROTATED")	\
	EM(rxrpc_txbuf_put_send_aborted,	"PUT SEND-X ")	\
	EM(rxrpc_txbuf_put_trans,		"PUT TRANS  ")	\
	EM(rxrpc_txbuf_see_out_of_step,		"OUT-OF-STEP")	\
	EM(rxrpc_txbuf_see_send_more,		"SEE SEND+  ")	\
	E_(rxrpc_txbuf_see_unacked,		"SEE UNACKED")

/*
 * Generate enums for tracing information.
 */
#ifndef __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY

#undef EM
#undef E_
#define EM(a, b) a,
#define E_(a, b) a

enum rxrpc_abort_reason		{ rxrpc_abort_reasons } __mode(byte);
enum rxrpc_bundle_trace		{ rxrpc_bundle_traces } __mode(byte);
enum rxrpc_call_poke_trace	{ rxrpc_call_poke_traces } __mode(byte);
enum rxrpc_call_trace		{ rxrpc_call_traces } __mode(byte);
enum rxrpc_client_trace		{ rxrpc_client_traces } __mode(byte);
enum rxrpc_congest_change	{ rxrpc_congest_changes } __mode(byte);
enum rxrpc_conn_trace		{ rxrpc_conn_traces } __mode(byte);
enum rxrpc_local_trace		{ rxrpc_local_traces } __mode(byte);
enum rxrpc_peer_trace		{ rxrpc_peer_traces } __mode(byte);
enum rxrpc_propose_ack_outcome	{ rxrpc_propose_ack_outcomes } __mode(byte);
enum rxrpc_propose_ack_trace	{ rxrpc_propose_ack_traces } __mode(byte);
enum rxrpc_receive_trace	{ rxrpc_receive_traces } __mode(byte);
enum rxrpc_recvmsg_trace	{ rxrpc_recvmsg_traces } __mode(byte);
enum rxrpc_req_ack_trace	{ rxrpc_req_ack_traces } __mode(byte);
enum rxrpc_rtt_rx_trace		{ rxrpc_rtt_rx_traces } __mode(byte);
enum rxrpc_rtt_tx_trace		{ rxrpc_rtt_tx_traces } __mode(byte);
enum rxrpc_sack_trace		{ rxrpc_sack_traces } __mode(byte);
enum rxrpc_skb_trace		{ rxrpc_skb_traces } __mode(byte);
enum rxrpc_timer_trace		{ rxrpc_timer_traces } __mode(byte);
enum rxrpc_tx_point		{ rxrpc_tx_points } __mode(byte);
enum rxrpc_txbuf_trace		{ rxrpc_txbuf_traces } __mode(byte);
enum rxrpc_txqueue_trace	{ rxrpc_txqueue_traces } __mode(byte);

#endif /* end __RXRPC_DECLARE_TRACE_ENUMS_ONCE_ONLY */

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_

#ifndef RXRPC_TRACE_ONLY_DEFINE_ENUMS

#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

rxrpc_abort_reasons;
rxrpc_bundle_traces;
rxrpc_call_poke_traces;
rxrpc_call_traces;
rxrpc_client_traces;
rxrpc_congest_changes;
rxrpc_congest_modes;
rxrpc_conn_traces;
rxrpc_local_traces;
rxrpc_propose_ack_traces;
rxrpc_receive_traces;
rxrpc_recvmsg_traces;
rxrpc_req_ack_traces;
rxrpc_rtt_rx_traces;
rxrpc_rtt_tx_traces;
rxrpc_sack_traces;
rxrpc_skb_traces;
rxrpc_timer_traces;
rxrpc_tx_points;
rxrpc_txbuf_traces;
rxrpc_txqueue_traces;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }

TRACE_EVENT(rxrpc_local,
	    TP_PROTO(unsigned int local_debug_id, enum rxrpc_local_trace op,
		     int ref, int usage),

	    TP_ARGS(local_debug_id, op, ref, usage),

	    TP_STRUCT__entry(
		    __field(unsigned int,	local)
		    __field(int,		op)
		    __field(int,		ref)
		    __field(int,		usage)
			     ),

	    TP_fast_assign(
		    __entry->local = local_debug_id;
		    __entry->op = op;
		    __entry->ref = ref;
		    __entry->usage = usage;
			   ),

	    TP_printk("L=%08x %s r=%d u=%d",
		      __entry->local,
		      __print_symbolic(__entry->op, rxrpc_local_traces),
		      __entry->ref,
		      __entry->usage)
	    );

TRACE_EVENT(rxrpc_peer,
	    TP_PROTO(unsigned int peer_debug_id, int ref, enum rxrpc_peer_trace why),

	    TP_ARGS(peer_debug_id, ref, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,	peer)
		    __field(int,		ref)
		    __field(enum rxrpc_peer_trace, why)
			     ),

	    TP_fast_assign(
		    __entry->peer = peer_debug_id;
		    __entry->ref = ref;
		    __entry->why = why;
			   ),

	    TP_printk("P=%08x %s r=%d",
		      __entry->peer,
		      __print_symbolic(__entry->why, rxrpc_peer_traces),
		      __entry->ref)
	    );

TRACE_EVENT(rxrpc_bundle,
	    TP_PROTO(unsigned int bundle_debug_id, int ref, enum rxrpc_bundle_trace why),

	    TP_ARGS(bundle_debug_id, ref, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,	bundle)
		    __field(int,		ref)
		    __field(int,		why)
			     ),

	    TP_fast_assign(
		    __entry->bundle = bundle_debug_id;
		    __entry->ref = ref;
		    __entry->why = why;
			   ),

	    TP_printk("CB=%08x %s r=%d",
		      __entry->bundle,
		      __print_symbolic(__entry->why, rxrpc_bundle_traces),
		      __entry->ref)
	    );

TRACE_EVENT(rxrpc_conn,
	    TP_PROTO(unsigned int conn_debug_id, int ref, enum rxrpc_conn_trace why),

	    TP_ARGS(conn_debug_id, ref, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,	conn)
		    __field(int,		ref)
		    __field(int,		why)
			     ),

	    TP_fast_assign(
		    __entry->conn = conn_debug_id;
		    __entry->ref = ref;
		    __entry->why = why;
			   ),

	    TP_printk("C=%08x %s r=%d",
		      __entry->conn,
		      __print_symbolic(__entry->why, rxrpc_conn_traces),
		      __entry->ref)
	    );

TRACE_EVENT(rxrpc_client,
	    TP_PROTO(struct rxrpc_connection *conn, int channel,
		     enum rxrpc_client_trace op),

	    TP_ARGS(conn, channel, op),

	    TP_STRUCT__entry(
		    __field(unsigned int,		conn)
		    __field(u32,			cid)
		    __field(int,			channel)
		    __field(int,			usage)
		    __field(enum rxrpc_client_trace,	op)
			     ),

	    TP_fast_assign(
		    __entry->conn = conn ? conn->debug_id : 0;
		    __entry->channel = channel;
		    __entry->usage = conn ? refcount_read(&conn->ref) : -2;
		    __entry->op = op;
		    __entry->cid = conn ? conn->proto.cid : 0;
			   ),

	    TP_printk("C=%08x h=%2d %s i=%08x u=%d",
		      __entry->conn,
		      __entry->channel,
		      __print_symbolic(__entry->op, rxrpc_client_traces),
		      __entry->cid,
		      __entry->usage)
	    );

TRACE_EVENT(rxrpc_call,
	    TP_PROTO(unsigned int call_debug_id, int ref, unsigned long aux,
		     enum rxrpc_call_trace why),

	    TP_ARGS(call_debug_id, ref, aux, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(int,		ref)
		    __field(int,		why)
		    __field(unsigned long,	aux)
			     ),

	    TP_fast_assign(
		    __entry->call = call_debug_id;
		    __entry->ref = ref;
		    __entry->why = why;
		    __entry->aux = aux;
			   ),

	    TP_printk("c=%08x %s r=%d a=%lx",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_call_traces),
		      __entry->ref,
		      __entry->aux)
	    );

TRACE_EVENT(rxrpc_skb,
	    TP_PROTO(struct sk_buff *skb, int usage, int mod_count,
		     enum rxrpc_skb_trace why),

	    TP_ARGS(skb, usage, mod_count, why),

	    TP_STRUCT__entry(
		    __field(struct sk_buff *,		skb)
		    __field(int,			usage)
		    __field(int,			mod_count)
		    __field(enum rxrpc_skb_trace,	why)
			     ),

	    TP_fast_assign(
		    __entry->skb = skb;
		    __entry->usage = usage;
		    __entry->mod_count = mod_count;
		    __entry->why = why;
			   ),

	    TP_printk("s=%p Rx %s u=%d m=%d",
		      __entry->skb,
		      __print_symbolic(__entry->why, rxrpc_skb_traces),
		      __entry->usage,
		      __entry->mod_count)
	    );

TRACE_EVENT(rxrpc_rx_packet,
	    TP_PROTO(struct rxrpc_skb_priv *sp),

	    TP_ARGS(sp),

	    TP_STRUCT__entry(
		    __field_struct(struct rxrpc_host_header,	hdr)
			     ),

	    TP_fast_assign(
		    memcpy(&__entry->hdr, &sp->hdr, sizeof(__entry->hdr));
			   ),

	    TP_printk("%08x:%08x:%08x:%04x %08x %08x %02x %02x %s",
		      __entry->hdr.epoch, __entry->hdr.cid,
		      __entry->hdr.callNumber, __entry->hdr.serviceId,
		      __entry->hdr.serial, __entry->hdr.seq,
		      __entry->hdr.securityIndex, __entry->hdr.flags,
		      __print_symbolic(__entry->hdr.type, rxrpc_pkts))
	    );

TRACE_EVENT(rxrpc_rx_done,
	    TP_PROTO(int result, int abort_code),

	    TP_ARGS(result, abort_code),

	    TP_STRUCT__entry(
		    __field(int,	result)
		    __field(int,	abort_code)
			     ),

	    TP_fast_assign(
		    __entry->result = result;
		    __entry->abort_code = abort_code;
			   ),

	    TP_printk("r=%d a=%d", __entry->result, __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_abort,
	    TP_PROTO(unsigned int call_nr, enum rxrpc_abort_reason why,
		     u32 cid, u32 call_id, rxrpc_seq_t seq, int abort_code, int error),

	    TP_ARGS(call_nr, why, cid, call_id, seq, abort_code, error),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call_nr)
		    __field(enum rxrpc_abort_reason,	why)
		    __field(u32,			cid)
		    __field(u32,			call_id)
		    __field(rxrpc_seq_t,		seq)
		    __field(int,			abort_code)
		    __field(int,			error)
			     ),

	    TP_fast_assign(
		    __entry->call_nr = call_nr;
		    __entry->why = why;
		    __entry->cid = cid;
		    __entry->call_id = call_id;
		    __entry->abort_code = abort_code;
		    __entry->error = error;
		    __entry->seq = seq;
			   ),

	    TP_printk("c=%08x %08x:%08x s=%u a=%d e=%d %s",
		      __entry->call_nr,
		      __entry->cid, __entry->call_id, __entry->seq,
		      __entry->abort_code, __entry->error,
		      __print_symbolic(__entry->why, rxrpc_abort_reasons))
	    );

TRACE_EVENT(rxrpc_call_complete,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_call_completion,	compl)
		    __field(int,			error)
		    __field(u32,			abort_code)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->compl = call->completion;
		    __entry->error = call->error;
		    __entry->abort_code = call->abort_code;
			   ),

	    TP_printk("c=%08x %s r=%d ac=%d",
		      __entry->call,
		      __print_symbolic(__entry->compl, rxrpc_completions),
		      __entry->error,
		      __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_txqueue,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_txqueue_trace why),

	    TP_ARGS(call, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_txqueue_trace,	why)
		    __field(rxrpc_seq_t,		acks_hard_ack)
		    __field(rxrpc_seq_t,		tx_bottom)
		    __field(rxrpc_seq_t,		tx_top)
		    __field(rxrpc_seq_t,		tx_prepared)
		    __field(int,			tx_winsize)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->why = why;
		    __entry->acks_hard_ack = call->acks_hard_ack;
		    __entry->tx_bottom = call->tx_bottom;
		    __entry->tx_top = call->tx_top;
		    __entry->tx_prepared = call->tx_prepared;
		    __entry->tx_winsize = call->tx_winsize;
			   ),

	    TP_printk("c=%08x %s f=%08x h=%08x n=%u/%u/%u/%u",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_txqueue_traces),
		      __entry->tx_bottom,
		      __entry->acks_hard_ack,
		      __entry->tx_top - __entry->tx_bottom,
		      __entry->tx_top - __entry->acks_hard_ack,
		      __entry->tx_prepared - __entry->tx_bottom,
		      __entry->tx_winsize)
	    );

TRACE_EVENT(rxrpc_rx_data,
	    TP_PROTO(unsigned int call, rxrpc_seq_t seq,
		     rxrpc_serial_t serial, u8 flags),

	    TP_ARGS(call, seq, serial, flags),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(rxrpc_seq_t,	seq)
		    __field(rxrpc_serial_t,	serial)
		    __field(u8,			flags)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->seq = seq;
		    __entry->serial = serial;
		    __entry->flags = flags;
			   ),

	    TP_printk("c=%08x DATA %08x q=%08x fl=%02x",
		      __entry->call,
		      __entry->serial,
		      __entry->seq,
		      __entry->flags)
	    );

TRACE_EVENT(rxrpc_rx_ack,
	    TP_PROTO(struct rxrpc_call *call,
		     rxrpc_serial_t serial, rxrpc_serial_t ack_serial,
		     rxrpc_seq_t first, rxrpc_seq_t prev, u8 reason, u8 n_acks),

	    TP_ARGS(call, serial, ack_serial, first, prev, reason, n_acks),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(rxrpc_serial_t,	serial)
		    __field(rxrpc_serial_t,	ack_serial)
		    __field(rxrpc_seq_t,	first)
		    __field(rxrpc_seq_t,	prev)
		    __field(u8,			reason)
		    __field(u8,			n_acks)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->serial = serial;
		    __entry->ack_serial = ack_serial;
		    __entry->first = first;
		    __entry->prev = prev;
		    __entry->reason = reason;
		    __entry->n_acks = n_acks;
			   ),

	    TP_printk("c=%08x %08x %s r=%08x f=%08x p=%08x n=%u",
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
		    __field(unsigned int,	call)
		    __field(rxrpc_serial_t,	serial)
		    __field(u32,		abort_code)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->serial = serial;
		    __entry->abort_code = abort_code;
			   ),

	    TP_printk("c=%08x ABORT %08x ac=%d",
		      __entry->call,
		      __entry->serial,
		      __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_rx_challenge,
	    TP_PROTO(struct rxrpc_connection *conn, rxrpc_serial_t serial,
		     u32 version, u32 nonce, u32 min_level),

	    TP_ARGS(conn, serial, version, nonce, min_level),

	    TP_STRUCT__entry(
		    __field(unsigned int,	conn)
		    __field(rxrpc_serial_t,	serial)
		    __field(u32,		version)
		    __field(u32,		nonce)
		    __field(u32,		min_level)
			     ),

	    TP_fast_assign(
		    __entry->conn = conn->debug_id;
		    __entry->serial = serial;
		    __entry->version = version;
		    __entry->nonce = nonce;
		    __entry->min_level = min_level;
			   ),

	    TP_printk("C=%08x CHALLENGE %08x v=%x n=%x ml=%x",
		      __entry->conn,
		      __entry->serial,
		      __entry->version,
		      __entry->nonce,
		      __entry->min_level)
	    );

TRACE_EVENT(rxrpc_rx_response,
	    TP_PROTO(struct rxrpc_connection *conn, rxrpc_serial_t serial,
		     u32 version, u32 kvno, u32 ticket_len),

	    TP_ARGS(conn, serial, version, kvno, ticket_len),

	    TP_STRUCT__entry(
		    __field(unsigned int,	conn)
		    __field(rxrpc_serial_t,	serial)
		    __field(u32,		version)
		    __field(u32,		kvno)
		    __field(u32,		ticket_len)
			     ),

	    TP_fast_assign(
		    __entry->conn = conn->debug_id;
		    __entry->serial = serial;
		    __entry->version = version;
		    __entry->kvno = kvno;
		    __entry->ticket_len = ticket_len;
			   ),

	    TP_printk("C=%08x RESPONSE %08x v=%x kvno=%x tl=%x",
		      __entry->conn,
		      __entry->serial,
		      __entry->version,
		      __entry->kvno,
		      __entry->ticket_len)
	    );

TRACE_EVENT(rxrpc_rx_rwind_change,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_serial_t serial,
		     u32 rwind, bool wake),

	    TP_ARGS(call, serial, rwind, wake),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(rxrpc_serial_t,	serial)
		    __field(u32,		rwind)
		    __field(bool,		wake)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->serial = serial;
		    __entry->rwind = rwind;
		    __entry->wake = wake;
			   ),

	    TP_printk("c=%08x %08x rw=%u%s",
		      __entry->call,
		      __entry->serial,
		      __entry->rwind,
		      __entry->wake ? " wake" : "")
	    );

TRACE_EVENT(rxrpc_tx_packet,
	    TP_PROTO(unsigned int call_id, struct rxrpc_wire_header *whdr,
		     enum rxrpc_tx_point where),

	    TP_ARGS(call_id, whdr, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,			call)
		    __field(enum rxrpc_tx_point,		where)
		    __field_struct(struct rxrpc_wire_header,	whdr)
			     ),

	    TP_fast_assign(
		    __entry->call = call_id;
		    memcpy(&__entry->whdr, whdr, sizeof(__entry->whdr));
		    __entry->where = where;
			   ),

	    TP_printk("c=%08x %08x:%08x:%08x:%04x %08x %08x %02x %02x %s %s",
		      __entry->call,
		      ntohl(__entry->whdr.epoch),
		      ntohl(__entry->whdr.cid),
		      ntohl(__entry->whdr.callNumber),
		      ntohs(__entry->whdr.serviceId),
		      ntohl(__entry->whdr.serial),
		      ntohl(__entry->whdr.seq),
		      __entry->whdr.type, __entry->whdr.flags,
		      __entry->whdr.type <= 15 ?
		      __print_symbolic(__entry->whdr.type, rxrpc_pkts) : "?UNK",
		      __print_symbolic(__entry->where, rxrpc_tx_points))
	    );

TRACE_EVENT(rxrpc_tx_data,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_seq_t seq,
		     rxrpc_serial_t serial, unsigned int flags, bool lose),

	    TP_ARGS(call, seq, serial, flags, lose),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(rxrpc_seq_t,	seq)
		    __field(rxrpc_serial_t,	serial)
		    __field(u32,		cid)
		    __field(u32,		call_id)
		    __field(u16,		flags)
		    __field(bool,		lose)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->cid = call->cid;
		    __entry->call_id = call->call_id;
		    __entry->seq = seq;
		    __entry->serial = serial;
		    __entry->flags = flags;
		    __entry->lose = lose;
			   ),

	    TP_printk("c=%08x DATA %08x:%08x %08x q=%08x fl=%02x%s%s",
		      __entry->call,
		      __entry->cid,
		      __entry->call_id,
		      __entry->serial,
		      __entry->seq,
		      __entry->flags & RXRPC_TXBUF_WIRE_FLAGS,
		      __entry->flags & RXRPC_TXBUF_RESENT ? " *RETRANS*" : "",
		      __entry->lose ? " *LOSE*" : "")
	    );

TRACE_EVENT(rxrpc_tx_ack,
	    TP_PROTO(unsigned int call, rxrpc_serial_t serial,
		     rxrpc_seq_t ack_first, rxrpc_serial_t ack_serial,
		     u8 reason, u8 n_acks, u16 rwind),

	    TP_ARGS(call, serial, ack_first, ack_serial, reason, n_acks, rwind),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(rxrpc_serial_t,	serial)
		    __field(rxrpc_seq_t,	ack_first)
		    __field(rxrpc_serial_t,	ack_serial)
		    __field(u8,			reason)
		    __field(u8,			n_acks)
		    __field(u16,		rwind)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->serial = serial;
		    __entry->ack_first = ack_first;
		    __entry->ack_serial = ack_serial;
		    __entry->reason = reason;
		    __entry->n_acks = n_acks;
		    __entry->rwind = rwind;
			   ),

	    TP_printk(" c=%08x ACK  %08x %s f=%08x r=%08x n=%u rw=%u",
		      __entry->call,
		      __entry->serial,
		      __print_symbolic(__entry->reason, rxrpc_ack_names),
		      __entry->ack_first,
		      __entry->ack_serial,
		      __entry->n_acks,
		      __entry->rwind)
	    );

TRACE_EVENT(rxrpc_receive,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_receive_trace why,
		     rxrpc_serial_t serial, rxrpc_seq_t seq),

	    TP_ARGS(call, why, serial, seq),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_receive_trace,	why)
		    __field(rxrpc_serial_t,		serial)
		    __field(rxrpc_seq_t,		seq)
		    __field(rxrpc_seq_t,		window)
		    __field(rxrpc_seq_t,		wtop)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->why = why;
		    __entry->serial = serial;
		    __entry->seq = seq;
		    __entry->window = call->ackr_window;
		    __entry->wtop = call->ackr_wtop;
			   ),

	    TP_printk("c=%08x %s r=%08x q=%08x w=%08x-%08x",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_receive_traces),
		      __entry->serial,
		      __entry->seq,
		      __entry->window,
		      __entry->wtop)
	    );

TRACE_EVENT(rxrpc_recvmsg,
	    TP_PROTO(unsigned int call_debug_id, enum rxrpc_recvmsg_trace why,
		     int ret),

	    TP_ARGS(call_debug_id, why, ret),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_recvmsg_trace,	why)
		    __field(int,			ret)
			     ),

	    TP_fast_assign(
		    __entry->call = call_debug_id;
		    __entry->why = why;
		    __entry->ret = ret;
			   ),

	    TP_printk("c=%08x %s ret=%d",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_recvmsg_traces),
		      __entry->ret)
	    );

TRACE_EVENT(rxrpc_recvdata,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_recvmsg_trace why,
		     rxrpc_seq_t seq, unsigned int offset, unsigned int len,
		     int ret),

	    TP_ARGS(call, why, seq, offset, len, ret),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_recvmsg_trace,	why)
		    __field(rxrpc_seq_t,		seq)
		    __field(unsigned int,		offset)
		    __field(unsigned int,		len)
		    __field(int,			ret)
			     ),

	    TP_fast_assign(
		    __entry->call = call ? call->debug_id : 0;
		    __entry->why = why;
		    __entry->seq = seq;
		    __entry->offset = offset;
		    __entry->len = len;
		    __entry->ret = ret;
			   ),

	    TP_printk("c=%08x %s q=%08x o=%u l=%u ret=%d",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_recvmsg_traces),
		      __entry->seq,
		      __entry->offset,
		      __entry->len,
		      __entry->ret)
	    );

TRACE_EVENT(rxrpc_rtt_tx,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_rtt_tx_trace why,
		     int slot, rxrpc_serial_t send_serial),

	    TP_ARGS(call, why, slot, send_serial),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_rtt_tx_trace,	why)
		    __field(int,			slot)
		    __field(rxrpc_serial_t,		send_serial)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->why = why;
		    __entry->slot = slot;
		    __entry->send_serial = send_serial;
			   ),

	    TP_printk("c=%08x [%d] %s sr=%08x",
		      __entry->call,
		      __entry->slot,
		      __print_symbolic(__entry->why, rxrpc_rtt_tx_traces),
		      __entry->send_serial)
	    );

TRACE_EVENT(rxrpc_rtt_rx,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_rtt_rx_trace why,
		     int slot,
		     rxrpc_serial_t send_serial, rxrpc_serial_t resp_serial,
		     u32 rtt, u32 rto),

	    TP_ARGS(call, why, slot, send_serial, resp_serial, rtt, rto),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_rtt_rx_trace,	why)
		    __field(int,			slot)
		    __field(rxrpc_serial_t,		send_serial)
		    __field(rxrpc_serial_t,		resp_serial)
		    __field(u32,			rtt)
		    __field(u32,			rto)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->why = why;
		    __entry->slot = slot;
		    __entry->send_serial = send_serial;
		    __entry->resp_serial = resp_serial;
		    __entry->rtt = rtt;
		    __entry->rto = rto;
			   ),

	    TP_printk("c=%08x [%d] %s sr=%08x rr=%08x rtt=%u rto=%u",
		      __entry->call,
		      __entry->slot,
		      __print_symbolic(__entry->why, rxrpc_rtt_rx_traces),
		      __entry->send_serial,
		      __entry->resp_serial,
		      __entry->rtt,
		      __entry->rto)
	    );

TRACE_EVENT(rxrpc_timer_set,
	    TP_PROTO(struct rxrpc_call *call, ktime_t delay,
		     enum rxrpc_timer_trace why),

	    TP_ARGS(call, delay, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_timer_trace,	why)
		    __field(ktime_t,			delay)
			     ),

	    TP_fast_assign(
		    __entry->call		= call->debug_id;
		    __entry->why		= why;
		    __entry->delay		= delay;
			   ),

	    TP_printk("c=%08x %s to=%lld",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_timer_traces),
		      ktime_to_us(__entry->delay))
	    );

TRACE_EVENT(rxrpc_timer_exp,
	    TP_PROTO(struct rxrpc_call *call, ktime_t delay,
		     enum rxrpc_timer_trace why),

	    TP_ARGS(call, delay, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_timer_trace,	why)
		    __field(ktime_t,			delay)
			     ),

	    TP_fast_assign(
		    __entry->call		= call->debug_id;
		    __entry->why		= why;
		    __entry->delay		= delay;
			   ),

	    TP_printk("c=%08x %s to=%lld",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_timer_traces),
		      ktime_to_us(__entry->delay))
	    );

TRACE_EVENT(rxrpc_timer_can,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_timer_trace why),

	    TP_ARGS(call, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_timer_trace,	why)
			     ),

	    TP_fast_assign(
		    __entry->call		= call->debug_id;
		    __entry->why		= why;
			   ),

	    TP_printk("c=%08x %s",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_timer_traces))
	    );

TRACE_EVENT(rxrpc_timer_restart,
	    TP_PROTO(struct rxrpc_call *call, ktime_t delay, unsigned long delayj),

	    TP_ARGS(call, delay, delayj),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(unsigned long,		delayj)
		    __field(ktime_t,			delay)
			     ),

	    TP_fast_assign(
		    __entry->call		= call->debug_id;
		    __entry->delayj		= delayj;
		    __entry->delay		= delay;
			   ),

	    TP_printk("c=%08x to=%lld j=%ld",
		      __entry->call,
		      ktime_to_us(__entry->delay),
		      __entry->delayj)
	    );

TRACE_EVENT(rxrpc_timer_expired,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
			     ),

	    TP_fast_assign(
		    __entry->call		= call->debug_id;
			   ),

	    TP_printk("c=%08x EXPIRED",
		      __entry->call)
	    );

TRACE_EVENT(rxrpc_rx_lose,
	    TP_PROTO(struct rxrpc_skb_priv *sp),

	    TP_ARGS(sp),

	    TP_STRUCT__entry(
		    __field_struct(struct rxrpc_host_header,	hdr)
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
		     u8 ack_reason, rxrpc_serial_t serial),

	    TP_ARGS(call, why, ack_reason, serial),

	    TP_STRUCT__entry(
		    __field(unsigned int,			call)
		    __field(enum rxrpc_propose_ack_trace,	why)
		    __field(rxrpc_serial_t,			serial)
		    __field(u8,					ack_reason)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->why	= why;
		    __entry->serial	= serial;
		    __entry->ack_reason	= ack_reason;
			   ),

	    TP_printk("c=%08x %s %s r=%08x",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_propose_ack_traces),
		      __print_symbolic(__entry->ack_reason, rxrpc_ack_names),
		      __entry->serial)
	    );

TRACE_EVENT(rxrpc_send_ack,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_propose_ack_trace why,
		     u8 ack_reason, rxrpc_serial_t serial),

	    TP_ARGS(call, why, ack_reason, serial),

	    TP_STRUCT__entry(
		    __field(unsigned int,			call)
		    __field(enum rxrpc_propose_ack_trace,	why)
		    __field(rxrpc_serial_t,			serial)
		    __field(u8,					ack_reason)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->why	= why;
		    __entry->serial	= serial;
		    __entry->ack_reason	= ack_reason;
			   ),

	    TP_printk("c=%08x %s %s r=%08x",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_propose_ack_traces),
		      __print_symbolic(__entry->ack_reason, rxrpc_ack_names),
		      __entry->serial)
	    );

TRACE_EVENT(rxrpc_drop_ack,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_propose_ack_trace why,
		     u8 ack_reason, rxrpc_serial_t serial, bool nobuf),

	    TP_ARGS(call, why, ack_reason, serial, nobuf),

	    TP_STRUCT__entry(
		    __field(unsigned int,			call)
		    __field(enum rxrpc_propose_ack_trace,	why)
		    __field(rxrpc_serial_t,			serial)
		    __field(u8,					ack_reason)
		    __field(bool,				nobuf)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->why	= why;
		    __entry->serial	= serial;
		    __entry->ack_reason	= ack_reason;
		    __entry->nobuf	= nobuf;
			   ),

	    TP_printk("c=%08x %s %s r=%08x nbf=%u",
		      __entry->call,
		      __print_symbolic(__entry->why, rxrpc_propose_ack_traces),
		      __print_symbolic(__entry->ack_reason, rxrpc_ack_names),
		      __entry->serial, __entry->nobuf)
	    );

TRACE_EVENT(rxrpc_retransmit,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_seq_t seq,
		     rxrpc_serial_t serial, ktime_t expiry),

	    TP_ARGS(call, seq, serial, expiry),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(rxrpc_seq_t,	seq)
		    __field(rxrpc_serial_t,	serial)
		    __field(ktime_t,		expiry)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->seq = seq;
		    __entry->serial = serial;
		    __entry->expiry = expiry;
			   ),

	    TP_printk("c=%08x q=%x r=%x xp=%lld",
		      __entry->call,
		      __entry->seq,
		      __entry->serial,
		      ktime_to_us(__entry->expiry))
	    );

TRACE_EVENT(rxrpc_congest,
	    TP_PROTO(struct rxrpc_call *call, struct rxrpc_ack_summary *summary,
		     rxrpc_serial_t ack_serial, enum rxrpc_congest_change change),

	    TP_ARGS(call, summary, ack_serial, change),

	    TP_STRUCT__entry(
		    __field(unsigned int,			call)
		    __field(enum rxrpc_congest_change,		change)
		    __field(rxrpc_seq_t,			hard_ack)
		    __field(rxrpc_seq_t,			top)
		    __field(rxrpc_seq_t,			lowest_nak)
		    __field(rxrpc_serial_t,			ack_serial)
		    __field_struct(struct rxrpc_ack_summary,	sum)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->change	= change;
		    __entry->hard_ack	= call->acks_hard_ack;
		    __entry->top	= call->tx_top;
		    __entry->lowest_nak	= call->acks_lowest_nak;
		    __entry->ack_serial	= ack_serial;
		    memcpy(&__entry->sum, summary, sizeof(__entry->sum));
			   ),

	    TP_printk("c=%08x r=%08x %s q=%08x %s cw=%u ss=%u nA=%u,%u+%u,%u b=%u u=%u d=%u l=%x%s%s%s",
		      __entry->call,
		      __entry->ack_serial,
		      __print_symbolic(__entry->sum.ack_reason, rxrpc_ack_names),
		      __entry->hard_ack,
		      __print_symbolic(__entry->sum.mode, rxrpc_congest_modes),
		      __entry->sum.cwnd,
		      __entry->sum.ssthresh,
		      __entry->sum.nr_acks, __entry->sum.nr_retained_nacks,
		      __entry->sum.nr_new_acks,
		      __entry->sum.nr_new_nacks,
		      __entry->top - __entry->hard_ack,
		      __entry->sum.cumulative_acks,
		      __entry->sum.dup_acks,
		      __entry->lowest_nak, __entry->sum.new_low_nack ? "!" : "",
		      __print_symbolic(__entry->change, rxrpc_congest_changes),
		      __entry->sum.retrans_timeo ? " rTxTo" : "")
	    );

TRACE_EVENT(rxrpc_reset_cwnd,
	    TP_PROTO(struct rxrpc_call *call, ktime_t now),

	    TP_ARGS(call, now),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(enum rxrpc_congest_mode,	mode)
		    __field(unsigned short,		cwnd)
		    __field(unsigned short,		extra)
		    __field(rxrpc_seq_t,		hard_ack)
		    __field(rxrpc_seq_t,		prepared)
		    __field(ktime_t,			since_last_tx)
		    __field(bool,			has_data)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->mode	= call->cong_mode;
		    __entry->cwnd	= call->cong_cwnd;
		    __entry->extra	= call->cong_extra;
		    __entry->hard_ack	= call->acks_hard_ack;
		    __entry->prepared	= call->tx_prepared - call->tx_bottom;
		    __entry->since_last_tx = ktime_sub(now, call->tx_last_sent);
		    __entry->has_data	= !list_empty(&call->tx_sendmsg);
			   ),

	    TP_printk("c=%08x q=%08x %s cw=%u+%u pr=%u tm=%llu d=%u",
		      __entry->call,
		      __entry->hard_ack,
		      __print_symbolic(__entry->mode, rxrpc_congest_modes),
		      __entry->cwnd,
		      __entry->extra,
		      __entry->prepared,
		      ktime_to_ns(__entry->since_last_tx),
		      __entry->has_data)
	    );

TRACE_EVENT(rxrpc_disconnect_call,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(u32,		abort_code)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->abort_code = call->abort_code;
			   ),

	    TP_printk("c=%08x ab=%08x",
		      __entry->call,
		      __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_improper_term,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(u32,		abort_code)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->abort_code = call->abort_code;
			   ),

	    TP_printk("c=%08x ab=%08x",
		      __entry->call,
		      __entry->abort_code)
	    );

TRACE_EVENT(rxrpc_connect_call,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call)
		    __field(unsigned long,		user_call_ID)
		    __field(u32,			cid)
		    __field(u32,			call_id)
		    __field_struct(struct sockaddr_rxrpc, srx)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->user_call_ID = call->user_call_ID;
		    __entry->cid = call->cid;
		    __entry->call_id = call->call_id;
		    __entry->srx = call->dest_srx;
			   ),

	    TP_printk("c=%08x u=%p %08x:%08x dst=%pISp",
		      __entry->call,
		      (void *)__entry->user_call_ID,
		      __entry->cid,
		      __entry->call_id,
		      &__entry->srx.transport)
	    );

TRACE_EVENT(rxrpc_resend,
	    TP_PROTO(struct rxrpc_call *call, struct sk_buff *ack),

	    TP_ARGS(call, ack),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call)
		    __field(rxrpc_seq_t,	seq)
		    __field(rxrpc_seq_t,	transmitted)
		    __field(rxrpc_serial_t,	ack_serial)
			     ),

	    TP_fast_assign(
		    struct rxrpc_skb_priv *sp = ack ? rxrpc_skb(ack) : NULL;
		    __entry->call = call->debug_id;
		    __entry->seq = call->acks_hard_ack;
		    __entry->transmitted = call->tx_transmitted;
		    __entry->ack_serial = sp ? sp->hdr.serial : 0;
			   ),

	    TP_printk("c=%08x r=%x q=%x tq=%x",
		      __entry->call,
		      __entry->ack_serial,
		      __entry->seq,
		      __entry->transmitted)
	    );

TRACE_EVENT(rxrpc_rx_icmp,
	    TP_PROTO(struct rxrpc_peer *peer, struct sock_extended_err *ee,
		     struct sockaddr_rxrpc *srx),

	    TP_ARGS(peer, ee, srx),

	    TP_STRUCT__entry(
		    __field(unsigned int,			peer)
		    __field_struct(struct sock_extended_err,	ee)
		    __field_struct(struct sockaddr_rxrpc,	srx)
			     ),

	    TP_fast_assign(
		    __entry->peer = peer->debug_id;
		    memcpy(&__entry->ee, ee, sizeof(__entry->ee));
		    memcpy(&__entry->srx, srx, sizeof(__entry->srx));
			   ),

	    TP_printk("P=%08x o=%u t=%u c=%u i=%u d=%u e=%d %pISp",
		      __entry->peer,
		      __entry->ee.ee_origin,
		      __entry->ee.ee_type,
		      __entry->ee.ee_code,
		      __entry->ee.ee_info,
		      __entry->ee.ee_data,
		      __entry->ee.ee_errno,
		      &__entry->srx.transport)
	    );

TRACE_EVENT(rxrpc_tx_fail,
	    TP_PROTO(unsigned int debug_id, rxrpc_serial_t serial, int ret,
		     enum rxrpc_tx_point where),

	    TP_ARGS(debug_id, serial, ret, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,		debug_id)
		    __field(rxrpc_serial_t,		serial)
		    __field(int,			ret)
		    __field(enum rxrpc_tx_point,	where)
			     ),

	    TP_fast_assign(
		    __entry->debug_id = debug_id;
		    __entry->serial = serial;
		    __entry->ret = ret;
		    __entry->where = where;
			   ),

	    TP_printk("c=%08x r=%x ret=%d %s",
		      __entry->debug_id,
		      __entry->serial,
		      __entry->ret,
		      __print_symbolic(__entry->where, rxrpc_tx_points))
	    );

TRACE_EVENT(rxrpc_call_reset,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,	debug_id)
		    __field(u32,		cid)
		    __field(u32,		call_id)
		    __field(rxrpc_serial_t,	call_serial)
		    __field(rxrpc_serial_t,	conn_serial)
		    __field(rxrpc_seq_t,	tx_seq)
		    __field(rxrpc_seq_t,	rx_seq)
			     ),

	    TP_fast_assign(
		    __entry->debug_id = call->debug_id;
		    __entry->cid = call->cid;
		    __entry->call_id = call->call_id;
		    __entry->call_serial = call->rx_serial;
		    __entry->conn_serial = call->conn->hi_serial;
		    __entry->tx_seq = call->acks_hard_ack;
		    __entry->rx_seq = call->rx_highest_seq;
			   ),

	    TP_printk("c=%08x %08x:%08x r=%08x/%08x tx=%08x rx=%08x",
		      __entry->debug_id,
		      __entry->cid, __entry->call_id,
		      __entry->call_serial, __entry->conn_serial,
		      __entry->tx_seq, __entry->rx_seq)
	    );

TRACE_EVENT(rxrpc_notify_socket,
	    TP_PROTO(unsigned int debug_id, rxrpc_serial_t serial),

	    TP_ARGS(debug_id, serial),

	    TP_STRUCT__entry(
		    __field(unsigned int,	debug_id)
		    __field(rxrpc_serial_t,	serial)
			     ),

	    TP_fast_assign(
		    __entry->debug_id = debug_id;
		    __entry->serial = serial;
			   ),

	    TP_printk("c=%08x r=%08x",
		      __entry->debug_id,
		      __entry->serial)
	    );

TRACE_EVENT(rxrpc_rx_discard_ack,
	    TP_PROTO(unsigned int debug_id, rxrpc_serial_t serial,
		     rxrpc_seq_t first_soft_ack, rxrpc_seq_t call_ackr_first,
		     rxrpc_seq_t prev_pkt, rxrpc_seq_t call_ackr_prev),

	    TP_ARGS(debug_id, serial, first_soft_ack, call_ackr_first,
		    prev_pkt, call_ackr_prev),

	    TP_STRUCT__entry(
		    __field(unsigned int,	debug_id)
		    __field(rxrpc_serial_t,	serial)
		    __field(rxrpc_seq_t,	first_soft_ack)
		    __field(rxrpc_seq_t,	call_ackr_first)
		    __field(rxrpc_seq_t,	prev_pkt)
		    __field(rxrpc_seq_t,	call_ackr_prev)
			     ),

	    TP_fast_assign(
		    __entry->debug_id		= debug_id;
		    __entry->serial		= serial;
		    __entry->first_soft_ack	= first_soft_ack;
		    __entry->call_ackr_first	= call_ackr_first;
		    __entry->prev_pkt		= prev_pkt;
		    __entry->call_ackr_prev	= call_ackr_prev;
			   ),

	    TP_printk("c=%08x r=%08x %08x<%08x %08x<%08x",
		      __entry->debug_id,
		      __entry->serial,
		      __entry->first_soft_ack,
		      __entry->call_ackr_first,
		      __entry->prev_pkt,
		      __entry->call_ackr_prev)
	    );

TRACE_EVENT(rxrpc_req_ack,
	    TP_PROTO(unsigned int call_debug_id, rxrpc_seq_t seq,
		     enum rxrpc_req_ack_trace why),

	    TP_ARGS(call_debug_id, seq, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call_debug_id)
		    __field(rxrpc_seq_t,		seq)
		    __field(enum rxrpc_req_ack_trace,	why)
			     ),

	    TP_fast_assign(
		    __entry->call_debug_id = call_debug_id;
		    __entry->seq = seq;
		    __entry->why = why;
			   ),

	    TP_printk("c=%08x q=%08x REQ-%s",
		      __entry->call_debug_id,
		      __entry->seq,
		      __print_symbolic(__entry->why, rxrpc_req_ack_traces))
	    );

TRACE_EVENT(rxrpc_txbuf,
	    TP_PROTO(unsigned int debug_id,
		     unsigned int call_debug_id, rxrpc_seq_t seq,
		     int ref, enum rxrpc_txbuf_trace what),

	    TP_ARGS(debug_id, call_debug_id, seq, ref, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		debug_id)
		    __field(unsigned int,		call_debug_id)
		    __field(rxrpc_seq_t,		seq)
		    __field(int,			ref)
		    __field(enum rxrpc_txbuf_trace,	what)
			     ),

	    TP_fast_assign(
		    __entry->debug_id = debug_id;
		    __entry->call_debug_id = call_debug_id;
		    __entry->seq = seq;
		    __entry->ref = ref;
		    __entry->what = what;
			   ),

	    TP_printk("B=%08x c=%08x q=%08x %s r=%d",
		      __entry->debug_id,
		      __entry->call_debug_id,
		      __entry->seq,
		      __print_symbolic(__entry->what, rxrpc_txbuf_traces),
		      __entry->ref)
	    );

TRACE_EVENT(rxrpc_poke_call,
	    TP_PROTO(struct rxrpc_call *call, bool busy,
		     enum rxrpc_call_poke_trace what),

	    TP_ARGS(call, busy, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call_debug_id)
		    __field(bool,			busy)
		    __field(enum rxrpc_call_poke_trace,	what)
			     ),

	    TP_fast_assign(
		    __entry->call_debug_id = call->debug_id;
		    __entry->busy = busy;
		    __entry->what = what;
			   ),

	    TP_printk("c=%08x %s%s",
		      __entry->call_debug_id,
		      __print_symbolic(__entry->what, rxrpc_call_poke_traces),
		      __entry->busy ? "!" : "")
	    );

TRACE_EVENT(rxrpc_call_poked,
	    TP_PROTO(struct rxrpc_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call_debug_id)
			     ),

	    TP_fast_assign(
		    __entry->call_debug_id = call->debug_id;
			   ),

	    TP_printk("c=%08x",
		      __entry->call_debug_id)
	    );

TRACE_EVENT(rxrpc_sack,
	    TP_PROTO(struct rxrpc_call *call, rxrpc_seq_t seq,
		     unsigned int sack, enum rxrpc_sack_trace what),

	    TP_ARGS(call, seq, sack, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call_debug_id)
		    __field(rxrpc_seq_t,		seq)
		    __field(unsigned int,		sack)
		    __field(enum rxrpc_sack_trace,	what)
			     ),

	    TP_fast_assign(
		    __entry->call_debug_id = call->debug_id;
		    __entry->seq = seq;
		    __entry->sack = sack;
		    __entry->what = what;
			   ),

	    TP_printk("c=%08x q=%08x %s k=%x",
		      __entry->call_debug_id,
		      __entry->seq,
		      __print_symbolic(__entry->what, rxrpc_sack_traces),
		      __entry->sack)
	    );

#undef EM
#undef E_

#endif /* RXRPC_TRACE_ONLY_DEFINE_ENUMS */
#endif /* _TRACE_RXRPC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
