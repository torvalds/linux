/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2007-2014 QLogic Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BXE_STATS_H
#define BXE_STATS_H

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

struct nig_stats {
    uint32_t brb_discard;
    uint32_t brb_packet;
    uint32_t brb_truncate;
    uint32_t flow_ctrl_discard;
    uint32_t flow_ctrl_octets;
    uint32_t flow_ctrl_packet;
    uint32_t mng_discard;
    uint32_t mng_octet_inp;
    uint32_t mng_octet_out;
    uint32_t mng_packet_inp;
    uint32_t mng_packet_out;
    uint32_t pbf_octets;
    uint32_t pbf_packet;
    uint32_t safc_inp;
    uint32_t egress_mac_pkt0_lo;
    uint32_t egress_mac_pkt0_hi;
    uint32_t egress_mac_pkt1_lo;
    uint32_t egress_mac_pkt1_hi;
};


enum bxe_stats_event {
    STATS_EVENT_PMF = 0,
    STATS_EVENT_LINK_UP,
    STATS_EVENT_UPDATE,
    STATS_EVENT_STOP,
    STATS_EVENT_MAX
};

enum bxe_stats_state {
    STATS_STATE_DISABLED = 0,
    STATS_STATE_ENABLED,
    STATS_STATE_MAX
};

struct bxe_eth_stats {
    uint32_t total_bytes_received_hi;
    uint32_t total_bytes_received_lo;
    uint32_t total_bytes_transmitted_hi;
    uint32_t total_bytes_transmitted_lo;
    uint32_t total_unicast_packets_received_hi;
    uint32_t total_unicast_packets_received_lo;
    uint32_t total_multicast_packets_received_hi;
    uint32_t total_multicast_packets_received_lo;
    uint32_t total_broadcast_packets_received_hi;
    uint32_t total_broadcast_packets_received_lo;
    uint32_t total_unicast_packets_transmitted_hi;
    uint32_t total_unicast_packets_transmitted_lo;
    uint32_t total_multicast_packets_transmitted_hi;
    uint32_t total_multicast_packets_transmitted_lo;
    uint32_t total_broadcast_packets_transmitted_hi;
    uint32_t total_broadcast_packets_transmitted_lo;
    uint32_t valid_bytes_received_hi;
    uint32_t valid_bytes_received_lo;

    uint32_t error_bytes_received_hi;
    uint32_t error_bytes_received_lo;
    uint32_t etherstatsoverrsizepkts_hi;
    uint32_t etherstatsoverrsizepkts_lo;
    uint32_t no_buff_discard_hi;
    uint32_t no_buff_discard_lo;

