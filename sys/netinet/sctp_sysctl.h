/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_SYSCTL_H_
#define _NETINET_SCTP_SYSCTL_H_

#include <netinet/sctp_os.h>
#include <netinet/sctp_constants.h>

struct sctp_sysctl {
	uint32_t sctp_sendspace;
	uint32_t sctp_recvspace;
	uint32_t sctp_auto_asconf;
	uint32_t sctp_multiple_asconfs;
	uint32_t sctp_ecn_enable;
	uint32_t sctp_pr_enable;
	uint32_t sctp_auth_enable;
	uint32_t sctp_asconf_enable;
	uint32_t sctp_reconfig_enable;
	uint32_t sctp_nrsack_enable;
	uint32_t sctp_pktdrop_enable;
	uint32_t sctp_fr_max_burst_default;
	uint32_t sctp_peer_chunk_oh;
	uint32_t sctp_max_burst_default;
	uint32_t sctp_max_chunks_on_queue;
	uint32_t sctp_hashtblsize;
	uint32_t sctp_pcbtblsize;
	uint32_t sctp_min_split_point;
	uint32_t sctp_chunkscale;
	uint32_t sctp_delayed_sack_time_default;
	uint32_t sctp_sack_freq_default;
	uint32_t sctp_system_free_resc_limit;
	uint32_t sctp_asoc_free_resc_limit;
	uint32_t sctp_heartbeat_interval_default;
	uint32_t sctp_pmtu_raise_time_default;
	uint32_t sctp_shutdown_guard_time_default;
	uint32_t sctp_secret_lifetime_default;
	uint32_t sctp_rto_max_default;
	uint32_t sctp_rto_min_default;
	uint32_t sctp_rto_initial_default;
	uint32_t sctp_init_rto_max_default;
	uint32_t sctp_valid_cookie_life_default;
	uint32_t sctp_init_rtx_max_default;
	uint32_t sctp_assoc_rtx_max_default;
	uint32_t sctp_path_rtx_max_default;
	uint32_t sctp_path_pf_threshold;
	uint32_t sctp_add_more_threshold;
	uint32_t sctp_nr_incoming_streams_default;
	uint32_t sctp_nr_outgoing_streams_default;
	uint32_t sctp_cmt_on_off;
	uint32_t sctp_cmt_use_dac;
	uint32_t sctp_use_cwnd_based_maxburst;
	uint32_t sctp_nat_friendly;
	uint32_t sctp_L2_abc_variable;
	uint32_t sctp_mbuf_threshold_count;
	uint32_t sctp_do_drain;
	uint32_t sctp_hb_maxburst;
	uint32_t sctp_abort_if_one_2_one_hits_limit;
	uint32_t sctp_min_residual;
	uint32_t sctp_max_retran_chunk;
	uint32_t sctp_logging_level;
	/* JRS - Variable for default congestion control module */
	uint32_t sctp_default_cc_module;
	/* RS - Variable for default stream scheduling module */
	uint32_t sctp_default_ss_module;
	uint32_t sctp_default_frag_interleave;
	uint32_t sctp_mobility_base;
	uint32_t sctp_mobility_fasthandoff;
	uint32_t sctp_inits_include_nat_friendly;
	uint32_t sctp_rttvar_bw;
	uint32_t sctp_rttvar_rtt;
	uint32_t sctp_rttvar_eqret;
	uint32_t sctp_steady_step;
	uint32_t sctp_use_dccc_ecn;
	uint32_t sctp_diag_info_code;
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_log sctp_log;
#endif
	uint32_t sctp_udp_tunneling_port;
	uint32_t sctp_enable_sack_immediately;
	uint32_t sctp_vtag_time_wait;
	uint32_t sctp_buffer_splitting;
	uint32_t sctp_initial_cwnd;
	uint32_t sctp_blackhole;
#if defined(SCTP_DEBUG)
	uint32_t sctp_debug_on;
#endif
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	uint32_t sctp_output_unlocked;
#endif
};

/*
 * limits for the sysctl variables
 */
/* maxdgram: Maximum outgoing SCTP buffer size */
#define SCTPCTL_MAXDGRAM_DESC		"Maximum outgoing SCTP buffer size"
#define SCTPCTL_MAXDGRAM_MIN		0
#define SCTPCTL_MAXDGRAM_MAX		0xFFFFFFFF
#define SCTPCTL_MAXDGRAM_DEFAULT	262144	/* 256k */

/* recvspace: Maximum incoming SCTP buffer size */
#define SCTPCTL_RECVSPACE_DESC		"Maximum incoming SCTP buffer size"
#define SCTPCTL_RECVSPACE_MIN		0
#define SCTPCTL_RECVSPACE_MAX		0xFFFFFFFF
#define SCTPCTL_RECVSPACE_DEFAULT	262144	/* 256k */

/* autoasconf: Enable SCTP Auto-ASCONF */
#define SCTPCTL_AUTOASCONF_DESC		"Enable SCTP Auto-ASCONF"
#define SCTPCTL_AUTOASCONF_MIN		0
#define SCTPCTL_AUTOASCONF_MAX		1
#define SCTPCTL_AUTOASCONF_DEFAULT	1

/* autoasconf: Enable SCTP Auto-ASCONF */
#define SCTPCTL_MULTIPLEASCONFS_DESC	"Enable SCTP Muliple-ASCONFs"
#define SCTPCTL_MULTIPLEASCONFS_MIN	0
#define SCTPCTL_MULTIPLEASCONFS_MAX	1
#define SCTPCTL_MULTIPLEASCONFS_DEFAULT	SCTP_DEFAULT_MULTIPLE_ASCONFS

/* ecn_enable: Enable SCTP ECN */
#define SCTPCTL_ECN_ENABLE_DESC		"Enable SCTP ECN"
#define SCTPCTL_ECN_ENABLE_MIN		0
#define SCTPCTL_ECN_ENABLE_MAX		1
#define SCTPCTL_ECN_ENABLE_DEFAULT	1

/* pr_enable: Enable PR-SCTP */
#define SCTPCTL_PR_ENABLE_DESC		"Enable PR-SCTP"
#define SCTPCTL_PR_ENABLE_MIN		0
#define SCTPCTL_PR_ENABLE_MAX		1
#define SCTPCTL_PR_ENABLE_DEFAULT	1

/* auth_enable: Enable SCTP AUTH function */
#define SCTPCTL_AUTH_ENABLE_DESC	"Enable SCTP AUTH function"
#define SCTPCTL_AUTH_ENABLE_MIN		0
#define SCTPCTL_AUTH_ENABLE_MAX		1
#define SCTPCTL_AUTH_ENABLE_DEFAULT	1

/* asconf_enable: Enable SCTP ASCONF */
#define SCTPCTL_ASCONF_ENABLE_DESC	"Enable SCTP ASCONF"
#define SCTPCTL_ASCONF_ENABLE_MIN	0
#define SCTPCTL_ASCONF_ENABLE_MAX	1
#define SCTPCTL_ASCONF_ENABLE_DEFAULT	1

/* reconfig_enable: Enable SCTP RE-CONFIG */
#define SCTPCTL_RECONFIG_ENABLE_DESC	"Enable SCTP RE-CONFIG"
#define SCTPCTL_RECONFIG_ENABLE_MIN	0
#define SCTPCTL_RECONFIG_ENABLE_MAX	1
#define SCTPCTL_RECONFIG_ENABLE_DEFAULT	1

/* nrsack_enable: Enable NR_SACK */
#define SCTPCTL_NRSACK_ENABLE_DESC	"Enable SCTP NR-SACK"
#define SCTPCTL_NRSACK_ENABLE_MIN	0
#define SCTPCTL_NRSACK_ENABLE_MAX	1
#define SCTPCTL_NRSACK_ENABLE_DEFAULT	0

/* pktdrop_enable: Enable SCTP Packet Drop Reports */
#define SCTPCTL_PKTDROP_ENABLE_DESC	"Enable SCTP PKTDROP"
#define SCTPCTL_PKTDROP_ENABLE_MIN	0
#define SCTPCTL_PKTDROP_ENABLE_MAX	1
#define SCTPCTL_PKTDROP_ENABLE_DEFAULT	0

/* loopback_nocsum: Enable NO Csum on packets sent on loopback */
#define SCTPCTL_LOOPBACK_NOCSUM_DESC	"Enable NO Csum on packets sent on loopback"
#define SCTPCTL_LOOPBACK_NOCSUM_MIN	0
#define SCTPCTL_LOOPBACK_NOCSUM_MAX	1
#define SCTPCTL_LOOPBACK_NOCSUM_DEFAULT	1