    uint32_t rx_stat_ifhcinbadoctets_hi;
    uint32_t rx_stat_ifhcinbadoctets_lo;
    uint32_t tx_stat_ifhcoutbadoctets_hi;
    uint32_t tx_stat_ifhcoutbadoctets_lo;
    uint32_t rx_stat_dot3statsfcserrors_hi;
    uint32_t rx_stat_dot3statsfcserrors_lo;
    uint32_t rx_stat_dot3statsalignmenterrors_hi;
    uint32_t rx_stat_dot3statsalignmenterrors_lo;
    uint32_t rx_stat_dot3statscarriersenseerrors_hi;
    uint32_t rx_stat_dot3statscarriersenseerrors_lo;
    uint32_t rx_stat_falsecarriererrors_hi;
    uint32_t rx_stat_falsecarriererrors_lo;
    uint32_t rx_stat_etherstatsundersizepkts_hi;
    uint32_t rx_stat_etherstatsundersizepkts_lo;
    uint32_t rx_stat_dot3statsframestoolong_hi;
    uint32_t rx_stat_dot3statsframestoolong_lo;
    uint32_t rx_stat_etherstatsfragments_hi;
    uint32_t rx_stat_etherstatsfragments_lo;
    uint32_t rx_stat_etherstatsjabbers_hi;
    uint32_t rx_stat_etherstatsjabbers_lo;
    uint32_t rx_stat_maccontrolframesreceived_hi;
    uint32_t rx_stat_maccontrolframesreceived_lo;
    uint32_t rx_stat_bmac_xpf_hi;
    uint32_t rx_stat_bmac_xpf_lo;
    uint32_t rx_stat_bmac_xcf_hi;
    uint32_t rx_stat_bmac_xcf_lo;
    uint32_t rx_stat_xoffstateentered_hi;
    uint32_t rx_stat_xoffstateentered_lo;
    uint32_t rx_stat_xonpauseframesreceived_hi;
    uint32_t rx_stat_xonpauseframesreceived_lo;
    uint32_t rx_stat_xoffpauseframesreceived_hi;
    uint32_t rx_stat_xoffpauseframesreceived_lo;
    uint32_t tx_stat_outxonsent_hi;
    uint32_t tx_stat_outxonsent_lo;
    uint32_t tx_stat_outxoffsent_hi;
    uint32_t tx_stat_outxoffsent_lo;
    uint32_t tx_stat_flowcontroldone_hi;
    uint32_t tx_stat_flowcontroldone_lo;
    uint32_t tx_stat_etherstatscollisions_hi;
    uint32_t tx_stat_etherstatscollisions_lo;
    uint32_t tx_stat_dot3statssinglecollisionframes_hi;
    uint32_t tx_stat_dot3statssinglecollisionframes_lo;
    uint32_t tx_stat_dot3statsmultiplecollisionframes_hi;
    uint32_t tx_stat_dot3statsmultiplecollisionframes_lo;
    uint32_t tx_stat_dot3statsdeferredtransmissions_hi;
    uint32_t tx_stat_dot3statsdeferredtransmissions_lo;
    uint32_t tx_stat_dot3statsexcessivecollisions_hi;
    uint32_t tx_stat_dot3statsexcessivecollisions_lo;
    uint32_t tx_stat_dot3statslatecollisions_hi;
    uint32_t tx_stat_dot3statslatecollisions_lo;
    uint32_t tx_stat_etherstatspkts64octets_hi;
    uint32_t tx_stat_etherstatspkts64octets_lo;
    uint32_t tx_stat_etherstatspkts65octetsto127octets_hi;
    uint32_t tx_stat_etherstatspkts65octetsto127octets_lo;
    uint32_t tx_stat_etherstatspkts128octetsto255octets_hi;
    uint32_t tx_stat_etherstatspkts128octetsto255octets_lo;
    uint32_t tx_stat_etherstatspkts256octetsto511octets_hi;
    uint32_t tx_stat_etherstatspkts256octetsto511octets_lo;
    uint32_t tx_stat_etherstatspkts512octetsto1023octets_hi;
    uint32_t tx_stat_etherstatspkts512octetsto1023octets_lo;
    uint32_t tx_stat_etherstatspkts1024octetsto1522octets_hi;
    uint32_t tx_stat_etherstatspkts1024octetsto1522octets_lo;
    uint32_t tx_stat_etherstatspktsover1522octets_hi;
    uint32_t tx_stat_etherstatspktsover1522octets_lo;
    uint32_t tx_stat_bmac_2047_hi;
    uint32_t tx_stat_bmac_2047_lo;
    uint32_t tx_stat_bmac_4095_hi;
    uint32_t tx_stat_bmac_4095_lo;
    uint32_t tx_stat_bmac_9216_hi;
    uint32_t tx_stat_bmac_9216_lo;
    uint32_t tx_stat_bmac_16383_hi;
    uint32_t tx_stat_bmac_16383_lo;
    uint32_t tx_stat_dot3statsinternalmactransmiterrors_hi;
    uint32_t tx_stat_dot3statsinternalmactransmiterrors_lo;
    uint32_t tx_stat_bmac_ufl_hi;
    uint32_t tx_stat_bmac_ufl_lo;

    uint32_t pause_frames_received_hi;
    uint32_t pause_frames_received_lo;
    uint32_t pause_frames_sent_hi;
    uint32_t pause_frames_sent_lo;

    uint32_t etherstatspkts1024octetsto1522octets_hi;
    uint32_t etherstatspkts1024octetsto1522octets_lo;
    uint32_t etherstatspktsover1522octets_hi;
    uint32_t etherstatspktsover1522octets_lo;

    uint32_t brb_drop_hi;
    uint32_t brb_drop_lo;
    uint32_t brb_truncate_hi;
    uint32_t brb_truncate_lo;

    uint32_t mac_filter_discard;
    uint32_t mf_tag_discard;
    uint32_t brb_truncate_discard;
    uint32_t mac_discard;

    uint32_t nig_timer_max;

    uint32_t total_tpa_aggregations_hi;
    uint32_t total_tpa_aggregations_lo;
    uint32_t total_tpa_aggregated_frames_hi;
    uint32_t total_tpa_aggregated_frames_lo;
    uint32_t total_tpa_bytes_hi;
    uint32_t total_tpa_bytes_lo;

    /* PFC */
    uint32_t pfc_frames_received_hi;
    uint32_t pfc_frames_received_lo;
    uint32_t pfc_frames_sent_hi;
    uint32_t pfc_frames_sent_lo;

    /* Recovery */
    uint32_t recoverable_error;
    uint32_t unrecoverable_error;

    /* src: Clear-on-Read register; Will not survive PMF Migration */
    uint32_t eee_tx_lpi;

    /* receive path driver statistics */
    uint32_t rx_calls;
    uint32_t rx_pkts;
    uint32_t rx_tpa_pkts;
    uint32_t rx_erroneous_jumbo_sge_pkts;
    uint32_t rx_bxe_service_rxsgl;
    uint32_t rx_jumbo_sge_pkts;
    uint32_t rx_soft_errors;
    uint32_t rx_hw_csum_errors;
    uint32_t rx_ofld_frames_csum_ip;
    uint32_t rx_ofld_frames_csum_tcp_udp;
    uint32_t rx_budget_reached;

    /* tx path driver statistics */
    uint32_t tx_pkts;
    uint32_t tx_soft_errors;
    uint32_t tx_ofld_frames_csum_ip;
    uint32_t tx_ofld_frames_csum_tcp;
    uint32_t tx_ofld_frames_csum_udp;
    uint32_t tx_ofld_frames_lso;
    uint32_t tx_ofld_frames_lso_hdr_splits;
    uint32_t tx_encap_failures;
    uint32_t tx_hw_queue_full;
    uint32_t tx_hw_max_queue_depth;
    uint32_t tx_dma_mapping_failure;
    uint32_t tx_max_drbr_queue_depth;
    uint32_t tx_window_violation_std;
    uint32_t tx_window_violation_tso;
    //uint32_t tx_unsupported_tso_request_ipv6;
    //uint32_t tx_unsupported_tso_request_not_tcp;
    uint32_t tx_chain_lost_mbuf;
    uint32_t tx_frames_deferred;
    uint32_t tx_queue_xoff;

    /* mbuf driver statistics */
    uint32_t mbuf_defrag_attempts;
    uint32_t mbuf_defrag_failures;
    uint32_t mbuf_rx_bd_alloc_failed;
    uint32_t mbuf_rx_bd_mapping_failed;
    uint32_t mbuf_rx_tpa_alloc_failed;
    uint32_t mbuf_rx_tpa_mapping_failed;
    uint32_t mbuf_rx_sge_alloc_failed;
    uint32_t mbuf_rx_sge_mapping_failed;

    /* track the number of allocated mbufs */
    uint32_t mbuf_alloc_tx;
    uint32_t mbuf_alloc_rx;
    uint32_t mbuf_alloc_sge;
    uint32_t mbuf_alloc_tpa;