/* peer_chkoh: Amount to debit peers rwnd per chunk sent */
#define SCTPCTL_PEER_CHKOH_DESC		"Amount to debit peers rwnd per chunk sent"
#define SCTPCTL_PEER_CHKOH_MIN		0
#define SCTPCTL_PEER_CHKOH_MAX		0xFFFFFFFF
#define SCTPCTL_PEER_CHKOH_DEFAULT	256

/* maxburst: Default max burst for sctp endpoints */
#define SCTPCTL_MAXBURST_DESC		"Default max burst for sctp endpoints"
#define SCTPCTL_MAXBURST_MIN		0
#define SCTPCTL_MAXBURST_MAX		0xFFFFFFFF
#define SCTPCTL_MAXBURST_DEFAULT	SCTP_DEF_MAX_BURST

/* fr_maxburst: Default max burst for sctp endpoints when fast retransmitting */
#define SCTPCTL_FRMAXBURST_DESC		"Default max burst for SCTP endpoints when fast retransmitting"
#define SCTPCTL_FRMAXBURST_MIN		0
#define SCTPCTL_FRMAXBURST_MAX		0xFFFFFFFF
#define SCTPCTL_FRMAXBURST_DEFAULT	SCTP_DEF_FRMAX_BURST


/* maxchunks: Default max chunks on queue per asoc */
#define SCTPCTL_MAXCHUNKS_DESC		"Default max chunks on queue per asoc"
#define SCTPCTL_MAXCHUNKS_MIN		0
#define SCTPCTL_MAXCHUNKS_MAX		0xFFFFFFFF
#define SCTPCTL_MAXCHUNKS_DEFAULT	SCTP_ASOC_MAX_CHUNKS_ON_QUEUE

/* tcbhashsize: Tunable for Hash table sizes */
#define SCTPCTL_TCBHASHSIZE_DESC	"Tunable for TCB hash table sizes"
#define SCTPCTL_TCBHASHSIZE_MIN		1
#define SCTPCTL_TCBHASHSIZE_MAX		0xFFFFFFFF
#define SCTPCTL_TCBHASHSIZE_DEFAULT	SCTP_TCBHASHSIZE

/* pcbhashsize: Tunable for PCB Hash table sizes */
#define SCTPCTL_PCBHASHSIZE_DESC	"Tunable for PCB hash table sizes"
#define SCTPCTL_PCBHASHSIZE_MIN		1
#define SCTPCTL_PCBHASHSIZE_MAX		0xFFFFFFFF
#define SCTPCTL_PCBHASHSIZE_DEFAULT	SCTP_PCBHASHSIZE

/* min_split_point: Minimum size when splitting a chunk */
#define SCTPCTL_MIN_SPLIT_POINT_DESC	"Minimum size when splitting a chunk"
#define SCTPCTL_MIN_SPLIT_POINT_MIN	0
#define SCTPCTL_MIN_SPLIT_POINT_MAX	0xFFFFFFFF
#define SCTPCTL_MIN_SPLIT_POINT_DEFAULT	SCTP_DEFAULT_SPLIT_POINT_MIN

/* chunkscale: Tunable for Scaling of number of chunks and messages */
#define SCTPCTL_CHUNKSCALE_DESC		"Tunable for scaling of number of chunks and messages"
#define SCTPCTL_CHUNKSCALE_MIN		1
#define SCTPCTL_CHUNKSCALE_MAX		0xFFFFFFFF
#define SCTPCTL_CHUNKSCALE_DEFAULT	SCTP_CHUNKQUEUE_SCALE

/* delayed_sack_time: Default delayed SACK timer in ms */
#define SCTPCTL_DELAYED_SACK_TIME_DESC	"Default delayed SACK timer in ms"
#define SCTPCTL_DELAYED_SACK_TIME_MIN	0
#define SCTPCTL_DELAYED_SACK_TIME_MAX	0xFFFFFFFF
#define SCTPCTL_DELAYED_SACK_TIME_DEFAULT	SCTP_RECV_MSEC

/* sack_freq: Default SACK frequency */
#define SCTPCTL_SACK_FREQ_DESC		"Default SACK frequency"
#define SCTPCTL_SACK_FREQ_MIN		0
#define SCTPCTL_SACK_FREQ_MAX		0xFFFFFFFF
#define SCTPCTL_SACK_FREQ_DEFAULT	SCTP_DEFAULT_SACK_FREQ

/* sys_resource: Max number of cached resources in the system */
#define SCTPCTL_SYS_RESOURCE_DESC	"Max number of cached resources in the system"
#define SCTPCTL_SYS_RESOURCE_MIN	0
#define SCTPCTL_SYS_RESOURCE_MAX	0xFFFFFFFF
#define SCTPCTL_SYS_RESOURCE_DEFAULT	SCTP_DEF_SYSTEM_RESC_LIMIT

/* asoc_resource: Max number of cached resources in an asoc */
#define SCTPCTL_ASOC_RESOURCE_DESC	"Max number of cached resources in an asoc"
#define SCTPCTL_ASOC_RESOURCE_MIN	0
#define SCTPCTL_ASOC_RESOURCE_MAX	0xFFFFFFFF
#define SCTPCTL_ASOC_RESOURCE_DEFAULT	SCTP_DEF_ASOC_RESC_LIMIT

/* heartbeat_interval: Default heartbeat interval in ms */
#define SCTPCTL_HEARTBEAT_INTERVAL_DESC	"Default heartbeat interval in ms"
#define SCTPCTL_HEARTBEAT_INTERVAL_MIN	0
#define SCTPCTL_HEARTBEAT_INTERVAL_MAX	0xFFFFFFFF
#define SCTPCTL_HEARTBEAT_INTERVAL_DEFAULT	SCTP_HB_DEFAULT_MSEC

/* pmtu_raise_time: Default PMTU raise timer in seconds */
#define SCTPCTL_PMTU_RAISE_TIME_DESC	"Default PMTU raise timer in seconds"
#define SCTPCTL_PMTU_RAISE_TIME_MIN	0
#define SCTPCTL_PMTU_RAISE_TIME_MAX	0xFFFFFFFF
#define SCTPCTL_PMTU_RAISE_TIME_DEFAULT	SCTP_DEF_PMTU_RAISE_SEC

/* shutdown_guard_time: Default shutdown guard timer in seconds */
#define SCTPCTL_SHUTDOWN_GUARD_TIME_DESC	"Shutdown guard timer in seconds (0 means 5 times RTO.Max)"
#define SCTPCTL_SHUTDOWN_GUARD_TIME_MIN		0
#define SCTPCTL_SHUTDOWN_GUARD_TIME_MAX		0xFFFFFFFF
#define SCTPCTL_SHUTDOWN_GUARD_TIME_DEFAULT	0

/* secret_lifetime: Default secret lifetime in seconds */
#define SCTPCTL_SECRET_LIFETIME_DESC	"Default secret lifetime in seconds"
#define SCTPCTL_SECRET_LIFETIME_MIN	0
#define SCTPCTL_SECRET_LIFETIME_MAX	0xFFFFFFFF
#define SCTPCTL_SECRET_LIFETIME_DEFAULT	SCTP_DEFAULT_SECRET_LIFE_SEC

/* rto_max: Default maximum retransmission timeout in ms */
#define SCTPCTL_RTO_MAX_DESC		"Default maximum retransmission timeout in ms"
#define SCTPCTL_RTO_MAX_MIN		0
#define SCTPCTL_RTO_MAX_MAX		0xFFFFFFFF
#define SCTPCTL_RTO_MAX_DEFAULT		SCTP_RTO_UPPER_BOUND

/* rto_min: Default minimum retransmission timeout in ms */
#define SCTPCTL_RTO_MIN_DESC		"Default minimum retransmission timeout in ms"
#define SCTPCTL_RTO_MIN_MIN		0
#define SCTPCTL_RTO_MIN_MAX		0xFFFFFFFF
#define SCTPCTL_RTO_MIN_DEFAULT		SCTP_RTO_LOWER_BOUND

/* rto_initial: Default initial retransmission timeout in ms */
#define SCTPCTL_RTO_INITIAL_DESC	"Default initial retransmission timeout in ms"
#define SCTPCTL_RTO_INITIAL_MIN		0
#define SCTPCTL_RTO_INITIAL_MAX		0xFFFFFFFF
#define SCTPCTL_RTO_INITIAL_DEFAULT	SCTP_RTO_INITIAL