    /* num. of times tx queue full occurred */
    uint32_t tx_queue_full_return;
    /* debug stats */
    uint32_t bxe_tx_mq_sc_state_failures;
    uint32_t tx_request_link_down_failures;
    uint32_t bd_avail_too_less_failures;
    uint32_t tx_mq_not_empty;
    uint32_t nsegs_path1_errors;
    uint32_t nsegs_path2_errors;

};


struct bxe_eth_q_stats {
    uint32_t total_unicast_bytes_received_hi;
    uint32_t total_unicast_bytes_received_lo;
    uint32_t total_broadcast_bytes_received_hi;
    uint32_t total_broadcast_bytes_received_lo;
    uint32_t total_multicast_bytes_received_hi;
    uint32_t total_multicast_bytes_received_lo;
    uint32_t total_bytes_received_hi;
    uint32_t total_bytes_received_lo;
    uint32_t total_unicast_bytes_transmitted_hi;
    uint32_t total_unicast_bytes_transmitted_lo;
    uint32_t total_broadcast_bytes_transmitted_hi;
    uint32_t total_broadcast_bytes_transmitted_lo;
    uint32_t total_multicast_bytes_transmitted_hi;
    uint32_t total_multicast_bytes_transmitted_lo;
    uint32_t total_bytes_transmitted_hi;
    uint32_t total_bytes_transmitted_lo;
    uint32_t total_unicast_packets_received_hi;
    uint32_t total_unicast_packets_received_lo;
    uint32_t total_multicast_packets_received_hi;
    uint32_t total_multicast_packets_received_lo;
    uint32_t total_broadcast_packets_received_hi;
    uint32_t total_broadcast_packets_received_lo;
    uint32_t total_unicast_packets_transmitted_hi;
    uint32_t total_unicast_packets_transmitted_lo;
    uint32_t total_multicast_packets_transmitted_hi;
    uint32_t total_multicast_packets_transmitted_lo;
    uint32_t total_broadcast_packets_transmitted_hi;
    uint32_t total_broadcast_packets_transmitted_lo;
    uint32_t valid_bytes_received_hi;
    uint32_t valid_bytes_received_lo;

    uint32_t etherstatsoverrsizepkts_hi;
    uint32_t etherstatsoverrsizepkts_lo;
    uint32_t no_buff_discard_hi;
    uint32_t no_buff_discard_lo;

    uint32_t total_packets_received_checksum_discarded_hi;
    uint32_t total_packets_received_checksum_discarded_lo;
    uint32_t total_packets_received_ttl0_discarded_hi;
    uint32_t total_packets_received_ttl0_discarded_lo;
    uint32_t total_transmitted_dropped_packets_error_hi;
    uint32_t total_transmitted_dropped_packets_error_lo;

    uint32_t total_tpa_aggregations_hi;
    uint32_t total_tpa_aggregations_lo;
    uint32_t total_tpa_aggregated_frames_hi;
    uint32_t total_tpa_aggregated_frames_lo;
    uint32_t total_tpa_bytes_hi;
    uint32_t total_tpa_bytes_lo;

    /* receive path driver statistics */
    uint32_t rx_calls;
    uint32_t rx_pkts;
    uint32_t rx_tpa_pkts;
    uint32_t rx_erroneous_jumbo_sge_pkts;
    uint32_t rx_bxe_service_rxsgl;
    uint32_t rx_jumbo_sge_pkts;
    uint32_t rx_soft_errors;
    uint32_t rx_hw_csum_errors;
    uint32_t rx_ofld_frames_csum_ip;
    uint32_t rx_ofld_frames_csum_tcp_udp;
    uint32_t rx_budget_reached;