/* init_rto_max: Default maximum retransmission timeout during association setup in ms */
#define SCTPCTL_INIT_RTO_MAX_DESC	"Default maximum retransmission timeout during association setup in ms"
#define SCTPCTL_INIT_RTO_MAX_MIN	0
#define SCTPCTL_INIT_RTO_MAX_MAX	0xFFFFFFFF
#define SCTPCTL_INIT_RTO_MAX_DEFAULT	SCTP_RTO_UPPER_BOUND

/* valid_cookie_life: Default cookie lifetime in sec */
#define SCTPCTL_VALID_COOKIE_LIFE_DESC	"Default cookie lifetime in seconds"
#define SCTPCTL_VALID_COOKIE_LIFE_MIN	0
#define SCTPCTL_VALID_COOKIE_LIFE_MAX	0xFFFFFFFF
#define SCTPCTL_VALID_COOKIE_LIFE_DEFAULT	SCTP_DEFAULT_COOKIE_LIFE

/* init_rtx_max: Default maximum number of retransmission for INIT chunks */
#define SCTPCTL_INIT_RTX_MAX_DESC	"Default maximum number of retransmissions for INIT chunks"
#define SCTPCTL_INIT_RTX_MAX_MIN	0
#define SCTPCTL_INIT_RTX_MAX_MAX	0xFFFFFFFF
#define SCTPCTL_INIT_RTX_MAX_DEFAULT	SCTP_DEF_MAX_INIT

/* assoc_rtx_max: Default maximum number of retransmissions per association */
#define SCTPCTL_ASSOC_RTX_MAX_DESC	"Default maximum number of retransmissions per association"
#define SCTPCTL_ASSOC_RTX_MAX_MIN	0
#define SCTPCTL_ASSOC_RTX_MAX_MAX	0xFFFFFFFF
#define SCTPCTL_ASSOC_RTX_MAX_DEFAULT	SCTP_DEF_MAX_SEND

/* path_rtx_max: Default maximum of retransmissions per path */
#define SCTPCTL_PATH_RTX_MAX_DESC	"Default maximum of retransmissions per path"
#define SCTPCTL_PATH_RTX_MAX_MIN	0
#define SCTPCTL_PATH_RTX_MAX_MAX	0xFFFFFFFF
#define SCTPCTL_PATH_RTX_MAX_DEFAULT	SCTP_DEF_MAX_PATH_RTX

/* path_pf_threshold: threshold for considering the path potentially failed */
#define SCTPCTL_PATH_PF_THRESHOLD_DESC		"Default potentially failed threshold"
#define SCTPCTL_PATH_PF_THRESHOLD_MIN		0
#define SCTPCTL_PATH_PF_THRESHOLD_MAX		0xFFFF
#define SCTPCTL_PATH_PF_THRESHOLD_DEFAULT	SCTPCTL_PATH_PF_THRESHOLD_MAX

/* add_more_on_output: When space-wise is it worthwhile to try to add more to a socket send buffer */
#define SCTPCTL_ADD_MORE_ON_OUTPUT_DESC	"When space-wise is it worthwhile to try to add more to a socket send buffer"
#define SCTPCTL_ADD_MORE_ON_OUTPUT_MIN	0
#define SCTPCTL_ADD_MORE_ON_OUTPUT_MAX	0xFFFFFFFF
#define SCTPCTL_ADD_MORE_ON_OUTPUT_DEFAULT SCTP_DEFAULT_ADD_MORE

/* incoming_streams: Default number of incoming streams */
#define SCTPCTL_INCOMING_STREAMS_DESC	"Default number of incoming streams"
#define SCTPCTL_INCOMING_STREAMS_MIN	1
#define SCTPCTL_INCOMING_STREAMS_MAX	65535
#define SCTPCTL_INCOMING_STREAMS_DEFAULT SCTP_ISTREAM_INITIAL

/* outgoing_streams: Default number of outgoing streams */
#define SCTPCTL_OUTGOING_STREAMS_DESC	"Default number of outgoing streams"
#define SCTPCTL_OUTGOING_STREAMS_MIN	1
#define SCTPCTL_OUTGOING_STREAMS_MAX	65535
#define SCTPCTL_OUTGOING_STREAMS_DEFAULT SCTP_OSTREAM_INITIAL

/* cmt_on_off: CMT on/off flag */
#define SCTPCTL_CMT_ON_OFF_DESC		"CMT settings"
#define SCTPCTL_CMT_ON_OFF_MIN		SCTP_CMT_OFF
#define SCTPCTL_CMT_ON_OFF_MAX		SCTP_CMT_MAX
#define SCTPCTL_CMT_ON_OFF_DEFAULT	SCTP_CMT_OFF

/* cmt_use_dac: CMT DAC on/off flag */
#define SCTPCTL_CMT_USE_DAC_DESC	"CMT DAC on/off flag"
#define SCTPCTL_CMT_USE_DAC_MIN		0
#define SCTPCTL_CMT_USE_DAC_MAX		1
#define SCTPCTL_CMT_USE_DAC_DEFAULT    	0

/* cwnd_maxburst: Use a CWND adjusting to implement maxburst */
#define SCTPCTL_CWND_MAXBURST_DESC	"Adjust congestion control window to limit maximum burst when sending"
#define SCTPCTL_CWND_MAXBURST_MIN	0
#define SCTPCTL_CWND_MAXBURST_MAX	1
#define SCTPCTL_CWND_MAXBURST_DEFAULT	1

/* nat_friendly: SCTP NAT friendly operation */
#define SCTPCTL_NAT_FRIENDLY_DESC	"SCTP NAT friendly operation"
#define SCTPCTL_NAT_FRIENDLY_MIN	0
#define SCTPCTL_NAT_FRIENDLY_MAX	1
#define SCTPCTL_NAT_FRIENDLY_DEFAULT	1

/* abc_l_var: SCTP ABC max increase per SACK (L) */
#define SCTPCTL_ABC_L_VAR_DESC		"SCTP ABC max increase per SACK (L)"
#define SCTPCTL_ABC_L_VAR_MIN		0
#define SCTPCTL_ABC_L_VAR_MAX		0xFFFFFFFF
#define SCTPCTL_ABC_L_VAR_DEFAULT	2

/* max_chained_mbufs: Default max number of small mbufs on a chain */
#define SCTPCTL_MAX_CHAINED_MBUFS_DESC	"Default max number of small mbufs on a chain"
#define SCTPCTL_MAX_CHAINED_MBUFS_MIN	0
#define SCTPCTL_MAX_CHAINED_MBUFS_MAX	0xFFFFFFFF
#define SCTPCTL_MAX_CHAINED_MBUFS_DEFAULT	SCTP_DEFAULT_MBUFS_IN_CHAIN

/* do_sctp_drain: Should SCTP respond to the drain calls */
#define SCTPCTL_DO_SCTP_DRAIN_DESC	"Should SCTP respond to the drain calls"
#define SCTPCTL_DO_SCTP_DRAIN_MIN	0
#define SCTPCTL_DO_SCTP_DRAIN_MAX	1
#define SCTPCTL_DO_SCTP_DRAIN_DEFAULT	1

/* hb_max_burst: Confirmation Heartbeat max burst? */
#define SCTPCTL_HB_MAX_BURST_DESC	"Confirmation Heartbeat max burst"
#define SCTPCTL_HB_MAX_BURST_MIN	1
#define SCTPCTL_HB_MAX_BURST_MAX	0xFFFFFFFF
#define SCTPCTL_HB_MAX_BURST_DEFAULT	SCTP_DEF_HBMAX_BURST

/* abort_at_limit: When one-2-one hits qlimit abort */
#define SCTPCTL_ABORT_AT_LIMIT_DESC	"Abort when one-to-one hits qlimit"
#define SCTPCTL_ABORT_AT_LIMIT_MIN	0
#define SCTPCTL_ABORT_AT_LIMIT_MAX	1
#define SCTPCTL_ABORT_AT_LIMIT_DEFAULT	0

/* min_residual: min residual in a data fragment leftover */
#define SCTPCTL_MIN_RESIDUAL_DESC	"Minimum residual data chunk in second part of split"
#define SCTPCTL_MIN_RESIDUAL_MIN	20
#define SCTPCTL_MIN_RESIDUAL_MAX	65535
#define SCTPCTL_MIN_RESIDUAL_DEFAULT	1452

/* max_retran_chunk: max chunk retransmissions */
#define SCTPCTL_MAX_RETRAN_CHUNK_DESC	"Maximum times an unlucky chunk can be retransmitted before assoc abort"
#define SCTPCTL_MAX_RETRAN_CHUNK_MIN	0
#define SCTPCTL_MAX_RETRAN_CHUNK_MAX	65535
#define SCTPCTL_MAX_RETRAN_CHUNK_DEFAULT	30