    /* tx path driver statistics */
    uint32_t tx_pkts;
    uint32_t tx_soft_errors;
    uint32_t tx_ofld_frames_csum_ip;
    uint32_t tx_ofld_frames_csum_tcp;
    uint32_t tx_ofld_frames_csum_udp;
    uint32_t tx_ofld_frames_lso;
    uint32_t tx_ofld_frames_lso_hdr_splits;
    uint32_t tx_encap_failures;
    uint32_t tx_hw_queue_full;
    uint32_t tx_hw_max_queue_depth;
    uint32_t tx_dma_mapping_failure;
    uint32_t tx_max_drbr_queue_depth;
    uint32_t tx_window_violation_std;
    uint32_t tx_window_violation_tso;
    //uint32_t tx_unsupported_tso_request_ipv6;
    //uint32_t tx_unsupported_tso_request_not_tcp;
    uint32_t tx_chain_lost_mbuf;
    uint32_t tx_frames_deferred;
    uint32_t tx_queue_xoff;

    /* mbuf driver statistics */
    uint32_t mbuf_defrag_attempts;
    uint32_t mbuf_defrag_failures;
    uint32_t mbuf_rx_bd_alloc_failed;
    uint32_t mbuf_rx_bd_mapping_failed;
    uint32_t mbuf_rx_tpa_alloc_failed;
    uint32_t mbuf_rx_tpa_mapping_failed;
    uint32_t mbuf_rx_sge_alloc_failed;
    uint32_t mbuf_rx_sge_mapping_failed;

    /* track the number of allocated mbufs */
    uint32_t mbuf_alloc_tx;
    uint32_t mbuf_alloc_rx;
    uint32_t mbuf_alloc_sge;
    uint32_t mbuf_alloc_tpa;

    /* num. of times tx queue full occurred */
    uint32_t tx_queue_full_return;

    /* debug stats */
    uint32_t bxe_tx_mq_sc_state_failures;
    uint32_t tx_request_link_down_failures;
    uint32_t bd_avail_too_less_failures;
    uint32_t tx_mq_not_empty;
    uint32_t nsegs_path1_errors;
    uint32_t nsegs_path2_errors;

};

struct bxe_eth_stats_old {
    uint32_t rx_stat_dot3statsframestoolong_hi;
    uint32_t rx_stat_dot3statsframestoolong_lo;
};

struct bxe_eth_q_stats_old {
    /* Fields to perserve over fw reset*/
    uint32_t total_unicast_bytes_received_hi;
    uint32_t total_unicast_bytes_received_lo;
    uint32_t total_broadcast_bytes_received_hi;
    uint32_t total_broadcast_bytes_received_lo;
    uint32_t total_multicast_bytes_received_hi;
    uint32_t total_multicast_bytes_received_lo;
    uint32_t total_unicast_bytes_transmitted_hi;
    uint32_t total_unicast_bytes_transmitted_lo;
    uint32_t total_broadcast_bytes_transmitted_hi;
    uint32_t total_broadcast_bytes_transmitted_lo;
    uint32_t total_multicast_bytes_transmitted_hi;
    uint32_t total_multicast_bytes_transmitted_lo;
    uint32_t total_tpa_bytes_hi;
    uint32_t total_tpa_bytes_lo;

    /* Fields to perserve last of */
    uint32_t total_bytes_received_hi;
    uint32_t total_bytes_received_lo;
    uint32_t total_bytes_transmitted_hi;
    uint32_t total_bytes_transmitted_lo;
    uint32_t total_unicast_packets_received_hi;
    uint32_t total_unicast_packets_received_lo;
    uint32_t total_multicast_packets_received_hi;
    uint32_t total_multicast_packets_received_lo;
    uint32_t total_broadcast_packets_received_hi;
    uint32_t total_broadcast_packets_received_lo;
    uint32_t total_unicast_packets_transmitted_hi;
    uint32_t total_unicast_packets_transmitted_lo;
    uint32_t total_multicast_packets_transmitted_hi;
    uint32_t total_multicast_packets_transmitted_lo;
    uint32_t total_broadcast_packets_transmitted_hi;
    uint32_t total_broadcast_packets_transmitted_lo;
    uint32_t valid_bytes_received_hi;
    uint32_t valid_bytes_received_lo;

    uint32_t total_tpa_bytes_hi_old;
    uint32_t total_tpa_bytes_lo_old;