/* sctp_logging: This gives us logging when the options are enabled */
#define SCTPCTL_LOGGING_LEVEL_DESC	"Ltrace/KTR trace logging level"
#define SCTPCTL_LOGGING_LEVEL_MIN	0
#define SCTPCTL_LOGGING_LEVEL_MAX	0xffffffff
#define SCTPCTL_LOGGING_LEVEL_DEFAULT	0

/* JRS - default congestion control module sysctl */
#define SCTPCTL_DEFAULT_CC_MODULE_DESC		"Default congestion control module"
#define SCTPCTL_DEFAULT_CC_MODULE_MIN		0
#define SCTPCTL_DEFAULT_CC_MODULE_MAX		2
#define SCTPCTL_DEFAULT_CC_MODULE_DEFAULT	0

/* RS - default stream scheduling module sysctl */
#define SCTPCTL_DEFAULT_SS_MODULE_DESC		"Default stream scheduling module"
#define SCTPCTL_DEFAULT_SS_MODULE_MIN		0
#define SCTPCTL_DEFAULT_SS_MODULE_MAX		5
#define SCTPCTL_DEFAULT_SS_MODULE_DEFAULT	0

/* RRS - default fragment interleave */
#define SCTPCTL_DEFAULT_FRAG_INTERLEAVE_DESC	"Default fragment interleave level"
#define SCTPCTL_DEFAULT_FRAG_INTERLEAVE_MIN	0
#define SCTPCTL_DEFAULT_FRAG_INTERLEAVE_MAX	2
#define SCTPCTL_DEFAULT_FRAG_INTERLEAVE_DEFAULT	1

/* mobility_base: Enable SCTP mobility support */
#define SCTPCTL_MOBILITY_BASE_DESC	"Enable SCTP base mobility"
#define SCTPCTL_MOBILITY_BASE_MIN	0
#define SCTPCTL_MOBILITY_BASE_MAX	1
#define SCTPCTL_MOBILITY_BASE_DEFAULT	0

/* mobility_fasthandoff: Enable SCTP fast handoff support */
#define SCTPCTL_MOBILITY_FASTHANDOFF_DESC	"Enable SCTP fast handoff"
#define SCTPCTL_MOBILITY_FASTHANDOFF_MIN	0
#define SCTPCTL_MOBILITY_FASTHANDOFF_MAX	1
#define SCTPCTL_MOBILITY_FASTHANDOFF_DEFAULT	0

/* Enable SCTP/UDP tunneling port */
#define SCTPCTL_UDP_TUNNELING_PORT_DESC		"Set the SCTP/UDP tunneling port"
#define SCTPCTL_UDP_TUNNELING_PORT_MIN		0
#define SCTPCTL_UDP_TUNNELING_PORT_MAX		65535
#define SCTPCTL_UDP_TUNNELING_PORT_DEFAULT	0

/* Enable sending of the SACK-IMMEDIATELY bit */
#define SCTPCTL_SACK_IMMEDIATELY_ENABLE_DESC	"Enable sending of the SACK-IMMEDIATELY-bit"
#define SCTPCTL_SACK_IMMEDIATELY_ENABLE_MIN	0
#define SCTPCTL_SACK_IMMEDIATELY_ENABLE_MAX	1
#define SCTPCTL_SACK_IMMEDIATELY_ENABLE_DEFAULT	SCTPCTL_SACK_IMMEDIATELY_ENABLE_MAX

/* Enable sending of the NAT-FRIENDLY message */
#define SCTPCTL_NAT_FRIENDLY_INITS_DESC	"Enable sending of the nat-friendly SCTP option on INITs"
#define SCTPCTL_NAT_FRIENDLY_INITS_MIN	0
#define SCTPCTL_NAT_FRIENDLY_INITS_MAX	1
#define SCTPCTL_NAT_FRIENDLY_INITS_DEFAULT	SCTPCTL_NAT_FRIENDLY_INITS_MIN

/* Vtag time wait in seconds */
#define SCTPCTL_TIME_WAIT_DESC	"Vtag time wait time in seconds, 0 disables it"
#define SCTPCTL_TIME_WAIT_MIN	0
#define SCTPCTL_TIME_WAIT_MAX	0xffffffff
#define SCTPCTL_TIME_WAIT_DEFAULT	SCTP_TIME_WAIT