    /* receive path driver statistics */
    uint32_t rx_calls_old;
    uint32_t rx_pkts_old;
    uint32_t rx_tpa_pkts_old;
    uint32_t rx_erroneous_jumbo_sge_pkts_old;
    uint32_t rx_bxe_service_rxsgl_old;
    uint32_t rx_jumbo_sge_pkts_old;
    uint32_t rx_soft_errors_old;
    uint32_t rx_hw_csum_errors_old;
    uint32_t rx_ofld_frames_csum_ip_old;
    uint32_t rx_ofld_frames_csum_tcp_udp_old;
    uint32_t rx_budget_reached_old;

    /* tx path driver statistics */
    uint32_t tx_pkts_old;
    uint32_t tx_soft_errors_old;
    uint32_t tx_ofld_frames_csum_ip_old;
    uint32_t tx_ofld_frames_csum_tcp_old;
    uint32_t tx_ofld_frames_csum_udp_old;
    uint32_t tx_ofld_frames_lso_old;
    uint32_t tx_ofld_frames_lso_hdr_splits_old;
    uint32_t tx_encap_failures_old;
    uint32_t tx_hw_queue_full_old;
    uint32_t tx_hw_max_queue_depth_old;
    uint32_t tx_dma_mapping_failure_old;
    uint32_t tx_max_drbr_queue_depth_old;
    uint32_t tx_window_violation_std_old;
    uint32_t tx_window_violation_tso_old;
    //uint32_t tx_unsupported_tso_request_ipv6_old;
    //uint32_t tx_unsupported_tso_request_not_tcp_old;
    uint32_t tx_chain_lost_mbuf_old;
    uint32_t tx_frames_deferred_old;
    uint32_t tx_queue_xoff_old;

    /* mbuf driver statistics */
    uint32_t mbuf_defrag_attempts_old;
    uint32_t mbuf_defrag_failures_old;
    uint32_t mbuf_rx_bd_alloc_failed_old;
    uint32_t mbuf_rx_bd_mapping_failed_old;
    uint32_t mbuf_rx_tpa_alloc_failed_old;
    uint32_t mbuf_rx_tpa_mapping_failed_old;
    uint32_t mbuf_rx_sge_alloc_failed_old;
    uint32_t mbuf_rx_sge_mapping_failed_old;

    /* track the number of allocated mbufs */
    int mbuf_alloc_tx_old;
    int mbuf_alloc_rx_old;
    int mbuf_alloc_sge_old;
    int mbuf_alloc_tpa_old;
};

struct bxe_net_stats_old {
    uint32_t rx_dropped;
};

struct bxe_fw_port_stats_old {
    uint32_t pfc_frames_tx_hi;
    uint32_t pfc_frames_tx_lo;
    uint32_t pfc_frames_rx_hi;
    uint32_t pfc_frames_rx_lo;

    uint32_t mac_filter_discard;
    uint32_t mf_tag_discard;
    uint32_t brb_truncate_discard;
    uint32_t mac_discard;
};

/* sum[hi:lo] += add[hi:lo] */
#define ADD_64(s_hi, a_hi, s_lo, a_lo)          \
    do {                                        \
        s_lo += a_lo;                           \
        s_hi += a_hi + ((s_lo < a_lo) ? 1 : 0); \
    } while (0)

#define LE32_0 ((uint32_t) 0)
#define LE16_0 ((uint16_t) 0)

/* The _force is for cases where high value is 0 */
#define ADD_64_LE(s_hi, a_hi_le, s_lo, a_lo_le) \
        ADD_64(s_hi, le32toh(a_hi_le),          \
               s_lo, le32toh(a_lo_le))

#define ADD_64_LE16(s_hi, a_hi_le, s_lo, a_lo_le) \
        ADD_64(s_hi, le16toh(a_hi_le),            \
               s_lo, le16toh(a_lo_le))