/* Enable Send/Receive buffer splitting */
#define SCTPCTL_BUFFER_SPLITTING_DESC		"Enable send/receive buffer splitting"
#define SCTPCTL_BUFFER_SPLITTING_MIN		0
#define SCTPCTL_BUFFER_SPLITTING_MAX		0x3
#define SCTPCTL_BUFFER_SPLITTING_DEFAULT	SCTPCTL_BUFFER_SPLITTING_MIN

/* Initial congestion window in MTUs */
#define SCTPCTL_INITIAL_CWND_DESC	"Defines the initial congestion window size in MTUs"
#define SCTPCTL_INITIAL_CWND_MIN	0
#define SCTPCTL_INITIAL_CWND_MAX	0xffffffff
#define SCTPCTL_INITIAL_CWND_DEFAULT	3

/* rttvar smooth avg for bw calc  */
#define SCTPCTL_RTTVAR_BW_DESC	"Shift amount DCCC uses for bw smoothing on rtt calc"
#define SCTPCTL_RTTVAR_BW_MIN	0
#define SCTPCTL_RTTVAR_BW_MAX	32
#define SCTPCTL_RTTVAR_BW_DEFAULT	4

/* rttvar smooth avg for bw calc  */
#define SCTPCTL_RTTVAR_RTT_DESC	"Shift amount DCCC uses for rtt smoothing on rtt calc"
#define SCTPCTL_RTTVAR_RTT_MIN	0
#define SCTPCTL_RTTVAR_RTT_MAX	32
#define SCTPCTL_RTTVAR_RTT_DEFAULT	5

#define SCTPCTL_RTTVAR_EQRET_DESC	"Whether DCCC increases cwnd when the rtt and bw are unchanged"
#define SCTPCTL_RTTVAR_EQRET_MIN	0
#define SCTPCTL_RTTVAR_EQRET_MAX	1
#define SCTPCTL_RTTVAR_EQRET_DEFAULT	0

#define SCTPCTL_RTTVAR_STEADYS_DESC	"Number of identical bw measurements DCCC takes to try step down of cwnd"
#define SCTPCTL_RTTVAR_STEADYS_MIN	0
#define SCTPCTL_RTTVAR_STEADYS_MAX	0xFFFF
#define SCTPCTL_RTTVAR_STEADYS_DEFAULT	20	/* 0 means disable feature */

#define SCTPCTL_RTTVAR_DCCCECN_DESC	"Enable ECN for DCCC."
#define SCTPCTL_RTTVAR_DCCCECN_MIN	0
#define SCTPCTL_RTTVAR_DCCCECN_MAX	1
#define SCTPCTL_RTTVAR_DCCCECN_DEFAULT	1	/* 0 means disable feature */

#define SCTPCTL_BLACKHOLE_DESC		"Enable SCTP blackholing, see blackhole(4) for more details"
#define SCTPCTL_BLACKHOLE_MIN		0
#define SCTPCTL_BLACKHOLE_MAX		2
#define SCTPCTL_BLACKHOLE_DEFAULT	SCTPCTL_BLACKHOLE_MIN

#define SCTPCTL_DIAG_INFO_CODE_DESC	"Diagnostic information error cause code"
#define SCTPCTL_DIAG_INFO_CODE_MIN	0
#define SCTPCTL_DIAG_INFO_CODE_MAX	65535
#define SCTPCTL_DIAG_INFO_CODE_DEFAULT	0

#if defined(SCTP_DEBUG)
/* debug: Configure debug output */
#define SCTPCTL_DEBUG_DESC	"Configure debug output"
#define SCTPCTL_DEBUG_MIN	0
#define SCTPCTL_DEBUG_MAX	0xFFFFFFFF
#define SCTPCTL_DEBUG_DEFAULT	0
#endif


#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
#define SCTPCTL_OUTPUT_UNLOCKED_DESC	"Unlock socket when sending packets down to IP"
#define SCTPCTL_OUTPUT_UNLOCKED_MIN	0
#define SCTPCTL_OUTPUT_UNLOCKED_MAX	1
#define SCTPCTL_OUTPUT_UNLOCKED_DEFAULT	SCTPCTL_OUTPUT_UNLOCKED_MIN
#endif


#if defined(_KERNEL) || defined(__Userspace__)
#if defined(SYSCTL_DECL)
SYSCTL_DECL(_net_inet_sctp);
#endif

void sctp_init_sysctls(void);

#endif				/* _KERNEL */
#endif				/* __sctp_sysctl_h__ */