/* difference = minuend - subtrahend */
#define DIFF_64(d_hi, m_hi, s_hi, d_lo, m_lo, s_lo)  \
    do {                                             \
        if (m_lo < s_lo) {                           \
            /* underflow */                          \
            d_hi = m_hi - s_hi;                      \
            if (d_hi > 0) {                          \
                /* we can 'loan' 1 */                \
                d_hi--;                              \
                d_lo = m_lo + (UINT_MAX - s_lo) + 1; \
            } else {                                 \
                /* m_hi <= s_hi */                   \
                d_hi = 0;                            \
                d_lo = 0;                            \
            }                                        \
        } else {                                     \
            /* m_lo >= s_lo */                       \
            if (m_hi < s_hi) {                       \
                d_hi = 0;                            \
                d_lo = 0;                            \
            } else {                                 \
                /* m_hi >= s_hi */                   \
                d_hi = m_hi - s_hi;                  \
                d_lo = m_lo - s_lo;                  \
            }                                        \
        }                                            \
    } while (0)

#define UPDATE_STAT64(s, t)                                      \
    do {                                                         \
        DIFF_64(diff.hi, new->s##_hi, pstats->mac_stx[0].t##_hi, \
            diff.lo, new->s##_lo, pstats->mac_stx[0].t##_lo);    \
        pstats->mac_stx[0].t##_hi = new->s##_hi;                 \
        pstats->mac_stx[0].t##_lo = new->s##_lo;                 \
        ADD_64(pstats->mac_stx[1].t##_hi, diff.hi,               \
               pstats->mac_stx[1].t##_lo, diff.lo);              \
    } while (0)

#define UPDATE_STAT64_NIG(s, t)                    \
    do {                                           \
        DIFF_64(diff.hi, new->s##_hi, old->s##_hi, \
            diff.lo, new->s##_lo, old->s##_lo);    \
        ADD_64(estats->t##_hi, diff.hi,            \
               estats->t##_lo, diff.lo);           \
    } while (0)

/* sum[hi:lo] += add */
#define ADD_EXTEND_64(s_hi, s_lo, a) \
    do {                             \
        s_lo += a;                   \
        s_hi += (s_lo < a) ? 1 : 0;  \
    } while (0)

#define ADD_STAT64(diff, t)                                \
    do {                                                   \
        ADD_64(pstats->mac_stx[1].t##_hi, new->diff##_hi,  \
               pstats->mac_stx[1].t##_lo, new->diff##_lo); \
    } while (0)

#define UPDATE_EXTEND_STAT(s)                    \
    do {                                         \
        ADD_EXTEND_64(pstats->mac_stx[1].s##_hi, \
                  pstats->mac_stx[1].s##_lo,     \
                  new->s);                       \
    } while (0)

#define UPDATE_EXTEND_TSTAT_X(s, t, size)                    \
    do {                                                     \
        diff = le##size##toh(tclient->s) -                   \
               le##size##toh(old_tclient->s);                \
        old_tclient->s = tclient->s;                         \
        ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
    } while (0)

#define UPDATE_EXTEND_TSTAT(s, t) UPDATE_EXTEND_TSTAT_X(s, t, 32)

#define UPDATE_EXTEND_E_TSTAT(s, t, size)                    \
    do {                                                     \
        UPDATE_EXTEND_TSTAT_X(s, t, size);                   \
        ADD_EXTEND_64(estats->t##_hi, estats->t##_lo, diff); \
    } while (0)

#define UPDATE_EXTEND_USTAT(s, t)                             \
    do {                                                      \
        diff = le32toh(uclient->s) - le32toh(old_uclient->s); \
        old_uclient->s = uclient->s;                          \
        ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff);  \
    } while (0)

#define UPDATE_EXTEND_E_USTAT(s, t)                          \
    do {                                                     \
        UPDATE_EXTEND_USTAT(s, t);                           \
        ADD_EXTEND_64(estats->t##_hi, estats->t##_lo, diff); \
    } while (0)

#define UPDATE_EXTEND_XSTAT(s, t)                             \
    do {                                                      \
        diff = le32toh(xclient->s) - le32toh(old_xclient->s); \
        old_xclient->s = xclient->s;                          \
        ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff);  \
    } while (0)

#define UPDATE_QSTAT(s, t)                                   \
    do {                                                     \
        qstats->t##_hi = qstats_old->t##_hi + le32toh(s.hi); \
        qstats->t##_lo = qstats_old->t##_lo + le32toh(s.lo); \
    } while (0)

#define UPDATE_QSTAT_OLD(f)        \
    do {                           \
        qstats_old->f = qstats->f; \
    } while (0)

#define UPDATE_ESTAT_QSTAT_64(s)                        \
    do {                                                \
        ADD_64(estats->s##_hi, qstats->s##_hi,          \
               estats->s##_lo, qstats->s##_lo);         \
        SUB_64(estats->s##_hi, qstats_old->s##_hi_old,  \
               estats->s##_lo, qstats_old->s##_lo_old); \
        qstats_old->s##_hi_old = qstats->s##_hi;        \
        qstats_old->s##_lo_old = qstats->s##_lo;        \
    } while (0)

#define UPDATE_ESTAT_QSTAT(s)             \
    do {                                  \
        estats->s += qstats->s;           \
        estats->s -= qstats_old->s##_old; \
        qstats_old->s##_old = qstats->s;  \
    } while (0)

#define UPDATE_FSTAT_QSTAT(s)                       \
    do {                                            \
        ADD_64(fstats->s##_hi, qstats->s##_hi,      \
               fstats->s##_lo, qstats->s##_lo);     \
        SUB_64(fstats->s##_hi, qstats_old->s##_hi,  \
               fstats->s##_lo, qstats_old->s##_lo); \
        estats->s##_hi = fstats->s##_hi;            \
        estats->s##_lo = fstats->s##_lo;            \
        qstats_old->s##_hi = qstats->s##_hi;        \
        qstats_old->s##_lo = qstats->s##_lo;        \
    } while (0)

#define UPDATE_FW_STAT(s)                           \
    do {                                            \
        estats->s = le32toh(tport->s) + fwstats->s; \
    } while (0)

#define UPDATE_FW_STAT_OLD(f)   \
    do {                        \
        fwstats->f = estats->f; \
    } while (0)

#define UPDATE_ESTAT(s, t)                          \
    do {                                            \
        SUB_64(estats->s##_hi, estats_old->t##_hi,  \
               estats->s##_lo, estats_old->t##_lo); \
        ADD_64(estats->s##_hi, estats->t##_hi,      \
               estats->s##_lo, estats->t##_lo);     \
        estats_old->t##_hi = estats->t##_hi;        \
        estats_old->t##_lo = estats->t##_lo;        \
    } while (0)

/* minuend -= subtrahend */
#define SUB_64(m_hi, s_hi, m_lo, s_lo)               \
    do {                                             \
        DIFF_64(m_hi, m_hi, s_hi, m_lo, m_lo, s_lo); \
    } while (0)

/* minuend[hi:lo] -= subtrahend */
#define SUB_EXTEND_64(m_hi, m_lo, s) \
    do {                             \
        SUB_64(m_hi, 0, m_lo, s);    \
    } while (0)

#define SUB_EXTEND_USTAT(s, t)                                \
    do {                                                      \
        diff = le32toh(uclient->s) - le32toh(old_uclient->s); \
        SUB_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff);  \
    } while (0)

struct bxe_softc;
void bxe_stats_init(struct bxe_softc *sc);
void bxe_stats_handle(struct bxe_softc *sc, enum bxe_stats_event event);
void bxe_save_statistics(struct bxe_softc *sc);
void bxe_afex_collect_stats(struct bxe_softc *sc, void *void_afex_stats, uint32_t stats_type);
uint64_t bxe_get_counter(if_t, ift_counter);

#endif /* BXE_STATS_H */

