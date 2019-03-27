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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define BXE_DRIVER_VERSION "1.78.91"

#include "bxe.h"
#include "ecore_sp.h"
#include "ecore_init.h"
#include "ecore_init_ops.h"

#include "57710_int_offsets.h"
#include "57711_int_offsets.h"
#include "57712_int_offsets.h"

/*
 * CTLTYPE_U64 and sysctl_handle_64 were added in r217616. Define these
 * explicitly here for older kernels that don't include this changeset.
 */
#ifndef CTLTYPE_U64
#define CTLTYPE_U64      CTLTYPE_QUAD
#define sysctl_handle_64 sysctl_handle_quad
#endif

/*
 * CSUM_TCP_IPV6 and CSUM_UDP_IPV6 were added in r236170. Define these
 * here as zero(0) for older kernels that don't include this changeset
 * thereby masking the functionality.
 */
#ifndef CSUM_TCP_IPV6
#define CSUM_TCP_IPV6 0
#define CSUM_UDP_IPV6 0
#endif

/*
 * pci_find_cap was added in r219865. Re-define this at pci_find_extcap
 * for older kernels that don't include this changeset.
 */
#if __FreeBSD_version < 900035
#define pci_find_cap pci_find_extcap
#endif

#define BXE_DEF_SB_ATT_IDX 0x0001
#define BXE_DEF_SB_IDX     0x0002

/*
 * FLR Support - bxe_pf_flr_clnup() is called during nic_load in the per
 * function HW initialization.
 */
#define FLR_WAIT_USEC     10000 /* 10 msecs */
#define FLR_WAIT_INTERVAL 50    /* usecs */
#define FLR_POLL_CNT      (FLR_WAIT_USEC / FLR_WAIT_INTERVAL) /* 200 */

struct pbf_pN_buf_regs {
    int pN;
    uint32_t init_crd;
    uint32_t crd;
    uint32_t crd_freed;
};

struct pbf_pN_cmd_regs {
    int pN;
    uint32_t lines_occup;
    uint32_t lines_freed;
};

/*
 * PCI Device ID Table used by bxe_probe().
 */
#define BXE_DEVDESC_MAX 64
static struct bxe_device_type bxe_devs[] = {
    {
        BRCM_VENDORID,
        CHIP_NUM_57710,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57710 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57711,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57711 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57711E,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57711E 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57712,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57712 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57712_MF,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57712 MF 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57800,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57800 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57800_MF,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57800 MF 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57810,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57810 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57810_MF,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57810 MF 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57811,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57811 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57811_MF,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57811 MF 10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57840_4_10,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57840 4x10GbE"
    },
    {
        QLOGIC_VENDORID,
        CHIP_NUM_57840_4_10,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57840 4x10GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57840_2_20,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57840 2x20GbE"
    },
    {
        BRCM_VENDORID,
        CHIP_NUM_57840_MF,
        PCI_ANY_ID, PCI_ANY_ID,
        "QLogic NetXtreme II BCM57840 MF 10GbE"
    },
    {
        0, 0, 0, 0, NULL
    }
};

MALLOC_DECLARE(M_BXE_ILT);
MALLOC_DEFINE(M_BXE_ILT, "bxe_ilt", "bxe ILT pointer");

/*
 * FreeBSD device entry points.
 */
static int bxe_probe(device_t);
static int bxe_attach(device_t);
static int bxe_detach(device_t);
static int bxe_shutdown(device_t);


/*
 * FreeBSD KLD module/device interface event handler method.
 */
static device_method_t bxe_methods[] = {
    /* Device interface (device_if.h) */
    DEVMETHOD(device_probe,     bxe_probe),
    DEVMETHOD(device_attach,    bxe_attach),
    DEVMETHOD(device_detach,    bxe_detach),
    DEVMETHOD(device_shutdown,  bxe_shutdown),
    /* Bus interface (bus_if.h) */
    DEVMETHOD(bus_print_child,  bus_generic_print_child),
    DEVMETHOD(bus_driver_added, bus_generic_driver_added),
    KOBJMETHOD_END
};

/*
 * FreeBSD KLD Module data declaration
 */
static driver_t bxe_driver = {
    "bxe",                   /* module name */
    bxe_methods,             /* event handler */
    sizeof(struct bxe_softc) /* extra data */
};

/*
 * FreeBSD dev class is needed to manage dev instances and
 * to associate with a bus type
 */
static devclass_t bxe_devclass;

MODULE_DEPEND(bxe, pci, 1, 1, 1);
MODULE_DEPEND(bxe, ether, 1, 1, 1);
DRIVER_MODULE(bxe, pci, bxe_driver, bxe_devclass, 0, 0);

NETDUMP_DEFINE(bxe);

/* resources needed for unloading a previously loaded device */

#define BXE_PREV_WAIT_NEEDED 1
struct mtx bxe_prev_mtx;
MTX_SYSINIT(bxe_prev_mtx, &bxe_prev_mtx, "bxe_prev_lock", MTX_DEF);
struct bxe_prev_list_node {
    LIST_ENTRY(bxe_prev_list_node) node;
    uint8_t bus;
    uint8_t slot;
    uint8_t path;
    uint8_t aer; /* XXX automatic error recovery */
    uint8_t undi;
};
static LIST_HEAD(, bxe_prev_list_node) bxe_prev_list = LIST_HEAD_INITIALIZER(bxe_prev_list);

static int load_count[2][3] = { {0} }; /* per-path: 0-common, 1-port0, 2-port1 */

/* Tunable device values... */

SYSCTL_NODE(_hw, OID_AUTO, bxe, CTLFLAG_RD, 0, "bxe driver parameters");

/* Debug */
unsigned long bxe_debug = 0;
SYSCTL_ULONG(_hw_bxe, OID_AUTO, debug, CTLFLAG_RDTUN,
             &bxe_debug, 0, "Debug logging mode");

/* Interrupt Mode: 0 (IRQ), 1 (MSI/IRQ), and 2 (MSI-X/MSI/IRQ) */
static int bxe_interrupt_mode = INTR_MODE_MSIX;
SYSCTL_INT(_hw_bxe, OID_AUTO, interrupt_mode, CTLFLAG_RDTUN,
           &bxe_interrupt_mode, 0, "Interrupt (MSI-X/MSI/INTx) mode");

/* Number of Queues: 0 (Auto) or 1 to 16 (fixed queue number) */
static int bxe_queue_count = 4;
SYSCTL_INT(_hw_bxe, OID_AUTO, queue_count, CTLFLAG_RDTUN,
           &bxe_queue_count, 0, "Multi-Queue queue count");

/* max number of buffers per queue (default RX_BD_USABLE) */
static int bxe_max_rx_bufs = 0;
SYSCTL_INT(_hw_bxe, OID_AUTO, max_rx_bufs, CTLFLAG_RDTUN,
           &bxe_max_rx_bufs, 0, "Maximum Number of Rx Buffers Per Queue");

/* Host interrupt coalescing RX tick timer (usecs) */
static int bxe_hc_rx_ticks = 25;
SYSCTL_INT(_hw_bxe, OID_AUTO, hc_rx_ticks, CTLFLAG_RDTUN,
           &bxe_hc_rx_ticks, 0, "Host Coalescing Rx ticks");

/* Host interrupt coalescing TX tick timer (usecs) */
static int bxe_hc_tx_ticks = 50;
SYSCTL_INT(_hw_bxe, OID_AUTO, hc_tx_ticks, CTLFLAG_RDTUN,
           &bxe_hc_tx_ticks, 0, "Host Coalescing Tx ticks");

/* Maximum number of Rx packets to process at a time */
static int bxe_rx_budget = 0xffffffff;
SYSCTL_INT(_hw_bxe, OID_AUTO, rx_budget, CTLFLAG_TUN,
           &bxe_rx_budget, 0, "Rx processing budget");

/* Maximum LRO aggregation size */
static int bxe_max_aggregation_size = 0;
SYSCTL_INT(_hw_bxe, OID_AUTO, max_aggregation_size, CTLFLAG_TUN,
           &bxe_max_aggregation_size, 0, "max aggregation size");

/* PCI MRRS: -1 (Auto), 0 (128B), 1 (256B), 2 (512B), 3 (1KB) */
static int bxe_mrrs = -1;
SYSCTL_INT(_hw_bxe, OID_AUTO, mrrs, CTLFLAG_RDTUN,
           &bxe_mrrs, 0, "PCIe maximum read request size");

/* AutoGrEEEn: 0 (hardware default), 1 (force on), 2 (force off) */
static int bxe_autogreeen = 0;
SYSCTL_INT(_hw_bxe, OID_AUTO, autogreeen, CTLFLAG_RDTUN,
           &bxe_autogreeen, 0, "AutoGrEEEn support");

/* 4-tuple RSS support for UDP: 0 (disabled), 1 (enabled) */
static int bxe_udp_rss = 0;
SYSCTL_INT(_hw_bxe, OID_AUTO, udp_rss, CTLFLAG_RDTUN,
           &bxe_udp_rss, 0, "UDP RSS support");


#define STAT_NAME_LEN 32 /* no stat names below can be longer than this */

#define STATS_OFFSET32(stat_name)                   \
    (offsetof(struct bxe_eth_stats, stat_name) / 4)

#define Q_STATS_OFFSET32(stat_name)                   \
    (offsetof(struct bxe_eth_q_stats, stat_name) / 4)

static const struct {
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
#define STATS_FLAGS_PORT  1
#define STATS_FLAGS_FUNC  2 /* MF only cares about function stats */
#define STATS_FLAGS_BOTH  (STATS_FLAGS_FUNC | STATS_FLAGS_PORT)
    char string[STAT_NAME_LEN];
} bxe_eth_stats_arr[] = {
    { STATS_OFFSET32(total_bytes_received_hi),
                8, STATS_FLAGS_BOTH, "rx_bytes" },
    { STATS_OFFSET32(error_bytes_received_hi),
                8, STATS_FLAGS_BOTH, "rx_error_bytes" },
    { STATS_OFFSET32(total_unicast_packets_received_hi),
                8, STATS_FLAGS_BOTH, "rx_ucast_packets" },
    { STATS_OFFSET32(total_multicast_packets_received_hi),
                8, STATS_FLAGS_BOTH, "rx_mcast_packets" },
    { STATS_OFFSET32(total_broadcast_packets_received_hi),
                8, STATS_FLAGS_BOTH, "rx_bcast_packets" },
    { STATS_OFFSET32(rx_stat_dot3statsfcserrors_hi),
                8, STATS_FLAGS_PORT, "rx_crc_errors" },
    { STATS_OFFSET32(rx_stat_dot3statsalignmenterrors_hi),
                8, STATS_FLAGS_PORT, "rx_align_errors" },
    { STATS_OFFSET32(rx_stat_etherstatsundersizepkts_hi),
                8, STATS_FLAGS_PORT, "rx_undersize_packets" },
    { STATS_OFFSET32(etherstatsoverrsizepkts_hi),
                8, STATS_FLAGS_PORT, "rx_oversize_packets" },
    { STATS_OFFSET32(rx_stat_etherstatsfragments_hi),
                8, STATS_FLAGS_PORT, "rx_fragments" },
    { STATS_OFFSET32(rx_stat_etherstatsjabbers_hi),
                8, STATS_FLAGS_PORT, "rx_jabbers" },
    { STATS_OFFSET32(no_buff_discard_hi),
                8, STATS_FLAGS_BOTH, "rx_discards" },
    { STATS_OFFSET32(mac_filter_discard),
                4, STATS_FLAGS_PORT, "rx_filtered_packets" },
    { STATS_OFFSET32(mf_tag_discard),
                4, STATS_FLAGS_PORT, "rx_mf_tag_discard" },
    { STATS_OFFSET32(pfc_frames_received_hi),
                8, STATS_FLAGS_PORT, "pfc_frames_received" },
    { STATS_OFFSET32(pfc_frames_sent_hi),
                8, STATS_FLAGS_PORT, "pfc_frames_sent" },
    { STATS_OFFSET32(brb_drop_hi),
                8, STATS_FLAGS_PORT, "rx_brb_discard" },
    { STATS_OFFSET32(brb_truncate_hi),
                8, STATS_FLAGS_PORT, "rx_brb_truncate" },
    { STATS_OFFSET32(pause_frames_received_hi),
                8, STATS_FLAGS_PORT, "rx_pause_frames" },
    { STATS_OFFSET32(rx_stat_maccontrolframesreceived_hi),
                8, STATS_FLAGS_PORT, "rx_mac_ctrl_frames" },
    { STATS_OFFSET32(nig_timer_max),
                4, STATS_FLAGS_PORT, "rx_constant_pause_events" },
    { STATS_OFFSET32(total_bytes_transmitted_hi),
                8, STATS_FLAGS_BOTH, "tx_bytes" },
    { STATS_OFFSET32(tx_stat_ifhcoutbadoctets_hi),
                8, STATS_FLAGS_PORT, "tx_error_bytes" },
    { STATS_OFFSET32(total_unicast_packets_transmitted_hi),
                8, STATS_FLAGS_BOTH, "tx_ucast_packets" },
    { STATS_OFFSET32(total_multicast_packets_transmitted_hi),
                8, STATS_FLAGS_BOTH, "tx_mcast_packets" },
    { STATS_OFFSET32(total_broadcast_packets_transmitted_hi),
                8, STATS_FLAGS_BOTH, "tx_bcast_packets" },
    { STATS_OFFSET32(tx_stat_dot3statsinternalmactransmiterrors_hi),
                8, STATS_FLAGS_PORT, "tx_mac_errors" },
    { STATS_OFFSET32(rx_stat_dot3statscarriersenseerrors_hi),
                8, STATS_FLAGS_PORT, "tx_carrier_errors" },
    { STATS_OFFSET32(tx_stat_dot3statssinglecollisionframes_hi),
                8, STATS_FLAGS_PORT, "tx_single_collisions" },
    { STATS_OFFSET32(tx_stat_dot3statsmultiplecollisionframes_hi),
                8, STATS_FLAGS_PORT, "tx_multi_collisions" },
    { STATS_OFFSET32(tx_stat_dot3statsdeferredtransmissions_hi),
                8, STATS_FLAGS_PORT, "tx_deferred" },
    { STATS_OFFSET32(tx_stat_dot3statsexcessivecollisions_hi),
                8, STATS_FLAGS_PORT, "tx_excess_collisions" },
    { STATS_OFFSET32(tx_stat_dot3statslatecollisions_hi),
                8, STATS_FLAGS_PORT, "tx_late_collisions" },
    { STATS_OFFSET32(tx_stat_etherstatscollisions_hi),
                8, STATS_FLAGS_PORT, "tx_total_collisions" },
    { STATS_OFFSET32(tx_stat_etherstatspkts64octets_hi),
                8, STATS_FLAGS_PORT, "tx_64_byte_packets" },
    { STATS_OFFSET32(tx_stat_etherstatspkts65octetsto127octets_hi),
                8, STATS_FLAGS_PORT, "tx_65_to_127_byte_packets" },
    { STATS_OFFSET32(tx_stat_etherstatspkts128octetsto255octets_hi),
                8, STATS_FLAGS_PORT, "tx_128_to_255_byte_packets" },
    { STATS_OFFSET32(tx_stat_etherstatspkts256octetsto511octets_hi),
                8, STATS_FLAGS_PORT, "tx_256_to_511_byte_packets" },
    { STATS_OFFSET32(tx_stat_etherstatspkts512octetsto1023octets_hi),
                8, STATS_FLAGS_PORT, "tx_512_to_1023_byte_packets" },
    { STATS_OFFSET32(etherstatspkts1024octetsto1522octets_hi),
                8, STATS_FLAGS_PORT, "tx_1024_to_1522_byte_packets" },
    { STATS_OFFSET32(etherstatspktsover1522octets_hi),
                8, STATS_FLAGS_PORT, "tx_1523_to_9022_byte_packets" },
    { STATS_OFFSET32(pause_frames_sent_hi),
                8, STATS_FLAGS_PORT, "tx_pause_frames" },
    { STATS_OFFSET32(total_tpa_aggregations_hi),
                8, STATS_FLAGS_FUNC, "tpa_aggregations" },
    { STATS_OFFSET32(total_tpa_aggregated_frames_hi),
                8, STATS_FLAGS_FUNC, "tpa_aggregated_frames"},
    { STATS_OFFSET32(total_tpa_bytes_hi),
                8, STATS_FLAGS_FUNC, "tpa_bytes"},
    { STATS_OFFSET32(eee_tx_lpi),
                4, STATS_FLAGS_PORT, "eee_tx_lpi"},
    { STATS_OFFSET32(rx_calls),
                4, STATS_FLAGS_FUNC, "rx_calls"},
    { STATS_OFFSET32(rx_pkts),
                4, STATS_FLAGS_FUNC, "rx_pkts"},
    { STATS_OFFSET32(rx_tpa_pkts),
                4, STATS_FLAGS_FUNC, "rx_tpa_pkts"},
    { STATS_OFFSET32(rx_erroneous_jumbo_sge_pkts),
                4, STATS_FLAGS_FUNC, "rx_erroneous_jumbo_sge_pkts"},
    { STATS_OFFSET32(rx_bxe_service_rxsgl),
                4, STATS_FLAGS_FUNC, "rx_bxe_service_rxsgl"},
    { STATS_OFFSET32(rx_jumbo_sge_pkts),
                4, STATS_FLAGS_FUNC, "rx_jumbo_sge_pkts"},
    { STATS_OFFSET32(rx_soft_errors),
                4, STATS_FLAGS_FUNC, "rx_soft_errors"},
    { STATS_OFFSET32(rx_hw_csum_errors),
                4, STATS_FLAGS_FUNC, "rx_hw_csum_errors"},
    { STATS_OFFSET32(rx_ofld_frames_csum_ip),
                4, STATS_FLAGS_FUNC, "rx_ofld_frames_csum_ip"},
    { STATS_OFFSET32(rx_ofld_frames_csum_tcp_udp),
                4, STATS_FLAGS_FUNC, "rx_ofld_frames_csum_tcp_udp"},
    { STATS_OFFSET32(rx_budget_reached),
                4, STATS_FLAGS_FUNC, "rx_budget_reached"},
    { STATS_OFFSET32(tx_pkts),
                4, STATS_FLAGS_FUNC, "tx_pkts"},
    { STATS_OFFSET32(tx_soft_errors),
                4, STATS_FLAGS_FUNC, "tx_soft_errors"},
    { STATS_OFFSET32(tx_ofld_frames_csum_ip),
                4, STATS_FLAGS_FUNC, "tx_ofld_frames_csum_ip"},
    { STATS_OFFSET32(tx_ofld_frames_csum_tcp),
                4, STATS_FLAGS_FUNC, "tx_ofld_frames_csum_tcp"},
    { STATS_OFFSET32(tx_ofld_frames_csum_udp),
                4, STATS_FLAGS_FUNC, "tx_ofld_frames_csum_udp"},
    { STATS_OFFSET32(tx_ofld_frames_lso),
                4, STATS_FLAGS_FUNC, "tx_ofld_frames_lso"},
    { STATS_OFFSET32(tx_ofld_frames_lso_hdr_splits),
                4, STATS_FLAGS_FUNC, "tx_ofld_frames_lso_hdr_splits"},
    { STATS_OFFSET32(tx_encap_failures),
                4, STATS_FLAGS_FUNC, "tx_encap_failures"},
    { STATS_OFFSET32(tx_hw_queue_full),
                4, STATS_FLAGS_FUNC, "tx_hw_queue_full"},
    { STATS_OFFSET32(tx_hw_max_queue_depth),
                4, STATS_FLAGS_FUNC, "tx_hw_max_queue_depth"},
    { STATS_OFFSET32(tx_dma_mapping_failure),
                4, STATS_FLAGS_FUNC, "tx_dma_mapping_failure"},
    { STATS_OFFSET32(tx_max_drbr_queue_depth),
                4, STATS_FLAGS_FUNC, "tx_max_drbr_queue_depth"},
    { STATS_OFFSET32(tx_window_violation_std),
                4, STATS_FLAGS_FUNC, "tx_window_violation_std"},
    { STATS_OFFSET32(tx_window_violation_tso),
                4, STATS_FLAGS_FUNC, "tx_window_violation_tso"},
    { STATS_OFFSET32(tx_chain_lost_mbuf),
                4, STATS_FLAGS_FUNC, "tx_chain_lost_mbuf"},
    { STATS_OFFSET32(tx_frames_deferred),
                4, STATS_FLAGS_FUNC, "tx_frames_deferred"},
    { STATS_OFFSET32(tx_queue_xoff),
                4, STATS_FLAGS_FUNC, "tx_queue_xoff"},
    { STATS_OFFSET32(mbuf_defrag_attempts),
                4, STATS_FLAGS_FUNC, "mbuf_defrag_attempts"},
    { STATS_OFFSET32(mbuf_defrag_failures),
                4, STATS_FLAGS_FUNC, "mbuf_defrag_failures"},
    { STATS_OFFSET32(mbuf_rx_bd_alloc_failed),
                4, STATS_FLAGS_FUNC, "mbuf_rx_bd_alloc_failed"},
    { STATS_OFFSET32(mbuf_rx_bd_mapping_failed),
                4, STATS_FLAGS_FUNC, "mbuf_rx_bd_mapping_failed"},
    { STATS_OFFSET32(mbuf_rx_tpa_alloc_failed),
                4, STATS_FLAGS_FUNC, "mbuf_rx_tpa_alloc_failed"},
    { STATS_OFFSET32(mbuf_rx_tpa_mapping_failed),
                4, STATS_FLAGS_FUNC, "mbuf_rx_tpa_mapping_failed"},
    { STATS_OFFSET32(mbuf_rx_sge_alloc_failed),
                4, STATS_FLAGS_FUNC, "mbuf_rx_sge_alloc_failed"},
    { STATS_OFFSET32(mbuf_rx_sge_mapping_failed),
                4, STATS_FLAGS_FUNC, "mbuf_rx_sge_mapping_failed"},
    { STATS_OFFSET32(mbuf_alloc_tx),
                4, STATS_FLAGS_FUNC, "mbuf_alloc_tx"},
    { STATS_OFFSET32(mbuf_alloc_rx),
                4, STATS_FLAGS_FUNC, "mbuf_alloc_rx"},
    { STATS_OFFSET32(mbuf_alloc_sge),
                4, STATS_FLAGS_FUNC, "mbuf_alloc_sge"},
    { STATS_OFFSET32(mbuf_alloc_tpa),
                4, STATS_FLAGS_FUNC, "mbuf_alloc_tpa"},
    { STATS_OFFSET32(tx_queue_full_return),
                4, STATS_FLAGS_FUNC, "tx_queue_full_return"},
    { STATS_OFFSET32(bxe_tx_mq_sc_state_failures),
                4, STATS_FLAGS_FUNC, "bxe_tx_mq_sc_state_failures"},
    { STATS_OFFSET32(tx_request_link_down_failures),
                4, STATS_FLAGS_FUNC, "tx_request_link_down_failures"},
    { STATS_OFFSET32(bd_avail_too_less_failures),
                4, STATS_FLAGS_FUNC, "bd_avail_too_less_failures"},
    { STATS_OFFSET32(tx_mq_not_empty),
                4, STATS_FLAGS_FUNC, "tx_mq_not_empty"},
    { STATS_OFFSET32(nsegs_path1_errors),
                4, STATS_FLAGS_FUNC, "nsegs_path1_errors"},
    { STATS_OFFSET32(nsegs_path2_errors),
                4, STATS_FLAGS_FUNC, "nsegs_path2_errors"}


};

static const struct {
    uint32_t offset;
    uint32_t size;
    char string[STAT_NAME_LEN];
} bxe_eth_q_stats_arr[] = {
    { Q_STATS_OFFSET32(total_bytes_received_hi),
                8, "rx_bytes" },
    { Q_STATS_OFFSET32(total_unicast_packets_received_hi),
                8, "rx_ucast_packets" },
    { Q_STATS_OFFSET32(total_multicast_packets_received_hi),
                8, "rx_mcast_packets" },
    { Q_STATS_OFFSET32(total_broadcast_packets_received_hi),
                8, "rx_bcast_packets" },
    { Q_STATS_OFFSET32(no_buff_discard_hi),
                8, "rx_discards" },
    { Q_STATS_OFFSET32(total_bytes_transmitted_hi),
                8, "tx_bytes" },
    { Q_STATS_OFFSET32(total_unicast_packets_transmitted_hi),
                8, "tx_ucast_packets" },
    { Q_STATS_OFFSET32(total_multicast_packets_transmitted_hi),
                8, "tx_mcast_packets" },
    { Q_STATS_OFFSET32(total_broadcast_packets_transmitted_hi),
                8, "tx_bcast_packets" },
    { Q_STATS_OFFSET32(total_tpa_aggregations_hi),
                8, "tpa_aggregations" },
    { Q_STATS_OFFSET32(total_tpa_aggregated_frames_hi),
                8, "tpa_aggregated_frames"},
    { Q_STATS_OFFSET32(total_tpa_bytes_hi),
                8, "tpa_bytes"},
    { Q_STATS_OFFSET32(rx_calls),
                4, "rx_calls"},
    { Q_STATS_OFFSET32(rx_pkts),
                4, "rx_pkts"},
    { Q_STATS_OFFSET32(rx_tpa_pkts),
                4, "rx_tpa_pkts"},
    { Q_STATS_OFFSET32(rx_erroneous_jumbo_sge_pkts),
                4, "rx_erroneous_jumbo_sge_pkts"},
    { Q_STATS_OFFSET32(rx_bxe_service_rxsgl),
                4, "rx_bxe_service_rxsgl"},
    { Q_STATS_OFFSET32(rx_jumbo_sge_pkts),
                4, "rx_jumbo_sge_pkts"},
    { Q_STATS_OFFSET32(rx_soft_errors),
                4, "rx_soft_errors"},
    { Q_STATS_OFFSET32(rx_hw_csum_errors),
                4, "rx_hw_csum_errors"},
    { Q_STATS_OFFSET32(rx_ofld_frames_csum_ip),
                4, "rx_ofld_frames_csum_ip"},
    { Q_STATS_OFFSET32(rx_ofld_frames_csum_tcp_udp),
                4, "rx_ofld_frames_csum_tcp_udp"},
    { Q_STATS_OFFSET32(rx_budget_reached),
                4, "rx_budget_reached"},
    { Q_STATS_OFFSET32(tx_pkts),
                4, "tx_pkts"},
    { Q_STATS_OFFSET32(tx_soft_errors),
                4, "tx_soft_errors"},
    { Q_STATS_OFFSET32(tx_ofld_frames_csum_ip),
                4, "tx_ofld_frames_csum_ip"},
    { Q_STATS_OFFSET32(tx_ofld_frames_csum_tcp),
                4, "tx_ofld_frames_csum_tcp"},
    { Q_STATS_OFFSET32(tx_ofld_frames_csum_udp),
                4, "tx_ofld_frames_csum_udp"},
    { Q_STATS_OFFSET32(tx_ofld_frames_lso),
                4, "tx_ofld_frames_lso"},
    { Q_STATS_OFFSET32(tx_ofld_frames_lso_hdr_splits),
                4, "tx_ofld_frames_lso_hdr_splits"},
    { Q_STATS_OFFSET32(tx_encap_failures),
                4, "tx_encap_failures"},
    { Q_STATS_OFFSET32(tx_hw_queue_full),
                4, "tx_hw_queue_full"},
    { Q_STATS_OFFSET32(tx_hw_max_queue_depth),
                4, "tx_hw_max_queue_depth"},
    { Q_STATS_OFFSET32(tx_dma_mapping_failure),
                4, "tx_dma_mapping_failure"},
    { Q_STATS_OFFSET32(tx_max_drbr_queue_depth),
                4, "tx_max_drbr_queue_depth"},
    { Q_STATS_OFFSET32(tx_window_violation_std),
                4, "tx_window_violation_std"},
    { Q_STATS_OFFSET32(tx_window_violation_tso),
                4, "tx_window_violation_tso"},
    { Q_STATS_OFFSET32(tx_chain_lost_mbuf),
                4, "tx_chain_lost_mbuf"},
    { Q_STATS_OFFSET32(tx_frames_deferred),
                4, "tx_frames_deferred"},
    { Q_STATS_OFFSET32(tx_queue_xoff),
                4, "tx_queue_xoff"},
    { Q_STATS_OFFSET32(mbuf_defrag_attempts),
                4, "mbuf_defrag_attempts"},
    { Q_STATS_OFFSET32(mbuf_defrag_failures),
                4, "mbuf_defrag_failures"},
    { Q_STATS_OFFSET32(mbuf_rx_bd_alloc_failed),
                4, "mbuf_rx_bd_alloc_failed"},
    { Q_STATS_OFFSET32(mbuf_rx_bd_mapping_failed),
                4, "mbuf_rx_bd_mapping_failed"},
    { Q_STATS_OFFSET32(mbuf_rx_tpa_alloc_failed),
                4, "mbuf_rx_tpa_alloc_failed"},
    { Q_STATS_OFFSET32(mbuf_rx_tpa_mapping_failed),
                4, "mbuf_rx_tpa_mapping_failed"},
    { Q_STATS_OFFSET32(mbuf_rx_sge_alloc_failed),
                4, "mbuf_rx_sge_alloc_failed"},
    { Q_STATS_OFFSET32(mbuf_rx_sge_mapping_failed),
                4, "mbuf_rx_sge_mapping_failed"},
    { Q_STATS_OFFSET32(mbuf_alloc_tx),
                4, "mbuf_alloc_tx"},
    { Q_STATS_OFFSET32(mbuf_alloc_rx),
                4, "mbuf_alloc_rx"},
    { Q_STATS_OFFSET32(mbuf_alloc_sge),
                4, "mbuf_alloc_sge"},
    { Q_STATS_OFFSET32(mbuf_alloc_tpa),
                4, "mbuf_alloc_tpa"},
    { Q_STATS_OFFSET32(tx_queue_full_return),
                4, "tx_queue_full_return"},
    { Q_STATS_OFFSET32(bxe_tx_mq_sc_state_failures),
                4, "bxe_tx_mq_sc_state_failures"},
    { Q_STATS_OFFSET32(tx_request_link_down_failures),
                4, "tx_request_link_down_failures"},
    { Q_STATS_OFFSET32(bd_avail_too_less_failures),
                4, "bd_avail_too_less_failures"},
    { Q_STATS_OFFSET32(tx_mq_not_empty),
                4, "tx_mq_not_empty"},
    { Q_STATS_OFFSET32(nsegs_path1_errors),
                4, "nsegs_path1_errors"},
    { Q_STATS_OFFSET32(nsegs_path2_errors),
                4, "nsegs_path2_errors"}


};

#define BXE_NUM_ETH_STATS   ARRAY_SIZE(bxe_eth_stats_arr)
#define BXE_NUM_ETH_Q_STATS ARRAY_SIZE(bxe_eth_q_stats_arr)


static void    bxe_cmng_fns_init(struct bxe_softc *sc,
                                 uint8_t          read_cfg,
                                 uint8_t          cmng_type);
static int     bxe_get_cmng_fns_mode(struct bxe_softc *sc);
static void    storm_memset_cmng(struct bxe_softc *sc,
                                 struct cmng_init *cmng,
                                 uint8_t          port);
static void    bxe_set_reset_global(struct bxe_softc *sc);
static void    bxe_set_reset_in_progress(struct bxe_softc *sc);
static uint8_t bxe_reset_is_done(struct bxe_softc *sc,
                                 int              engine);
static uint8_t bxe_clear_pf_load(struct bxe_softc *sc);
static uint8_t bxe_chk_parity_attn(struct bxe_softc *sc,
                                   uint8_t          *global,
                                   uint8_t          print);
static void    bxe_int_disable(struct bxe_softc *sc);
static int     bxe_release_leader_lock(struct bxe_softc *sc);
static void    bxe_pf_disable(struct bxe_softc *sc);
static void    bxe_free_fp_buffers(struct bxe_softc *sc);
static inline void bxe_update_rx_prod(struct bxe_softc    *sc,
                                      struct bxe_fastpath *fp,
                                      uint16_t            rx_bd_prod,
                                      uint16_t            rx_cq_prod,
                                      uint16_t            rx_sge_prod);
static void    bxe_link_report_locked(struct bxe_softc *sc);
static void    bxe_link_report(struct bxe_softc *sc);
static void    bxe_link_status_update(struct bxe_softc *sc);
static void    bxe_periodic_callout_func(void *xsc);
static void    bxe_periodic_start(struct bxe_softc *sc);
static void    bxe_periodic_stop(struct bxe_softc *sc);
static int     bxe_alloc_rx_bd_mbuf(struct bxe_fastpath *fp,
                                    uint16_t prev_index,
                                    uint16_t index);
static int     bxe_alloc_rx_tpa_mbuf(struct bxe_fastpath *fp,
                                     int                 queue);
static int     bxe_alloc_rx_sge_mbuf(struct bxe_fastpath *fp,
                                     uint16_t            index);
static uint8_t bxe_txeof(struct bxe_softc *sc,
                         struct bxe_fastpath *fp);
static void    bxe_task_fp(struct bxe_fastpath *fp);
static __noinline void bxe_dump_mbuf(struct bxe_softc *sc,
                                     struct mbuf      *m,
                                     uint8_t          contents);
static int     bxe_alloc_mem(struct bxe_softc *sc);
static void    bxe_free_mem(struct bxe_softc *sc);
static int     bxe_alloc_fw_stats_mem(struct bxe_softc *sc);
static void    bxe_free_fw_stats_mem(struct bxe_softc *sc);
static int     bxe_interrupt_attach(struct bxe_softc *sc);
static void    bxe_interrupt_detach(struct bxe_softc *sc);
static void    bxe_set_rx_mode(struct bxe_softc *sc);
static int     bxe_init_locked(struct bxe_softc *sc);
static int     bxe_stop_locked(struct bxe_softc *sc);
static void    bxe_sp_err_timeout_task(void *arg, int pending);
void           bxe_parity_recover(struct bxe_softc *sc);
void           bxe_handle_error(struct bxe_softc *sc);
static __noinline int bxe_nic_load(struct bxe_softc *sc,
                                   int              load_mode);
static __noinline int bxe_nic_unload(struct bxe_softc *sc,
                                     uint32_t         unload_mode,
                                     uint8_t          keep_link);

static void bxe_handle_sp_tq(void *context, int pending);
static void bxe_handle_fp_tq(void *context, int pending);

static int bxe_add_cdev(struct bxe_softc *sc);
static void bxe_del_cdev(struct bxe_softc *sc);
int bxe_grc_dump(struct bxe_softc *sc);
static int bxe_alloc_buf_rings(struct bxe_softc *sc);
static void bxe_free_buf_rings(struct bxe_softc *sc);

/* calculate crc32 on a buffer (NOTE: crc32_length MUST be aligned to 8) */
uint32_t
calc_crc32(uint8_t  *crc32_packet,
           uint32_t crc32_length,
           uint32_t crc32_seed,
           uint8_t  complement)
{
   uint32_t byte         = 0;
   uint32_t bit          = 0;
   uint8_t  msb          = 0;
   uint32_t temp         = 0;
   uint32_t shft         = 0;
   uint8_t  current_byte = 0;
   uint32_t crc32_result = crc32_seed;
   const uint32_t CRC32_POLY = 0x1edc6f41;

   if ((crc32_packet == NULL) ||
       (crc32_length == 0) ||
       ((crc32_length % 8) != 0))
    {
        return (crc32_result);
    }

    for (byte = 0; byte < crc32_length; byte = byte + 1)
    {
        current_byte = crc32_packet[byte];
        for (bit = 0; bit < 8; bit = bit + 1)
        {
            /* msb = crc32_result[31]; */
            msb = (uint8_t)(crc32_result >> 31);

            crc32_result = crc32_result << 1;

            /* it (msb != current_byte[bit]) */
            if (msb != (0x1 & (current_byte >> bit)))
            {
                crc32_result = crc32_result ^ CRC32_POLY;
                /* crc32_result[0] = 1 */
                crc32_result |= 1;
            }
        }
    }

    /* Last step is to:
     * 1. "mirror" every bit
     * 2. swap the 4 bytes
     * 3. complement each bit
     */

    /* Mirror */
    temp = crc32_result;
    shft = sizeof(crc32_result) * 8 - 1;

    for (crc32_result >>= 1; crc32_result; crc32_result >>= 1)
    {
        temp <<= 1;
        temp |= crc32_result & 1;
        shft-- ;
    }

    /* temp[31-bit] = crc32_result[bit] */
    temp <<= shft;

    /* Swap */
    /* crc32_result = {temp[7:0], temp[15:8], temp[23:16], temp[31:24]} */
    {
        uint32_t t0, t1, t2, t3;
        t0 = (0x000000ff & (temp >> 24));
        t1 = (0x0000ff00 & (temp >> 8));
        t2 = (0x00ff0000 & (temp << 8));
        t3 = (0xff000000 & (temp << 24));
        crc32_result = t0 | t1 | t2 | t3;
    }

    /* Complement */
    if (complement)
    {
        crc32_result = ~crc32_result;
    }

    return (crc32_result);
}

int
bxe_test_bit(int                    nr,
             volatile unsigned long *addr)
{
    return ((atomic_load_acq_long(addr) & (1 << nr)) != 0);
}

void
bxe_set_bit(unsigned int           nr,
            volatile unsigned long *addr)
{
    atomic_set_acq_long(addr, (1 << nr));
}

void
bxe_clear_bit(int                    nr,
              volatile unsigned long *addr)
{
    atomic_clear_acq_long(addr, (1 << nr));
}

int
bxe_test_and_set_bit(int                    nr,
                       volatile unsigned long *addr)
{
    unsigned long x;
    nr = (1 << nr);
    do {
        x = *addr;
    } while (atomic_cmpset_acq_long(addr, x, x | nr) == 0);
    // if (x & nr) bit_was_set; else bit_was_not_set;
    return (x & nr);
}

int
bxe_test_and_clear_bit(int                    nr,
                       volatile unsigned long *addr)
{
    unsigned long x;
    nr = (1 << nr);
    do {
        x = *addr;
    } while (atomic_cmpset_acq_long(addr, x, x & ~nr) == 0);
    // if (x & nr) bit_was_set; else bit_was_not_set;
    return (x & nr);
}

int
bxe_cmpxchg(volatile int *addr,
            int          old,
            int          new)
{
    int x;
    do {
        x = *addr;
    } while (atomic_cmpset_acq_int(addr, old, new) == 0);
    return (x);
}

/*
 * Get DMA memory from the OS.
 *
 * Validates that the OS has provided DMA buffers in response to a
 * bus_dmamap_load call and saves the physical address of those buffers.
 * When the callback is used the OS will return 0 for the mapping function
 * (bus_dmamap_load) so we use the value of map_arg->maxsegs to pass any
 * failures back to the caller.
 *
 * Returns:
 *   Nothing.
 */
static void
bxe_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct bxe_dma *dma = arg;

    if (error) {
        dma->paddr = 0;
        dma->nseg  = 0;
        BLOGE(dma->sc, "Failed DMA alloc '%s' (%d)!\n", dma->msg, error);
    } else {
        dma->paddr = segs->ds_addr;
        dma->nseg  = nseg;
    }
}

/*
 * Allocate a block of memory and map it for DMA. No partial completions
 * allowed and release any resources acquired if we can't acquire all
 * resources.
 *
 * Returns:
 *   0 = Success, !0 = Failure
 */
int
bxe_dma_alloc(struct bxe_softc *sc,
              bus_size_t       size,
              struct bxe_dma   *dma,
              const char       *msg)
{
    int rc;

    if (dma->size > 0) {
        BLOGE(sc, "dma block '%s' already has size %lu\n", msg,
              (unsigned long)dma->size);
        return (1);
    }

    memset(dma, 0, sizeof(*dma)); /* sanity */
    dma->sc   = sc;
    dma->size = size;
    snprintf(dma->msg, sizeof(dma->msg), "%s", msg);

    rc = bus_dma_tag_create(sc->parent_dma_tag, /* parent tag */
                            BCM_PAGE_SIZE,      /* alignment */
                            0,                  /* boundary limit */
                            BUS_SPACE_MAXADDR,  /* restricted low */
                            BUS_SPACE_MAXADDR,  /* restricted hi */
                            NULL,               /* addr filter() */
                            NULL,               /* addr filter() arg */
                            size,               /* max map size */
                            1,                  /* num discontinuous */
                            size,               /* max seg size */
                            BUS_DMA_ALLOCNOW,   /* flags */
                            NULL,               /* lock() */
                            NULL,               /* lock() arg */
                            &dma->tag);         /* returned dma tag */
    if (rc != 0) {
        BLOGE(sc, "Failed to create dma tag for '%s' (%d)\n", msg, rc);
        memset(dma, 0, sizeof(*dma));
        return (1);
    }

    rc = bus_dmamem_alloc(dma->tag,
                          (void **)&dma->vaddr,
                          (BUS_DMA_NOWAIT | BUS_DMA_ZERO),
                          &dma->map);
    if (rc != 0) {
        BLOGE(sc, "Failed to alloc dma mem for '%s' (%d)\n", msg, rc);
        bus_dma_tag_destroy(dma->tag);
        memset(dma, 0, sizeof(*dma));
        return (1);
    }

    rc = bus_dmamap_load(dma->tag,
                         dma->map,
                         dma->vaddr,
                         size,
                         bxe_dma_map_addr, /* BLOGD in here */
                         dma,
                         BUS_DMA_NOWAIT);
    if (rc != 0) {
        BLOGE(sc, "Failed to load dma map for '%s' (%d)\n", msg, rc);
        bus_dmamem_free(dma->tag, dma->vaddr, dma->map);
        bus_dma_tag_destroy(dma->tag);
        memset(dma, 0, sizeof(*dma));
        return (1);
    }

    return (0);
}

void
bxe_dma_free(struct bxe_softc *sc,
             struct bxe_dma   *dma)
{
    if (dma->size > 0) {
        DBASSERT(sc, (dma->tag != NULL), ("dma tag is NULL"));

        bus_dmamap_sync(dma->tag, dma->map,
                        (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE));
        bus_dmamap_unload(dma->tag, dma->map);
        bus_dmamem_free(dma->tag, dma->vaddr, dma->map);
        bus_dma_tag_destroy(dma->tag);
    }

    memset(dma, 0, sizeof(*dma));
}

/*
 * These indirect read and write routines are only during init.
 * The locking is handled by the MCP.
 */

void
bxe_reg_wr_ind(struct bxe_softc *sc,
               uint32_t         addr,
               uint32_t         val)
{
    pci_write_config(sc->dev, PCICFG_GRC_ADDRESS, addr, 4);
    pci_write_config(sc->dev, PCICFG_GRC_DATA, val, 4);
    pci_write_config(sc->dev, PCICFG_GRC_ADDRESS, 0, 4);
}

uint32_t
bxe_reg_rd_ind(struct bxe_softc *sc,
               uint32_t         addr)
{
    uint32_t val;

    pci_write_config(sc->dev, PCICFG_GRC_ADDRESS, addr, 4);
    val = pci_read_config(sc->dev, PCICFG_GRC_DATA, 4);
    pci_write_config(sc->dev, PCICFG_GRC_ADDRESS, 0, 4);

    return (val);
}

static int
bxe_acquire_hw_lock(struct bxe_softc *sc,
                    uint32_t         resource)
{
    uint32_t lock_status;
    uint32_t resource_bit = (1 << resource);
    int func = SC_FUNC(sc);
    uint32_t hw_lock_control_reg;
    int cnt;

    /* validate the resource is within range */
    if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
        BLOGE(sc, "(resource 0x%x > HW_LOCK_MAX_RESOURCE_VALUE)"
            " resource_bit 0x%x\n", resource, resource_bit);
        return (-1);
    }

    if (func <= 5) {
        hw_lock_control_reg = (MISC_REG_DRIVER_CONTROL_1 + (func * 8));
    } else {
        hw_lock_control_reg =
                (MISC_REG_DRIVER_CONTROL_7 + ((func - 6) * 8));
    }

    /* validate the resource is not already taken */
    lock_status = REG_RD(sc, hw_lock_control_reg);
    if (lock_status & resource_bit) {
        BLOGE(sc, "resource (0x%x) in use (status 0x%x bit 0x%x)\n",
              resource, lock_status, resource_bit);
        return (-1);
    }

    /* try every 5ms for 5 seconds */
    for (cnt = 0; cnt < 1000; cnt++) {
        REG_WR(sc, (hw_lock_control_reg + 4), resource_bit);
        lock_status = REG_RD(sc, hw_lock_control_reg);
        if (lock_status & resource_bit) {
            return (0);
        }
        DELAY(5000);
    }

    BLOGE(sc, "Resource 0x%x resource_bit 0x%x lock timeout!\n",
        resource, resource_bit);
    return (-1);
}

static int
bxe_release_hw_lock(struct bxe_softc *sc,
                    uint32_t         resource)
{
    uint32_t lock_status;
    uint32_t resource_bit = (1 << resource);
    int func = SC_FUNC(sc);
    uint32_t hw_lock_control_reg;

    /* validate the resource is within range */
    if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
        BLOGE(sc, "(resource 0x%x > HW_LOCK_MAX_RESOURCE_VALUE)"
            " resource_bit 0x%x\n", resource, resource_bit);
        return (-1);
    }

    if (func <= 5) {
        hw_lock_control_reg = (MISC_REG_DRIVER_CONTROL_1 + (func * 8));
    } else {
        hw_lock_control_reg =
                (MISC_REG_DRIVER_CONTROL_7 + ((func - 6) * 8));
    }

    /* validate the resource is currently taken */
    lock_status = REG_RD(sc, hw_lock_control_reg);
    if (!(lock_status & resource_bit)) {
        BLOGE(sc, "resource (0x%x) not in use (status 0x%x bit 0x%x)\n",
              resource, lock_status, resource_bit);
        return (-1);
    }

    REG_WR(sc, hw_lock_control_reg, resource_bit);
    return (0);
}
static void bxe_acquire_phy_lock(struct bxe_softc *sc)
{
	BXE_PHY_LOCK(sc);
	bxe_acquire_hw_lock(sc,HW_LOCK_RESOURCE_MDIO); 
}

static void bxe_release_phy_lock(struct bxe_softc *sc)
{
	bxe_release_hw_lock(sc,HW_LOCK_RESOURCE_MDIO); 
	BXE_PHY_UNLOCK(sc);
}
/*
 * Per pf misc lock must be acquired before the per port mcp lock. Otherwise,
 * had we done things the other way around, if two pfs from the same port
 * would attempt to access nvram at the same time, we could run into a
 * scenario such as:
 * pf A takes the port lock.
 * pf B succeeds in taking the same lock since they are from the same port.
 * pf A takes the per pf misc lock. Performs eeprom access.
 * pf A finishes. Unlocks the per pf misc lock.
 * Pf B takes the lock and proceeds to perform it's own access.
 * pf A unlocks the per port lock, while pf B is still working (!).
 * mcp takes the per port lock and corrupts pf B's access (and/or has it's own
 * access corrupted by pf B).*
 */
static int
bxe_acquire_nvram_lock(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    int count, i;
    uint32_t val = 0;

    /* acquire HW lock: protect against other PFs in PF Direct Assignment */
    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_NVRAM);

    /* adjust timeout for emulation/FPGA */
    count = NVRAM_TIMEOUT_COUNT;
    if (CHIP_REV_IS_SLOW(sc)) {
        count *= 100;
    }

    /* request access to nvram interface */
    REG_WR(sc, MCP_REG_MCPR_NVM_SW_ARB,
           (MCPR_NVM_SW_ARB_ARB_REQ_SET1 << port));

    for (i = 0; i < count*10; i++) {
        val = REG_RD(sc, MCP_REG_MCPR_NVM_SW_ARB);
        if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)) {
            break;
        }

        DELAY(5);
    }

    if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))) {
        BLOGE(sc, "Cannot get access to nvram interface "
            "port %d val 0x%x (MCPR_NVM_SW_ARB_ARB_ARB1 << port)\n",
            port, val);
        return (-1);
    }

    return (0);
}

static int
bxe_release_nvram_lock(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    int count, i;
    uint32_t val = 0;

    /* adjust timeout for emulation/FPGA */
    count = NVRAM_TIMEOUT_COUNT;
    if (CHIP_REV_IS_SLOW(sc)) {
        count *= 100;
    }

    /* relinquish nvram interface */
    REG_WR(sc, MCP_REG_MCPR_NVM_SW_ARB,
           (MCPR_NVM_SW_ARB_ARB_REQ_CLR1 << port));

    for (i = 0; i < count*10; i++) {
        val = REG_RD(sc, MCP_REG_MCPR_NVM_SW_ARB);
        if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))) {
            break;
        }

        DELAY(5);
    }

    if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)) {
        BLOGE(sc, "Cannot free access to nvram interface "
            "port %d val 0x%x (MCPR_NVM_SW_ARB_ARB_ARB1 << port)\n",
            port, val);
        return (-1);
    }

    /* release HW lock: protect against other PFs in PF Direct Assignment */
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_NVRAM);

    return (0);
}

static void
bxe_enable_nvram_access(struct bxe_softc *sc)
{
    uint32_t val;

    val = REG_RD(sc, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

    /* enable both bits, even on read */
    REG_WR(sc, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
           (val | MCPR_NVM_ACCESS_ENABLE_EN | MCPR_NVM_ACCESS_ENABLE_WR_EN));
}

static void
bxe_disable_nvram_access(struct bxe_softc *sc)
{
    uint32_t val;

    val = REG_RD(sc, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

    /* disable both bits, even after read */
    REG_WR(sc, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
           (val & ~(MCPR_NVM_ACCESS_ENABLE_EN |
                    MCPR_NVM_ACCESS_ENABLE_WR_EN)));
}

static int
bxe_nvram_read_dword(struct bxe_softc *sc,
                     uint32_t         offset,
                     uint32_t         *ret_val,
                     uint32_t         cmd_flags)
{
    int count, i, rc;
    uint32_t val;

    /* build the command word */
    cmd_flags |= MCPR_NVM_COMMAND_DOIT;

    /* need to clear DONE bit separately */
    REG_WR(sc, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

    /* address of the NVRAM to read from */
    REG_WR(sc, MCP_REG_MCPR_NVM_ADDR,
           (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

    /* issue a read command */
    REG_WR(sc, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

    /* adjust timeout for emulation/FPGA */
    count = NVRAM_TIMEOUT_COUNT;
    if (CHIP_REV_IS_SLOW(sc)) {
        count *= 100;
    }

    /* wait for completion */
    *ret_val = 0;
    rc = -1;
    for (i = 0; i < count; i++) {
        DELAY(5);
        val = REG_RD(sc, MCP_REG_MCPR_NVM_COMMAND);

        if (val & MCPR_NVM_COMMAND_DONE) {
            val = REG_RD(sc, MCP_REG_MCPR_NVM_READ);
            /* we read nvram data in cpu order
             * but ethtool sees it as an array of bytes
             * converting to big-endian will do the work
             */
            *ret_val = htobe32(val);
            rc = 0;
            break;
        }
    }

    if (rc == -1) {
        BLOGE(sc, "nvram read timeout expired "
            "(offset 0x%x cmd_flags 0x%x val 0x%x)\n",
            offset, cmd_flags, val);
    }

    return (rc);
}

static int
bxe_nvram_read(struct bxe_softc *sc,
               uint32_t         offset,
               uint8_t          *ret_buf,
               int              buf_size)
{
    uint32_t cmd_flags;
    uint32_t val;
    int rc;

    if ((offset & 0x03) || (buf_size & 0x03) || (buf_size == 0)) {
        BLOGE(sc, "Invalid parameter, offset 0x%x buf_size 0x%x\n",
              offset, buf_size);
        return (-1);
    }

    if ((offset + buf_size) > sc->devinfo.flash_size) {
        BLOGE(sc, "Invalid parameter, "
                  "offset 0x%x + buf_size 0x%x > flash_size 0x%x\n",
              offset, buf_size, sc->devinfo.flash_size);
        return (-1);
    }

    /* request access to nvram interface */
    rc = bxe_acquire_nvram_lock(sc);
    if (rc) {
        return (rc);
    }

    /* enable access to nvram interface */
    bxe_enable_nvram_access(sc);

    /* read the first word(s) */
    cmd_flags = MCPR_NVM_COMMAND_FIRST;
    while ((buf_size > sizeof(uint32_t)) && (rc == 0)) {
        rc = bxe_nvram_read_dword(sc, offset, &val, cmd_flags);
        memcpy(ret_buf, &val, 4);

        /* advance to the next dword */
        offset += sizeof(uint32_t);
        ret_buf += sizeof(uint32_t);
        buf_size -= sizeof(uint32_t);
        cmd_flags = 0;
    }

    if (rc == 0) {
        cmd_flags |= MCPR_NVM_COMMAND_LAST;
        rc = bxe_nvram_read_dword(sc, offset, &val, cmd_flags);
        memcpy(ret_buf, &val, 4);
    }

    /* disable access to nvram interface */
    bxe_disable_nvram_access(sc);
    bxe_release_nvram_lock(sc);

    return (rc);
}

static int
bxe_nvram_write_dword(struct bxe_softc *sc,
                      uint32_t         offset,
                      uint32_t         val,
                      uint32_t         cmd_flags)
{
    int count, i, rc;

    /* build the command word */
    cmd_flags |= (MCPR_NVM_COMMAND_DOIT | MCPR_NVM_COMMAND_WR);

    /* need to clear DONE bit separately */
    REG_WR(sc, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

    /* write the data */
    REG_WR(sc, MCP_REG_MCPR_NVM_WRITE, val);

    /* address of the NVRAM to write to */
    REG_WR(sc, MCP_REG_MCPR_NVM_ADDR,
           (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

    /* issue the write command */
    REG_WR(sc, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

    /* adjust timeout for emulation/FPGA */
    count = NVRAM_TIMEOUT_COUNT;
    if (CHIP_REV_IS_SLOW(sc)) {
        count *= 100;
    }

    /* wait for completion */
    rc = -1;
    for (i = 0; i < count; i++) {
        DELAY(5);
        val = REG_RD(sc, MCP_REG_MCPR_NVM_COMMAND);
        if (val & MCPR_NVM_COMMAND_DONE) {
            rc = 0;
            break;
        }
    }

    if (rc == -1) {
        BLOGE(sc, "nvram write timeout expired "
            "(offset 0x%x cmd_flags 0x%x val 0x%x)\n",
            offset, cmd_flags, val);
    }

    return (rc);
}

#define BYTE_OFFSET(offset) (8 * (offset & 0x03))

static int
bxe_nvram_write1(struct bxe_softc *sc,
                 uint32_t         offset,
                 uint8_t          *data_buf,
                 int              buf_size)
{
    uint32_t cmd_flags;
    uint32_t align_offset;
    uint32_t val;
    int rc;

    if ((offset + buf_size) > sc->devinfo.flash_size) {
        BLOGE(sc, "Invalid parameter, "
                  "offset 0x%x + buf_size 0x%x > flash_size 0x%x\n",
              offset, buf_size, sc->devinfo.flash_size);
        return (-1);
    }

    /* request access to nvram interface */
    rc = bxe_acquire_nvram_lock(sc);
    if (rc) {
        return (rc);
    }

    /* enable access to nvram interface */
    bxe_enable_nvram_access(sc);

    cmd_flags = (MCPR_NVM_COMMAND_FIRST | MCPR_NVM_COMMAND_LAST);
    align_offset = (offset & ~0x03);
    rc = bxe_nvram_read_dword(sc, align_offset, &val, cmd_flags);

    if (rc == 0) {
        val &= ~(0xff << BYTE_OFFSET(offset));
        val |= (*data_buf << BYTE_OFFSET(offset));

        /* nvram data is returned as an array of bytes
         * convert it back to cpu order
         */
        val = be32toh(val);

        rc = bxe_nvram_write_dword(sc, align_offset, val, cmd_flags);
    }

    /* disable access to nvram interface */
    bxe_disable_nvram_access(sc);
    bxe_release_nvram_lock(sc);

    return (rc);
}

static int
bxe_nvram_write(struct bxe_softc *sc,
                uint32_t         offset,
                uint8_t          *data_buf,
                int              buf_size)
{
    uint32_t cmd_flags;
    uint32_t val;
    uint32_t written_so_far;
    int rc;

    if (buf_size == 1) {
        return (bxe_nvram_write1(sc, offset, data_buf, buf_size));
    }

    if ((offset & 0x03) || (buf_size & 0x03) /* || (buf_size == 0) */) {
        BLOGE(sc, "Invalid parameter, offset 0x%x buf_size 0x%x\n",
              offset, buf_size);
        return (-1);
    }

    if (buf_size == 0) {
        return (0); /* nothing to do */
    }

    if ((offset + buf_size) > sc->devinfo.flash_size) {
        BLOGE(sc, "Invalid parameter, "
                  "offset 0x%x + buf_size 0x%x > flash_size 0x%x\n",
              offset, buf_size, sc->devinfo.flash_size);
        return (-1);
    }

    /* request access to nvram interface */
    rc = bxe_acquire_nvram_lock(sc);
    if (rc) {
        return (rc);
    }

    /* enable access to nvram interface */
    bxe_enable_nvram_access(sc);

    written_so_far = 0;
    cmd_flags = MCPR_NVM_COMMAND_FIRST;
    while ((written_so_far < buf_size) && (rc == 0)) {
        if (written_so_far == (buf_size - sizeof(uint32_t))) {
            cmd_flags |= MCPR_NVM_COMMAND_LAST;
        } else if (((offset + 4) % NVRAM_PAGE_SIZE) == 0) {
            cmd_flags |= MCPR_NVM_COMMAND_LAST;
        } else if ((offset % NVRAM_PAGE_SIZE) == 0) {
            cmd_flags |= MCPR_NVM_COMMAND_FIRST;
        }

        memcpy(&val, data_buf, 4);

        rc = bxe_nvram_write_dword(sc, offset, val, cmd_flags);

        /* advance to the next dword */
        offset += sizeof(uint32_t);
        data_buf += sizeof(uint32_t);
        written_so_far += sizeof(uint32_t);
        cmd_flags = 0;
    }

    /* disable access to nvram interface */
    bxe_disable_nvram_access(sc);
    bxe_release_nvram_lock(sc);

    return (rc);
}

/* copy command into DMAE command memory and set DMAE command Go */
void
bxe_post_dmae(struct bxe_softc    *sc,
              struct dmae_cmd *dmae,
              int                 idx)
{
    uint32_t cmd_offset;
    int i;

    cmd_offset = (DMAE_REG_CMD_MEM + (sizeof(struct dmae_cmd) * idx));
    for (i = 0; i < ((sizeof(struct dmae_cmd) / 4)); i++) {
        REG_WR(sc, (cmd_offset + (i * 4)), *(((uint32_t *)dmae) + i));
    }

    REG_WR(sc, dmae_reg_go_c[idx], 1);
}

uint32_t
bxe_dmae_opcode_add_comp(uint32_t opcode,
                         uint8_t  comp_type)
{
    return (opcode | ((comp_type << DMAE_CMD_C_DST_SHIFT) |
                      DMAE_CMD_C_TYPE_ENABLE));
}

uint32_t
bxe_dmae_opcode_clr_src_reset(uint32_t opcode)
{
    return (opcode & ~DMAE_CMD_SRC_RESET);
}

uint32_t
bxe_dmae_opcode(struct bxe_softc *sc,
                uint8_t          src_type,
                uint8_t          dst_type,
                uint8_t          with_comp,
                uint8_t          comp_type)
{
    uint32_t opcode = 0;

    opcode |= ((src_type << DMAE_CMD_SRC_SHIFT) |
               (dst_type << DMAE_CMD_DST_SHIFT));

    opcode |= (DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET);

    opcode |= (SC_PORT(sc) ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0);

    opcode |= ((SC_VN(sc) << DMAE_CMD_E1HVN_SHIFT) |
               (SC_VN(sc) << DMAE_CMD_DST_VN_SHIFT));

    opcode |= (DMAE_COM_SET_ERR << DMAE_CMD_ERR_POLICY_SHIFT);

#ifdef __BIG_ENDIAN
    opcode |= DMAE_CMD_ENDIANITY_B_DW_SWAP;
#else
    opcode |= DMAE_CMD_ENDIANITY_DW_SWAP;
#endif

    if (with_comp) {
        opcode = bxe_dmae_opcode_add_comp(opcode, comp_type);
    }

    return (opcode);
}

static void
bxe_prep_dmae_with_comp(struct bxe_softc    *sc,
                        struct dmae_cmd *dmae,
                        uint8_t             src_type,
                        uint8_t             dst_type)
{
    memset(dmae, 0, sizeof(struct dmae_cmd));

    /* set the opcode */
    dmae->opcode = bxe_dmae_opcode(sc, src_type, dst_type,
                                   TRUE, DMAE_COMP_PCI);

    /* fill in the completion parameters */
    dmae->comp_addr_lo = U64_LO(BXE_SP_MAPPING(sc, wb_comp));
    dmae->comp_addr_hi = U64_HI(BXE_SP_MAPPING(sc, wb_comp));
    dmae->comp_val     = DMAE_COMP_VAL;
}

/* issue a DMAE command over the init channel and wait for completion */
static int
bxe_issue_dmae_with_comp(struct bxe_softc    *sc,
                         struct dmae_cmd *dmae)
{
    uint32_t *wb_comp = BXE_SP(sc, wb_comp);
    int timeout = CHIP_REV_IS_SLOW(sc) ? 400000 : 4000;

    BXE_DMAE_LOCK(sc);

    /* reset completion */
    *wb_comp = 0;

    /* post the command on the channel used for initializations */
    bxe_post_dmae(sc, dmae, INIT_DMAE_C(sc));

    /* wait for completion */
    DELAY(5);

    while ((*wb_comp & ~DMAE_PCI_ERR_FLAG) != DMAE_COMP_VAL) {
        if (!timeout ||
            (sc->recovery_state != BXE_RECOVERY_DONE &&
             sc->recovery_state != BXE_RECOVERY_NIC_LOADING)) {
            BLOGE(sc, "DMAE timeout! *wb_comp 0x%x recovery_state 0x%x\n",
                *wb_comp, sc->recovery_state);
            BXE_DMAE_UNLOCK(sc);
            return (DMAE_TIMEOUT);
        }

        timeout--;
        DELAY(50);
    }

    if (*wb_comp & DMAE_PCI_ERR_FLAG) {
        BLOGE(sc, "DMAE PCI error! *wb_comp 0x%x recovery_state 0x%x\n",
                *wb_comp, sc->recovery_state);
        BXE_DMAE_UNLOCK(sc);
        return (DMAE_PCI_ERROR);
    }

    BXE_DMAE_UNLOCK(sc);
    return (0);
}

void
bxe_read_dmae(struct bxe_softc *sc,
              uint32_t         src_addr,
              uint32_t         len32)
{
    struct dmae_cmd dmae;
    uint32_t *data;
    int i, rc;

    DBASSERT(sc, (len32 <= 4), ("DMAE read length is %d", len32));

    if (!sc->dmae_ready) {
        data = BXE_SP(sc, wb_data[0]);

        for (i = 0; i < len32; i++) {
            data[i] = (CHIP_IS_E1(sc)) ?
                          bxe_reg_rd_ind(sc, (src_addr + (i * 4))) :
                          REG_RD(sc, (src_addr + (i * 4)));
        }

        return;
    }

    /* set opcode and fixed command fields */
    bxe_prep_dmae_with_comp(sc, &dmae, DMAE_SRC_GRC, DMAE_DST_PCI);

    /* fill in addresses and len */
    dmae.src_addr_lo = (src_addr >> 2); /* GRC addr has dword resolution */
    dmae.src_addr_hi = 0;
    dmae.dst_addr_lo = U64_LO(BXE_SP_MAPPING(sc, wb_data));
    dmae.dst_addr_hi = U64_HI(BXE_SP_MAPPING(sc, wb_data));
    dmae.len         = len32;

    /* issue the command and wait for completion */
    if ((rc = bxe_issue_dmae_with_comp(sc, &dmae)) != 0) {
        bxe_panic(sc, ("DMAE failed (%d)\n", rc));
    }
}

void
bxe_write_dmae(struct bxe_softc *sc,
               bus_addr_t       dma_addr,
               uint32_t         dst_addr,
               uint32_t         len32)
{
    struct dmae_cmd dmae;
    int rc;

    if (!sc->dmae_ready) {
        DBASSERT(sc, (len32 <= 4), ("DMAE not ready and length is %d", len32));

        if (CHIP_IS_E1(sc)) {
            ecore_init_ind_wr(sc, dst_addr, BXE_SP(sc, wb_data[0]), len32);
        } else {
            ecore_init_str_wr(sc, dst_addr, BXE_SP(sc, wb_data[0]), len32);
        }

        return;
    }

    /* set opcode and fixed command fields */
    bxe_prep_dmae_with_comp(sc, &dmae, DMAE_SRC_PCI, DMAE_DST_GRC);

    /* fill in addresses and len */
    dmae.src_addr_lo = U64_LO(dma_addr);
    dmae.src_addr_hi = U64_HI(dma_addr);
    dmae.dst_addr_lo = (dst_addr >> 2); /* GRC addr has dword resolution */
    dmae.dst_addr_hi = 0;
    dmae.len         = len32;

    /* issue the command and wait for completion */
    if ((rc = bxe_issue_dmae_with_comp(sc, &dmae)) != 0) {
        bxe_panic(sc, ("DMAE failed (%d)\n", rc));
    }
}

void
bxe_write_dmae_phys_len(struct bxe_softc *sc,
                        bus_addr_t       phys_addr,
                        uint32_t         addr,
                        uint32_t         len)
{
    int dmae_wr_max = DMAE_LEN32_WR_MAX(sc);
    int offset = 0;

    while (len > dmae_wr_max) {
        bxe_write_dmae(sc,
                       (phys_addr + offset), /* src DMA address */
                       (addr + offset),      /* dst GRC address */
                       dmae_wr_max);
        offset += (dmae_wr_max * 4);
        len -= dmae_wr_max;
    }

    bxe_write_dmae(sc,
                   (phys_addr + offset), /* src DMA address */
                   (addr + offset),      /* dst GRC address */
                   len);
}

void
bxe_set_ctx_validation(struct bxe_softc   *sc,
                       struct eth_context *cxt,
                       uint32_t           cid)
{
    /* ustorm cxt validation */
    cxt->ustorm_ag_context.cdu_usage =
        CDU_RSRVD_VALUE_TYPE_A(HW_CID(sc, cid),
            CDU_REGION_NUMBER_UCM_AG, ETH_CONNECTION_TYPE);
    /* xcontext validation */
    cxt->xstorm_ag_context.cdu_reserved =
        CDU_RSRVD_VALUE_TYPE_A(HW_CID(sc, cid),
            CDU_REGION_NUMBER_XCM_AG, ETH_CONNECTION_TYPE);
}

static void
bxe_storm_memset_hc_timeout(struct bxe_softc *sc,
                            uint8_t          port,
                            uint8_t          fw_sb_id,
                            uint8_t          sb_index,
                            uint8_t          ticks)
{
    uint32_t addr =
        (BAR_CSTRORM_INTMEM +
         CSTORM_STATUS_BLOCK_DATA_TIMEOUT_OFFSET(fw_sb_id, sb_index));

    REG_WR8(sc, addr, ticks);

    BLOGD(sc, DBG_LOAD,
          "port %d fw_sb_id %d sb_index %d ticks %d\n",
          port, fw_sb_id, sb_index, ticks);
}

static void
bxe_storm_memset_hc_disable(struct bxe_softc *sc,
                            uint8_t          port,
                            uint16_t         fw_sb_id,
                            uint8_t          sb_index,
                            uint8_t          disable)
{
    uint32_t enable_flag =
        (disable) ? 0 : (1 << HC_INDEX_DATA_HC_ENABLED_SHIFT);
    uint32_t addr =
        (BAR_CSTRORM_INTMEM +
         CSTORM_STATUS_BLOCK_DATA_FLAGS_OFFSET(fw_sb_id, sb_index));
    uint8_t flags;

    /* clear and set */
    flags = REG_RD8(sc, addr);
    flags &= ~HC_INDEX_DATA_HC_ENABLED;
    flags |= enable_flag;
    REG_WR8(sc, addr, flags);

    BLOGD(sc, DBG_LOAD,
          "port %d fw_sb_id %d sb_index %d disable %d\n",
          port, fw_sb_id, sb_index, disable);
}

void
bxe_update_coalesce_sb_index(struct bxe_softc *sc,
                             uint8_t          fw_sb_id,
                             uint8_t          sb_index,
                             uint8_t          disable,
                             uint16_t         usec)
{
    int port = SC_PORT(sc);
    uint8_t ticks = (usec / 4); /* XXX ??? */

    bxe_storm_memset_hc_timeout(sc, port, fw_sb_id, sb_index, ticks);

    disable = (disable) ? 1 : ((usec) ? 0 : 1);
    bxe_storm_memset_hc_disable(sc, port, fw_sb_id, sb_index, disable);
}

void
elink_cb_udelay(struct bxe_softc *sc,
                uint32_t         usecs)
{
    DELAY(usecs);
}

uint32_t
elink_cb_reg_read(struct bxe_softc *sc,
                  uint32_t         reg_addr)
{
    return (REG_RD(sc, reg_addr));
}

void
elink_cb_reg_write(struct bxe_softc *sc,
                   uint32_t         reg_addr,
                   uint32_t         val)
{
    REG_WR(sc, reg_addr, val);
}

void
elink_cb_reg_wb_write(struct bxe_softc *sc,
                      uint32_t         offset,
                      uint32_t         *wb_write,
                      uint16_t         len)
{
    REG_WR_DMAE(sc, offset, wb_write, len);
}

void
elink_cb_reg_wb_read(struct bxe_softc *sc,
                     uint32_t         offset,
                     uint32_t         *wb_write,
                     uint16_t         len)
{
    REG_RD_DMAE(sc, offset, wb_write, len);
}

uint8_t
elink_cb_path_id(struct bxe_softc *sc)
{
    return (SC_PATH(sc));
}

void
elink_cb_event_log(struct bxe_softc     *sc,
                   const elink_log_id_t elink_log_id,
                   ...)
{
    /* XXX */
    BLOGI(sc, "ELINK EVENT LOG (%d)\n", elink_log_id);
}

static int
bxe_set_spio(struct bxe_softc *sc,
             int              spio,
             uint32_t         mode)
{
    uint32_t spio_reg;

    /* Only 2 SPIOs are configurable */
    if ((spio != MISC_SPIO_SPIO4) && (spio != MISC_SPIO_SPIO5)) {
        BLOGE(sc, "Invalid SPIO 0x%x mode 0x%x\n", spio, mode);
        return (-1);
    }

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_SPIO);

    /* read SPIO and mask except the float bits */
    spio_reg = (REG_RD(sc, MISC_REG_SPIO) & MISC_SPIO_FLOAT);

    switch (mode) {
    case MISC_SPIO_OUTPUT_LOW:
        BLOGD(sc, DBG_LOAD, "Set SPIO 0x%x -> output low\n", spio);
        /* clear FLOAT and set CLR */
        spio_reg &= ~(spio << MISC_SPIO_FLOAT_POS);
        spio_reg |=  (spio << MISC_SPIO_CLR_POS);
        break;

    case MISC_SPIO_OUTPUT_HIGH:
        BLOGD(sc, DBG_LOAD, "Set SPIO 0x%x -> output high\n", spio);
        /* clear FLOAT and set SET */
        spio_reg &= ~(spio << MISC_SPIO_FLOAT_POS);
        spio_reg |=  (spio << MISC_SPIO_SET_POS);
        break;

    case MISC_SPIO_INPUT_HI_Z:
        BLOGD(sc, DBG_LOAD, "Set SPIO 0x%x -> input\n", spio);
        /* set FLOAT */
        spio_reg |= (spio << MISC_SPIO_FLOAT_POS);
        break;

    default:
        break;
    }

    REG_WR(sc, MISC_REG_SPIO, spio_reg);
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_SPIO);

    return (0);
}

static int
bxe_gpio_read(struct bxe_softc *sc,
              int              gpio_num,
              uint8_t          port)
{
    /* The GPIO should be swapped if swap register is set and active */
    int gpio_port = ((REG_RD(sc, NIG_REG_PORT_SWAP) &&
                      REG_RD(sc, NIG_REG_STRAP_OVERRIDE)) ^ port);
    int gpio_shift = (gpio_num +
                      (gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0));
    uint32_t gpio_mask = (1 << gpio_shift);
    uint32_t gpio_reg;

    if (gpio_num > MISC_REGISTERS_GPIO_3) {
        BLOGE(sc, "Invalid GPIO %d port 0x%x gpio_port %d gpio_shift %d"
            " gpio_mask 0x%x\n", gpio_num, port, gpio_port, gpio_shift,
            gpio_mask);
        return (-1);
    }

    /* read GPIO value */
    gpio_reg = REG_RD(sc, MISC_REG_GPIO);

    /* get the requested pin value */
    return ((gpio_reg & gpio_mask) == gpio_mask) ? 1 : 0;
}

static int
bxe_gpio_write(struct bxe_softc *sc,
               int              gpio_num,
               uint32_t         mode,
               uint8_t          port)
{
    /* The GPIO should be swapped if swap register is set and active */
    int gpio_port = ((REG_RD(sc, NIG_REG_PORT_SWAP) &&
                      REG_RD(sc, NIG_REG_STRAP_OVERRIDE)) ^ port);
    int gpio_shift = (gpio_num +
                      (gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0));
    uint32_t gpio_mask = (1 << gpio_shift);
    uint32_t gpio_reg;

    if (gpio_num > MISC_REGISTERS_GPIO_3) {
        BLOGE(sc, "Invalid GPIO %d mode 0x%x port 0x%x gpio_port %d"
            " gpio_shift %d gpio_mask 0x%x\n",
            gpio_num, mode, port, gpio_port, gpio_shift, gpio_mask);
        return (-1);
    }

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_GPIO);

    /* read GPIO and mask except the float bits */
    gpio_reg = (REG_RD(sc, MISC_REG_GPIO) & MISC_REGISTERS_GPIO_FLOAT);

    switch (mode) {
    case MISC_REGISTERS_GPIO_OUTPUT_LOW:
        BLOGD(sc, DBG_PHY,
              "Set GPIO %d (shift %d) -> output low\n",
              gpio_num, gpio_shift);
        /* clear FLOAT and set CLR */
        gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
        gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_CLR_POS);
        break;

    case MISC_REGISTERS_GPIO_OUTPUT_HIGH:
        BLOGD(sc, DBG_PHY,
              "Set GPIO %d (shift %d) -> output high\n",
              gpio_num, gpio_shift);
        /* clear FLOAT and set SET */
        gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
        gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_SET_POS);
        break;

    case MISC_REGISTERS_GPIO_INPUT_HI_Z:
        BLOGD(sc, DBG_PHY,
              "Set GPIO %d (shift %d) -> input\n",
              gpio_num, gpio_shift);
        /* set FLOAT */
        gpio_reg |= (gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
        break;

    default:
        break;
    }

    REG_WR(sc, MISC_REG_GPIO, gpio_reg);
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_GPIO);

    return (0);
}

static int
bxe_gpio_mult_write(struct bxe_softc *sc,
                    uint8_t          pins,
                    uint32_t         mode)
{
    uint32_t gpio_reg;

    /* any port swapping should be handled by caller */

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_GPIO);

    /* read GPIO and mask except the float bits */
    gpio_reg = REG_RD(sc, MISC_REG_GPIO);
    gpio_reg &= ~(pins << MISC_REGISTERS_GPIO_FLOAT_POS);
    gpio_reg &= ~(pins << MISC_REGISTERS_GPIO_CLR_POS);
    gpio_reg &= ~(pins << MISC_REGISTERS_GPIO_SET_POS);

    switch (mode) {
    case MISC_REGISTERS_GPIO_OUTPUT_LOW:
        BLOGD(sc, DBG_PHY, "Set GPIO 0x%x -> output low\n", pins);
        /* set CLR */
        gpio_reg |= (pins << MISC_REGISTERS_GPIO_CLR_POS);
        break;

    case MISC_REGISTERS_GPIO_OUTPUT_HIGH:
        BLOGD(sc, DBG_PHY, "Set GPIO 0x%x -> output high\n", pins);
        /* set SET */
        gpio_reg |= (pins << MISC_REGISTERS_GPIO_SET_POS);
        break;

    case MISC_REGISTERS_GPIO_INPUT_HI_Z:
        BLOGD(sc, DBG_PHY, "Set GPIO 0x%x -> input\n", pins);
        /* set FLOAT */
        gpio_reg |= (pins << MISC_REGISTERS_GPIO_FLOAT_POS);
        break;

    default:
        BLOGE(sc, "Invalid GPIO mode assignment pins 0x%x mode 0x%x"
            " gpio_reg 0x%x\n", pins, mode, gpio_reg);
        bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_GPIO);
        return (-1);
    }

    REG_WR(sc, MISC_REG_GPIO, gpio_reg);
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_GPIO);

    return (0);
}

static int
bxe_gpio_int_write(struct bxe_softc *sc,
                   int              gpio_num,
                   uint32_t         mode,
                   uint8_t          port)
{
    /* The GPIO should be swapped if swap register is set and active */
    int gpio_port = ((REG_RD(sc, NIG_REG_PORT_SWAP) &&
                      REG_RD(sc, NIG_REG_STRAP_OVERRIDE)) ^ port);
    int gpio_shift = (gpio_num +
                      (gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0));
    uint32_t gpio_mask = (1 << gpio_shift);
    uint32_t gpio_reg;

    if (gpio_num > MISC_REGISTERS_GPIO_3) {
        BLOGE(sc, "Invalid GPIO %d mode 0x%x port 0x%x gpio_port %d"
            " gpio_shift %d gpio_mask 0x%x\n",
            gpio_num, mode, port, gpio_port, gpio_shift, gpio_mask);
        return (-1);
    }

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_GPIO);

    /* read GPIO int */
    gpio_reg = REG_RD(sc, MISC_REG_GPIO_INT);

    switch (mode) {
    case MISC_REGISTERS_GPIO_INT_OUTPUT_CLR:
        BLOGD(sc, DBG_PHY,
              "Clear GPIO INT %d (shift %d) -> output low\n",
              gpio_num, gpio_shift);
        /* clear SET and set CLR */
        gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_INT_SET_POS);
        gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_INT_CLR_POS);
        break;

    case MISC_REGISTERS_GPIO_INT_OUTPUT_SET:
        BLOGD(sc, DBG_PHY,
              "Set GPIO INT %d (shift %d) -> output high\n",
              gpio_num, gpio_shift);
        /* clear CLR and set SET */
        gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_INT_CLR_POS);
        gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_INT_SET_POS);
        break;

    default:
        break;
    }

    REG_WR(sc, MISC_REG_GPIO_INT, gpio_reg);
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_GPIO);

    return (0);
}

uint32_t
elink_cb_gpio_read(struct bxe_softc *sc,
                   uint16_t         gpio_num,
                   uint8_t          port)
{
    return (bxe_gpio_read(sc, gpio_num, port));
}

uint8_t
elink_cb_gpio_write(struct bxe_softc *sc,
                    uint16_t         gpio_num,
                    uint8_t          mode, /* 0=low 1=high */
                    uint8_t          port)
{
    return (bxe_gpio_write(sc, gpio_num, mode, port));
}

uint8_t
elink_cb_gpio_mult_write(struct bxe_softc *sc,
                         uint8_t          pins,
                         uint8_t          mode) /* 0=low 1=high */
{
    return (bxe_gpio_mult_write(sc, pins, mode));
}

uint8_t
elink_cb_gpio_int_write(struct bxe_softc *sc,
                        uint16_t         gpio_num,
                        uint8_t          mode, /* 0=low 1=high */
                        uint8_t          port)
{
    return (bxe_gpio_int_write(sc, gpio_num, mode, port));
}

void
elink_cb_notify_link_changed(struct bxe_softc *sc)
{
    REG_WR(sc, (MISC_REG_AEU_GENERAL_ATTN_12 +
                (SC_FUNC(sc) * sizeof(uint32_t))), 1);
}

/* send the MCP a request, block until there is a reply */
uint32_t
elink_cb_fw_command(struct bxe_softc *sc,
                    uint32_t         command,
                    uint32_t         param)
{
    int mb_idx = SC_FW_MB_IDX(sc);
    uint32_t seq;
    uint32_t rc = 0;
    uint32_t cnt = 1;
    uint8_t delay = CHIP_REV_IS_SLOW(sc) ? 100 : 10;

    BXE_FWMB_LOCK(sc);

    seq = ++sc->fw_seq;
    SHMEM_WR(sc, func_mb[mb_idx].drv_mb_param, param);
    SHMEM_WR(sc, func_mb[mb_idx].drv_mb_header, (command | seq));

    BLOGD(sc, DBG_PHY,
          "wrote command 0x%08x to FW MB param 0x%08x\n",
          (command | seq), param);

    /* Let the FW do it's magic. GIve it up to 5 seconds... */
    do {
        DELAY(delay * 1000);
        rc = SHMEM_RD(sc, func_mb[mb_idx].fw_mb_header);
    } while ((seq != (rc & FW_MSG_SEQ_NUMBER_MASK)) && (cnt++ < 500));

    BLOGD(sc, DBG_PHY,
          "[after %d ms] read 0x%x seq 0x%x from FW MB\n",
          cnt*delay, rc, seq);

    /* is this a reply to our command? */
    if (seq == (rc & FW_MSG_SEQ_NUMBER_MASK)) {
        rc &= FW_MSG_CODE_MASK;
    } else {
        /* Ruh-roh! */
        BLOGE(sc, "FW failed to respond!\n");
        // XXX bxe_fw_dump(sc);
        rc = 0;
    }

    BXE_FWMB_UNLOCK(sc);
    return (rc);
}

static uint32_t
bxe_fw_command(struct bxe_softc *sc,
               uint32_t         command,
               uint32_t         param)
{
    return (elink_cb_fw_command(sc, command, param));
}

static void
__storm_memset_dma_mapping(struct bxe_softc *sc,
                           uint32_t         addr,
                           bus_addr_t       mapping)
{
    REG_WR(sc, addr, U64_LO(mapping));
    REG_WR(sc, (addr + 4), U64_HI(mapping));
}

static void
storm_memset_spq_addr(struct bxe_softc *sc,
                      bus_addr_t       mapping,
                      uint16_t         abs_fid)
{
    uint32_t addr = (XSEM_REG_FAST_MEMORY +
                     XSTORM_SPQ_PAGE_BASE_OFFSET(abs_fid));
    __storm_memset_dma_mapping(sc, addr, mapping);
}

static void
storm_memset_vf_to_pf(struct bxe_softc *sc,
                      uint16_t         abs_fid,
                      uint16_t         pf_id)
{
    REG_WR8(sc, (BAR_XSTRORM_INTMEM + XSTORM_VF_TO_PF_OFFSET(abs_fid)), pf_id);
    REG_WR8(sc, (BAR_CSTRORM_INTMEM + CSTORM_VF_TO_PF_OFFSET(abs_fid)), pf_id);
    REG_WR8(sc, (BAR_TSTRORM_INTMEM + TSTORM_VF_TO_PF_OFFSET(abs_fid)), pf_id);
    REG_WR8(sc, (BAR_USTRORM_INTMEM + USTORM_VF_TO_PF_OFFSET(abs_fid)), pf_id);
}

static void
storm_memset_func_en(struct bxe_softc *sc,
                     uint16_t         abs_fid,
                     uint8_t          enable)
{
    REG_WR8(sc, (BAR_XSTRORM_INTMEM + XSTORM_FUNC_EN_OFFSET(abs_fid)), enable);
    REG_WR8(sc, (BAR_CSTRORM_INTMEM + CSTORM_FUNC_EN_OFFSET(abs_fid)), enable);
    REG_WR8(sc, (BAR_TSTRORM_INTMEM + TSTORM_FUNC_EN_OFFSET(abs_fid)), enable);
    REG_WR8(sc, (BAR_USTRORM_INTMEM + USTORM_FUNC_EN_OFFSET(abs_fid)), enable);
}

static void
storm_memset_eq_data(struct bxe_softc       *sc,
                     struct event_ring_data *eq_data,
                     uint16_t               pfid)
{
    uint32_t addr;
    size_t size;

    addr = (BAR_CSTRORM_INTMEM + CSTORM_EVENT_RING_DATA_OFFSET(pfid));
    size = sizeof(struct event_ring_data);
    ecore_storm_memset_struct(sc, addr, size, (uint32_t *)eq_data);
}

static void
storm_memset_eq_prod(struct bxe_softc *sc,
                     uint16_t         eq_prod,
                     uint16_t         pfid)
{
    uint32_t addr = (BAR_CSTRORM_INTMEM +
                     CSTORM_EVENT_RING_PROD_OFFSET(pfid));
    REG_WR16(sc, addr, eq_prod);
}

/*
 * Post a slowpath command.
 *
 * A slowpath command is used to propagate a configuration change through
 * the controller in a controlled manner, allowing each STORM processor and
 * other H/W blocks to phase in the change.  The commands sent on the
 * slowpath are referred to as ramrods.  Depending on the ramrod used the
 * completion of the ramrod will occur in different ways.  Here's a
 * breakdown of ramrods and how they complete:
 *
 * RAMROD_CMD_ID_ETH_PORT_SETUP
 *   Used to setup the leading connection on a port.  Completes on the
 *   Receive Completion Queue (RCQ) of that port (typically fp[0]).
 *
 * RAMROD_CMD_ID_ETH_CLIENT_SETUP
 *   Used to setup an additional connection on a port.  Completes on the
 *   RCQ of the multi-queue/RSS connection being initialized.
 *
 * RAMROD_CMD_ID_ETH_STAT_QUERY
 *   Used to force the storm processors to update the statistics database
 *   in host memory.  This ramrod is send on the leading connection CID and
 *   completes as an index increment of the CSTORM on the default status
 *   block.
 *
 * RAMROD_CMD_ID_ETH_UPDATE
 *   Used to update the state of the leading connection, usually to udpate
 *   the RSS indirection table.  Completes on the RCQ of the leading
 *   connection. (Not currently used under FreeBSD until OS support becomes
 *   available.)
 *
 * RAMROD_CMD_ID_ETH_HALT
 *   Used when tearing down a connection prior to driver unload.  Completes
 *   on the RCQ of the multi-queue/RSS connection being torn down.  Don't
 *   use this on the leading connection.
 *
 * RAMROD_CMD_ID_ETH_SET_MAC
 *   Sets the Unicast/Broadcast/Multicast used by the port.  Completes on
 *   the RCQ of the leading connection.
 *
 * RAMROD_CMD_ID_ETH_CFC_DEL
 *   Used when tearing down a conneciton prior to driver unload.  Completes
 *   on the RCQ of the leading connection (since the current connection
 *   has been completely removed from controller memory).
 *
 * RAMROD_CMD_ID_ETH_PORT_DEL
 *   Used to tear down the leading connection prior to driver unload,
 *   typically fp[0].  Completes as an index increment of the CSTORM on the
 *   default status block.
 *
 * RAMROD_CMD_ID_ETH_FORWARD_SETUP
 *   Used for connection offload.  Completes on the RCQ of the multi-queue
 *   RSS connection that is being offloaded.  (Not currently used under
 *   FreeBSD.)
 *
 * There can only be one command pending per function.
 *
 * Returns:
 *   0 = Success, !0 = Failure.
 */

/* must be called under the spq lock */
static inline
struct eth_spe *bxe_sp_get_next(struct bxe_softc *sc)
{
    struct eth_spe *next_spe = sc->spq_prod_bd;

    if (sc->spq_prod_bd == sc->spq_last_bd) {
        /* wrap back to the first eth_spq */
        sc->spq_prod_bd = sc->spq;
        sc->spq_prod_idx = 0;
    } else {
        sc->spq_prod_bd++;
        sc->spq_prod_idx++;
    }

    return (next_spe);
}

/* must be called under the spq lock */
static inline
void bxe_sp_prod_update(struct bxe_softc *sc)
{
    int func = SC_FUNC(sc);

    /*
     * Make sure that BD data is updated before writing the producer.
     * BD data is written to the memory, the producer is read from the
     * memory, thus we need a full memory barrier to ensure the ordering.
     */
    mb();

    REG_WR16(sc, (BAR_XSTRORM_INTMEM + XSTORM_SPQ_PROD_OFFSET(func)),
             sc->spq_prod_idx);

    bus_space_barrier(sc->bar[BAR0].tag, sc->bar[BAR0].handle, 0, 0,
                      BUS_SPACE_BARRIER_WRITE);
}

/**
 * bxe_is_contextless_ramrod - check if the current command ends on EQ
 *
 * @cmd:      command to check
 * @cmd_type: command type
 */
static inline
int bxe_is_contextless_ramrod(int cmd,
                              int cmd_type)
{
    if ((cmd_type == NONE_CONNECTION_TYPE) ||
        (cmd == RAMROD_CMD_ID_ETH_FORWARD_SETUP) ||
        (cmd == RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES) ||
        (cmd == RAMROD_CMD_ID_ETH_FILTER_RULES) ||
        (cmd == RAMROD_CMD_ID_ETH_MULTICAST_RULES) ||
        (cmd == RAMROD_CMD_ID_ETH_SET_MAC) ||
        (cmd == RAMROD_CMD_ID_ETH_RSS_UPDATE)) {
        return (TRUE);
    } else {
        return (FALSE);
    }
}

/**
 * bxe_sp_post - place a single command on an SP ring
 *
 * @sc:         driver handle
 * @command:    command to place (e.g. SETUP, FILTER_RULES, etc.)
 * @cid:        SW CID the command is related to
 * @data_hi:    command private data address (high 32 bits)
 * @data_lo:    command private data address (low 32 bits)
 * @cmd_type:   command type (e.g. NONE, ETH)
 *
 * SP data is handled as if it's always an address pair, thus data fields are
 * not swapped to little endian in upper functions. Instead this function swaps
 * data as if it's two uint32 fields.
 */
int
bxe_sp_post(struct bxe_softc *sc,
            int              command,
            int              cid,
            uint32_t         data_hi,
            uint32_t         data_lo,
            int              cmd_type)
{
    struct eth_spe *spe;
    uint16_t type;
    int common;

    common = bxe_is_contextless_ramrod(command, cmd_type);

    BXE_SP_LOCK(sc);

    if (common) {
        if (!atomic_load_acq_long(&sc->eq_spq_left)) {
            BLOGE(sc, "EQ ring is full!\n");
            BXE_SP_UNLOCK(sc);
            return (-1);
        }
    } else {
        if (!atomic_load_acq_long(&sc->cq_spq_left)) {
            BLOGE(sc, "SPQ ring is full!\n");
            BXE_SP_UNLOCK(sc);
            return (-1);
        }
    }

    spe = bxe_sp_get_next(sc);

    /* CID needs port number to be encoded int it */
    spe->hdr.conn_and_cmd_data =
        htole32((command << SPE_HDR_T_CMD_ID_SHIFT) | HW_CID(sc, cid));

    type = (cmd_type << SPE_HDR_T_CONN_TYPE_SHIFT) & SPE_HDR_T_CONN_TYPE;

    /* TBD: Check if it works for VFs */
    type |= ((SC_FUNC(sc) << SPE_HDR_T_FUNCTION_ID_SHIFT) &
             SPE_HDR_T_FUNCTION_ID);

    spe->hdr.type = htole16(type);

    spe->data.update_data_addr.hi = htole32(data_hi);
    spe->data.update_data_addr.lo = htole32(data_lo);

    /*
     * It's ok if the actual decrement is issued towards the memory
     * somewhere between the lock and unlock. Thus no more explict
     * memory barrier is needed.
     */
    if (common) {
        atomic_subtract_acq_long(&sc->eq_spq_left, 1);
    } else {
        atomic_subtract_acq_long(&sc->cq_spq_left, 1);
    }

    BLOGD(sc, DBG_SP, "SPQE -> %#jx\n", (uintmax_t)sc->spq_dma.paddr);
    BLOGD(sc, DBG_SP, "FUNC_RDATA -> %p / %#jx\n",
          BXE_SP(sc, func_rdata), (uintmax_t)BXE_SP_MAPPING(sc, func_rdata));
    BLOGD(sc, DBG_SP,
          "SPQE[%x] (%x:%x) (cmd, common?) (%d,%d) hw_cid %x data (%x:%x) type(0x%x) left (CQ, EQ) (%lx,%lx)\n",
          sc->spq_prod_idx,
          (uint32_t)U64_HI(sc->spq_dma.paddr),
          (uint32_t)(U64_LO(sc->spq_dma.paddr) + (uint8_t *)sc->spq_prod_bd - (uint8_t *)sc->spq),
          command,
          common,
          HW_CID(sc, cid),
          data_hi,
          data_lo,
          type,
          atomic_load_acq_long(&sc->cq_spq_left),
          atomic_load_acq_long(&sc->eq_spq_left));

    bxe_sp_prod_update(sc);

    BXE_SP_UNLOCK(sc);
    return (0);
}

/**
 * bxe_debug_print_ind_table - prints the indirection table configuration.
 *
 * @sc: driver hanlde
 * @p:  pointer to rss configuration
 */

/*
 * FreeBSD Device probe function.
 *
 * Compares the device found to the driver's list of supported devices and
 * reports back to the bsd loader whether this is the right driver for the device.
 * This is the driver entry function called from the "kldload" command.
 *
 * Returns:
 *   BUS_PROBE_DEFAULT on success, positive value on failure.
 */
static int
bxe_probe(device_t dev)
{
    struct bxe_device_type *t;
    char *descbuf;
    uint16_t did, sdid, svid, vid;

    /* Find our device structure */
    t = bxe_devs;

    /* Get the data for the device to be probed. */
    vid  = pci_get_vendor(dev);
    did  = pci_get_device(dev);
    svid = pci_get_subvendor(dev);
    sdid = pci_get_subdevice(dev);

    /* Look through the list of known devices for a match. */
    while (t->bxe_name != NULL) {
        if ((vid == t->bxe_vid) && (did == t->bxe_did) &&
            ((svid == t->bxe_svid) || (t->bxe_svid == PCI_ANY_ID)) &&
            ((sdid == t->bxe_sdid) || (t->bxe_sdid == PCI_ANY_ID))) {
            descbuf = malloc(BXE_DEVDESC_MAX, M_TEMP, M_NOWAIT);
            if (descbuf == NULL)
                return (ENOMEM);

            /* Print out the device identity. */
            snprintf(descbuf, BXE_DEVDESC_MAX,
                     "%s (%c%d) BXE v:%s\n", t->bxe_name,
                     (((pci_read_config(dev, PCIR_REVID, 4) &
                        0xf0) >> 4) + 'A'),
                     (pci_read_config(dev, PCIR_REVID, 4) & 0xf),
                     BXE_DRIVER_VERSION);

            device_set_desc_copy(dev, descbuf);
            free(descbuf, M_TEMP);
            return (BUS_PROBE_DEFAULT);
        }
        t++;
    }

    return (ENXIO);
}

static void
bxe_init_mutexes(struct bxe_softc *sc)
{
#ifdef BXE_CORE_LOCK_SX
    snprintf(sc->core_sx_name, sizeof(sc->core_sx_name),
             "bxe%d_core_lock", sc->unit);
    sx_init(&sc->core_sx, sc->core_sx_name);
#else
    snprintf(sc->core_mtx_name, sizeof(sc->core_mtx_name),
             "bxe%d_core_lock", sc->unit);
    mtx_init(&sc->core_mtx, sc->core_mtx_name, NULL, MTX_DEF);
#endif

    snprintf(sc->sp_mtx_name, sizeof(sc->sp_mtx_name),
             "bxe%d_sp_lock", sc->unit);
    mtx_init(&sc->sp_mtx, sc->sp_mtx_name, NULL, MTX_DEF);

    snprintf(sc->dmae_mtx_name, sizeof(sc->dmae_mtx_name),
             "bxe%d_dmae_lock", sc->unit);
    mtx_init(&sc->dmae_mtx, sc->dmae_mtx_name, NULL, MTX_DEF);

    snprintf(sc->port.phy_mtx_name, sizeof(sc->port.phy_mtx_name),
             "bxe%d_phy_lock", sc->unit);
    mtx_init(&sc->port.phy_mtx, sc->port.phy_mtx_name, NULL, MTX_DEF);

    snprintf(sc->fwmb_mtx_name, sizeof(sc->fwmb_mtx_name),
             "bxe%d_fwmb_lock", sc->unit);
    mtx_init(&sc->fwmb_mtx, sc->fwmb_mtx_name, NULL, MTX_DEF);

    snprintf(sc->print_mtx_name, sizeof(sc->print_mtx_name),
             "bxe%d_print_lock", sc->unit);
    mtx_init(&(sc->print_mtx), sc->print_mtx_name, NULL, MTX_DEF);

    snprintf(sc->stats_mtx_name, sizeof(sc->stats_mtx_name),
             "bxe%d_stats_lock", sc->unit);
    mtx_init(&(sc->stats_mtx), sc->stats_mtx_name, NULL, MTX_DEF);

    snprintf(sc->mcast_mtx_name, sizeof(sc->mcast_mtx_name),
             "bxe%d_mcast_lock", sc->unit);
    mtx_init(&(sc->mcast_mtx), sc->mcast_mtx_name, NULL, MTX_DEF);
}

static void
bxe_release_mutexes(struct bxe_softc *sc)
{
#ifdef BXE_CORE_LOCK_SX
    sx_destroy(&sc->core_sx);
#else
    if (mtx_initialized(&sc->core_mtx)) {
        mtx_destroy(&sc->core_mtx);
    }
#endif

    if (mtx_initialized(&sc->sp_mtx)) {
        mtx_destroy(&sc->sp_mtx);
    }

    if (mtx_initialized(&sc->dmae_mtx)) {
        mtx_destroy(&sc->dmae_mtx);
    }

    if (mtx_initialized(&sc->port.phy_mtx)) {
        mtx_destroy(&sc->port.phy_mtx);
    }

    if (mtx_initialized(&sc->fwmb_mtx)) {
        mtx_destroy(&sc->fwmb_mtx);
    }

    if (mtx_initialized(&sc->print_mtx)) {
        mtx_destroy(&sc->print_mtx);
    }

    if (mtx_initialized(&sc->stats_mtx)) {
        mtx_destroy(&sc->stats_mtx);
    }

    if (mtx_initialized(&sc->mcast_mtx)) {
        mtx_destroy(&sc->mcast_mtx);
    }
}

static void
bxe_tx_disable(struct bxe_softc* sc)
{
    if_t ifp = sc->ifp;

    /* tell the stack the driver is stopped and TX queue is full */
    if (ifp !=  NULL) {
        if_setdrvflags(ifp, 0);
    }
}

static void
bxe_drv_pulse(struct bxe_softc *sc)
{
    SHMEM_WR(sc, func_mb[SC_FW_MB_IDX(sc)].drv_pulse_mb,
             sc->fw_drv_pulse_wr_seq);
}

static inline uint16_t
bxe_tx_avail(struct bxe_softc *sc,
             struct bxe_fastpath *fp)
{
    int16_t  used;
    uint16_t prod;
    uint16_t cons;

    prod = fp->tx_bd_prod;
    cons = fp->tx_bd_cons;

    used = SUB_S16(prod, cons);

    return (int16_t)(sc->tx_ring_size) - used;
}

static inline int
bxe_tx_queue_has_work(struct bxe_fastpath *fp)
{
    uint16_t hw_cons;

    mb(); /* status block fields can change */
    hw_cons = le16toh(*fp->tx_cons_sb);
    return (hw_cons != fp->tx_pkt_cons);
}

static inline uint8_t
bxe_has_tx_work(struct bxe_fastpath *fp)
{
    /* expand this for multi-cos if ever supported */
    return (bxe_tx_queue_has_work(fp)) ? TRUE : FALSE;
}

static inline int
bxe_has_rx_work(struct bxe_fastpath *fp)
{
    uint16_t rx_cq_cons_sb;

    mb(); /* status block fields can change */
    rx_cq_cons_sb = le16toh(*fp->rx_cq_cons_sb);
    if ((rx_cq_cons_sb & RCQ_MAX) == RCQ_MAX)
        rx_cq_cons_sb++;
    return (fp->rx_cq_cons != rx_cq_cons_sb);
}

static void
bxe_sp_event(struct bxe_softc    *sc,
             struct bxe_fastpath *fp,
             union eth_rx_cqe    *rr_cqe)
{
    int cid = SW_CID(rr_cqe->ramrod_cqe.conn_and_cmd_data);
    int command = CQE_CMD(rr_cqe->ramrod_cqe.conn_and_cmd_data);
    enum ecore_queue_cmd drv_cmd = ECORE_Q_CMD_MAX;
    struct ecore_queue_sp_obj *q_obj = &BXE_SP_OBJ(sc, fp).q_obj;

    BLOGD(sc, DBG_SP, "fp=%d cid=%d got ramrod #%d state is %x type is %d\n",
          fp->index, cid, command, sc->state, rr_cqe->ramrod_cqe.ramrod_type);

    switch (command) {
    case (RAMROD_CMD_ID_ETH_CLIENT_UPDATE):
        BLOGD(sc, DBG_SP, "got UPDATE ramrod. CID %d\n", cid);
        drv_cmd = ECORE_Q_CMD_UPDATE;
        break;

    case (RAMROD_CMD_ID_ETH_CLIENT_SETUP):
        BLOGD(sc, DBG_SP, "got MULTI[%d] setup ramrod\n", cid);
        drv_cmd = ECORE_Q_CMD_SETUP;
        break;

    case (RAMROD_CMD_ID_ETH_TX_QUEUE_SETUP):
        BLOGD(sc, DBG_SP, "got MULTI[%d] tx-only setup ramrod\n", cid);
        drv_cmd = ECORE_Q_CMD_SETUP_TX_ONLY;
        break;

    case (RAMROD_CMD_ID_ETH_HALT):
        BLOGD(sc, DBG_SP, "got MULTI[%d] halt ramrod\n", cid);
        drv_cmd = ECORE_Q_CMD_HALT;
        break;

    case (RAMROD_CMD_ID_ETH_TERMINATE):
        BLOGD(sc, DBG_SP, "got MULTI[%d] teminate ramrod\n", cid);
        drv_cmd = ECORE_Q_CMD_TERMINATE;
        break;

    case (RAMROD_CMD_ID_ETH_EMPTY):
        BLOGD(sc, DBG_SP, "got MULTI[%d] empty ramrod\n", cid);
        drv_cmd = ECORE_Q_CMD_EMPTY;
        break;

    default:
        BLOGD(sc, DBG_SP, "ERROR: unexpected MC reply (%d) on fp[%d]\n",
              command, fp->index);
        return;
    }

    if ((drv_cmd != ECORE_Q_CMD_MAX) &&
        q_obj->complete_cmd(sc, q_obj, drv_cmd)) {
        /*
         * q_obj->complete_cmd() failure means that this was
         * an unexpected completion.
         *
         * In this case we don't want to increase the sc->spq_left
         * because apparently we haven't sent this command the first
         * place.
         */
        // bxe_panic(sc, ("Unexpected SP completion\n"));
        return;
    }

    atomic_add_acq_long(&sc->cq_spq_left, 1);

    BLOGD(sc, DBG_SP, "sc->cq_spq_left 0x%lx\n",
          atomic_load_acq_long(&sc->cq_spq_left));
}

/*
 * The current mbuf is part of an aggregation. Move the mbuf into the TPA
 * aggregation queue, put an empty mbuf back onto the receive chain, and mark
 * the current aggregation queue as in-progress.
 */
static void
bxe_tpa_start(struct bxe_softc            *sc,
              struct bxe_fastpath         *fp,
              uint16_t                    queue,
              uint16_t                    cons,
              uint16_t                    prod,
              struct eth_fast_path_rx_cqe *cqe)
{
    struct bxe_sw_rx_bd tmp_bd;
    struct bxe_sw_rx_bd *rx_buf;
    struct eth_rx_bd *rx_bd;
    int max_agg_queues;
    struct bxe_sw_tpa_info *tpa_info = &fp->rx_tpa_info[queue];
    uint16_t index;

    BLOGD(sc, DBG_LRO, "fp[%02d].tpa[%02d] TPA START "
                       "cons=%d prod=%d\n",
          fp->index, queue, cons, prod);

    max_agg_queues = MAX_AGG_QS(sc);

    KASSERT((queue < max_agg_queues),
            ("fp[%02d] invalid aggr queue (%d >= %d)!",
             fp->index, queue, max_agg_queues));

    KASSERT((tpa_info->state == BXE_TPA_STATE_STOP),
            ("fp[%02d].tpa[%02d] starting aggr on queue not stopped!",
             fp->index, queue));

    /* copy the existing mbuf and mapping from the TPA pool */
    tmp_bd = tpa_info->bd;

    if (tmp_bd.m == NULL) {
        uint32_t *tmp;

        tmp = (uint32_t *)cqe;

        BLOGE(sc, "fp[%02d].tpa[%02d] cons[%d] prod[%d]mbuf not allocated!\n",
              fp->index, queue, cons, prod);
        BLOGE(sc, "cqe [0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x]\n",
            *tmp, *(tmp+1), *(tmp+2), *(tmp+3), *(tmp+4), *(tmp+5), *(tmp+6), *(tmp+7)); 
            
        /* XXX Error handling? */
        return;
    }

    /* change the TPA queue to the start state */
    tpa_info->state            = BXE_TPA_STATE_START;
    tpa_info->placement_offset = cqe->placement_offset;
    tpa_info->parsing_flags    = le16toh(cqe->pars_flags.flags);
    tpa_info->vlan_tag         = le16toh(cqe->vlan_tag);
    tpa_info->len_on_bd        = le16toh(cqe->len_on_bd);

    fp->rx_tpa_queue_used |= (1 << queue);

    /*
     * If all the buffer descriptors are filled with mbufs then fill in
     * the current consumer index with a new BD. Else if a maximum Rx
     * buffer limit is imposed then fill in the next producer index.
     */
    index = (sc->max_rx_bufs != RX_BD_USABLE) ?
                prod : cons;

    /* move the received mbuf and mapping to TPA pool */
    tpa_info->bd = fp->rx_mbuf_chain[cons];

    /* release any existing RX BD mbuf mappings */
    if (cons != index) {
        rx_buf = &fp->rx_mbuf_chain[cons];

        if (rx_buf->m_map != NULL) {
            bus_dmamap_sync(fp->rx_mbuf_tag, rx_buf->m_map,
                            BUS_DMASYNC_POSTREAD);
            bus_dmamap_unload(fp->rx_mbuf_tag, rx_buf->m_map);
        }

        /*
         * We get here when the maximum number of rx buffers is less than
         * RX_BD_USABLE. The mbuf is already saved above so it's OK to NULL
         * it out here without concern of a memory leak.
         */
        fp->rx_mbuf_chain[cons].m = NULL;
    }

    /* update the Rx SW BD with the mbuf info from the TPA pool */
    fp->rx_mbuf_chain[index] = tmp_bd;

    /* update the Rx BD with the empty mbuf phys address from the TPA pool */
    rx_bd = &fp->rx_chain[index];
    rx_bd->addr_hi = htole32(U64_HI(tpa_info->seg.ds_addr));
    rx_bd->addr_lo = htole32(U64_LO(tpa_info->seg.ds_addr));
}

/*
 * When a TPA aggregation is completed, loop through the individual mbufs
 * of the aggregation, combining them into a single mbuf which will be sent
 * up the stack. Refill all freed SGEs with mbufs as we go along.
 */
static int
bxe_fill_frag_mbuf(struct bxe_softc          *sc,
                   struct bxe_fastpath       *fp,
                   struct bxe_sw_tpa_info    *tpa_info,
                   uint16_t                  queue,
                   uint16_t                  pages,
                   struct mbuf               *m,
			       struct eth_end_agg_rx_cqe *cqe,
                   uint16_t                  cqe_idx)
{
    struct mbuf *m_frag;
    uint32_t frag_len, frag_size, i;
    uint16_t sge_idx;
    int rc = 0;
    int j;

    frag_size = le16toh(cqe->pkt_len) - tpa_info->len_on_bd;

    BLOGD(sc, DBG_LRO,
          "fp[%02d].tpa[%02d] TPA fill len_on_bd=%d frag_size=%d pages=%d\n",
          fp->index, queue, tpa_info->len_on_bd, frag_size, pages);

    /* make sure the aggregated frame is not too big to handle */
    if (pages > 8 * PAGES_PER_SGE) {

        uint32_t *tmp = (uint32_t *)cqe;

        BLOGE(sc, "fp[%02d].sge[0x%04x] has too many pages (%d)! "
                  "pkt_len=%d len_on_bd=%d frag_size=%d\n",
              fp->index, cqe_idx, pages, le16toh(cqe->pkt_len),
              tpa_info->len_on_bd, frag_size);

        BLOGE(sc, "cqe [0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x]\n",
            *tmp, *(tmp+1), *(tmp+2), *(tmp+3), *(tmp+4), *(tmp+5), *(tmp+6), *(tmp+7)); 

        bxe_panic(sc, ("sge page count error\n"));
        return (EINVAL);
    }

    /*
     * Scan through the scatter gather list pulling individual mbufs into a
     * single mbuf for the host stack.
     */
    for (i = 0, j = 0; i < pages; i += PAGES_PER_SGE, j++) {
        sge_idx = RX_SGE(le16toh(cqe->sgl_or_raw_data.sgl[j]));

        /*
         * Firmware gives the indices of the SGE as if the ring is an array
         * (meaning that the "next" element will consume 2 indices).
         */
        frag_len = min(frag_size, (uint32_t)(SGE_PAGES));

        BLOGD(sc, DBG_LRO, "fp[%02d].tpa[%02d] TPA fill i=%d j=%d "
                           "sge_idx=%d frag_size=%d frag_len=%d\n",
              fp->index, queue, i, j, sge_idx, frag_size, frag_len);

        m_frag = fp->rx_sge_mbuf_chain[sge_idx].m;

        /* allocate a new mbuf for the SGE */
        rc = bxe_alloc_rx_sge_mbuf(fp, sge_idx);
        if (rc) {
            /* Leave all remaining SGEs in the ring! */
            return (rc);
        }

        /* update the fragment length */
        m_frag->m_len = frag_len;

        /* concatenate the fragment to the head mbuf */
        m_cat(m, m_frag);
        fp->eth_q_stats.mbuf_alloc_sge--;

        /* update the TPA mbuf size and remaining fragment size */
        m->m_pkthdr.len += frag_len;
        frag_size -= frag_len;
    }

    BLOGD(sc, DBG_LRO,
          "fp[%02d].tpa[%02d] TPA fill done frag_size=%d\n",
          fp->index, queue, frag_size);

    return (rc);
}

static inline void
bxe_clear_sge_mask_next_elems(struct bxe_fastpath *fp)
{
    int i, j;

    for (i = 1; i <= RX_SGE_NUM_PAGES; i++) {
        int idx = RX_SGE_TOTAL_PER_PAGE * i - 1;

        for (j = 0; j < 2; j++) {
            BIT_VEC64_CLEAR_BIT(fp->sge_mask, idx);
            idx--;
        }
    }
}

static inline void
bxe_init_sge_ring_bit_mask(struct bxe_fastpath *fp)
{
    /* set the mask to all 1's, it's faster to compare to 0 than to 0xf's */
    memset(fp->sge_mask, 0xff, sizeof(fp->sge_mask));

    /*
     * Clear the two last indices in the page to 1. These are the indices that
     * correspond to the "next" element, hence will never be indicated and
     * should be removed from the calculations.
     */
    bxe_clear_sge_mask_next_elems(fp);
}

static inline void
bxe_update_last_max_sge(struct bxe_fastpath *fp,
                        uint16_t            idx)
{
    uint16_t last_max = fp->last_max_sge;

    if (SUB_S16(idx, last_max) > 0) {
        fp->last_max_sge = idx;
    }
}

static inline void
bxe_update_sge_prod(struct bxe_softc          *sc,
                    struct bxe_fastpath       *fp,
                    uint16_t                  sge_len,
                    union eth_sgl_or_raw_data *cqe)
{
    uint16_t last_max, last_elem, first_elem;
    uint16_t delta = 0;
    uint16_t i;

    if (!sge_len) {
        return;
    }

    /* first mark all used pages */
    for (i = 0; i < sge_len; i++) {
        BIT_VEC64_CLEAR_BIT(fp->sge_mask,
                            RX_SGE(le16toh(cqe->sgl[i])));
    }

    BLOGD(sc, DBG_LRO,
          "fp[%02d] fp_cqe->sgl[%d] = %d\n",
          fp->index, sge_len - 1,
          le16toh(cqe->sgl[sge_len - 1]));

    /* assume that the last SGE index is the biggest */
    bxe_update_last_max_sge(fp,
                            le16toh(cqe->sgl[sge_len - 1]));

    last_max = RX_SGE(fp->last_max_sge);
    last_elem = last_max >> BIT_VEC64_ELEM_SHIFT;
    first_elem = RX_SGE(fp->rx_sge_prod) >> BIT_VEC64_ELEM_SHIFT;

    /* if ring is not full */
    if (last_elem + 1 != first_elem) {
        last_elem++;
    }

    /* now update the prod */
    for (i = first_elem; i != last_elem; i = RX_SGE_NEXT_MASK_ELEM(i)) {
        if (__predict_true(fp->sge_mask[i])) {
            break;
        }

        fp->sge_mask[i] = BIT_VEC64_ELEM_ONE_MASK;
        delta += BIT_VEC64_ELEM_SZ;
    }

    if (delta > 0) {
        fp->rx_sge_prod += delta;
        /* clear page-end entries */
        bxe_clear_sge_mask_next_elems(fp);
    }

    BLOGD(sc, DBG_LRO,
          "fp[%02d] fp->last_max_sge=%d fp->rx_sge_prod=%d\n",
          fp->index, fp->last_max_sge, fp->rx_sge_prod);
}

/*
 * The aggregation on the current TPA queue has completed. Pull the individual
 * mbuf fragments together into a single mbuf, perform all necessary checksum
 * calculations, and send the resuting mbuf to the stack.
 */
static void
bxe_tpa_stop(struct bxe_softc          *sc,
             struct bxe_fastpath       *fp,
             struct bxe_sw_tpa_info    *tpa_info,
             uint16_t                  queue,
             uint16_t                  pages,
			 struct eth_end_agg_rx_cqe *cqe,
             uint16_t                  cqe_idx)
{
    if_t ifp = sc->ifp;
    struct mbuf *m;
    int rc = 0;

    BLOGD(sc, DBG_LRO,
          "fp[%02d].tpa[%02d] pad=%d pkt_len=%d pages=%d vlan=%d\n",
          fp->index, queue, tpa_info->placement_offset,
          le16toh(cqe->pkt_len), pages, tpa_info->vlan_tag);

    m = tpa_info->bd.m;

    /* allocate a replacement before modifying existing mbuf */
    rc = bxe_alloc_rx_tpa_mbuf(fp, queue);
    if (rc) {
        /* drop the frame and log an error */
        fp->eth_q_stats.rx_soft_errors++;
        goto bxe_tpa_stop_exit;
    }

    /* we have a replacement, fixup the current mbuf */
    m_adj(m, tpa_info->placement_offset);
    m->m_pkthdr.len = m->m_len = tpa_info->len_on_bd;

    /* mark the checksums valid (taken care of by the firmware) */
    fp->eth_q_stats.rx_ofld_frames_csum_ip++;
    fp->eth_q_stats.rx_ofld_frames_csum_tcp_udp++;
    m->m_pkthdr.csum_data = 0xffff;
    m->m_pkthdr.csum_flags |= (CSUM_IP_CHECKED |
                               CSUM_IP_VALID   |
                               CSUM_DATA_VALID |
                               CSUM_PSEUDO_HDR);

    /* aggregate all of the SGEs into a single mbuf */
    rc = bxe_fill_frag_mbuf(sc, fp, tpa_info, queue, pages, m, cqe, cqe_idx);
    if (rc) {
        /* drop the packet and log an error */
        fp->eth_q_stats.rx_soft_errors++;
        m_freem(m);
    } else {
        if (tpa_info->parsing_flags & PARSING_FLAGS_INNER_VLAN_EXIST) {
            m->m_pkthdr.ether_vtag = tpa_info->vlan_tag;
            m->m_flags |= M_VLANTAG;
        }

        /* assign packet to this interface interface */
        if_setrcvif(m, ifp);

#if __FreeBSD_version >= 800000
        /* specify what RSS queue was used for this flow */
        m->m_pkthdr.flowid = fp->index;
        BXE_SET_FLOWID(m);
#endif

        if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
        fp->eth_q_stats.rx_tpa_pkts++;

        /* pass the frame to the stack */
        if_input(ifp, m);
    }

    /* we passed an mbuf up the stack or dropped the frame */
    fp->eth_q_stats.mbuf_alloc_tpa--;

bxe_tpa_stop_exit:

    fp->rx_tpa_info[queue].state = BXE_TPA_STATE_STOP;
    fp->rx_tpa_queue_used &= ~(1 << queue);
}

static uint8_t
bxe_service_rxsgl(
                 struct bxe_fastpath *fp,
                 uint16_t len,
                 uint16_t lenonbd,
                 struct mbuf *m,
                 struct eth_fast_path_rx_cqe *cqe_fp)
{
    struct mbuf *m_frag;
    uint16_t frags, frag_len;
    uint16_t sge_idx = 0;
    uint16_t j;
    uint8_t i, rc = 0;
    uint32_t frag_size;

    /* adjust the mbuf */
    m->m_len = lenonbd;

    frag_size =  len - lenonbd;
    frags = SGE_PAGE_ALIGN(frag_size) >> SGE_PAGE_SHIFT;

    for (i = 0, j = 0; i < frags; i += PAGES_PER_SGE, j++) {
        sge_idx = RX_SGE(le16toh(cqe_fp->sgl_or_raw_data.sgl[j]));

        m_frag = fp->rx_sge_mbuf_chain[sge_idx].m;
        frag_len = min(frag_size, (uint32_t)(SGE_PAGE_SIZE));
        m_frag->m_len = frag_len;

       /* allocate a new mbuf for the SGE */
        rc = bxe_alloc_rx_sge_mbuf(fp, sge_idx);
        if (rc) {
            /* Leave all remaining SGEs in the ring! */
            return (rc);
        }
        fp->eth_q_stats.mbuf_alloc_sge--;

        /* concatenate the fragment to the head mbuf */
        m_cat(m, m_frag);

        frag_size -= frag_len;
    }

    bxe_update_sge_prod(fp->sc, fp, frags, &cqe_fp->sgl_or_raw_data);

    return rc;
}

static uint8_t
bxe_rxeof(struct bxe_softc    *sc,
          struct bxe_fastpath *fp)
{
    if_t ifp = sc->ifp;
    uint16_t bd_cons, bd_prod, bd_prod_fw, comp_ring_cons;
    uint16_t hw_cq_cons, sw_cq_cons, sw_cq_prod;
    int rx_pkts = 0;
    int rc = 0;

    BXE_FP_RX_LOCK(fp);

    /* CQ "next element" is of the size of the regular element */
    hw_cq_cons = le16toh(*fp->rx_cq_cons_sb);
    if ((hw_cq_cons & RCQ_USABLE_PER_PAGE) == RCQ_USABLE_PER_PAGE) {
        hw_cq_cons++;
    }

    bd_cons = fp->rx_bd_cons;
    bd_prod = fp->rx_bd_prod;
    bd_prod_fw = bd_prod;
    sw_cq_cons = fp->rx_cq_cons;
    sw_cq_prod = fp->rx_cq_prod;

    /*
     * Memory barrier necessary as speculative reads of the rx
     * buffer can be ahead of the index in the status block
     */
    rmb();

    BLOGD(sc, DBG_RX,
          "fp[%02d] Rx START hw_cq_cons=%u sw_cq_cons=%u\n",
          fp->index, hw_cq_cons, sw_cq_cons);

    while (sw_cq_cons != hw_cq_cons) {
        struct bxe_sw_rx_bd *rx_buf = NULL;
        union eth_rx_cqe *cqe;
        struct eth_fast_path_rx_cqe *cqe_fp;
        uint8_t cqe_fp_flags;
        enum eth_rx_cqe_type cqe_fp_type;
        uint16_t len, lenonbd,  pad;
        struct mbuf *m = NULL;

        comp_ring_cons = RCQ(sw_cq_cons);
        bd_prod = RX_BD(bd_prod);
        bd_cons = RX_BD(bd_cons);

        cqe          = &fp->rcq_chain[comp_ring_cons];
        cqe_fp       = &cqe->fast_path_cqe;
        cqe_fp_flags = cqe_fp->type_error_flags;
        cqe_fp_type  = cqe_fp_flags & ETH_FAST_PATH_RX_CQE_TYPE;

        BLOGD(sc, DBG_RX,
              "fp[%02d] Rx hw_cq_cons=%d hw_sw_cons=%d "
              "BD prod=%d cons=%d CQE type=0x%x err=0x%x "
              "status=0x%x rss_hash=0x%x vlan=0x%x len=%u lenonbd=%u\n",
              fp->index,
              hw_cq_cons,
              sw_cq_cons,
              bd_prod,
              bd_cons,
              CQE_TYPE(cqe_fp_flags),
              cqe_fp_flags,
              cqe_fp->status_flags,
              le32toh(cqe_fp->rss_hash_result),
              le16toh(cqe_fp->vlan_tag),
              le16toh(cqe_fp->pkt_len_or_gro_seg_len),
              le16toh(cqe_fp->len_on_bd));

        /* is this a slowpath msg? */
        if (__predict_false(CQE_TYPE_SLOW(cqe_fp_type))) {
            bxe_sp_event(sc, fp, cqe);
            goto next_cqe;
        }

        rx_buf = &fp->rx_mbuf_chain[bd_cons];

        if (!CQE_TYPE_FAST(cqe_fp_type)) {
            struct bxe_sw_tpa_info *tpa_info;
            uint16_t frag_size, pages;
            uint8_t queue;

            if (CQE_TYPE_START(cqe_fp_type)) {
                bxe_tpa_start(sc, fp, cqe_fp->queue_index,
                              bd_cons, bd_prod, cqe_fp);
                m = NULL; /* packet not ready yet */
                goto next_rx;
            }

            KASSERT(CQE_TYPE_STOP(cqe_fp_type),
                    ("CQE type is not STOP! (0x%x)\n", cqe_fp_type));

            queue = cqe->end_agg_cqe.queue_index;
            tpa_info = &fp->rx_tpa_info[queue];

            BLOGD(sc, DBG_LRO, "fp[%02d].tpa[%02d] TPA STOP\n",
                  fp->index, queue);

            frag_size = (le16toh(cqe->end_agg_cqe.pkt_len) -
                         tpa_info->len_on_bd);
            pages = SGE_PAGE_ALIGN(frag_size) >> SGE_PAGE_SHIFT;

            bxe_tpa_stop(sc, fp, tpa_info, queue, pages,
                         &cqe->end_agg_cqe, comp_ring_cons);

            bxe_update_sge_prod(sc, fp, pages, &cqe->end_agg_cqe.sgl_or_raw_data);

            goto next_cqe;
        }

        /* non TPA */

        /* is this an error packet? */
        if (__predict_false(cqe_fp_flags &
                            ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG)) {
            BLOGE(sc, "flags 0x%x rx packet %u\n", cqe_fp_flags, sw_cq_cons);
            fp->eth_q_stats.rx_soft_errors++;
            goto next_rx;
        }

        len = le16toh(cqe_fp->pkt_len_or_gro_seg_len);
        lenonbd = le16toh(cqe_fp->len_on_bd);
        pad = cqe_fp->placement_offset;

        m = rx_buf->m;

        if (__predict_false(m == NULL)) {
            BLOGE(sc, "No mbuf in rx chain descriptor %d for fp[%02d]\n",
                  bd_cons, fp->index);
            goto next_rx;
        }

        /* XXX double copy if packet length under a threshold */

        /*
         * If all the buffer descriptors are filled with mbufs then fill in
         * the current consumer index with a new BD. Else if a maximum Rx
         * buffer limit is imposed then fill in the next producer index.
         */
        rc = bxe_alloc_rx_bd_mbuf(fp, bd_cons,
                                  (sc->max_rx_bufs != RX_BD_USABLE) ?
                                      bd_prod : bd_cons);
        if (rc != 0) {

            /* we simply reuse the received mbuf and don't post it to the stack */
            m = NULL;

            BLOGE(sc, "mbuf alloc fail for fp[%02d] rx chain (%d)\n",
                  fp->index, rc);
            fp->eth_q_stats.rx_soft_errors++;

            if (sc->max_rx_bufs != RX_BD_USABLE) {
                /* copy this consumer index to the producer index */
                memcpy(&fp->rx_mbuf_chain[bd_prod], rx_buf,
                       sizeof(struct bxe_sw_rx_bd));
                memset(rx_buf, 0, sizeof(struct bxe_sw_rx_bd));
            }

            goto next_rx;
        }

        /* current mbuf was detached from the bd */
        fp->eth_q_stats.mbuf_alloc_rx--;

        /* we allocated a replacement mbuf, fixup the current one */
        m_adj(m, pad);
        m->m_pkthdr.len = m->m_len = len;

        if ((len > 60) && (len > lenonbd)) {
            fp->eth_q_stats.rx_bxe_service_rxsgl++;
            rc = bxe_service_rxsgl(fp, len, lenonbd, m, cqe_fp);
            if (rc)
                break;
            fp->eth_q_stats.rx_jumbo_sge_pkts++;
        } else if (lenonbd < len) {
            fp->eth_q_stats.rx_erroneous_jumbo_sge_pkts++;
        }

        /* assign packet to this interface interface */
	if_setrcvif(m, ifp);

        /* assume no hardware checksum has complated */
        m->m_pkthdr.csum_flags = 0;

        /* validate checksum if offload enabled */
        if (if_getcapenable(ifp) & IFCAP_RXCSUM) {
            /* check for a valid IP frame */
            if (!(cqe->fast_path_cqe.status_flags &
                  ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG)) {
                m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
                if (__predict_false(cqe_fp_flags &
                                    ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG)) {
                    fp->eth_q_stats.rx_hw_csum_errors++;
                } else {
                    fp->eth_q_stats.rx_ofld_frames_csum_ip++;
                    m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
                }
            }

            /* check for a valid TCP/UDP frame */
            if (!(cqe->fast_path_cqe.status_flags &
                  ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG)) {
                if (__predict_false(cqe_fp_flags &
                                    ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG)) {
                    fp->eth_q_stats.rx_hw_csum_errors++;
                } else {
                    fp->eth_q_stats.rx_ofld_frames_csum_tcp_udp++;
                    m->m_pkthdr.csum_data = 0xFFFF;
                    m->m_pkthdr.csum_flags |= (CSUM_DATA_VALID |
                                               CSUM_PSEUDO_HDR);
                }
            }
        }

        /* if there is a VLAN tag then flag that info */
        if (cqe->fast_path_cqe.pars_flags.flags & PARSING_FLAGS_INNER_VLAN_EXIST) {
            m->m_pkthdr.ether_vtag = cqe->fast_path_cqe.vlan_tag;
            m->m_flags |= M_VLANTAG;
        }

#if __FreeBSD_version >= 800000
        /* specify what RSS queue was used for this flow */
        m->m_pkthdr.flowid = fp->index;
        BXE_SET_FLOWID(m);
#endif

next_rx:

        bd_cons    = RX_BD_NEXT(bd_cons);
        bd_prod    = RX_BD_NEXT(bd_prod);
        bd_prod_fw = RX_BD_NEXT(bd_prod_fw);

        /* pass the frame to the stack */
        if (__predict_true(m != NULL)) {
            if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
            rx_pkts++;
            if_input(ifp, m);
        }

next_cqe:

        sw_cq_prod = RCQ_NEXT(sw_cq_prod);
        sw_cq_cons = RCQ_NEXT(sw_cq_cons);

        /* limit spinning on the queue */
        if (rc != 0)
            break;

        if (rx_pkts == sc->rx_budget) {
            fp->eth_q_stats.rx_budget_reached++;
            break;
        }
    } /* while work to do */

    fp->rx_bd_cons = bd_cons;
    fp->rx_bd_prod = bd_prod_fw;
    fp->rx_cq_cons = sw_cq_cons;
    fp->rx_cq_prod = sw_cq_prod;

    /* Update producers */
    bxe_update_rx_prod(sc, fp, bd_prod_fw, sw_cq_prod, fp->rx_sge_prod);

    fp->eth_q_stats.rx_pkts += rx_pkts;
    fp->eth_q_stats.rx_calls++;

    BXE_FP_RX_UNLOCK(fp);

    return (sw_cq_cons != hw_cq_cons);
}

static uint16_t
bxe_free_tx_pkt(struct bxe_softc    *sc,
                struct bxe_fastpath *fp,
                uint16_t            idx)
{
    struct bxe_sw_tx_bd *tx_buf = &fp->tx_mbuf_chain[idx];
    struct eth_tx_start_bd *tx_start_bd;
    uint16_t bd_idx = TX_BD(tx_buf->first_bd);
    uint16_t new_cons;
    int nbd;

    /* unmap the mbuf from non-paged memory */
    bus_dmamap_unload(fp->tx_mbuf_tag, tx_buf->m_map);

    tx_start_bd = &fp->tx_chain[bd_idx].start_bd;
    nbd = le16toh(tx_start_bd->nbd) - 1;

    new_cons = (tx_buf->first_bd + nbd);

    /* free the mbuf */
    if (__predict_true(tx_buf->m != NULL)) {
        m_freem(tx_buf->m);
        fp->eth_q_stats.mbuf_alloc_tx--;
    } else {
        fp->eth_q_stats.tx_chain_lost_mbuf++;
    }

    tx_buf->m = NULL;
    tx_buf->first_bd = 0;

    return (new_cons);
}

/* transmit timeout watchdog */
static int
bxe_watchdog(struct bxe_softc    *sc,
             struct bxe_fastpath *fp)
{
    BXE_FP_TX_LOCK(fp);

    if ((fp->watchdog_timer == 0) || (--fp->watchdog_timer)) {
        BXE_FP_TX_UNLOCK(fp);
        return (0);
    }

    BLOGE(sc, "TX watchdog timeout on fp[%02d], resetting!\n", fp->index);

    BXE_FP_TX_UNLOCK(fp);
    BXE_SET_ERROR_BIT(sc, BXE_ERR_TXQ_STUCK);
    taskqueue_enqueue_timeout(taskqueue_thread,
        &sc->sp_err_timeout_task, hz/10);

    return (-1);
}

/* processes transmit completions */
static uint8_t
bxe_txeof(struct bxe_softc    *sc,
          struct bxe_fastpath *fp)
{
    if_t ifp = sc->ifp;
    uint16_t bd_cons, hw_cons, sw_cons, pkt_cons;
    uint16_t tx_bd_avail;

    BXE_FP_TX_LOCK_ASSERT(fp);

    bd_cons = fp->tx_bd_cons;
    hw_cons = le16toh(*fp->tx_cons_sb);
    sw_cons = fp->tx_pkt_cons;

    while (sw_cons != hw_cons) {
        pkt_cons = TX_BD(sw_cons);

        BLOGD(sc, DBG_TX,
              "TX: fp[%d]: hw_cons=%u sw_cons=%u pkt_cons=%u\n",
              fp->index, hw_cons, sw_cons, pkt_cons);

        bd_cons = bxe_free_tx_pkt(sc, fp, pkt_cons);

        sw_cons++;
    }

    fp->tx_pkt_cons = sw_cons;
    fp->tx_bd_cons  = bd_cons;

    BLOGD(sc, DBG_TX,
          "TX done: fp[%d]: hw_cons=%u sw_cons=%u sw_prod=%u\n",
          fp->index, hw_cons, fp->tx_pkt_cons, fp->tx_pkt_prod);

    mb();

    tx_bd_avail = bxe_tx_avail(sc, fp);

    if (tx_bd_avail < BXE_TX_CLEANUP_THRESHOLD) {
        if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
    } else {
        if_setdrvflagbits(ifp, 0, IFF_DRV_OACTIVE);
    }

    if (fp->tx_pkt_prod != fp->tx_pkt_cons) {
        /* reset the watchdog timer if there are pending transmits */
        fp->watchdog_timer = BXE_TX_TIMEOUT;
        return (TRUE);
    } else {
        /* clear watchdog when there are no pending transmits */
        fp->watchdog_timer = 0;
        return (FALSE);
    }
}

static void
bxe_drain_tx_queues(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int i, count;

    /* wait until all TX fastpath tasks have completed */
    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];

        count = 1000;

        while (bxe_has_tx_work(fp)) {

            BXE_FP_TX_LOCK(fp);
            bxe_txeof(sc, fp);
            BXE_FP_TX_UNLOCK(fp);

            if (count == 0) {
                BLOGE(sc, "Timeout waiting for fp[%d] "
                          "transmits to complete!\n", i);
                bxe_panic(sc, ("tx drain failure\n"));
                return;
            }

            count--;
            DELAY(1000);
            rmb();
        }
    }

    return;
}

static int
bxe_del_all_macs(struct bxe_softc          *sc,
                 struct ecore_vlan_mac_obj *mac_obj,
                 int                       mac_type,
                 uint8_t                   wait_for_comp)
{
    unsigned long ramrod_flags = 0, vlan_mac_flags = 0;
    int rc;

    /* wait for completion of requested */
    if (wait_for_comp) {
        bxe_set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
    }

    /* Set the mac type of addresses we want to clear */
    bxe_set_bit(mac_type, &vlan_mac_flags);

    rc = mac_obj->delete_all(sc, mac_obj, &vlan_mac_flags, &ramrod_flags);
    if (rc < 0) {
        BLOGE(sc, "Failed to delete MACs (%d) mac_type %d wait_for_comp 0x%x\n",
            rc, mac_type, wait_for_comp);
    }

    return (rc);
}

static int
bxe_fill_accept_flags(struct bxe_softc *sc,
                      uint32_t         rx_mode,
                      unsigned long    *rx_accept_flags,
                      unsigned long    *tx_accept_flags)
{
    /* Clear the flags first */
    *rx_accept_flags = 0;
    *tx_accept_flags = 0;

    switch (rx_mode) {
    case BXE_RX_MODE_NONE:
        /*
         * 'drop all' supersedes any accept flags that may have been
         * passed to the function.
         */
        break;

    case BXE_RX_MODE_NORMAL:
        bxe_set_bit(ECORE_ACCEPT_UNICAST, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_MULTICAST, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_BROADCAST, rx_accept_flags);

        /* internal switching mode */
        bxe_set_bit(ECORE_ACCEPT_UNICAST, tx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_MULTICAST, tx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_BROADCAST, tx_accept_flags);

        break;

    case BXE_RX_MODE_ALLMULTI:
        bxe_set_bit(ECORE_ACCEPT_UNICAST, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_ALL_MULTICAST, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_BROADCAST, rx_accept_flags);

        /* internal switching mode */
        bxe_set_bit(ECORE_ACCEPT_UNICAST, tx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_ALL_MULTICAST, tx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_BROADCAST, tx_accept_flags);

        break;

    case BXE_RX_MODE_PROMISC:
        /*
         * According to deffinition of SI mode, iface in promisc mode
         * should receive matched and unmatched (in resolution of port)
         * unicast packets.
         */
        bxe_set_bit(ECORE_ACCEPT_UNMATCHED, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_UNICAST, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_ALL_MULTICAST, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_BROADCAST, rx_accept_flags);

        /* internal switching mode */
        bxe_set_bit(ECORE_ACCEPT_ALL_MULTICAST, tx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_BROADCAST, tx_accept_flags);

        if (IS_MF_SI(sc)) {
            bxe_set_bit(ECORE_ACCEPT_ALL_UNICAST, tx_accept_flags);
        } else {
            bxe_set_bit(ECORE_ACCEPT_UNICAST, tx_accept_flags);
        }

        break;

    default:
        BLOGE(sc, "Unknown rx_mode (0x%x)\n", rx_mode);
        return (-1);
    }

    /* Set ACCEPT_ANY_VLAN as we do not enable filtering by VLAN */
    if (rx_mode != BXE_RX_MODE_NONE) {
        bxe_set_bit(ECORE_ACCEPT_ANY_VLAN, rx_accept_flags);
        bxe_set_bit(ECORE_ACCEPT_ANY_VLAN, tx_accept_flags);
    }

    return (0);
}

static int
bxe_set_q_rx_mode(struct bxe_softc *sc,
                  uint8_t          cl_id,
                  unsigned long    rx_mode_flags,
                  unsigned long    rx_accept_flags,
                  unsigned long    tx_accept_flags,
                  unsigned long    ramrod_flags)
{
    struct ecore_rx_mode_ramrod_params ramrod_param;
    int rc;

    memset(&ramrod_param, 0, sizeof(ramrod_param));

    /* Prepare ramrod parameters */
    ramrod_param.cid = 0;
    ramrod_param.cl_id = cl_id;
    ramrod_param.rx_mode_obj = &sc->rx_mode_obj;
    ramrod_param.func_id = SC_FUNC(sc);

    ramrod_param.pstate = &sc->sp_state;
    ramrod_param.state = ECORE_FILTER_RX_MODE_PENDING;

    ramrod_param.rdata = BXE_SP(sc, rx_mode_rdata);
    ramrod_param.rdata_mapping = BXE_SP_MAPPING(sc, rx_mode_rdata);

    bxe_set_bit(ECORE_FILTER_RX_MODE_PENDING, &sc->sp_state);

    ramrod_param.ramrod_flags = ramrod_flags;
    ramrod_param.rx_mode_flags = rx_mode_flags;

    ramrod_param.rx_accept_flags = rx_accept_flags;
    ramrod_param.tx_accept_flags = tx_accept_flags;

    rc = ecore_config_rx_mode(sc, &ramrod_param);
    if (rc < 0) {
        BLOGE(sc, "Set rx_mode %d cli_id 0x%x rx_mode_flags 0x%x "
            "rx_accept_flags 0x%x tx_accept_flags 0x%x "
            "ramrod_flags 0x%x rc %d failed\n", sc->rx_mode, cl_id,
            (uint32_t)rx_mode_flags, (uint32_t)rx_accept_flags,
            (uint32_t)tx_accept_flags, (uint32_t)ramrod_flags, rc);
        return (rc);
    }

    return (0);
}

static int
bxe_set_storm_rx_mode(struct bxe_softc *sc)
{
    unsigned long rx_mode_flags = 0, ramrod_flags = 0;
    unsigned long rx_accept_flags = 0, tx_accept_flags = 0;
    int rc;

    rc = bxe_fill_accept_flags(sc, sc->rx_mode, &rx_accept_flags,
                               &tx_accept_flags);
    if (rc) {
        return (rc);
    }

    bxe_set_bit(RAMROD_RX, &ramrod_flags);
    bxe_set_bit(RAMROD_TX, &ramrod_flags);

    /* XXX ensure all fastpath have same cl_id and/or move it to bxe_softc */
    return (bxe_set_q_rx_mode(sc, sc->fp[0].cl_id, rx_mode_flags,
                              rx_accept_flags, tx_accept_flags,
                              ramrod_flags));
}

/* returns the "mcp load_code" according to global load_count array */
static int
bxe_nic_load_no_mcp(struct bxe_softc *sc)
{
    int path = SC_PATH(sc);
    int port = SC_PORT(sc);

    BLOGI(sc, "NO MCP - load counts[%d]      %d, %d, %d\n",
          path, load_count[path][0], load_count[path][1],
          load_count[path][2]);
    load_count[path][0]++;
    load_count[path][1 + port]++;
    BLOGI(sc, "NO MCP - new load counts[%d]  %d, %d, %d\n",
          path, load_count[path][0], load_count[path][1],
          load_count[path][2]);
    if (load_count[path][0] == 1) {
        return (FW_MSG_CODE_DRV_LOAD_COMMON);
    } else if (load_count[path][1 + port] == 1) {
        return (FW_MSG_CODE_DRV_LOAD_PORT);
    } else {
        return (FW_MSG_CODE_DRV_LOAD_FUNCTION);
    }
}

/* returns the "mcp load_code" according to global load_count array */
static int
bxe_nic_unload_no_mcp(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    int path = SC_PATH(sc);

    BLOGI(sc, "NO MCP - load counts[%d]      %d, %d, %d\n",
          path, load_count[path][0], load_count[path][1],
          load_count[path][2]);
    load_count[path][0]--;
    load_count[path][1 + port]--;
    BLOGI(sc, "NO MCP - new load counts[%d]  %d, %d, %d\n",
          path, load_count[path][0], load_count[path][1],
          load_count[path][2]);
    if (load_count[path][0] == 0) {
        return (FW_MSG_CODE_DRV_UNLOAD_COMMON);
    } else if (load_count[path][1 + port] == 0) {
        return (FW_MSG_CODE_DRV_UNLOAD_PORT);
    } else {
        return (FW_MSG_CODE_DRV_UNLOAD_FUNCTION);
    }
}

/* request unload mode from the MCP: COMMON, PORT or FUNCTION */
static uint32_t
bxe_send_unload_req(struct bxe_softc *sc,
                    int              unload_mode)
{
    uint32_t reset_code = 0;

    /* Select the UNLOAD request mode */
    if (unload_mode == UNLOAD_NORMAL) {
        reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;
    } else {
        reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;
    }

    /* Send the request to the MCP */
    if (!BXE_NOMCP(sc)) {
        reset_code = bxe_fw_command(sc, reset_code, 0);
    } else {
        reset_code = bxe_nic_unload_no_mcp(sc);
    }

    return (reset_code);
}

/* send UNLOAD_DONE command to the MCP */
static void
bxe_send_unload_done(struct bxe_softc *sc,
                     uint8_t          keep_link)
{
    uint32_t reset_param =
        keep_link ? DRV_MSG_CODE_UNLOAD_SKIP_LINK_RESET : 0;

    /* Report UNLOAD_DONE to MCP */
    if (!BXE_NOMCP(sc)) {
        bxe_fw_command(sc, DRV_MSG_CODE_UNLOAD_DONE, reset_param);
    }
}

static int
bxe_func_wait_started(struct bxe_softc *sc)
{
    int tout = 50;

    if (!sc->port.pmf) {
        return (0);
    }

    /*
     * (assumption: No Attention from MCP at this stage)
     * PMF probably in the middle of TX disable/enable transaction
     * 1. Sync IRS for default SB
     * 2. Sync SP queue - this guarantees us that attention handling started
     * 3. Wait, that TX disable/enable transaction completes
     *
     * 1+2 guarantee that if DCBX attention was scheduled it already changed
     * pending bit of transaction from STARTED-->TX_STOPPED, if we already
     * received completion for the transaction the state is TX_STOPPED.
     * State will return to STARTED after completion of TX_STOPPED-->STARTED
     * transaction.
     */

    /* XXX make sure default SB ISR is done */
    /* need a way to synchronize an irq (intr_mtx?) */

    /* XXX flush any work queues */

    while (ecore_func_get_state(sc, &sc->func_obj) !=
           ECORE_F_STATE_STARTED && tout--) {
        DELAY(20000);
    }

    if (ecore_func_get_state(sc, &sc->func_obj) != ECORE_F_STATE_STARTED) {
        /*
         * Failed to complete the transaction in a "good way"
         * Force both transactions with CLR bit.
         */
        struct ecore_func_state_params func_params = { NULL };

        BLOGE(sc, "Unexpected function state! "
                  "Forcing STARTED-->TX_STOPPED-->STARTED\n");

        func_params.f_obj = &sc->func_obj;
        bxe_set_bit(RAMROD_DRV_CLR_ONLY, &func_params.ramrod_flags);

        /* STARTED-->TX_STOPPED */
        func_params.cmd = ECORE_F_CMD_TX_STOP;
        ecore_func_state_change(sc, &func_params);

        /* TX_STOPPED-->STARTED */
        func_params.cmd = ECORE_F_CMD_TX_START;
        return (ecore_func_state_change(sc, &func_params));
    }

    return (0);
}

static int
bxe_stop_queue(struct bxe_softc *sc,
               int              index)
{
    struct bxe_fastpath *fp = &sc->fp[index];
    struct ecore_queue_state_params q_params = { NULL };
    int rc;

    BLOGD(sc, DBG_LOAD, "stopping queue %d cid %d\n", index, fp->index);

    q_params.q_obj = &sc->sp_objs[fp->index].q_obj;
    /* We want to wait for completion in this context */
    bxe_set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);

    /* Stop the primary connection: */

    /* ...halt the connection */
    q_params.cmd = ECORE_Q_CMD_HALT;
    rc = ecore_queue_state_change(sc, &q_params);
    if (rc) {
        return (rc);
    }

    /* ...terminate the connection */
    q_params.cmd = ECORE_Q_CMD_TERMINATE;
    memset(&q_params.params.terminate, 0, sizeof(q_params.params.terminate));
    q_params.params.terminate.cid_index = FIRST_TX_COS_INDEX;
    rc = ecore_queue_state_change(sc, &q_params);
    if (rc) {
        return (rc);
    }

    /* ...delete cfc entry */
    q_params.cmd = ECORE_Q_CMD_CFC_DEL;
    memset(&q_params.params.cfc_del, 0, sizeof(q_params.params.cfc_del));
    q_params.params.cfc_del.cid_index = FIRST_TX_COS_INDEX;
    return (ecore_queue_state_change(sc, &q_params));
}

/* wait for the outstanding SP commands */
static inline uint8_t
bxe_wait_sp_comp(struct bxe_softc *sc,
                 unsigned long    mask)
{
    unsigned long tmp;
    int tout = 5000; /* wait for 5 secs tops */

    while (tout--) {
        mb();
        if (!(atomic_load_acq_long(&sc->sp_state) & mask)) {
            return (TRUE);
        }

        DELAY(1000);
    }

    mb();

    tmp = atomic_load_acq_long(&sc->sp_state);
    if (tmp & mask) {
        BLOGE(sc, "Filtering completion timed out: "
                  "sp_state 0x%lx, mask 0x%lx\n",
              tmp, mask);
        return (FALSE);
    }

    return (FALSE);
}

static int
bxe_func_stop(struct bxe_softc *sc)
{
    struct ecore_func_state_params func_params = { NULL };
    int rc;

    /* prepare parameters for function state transitions */
    bxe_set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);
    func_params.f_obj = &sc->func_obj;
    func_params.cmd = ECORE_F_CMD_STOP;

    /*
     * Try to stop the function the 'good way'. If it fails (in case
     * of a parity error during bxe_chip_cleanup()) and we are
     * not in a debug mode, perform a state transaction in order to
     * enable further HW_RESET transaction.
     */
    rc = ecore_func_state_change(sc, &func_params);
    if (rc) {
        BLOGE(sc, "FUNC_STOP ramrod failed. "
                  "Running a dry transaction (%d)\n", rc);
        bxe_set_bit(RAMROD_DRV_CLR_ONLY, &func_params.ramrod_flags);
        return (ecore_func_state_change(sc, &func_params));
    }

    return (0);
}

static int
bxe_reset_hw(struct bxe_softc *sc,
             uint32_t         load_code)
{
    struct ecore_func_state_params func_params = { NULL };

    /* Prepare parameters for function state transitions */
    bxe_set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);

    func_params.f_obj = &sc->func_obj;
    func_params.cmd = ECORE_F_CMD_HW_RESET;

    func_params.params.hw_init.load_phase = load_code;

    return (ecore_func_state_change(sc, &func_params));
}

static void
bxe_int_disable_sync(struct bxe_softc *sc,
                     int              disable_hw)
{
    if (disable_hw) {
        /* prevent the HW from sending interrupts */
        bxe_int_disable(sc);
    }

    /* XXX need a way to synchronize ALL irqs (intr_mtx?) */
    /* make sure all ISRs are done */

    /* XXX make sure sp_task is not running */
    /* cancel and flush work queues */
}

static void
bxe_chip_cleanup(struct bxe_softc *sc,
                 uint32_t         unload_mode,
                 uint8_t          keep_link)
{
    int port = SC_PORT(sc);
    struct ecore_mcast_ramrod_params rparam = { NULL };
    uint32_t reset_code;
    int i, rc = 0;

    bxe_drain_tx_queues(sc);

    /* give HW time to discard old tx messages */
    DELAY(1000);

    /* Clean all ETH MACs */
    rc = bxe_del_all_macs(sc, &sc->sp_objs[0].mac_obj, ECORE_ETH_MAC, FALSE);
    if (rc < 0) {
        BLOGE(sc, "Failed to delete all ETH MACs (%d)\n", rc);
    }

    /* Clean up UC list  */
    rc = bxe_del_all_macs(sc, &sc->sp_objs[0].mac_obj, ECORE_UC_LIST_MAC, TRUE);
    if (rc < 0) {
        BLOGE(sc, "Failed to delete UC MACs list (%d)\n", rc);
    }

    /* Disable LLH */
    if (!CHIP_IS_E1(sc)) {
        REG_WR(sc, NIG_REG_LLH0_FUNC_EN + port*8, 0);
    }

    /* Set "drop all" to stop Rx */

    /*
     * We need to take the BXE_MCAST_LOCK() here in order to prevent
     * a race between the completion code and this code.
     */
    BXE_MCAST_LOCK(sc);

    if (bxe_test_bit(ECORE_FILTER_RX_MODE_PENDING, &sc->sp_state)) {
        bxe_set_bit(ECORE_FILTER_RX_MODE_SCHED, &sc->sp_state);
    } else {
        bxe_set_storm_rx_mode(sc);
    }

    /* Clean up multicast configuration */
    rparam.mcast_obj = &sc->mcast_obj;
    rc = ecore_config_mcast(sc, &rparam, ECORE_MCAST_CMD_DEL);
    if (rc < 0) {
        BLOGE(sc, "Failed to send DEL MCAST command (%d)\n", rc);
    }

    BXE_MCAST_UNLOCK(sc);

    // XXX bxe_iov_chip_cleanup(sc);

    /*
     * Send the UNLOAD_REQUEST to the MCP. This will return if
     * this function should perform FUNCTION, PORT, or COMMON HW
     * reset.
     */
    reset_code = bxe_send_unload_req(sc, unload_mode);

    /*
     * (assumption: No Attention from MCP at this stage)
     * PMF probably in the middle of TX disable/enable transaction
     */
    rc = bxe_func_wait_started(sc);
    if (rc) {
        BLOGE(sc, "bxe_func_wait_started failed (%d)\n", rc);
    }

    /*
     * Close multi and leading connections
     * Completions for ramrods are collected in a synchronous way
     */
    for (i = 0; i < sc->num_queues; i++) {
        if (bxe_stop_queue(sc, i)) {
            goto unload_error;
        }
    }

    /*
     * If SP settings didn't get completed so far - something
     * very wrong has happen.
     */
    if (!bxe_wait_sp_comp(sc, ~0x0UL)) {
        BLOGE(sc, "Common slow path ramrods got stuck!(%d)\n", rc);
    }

unload_error:

    rc = bxe_func_stop(sc);
    if (rc) {
        BLOGE(sc, "Function stop failed!(%d)\n", rc);
    }

    /* disable HW interrupts */
    bxe_int_disable_sync(sc, TRUE);

    /* detach interrupts */
    bxe_interrupt_detach(sc);

    /* Reset the chip */
    rc = bxe_reset_hw(sc, reset_code);
    if (rc) {
        BLOGE(sc, "Hardware reset failed(%d)\n", rc);
    }

    /* Report UNLOAD_DONE to MCP */
    bxe_send_unload_done(sc, keep_link);
}

static void
bxe_disable_close_the_gate(struct bxe_softc *sc)
{
    uint32_t val;
    int port = SC_PORT(sc);

    BLOGD(sc, DBG_LOAD,
          "Disabling 'close the gates'\n");

    if (CHIP_IS_E1(sc)) {
        uint32_t addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
                               MISC_REG_AEU_MASK_ATTN_FUNC_0;
        val = REG_RD(sc, addr);
        val &= ~(0x300);
        REG_WR(sc, addr, val);
    } else {
        val = REG_RD(sc, MISC_REG_AEU_GENERAL_MASK);
        val &= ~(MISC_AEU_GENERAL_MASK_REG_AEU_PXP_CLOSE_MASK |
                 MISC_AEU_GENERAL_MASK_REG_AEU_NIG_CLOSE_MASK);
        REG_WR(sc, MISC_REG_AEU_GENERAL_MASK, val);
    }
}

/*
 * Cleans the object that have internal lists without sending
 * ramrods. Should be run when interrutps are disabled.
 */
static void
bxe_squeeze_objects(struct bxe_softc *sc)
{
    unsigned long ramrod_flags = 0, vlan_mac_flags = 0;
    struct ecore_mcast_ramrod_params rparam = { NULL };
    struct ecore_vlan_mac_obj *mac_obj = &sc->sp_objs->mac_obj;
    int rc;

    /* Cleanup MACs' object first... */

    /* Wait for completion of requested */
    bxe_set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
    /* Perform a dry cleanup */
    bxe_set_bit(RAMROD_DRV_CLR_ONLY, &ramrod_flags);

    /* Clean ETH primary MAC */
    bxe_set_bit(ECORE_ETH_MAC, &vlan_mac_flags);
    rc = mac_obj->delete_all(sc, &sc->sp_objs->mac_obj, &vlan_mac_flags,
                             &ramrod_flags);
    if (rc != 0) {
        BLOGE(sc, "Failed to clean ETH MACs (%d)\n", rc);
    }

    /* Cleanup UC list */
    vlan_mac_flags = 0;
    bxe_set_bit(ECORE_UC_LIST_MAC, &vlan_mac_flags);
    rc = mac_obj->delete_all(sc, mac_obj, &vlan_mac_flags,
                             &ramrod_flags);
    if (rc != 0) {
        BLOGE(sc, "Failed to clean UC list MACs (%d)\n", rc);
    }

    /* Now clean mcast object... */

    rparam.mcast_obj = &sc->mcast_obj;
    bxe_set_bit(RAMROD_DRV_CLR_ONLY, &rparam.ramrod_flags);

    /* Add a DEL command... */
    rc = ecore_config_mcast(sc, &rparam, ECORE_MCAST_CMD_DEL);
    if (rc < 0) {
        BLOGE(sc, "Failed to send DEL MCAST command (%d)\n", rc);
    }

    /* now wait until all pending commands are cleared */

    rc = ecore_config_mcast(sc, &rparam, ECORE_MCAST_CMD_CONT);
    while (rc != 0) {
        if (rc < 0) {
            BLOGE(sc, "Failed to clean MCAST object (%d)\n", rc);
            return;
        }

        rc = ecore_config_mcast(sc, &rparam, ECORE_MCAST_CMD_CONT);
    }
}

/* stop the controller */
static __noinline int
bxe_nic_unload(struct bxe_softc *sc,
               uint32_t         unload_mode,
               uint8_t          keep_link)
{
    uint8_t global = FALSE;
    uint32_t val;
    int i;

    BXE_CORE_LOCK_ASSERT(sc);

    if_setdrvflagbits(sc->ifp, 0, IFF_DRV_RUNNING);

    for (i = 0; i < sc->num_queues; i++) {
        struct bxe_fastpath *fp;

        fp = &sc->fp[i];
	fp->watchdog_timer = 0;
        BXE_FP_TX_LOCK(fp);
        BXE_FP_TX_UNLOCK(fp);
    }

    BLOGD(sc, DBG_LOAD, "Starting NIC unload...\n");

    /* mark driver as unloaded in shmem2 */
    if (IS_PF(sc) && SHMEM2_HAS(sc, drv_capabilities_flag)) {
        val = SHMEM2_RD(sc, drv_capabilities_flag[SC_FW_MB_IDX(sc)]);
        SHMEM2_WR(sc, drv_capabilities_flag[SC_FW_MB_IDX(sc)],
                  val & ~DRV_FLAGS_CAPABILITIES_LOADED_L2);
    }

    if (IS_PF(sc) && sc->recovery_state != BXE_RECOVERY_DONE &&
        (sc->state == BXE_STATE_CLOSED || sc->state == BXE_STATE_ERROR)) {

	if(CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) {
            /*
             * We can get here if the driver has been unloaded
             * during parity error recovery and is either waiting for a
             * leader to complete or for other functions to unload and
             * then ifconfig down has been issued. In this case we want to
             * unload and let other functions to complete a recovery
             * process.
             */
            sc->recovery_state = BXE_RECOVERY_DONE;
            sc->is_leader = 0;
            bxe_release_leader_lock(sc);
            mb();
            BLOGD(sc, DBG_LOAD, "Releasing a leadership...\n");
	}
        BLOGE(sc, "Can't unload in closed or error state recover_state 0x%x"
            " state = 0x%x\n", sc->recovery_state, sc->state);
        return (-1);
    }

    /*
     * Nothing to do during unload if previous bxe_nic_load()
     * did not completed successfully - all resourses are released.
     */
    if ((sc->state == BXE_STATE_CLOSED) ||
        (sc->state == BXE_STATE_ERROR)) {
        return (0);
    }

    sc->state = BXE_STATE_CLOSING_WAITING_HALT;
    mb();

    /* stop tx */
    bxe_tx_disable(sc);

    sc->rx_mode = BXE_RX_MODE_NONE;
    /* XXX set rx mode ??? */

    if (IS_PF(sc) && !sc->grcdump_done) {
        /* set ALWAYS_ALIVE bit in shmem */
        sc->fw_drv_pulse_wr_seq |= DRV_PULSE_ALWAYS_ALIVE;

        bxe_drv_pulse(sc);

        bxe_stats_handle(sc, STATS_EVENT_STOP);
        bxe_save_statistics(sc);
    }

    /* wait till consumers catch up with producers in all queues */
    bxe_drain_tx_queues(sc);

    /* if VF indicate to PF this function is going down (PF will delete sp
     * elements and clear initializations
     */
    if (IS_VF(sc)) {
        ; /* bxe_vfpf_close_vf(sc); */
    } else if (unload_mode != UNLOAD_RECOVERY) {
        /* if this is a normal/close unload need to clean up chip */
        if (!sc->grcdump_done)
            bxe_chip_cleanup(sc, unload_mode, keep_link);
    } else {
        /* Send the UNLOAD_REQUEST to the MCP */
        bxe_send_unload_req(sc, unload_mode);

        /*
         * Prevent transactions to host from the functions on the
         * engine that doesn't reset global blocks in case of global
         * attention once gloabl blocks are reset and gates are opened
         * (the engine which leader will perform the recovery
         * last).
         */
        if (!CHIP_IS_E1x(sc)) {
            bxe_pf_disable(sc);
        }

        /* disable HW interrupts */
        bxe_int_disable_sync(sc, TRUE);

        /* detach interrupts */
        bxe_interrupt_detach(sc);

        /* Report UNLOAD_DONE to MCP */
        bxe_send_unload_done(sc, FALSE);
    }

    /*
     * At this stage no more interrupts will arrive so we may safely clean
     * the queue'able objects here in case they failed to get cleaned so far.
     */
    if (IS_PF(sc)) {
        bxe_squeeze_objects(sc);
    }

    /* There should be no more pending SP commands at this stage */
    sc->sp_state = 0;

    sc->port.pmf = 0;

    bxe_free_fp_buffers(sc);

    if (IS_PF(sc)) {
        bxe_free_mem(sc);
    }

    bxe_free_fw_stats_mem(sc);

    sc->state = BXE_STATE_CLOSED;

    /*
     * Check if there are pending parity attentions. If there are - set
     * RECOVERY_IN_PROGRESS.
     */
    if (IS_PF(sc) && bxe_chk_parity_attn(sc, &global, FALSE)) {
        bxe_set_reset_in_progress(sc);

        /* Set RESET_IS_GLOBAL if needed */
        if (global) {
            bxe_set_reset_global(sc);
        }
    }

    /*
     * The last driver must disable a "close the gate" if there is no
     * parity attention or "process kill" pending.
     */
    if (IS_PF(sc) && !bxe_clear_pf_load(sc) &&
        bxe_reset_is_done(sc, SC_PATH(sc))) {
        bxe_disable_close_the_gate(sc);
    }

    BLOGD(sc, DBG_LOAD, "Ended NIC unload\n");

    bxe_link_report(sc);

    return (0);
}

/*
 * Called by the OS to set various media options (i.e. link, speed, etc.) when
 * the user runs "ifconfig bxe media ..." or "ifconfig bxe mediaopt ...".
 */
static int
bxe_ifmedia_update(struct ifnet  *ifp)
{
    struct bxe_softc *sc = (struct bxe_softc *)if_getsoftc(ifp);
    struct ifmedia *ifm;

    ifm = &sc->ifmedia;

    /* We only support Ethernet media type. */
    if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER) {
        return (EINVAL);
    }

    switch (IFM_SUBTYPE(ifm->ifm_media)) {
    case IFM_AUTO:
         break;
    case IFM_10G_CX4:
    case IFM_10G_SR:
    case IFM_10G_T:
    case IFM_10G_TWINAX:
    default:
        /* We don't support changing the media type. */
        BLOGD(sc, DBG_LOAD, "Invalid media type (%d)\n",
              IFM_SUBTYPE(ifm->ifm_media));
        return (EINVAL);
    }

    return (0);
}

/*
 * Called by the OS to get the current media status (i.e. link, speed, etc.).
 */
static void
bxe_ifmedia_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
    struct bxe_softc *sc = if_getsoftc(ifp);

    /* Bug 165447: the 'ifconfig' tool skips printing of the "status: ..."
       line if the IFM_AVALID flag is *NOT* set. So we need to set this
       flag unconditionally (irrespective of the admininistrative
       'up/down' state of the interface) to ensure that that line is always
       displayed.
    */
    ifmr->ifm_status = IFM_AVALID;

    /* Setup the default interface info. */
    ifmr->ifm_active = IFM_ETHER;

    /* Report link down if the driver isn't running. */
    if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
        ifmr->ifm_active |= IFM_NONE;
        BLOGD(sc, DBG_PHY, "in %s : nic still not loaded fully\n", __func__);
        BLOGD(sc, DBG_PHY, "in %s : link_up (1) : %d\n",
                __func__, sc->link_vars.link_up);
        return;
    }


    if (sc->link_vars.link_up) {
        ifmr->ifm_status |= IFM_ACTIVE;
        ifmr->ifm_active |= IFM_FDX;
    } else {
        ifmr->ifm_active |= IFM_NONE;
        BLOGD(sc, DBG_PHY, "in %s : setting IFM_NONE\n",
                __func__);
        return;
    }

    ifmr->ifm_active |= sc->media;
    return;
}

static void
bxe_handle_chip_tq(void *context,
                   int  pending)
{
    struct bxe_softc *sc = (struct bxe_softc *)context;
    long work = atomic_load_acq_long(&sc->chip_tq_flags);

    switch (work)
    {

    case CHIP_TQ_REINIT:
        if (if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) {
            /* restart the interface */
            BLOGD(sc, DBG_LOAD, "Restarting the interface...\n");
            bxe_periodic_stop(sc);
            BXE_CORE_LOCK(sc);
            bxe_stop_locked(sc);
            bxe_init_locked(sc);
            BXE_CORE_UNLOCK(sc);
        }
        break;

    default:
        break;
    }
}

/*
 * Handles any IOCTL calls from the operating system.
 *
 * Returns:
 *   0 = Success, >0 Failure
 */
static int
bxe_ioctl(if_t ifp,
          u_long       command,
          caddr_t      data)
{
    struct bxe_softc *sc = if_getsoftc(ifp);
    struct ifreq *ifr = (struct ifreq *)data;
    int mask = 0;
    int reinit = 0;
    int error = 0;

    int mtu_min = (ETH_MIN_PACKET_SIZE - ETH_HLEN);
    int mtu_max = (MJUM9BYTES - ETH_OVERHEAD - IP_HEADER_ALIGNMENT_PADDING);

    switch (command)
    {
    case SIOCSIFMTU:
        BLOGD(sc, DBG_IOCTL, "Received SIOCSIFMTU ioctl (mtu=%d)\n",
              ifr->ifr_mtu);

        if (sc->mtu == ifr->ifr_mtu) {
            /* nothing to change */
            break;
        }

        if ((ifr->ifr_mtu < mtu_min) || (ifr->ifr_mtu > mtu_max)) {
            BLOGE(sc, "Unsupported MTU size %d (range is %d-%d)\n",
                  ifr->ifr_mtu, mtu_min, mtu_max);
            error = EINVAL;
            break;
        }

        atomic_store_rel_int((volatile unsigned int *)&sc->mtu,
                             (unsigned long)ifr->ifr_mtu);
	/* 
        atomic_store_rel_long((volatile unsigned long *)&if_getmtu(ifp),
                              (unsigned long)ifr->ifr_mtu);
	XXX - Not sure why it needs to be atomic
	*/
	if_setmtu(ifp, ifr->ifr_mtu);
        reinit = 1;
        break;

    case SIOCSIFFLAGS:
        /* toggle the interface state up or down */
        BLOGD(sc, DBG_IOCTL, "Received SIOCSIFFLAGS ioctl\n");

	BXE_CORE_LOCK(sc);
        /* check if the interface is up */
        if (if_getflags(ifp) & IFF_UP) {
            if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
                /* set the receive mode flags */
                bxe_set_rx_mode(sc);
            } else if(sc->state != BXE_STATE_DISABLED) {
		bxe_init_locked(sc);
            }
        } else {
            if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
		bxe_periodic_stop(sc);
		bxe_stop_locked(sc);
            }
        }
	BXE_CORE_UNLOCK(sc);

        break;

    case SIOCADDMULTI:
    case SIOCDELMULTI:
        /* add/delete multicast addresses */
        BLOGD(sc, DBG_IOCTL, "Received SIOCADDMULTI/SIOCDELMULTI ioctl\n");

        /* check if the interface is up */
        if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
            /* set the receive mode flags */
	    BXE_CORE_LOCK(sc);
            bxe_set_rx_mode(sc);
	    BXE_CORE_UNLOCK(sc); 
        }

        break;

    case SIOCSIFCAP:
        /* find out which capabilities have changed */
        mask = (ifr->ifr_reqcap ^ if_getcapenable(ifp));

        BLOGD(sc, DBG_IOCTL, "Received SIOCSIFCAP ioctl (mask=0x%08x)\n",
              mask);

        /* toggle the LRO capabilites enable flag */
        if (mask & IFCAP_LRO) {
	    if_togglecapenable(ifp, IFCAP_LRO);
            BLOGD(sc, DBG_IOCTL, "Turning LRO %s\n",
                  (if_getcapenable(ifp) & IFCAP_LRO) ? "ON" : "OFF");
            reinit = 1;
        }

        /* toggle the TXCSUM checksum capabilites enable flag */
        if (mask & IFCAP_TXCSUM) {
	    if_togglecapenable(ifp, IFCAP_TXCSUM);
            BLOGD(sc, DBG_IOCTL, "Turning TXCSUM %s\n",
                  (if_getcapenable(ifp) & IFCAP_TXCSUM) ? "ON" : "OFF");
            if (if_getcapenable(ifp) & IFCAP_TXCSUM) {
                if_sethwassistbits(ifp, (CSUM_IP      | 
                                    CSUM_TCP      |
                                    CSUM_UDP      |
                                    CSUM_TSO      |
                                    CSUM_TCP_IPV6 |
                                    CSUM_UDP_IPV6), 0);
            } else {
		if_clearhwassist(ifp); /* XXX */
            }
        }

        /* toggle the RXCSUM checksum capabilities enable flag */
        if (mask & IFCAP_RXCSUM) {
	    if_togglecapenable(ifp, IFCAP_RXCSUM);
            BLOGD(sc, DBG_IOCTL, "Turning RXCSUM %s\n",
                  (if_getcapenable(ifp) & IFCAP_RXCSUM) ? "ON" : "OFF");
            if (if_getcapenable(ifp) & IFCAP_RXCSUM) {
                if_sethwassistbits(ifp, (CSUM_IP      |
                                    CSUM_TCP      |
                                    CSUM_UDP      |
                                    CSUM_TSO      |
                                    CSUM_TCP_IPV6 |
                                    CSUM_UDP_IPV6), 0);
            } else {
		if_clearhwassist(ifp); /* XXX */
            }
        }

        /* toggle TSO4 capabilities enabled flag */
        if (mask & IFCAP_TSO4) {
            if_togglecapenable(ifp, IFCAP_TSO4);
            BLOGD(sc, DBG_IOCTL, "Turning TSO4 %s\n",
                  (if_getcapenable(ifp) & IFCAP_TSO4) ? "ON" : "OFF");
        }

        /* toggle TSO6 capabilities enabled flag */
        if (mask & IFCAP_TSO6) {
	    if_togglecapenable(ifp, IFCAP_TSO6);
            BLOGD(sc, DBG_IOCTL, "Turning TSO6 %s\n",
                  (if_getcapenable(ifp) & IFCAP_TSO6) ? "ON" : "OFF");
        }

        /* toggle VLAN_HWTSO capabilities enabled flag */
        if (mask & IFCAP_VLAN_HWTSO) {

	    if_togglecapenable(ifp, IFCAP_VLAN_HWTSO);
            BLOGD(sc, DBG_IOCTL, "Turning VLAN_HWTSO %s\n",
                  (if_getcapenable(ifp) & IFCAP_VLAN_HWTSO) ? "ON" : "OFF");
        }

        /* toggle VLAN_HWCSUM capabilities enabled flag */
        if (mask & IFCAP_VLAN_HWCSUM) {
            /* XXX investigate this... */
            BLOGE(sc, "Changing VLAN_HWCSUM is not supported!\n");
            error = EINVAL;
        }

        /* toggle VLAN_MTU capabilities enable flag */
        if (mask & IFCAP_VLAN_MTU) {
            /* XXX investigate this... */
            BLOGE(sc, "Changing VLAN_MTU is not supported!\n");
            error = EINVAL;
        }

        /* toggle VLAN_HWTAGGING capabilities enabled flag */
        if (mask & IFCAP_VLAN_HWTAGGING) {
            /* XXX investigate this... */
            BLOGE(sc, "Changing VLAN_HWTAGGING is not supported!\n");
            error = EINVAL;
        }

        /* toggle VLAN_HWFILTER capabilities enabled flag */
        if (mask & IFCAP_VLAN_HWFILTER) {
            /* XXX investigate this... */
            BLOGE(sc, "Changing VLAN_HWFILTER is not supported!\n");
            error = EINVAL;
        }

        /* XXX not yet...
         * IFCAP_WOL_MAGIC
         */

        break;

    case SIOCSIFMEDIA:
    case SIOCGIFMEDIA:
        /* set/get interface media */
        BLOGD(sc, DBG_IOCTL,
              "Received SIOCSIFMEDIA/SIOCGIFMEDIA ioctl (cmd=%lu)\n",
              (command & 0xff));
        error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, command);
        break;

    default:
        BLOGD(sc, DBG_IOCTL, "Received Unknown Ioctl (cmd=%lu)\n",
              (command & 0xff));
        error = ether_ioctl(ifp, command, data);
        break;
    }

    if (reinit && (if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING)) {
        BLOGD(sc, DBG_LOAD | DBG_IOCTL,
              "Re-initializing hardware from IOCTL change\n");
	bxe_periodic_stop(sc);
	BXE_CORE_LOCK(sc);
	bxe_stop_locked(sc);
	bxe_init_locked(sc);
	BXE_CORE_UNLOCK(sc);
    }

    return (error);
}

static __noinline void
bxe_dump_mbuf(struct bxe_softc *sc,
              struct mbuf      *m,
              uint8_t          contents)
{
    char * type;
    int i = 0;

    if (!(sc->debug & DBG_MBUF)) {
        return;
    }

    if (m == NULL) {
        BLOGD(sc, DBG_MBUF, "mbuf: null pointer\n");
        return;
    }

    while (m) {

#if __FreeBSD_version >= 1000000
        BLOGD(sc, DBG_MBUF,
              "%02d: mbuf=%p m_len=%d m_flags=0x%b m_data=%p\n",
              i, m, m->m_len, m->m_flags, M_FLAG_BITS, m->m_data);

        if (m->m_flags & M_PKTHDR) {
             BLOGD(sc, DBG_MBUF,
                   "%02d: - m_pkthdr: tot_len=%d flags=0x%b csum_flags=%b\n",
                   i, m->m_pkthdr.len, m->m_flags, M_FLAG_BITS,
                   (int)m->m_pkthdr.csum_flags, CSUM_BITS);
        }
#else
        BLOGD(sc, DBG_MBUF,
              "%02d: mbuf=%p m_len=%d m_flags=0x%b m_data=%p\n",
              i, m, m->m_len, m->m_flags,
              "\20\1M_EXT\2M_PKTHDR\3M_EOR\4M_RDONLY", m->m_data);

        if (m->m_flags & M_PKTHDR) {
             BLOGD(sc, DBG_MBUF,
                   "%02d: - m_pkthdr: tot_len=%d flags=0x%b csum_flags=%b\n",
                   i, m->m_pkthdr.len, m->m_flags,
                   "\20\12M_BCAST\13M_MCAST\14M_FRAG"
                   "\15M_FIRSTFRAG\16M_LASTFRAG\21M_VLANTAG"
                   "\22M_PROMISC\23M_NOFREE",
                   (int)m->m_pkthdr.csum_flags,
                   "\20\1CSUM_IP\2CSUM_TCP\3CSUM_UDP\4CSUM_IP_FRAGS"
                   "\5CSUM_FRAGMENT\6CSUM_TSO\11CSUM_IP_CHECKED"
                   "\12CSUM_IP_VALID\13CSUM_DATA_VALID"
                   "\14CSUM_PSEUDO_HDR");
        }
#endif /* #if __FreeBSD_version >= 1000000 */

        if (m->m_flags & M_EXT) {
            switch (m->m_ext.ext_type) {
            case EXT_CLUSTER:    type = "EXT_CLUSTER";    break;
            case EXT_SFBUF:      type = "EXT_SFBUF";      break;
            case EXT_JUMBOP:     type = "EXT_JUMBOP";     break;
            case EXT_JUMBO9:     type = "EXT_JUMBO9";     break;
            case EXT_JUMBO16:    type = "EXT_JUMBO16";    break;
            case EXT_PACKET:     type = "EXT_PACKET";     break;
            case EXT_MBUF:       type = "EXT_MBUF";       break;
            case EXT_NET_DRV:    type = "EXT_NET_DRV";    break;
            case EXT_MOD_TYPE:   type = "EXT_MOD_TYPE";   break;
            case EXT_DISPOSABLE: type = "EXT_DISPOSABLE"; break;
            case EXT_EXTREF:     type = "EXT_EXTREF";     break;
            default:             type = "UNKNOWN";        break;
            }

            BLOGD(sc, DBG_MBUF,
                  "%02d: - m_ext: %p ext_size=%d type=%s\n",
                  i, m->m_ext.ext_buf, m->m_ext.ext_size, type);
        }

        if (contents) {
            bxe_dump_mbuf_data(sc, "mbuf data", m, TRUE);
        }

        m = m->m_next;
        i++;
    }
}

/*
 * Checks to ensure the 13 bd sliding window is >= MSS for TSO.
 * Check that (13 total bds - 3 bds) = 10 bd window >= MSS.
 * The window: 3 bds are = 1 for headers BD + 2 for parse BD and last BD
 * The headers comes in a separate bd in FreeBSD so 13-3=10.
 * Returns: 0 if OK to send, 1 if packet needs further defragmentation
 */
static int
bxe_chktso_window(struct bxe_softc  *sc,
                  int               nsegs,
                  bus_dma_segment_t *segs,
                  struct mbuf       *m)
{
    uint32_t num_wnds, wnd_size, wnd_sum;
    int32_t frag_idx, wnd_idx;
    unsigned short lso_mss;
    int defrag;

    defrag = 0;
    wnd_sum = 0;
    wnd_size = 10;
    num_wnds = nsegs - wnd_size;
    lso_mss = htole16(m->m_pkthdr.tso_segsz);

    /*
     * Total header lengths Eth+IP+TCP in first FreeBSD mbuf so calculate the
     * first window sum of data while skipping the first assuming it is the
     * header in FreeBSD.
     */
    for (frag_idx = 1; (frag_idx <= wnd_size); frag_idx++) {
        wnd_sum += htole16(segs[frag_idx].ds_len);
    }

    /* check the first 10 bd window size */
    if (wnd_sum < lso_mss) {
        return (1);
    }

    /* run through the windows */
    for (wnd_idx = 0; wnd_idx < num_wnds; wnd_idx++, frag_idx++) {
        /* subtract the first mbuf->m_len of the last wndw(-header) */
        wnd_sum -= htole16(segs[wnd_idx+1].ds_len);
        /* add the next mbuf len to the len of our new window */
        wnd_sum += htole16(segs[frag_idx].ds_len);
        if (wnd_sum < lso_mss) {
            return (1);
        }
    }

    return (0);
}

static uint8_t
bxe_set_pbd_csum_e2(struct bxe_fastpath *fp,
                    struct mbuf         *m,
                    uint32_t            *parsing_data)
{
    struct ether_vlan_header *eh = NULL;
    struct ip *ip4 = NULL;
    struct ip6_hdr *ip6 = NULL;
    caddr_t ip = NULL;
    struct tcphdr *th = NULL;
    int e_hlen, ip_hlen, l4_off;
    uint16_t proto;

    if (m->m_pkthdr.csum_flags == CSUM_IP) {
        /* no L4 checksum offload needed */
        return (0);
    }

    /* get the Ethernet header */
    eh = mtod(m, struct ether_vlan_header *);

    /* handle VLAN encapsulation if present */
    if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
        e_hlen = (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN);
        proto  = ntohs(eh->evl_proto);
    } else {
        e_hlen = ETHER_HDR_LEN;
        proto  = ntohs(eh->evl_encap_proto);
    }

    switch (proto) {
    case ETHERTYPE_IP:
        /* get the IP header, if mbuf len < 20 then header in next mbuf */
        ip4 = (m->m_len < sizeof(struct ip)) ?
                  (struct ip *)m->m_next->m_data :
                  (struct ip *)(m->m_data + e_hlen);
        /* ip_hl is number of 32-bit words */
        ip_hlen = (ip4->ip_hl << 2);
        ip = (caddr_t)ip4;
        break;
    case ETHERTYPE_IPV6:
        /* get the IPv6 header, if mbuf len < 40 then header in next mbuf */
        ip6 = (m->m_len < sizeof(struct ip6_hdr)) ?
                  (struct ip6_hdr *)m->m_next->m_data :
                  (struct ip6_hdr *)(m->m_data + e_hlen);
        /* XXX cannot support offload with IPv6 extensions */
        ip_hlen = sizeof(struct ip6_hdr);
        ip = (caddr_t)ip6;
        break;
    default:
        /* We can't offload in this case... */
        /* XXX error stat ??? */
        return (0);
    }

    /* XXX assuming L4 header is contiguous to IPv4/IPv6 in the same mbuf */
    l4_off = (e_hlen + ip_hlen);

    *parsing_data |=
        (((l4_off >> 1) << ETH_TX_PARSE_BD_E2_L4_HDR_START_OFFSET_W_SHIFT) &
         ETH_TX_PARSE_BD_E2_L4_HDR_START_OFFSET_W);

    if (m->m_pkthdr.csum_flags & (CSUM_TCP |
                                  CSUM_TSO |
                                  CSUM_TCP_IPV6)) {
        fp->eth_q_stats.tx_ofld_frames_csum_tcp++;
        th = (struct tcphdr *)(ip + ip_hlen);
        /* th_off is number of 32-bit words */
        *parsing_data |= ((th->th_off <<
                           ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW_SHIFT) &
                          ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW);
        return (l4_off + (th->th_off << 2)); /* entire header length */
    } else if (m->m_pkthdr.csum_flags & (CSUM_UDP |
                                         CSUM_UDP_IPV6)) {
        fp->eth_q_stats.tx_ofld_frames_csum_udp++;
        return (l4_off + sizeof(struct udphdr)); /* entire header length */
    } else {
        /* XXX error stat ??? */
        return (0);
    }
}

static uint8_t
bxe_set_pbd_csum(struct bxe_fastpath        *fp,
                 struct mbuf                *m,
                 struct eth_tx_parse_bd_e1x *pbd)
{
    struct ether_vlan_header *eh = NULL;
    struct ip *ip4 = NULL;
    struct ip6_hdr *ip6 = NULL;
    caddr_t ip = NULL;
    struct tcphdr *th = NULL;
    struct udphdr *uh = NULL;
    int e_hlen, ip_hlen;
    uint16_t proto;
    uint8_t hlen;
    uint16_t tmp_csum;
    uint32_t *tmp_uh;

    /* get the Ethernet header */
    eh = mtod(m, struct ether_vlan_header *);

    /* handle VLAN encapsulation if present */
    if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
        e_hlen = (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN);
        proto  = ntohs(eh->evl_proto);
    } else {
        e_hlen = ETHER_HDR_LEN;
        proto  = ntohs(eh->evl_encap_proto);
    }

    switch (proto) {
    case ETHERTYPE_IP:
        /* get the IP header, if mbuf len < 20 then header in next mbuf */
        ip4 = (m->m_len < sizeof(struct ip)) ?
                  (struct ip *)m->m_next->m_data :
                  (struct ip *)(m->m_data + e_hlen);
        /* ip_hl is number of 32-bit words */
        ip_hlen = (ip4->ip_hl << 1);
        ip = (caddr_t)ip4;
        break;
    case ETHERTYPE_IPV6:
        /* get the IPv6 header, if mbuf len < 40 then header in next mbuf */
        ip6 = (m->m_len < sizeof(struct ip6_hdr)) ?
                  (struct ip6_hdr *)m->m_next->m_data :
                  (struct ip6_hdr *)(m->m_data + e_hlen);
        /* XXX cannot support offload with IPv6 extensions */
        ip_hlen = (sizeof(struct ip6_hdr) >> 1);
        ip = (caddr_t)ip6;
        break;
    default:
        /* We can't offload in this case... */
        /* XXX error stat ??? */
        return (0);
    }

    hlen = (e_hlen >> 1);

    /* note that rest of global_data is indirectly zeroed here */
    if (m->m_flags & M_VLANTAG) {
        pbd->global_data =
            htole16(hlen | (1 << ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN_SHIFT));
    } else {
        pbd->global_data = htole16(hlen);
    }

    pbd->ip_hlen_w = ip_hlen;

    hlen += pbd->ip_hlen_w;

    /* XXX assuming L4 header is contiguous to IPv4/IPv6 in the same mbuf */

    if (m->m_pkthdr.csum_flags & (CSUM_TCP |
                                  CSUM_TSO |
                                  CSUM_TCP_IPV6)) {
        th = (struct tcphdr *)(ip + (ip_hlen << 1));
        /* th_off is number of 32-bit words */
        hlen += (uint16_t)(th->th_off << 1);
    } else if (m->m_pkthdr.csum_flags & (CSUM_UDP |
                                         CSUM_UDP_IPV6)) {
        uh = (struct udphdr *)(ip + (ip_hlen << 1));
        hlen += (sizeof(struct udphdr) / 2);
    } else {
        /* valid case as only CSUM_IP was set */
        return (0);
    }

    pbd->total_hlen_w = htole16(hlen);

    if (m->m_pkthdr.csum_flags & (CSUM_TCP |
                                  CSUM_TSO |
                                  CSUM_TCP_IPV6)) {
        fp->eth_q_stats.tx_ofld_frames_csum_tcp++;
        pbd->tcp_pseudo_csum = ntohs(th->th_sum);
    } else if (m->m_pkthdr.csum_flags & (CSUM_UDP |
                                         CSUM_UDP_IPV6)) {
        fp->eth_q_stats.tx_ofld_frames_csum_udp++;

        /*
         * Everest1 (i.e. 57710, 57711, 57711E) does not natively support UDP
         * checksums and does not know anything about the UDP header and where
         * the checksum field is located. It only knows about TCP. Therefore
         * we "lie" to the hardware for outgoing UDP packets w/ checksum
         * offload. Since the checksum field offset for TCP is 16 bytes and
         * for UDP it is 6 bytes we pass a pointer to the hardware that is 10
         * bytes less than the start of the UDP header. This allows the
         * hardware to write the checksum in the correct spot. But the
         * hardware will compute a checksum which includes the last 10 bytes
         * of the IP header. To correct this we tweak the stack computed
         * pseudo checksum by folding in the calculation of the inverse
         * checksum for those final 10 bytes of the IP header. This allows
         * the correct checksum to be computed by the hardware.
         */

        /* set pointer 10 bytes before UDP header */
        tmp_uh = (uint32_t *)((uint8_t *)uh - 10);

        /* calculate a pseudo header checksum over the first 10 bytes */
        tmp_csum = in_pseudo(*tmp_uh,
                             *(tmp_uh + 1),
                             *(uint16_t *)(tmp_uh + 2));

        pbd->tcp_pseudo_csum = ntohs(in_addword(uh->uh_sum, ~tmp_csum));
    }

    return (hlen * 2); /* entire header length, number of bytes */
}

static void
bxe_set_pbd_lso_e2(struct mbuf *m,
                   uint32_t    *parsing_data)
{
    *parsing_data |= ((m->m_pkthdr.tso_segsz <<
                       ETH_TX_PARSE_BD_E2_LSO_MSS_SHIFT) &
                      ETH_TX_PARSE_BD_E2_LSO_MSS);

    /* XXX test for IPv6 with extension header... */
}

static void
bxe_set_pbd_lso(struct mbuf                *m,
                struct eth_tx_parse_bd_e1x *pbd)
{
    struct ether_vlan_header *eh = NULL;
    struct ip *ip = NULL;
    struct tcphdr *th = NULL;
    int e_hlen;

    /* get the Ethernet header */
    eh = mtod(m, struct ether_vlan_header *);

    /* handle VLAN encapsulation if present */
    e_hlen = (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) ?
                 (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN) : ETHER_HDR_LEN;

    /* get the IP and TCP header, with LSO entire header in first mbuf */
    /* XXX assuming IPv4 */
    ip = (struct ip *)(m->m_data + e_hlen);
    th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));

    pbd->lso_mss = htole16(m->m_pkthdr.tso_segsz);
    pbd->tcp_send_seq = ntohl(th->th_seq);
    pbd->tcp_flags = ((ntohl(((uint32_t *)th)[3]) >> 16) & 0xff);

#if 1
        /* XXX IPv4 */
        pbd->ip_id = ntohs(ip->ip_id);
        pbd->tcp_pseudo_csum =
            ntohs(in_pseudo(ip->ip_src.s_addr,
                            ip->ip_dst.s_addr,
                            htons(IPPROTO_TCP)));
#else
        /* XXX IPv6 */
        pbd->tcp_pseudo_csum =
            ntohs(in_pseudo(&ip6->ip6_src,
                            &ip6->ip6_dst,
                            htons(IPPROTO_TCP)));
#endif

    pbd->global_data |=
        htole16(ETH_TX_PARSE_BD_E1X_PSEUDO_CS_WITHOUT_LEN);
}

/*
 * Encapsulte an mbuf cluster into the tx bd chain and makes the memory
 * visible to the controller.
 *
 * If an mbuf is submitted to this routine and cannot be given to the
 * controller (e.g. it has too many fragments) then the function may free
 * the mbuf and return to the caller.
 *
 * Returns:
 *   0 = Success, !0 = Failure
 *   Note the side effect that an mbuf may be freed if it causes a problem.
 */
static int
bxe_tx_encap(struct bxe_fastpath *fp, struct mbuf **m_head)
{
    bus_dma_segment_t segs[32];
    struct mbuf *m0;
    struct bxe_sw_tx_bd *tx_buf;
    struct eth_tx_parse_bd_e1x *pbd_e1x = NULL;
    struct eth_tx_parse_bd_e2 *pbd_e2 = NULL;
    /* struct eth_tx_parse_2nd_bd *pbd2 = NULL; */
    struct eth_tx_bd *tx_data_bd;
    struct eth_tx_bd *tx_total_pkt_size_bd;
    struct eth_tx_start_bd *tx_start_bd;
    uint16_t bd_prod, pkt_prod, total_pkt_size;
    uint8_t mac_type;
    int defragged, error, nsegs, rc, nbds, vlan_off, ovlan;
    struct bxe_softc *sc;
    uint16_t tx_bd_avail;
    struct ether_vlan_header *eh;
    uint32_t pbd_e2_parsing_data = 0;
    uint8_t hlen = 0;
    int tmp_bd;
    int i;

    sc = fp->sc;

#if __FreeBSD_version >= 800000
    M_ASSERTPKTHDR(*m_head);
#endif /* #if __FreeBSD_version >= 800000 */

    m0 = *m_head;
    rc = defragged = nbds = ovlan = vlan_off = total_pkt_size = 0;
    tx_start_bd = NULL;
    tx_data_bd = NULL;
    tx_total_pkt_size_bd = NULL;

    /* get the H/W pointer for packets and BDs */
    pkt_prod = fp->tx_pkt_prod;
    bd_prod = fp->tx_bd_prod;

    mac_type = UNICAST_ADDRESS;

    /* map the mbuf into the next open DMAable memory */
    tx_buf = &fp->tx_mbuf_chain[TX_BD(pkt_prod)];
    error = bus_dmamap_load_mbuf_sg(fp->tx_mbuf_tag,
                                    tx_buf->m_map, m0,
                                    segs, &nsegs, BUS_DMA_NOWAIT);

    /* mapping errors */
    if(__predict_false(error != 0)) {
        fp->eth_q_stats.tx_dma_mapping_failure++;
        if (error == ENOMEM) {
            /* resource issue, try again later */
            rc = ENOMEM;
        } else if (error == EFBIG) {
            /* possibly recoverable with defragmentation */
            fp->eth_q_stats.mbuf_defrag_attempts++;
            m0 = m_defrag(*m_head, M_NOWAIT);
            if (m0 == NULL) {
                fp->eth_q_stats.mbuf_defrag_failures++;
                rc = ENOBUFS;
            } else {
                /* defrag successful, try mapping again */
                *m_head = m0;
                error = bus_dmamap_load_mbuf_sg(fp->tx_mbuf_tag,
                                                tx_buf->m_map, m0,
                                                segs, &nsegs, BUS_DMA_NOWAIT);
                if (error) {
                    fp->eth_q_stats.tx_dma_mapping_failure++;
                    rc = error;
                }
            }
        } else {
            /* unknown, unrecoverable mapping error */
            BLOGE(sc, "Unknown TX mapping error rc=%d\n", error);
            bxe_dump_mbuf(sc, m0, FALSE);
            rc = error;
        }

        goto bxe_tx_encap_continue;
    }

    tx_bd_avail = bxe_tx_avail(sc, fp);

    /* make sure there is enough room in the send queue */
    if (__predict_false(tx_bd_avail < (nsegs + 2))) {
        /* Recoverable, try again later. */
        fp->eth_q_stats.tx_hw_queue_full++;
        bus_dmamap_unload(fp->tx_mbuf_tag, tx_buf->m_map);
        rc = ENOMEM;
        goto bxe_tx_encap_continue;
    }

    /* capture the current H/W TX chain high watermark */
    if (__predict_false(fp->eth_q_stats.tx_hw_max_queue_depth <
                        (TX_BD_USABLE - tx_bd_avail))) {
        fp->eth_q_stats.tx_hw_max_queue_depth = (TX_BD_USABLE - tx_bd_avail);
    }

    /* make sure it fits in the packet window */
    if (__predict_false(nsegs > BXE_MAX_SEGMENTS)) {
        /*
         * The mbuf may be to big for the controller to handle. If the frame
         * is a TSO frame we'll need to do an additional check.
         */
        if (m0->m_pkthdr.csum_flags & CSUM_TSO) {
            if (bxe_chktso_window(sc, nsegs, segs, m0) == 0) {
                goto bxe_tx_encap_continue; /* OK to send */
            } else {
                fp->eth_q_stats.tx_window_violation_tso++;
            }
        } else {
            fp->eth_q_stats.tx_window_violation_std++;
        }

        /* lets try to defragment this mbuf and remap it */
        fp->eth_q_stats.mbuf_defrag_attempts++;
        bus_dmamap_unload(fp->tx_mbuf_tag, tx_buf->m_map);

        m0 = m_defrag(*m_head, M_NOWAIT);
        if (m0 == NULL) {
            fp->eth_q_stats.mbuf_defrag_failures++;
            /* Ugh, just drop the frame... :( */
            rc = ENOBUFS;
        } else {
            /* defrag successful, try mapping again */
            *m_head = m0;
            error = bus_dmamap_load_mbuf_sg(fp->tx_mbuf_tag,
                                            tx_buf->m_map, m0,
                                            segs, &nsegs, BUS_DMA_NOWAIT);
            if (error) {
                fp->eth_q_stats.tx_dma_mapping_failure++;
                /* No sense in trying to defrag/copy chain, drop it. :( */
                rc = error;
            } else {
               /* if the chain is still too long then drop it */
                if(m0->m_pkthdr.csum_flags & CSUM_TSO) {
                    /*
                     * in case TSO is enabled nsegs should be checked against
                     * BXE_TSO_MAX_SEGMENTS
                     */
                    if (__predict_false(nsegs > BXE_TSO_MAX_SEGMENTS)) {
                        bus_dmamap_unload(fp->tx_mbuf_tag, tx_buf->m_map);
                        fp->eth_q_stats.nsegs_path1_errors++;
                        rc = ENODEV;
                    }
                } else {
                    if (__predict_false(nsegs > BXE_MAX_SEGMENTS)) {
                        bus_dmamap_unload(fp->tx_mbuf_tag, tx_buf->m_map);
                        fp->eth_q_stats.nsegs_path2_errors++;
                        rc = ENODEV;
                    }
                }
            }
        }
    }

bxe_tx_encap_continue:

    /* Check for errors */
    if (rc) {
        if (rc == ENOMEM) {
            /* recoverable try again later  */
        } else {
            fp->eth_q_stats.tx_soft_errors++;
            fp->eth_q_stats.mbuf_alloc_tx--;
            m_freem(*m_head);
            *m_head = NULL;
        }

        return (rc);
    }

    /* set flag according to packet type (UNICAST_ADDRESS is default) */
    if (m0->m_flags & M_BCAST) {
        mac_type = BROADCAST_ADDRESS;
    } else if (m0->m_flags & M_MCAST) {
        mac_type = MULTICAST_ADDRESS;
    }

    /* store the mbuf into the mbuf ring */
    tx_buf->m        = m0;
    tx_buf->first_bd = fp->tx_bd_prod;
    tx_buf->flags    = 0;

    /* prepare the first transmit (start) BD for the mbuf */
    tx_start_bd = &fp->tx_chain[TX_BD(bd_prod)].start_bd;

    BLOGD(sc, DBG_TX,
          "sending pkt_prod=%u tx_buf=%p next_idx=%u bd=%u tx_start_bd=%p\n",
          pkt_prod, tx_buf, fp->tx_pkt_prod, bd_prod, tx_start_bd);

    tx_start_bd->addr_lo = htole32(U64_LO(segs[0].ds_addr));
    tx_start_bd->addr_hi = htole32(U64_HI(segs[0].ds_addr));
    tx_start_bd->nbytes  = htole16(segs[0].ds_len);
    total_pkt_size += tx_start_bd->nbytes;
    tx_start_bd->bd_flags.as_bitfield = ETH_TX_BD_FLAGS_START_BD;

    tx_start_bd->general_data = (1 << ETH_TX_START_BD_HDR_NBDS_SHIFT);

    /* all frames have at least Start BD + Parsing BD */
    nbds = nsegs + 1;
    tx_start_bd->nbd = htole16(nbds);

    if (m0->m_flags & M_VLANTAG) {
        tx_start_bd->vlan_or_ethertype = htole16(m0->m_pkthdr.ether_vtag);
        tx_start_bd->bd_flags.as_bitfield |=
            (X_ETH_OUTBAND_VLAN << ETH_TX_BD_FLAGS_VLAN_MODE_SHIFT);
    } else {
        /* vf tx, start bd must hold the ethertype for fw to enforce it */
        if (IS_VF(sc)) {
            /* map ethernet header to find type and header length */
            eh = mtod(m0, struct ether_vlan_header *);
            tx_start_bd->vlan_or_ethertype = eh->evl_encap_proto;
        } else {
            /* used by FW for packet accounting */
            tx_start_bd->vlan_or_ethertype = htole16(fp->tx_pkt_prod);
        }
    }

    /*
     * add a parsing BD from the chain. The parsing BD is always added
     * though it is only used for TSO and chksum
     */
    bd_prod = TX_BD_NEXT(bd_prod);

    if (m0->m_pkthdr.csum_flags) {
        if (m0->m_pkthdr.csum_flags & CSUM_IP) {
            fp->eth_q_stats.tx_ofld_frames_csum_ip++;
            tx_start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_IP_CSUM;
        }

        if (m0->m_pkthdr.csum_flags & CSUM_TCP_IPV6) {
            tx_start_bd->bd_flags.as_bitfield |= (ETH_TX_BD_FLAGS_IPV6 |
                                                  ETH_TX_BD_FLAGS_L4_CSUM);
        } else if (m0->m_pkthdr.csum_flags & CSUM_UDP_IPV6) {
            tx_start_bd->bd_flags.as_bitfield |= (ETH_TX_BD_FLAGS_IPV6   |
                                                  ETH_TX_BD_FLAGS_IS_UDP |
                                                  ETH_TX_BD_FLAGS_L4_CSUM);
        } else if ((m0->m_pkthdr.csum_flags & CSUM_TCP) ||
                   (m0->m_pkthdr.csum_flags & CSUM_TSO)) {
            tx_start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_L4_CSUM;
        } else if (m0->m_pkthdr.csum_flags & CSUM_UDP) {
            tx_start_bd->bd_flags.as_bitfield |= (ETH_TX_BD_FLAGS_L4_CSUM |
                                                  ETH_TX_BD_FLAGS_IS_UDP);
        }
    }

    if (!CHIP_IS_E1x(sc)) {
        pbd_e2 = &fp->tx_chain[TX_BD(bd_prod)].parse_bd_e2;
        memset(pbd_e2, 0, sizeof(struct eth_tx_parse_bd_e2));

        if (m0->m_pkthdr.csum_flags) {
            hlen = bxe_set_pbd_csum_e2(fp, m0, &pbd_e2_parsing_data);
        }

        SET_FLAG(pbd_e2_parsing_data, ETH_TX_PARSE_BD_E2_ETH_ADDR_TYPE,
                 mac_type);
    } else {
        uint16_t global_data = 0;

        pbd_e1x = &fp->tx_chain[TX_BD(bd_prod)].parse_bd_e1x;
        memset(pbd_e1x, 0, sizeof(struct eth_tx_parse_bd_e1x));

        if (m0->m_pkthdr.csum_flags) {
            hlen = bxe_set_pbd_csum(fp, m0, pbd_e1x);
        }

        SET_FLAG(global_data,
                 ETH_TX_PARSE_BD_E1X_ETH_ADDR_TYPE, mac_type);
        pbd_e1x->global_data |= htole16(global_data);
    }

    /* setup the parsing BD with TSO specific info */
    if (m0->m_pkthdr.csum_flags & CSUM_TSO) {
        fp->eth_q_stats.tx_ofld_frames_lso++;
        tx_start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_SW_LSO;

        if (__predict_false(tx_start_bd->nbytes > hlen)) {
            fp->eth_q_stats.tx_ofld_frames_lso_hdr_splits++;

            /* split the first BD into header/data making the fw job easy */
            nbds++;
            tx_start_bd->nbd = htole16(nbds);
            tx_start_bd->nbytes = htole16(hlen);

            bd_prod = TX_BD_NEXT(bd_prod);

            /* new transmit BD after the tx_parse_bd */
            tx_data_bd = &fp->tx_chain[TX_BD(bd_prod)].reg_bd;
            tx_data_bd->addr_hi = htole32(U64_HI(segs[0].ds_addr + hlen));
            tx_data_bd->addr_lo = htole32(U64_LO(segs[0].ds_addr + hlen));
            tx_data_bd->nbytes  = htole16(segs[0].ds_len - hlen);
            if (tx_total_pkt_size_bd == NULL) {
                tx_total_pkt_size_bd = tx_data_bd;
            }

            BLOGD(sc, DBG_TX,
                  "TSO split header size is %d (%x:%x) nbds %d\n",
                  le16toh(tx_start_bd->nbytes),
                  le32toh(tx_start_bd->addr_hi),
                  le32toh(tx_start_bd->addr_lo),
                  nbds);
        }

        if (!CHIP_IS_E1x(sc)) {
            bxe_set_pbd_lso_e2(m0, &pbd_e2_parsing_data);
        } else {
            bxe_set_pbd_lso(m0, pbd_e1x);
        }
    }

    if (pbd_e2_parsing_data) {
        pbd_e2->parsing_data = htole32(pbd_e2_parsing_data);
    }

    /* prepare remaining BDs, start tx bd contains first seg/frag */
    for (i = 1; i < nsegs ; i++) {
        bd_prod = TX_BD_NEXT(bd_prod);
        tx_data_bd = &fp->tx_chain[TX_BD(bd_prod)].reg_bd;
        tx_data_bd->addr_lo = htole32(U64_LO(segs[i].ds_addr));
        tx_data_bd->addr_hi = htole32(U64_HI(segs[i].ds_addr));
        tx_data_bd->nbytes  = htole16(segs[i].ds_len);
        if (tx_total_pkt_size_bd == NULL) {
            tx_total_pkt_size_bd = tx_data_bd;
        }
        total_pkt_size += tx_data_bd->nbytes;
    }

    BLOGD(sc, DBG_TX, "last bd %p\n", tx_data_bd);

    if (tx_total_pkt_size_bd != NULL) {
        tx_total_pkt_size_bd->total_pkt_bytes = total_pkt_size;
    }

    if (__predict_false(sc->debug & DBG_TX)) {
        tmp_bd = tx_buf->first_bd;
        for (i = 0; i < nbds; i++)
        {
            if (i == 0) {
                BLOGD(sc, DBG_TX,
                      "TX Strt: %p bd=%d nbd=%d vlan=0x%x "
                      "bd_flags=0x%x hdr_nbds=%d\n",
                      tx_start_bd,
                      tmp_bd,
                      le16toh(tx_start_bd->nbd),
                      le16toh(tx_start_bd->vlan_or_ethertype),
                      tx_start_bd->bd_flags.as_bitfield,
                      (tx_start_bd->general_data & ETH_TX_START_BD_HDR_NBDS));
            } else if (i == 1) {
                if (pbd_e1x) {
                    BLOGD(sc, DBG_TX,
                          "-> Prse: %p bd=%d global=0x%x ip_hlen_w=%u "
                          "ip_id=%u lso_mss=%u tcp_flags=0x%x csum=0x%x "
                          "tcp_seq=%u total_hlen_w=%u\n",
                          pbd_e1x,
                          tmp_bd,
                          pbd_e1x->global_data,
                          pbd_e1x->ip_hlen_w,
                          pbd_e1x->ip_id,
                          pbd_e1x->lso_mss,
                          pbd_e1x->tcp_flags,
                          pbd_e1x->tcp_pseudo_csum,
                          pbd_e1x->tcp_send_seq,
                          le16toh(pbd_e1x->total_hlen_w));
                } else { /* if (pbd_e2) */
                    BLOGD(sc, DBG_TX,
                          "-> Parse: %p bd=%d dst=%02x:%02x:%02x "
                          "src=%02x:%02x:%02x parsing_data=0x%x\n",
                          pbd_e2,
                          tmp_bd,
                          pbd_e2->data.mac_addr.dst_hi,
                          pbd_e2->data.mac_addr.dst_mid,
                          pbd_e2->data.mac_addr.dst_lo,
                          pbd_e2->data.mac_addr.src_hi,
                          pbd_e2->data.mac_addr.src_mid,
                          pbd_e2->data.mac_addr.src_lo,
                          pbd_e2->parsing_data);
                }
            }

            if (i != 1) { /* skip parse db as it doesn't hold data */
                tx_data_bd = &fp->tx_chain[TX_BD(tmp_bd)].reg_bd;
                BLOGD(sc, DBG_TX,
                      "-> Frag: %p bd=%d nbytes=%d hi=0x%x lo: 0x%x\n",
                      tx_data_bd,
                      tmp_bd,
                      le16toh(tx_data_bd->nbytes),
                      le32toh(tx_data_bd->addr_hi),
                      le32toh(tx_data_bd->addr_lo));
            }

            tmp_bd = TX_BD_NEXT(tmp_bd);
        }
    }

    BLOGD(sc, DBG_TX, "doorbell: nbds=%d bd=%u\n", nbds, bd_prod);

    /* update TX BD producer index value for next TX */
    bd_prod = TX_BD_NEXT(bd_prod);

    /*
     * If the chain of tx_bd's describing this frame is adjacent to or spans
     * an eth_tx_next_bd element then we need to increment the nbds value.
     */
    if (TX_BD_IDX(bd_prod) < nbds) {
        nbds++;
    }

    /* don't allow reordering of writes for nbd and packets */
    mb();

    fp->tx_db.data.prod += nbds;

    /* producer points to the next free tx_bd at this point */
    fp->tx_pkt_prod++;
    fp->tx_bd_prod = bd_prod;

    DOORBELL(sc, fp->index, fp->tx_db.raw);

    fp->eth_q_stats.tx_pkts++;

    /* Prevent speculative reads from getting ahead of the status block. */
    bus_space_barrier(sc->bar[BAR0].tag, sc->bar[BAR0].handle,
                      0, 0, BUS_SPACE_BARRIER_READ);

    /* Prevent speculative reads from getting ahead of the doorbell. */
    bus_space_barrier(sc->bar[BAR2].tag, sc->bar[BAR2].handle,
                      0, 0, BUS_SPACE_BARRIER_READ);

    return (0);
}

static void
bxe_tx_start_locked(struct bxe_softc *sc,
                    if_t ifp,
                    struct bxe_fastpath *fp)
{
    struct mbuf *m = NULL;
    int tx_count = 0;
    uint16_t tx_bd_avail;

    BXE_FP_TX_LOCK_ASSERT(fp);

    /* keep adding entries while there are frames to send */
    while (!if_sendq_empty(ifp)) {

        /*
         * check for any frames to send
         * dequeue can still be NULL even if queue is not empty
         */
        m = if_dequeue(ifp);
        if (__predict_false(m == NULL)) {
            break;
        }

        /* the mbuf now belongs to us */
        fp->eth_q_stats.mbuf_alloc_tx++;

        /*
         * Put the frame into the transmit ring. If we don't have room,
         * place the mbuf back at the head of the TX queue, set the
         * OACTIVE flag, and wait for the NIC to drain the chain.
         */
        if (__predict_false(bxe_tx_encap(fp, &m))) {
            fp->eth_q_stats.tx_encap_failures++;
            if (m != NULL) {
                /* mark the TX queue as full and return the frame */
                if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
		if_sendq_prepend(ifp, m);
                fp->eth_q_stats.mbuf_alloc_tx--;
                fp->eth_q_stats.tx_queue_xoff++;
            }

            /* stop looking for more work */
            break;
        }

        /* the frame was enqueued successfully */
        tx_count++;

        /* send a copy of the frame to any BPF listeners. */
        if_etherbpfmtap(ifp, m);

        tx_bd_avail = bxe_tx_avail(sc, fp);

        /* handle any completions if we're running low */
        if (tx_bd_avail < BXE_TX_CLEANUP_THRESHOLD) {
            /* bxe_txeof will set IFF_DRV_OACTIVE appropriately */
            bxe_txeof(sc, fp);
            if (if_getdrvflags(ifp) & IFF_DRV_OACTIVE) {
                break;
            }
        }
    }

    /* all TX packets were dequeued and/or the tx ring is full */
    if (tx_count > 0) {
        /* reset the TX watchdog timeout timer */
        fp->watchdog_timer = BXE_TX_TIMEOUT;
    }
}

/* Legacy (non-RSS) dispatch routine */
static void
bxe_tx_start(if_t ifp)
{
    struct bxe_softc *sc;
    struct bxe_fastpath *fp;

    sc = if_getsoftc(ifp);

    if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING)) {
        BLOGW(sc, "Interface not running, ignoring transmit request\n");
        return;
    }

    if (!sc->link_vars.link_up) {
        BLOGW(sc, "Interface link is down, ignoring transmit request\n");
        return;
    }

    fp = &sc->fp[0];

    if (if_getdrvflags(ifp) & IFF_DRV_OACTIVE) {
        fp->eth_q_stats.tx_queue_full_return++;
        return;
    }

    BXE_FP_TX_LOCK(fp);
    bxe_tx_start_locked(sc, ifp, fp);
    BXE_FP_TX_UNLOCK(fp);
}

#if __FreeBSD_version >= 901504

static int
bxe_tx_mq_start_locked(struct bxe_softc    *sc,
                       if_t                ifp,
                       struct bxe_fastpath *fp,
                       struct mbuf         *m)
{
    struct buf_ring *tx_br = fp->tx_br;
    struct mbuf *next;
    int depth, rc, tx_count;
    uint16_t tx_bd_avail;

    rc = tx_count = 0;

    BXE_FP_TX_LOCK_ASSERT(fp);

    if (sc->state != BXE_STATE_OPEN)  {
        fp->eth_q_stats.bxe_tx_mq_sc_state_failures++;
        return ENETDOWN;
    }

    if (!tx_br) {
        BLOGE(sc, "Multiqueue TX and no buf_ring!\n");
        return (EINVAL);
    }

    if (m != NULL) {
        rc = drbr_enqueue(ifp, tx_br, m);
        if (rc != 0) {
            fp->eth_q_stats.tx_soft_errors++;
            goto bxe_tx_mq_start_locked_exit;
        }
    }

    if (!sc->link_vars.link_up || !(if_getdrvflags(ifp) & IFF_DRV_RUNNING)) {
        fp->eth_q_stats.tx_request_link_down_failures++;
        goto bxe_tx_mq_start_locked_exit;
    }

    /* fetch the depth of the driver queue */
    depth = drbr_inuse_drv(ifp, tx_br);
    if (depth > fp->eth_q_stats.tx_max_drbr_queue_depth) {
        fp->eth_q_stats.tx_max_drbr_queue_depth = depth;
    }

    /* keep adding entries while there are frames to send */
    while ((next = drbr_peek(ifp, tx_br)) != NULL) {
        /* handle any completions if we're running low */
        tx_bd_avail = bxe_tx_avail(sc, fp);
        if (tx_bd_avail < BXE_TX_CLEANUP_THRESHOLD) {
            /* bxe_txeof will set IFF_DRV_OACTIVE appropriately */
            bxe_txeof(sc, fp);
            tx_bd_avail = bxe_tx_avail(sc, fp);
            if (tx_bd_avail < (BXE_TSO_MAX_SEGMENTS + 1)) {
                fp->eth_q_stats.bd_avail_too_less_failures++;
                m_freem(next);
                drbr_advance(ifp, tx_br);
                rc = ENOBUFS;
                break;
            }
        }

        /* the mbuf now belongs to us */
        fp->eth_q_stats.mbuf_alloc_tx++;

        /*
         * Put the frame into the transmit ring. If we don't have room,
         * place the mbuf back at the head of the TX queue, set the
         * OACTIVE flag, and wait for the NIC to drain the chain.
         */
        rc = bxe_tx_encap(fp, &next);
        if (__predict_false(rc != 0)) {
            fp->eth_q_stats.tx_encap_failures++;
            if (next != NULL) {
                /* mark the TX queue as full and save the frame */
                if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
                drbr_putback(ifp, tx_br, next);
                fp->eth_q_stats.mbuf_alloc_tx--;
                fp->eth_q_stats.tx_frames_deferred++;
            } else
                drbr_advance(ifp, tx_br);

            /* stop looking for more work */
            break;
        }

        /* the transmit frame was enqueued successfully */
        tx_count++;

        /* send a copy of the frame to any BPF listeners */
	if_etherbpfmtap(ifp, next);

        drbr_advance(ifp, tx_br);
    }

    /* all TX packets were dequeued and/or the tx ring is full */
    if (tx_count > 0) {
        /* reset the TX watchdog timeout timer */
        fp->watchdog_timer = BXE_TX_TIMEOUT;
    }

bxe_tx_mq_start_locked_exit:
    /* If we didn't drain the drbr, enqueue a task in the future to do it. */
    if (!drbr_empty(ifp, tx_br)) {
        fp->eth_q_stats.tx_mq_not_empty++;
        taskqueue_enqueue_timeout(fp->tq, &fp->tx_timeout_task, 1);
    }

    return (rc);
}

static void
bxe_tx_mq_start_deferred(void *arg,
                         int pending)
{
    struct bxe_fastpath *fp = (struct bxe_fastpath *)arg;
    struct bxe_softc *sc = fp->sc;
    if_t ifp = sc->ifp;

    BXE_FP_TX_LOCK(fp);
    bxe_tx_mq_start_locked(sc, ifp, fp, NULL);
    BXE_FP_TX_UNLOCK(fp);
}

/* Multiqueue (TSS) dispatch routine. */
static int
bxe_tx_mq_start(struct ifnet *ifp,
                struct mbuf  *m)
{
    struct bxe_softc *sc = if_getsoftc(ifp);
    struct bxe_fastpath *fp;
    int fp_index, rc;

    fp_index = 0; /* default is the first queue */

    /* check if flowid is set */

    if (BXE_VALID_FLOWID(m))
        fp_index = (m->m_pkthdr.flowid % sc->num_queues);

    fp = &sc->fp[fp_index];

    if (sc->state != BXE_STATE_OPEN)  {
        fp->eth_q_stats.bxe_tx_mq_sc_state_failures++;
        return ENETDOWN;
    }

    if (BXE_FP_TX_TRYLOCK(fp)) {
        rc = bxe_tx_mq_start_locked(sc, ifp, fp, m);
        BXE_FP_TX_UNLOCK(fp);
    } else {
        rc = drbr_enqueue(ifp, fp->tx_br, m);
        taskqueue_enqueue(fp->tq, &fp->tx_task);
    }

    return (rc);
}

static void
bxe_mq_flush(struct ifnet *ifp)
{
    struct bxe_softc *sc = if_getsoftc(ifp);
    struct bxe_fastpath *fp;
    struct mbuf *m;
    int i;

    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];

        if (fp->state != BXE_FP_STATE_IRQ) {
            BLOGD(sc, DBG_LOAD, "Not clearing fp[%02d] buf_ring (state=%d)\n",
                  fp->index, fp->state);
            continue;
        }

        if (fp->tx_br != NULL) {
            BLOGD(sc, DBG_LOAD, "Clearing fp[%02d] buf_ring\n", fp->index);
            BXE_FP_TX_LOCK(fp);
            while ((m = buf_ring_dequeue_sc(fp->tx_br)) != NULL) {
                m_freem(m);
            }
            BXE_FP_TX_UNLOCK(fp);
        }
    }

    if_qflush(ifp);
}

#endif /* FreeBSD_version >= 901504 */

static uint16_t
bxe_cid_ilt_lines(struct bxe_softc *sc)
{
    if (IS_SRIOV(sc)) {
        return ((BXE_FIRST_VF_CID + BXE_VF_CIDS) / ILT_PAGE_CIDS);
    }
    return (L2_ILT_LINES(sc));
}

static void
bxe_ilt_set_info(struct bxe_softc *sc)
{
    struct ilt_client_info *ilt_client;
    struct ecore_ilt *ilt = sc->ilt;
    uint16_t line = 0;

    ilt->start_line = FUNC_ILT_BASE(SC_FUNC(sc));
    BLOGD(sc, DBG_LOAD, "ilt starts at line %d\n", ilt->start_line);

    /* CDU */
    ilt_client = &ilt->clients[ILT_CLIENT_CDU];
    ilt_client->client_num = ILT_CLIENT_CDU;
    ilt_client->page_size = CDU_ILT_PAGE_SZ;
    ilt_client->flags = ILT_CLIENT_SKIP_MEM;
    ilt_client->start = line;
    line += bxe_cid_ilt_lines(sc);

    if (CNIC_SUPPORT(sc)) {
        line += CNIC_ILT_LINES;
    }

    ilt_client->end = (line - 1);

    BLOGD(sc, DBG_LOAD,
          "ilt client[CDU]: start %d, end %d, "
          "psz 0x%x, flags 0x%x, hw psz %d\n",
          ilt_client->start, ilt_client->end,
          ilt_client->page_size,
          ilt_client->flags,
          ilog2(ilt_client->page_size >> 12));

    /* QM */
    if (QM_INIT(sc->qm_cid_count)) {
        ilt_client = &ilt->clients[ILT_CLIENT_QM];
        ilt_client->client_num = ILT_CLIENT_QM;
        ilt_client->page_size = QM_ILT_PAGE_SZ;
        ilt_client->flags = 0;
        ilt_client->start = line;

        /* 4 bytes for each cid */
        line += DIV_ROUND_UP(sc->qm_cid_count * QM_QUEUES_PER_FUNC * 4,
                             QM_ILT_PAGE_SZ);

        ilt_client->end = (line - 1);

        BLOGD(sc, DBG_LOAD,
              "ilt client[QM]: start %d, end %d, "
              "psz 0x%x, flags 0x%x, hw psz %d\n",
              ilt_client->start, ilt_client->end,
              ilt_client->page_size, ilt_client->flags,
              ilog2(ilt_client->page_size >> 12));
    }

    if (CNIC_SUPPORT(sc)) {
        /* SRC */
        ilt_client = &ilt->clients[ILT_CLIENT_SRC];
        ilt_client->client_num = ILT_CLIENT_SRC;
        ilt_client->page_size = SRC_ILT_PAGE_SZ;
        ilt_client->flags = 0;
        ilt_client->start = line;
        line += SRC_ILT_LINES;
        ilt_client->end = (line - 1);

        BLOGD(sc, DBG_LOAD,
              "ilt client[SRC]: start %d, end %d, "
              "psz 0x%x, flags 0x%x, hw psz %d\n",
              ilt_client->start, ilt_client->end,
              ilt_client->page_size, ilt_client->flags,
              ilog2(ilt_client->page_size >> 12));

        /* TM */
        ilt_client = &ilt->clients[ILT_CLIENT_TM];
        ilt_client->client_num = ILT_CLIENT_TM;
        ilt_client->page_size = TM_ILT_PAGE_SZ;
        ilt_client->flags = 0;
        ilt_client->start = line;
        line += TM_ILT_LINES;
        ilt_client->end = (line - 1);

        BLOGD(sc, DBG_LOAD,
              "ilt client[TM]: start %d, end %d, "
              "psz 0x%x, flags 0x%x, hw psz %d\n",
              ilt_client->start, ilt_client->end,
              ilt_client->page_size, ilt_client->flags,
              ilog2(ilt_client->page_size >> 12));
    }

    KASSERT((line <= ILT_MAX_LINES), ("Invalid number of ILT lines!"));
}

static void
bxe_set_fp_rx_buf_size(struct bxe_softc *sc)
{
    int i;
    uint32_t rx_buf_size;

    rx_buf_size = (IP_HEADER_ALIGNMENT_PADDING + ETH_OVERHEAD + sc->mtu);

    for (i = 0; i < sc->num_queues; i++) {
        if(rx_buf_size <= MCLBYTES){
            sc->fp[i].rx_buf_size = rx_buf_size;
            sc->fp[i].mbuf_alloc_size = MCLBYTES;
        }else if (rx_buf_size <= MJUMPAGESIZE){
            sc->fp[i].rx_buf_size = rx_buf_size;
            sc->fp[i].mbuf_alloc_size = MJUMPAGESIZE;
        }else if (rx_buf_size <= (MJUMPAGESIZE + MCLBYTES)){
            sc->fp[i].rx_buf_size = MCLBYTES;
            sc->fp[i].mbuf_alloc_size = MCLBYTES;
        }else if (rx_buf_size <= (2 * MJUMPAGESIZE)){
            sc->fp[i].rx_buf_size = MJUMPAGESIZE;
            sc->fp[i].mbuf_alloc_size = MJUMPAGESIZE;
        }else {
            sc->fp[i].rx_buf_size = MCLBYTES;
            sc->fp[i].mbuf_alloc_size = MCLBYTES;
        }
    }
}

static int
bxe_alloc_ilt_mem(struct bxe_softc *sc)
{
    int rc = 0;

    if ((sc->ilt =
         (struct ecore_ilt *)malloc(sizeof(struct ecore_ilt),
                                    M_BXE_ILT,
                                    (M_NOWAIT | M_ZERO))) == NULL) {
        rc = 1;
    }

    return (rc);
}

static int
bxe_alloc_ilt_lines_mem(struct bxe_softc *sc)
{
    int rc = 0;

    if ((sc->ilt->lines =
         (struct ilt_line *)malloc((sizeof(struct ilt_line) * ILT_MAX_LINES),
                                    M_BXE_ILT,
                                    (M_NOWAIT | M_ZERO))) == NULL) {
        rc = 1;
    }

    return (rc);
}

static void
bxe_free_ilt_mem(struct bxe_softc *sc)
{
    if (sc->ilt != NULL) {
        free(sc->ilt, M_BXE_ILT);
        sc->ilt = NULL;
    }
}

static void
bxe_free_ilt_lines_mem(struct bxe_softc *sc)
{
    if (sc->ilt->lines != NULL) {
        free(sc->ilt->lines, M_BXE_ILT);
        sc->ilt->lines = NULL;
    }
}

static void
bxe_free_mem(struct bxe_softc *sc)
{
    int i;

    for (i = 0; i < L2_ILT_LINES(sc); i++) {
        bxe_dma_free(sc, &sc->context[i].vcxt_dma);
        sc->context[i].vcxt = NULL;
        sc->context[i].size = 0;
    }

    ecore_ilt_mem_op(sc, ILT_MEMOP_FREE);

    bxe_free_ilt_lines_mem(sc);

}

static int
bxe_alloc_mem(struct bxe_softc *sc)
{

    int context_size;
    int allocated;
    int i;

    /*
     * Allocate memory for CDU context:
     * This memory is allocated separately and not in the generic ILT
     * functions because CDU differs in few aspects:
     * 1. There can be multiple entities allocating memory for context -
     * regular L2, CNIC, and SRIOV drivers. Each separately controls
     * its own ILT lines.
     * 2. Since CDU page-size is not a single 4KB page (which is the case
     * for the other ILT clients), to be efficient we want to support
     * allocation of sub-page-size in the last entry.
     * 3. Context pointers are used by the driver to pass to FW / update
     * the context (for the other ILT clients the pointers are used just to
     * free the memory during unload).
     */
    context_size = (sizeof(union cdu_context) * BXE_L2_CID_COUNT(sc));
    for (i = 0, allocated = 0; allocated < context_size; i++) {
        sc->context[i].size = min(CDU_ILT_PAGE_SZ,
                                  (context_size - allocated));

        if (bxe_dma_alloc(sc, sc->context[i].size,
                          &sc->context[i].vcxt_dma,
                          "cdu context") != 0) {
            bxe_free_mem(sc);
            return (-1);
        }

        sc->context[i].vcxt =
            (union cdu_context *)sc->context[i].vcxt_dma.vaddr;

        allocated += sc->context[i].size;
    }

    bxe_alloc_ilt_lines_mem(sc);

    BLOGD(sc, DBG_LOAD, "ilt=%p start_line=%u lines=%p\n",
          sc->ilt, sc->ilt->start_line, sc->ilt->lines);
    {
        for (i = 0; i < 4; i++) {
            BLOGD(sc, DBG_LOAD,
                  "c%d page_size=%u start=%u end=%u num=%u flags=0x%x\n",
                  i,
                  sc->ilt->clients[i].page_size,
                  sc->ilt->clients[i].start,
                  sc->ilt->clients[i].end,
                  sc->ilt->clients[i].client_num,
                  sc->ilt->clients[i].flags);
        }
    }
    if (ecore_ilt_mem_op(sc, ILT_MEMOP_ALLOC)) {
        BLOGE(sc, "ecore_ilt_mem_op ILT_MEMOP_ALLOC failed\n");
        bxe_free_mem(sc);
        return (-1);
    }

    return (0);
}

static void
bxe_free_rx_bd_chain(struct bxe_fastpath *fp)
{
    struct bxe_softc *sc;
    int i;

    sc = fp->sc;

    if (fp->rx_mbuf_tag == NULL) {
        return;
    }

    /* free all mbufs and unload all maps */
    for (i = 0; i < RX_BD_TOTAL; i++) {
        if (fp->rx_mbuf_chain[i].m_map != NULL) {
            bus_dmamap_sync(fp->rx_mbuf_tag,
                            fp->rx_mbuf_chain[i].m_map,
                            BUS_DMASYNC_POSTREAD);
            bus_dmamap_unload(fp->rx_mbuf_tag,
                              fp->rx_mbuf_chain[i].m_map);
        }

        if (fp->rx_mbuf_chain[i].m != NULL) {
            m_freem(fp->rx_mbuf_chain[i].m);
            fp->rx_mbuf_chain[i].m = NULL;
            fp->eth_q_stats.mbuf_alloc_rx--;
        }
    }
}

static void
bxe_free_tpa_pool(struct bxe_fastpath *fp)
{
    struct bxe_softc *sc;
    int i, max_agg_queues;

    sc = fp->sc;

    if (fp->rx_mbuf_tag == NULL) {
        return;
    }

    max_agg_queues = MAX_AGG_QS(sc);

    /* release all mbufs and unload all DMA maps in the TPA pool */
    for (i = 0; i < max_agg_queues; i++) {
        if (fp->rx_tpa_info[i].bd.m_map != NULL) {
            bus_dmamap_sync(fp->rx_mbuf_tag,
                            fp->rx_tpa_info[i].bd.m_map,
                            BUS_DMASYNC_POSTREAD);
            bus_dmamap_unload(fp->rx_mbuf_tag,
                              fp->rx_tpa_info[i].bd.m_map);
        }

        if (fp->rx_tpa_info[i].bd.m != NULL) {
            m_freem(fp->rx_tpa_info[i].bd.m);
            fp->rx_tpa_info[i].bd.m = NULL;
            fp->eth_q_stats.mbuf_alloc_tpa--;
        }
    }
}

static void
bxe_free_sge_chain(struct bxe_fastpath *fp)
{
    struct bxe_softc *sc;
    int i;

    sc = fp->sc;

    if (fp->rx_sge_mbuf_tag == NULL) {
        return;
    }

    /* rree all mbufs and unload all maps */
    for (i = 0; i < RX_SGE_TOTAL; i++) {
        if (fp->rx_sge_mbuf_chain[i].m_map != NULL) {
            bus_dmamap_sync(fp->rx_sge_mbuf_tag,
                            fp->rx_sge_mbuf_chain[i].m_map,
                            BUS_DMASYNC_POSTREAD);
            bus_dmamap_unload(fp->rx_sge_mbuf_tag,
                              fp->rx_sge_mbuf_chain[i].m_map);
        }

        if (fp->rx_sge_mbuf_chain[i].m != NULL) {
            m_freem(fp->rx_sge_mbuf_chain[i].m);
            fp->rx_sge_mbuf_chain[i].m = NULL;
            fp->eth_q_stats.mbuf_alloc_sge--;
        }
    }
}

static void
bxe_free_fp_buffers(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int i;

    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];

#if __FreeBSD_version >= 901504
        if (fp->tx_br != NULL) {
            /* just in case bxe_mq_flush() wasn't called */
            if (mtx_initialized(&fp->tx_mtx)) {
                struct mbuf *m;

                BXE_FP_TX_LOCK(fp);
                while ((m = buf_ring_dequeue_sc(fp->tx_br)) != NULL)
                    m_freem(m);
                BXE_FP_TX_UNLOCK(fp);
            }
        }
#endif

        /* free all RX buffers */
        bxe_free_rx_bd_chain(fp);
        bxe_free_tpa_pool(fp);
        bxe_free_sge_chain(fp);

        if (fp->eth_q_stats.mbuf_alloc_rx != 0) {
            BLOGE(sc, "failed to claim all rx mbufs (%d left)\n",
                  fp->eth_q_stats.mbuf_alloc_rx);
        }

        if (fp->eth_q_stats.mbuf_alloc_sge != 0) {
            BLOGE(sc, "failed to claim all sge mbufs (%d left)\n",
                  fp->eth_q_stats.mbuf_alloc_sge);
        }

        if (fp->eth_q_stats.mbuf_alloc_tpa != 0) {
            BLOGE(sc, "failed to claim all sge mbufs (%d left)\n",
                  fp->eth_q_stats.mbuf_alloc_tpa);
        }

        if (fp->eth_q_stats.mbuf_alloc_tx != 0) {
            BLOGE(sc, "failed to release tx mbufs (%d left)\n",
                  fp->eth_q_stats.mbuf_alloc_tx);
        }

        /* XXX verify all mbufs were reclaimed */
    }
}

static int
bxe_alloc_rx_bd_mbuf(struct bxe_fastpath *fp,
                     uint16_t            prev_index,
                     uint16_t            index)
{
    struct bxe_sw_rx_bd *rx_buf;
    struct eth_rx_bd *rx_bd;
    bus_dma_segment_t segs[1];
    bus_dmamap_t map;
    struct mbuf *m;
    int nsegs, rc;

    rc = 0;

    /* allocate the new RX BD mbuf */
    m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, fp->mbuf_alloc_size);
    if (__predict_false(m == NULL)) {
        fp->eth_q_stats.mbuf_rx_bd_alloc_failed++;
        return (ENOBUFS);
    }

    fp->eth_q_stats.mbuf_alloc_rx++;

    /* initialize the mbuf buffer length */
    m->m_pkthdr.len = m->m_len = fp->rx_buf_size;

    /* map the mbuf into non-paged pool */
    rc = bus_dmamap_load_mbuf_sg(fp->rx_mbuf_tag,
                                 fp->rx_mbuf_spare_map,
                                 m, segs, &nsegs, BUS_DMA_NOWAIT);
    if (__predict_false(rc != 0)) {
        fp->eth_q_stats.mbuf_rx_bd_mapping_failed++;
        m_freem(m);
        fp->eth_q_stats.mbuf_alloc_rx--;
        return (rc);
    }

    /* all mbufs must map to a single segment */
    KASSERT((nsegs == 1), ("Too many segments, %d returned!", nsegs));

    /* release any existing RX BD mbuf mappings */

    if (prev_index != index) {
        rx_buf = &fp->rx_mbuf_chain[prev_index];

        if (rx_buf->m_map != NULL) {
            bus_dmamap_sync(fp->rx_mbuf_tag, rx_buf->m_map,
                            BUS_DMASYNC_POSTREAD);
            bus_dmamap_unload(fp->rx_mbuf_tag, rx_buf->m_map);
        }

        /*
         * We only get here from bxe_rxeof() when the maximum number
         * of rx buffers is less than RX_BD_USABLE. bxe_rxeof() already
         * holds the mbuf in the prev_index so it's OK to NULL it out
         * here without concern of a memory leak.
         */
        fp->rx_mbuf_chain[prev_index].m = NULL;
    }

    rx_buf = &fp->rx_mbuf_chain[index];

    if (rx_buf->m_map != NULL) {
        bus_dmamap_sync(fp->rx_mbuf_tag, rx_buf->m_map,
                        BUS_DMASYNC_POSTREAD);
        bus_dmamap_unload(fp->rx_mbuf_tag, rx_buf->m_map);
    }

    /* save the mbuf and mapping info for a future packet */
    map = (prev_index != index) ?
              fp->rx_mbuf_chain[prev_index].m_map : rx_buf->m_map;
    rx_buf->m_map = fp->rx_mbuf_spare_map;
    fp->rx_mbuf_spare_map = map;
    bus_dmamap_sync(fp->rx_mbuf_tag, rx_buf->m_map,
                    BUS_DMASYNC_PREREAD);
    rx_buf->m = m;

    rx_bd = &fp->rx_chain[index];
    rx_bd->addr_hi = htole32(U64_HI(segs[0].ds_addr));
    rx_bd->addr_lo = htole32(U64_LO(segs[0].ds_addr));

    return (rc);
}

static int
bxe_alloc_rx_tpa_mbuf(struct bxe_fastpath *fp,
                      int                 queue)
{
    struct bxe_sw_tpa_info *tpa_info = &fp->rx_tpa_info[queue];
    bus_dma_segment_t segs[1];
    bus_dmamap_t map;
    struct mbuf *m;
    int nsegs;
    int rc = 0;

    /* allocate the new TPA mbuf */
    m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, fp->mbuf_alloc_size);
    if (__predict_false(m == NULL)) {
        fp->eth_q_stats.mbuf_rx_tpa_alloc_failed++;
        return (ENOBUFS);
    }

    fp->eth_q_stats.mbuf_alloc_tpa++;

    /* initialize the mbuf buffer length */
    m->m_pkthdr.len = m->m_len = fp->rx_buf_size;

    /* map the mbuf into non-paged pool */
    rc = bus_dmamap_load_mbuf_sg(fp->rx_mbuf_tag,
                                 fp->rx_tpa_info_mbuf_spare_map,
                                 m, segs, &nsegs, BUS_DMA_NOWAIT);
    if (__predict_false(rc != 0)) {
        fp->eth_q_stats.mbuf_rx_tpa_mapping_failed++;
        m_free(m);
        fp->eth_q_stats.mbuf_alloc_tpa--;
        return (rc);
    }

    /* all mbufs must map to a single segment */
    KASSERT((nsegs == 1), ("Too many segments, %d returned!", nsegs));

    /* release any existing TPA mbuf mapping */
    if (tpa_info->bd.m_map != NULL) {
        bus_dmamap_sync(fp->rx_mbuf_tag, tpa_info->bd.m_map,
                        BUS_DMASYNC_POSTREAD);
        bus_dmamap_unload(fp->rx_mbuf_tag, tpa_info->bd.m_map);
    }

    /* save the mbuf and mapping info for the TPA mbuf */
    map = tpa_info->bd.m_map;
    tpa_info->bd.m_map = fp->rx_tpa_info_mbuf_spare_map;
    fp->rx_tpa_info_mbuf_spare_map = map;
    bus_dmamap_sync(fp->rx_mbuf_tag, tpa_info->bd.m_map,
                    BUS_DMASYNC_PREREAD);
    tpa_info->bd.m = m;
    tpa_info->seg = segs[0];

    return (rc);
}

/*
 * Allocate an mbuf and assign it to the receive scatter gather chain. The
 * caller must take care to save a copy of the existing mbuf in the SG mbuf
 * chain.
 */
static int
bxe_alloc_rx_sge_mbuf(struct bxe_fastpath *fp,
                      uint16_t            index)
{
    struct bxe_sw_rx_bd *sge_buf;
    struct eth_rx_sge *sge;
    bus_dma_segment_t segs[1];
    bus_dmamap_t map;
    struct mbuf *m;
    int nsegs;
    int rc = 0;

    /* allocate a new SGE mbuf */
    m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, SGE_PAGE_SIZE);
    if (__predict_false(m == NULL)) {
        fp->eth_q_stats.mbuf_rx_sge_alloc_failed++;
        return (ENOMEM);
    }

    fp->eth_q_stats.mbuf_alloc_sge++;

    /* initialize the mbuf buffer length */
    m->m_pkthdr.len = m->m_len = SGE_PAGE_SIZE;

    /* map the SGE mbuf into non-paged pool */
    rc = bus_dmamap_load_mbuf_sg(fp->rx_sge_mbuf_tag,
                                 fp->rx_sge_mbuf_spare_map,
                                 m, segs, &nsegs, BUS_DMA_NOWAIT);
    if (__predict_false(rc != 0)) {
        fp->eth_q_stats.mbuf_rx_sge_mapping_failed++;
        m_freem(m);
        fp->eth_q_stats.mbuf_alloc_sge--;
        return (rc);
    }

    /* all mbufs must map to a single segment */
    KASSERT((nsegs == 1), ("Too many segments, %d returned!", nsegs));

    sge_buf = &fp->rx_sge_mbuf_chain[index];

    /* release any existing SGE mbuf mapping */
    if (sge_buf->m_map != NULL) {
        bus_dmamap_sync(fp->rx_sge_mbuf_tag, sge_buf->m_map,
                        BUS_DMASYNC_POSTREAD);
        bus_dmamap_unload(fp->rx_sge_mbuf_tag, sge_buf->m_map);
    }

    /* save the mbuf and mapping info for a future packet */
    map = sge_buf->m_map;
    sge_buf->m_map = fp->rx_sge_mbuf_spare_map;
    fp->rx_sge_mbuf_spare_map = map;
    bus_dmamap_sync(fp->rx_sge_mbuf_tag, sge_buf->m_map,
                    BUS_DMASYNC_PREREAD);
    sge_buf->m = m;

    sge = &fp->rx_sge_chain[index];
    sge->addr_hi = htole32(U64_HI(segs[0].ds_addr));
    sge->addr_lo = htole32(U64_LO(segs[0].ds_addr));

    return (rc);
}

static __noinline int
bxe_alloc_fp_buffers(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int i, j, rc = 0;
    int ring_prod, cqe_ring_prod;
    int max_agg_queues;

    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];

        ring_prod = cqe_ring_prod = 0;
        fp->rx_bd_cons = 0;
        fp->rx_cq_cons = 0;

        /* allocate buffers for the RX BDs in RX BD chain */
        for (j = 0; j < sc->max_rx_bufs; j++) {
            rc = bxe_alloc_rx_bd_mbuf(fp, ring_prod, ring_prod);
            if (rc != 0) {
                BLOGE(sc, "mbuf alloc fail for fp[%02d] rx chain (%d)\n",
                      i, rc);
                goto bxe_alloc_fp_buffers_error;
            }

            ring_prod     = RX_BD_NEXT(ring_prod);
            cqe_ring_prod = RCQ_NEXT(cqe_ring_prod);
        }

        fp->rx_bd_prod = ring_prod;
        fp->rx_cq_prod = cqe_ring_prod;
        fp->eth_q_stats.rx_calls = fp->eth_q_stats.rx_pkts = 0;

        max_agg_queues = MAX_AGG_QS(sc);

        fp->tpa_enable = TRUE;

        /* fill the TPA pool */
        for (j = 0; j < max_agg_queues; j++) {
            rc = bxe_alloc_rx_tpa_mbuf(fp, j);
            if (rc != 0) {
                BLOGE(sc, "mbuf alloc fail for fp[%02d] TPA queue %d\n",
                          i, j);
                fp->tpa_enable = FALSE;
                goto bxe_alloc_fp_buffers_error;
            }

            fp->rx_tpa_info[j].state = BXE_TPA_STATE_STOP;
        }

        if (fp->tpa_enable) {
            /* fill the RX SGE chain */
            ring_prod = 0;
            for (j = 0; j < RX_SGE_USABLE; j++) {
                rc = bxe_alloc_rx_sge_mbuf(fp, ring_prod);
                if (rc != 0) {
                    BLOGE(sc, "mbuf alloc fail for fp[%02d] SGE %d\n",
                              i, ring_prod);
                    fp->tpa_enable = FALSE;
                    ring_prod = 0;
                    goto bxe_alloc_fp_buffers_error;
                }

                ring_prod = RX_SGE_NEXT(ring_prod);
            }

            fp->rx_sge_prod = ring_prod;
        }
    }

    return (0);

bxe_alloc_fp_buffers_error:

    /* unwind what was already allocated */
    bxe_free_rx_bd_chain(fp);
    bxe_free_tpa_pool(fp);
    bxe_free_sge_chain(fp);

    return (ENOBUFS);
}

static void
bxe_free_fw_stats_mem(struct bxe_softc *sc)
{
    bxe_dma_free(sc, &sc->fw_stats_dma);

    sc->fw_stats_num = 0;

    sc->fw_stats_req_size = 0;
    sc->fw_stats_req = NULL;
    sc->fw_stats_req_mapping = 0;

    sc->fw_stats_data_size = 0;
    sc->fw_stats_data = NULL;
    sc->fw_stats_data_mapping = 0;
}

static int
bxe_alloc_fw_stats_mem(struct bxe_softc *sc)
{
    uint8_t num_queue_stats;
    int num_groups;

    /* number of queues for statistics is number of eth queues */
    num_queue_stats = BXE_NUM_ETH_QUEUES(sc);

    /*
     * Total number of FW statistics requests =
     *   1 for port stats + 1 for PF stats + num of queues
     */
    sc->fw_stats_num = (2 + num_queue_stats);

    /*
     * Request is built from stats_query_header and an array of
     * stats_query_cmd_group each of which contains STATS_QUERY_CMD_COUNT
     * rules. The real number or requests is configured in the
     * stats_query_header.
     */
    num_groups =
        ((sc->fw_stats_num / STATS_QUERY_CMD_COUNT) +
         ((sc->fw_stats_num % STATS_QUERY_CMD_COUNT) ? 1 : 0));

    BLOGD(sc, DBG_LOAD, "stats fw_stats_num %d num_groups %d\n",
          sc->fw_stats_num, num_groups);

    sc->fw_stats_req_size =
        (sizeof(struct stats_query_header) +
         (num_groups * sizeof(struct stats_query_cmd_group)));

    /*
     * Data for statistics requests + stats_counter.
     * stats_counter holds per-STORM counters that are incremented when
     * STORM has finished with the current request. Memory for FCoE
     * offloaded statistics are counted anyway, even if they will not be sent.
     * VF stats are not accounted for here as the data of VF stats is stored
     * in memory allocated by the VF, not here.
     */
    sc->fw_stats_data_size =
        (sizeof(struct stats_counter) +
         sizeof(struct per_port_stats) +
         sizeof(struct per_pf_stats) +
         /* sizeof(struct fcoe_statistics_params) + */
         (sizeof(struct per_queue_stats) * num_queue_stats));

    if (bxe_dma_alloc(sc, (sc->fw_stats_req_size + sc->fw_stats_data_size),
                      &sc->fw_stats_dma, "fw stats") != 0) {
        bxe_free_fw_stats_mem(sc);
        return (-1);
    }

    /* set up the shortcuts */

    sc->fw_stats_req =
        (struct bxe_fw_stats_req *)sc->fw_stats_dma.vaddr;
    sc->fw_stats_req_mapping = sc->fw_stats_dma.paddr;

    sc->fw_stats_data =
        (struct bxe_fw_stats_data *)((uint8_t *)sc->fw_stats_dma.vaddr +
                                     sc->fw_stats_req_size);
    sc->fw_stats_data_mapping = (sc->fw_stats_dma.paddr +
                                 sc->fw_stats_req_size);

    BLOGD(sc, DBG_LOAD, "statistics request base address set to %#jx\n",
          (uintmax_t)sc->fw_stats_req_mapping);

    BLOGD(sc, DBG_LOAD, "statistics data base address set to %#jx\n",
          (uintmax_t)sc->fw_stats_data_mapping);

    return (0);
}

/*
 * Bits map:
 * 0-7  - Engine0 load counter.
 * 8-15 - Engine1 load counter.
 * 16   - Engine0 RESET_IN_PROGRESS bit.
 * 17   - Engine1 RESET_IN_PROGRESS bit.
 * 18   - Engine0 ONE_IS_LOADED. Set when there is at least one active
 *        function on the engine
 * 19   - Engine1 ONE_IS_LOADED.
 * 20   - Chip reset flow bit. When set none-leader must wait for both engines
 *        leader to complete (check for both RESET_IN_PROGRESS bits and not
 *        for just the one belonging to its engine).
 */
#define BXE_RECOVERY_GLOB_REG     MISC_REG_GENERIC_POR_1
#define BXE_PATH0_LOAD_CNT_MASK   0x000000ff
#define BXE_PATH0_LOAD_CNT_SHIFT  0
#define BXE_PATH1_LOAD_CNT_MASK   0x0000ff00
#define BXE_PATH1_LOAD_CNT_SHIFT  8
#define BXE_PATH0_RST_IN_PROG_BIT 0x00010000
#define BXE_PATH1_RST_IN_PROG_BIT 0x00020000
#define BXE_GLOBAL_RESET_BIT      0x00040000

/* set the GLOBAL_RESET bit, should be run under rtnl lock */
static void
bxe_set_reset_global(struct bxe_softc *sc)
{
    uint32_t val;
    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
    val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    REG_WR(sc, BXE_RECOVERY_GLOB_REG, val | BXE_GLOBAL_RESET_BIT);
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/* clear the GLOBAL_RESET bit, should be run under rtnl lock */
static void
bxe_clear_reset_global(struct bxe_softc *sc)
{
    uint32_t val;
    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
    val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    REG_WR(sc, BXE_RECOVERY_GLOB_REG, val & (~BXE_GLOBAL_RESET_BIT));
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/* checks the GLOBAL_RESET bit, should be run under rtnl lock */
static uint8_t
bxe_reset_is_global(struct bxe_softc *sc)
{
    uint32_t val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    BLOGD(sc, DBG_LOAD, "GLOB_REG=0x%08x\n", val);
    return (val & BXE_GLOBAL_RESET_BIT) ? TRUE : FALSE;
}

/* clear RESET_IN_PROGRESS bit for the engine, should be run under rtnl lock */
static void
bxe_set_reset_done(struct bxe_softc *sc)
{
    uint32_t val;
    uint32_t bit = SC_PATH(sc) ? BXE_PATH1_RST_IN_PROG_BIT :
                                 BXE_PATH0_RST_IN_PROG_BIT;

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);

    val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    /* Clear the bit */
    val &= ~bit;
    REG_WR(sc, BXE_RECOVERY_GLOB_REG, val);

    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/* set RESET_IN_PROGRESS for the engine, should be run under rtnl lock */
static void
bxe_set_reset_in_progress(struct bxe_softc *sc)
{
    uint32_t val;
    uint32_t bit = SC_PATH(sc) ? BXE_PATH1_RST_IN_PROG_BIT :
                                 BXE_PATH0_RST_IN_PROG_BIT;

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);

    val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    /* Set the bit */
    val |= bit;
    REG_WR(sc, BXE_RECOVERY_GLOB_REG, val);

    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/* check RESET_IN_PROGRESS bit for an engine, should be run under rtnl lock */
static uint8_t
bxe_reset_is_done(struct bxe_softc *sc,
                  int              engine)
{
    uint32_t val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    uint32_t bit = engine ? BXE_PATH1_RST_IN_PROG_BIT :
                            BXE_PATH0_RST_IN_PROG_BIT;

    /* return false if bit is set */
    return (val & bit) ? FALSE : TRUE;
}

/* get the load status for an engine, should be run under rtnl lock */
static uint8_t
bxe_get_load_status(struct bxe_softc *sc,
                    int              engine)
{
    uint32_t mask = engine ? BXE_PATH1_LOAD_CNT_MASK :
                             BXE_PATH0_LOAD_CNT_MASK;
    uint32_t shift = engine ? BXE_PATH1_LOAD_CNT_SHIFT :
                              BXE_PATH0_LOAD_CNT_SHIFT;
    uint32_t val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);

    BLOGD(sc, DBG_LOAD, "Old value for GLOB_REG=0x%08x\n", val);

    val = ((val & mask) >> shift);

    BLOGD(sc, DBG_LOAD, "Load mask engine %d = 0x%08x\n", engine, val);

    return (val != 0);
}

/* set pf load mark */
/* XXX needs to be under rtnl lock */
static void
bxe_set_pf_load(struct bxe_softc *sc)
{
    uint32_t val;
    uint32_t val1;
    uint32_t mask = SC_PATH(sc) ? BXE_PATH1_LOAD_CNT_MASK :
                                  BXE_PATH0_LOAD_CNT_MASK;
    uint32_t shift = SC_PATH(sc) ? BXE_PATH1_LOAD_CNT_SHIFT :
                                   BXE_PATH0_LOAD_CNT_SHIFT;

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);

    val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    BLOGD(sc, DBG_LOAD, "Old value for GLOB_REG=0x%08x\n", val);

    /* get the current counter value */
    val1 = ((val & mask) >> shift);

    /* set bit of this PF */
    val1 |= (1 << SC_ABS_FUNC(sc));

    /* clear the old value */
    val &= ~mask;

    /* set the new one */
    val |= ((val1 << shift) & mask);

    REG_WR(sc, BXE_RECOVERY_GLOB_REG, val);

    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/* clear pf load mark */
/* XXX needs to be under rtnl lock */
static uint8_t
bxe_clear_pf_load(struct bxe_softc *sc)
{
    uint32_t val1, val;
    uint32_t mask = SC_PATH(sc) ? BXE_PATH1_LOAD_CNT_MASK :
                                  BXE_PATH0_LOAD_CNT_MASK;
    uint32_t shift = SC_PATH(sc) ? BXE_PATH1_LOAD_CNT_SHIFT :
                                   BXE_PATH0_LOAD_CNT_SHIFT;

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
    val = REG_RD(sc, BXE_RECOVERY_GLOB_REG);
    BLOGD(sc, DBG_LOAD, "Old GEN_REG_VAL=0x%08x\n", val);

    /* get the current counter value */
    val1 = (val & mask) >> shift;

    /* clear bit of that PF */
    val1 &= ~(1 << SC_ABS_FUNC(sc));

    /* clear the old value */
    val &= ~mask;

    /* set the new one */
    val |= ((val1 << shift) & mask);

    REG_WR(sc, BXE_RECOVERY_GLOB_REG, val);
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RECOVERY_REG);
    return (val1 != 0);
}

/* send load requrest to mcp and analyze response */
static int
bxe_nic_load_request(struct bxe_softc *sc,
                     uint32_t         *load_code)
{
    /* init fw_seq */
    sc->fw_seq =
        (SHMEM_RD(sc, func_mb[SC_FW_MB_IDX(sc)].drv_mb_header) &
         DRV_MSG_SEQ_NUMBER_MASK);

    BLOGD(sc, DBG_LOAD, "initial fw_seq 0x%04x\n", sc->fw_seq);

    /* get the current FW pulse sequence */
    sc->fw_drv_pulse_wr_seq =
        (SHMEM_RD(sc, func_mb[SC_FW_MB_IDX(sc)].drv_pulse_mb) &
         DRV_PULSE_SEQ_MASK);

    BLOGD(sc, DBG_LOAD, "initial drv_pulse 0x%04x\n",
          sc->fw_drv_pulse_wr_seq);

    /* load request */
    (*load_code) = bxe_fw_command(sc, DRV_MSG_CODE_LOAD_REQ,
                                  DRV_MSG_CODE_LOAD_REQ_WITH_LFA);

    /* if the MCP fails to respond we must abort */
    if (!(*load_code)) {
        BLOGE(sc, "MCP response failure!\n");
        return (-1);
    }

    /* if MCP refused then must abort */
    if ((*load_code) == FW_MSG_CODE_DRV_LOAD_REFUSED) {
        BLOGE(sc, "MCP refused load request\n");
        return (-1);
    }

    return (0);
}

/*
 * Check whether another PF has already loaded FW to chip. In virtualized
 * environments a pf from anoth VM may have already initialized the device
 * including loading FW.
 */
static int
bxe_nic_load_analyze_req(struct bxe_softc *sc,
                         uint32_t         load_code)
{
    uint32_t my_fw, loaded_fw;

    /* is another pf loaded on this engine? */
    if ((load_code != FW_MSG_CODE_DRV_LOAD_COMMON_CHIP) &&
        (load_code != FW_MSG_CODE_DRV_LOAD_COMMON)) {
        /* build my FW version dword */
        my_fw = (BCM_5710_FW_MAJOR_VERSION +
                 (BCM_5710_FW_MINOR_VERSION << 8 ) +
                 (BCM_5710_FW_REVISION_VERSION << 16) +
                 (BCM_5710_FW_ENGINEERING_VERSION << 24));

        /* read loaded FW from chip */
        loaded_fw = REG_RD(sc, XSEM_REG_PRAM);
        BLOGD(sc, DBG_LOAD, "loaded FW 0x%08x / my FW 0x%08x\n",
              loaded_fw, my_fw);

        /* abort nic load if version mismatch */
        if (my_fw != loaded_fw) {
            BLOGE(sc, "FW 0x%08x already loaded (mine is 0x%08x)",
                  loaded_fw, my_fw);
            return (-1);
        }
    }

    return (0);
}

/* mark PMF if applicable */
static void
bxe_nic_load_pmf(struct bxe_softc *sc,
                 uint32_t         load_code)
{
    uint32_t ncsi_oem_data_addr;

    if ((load_code == FW_MSG_CODE_DRV_LOAD_COMMON) ||
        (load_code == FW_MSG_CODE_DRV_LOAD_COMMON_CHIP) ||
        (load_code == FW_MSG_CODE_DRV_LOAD_PORT)) {
        /*
         * Barrier here for ordering between the writing to sc->port.pmf here
         * and reading it from the periodic task.
         */
        sc->port.pmf = 1;
        mb();
    } else {
        sc->port.pmf = 0;
    }

    BLOGD(sc, DBG_LOAD, "pmf %d\n", sc->port.pmf);

    /* XXX needed? */
    if (load_code == FW_MSG_CODE_DRV_LOAD_COMMON_CHIP) {
        if (SHMEM2_HAS(sc, ncsi_oem_data_addr)) {
            ncsi_oem_data_addr = SHMEM2_RD(sc, ncsi_oem_data_addr);
            if (ncsi_oem_data_addr) {
                REG_WR(sc,
                       (ncsi_oem_data_addr +
                        offsetof(struct glob_ncsi_oem_data, driver_version)),
                       0);
            }
        }
    }
}

static void
bxe_read_mf_cfg(struct bxe_softc *sc)
{
    int n = (CHIP_IS_MODE_4_PORT(sc) ? 2 : 1);
    int abs_func;
    int vn;

    if (BXE_NOMCP(sc)) {
        return; /* what should be the default bvalue in this case */
    }

    /*
     * The formula for computing the absolute function number is...
     * For 2 port configuration (4 functions per port):
     *   abs_func = 2 * vn + SC_PORT + SC_PATH
     * For 4 port configuration (2 functions per port):
     *   abs_func = 4 * vn + 2 * SC_PORT + SC_PATH
     */
    for (vn = VN_0; vn < SC_MAX_VN_NUM(sc); vn++) {
        abs_func = (n * (2 * vn + SC_PORT(sc)) + SC_PATH(sc));
        if (abs_func >= E1H_FUNC_MAX) {
            break;
        }
        sc->devinfo.mf_info.mf_config[vn] =
            MFCFG_RD(sc, func_mf_config[abs_func].config);
    }

    if (sc->devinfo.mf_info.mf_config[SC_VN(sc)] &
        FUNC_MF_CFG_FUNC_DISABLED) {
        BLOGD(sc, DBG_LOAD, "mf_cfg function disabled\n");
        sc->flags |= BXE_MF_FUNC_DIS;
    } else {
        BLOGD(sc, DBG_LOAD, "mf_cfg function enabled\n");
        sc->flags &= ~BXE_MF_FUNC_DIS;
    }
}

/* acquire split MCP access lock register */
static int bxe_acquire_alr(struct bxe_softc *sc)
{
    uint32_t j, val;

    for (j = 0; j < 1000; j++) {
        val = (1UL << 31);
        REG_WR(sc, GRCBASE_MCP + 0x9c, val);
        val = REG_RD(sc, GRCBASE_MCP + 0x9c);
        if (val & (1L << 31))
            break;

        DELAY(5000);
    }

    if (!(val & (1L << 31))) {
        BLOGE(sc, "Cannot acquire MCP access lock register\n");
        return (-1);
    }

    return (0);
}

/* release split MCP access lock register */
static void bxe_release_alr(struct bxe_softc *sc)
{
    REG_WR(sc, GRCBASE_MCP + 0x9c, 0);
}

static void
bxe_fan_failure(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    uint32_t ext_phy_config;

    /* mark the failure */
    ext_phy_config =
        SHMEM_RD(sc, dev_info.port_hw_config[port].external_phy_config);

    ext_phy_config &= ~PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK;
    ext_phy_config |= PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE;
    SHMEM_WR(sc, dev_info.port_hw_config[port].external_phy_config,
             ext_phy_config);

    /* log the failure */
    BLOGW(sc, "Fan Failure has caused the driver to shutdown "
              "the card to prevent permanent damage. "
              "Please contact OEM Support for assistance\n");

    /* XXX */
#if 1
    bxe_panic(sc, ("Schedule task to handle fan failure\n"));
#else
    /*
     * Schedule device reset (unload)
     * This is due to some boards consuming sufficient power when driver is
     * up to overheat if fan fails.
     */
    bxe_set_bit(BXE_SP_RTNL_FAN_FAILURE, &sc->sp_rtnl_state);
    schedule_delayed_work(&sc->sp_rtnl_task, 0);
#endif
}

/* this function is called upon a link interrupt */
static void
bxe_link_attn(struct bxe_softc *sc)
{
    uint32_t pause_enabled = 0;
    struct host_port_stats *pstats;
    int cmng_fns;
    struct bxe_fastpath *fp;
    int i;

    /* Make sure that we are synced with the current statistics */
    bxe_stats_handle(sc, STATS_EVENT_STOP);
    BLOGD(sc, DBG_LOAD, "link_vars phy_flags : %x\n", sc->link_vars.phy_flags);
    elink_link_update(&sc->link_params, &sc->link_vars);

    if (sc->link_vars.link_up) {

        /* dropless flow control */
        if (!CHIP_IS_E1(sc) && sc->dropless_fc) {
            pause_enabled = 0;

            if (sc->link_vars.flow_ctrl & ELINK_FLOW_CTRL_TX) {
                pause_enabled = 1;
            }

            REG_WR(sc,
                   (BAR_USTRORM_INTMEM +
                    USTORM_ETH_PAUSE_ENABLED_OFFSET(SC_PORT(sc))),
                   pause_enabled);
        }

        if (sc->link_vars.mac_type != ELINK_MAC_TYPE_EMAC) {
            pstats = BXE_SP(sc, port_stats);
            /* reset old mac stats */
            memset(&(pstats->mac_stx[0]), 0, sizeof(struct mac_stx));
        }

        if (sc->state == BXE_STATE_OPEN) {
            bxe_stats_handle(sc, STATS_EVENT_LINK_UP);
	    /* Restart tx when the link comes back. */
	    FOR_EACH_ETH_QUEUE(sc, i) {
		fp = &sc->fp[i];
		taskqueue_enqueue(fp->tq, &fp->tx_task);
	    }
        }

    }

    if (sc->link_vars.link_up && sc->link_vars.line_speed) {
        cmng_fns = bxe_get_cmng_fns_mode(sc);

        if (cmng_fns != CMNG_FNS_NONE) {
            bxe_cmng_fns_init(sc, FALSE, cmng_fns);
            storm_memset_cmng(sc, &sc->cmng, SC_PORT(sc));
        } else {
            /* rate shaping and fairness are disabled */
            BLOGD(sc, DBG_LOAD, "single function mode without fairness\n");
        }
    }

    bxe_link_report_locked(sc);

    if (IS_MF(sc)) {
        ; // XXX bxe_link_sync_notify(sc);
    }
}

static void
bxe_attn_int_asserted(struct bxe_softc *sc,
                      uint32_t         asserted)
{
    int port = SC_PORT(sc);
    uint32_t aeu_addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
                               MISC_REG_AEU_MASK_ATTN_FUNC_0;
    uint32_t nig_int_mask_addr = port ? NIG_REG_MASK_INTERRUPT_PORT1 :
                                        NIG_REG_MASK_INTERRUPT_PORT0;
    uint32_t aeu_mask;
    uint32_t nig_mask = 0;
    uint32_t reg_addr;
    uint32_t igu_acked;
    uint32_t cnt;

    if (sc->attn_state & asserted) {
        BLOGE(sc, "IGU ERROR attn=0x%08x\n", asserted);
    }

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);

    aeu_mask = REG_RD(sc, aeu_addr);

    BLOGD(sc, DBG_INTR, "aeu_mask 0x%08x newly asserted 0x%08x\n",
          aeu_mask, asserted);

    aeu_mask &= ~(asserted & 0x3ff);

    BLOGD(sc, DBG_INTR, "new mask 0x%08x\n", aeu_mask);

    REG_WR(sc, aeu_addr, aeu_mask);

    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);

    BLOGD(sc, DBG_INTR, "attn_state 0x%08x\n", sc->attn_state);
    sc->attn_state |= asserted;
    BLOGD(sc, DBG_INTR, "new state 0x%08x\n", sc->attn_state);

    if (asserted & ATTN_HARD_WIRED_MASK) {
        if (asserted & ATTN_NIG_FOR_FUNC) {

	    bxe_acquire_phy_lock(sc);
            /* save nig interrupt mask */
            nig_mask = REG_RD(sc, nig_int_mask_addr);

            /* If nig_mask is not set, no need to call the update function */
            if (nig_mask) {
                REG_WR(sc, nig_int_mask_addr, 0);

                bxe_link_attn(sc);
            }

            /* handle unicore attn? */
        }

        if (asserted & ATTN_SW_TIMER_4_FUNC) {
            BLOGD(sc, DBG_INTR, "ATTN_SW_TIMER_4_FUNC!\n");
        }

        if (asserted & GPIO_2_FUNC) {
            BLOGD(sc, DBG_INTR, "GPIO_2_FUNC!\n");
        }

        if (asserted & GPIO_3_FUNC) {
            BLOGD(sc, DBG_INTR, "GPIO_3_FUNC!\n");
        }

        if (asserted & GPIO_4_FUNC) {
            BLOGD(sc, DBG_INTR, "GPIO_4_FUNC!\n");
        }

        if (port == 0) {
            if (asserted & ATTN_GENERAL_ATTN_1) {
                BLOGD(sc, DBG_INTR, "ATTN_GENERAL_ATTN_1!\n");
                REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_1, 0x0);
            }
            if (asserted & ATTN_GENERAL_ATTN_2) {
                BLOGD(sc, DBG_INTR, "ATTN_GENERAL_ATTN_2!\n");
                REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_2, 0x0);
            }
            if (asserted & ATTN_GENERAL_ATTN_3) {
                BLOGD(sc, DBG_INTR, "ATTN_GENERAL_ATTN_3!\n");
                REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_3, 0x0);
            }
        } else {
            if (asserted & ATTN_GENERAL_ATTN_4) {
                BLOGD(sc, DBG_INTR, "ATTN_GENERAL_ATTN_4!\n");
                REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_4, 0x0);
            }
            if (asserted & ATTN_GENERAL_ATTN_5) {
                BLOGD(sc, DBG_INTR, "ATTN_GENERAL_ATTN_5!\n");
                REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_5, 0x0);
            }
            if (asserted & ATTN_GENERAL_ATTN_6) {
                BLOGD(sc, DBG_INTR, "ATTN_GENERAL_ATTN_6!\n");
                REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_6, 0x0);
            }
        }
    } /* hardwired */

    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        reg_addr = (HC_REG_COMMAND_REG + port*32 + COMMAND_REG_ATTN_BITS_SET);
    } else {
        reg_addr = (BAR_IGU_INTMEM + IGU_CMD_ATTN_BIT_SET_UPPER*8);
    }

    BLOGD(sc, DBG_INTR, "about to mask 0x%08x at %s addr 0x%08x\n",
          asserted,
          (sc->devinfo.int_block == INT_BLOCK_HC) ? "HC" : "IGU", reg_addr);
    REG_WR(sc, reg_addr, asserted);

    /* now set back the mask */
    if (asserted & ATTN_NIG_FOR_FUNC) {
        /*
         * Verify that IGU ack through BAR was written before restoring
         * NIG mask. This loop should exit after 2-3 iterations max.
         */
        if (sc->devinfo.int_block != INT_BLOCK_HC) {
            cnt = 0;

            do {
                igu_acked = REG_RD(sc, IGU_REG_ATTENTION_ACK_BITS);
            } while (((igu_acked & ATTN_NIG_FOR_FUNC) == 0) &&
                     (++cnt < MAX_IGU_ATTN_ACK_TO));

            if (!igu_acked) {
                BLOGE(sc, "Failed to verify IGU ack on time\n");
            }

            mb();
        }

        REG_WR(sc, nig_int_mask_addr, nig_mask);

	bxe_release_phy_lock(sc);
    }
}

static void
bxe_print_next_block(struct bxe_softc *sc,
                     int              idx,
                     const char       *blk)
{
    BLOGI(sc, "%s%s", idx ? ", " : "", blk);
}

static int
bxe_check_blocks_with_parity0(struct bxe_softc *sc,
                              uint32_t         sig,
                              int              par_num,
                              uint8_t          print)
{
    uint32_t cur_bit = 0;
    int i = 0;

    for (i = 0; sig; i++) {
        cur_bit = ((uint32_t)0x1 << i);
        if (sig & cur_bit) {
            switch (cur_bit) {
            case AEU_INPUTS_ATTN_BITS_BRB_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "BRB");
                break;
            case AEU_INPUTS_ATTN_BITS_PARSER_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "PARSER");
                break;
            case AEU_INPUTS_ATTN_BITS_TSDM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "TSDM");
                break;
            case AEU_INPUTS_ATTN_BITS_SEARCHER_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "SEARCHER");
                break;
            case AEU_INPUTS_ATTN_BITS_TCM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "TCM");
                break;
            case AEU_INPUTS_ATTN_BITS_TSEMI_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "TSEMI");
                break;
            case AEU_INPUTS_ATTN_BITS_PBCLIENT_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "XPB");
                break;
            }

            /* Clear the bit */
            sig &= ~cur_bit;
        }
    }

    return (par_num);
}

static int
bxe_check_blocks_with_parity1(struct bxe_softc *sc,
                              uint32_t         sig,
                              int              par_num,
                              uint8_t          *global,
                              uint8_t          print)
{
    int i = 0;
    uint32_t cur_bit = 0;
    for (i = 0; sig; i++) {
        cur_bit = ((uint32_t)0x1 << i);
        if (sig & cur_bit) {
            switch (cur_bit) {
            case AEU_INPUTS_ATTN_BITS_PBF_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "PBF");
                break;
            case AEU_INPUTS_ATTN_BITS_QM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "QM");
                break;
            case AEU_INPUTS_ATTN_BITS_TIMERS_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "TM");
                break;
            case AEU_INPUTS_ATTN_BITS_XSDM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "XSDM");
                break;
            case AEU_INPUTS_ATTN_BITS_XCM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "XCM");
                break;
            case AEU_INPUTS_ATTN_BITS_XSEMI_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "XSEMI");
                break;
            case AEU_INPUTS_ATTN_BITS_DOORBELLQ_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "DOORBELLQ");
                break;
            case AEU_INPUTS_ATTN_BITS_NIG_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "NIG");
                break;
            case AEU_INPUTS_ATTN_BITS_VAUX_PCI_CORE_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "VAUX PCI CORE");
                *global = TRUE;
                break;
            case AEU_INPUTS_ATTN_BITS_DEBUG_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "DEBUG");
                break;
            case AEU_INPUTS_ATTN_BITS_USDM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "USDM");
                break;
            case AEU_INPUTS_ATTN_BITS_UCM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "UCM");
                break;
            case AEU_INPUTS_ATTN_BITS_USEMI_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "USEMI");
                break;
            case AEU_INPUTS_ATTN_BITS_UPB_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "UPB");
                break;
            case AEU_INPUTS_ATTN_BITS_CSDM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "CSDM");
                break;
            case AEU_INPUTS_ATTN_BITS_CCM_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "CCM");
                break;
            }

            /* Clear the bit */
            sig &= ~cur_bit;
        }
    }

    return (par_num);
}

static int
bxe_check_blocks_with_parity2(struct bxe_softc *sc,
                              uint32_t         sig,
                              int              par_num,
                              uint8_t          print)
{
    uint32_t cur_bit = 0;
    int i = 0;

    for (i = 0; sig; i++) {
        cur_bit = ((uint32_t)0x1 << i);
        if (sig & cur_bit) {
            switch (cur_bit) {
            case AEU_INPUTS_ATTN_BITS_CSEMI_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "CSEMI");
                break;
            case AEU_INPUTS_ATTN_BITS_PXP_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "PXP");
                break;
            case AEU_IN_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "PXPPCICLOCKCLIENT");
                break;
            case AEU_INPUTS_ATTN_BITS_CFC_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "CFC");
                break;
            case AEU_INPUTS_ATTN_BITS_CDU_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "CDU");
                break;
            case AEU_INPUTS_ATTN_BITS_DMAE_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "DMAE");
                break;
            case AEU_INPUTS_ATTN_BITS_IGU_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "IGU");
                break;
            case AEU_INPUTS_ATTN_BITS_MISC_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "MISC");
                break;
            }

            /* Clear the bit */
            sig &= ~cur_bit;
        }
    }

    return (par_num);
}

static int
bxe_check_blocks_with_parity3(struct bxe_softc *sc,
                              uint32_t         sig,
                              int              par_num,
                              uint8_t          *global,
                              uint8_t          print)
{
    uint32_t cur_bit = 0;
    int i = 0;

    for (i = 0; sig; i++) {
        cur_bit = ((uint32_t)0x1 << i);
        if (sig & cur_bit) {
            switch (cur_bit) {
            case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY:
                if (print)
                    bxe_print_next_block(sc, par_num++, "MCP ROM");
                *global = TRUE;
                break;
            case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY:
                if (print)
                    bxe_print_next_block(sc, par_num++,
                              "MCP UMP RX");
                *global = TRUE;
                break;
            case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY:
                if (print)
                    bxe_print_next_block(sc, par_num++,
                              "MCP UMP TX");
                *global = TRUE;
                break;
            case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY:
                if (print)
                    bxe_print_next_block(sc, par_num++,
                              "MCP SCPAD");
                *global = TRUE;
                break;
            }

            /* Clear the bit */
            sig &= ~cur_bit;
        }
    }

    return (par_num);
}

static int
bxe_check_blocks_with_parity4(struct bxe_softc *sc,
                              uint32_t         sig,
                              int              par_num,
                              uint8_t          print)
{
    uint32_t cur_bit = 0;
    int i = 0;

    for (i = 0; sig; i++) {
        cur_bit = ((uint32_t)0x1 << i);
        if (sig & cur_bit) {
            switch (cur_bit) {
            case AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "PGLUE_B");
                break;
            case AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR:
                if (print)
                    bxe_print_next_block(sc, par_num++, "ATC");
                break;
            }

            /* Clear the bit */
            sig &= ~cur_bit;
        }
    }

    return (par_num);
}

static uint8_t
bxe_parity_attn(struct bxe_softc *sc,
                uint8_t          *global,
                uint8_t          print,
                uint32_t         *sig)
{
    int par_num = 0;

    if ((sig[0] & HW_PRTY_ASSERT_SET_0) ||
        (sig[1] & HW_PRTY_ASSERT_SET_1) ||
        (sig[2] & HW_PRTY_ASSERT_SET_2) ||
        (sig[3] & HW_PRTY_ASSERT_SET_3) ||
        (sig[4] & HW_PRTY_ASSERT_SET_4)) {
        BLOGE(sc, "Parity error: HW block parity attention:\n"
                  "[0]:0x%08x [1]:0x%08x [2]:0x%08x [3]:0x%08x [4]:0x%08x\n",
              (uint32_t)(sig[0] & HW_PRTY_ASSERT_SET_0),
              (uint32_t)(sig[1] & HW_PRTY_ASSERT_SET_1),
              (uint32_t)(sig[2] & HW_PRTY_ASSERT_SET_2),
              (uint32_t)(sig[3] & HW_PRTY_ASSERT_SET_3),
              (uint32_t)(sig[4] & HW_PRTY_ASSERT_SET_4));

        if (print)
            BLOGI(sc, "Parity errors detected in blocks: ");

        par_num =
            bxe_check_blocks_with_parity0(sc, sig[0] &
                                          HW_PRTY_ASSERT_SET_0,
                                          par_num, print);
        par_num =
            bxe_check_blocks_with_parity1(sc, sig[1] &
                                          HW_PRTY_ASSERT_SET_1,
                                          par_num, global, print);
        par_num =
            bxe_check_blocks_with_parity2(sc, sig[2] &
                                          HW_PRTY_ASSERT_SET_2,
                                          par_num, print);
        par_num =
            bxe_check_blocks_with_parity3(sc, sig[3] &
                                          HW_PRTY_ASSERT_SET_3,
                                          par_num, global, print);
        par_num =
            bxe_check_blocks_with_parity4(sc, sig[4] &
                                          HW_PRTY_ASSERT_SET_4,
                                          par_num, print);

        if (print)
            BLOGI(sc, "\n");

	if( *global == TRUE ) {
                BXE_SET_ERROR_BIT(sc, BXE_ERR_GLOBAL);
        }

        return (TRUE);
    }

    return (FALSE);
}

static uint8_t
bxe_chk_parity_attn(struct bxe_softc *sc,
                    uint8_t          *global,
                    uint8_t          print)
{
    struct attn_route attn = { {0} };
    int port = SC_PORT(sc);

    if(sc->state != BXE_STATE_OPEN)
        return FALSE;

    attn.sig[0] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + port*4);
    attn.sig[1] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_2_FUNC_0 + port*4);
    attn.sig[2] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_3_FUNC_0 + port*4);
    attn.sig[3] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_4_FUNC_0 + port*4);

    /*
     * Since MCP attentions can't be disabled inside the block, we need to
     * read AEU registers to see whether they're currently disabled
     */
    attn.sig[3] &= ((REG_RD(sc, (!port ? MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0
                                      : MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0)) &
                         MISC_AEU_ENABLE_MCP_PRTY_BITS) |
                        ~MISC_AEU_ENABLE_MCP_PRTY_BITS);


    if (!CHIP_IS_E1x(sc))
        attn.sig[4] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_5_FUNC_0 + port*4);

    return (bxe_parity_attn(sc, global, print, attn.sig));
}

static void
bxe_attn_int_deasserted4(struct bxe_softc *sc,
                         uint32_t         attn)
{
    uint32_t val;
    boolean_t err_flg = FALSE;

    if (attn & AEU_INPUTS_ATTN_BITS_PGLUE_HW_INTERRUPT) {
        val = REG_RD(sc, PGLUE_B_REG_PGLUE_B_INT_STS_CLR);
        BLOGE(sc, "PGLUE hw attention 0x%08x\n", val);
        err_flg = TRUE;
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_ADDRESS_ERROR)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_ADDRESS_ERROR\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_INCORRECT_RCV_BEHAVIOR)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_INCORRECT_RCV_BEHAVIOR\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_WAS_ERROR_ATTN)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_WAS_ERROR_ATTN\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_VF_LENGTH_VIOLATION_ATTN)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_VF_LENGTH_VIOLATION_ATTN\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_VF_GRC_SPACE_VIOLATION_ATTN)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_VF_GRC_SPACE_VIOLATION_ATTN\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_VF_MSIX_BAR_VIOLATION_ATTN)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_VF_MSIX_BAR_VIOLATION_ATTN\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_ERROR_ATTN)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_ERROR_ATTN\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_IN_TWO_RCBS_ATTN)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_IN_TWO_RCBS_ATTN\n");
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_CSSNOOP_FIFO_OVERFLOW)
            BLOGE(sc, "PGLUE_B_PGLUE_B_INT_STS_REG_CSSNOOP_FIFO_OVERFLOW\n");
    }

    if (attn & AEU_INPUTS_ATTN_BITS_ATC_HW_INTERRUPT) {
        val = REG_RD(sc, ATC_REG_ATC_INT_STS_CLR);
        BLOGE(sc, "ATC hw attention 0x%08x\n", val);
	err_flg = TRUE;
        if (val & ATC_ATC_INT_STS_REG_ADDRESS_ERROR)
            BLOGE(sc, "ATC_ATC_INT_STS_REG_ADDRESS_ERROR\n");
        if (val & ATC_ATC_INT_STS_REG_ATC_TCPL_TO_NOT_PEND)
            BLOGE(sc, "ATC_ATC_INT_STS_REG_ATC_TCPL_TO_NOT_PEND\n");
        if (val & ATC_ATC_INT_STS_REG_ATC_GPA_MULTIPLE_HITS)
            BLOGE(sc, "ATC_ATC_INT_STS_REG_ATC_GPA_MULTIPLE_HITS\n");
        if (val & ATC_ATC_INT_STS_REG_ATC_RCPL_TO_EMPTY_CNT)
            BLOGE(sc, "ATC_ATC_INT_STS_REG_ATC_RCPL_TO_EMPTY_CNT\n");
        if (val & ATC_ATC_INT_STS_REG_ATC_TCPL_ERROR)
            BLOGE(sc, "ATC_ATC_INT_STS_REG_ATC_TCPL_ERROR\n");
        if (val & ATC_ATC_INT_STS_REG_ATC_IREQ_LESS_THAN_STU)
            BLOGE(sc, "ATC_ATC_INT_STS_REG_ATC_IREQ_LESS_THAN_STU\n");
    }

    if (attn & (AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR |
                AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR)) {
        BLOGE(sc, "FATAL parity attention set4 0x%08x\n",
              (uint32_t)(attn & (AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR |
                                 AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR)));
	err_flg = TRUE;
    }
    if (err_flg) {
	BXE_SET_ERROR_BIT(sc, BXE_ERR_MISC);
	taskqueue_enqueue_timeout(taskqueue_thread,
	    &sc->sp_err_timeout_task, hz/10);
    }

}

static void
bxe_e1h_disable(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);

    bxe_tx_disable(sc);

    REG_WR(sc, NIG_REG_LLH0_FUNC_EN + port*8, 0);
}

static void
bxe_e1h_enable(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);

    REG_WR(sc, NIG_REG_LLH0_FUNC_EN + port*8, 1);

    // XXX bxe_tx_enable(sc);
}

/*
 * called due to MCP event (on pmf):
 *   reread new bandwidth configuration
 *   configure FW
 *   notify others function about the change
 */
static void
bxe_config_mf_bw(struct bxe_softc *sc)
{
    if (sc->link_vars.link_up) {
        bxe_cmng_fns_init(sc, TRUE, CMNG_FNS_MINMAX);
        // XXX bxe_link_sync_notify(sc);
    }

    storm_memset_cmng(sc, &sc->cmng, SC_PORT(sc));
}

static void
bxe_set_mf_bw(struct bxe_softc *sc)
{
    bxe_config_mf_bw(sc);
    bxe_fw_command(sc, DRV_MSG_CODE_SET_MF_BW_ACK, 0);
}

static void
bxe_handle_eee_event(struct bxe_softc *sc)
{
    BLOGD(sc, DBG_INTR, "EEE - LLDP event\n");
    bxe_fw_command(sc, DRV_MSG_CODE_EEE_RESULTS_ACK, 0);
}

#define DRV_INFO_ETH_STAT_NUM_MACS_REQUIRED 3

static void
bxe_drv_info_ether_stat(struct bxe_softc *sc)
{
    struct eth_stats_info *ether_stat =
        &sc->sp->drv_info_to_mcp.ether_stat;

    strlcpy(ether_stat->version, BXE_DRIVER_VERSION,
            ETH_STAT_INFO_VERSION_LEN);

    /* XXX (+ MAC_PAD) taken from other driver... verify this is right */
    sc->sp_objs[0].mac_obj.get_n_elements(sc, &sc->sp_objs[0].mac_obj,
                                          DRV_INFO_ETH_STAT_NUM_MACS_REQUIRED,
                                          ether_stat->mac_local + MAC_PAD,
                                          MAC_PAD, ETH_ALEN);

    ether_stat->mtu_size = sc->mtu;

    ether_stat->feature_flags |= FEATURE_ETH_CHKSUM_OFFLOAD_MASK;
    if (if_getcapenable(sc->ifp) & (IFCAP_TSO4 | IFCAP_TSO6)) {
        ether_stat->feature_flags |= FEATURE_ETH_LSO_MASK;
    }

    // XXX ether_stat->feature_flags |= ???;

    ether_stat->promiscuous_mode = 0; // (flags & PROMISC) ? 1 : 0;

    ether_stat->txq_size = sc->tx_ring_size;
    ether_stat->rxq_size = sc->rx_ring_size;
}

static void
bxe_handle_drv_info_req(struct bxe_softc *sc)
{
    enum drv_info_opcode op_code;
    uint32_t drv_info_ctl = SHMEM2_RD(sc, drv_info_control);

    /* if drv_info version supported by MFW doesn't match - send NACK */
    if ((drv_info_ctl & DRV_INFO_CONTROL_VER_MASK) != DRV_INFO_CUR_VER) {
        bxe_fw_command(sc, DRV_MSG_CODE_DRV_INFO_NACK, 0);
        return;
    }

    op_code = ((drv_info_ctl & DRV_INFO_CONTROL_OP_CODE_MASK) >>
               DRV_INFO_CONTROL_OP_CODE_SHIFT);

    memset(&sc->sp->drv_info_to_mcp, 0, sizeof(union drv_info_to_mcp));

    switch (op_code) {
    case ETH_STATS_OPCODE:
        bxe_drv_info_ether_stat(sc);
        break;
    case FCOE_STATS_OPCODE:
    case ISCSI_STATS_OPCODE:
    default:
        /* if op code isn't supported - send NACK */
        bxe_fw_command(sc, DRV_MSG_CODE_DRV_INFO_NACK, 0);
        return;
    }

    /*
     * If we got drv_info attn from MFW then these fields are defined in
     * shmem2 for sure
     */
    SHMEM2_WR(sc, drv_info_host_addr_lo,
              U64_LO(BXE_SP_MAPPING(sc, drv_info_to_mcp)));
    SHMEM2_WR(sc, drv_info_host_addr_hi,
              U64_HI(BXE_SP_MAPPING(sc, drv_info_to_mcp)));

    bxe_fw_command(sc, DRV_MSG_CODE_DRV_INFO_ACK, 0);
}

static void
bxe_dcc_event(struct bxe_softc *sc,
              uint32_t         dcc_event)
{
    BLOGD(sc, DBG_INTR, "dcc_event 0x%08x\n", dcc_event);

    if (dcc_event & DRV_STATUS_DCC_DISABLE_ENABLE_PF) {
        /*
         * This is the only place besides the function initialization
         * where the sc->flags can change so it is done without any
         * locks
         */
        if (sc->devinfo.mf_info.mf_config[SC_VN(sc)] & FUNC_MF_CFG_FUNC_DISABLED) {
            BLOGD(sc, DBG_INTR, "mf_cfg function disabled\n");
            sc->flags |= BXE_MF_FUNC_DIS;
            bxe_e1h_disable(sc);
        } else {
            BLOGD(sc, DBG_INTR, "mf_cfg function enabled\n");
            sc->flags &= ~BXE_MF_FUNC_DIS;
            bxe_e1h_enable(sc);
        }
        dcc_event &= ~DRV_STATUS_DCC_DISABLE_ENABLE_PF;
    }

    if (dcc_event & DRV_STATUS_DCC_BANDWIDTH_ALLOCATION) {
        bxe_config_mf_bw(sc);
        dcc_event &= ~DRV_STATUS_DCC_BANDWIDTH_ALLOCATION;
    }

    /* Report results to MCP */
    if (dcc_event)
        bxe_fw_command(sc, DRV_MSG_CODE_DCC_FAILURE, 0);
    else
        bxe_fw_command(sc, DRV_MSG_CODE_DCC_OK, 0);
}

static void
bxe_pmf_update(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    uint32_t val;

    sc->port.pmf = 1;
    BLOGD(sc, DBG_INTR, "pmf %d\n", sc->port.pmf);

    /*
     * We need the mb() to ensure the ordering between the writing to
     * sc->port.pmf here and reading it from the bxe_periodic_task().
     */
    mb();

    /* queue a periodic task */
    // XXX schedule task...

    // XXX bxe_dcbx_pmf_update(sc);

    /* enable nig attention */
    val = (0xff0f | (1 << (SC_VN(sc) + 4)));
    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        REG_WR(sc, HC_REG_TRAILING_EDGE_0 + port*8, val);
        REG_WR(sc, HC_REG_LEADING_EDGE_0 + port*8, val);
    } else if (!CHIP_IS_E1x(sc)) {
        REG_WR(sc, IGU_REG_TRAILING_EDGE_LATCH, val);
        REG_WR(sc, IGU_REG_LEADING_EDGE_LATCH, val);
    }

    bxe_stats_handle(sc, STATS_EVENT_PMF);
}

static int
bxe_mc_assert(struct bxe_softc *sc)
{
    char last_idx;
    int i, rc = 0;
    uint32_t row0, row1, row2, row3;

    /* XSTORM */
    last_idx = REG_RD8(sc, BAR_XSTRORM_INTMEM + XSTORM_ASSERT_LIST_INDEX_OFFSET);
    if (last_idx)
        BLOGE(sc, "XSTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);

    /* print the asserts */
    for (i = 0; i < STORM_ASSERT_ARRAY_SIZE; i++) {

        row0 = REG_RD(sc, BAR_XSTRORM_INTMEM + XSTORM_ASSERT_LIST_OFFSET(i));
        row1 = REG_RD(sc, BAR_XSTRORM_INTMEM + XSTORM_ASSERT_LIST_OFFSET(i) + 4);
        row2 = REG_RD(sc, BAR_XSTRORM_INTMEM + XSTORM_ASSERT_LIST_OFFSET(i) + 8);
        row3 = REG_RD(sc, BAR_XSTRORM_INTMEM + XSTORM_ASSERT_LIST_OFFSET(i) + 12);

        if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
            BLOGE(sc, "XSTORM_ASSERT_INDEX 0x%x = 0x%08x 0x%08x 0x%08x 0x%08x\n",
                  i, row3, row2, row1, row0);
            rc++;
        } else {
            break;
        }
    }

    /* TSTORM */
    last_idx = REG_RD8(sc, BAR_TSTRORM_INTMEM + TSTORM_ASSERT_LIST_INDEX_OFFSET);
    if (last_idx) {
        BLOGE(sc, "TSTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);
    }

    /* print the asserts */
    for (i = 0; i < STORM_ASSERT_ARRAY_SIZE; i++) {

        row0 = REG_RD(sc, BAR_TSTRORM_INTMEM + TSTORM_ASSERT_LIST_OFFSET(i));
        row1 = REG_RD(sc, BAR_TSTRORM_INTMEM + TSTORM_ASSERT_LIST_OFFSET(i) + 4);
        row2 = REG_RD(sc, BAR_TSTRORM_INTMEM + TSTORM_ASSERT_LIST_OFFSET(i) + 8);
        row3 = REG_RD(sc, BAR_TSTRORM_INTMEM + TSTORM_ASSERT_LIST_OFFSET(i) + 12);

        if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
            BLOGE(sc, "TSTORM_ASSERT_INDEX 0x%x = 0x%08x 0x%08x 0x%08x 0x%08x\n",
                  i, row3, row2, row1, row0);
            rc++;
        } else {
            break;
        }
    }

    /* CSTORM */
    last_idx = REG_RD8(sc, BAR_CSTRORM_INTMEM + CSTORM_ASSERT_LIST_INDEX_OFFSET);
    if (last_idx) {
        BLOGE(sc, "CSTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);
    }

    /* print the asserts */
    for (i = 0; i < STORM_ASSERT_ARRAY_SIZE; i++) {

        row0 = REG_RD(sc, BAR_CSTRORM_INTMEM + CSTORM_ASSERT_LIST_OFFSET(i));
        row1 = REG_RD(sc, BAR_CSTRORM_INTMEM + CSTORM_ASSERT_LIST_OFFSET(i) + 4);
        row2 = REG_RD(sc, BAR_CSTRORM_INTMEM + CSTORM_ASSERT_LIST_OFFSET(i) + 8);
        row3 = REG_RD(sc, BAR_CSTRORM_INTMEM + CSTORM_ASSERT_LIST_OFFSET(i) + 12);

        if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
            BLOGE(sc, "CSTORM_ASSERT_INDEX 0x%x = 0x%08x 0x%08x 0x%08x 0x%08x\n",
                  i, row3, row2, row1, row0);
            rc++;
        } else {
            break;
        }
    }

    /* USTORM */
    last_idx = REG_RD8(sc, BAR_USTRORM_INTMEM + USTORM_ASSERT_LIST_INDEX_OFFSET);
    if (last_idx) {
        BLOGE(sc, "USTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);
    }

    /* print the asserts */
    for (i = 0; i < STORM_ASSERT_ARRAY_SIZE; i++) {

        row0 = REG_RD(sc, BAR_USTRORM_INTMEM + USTORM_ASSERT_LIST_OFFSET(i));
        row1 = REG_RD(sc, BAR_USTRORM_INTMEM + USTORM_ASSERT_LIST_OFFSET(i) + 4);
        row2 = REG_RD(sc, BAR_USTRORM_INTMEM + USTORM_ASSERT_LIST_OFFSET(i) + 8);
        row3 = REG_RD(sc, BAR_USTRORM_INTMEM + USTORM_ASSERT_LIST_OFFSET(i) + 12);

        if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
            BLOGE(sc, "USTORM_ASSERT_INDEX 0x%x = 0x%08x 0x%08x 0x%08x 0x%08x\n",
                  i, row3, row2, row1, row0);
            rc++;
        } else {
            break;
        }
    }

    return (rc);
}

static void
bxe_attn_int_deasserted3(struct bxe_softc *sc,
                         uint32_t         attn)
{
    int func = SC_FUNC(sc);
    uint32_t val;

    if (attn & EVEREST_GEN_ATTN_IN_USE_MASK) {

        if (attn & BXE_PMF_LINK_ASSERT(sc)) {

            REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);
            bxe_read_mf_cfg(sc);
            sc->devinfo.mf_info.mf_config[SC_VN(sc)] =
                MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].config);
            val = SHMEM_RD(sc, func_mb[SC_FW_MB_IDX(sc)].drv_status);

            if (val & DRV_STATUS_DCC_EVENT_MASK)
                bxe_dcc_event(sc, (val & DRV_STATUS_DCC_EVENT_MASK));

            if (val & DRV_STATUS_SET_MF_BW)
                bxe_set_mf_bw(sc);

            if (val & DRV_STATUS_DRV_INFO_REQ)
                bxe_handle_drv_info_req(sc);

            if ((sc->port.pmf == 0) && (val & DRV_STATUS_PMF))
                bxe_pmf_update(sc);

            if (val & DRV_STATUS_EEE_NEGOTIATION_RESULTS)
                bxe_handle_eee_event(sc);

            if (sc->link_vars.periodic_flags &
                ELINK_PERIODIC_FLAGS_LINK_EVENT) {
                /* sync with link */
		bxe_acquire_phy_lock(sc);
                sc->link_vars.periodic_flags &=
                    ~ELINK_PERIODIC_FLAGS_LINK_EVENT;
		bxe_release_phy_lock(sc);
                if (IS_MF(sc))
                    ; // XXX bxe_link_sync_notify(sc);
                bxe_link_report(sc);
            }

            /*
             * Always call it here: bxe_link_report() will
             * prevent the link indication duplication.
             */
            bxe_link_status_update(sc);

        } else if (attn & BXE_MC_ASSERT_BITS) {

            BLOGE(sc, "MC assert!\n");
            bxe_mc_assert(sc);
            REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_10, 0);
            REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_9, 0);
            REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_8, 0);
            REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_7, 0);
            bxe_int_disable(sc);
            BXE_SET_ERROR_BIT(sc, BXE_ERR_MC_ASSERT);
            taskqueue_enqueue_timeout(taskqueue_thread,
                &sc->sp_err_timeout_task, hz/10);
	
        } else if (attn & BXE_MCP_ASSERT) {

            BLOGE(sc, "MCP assert!\n");
            REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_11, 0);
            BXE_SET_ERROR_BIT(sc, BXE_ERR_MCP_ASSERT);
            taskqueue_enqueue_timeout(taskqueue_thread,
                &sc->sp_err_timeout_task, hz/10);
            bxe_int_disable(sc);  /*avoid repetive assert alert */


        } else {
            BLOGE(sc, "Unknown HW assert! (attn 0x%08x)\n", attn);
        }
    }

    if (attn & EVEREST_LATCHED_ATTN_IN_USE_MASK) {
        BLOGE(sc, "LATCHED attention 0x%08x (masked)\n", attn);
        if (attn & BXE_GRC_TIMEOUT) {
            val = CHIP_IS_E1(sc) ? 0 : REG_RD(sc, MISC_REG_GRC_TIMEOUT_ATTN);
            BLOGE(sc, "GRC time-out 0x%08x\n", val);
        }
        if (attn & BXE_GRC_RSV) {
            val = CHIP_IS_E1(sc) ? 0 : REG_RD(sc, MISC_REG_GRC_RSV_ATTN);
            BLOGE(sc, "GRC reserved 0x%08x\n", val);
        }
        REG_WR(sc, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x7ff);
    }
}

static void
bxe_attn_int_deasserted2(struct bxe_softc *sc,
                         uint32_t         attn)
{
    int port = SC_PORT(sc);
    int reg_offset;
    uint32_t val0, mask0, val1, mask1;
    uint32_t val;
    boolean_t err_flg = FALSE;

    if (attn & AEU_INPUTS_ATTN_BITS_CFC_HW_INTERRUPT) {
        val = REG_RD(sc, CFC_REG_CFC_INT_STS_CLR);
        BLOGE(sc, "CFC hw attention 0x%08x\n", val);
        /* CFC error attention */
        if (val & 0x2) {
            BLOGE(sc, "FATAL error from CFC\n");
	    err_flg = TRUE;
        }
    }

    if (attn & AEU_INPUTS_ATTN_BITS_PXP_HW_INTERRUPT) {
        val = REG_RD(sc, PXP_REG_PXP_INT_STS_CLR_0);
        BLOGE(sc, "PXP hw attention-0 0x%08x\n", val);
        /* RQ_USDMDP_FIFO_OVERFLOW */
        if (val & 0x18000) {
            BLOGE(sc, "FATAL error from PXP\n");
	    err_flg = TRUE;
        }

        if (!CHIP_IS_E1x(sc)) {
            val = REG_RD(sc, PXP_REG_PXP_INT_STS_CLR_1);
            BLOGE(sc, "PXP hw attention-1 0x%08x\n", val);
	    err_flg = TRUE;
        }
    }

#define PXP2_EOP_ERROR_BIT  PXP2_PXP2_INT_STS_CLR_0_REG_WR_PGLUE_EOP_ERROR
#define AEU_PXP2_HW_INT_BIT AEU_INPUTS_ATTN_BITS_PXPPCICLOCKCLIENT_HW_INTERRUPT

    if (attn & AEU_PXP2_HW_INT_BIT) {
        /*  CQ47854 workaround do not panic on
         *  PXP2_PXP2_INT_STS_0_REG_WR_PGLUE_EOP_ERROR
         */
        if (!CHIP_IS_E1x(sc)) {
            mask0 = REG_RD(sc, PXP2_REG_PXP2_INT_MASK_0);
            val1 = REG_RD(sc, PXP2_REG_PXP2_INT_STS_1);
            mask1 = REG_RD(sc, PXP2_REG_PXP2_INT_MASK_1);
            val0 = REG_RD(sc, PXP2_REG_PXP2_INT_STS_0);
            /*
             * If the only PXP2_EOP_ERROR_BIT is set in
             * STS0 and STS1 - clear it
             *
             * probably we lose additional attentions between
             * STS0 and STS_CLR0, in this case user will not
             * be notified about them
             */
            if (val0 & mask0 & PXP2_EOP_ERROR_BIT &&
                !(val1 & mask1))
                val0 = REG_RD(sc, PXP2_REG_PXP2_INT_STS_CLR_0);

            /* print the register, since no one can restore it */
            BLOGE(sc, "PXP2_REG_PXP2_INT_STS_CLR_0 0x%08x\n", val0);

            /*
             * if PXP2_PXP2_INT_STS_0_REG_WR_PGLUE_EOP_ERROR
             * then notify
             */
            if (val0 & PXP2_EOP_ERROR_BIT) {
                BLOGE(sc, "PXP2_WR_PGLUE_EOP_ERROR\n");
		err_flg = TRUE;

                /*
                 * if only PXP2_PXP2_INT_STS_0_REG_WR_PGLUE_EOP_ERROR is
                 * set then clear attention from PXP2 block without panic
                 */
                if (((val0 & mask0) == PXP2_EOP_ERROR_BIT) &&
                    ((val1 & mask1) == 0))
                    attn &= ~AEU_PXP2_HW_INT_BIT;
            }
        }
    }

    if (attn & HW_INTERRUT_ASSERT_SET_2) {
        reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_2 :
                             MISC_REG_AEU_ENABLE1_FUNC_0_OUT_2);

        val = REG_RD(sc, reg_offset);
        val &= ~(attn & HW_INTERRUT_ASSERT_SET_2);
        REG_WR(sc, reg_offset, val);

        BLOGE(sc, "FATAL HW block attention set2 0x%x\n",
              (uint32_t)(attn & HW_INTERRUT_ASSERT_SET_2));
	err_flg = TRUE;
        bxe_panic(sc, ("HW block attention set2\n"));
    }
    if(err_flg) {
        BXE_SET_ERROR_BIT(sc, BXE_ERR_GLOBAL);
        taskqueue_enqueue_timeout(taskqueue_thread,
           &sc->sp_err_timeout_task, hz/10);
    }

}

static void
bxe_attn_int_deasserted1(struct bxe_softc *sc,
                         uint32_t         attn)
{
    int port = SC_PORT(sc);
    int reg_offset;
    uint32_t val;
    boolean_t err_flg = FALSE;

    if (attn & AEU_INPUTS_ATTN_BITS_DOORBELLQ_HW_INTERRUPT) {
        val = REG_RD(sc, DORQ_REG_DORQ_INT_STS_CLR);
        BLOGE(sc, "DB hw attention 0x%08x\n", val);
        /* DORQ discard attention */
        if (val & 0x2) {
            BLOGE(sc, "FATAL error from DORQ\n");
	    err_flg = TRUE;
        }
    }

    if (attn & HW_INTERRUT_ASSERT_SET_1) {
        reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_1 :
                             MISC_REG_AEU_ENABLE1_FUNC_0_OUT_1);

        val = REG_RD(sc, reg_offset);
        val &= ~(attn & HW_INTERRUT_ASSERT_SET_1);
        REG_WR(sc, reg_offset, val);

        BLOGE(sc, "FATAL HW block attention set1 0x%08x\n",
              (uint32_t)(attn & HW_INTERRUT_ASSERT_SET_1));
        err_flg = TRUE;
        bxe_panic(sc, ("HW block attention set1\n"));
    }
    if(err_flg) {
        BXE_SET_ERROR_BIT(sc, BXE_ERR_MISC);
        taskqueue_enqueue_timeout(taskqueue_thread,
           &sc->sp_err_timeout_task, hz/10);
    }

}

static void
bxe_attn_int_deasserted0(struct bxe_softc *sc,
                         uint32_t         attn)
{
    int port = SC_PORT(sc);
    int reg_offset;
    uint32_t val;

    reg_offset = (port) ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
                          MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0;

    if (attn & AEU_INPUTS_ATTN_BITS_SPIO5) {
        val = REG_RD(sc, reg_offset);
        val &= ~AEU_INPUTS_ATTN_BITS_SPIO5;
        REG_WR(sc, reg_offset, val);

        BLOGW(sc, "SPIO5 hw attention\n");

        /* Fan failure attention */
        elink_hw_reset_phy(&sc->link_params);
        bxe_fan_failure(sc);
    }

    if ((attn & sc->link_vars.aeu_int_mask) && sc->port.pmf) {
	bxe_acquire_phy_lock(sc);
        elink_handle_module_detect_int(&sc->link_params);
	bxe_release_phy_lock(sc);
    }

    if (attn & HW_INTERRUT_ASSERT_SET_0) {
        val = REG_RD(sc, reg_offset);
        val &= ~(attn & HW_INTERRUT_ASSERT_SET_0);
        REG_WR(sc, reg_offset, val);


        BXE_SET_ERROR_BIT(sc, BXE_ERR_MISC);
        taskqueue_enqueue_timeout(taskqueue_thread,
           &sc->sp_err_timeout_task, hz/10);

        bxe_panic(sc, ("FATAL HW block attention set0 0x%lx\n",
                       (attn & HW_INTERRUT_ASSERT_SET_0)));
    }
}

static void
bxe_attn_int_deasserted(struct bxe_softc *sc,
                        uint32_t         deasserted)
{
    struct attn_route attn;
    struct attn_route *group_mask;
    int port = SC_PORT(sc);
    int index;
    uint32_t reg_addr;
    uint32_t val;
    uint32_t aeu_mask;
    uint8_t global = FALSE;

    /*
     * Need to take HW lock because MCP or other port might also
     * try to handle this event.
     */
    bxe_acquire_alr(sc);

    if (bxe_chk_parity_attn(sc, &global, TRUE)) {
        /* XXX
         * In case of parity errors don't handle attentions so that
         * other function would "see" parity errors.
         */
        // XXX schedule a recovery task...
        /* disable HW interrupts */
        bxe_int_disable(sc);
        BXE_SET_ERROR_BIT(sc, BXE_ERR_PARITY);
        taskqueue_enqueue_timeout(taskqueue_thread,
           &sc->sp_err_timeout_task, hz/10);
        bxe_release_alr(sc);
        return;
    }

    attn.sig[0] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + port*4);
    attn.sig[1] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_2_FUNC_0 + port*4);
    attn.sig[2] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_3_FUNC_0 + port*4);
    attn.sig[3] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_4_FUNC_0 + port*4);
    if (!CHIP_IS_E1x(sc)) {
        attn.sig[4] = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_5_FUNC_0 + port*4);
    } else {
        attn.sig[4] = 0;
    }

    BLOGD(sc, DBG_INTR, "attn: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
          attn.sig[0], attn.sig[1], attn.sig[2], attn.sig[3], attn.sig[4]);

    for (index = 0; index < MAX_DYNAMIC_ATTN_GRPS; index++) {
        if (deasserted & (1 << index)) {
            group_mask = &sc->attn_group[index];

            BLOGD(sc, DBG_INTR,
                  "group[%d]: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n", index,
                  group_mask->sig[0], group_mask->sig[1],
                  group_mask->sig[2], group_mask->sig[3],
                  group_mask->sig[4]);

            bxe_attn_int_deasserted4(sc, attn.sig[4] & group_mask->sig[4]);
            bxe_attn_int_deasserted3(sc, attn.sig[3] & group_mask->sig[3]);
            bxe_attn_int_deasserted1(sc, attn.sig[1] & group_mask->sig[1]);
            bxe_attn_int_deasserted2(sc, attn.sig[2] & group_mask->sig[2]);
            bxe_attn_int_deasserted0(sc, attn.sig[0] & group_mask->sig[0]);
        }
    }

    bxe_release_alr(sc);

    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        reg_addr = (HC_REG_COMMAND_REG + port*32 +
                    COMMAND_REG_ATTN_BITS_CLR);
    } else {
        reg_addr = (BAR_IGU_INTMEM + IGU_CMD_ATTN_BIT_CLR_UPPER*8);
    }

    val = ~deasserted;
    BLOGD(sc, DBG_INTR,
          "about to mask 0x%08x at %s addr 0x%08x\n", val,
          (sc->devinfo.int_block == INT_BLOCK_HC) ? "HC" : "IGU", reg_addr);
    REG_WR(sc, reg_addr, val);

    if (~sc->attn_state & deasserted) {
        BLOGE(sc, "IGU error\n");
    }

    reg_addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
                      MISC_REG_AEU_MASK_ATTN_FUNC_0;

    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);

    aeu_mask = REG_RD(sc, reg_addr);

    BLOGD(sc, DBG_INTR, "aeu_mask 0x%08x newly deasserted 0x%08x\n",
          aeu_mask, deasserted);
    aeu_mask |= (deasserted & 0x3ff);
    BLOGD(sc, DBG_INTR, "new mask 0x%08x\n", aeu_mask);

    REG_WR(sc, reg_addr, aeu_mask);
    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);

    BLOGD(sc, DBG_INTR, "attn_state 0x%08x\n", sc->attn_state);
    sc->attn_state &= ~deasserted;
    BLOGD(sc, DBG_INTR, "new state 0x%08x\n", sc->attn_state);
}

static void
bxe_attn_int(struct bxe_softc *sc)
{
    /* read local copy of bits */
    uint32_t attn_bits = le32toh(sc->def_sb->atten_status_block.attn_bits);
    uint32_t attn_ack = le32toh(sc->def_sb->atten_status_block.attn_bits_ack);
    uint32_t attn_state = sc->attn_state;

    /* look for changed bits */
    uint32_t asserted   =  attn_bits & ~attn_ack & ~attn_state;
    uint32_t deasserted = ~attn_bits &  attn_ack &  attn_state;

    BLOGD(sc, DBG_INTR,
          "attn_bits 0x%08x attn_ack 0x%08x asserted 0x%08x deasserted 0x%08x\n",
          attn_bits, attn_ack, asserted, deasserted);

    if (~(attn_bits ^ attn_ack) & (attn_bits ^ attn_state)) {
        BLOGE(sc, "BAD attention state\n");
    }

    /* handle bits that were raised */
    if (asserted) {
        bxe_attn_int_asserted(sc, asserted);
    }

    if (deasserted) {
        bxe_attn_int_deasserted(sc, deasserted);
    }
}

static uint16_t
bxe_update_dsb_idx(struct bxe_softc *sc)
{
    struct host_sp_status_block *def_sb = sc->def_sb;
    uint16_t rc = 0;

    mb(); /* status block is written to by the chip */

    if (sc->def_att_idx != def_sb->atten_status_block.attn_bits_index) {
        sc->def_att_idx = def_sb->atten_status_block.attn_bits_index;
        rc |= BXE_DEF_SB_ATT_IDX;
    }

    if (sc->def_idx != def_sb->sp_sb.running_index) {
        sc->def_idx = def_sb->sp_sb.running_index;
        rc |= BXE_DEF_SB_IDX;
    }

    mb();

    return (rc);
}

static inline struct ecore_queue_sp_obj *
bxe_cid_to_q_obj(struct bxe_softc *sc,
                 uint32_t         cid)
{
    BLOGD(sc, DBG_SP, "retrieving fp from cid %d\n", cid);
    return (&sc->sp_objs[CID_TO_FP(cid, sc)].q_obj);
}

static void
bxe_handle_mcast_eqe(struct bxe_softc *sc)
{
    struct ecore_mcast_ramrod_params rparam;
    int rc;

    memset(&rparam, 0, sizeof(rparam));

    rparam.mcast_obj = &sc->mcast_obj;

    BXE_MCAST_LOCK(sc);

    /* clear pending state for the last command */
    sc->mcast_obj.raw.clear_pending(&sc->mcast_obj.raw);

    /* if there are pending mcast commands - send them */
    if (sc->mcast_obj.check_pending(&sc->mcast_obj)) {
        rc = ecore_config_mcast(sc, &rparam, ECORE_MCAST_CMD_CONT);
        if (rc < 0) {
            BLOGD(sc, DBG_SP,
                "ERROR: Failed to send pending mcast commands (%d)\n", rc);
        }
    }

    BXE_MCAST_UNLOCK(sc);
}

static void
bxe_handle_classification_eqe(struct bxe_softc      *sc,
                              union event_ring_elem *elem)
{
    unsigned long ramrod_flags = 0;
    int rc = 0;
    uint32_t cid = elem->message.data.eth_event.echo & BXE_SWCID_MASK;
    struct ecore_vlan_mac_obj *vlan_mac_obj;

    /* always push next commands out, don't wait here */
    bit_set(&ramrod_flags, RAMROD_CONT);

    switch (le32toh(elem->message.data.eth_event.echo) >> BXE_SWCID_SHIFT) {
    case ECORE_FILTER_MAC_PENDING:
        BLOGD(sc, DBG_SP, "Got SETUP_MAC completions\n");
        vlan_mac_obj = &sc->sp_objs[cid].mac_obj;
        break;

    case ECORE_FILTER_MCAST_PENDING:
        BLOGD(sc, DBG_SP, "Got SETUP_MCAST completions\n");
        /*
         * This is only relevant for 57710 where multicast MACs are
         * configured as unicast MACs using the same ramrod.
         */
        bxe_handle_mcast_eqe(sc);
        return;

    default:
        BLOGE(sc, "Unsupported classification command: %d\n",
              elem->message.data.eth_event.echo);
        return;
    }

    rc = vlan_mac_obj->complete(sc, vlan_mac_obj, elem, &ramrod_flags);

    if (rc < 0) {
        BLOGE(sc, "Failed to schedule new commands (%d)\n", rc);
    } else if (rc > 0) {
        BLOGD(sc, DBG_SP, "Scheduled next pending commands...\n");
    }
}

static void
bxe_handle_rx_mode_eqe(struct bxe_softc      *sc,
                       union event_ring_elem *elem)
{
    bxe_clear_bit(ECORE_FILTER_RX_MODE_PENDING, &sc->sp_state);

    /* send rx_mode command again if was requested */
    if (bxe_test_and_clear_bit(ECORE_FILTER_RX_MODE_SCHED,
                               &sc->sp_state)) {
        bxe_set_storm_rx_mode(sc);
    }
}

static void
bxe_update_eq_prod(struct bxe_softc *sc,
                   uint16_t         prod)
{
    storm_memset_eq_prod(sc, prod, SC_FUNC(sc));
    wmb(); /* keep prod updates ordered */
}

static void
bxe_eq_int(struct bxe_softc *sc)
{
    uint16_t hw_cons, sw_cons, sw_prod;
    union event_ring_elem *elem;
    uint8_t echo;
    uint32_t cid;
    uint8_t opcode;
    int spqe_cnt = 0;
    struct ecore_queue_sp_obj *q_obj;
    struct ecore_func_sp_obj *f_obj = &sc->func_obj;
    struct ecore_raw_obj *rss_raw = &sc->rss_conf_obj.raw;

    hw_cons = le16toh(*sc->eq_cons_sb);

    /*
     * The hw_cons range is 1-255, 257 - the sw_cons range is 0-254, 256.
     * when we get to the next-page we need to adjust so the loop
     * condition below will be met. The next element is the size of a
     * regular element and hence incrementing by 1
     */
    if ((hw_cons & EQ_DESC_MAX_PAGE) == EQ_DESC_MAX_PAGE) {
        hw_cons++;
    }

    /*
     * This function may never run in parallel with itself for a
     * specific sc and no need for a read memory barrier here.
     */
    sw_cons = sc->eq_cons;
    sw_prod = sc->eq_prod;

    BLOGD(sc, DBG_SP,"EQ: hw_cons=%u sw_cons=%u eq_spq_left=0x%lx\n",
          hw_cons, sw_cons, atomic_load_acq_long(&sc->eq_spq_left));

    for (;
         sw_cons != hw_cons;
         sw_prod = NEXT_EQ_IDX(sw_prod), sw_cons = NEXT_EQ_IDX(sw_cons)) {

        elem = &sc->eq[EQ_DESC(sw_cons)];

        /* elem CID originates from FW, actually LE */
        cid = SW_CID(elem->message.data.cfc_del_event.cid);
        opcode = elem->message.opcode;

        /* handle eq element */
        switch (opcode) {

        case EVENT_RING_OPCODE_STAT_QUERY:
            BLOGD(sc, DBG_SP, "got statistics completion event %d\n",
                  sc->stats_comp++);
            /* nothing to do with stats comp */
            goto next_spqe;

        case EVENT_RING_OPCODE_CFC_DEL:
            /* handle according to cid range */
            /* we may want to verify here that the sc state is HALTING */
            BLOGD(sc, DBG_SP, "got delete ramrod for MULTI[%d]\n", cid);
            q_obj = bxe_cid_to_q_obj(sc, cid);
            if (q_obj->complete_cmd(sc, q_obj, ECORE_Q_CMD_CFC_DEL)) {
                break;
            }
            goto next_spqe;

        case EVENT_RING_OPCODE_STOP_TRAFFIC:
            BLOGD(sc, DBG_SP, "got STOP TRAFFIC\n");
            if (f_obj->complete_cmd(sc, f_obj, ECORE_F_CMD_TX_STOP)) {
                break;
            }
            // XXX bxe_dcbx_set_params(sc, BXE_DCBX_STATE_TX_PAUSED);
            goto next_spqe;

        case EVENT_RING_OPCODE_START_TRAFFIC:
            BLOGD(sc, DBG_SP, "got START TRAFFIC\n");
            if (f_obj->complete_cmd(sc, f_obj, ECORE_F_CMD_TX_START)) {
                break;
            }
            // XXX bxe_dcbx_set_params(sc, BXE_DCBX_STATE_TX_RELEASED);
            goto next_spqe;

        case EVENT_RING_OPCODE_FUNCTION_UPDATE:
            echo = elem->message.data.function_update_event.echo;
            if (echo == SWITCH_UPDATE) {
                BLOGD(sc, DBG_SP, "got FUNC_SWITCH_UPDATE ramrod\n");
                if (f_obj->complete_cmd(sc, f_obj,
                                        ECORE_F_CMD_SWITCH_UPDATE)) {
                    break;
                }
            }
            else {
                BLOGD(sc, DBG_SP,
                      "AFEX: ramrod completed FUNCTION_UPDATE\n");
            }
            goto next_spqe;

        case EVENT_RING_OPCODE_FORWARD_SETUP:
            q_obj = &bxe_fwd_sp_obj(sc, q_obj);
            if (q_obj->complete_cmd(sc, q_obj,
                                    ECORE_Q_CMD_SETUP_TX_ONLY)) {
                break;
            }
            goto next_spqe;

        case EVENT_RING_OPCODE_FUNCTION_START:
            BLOGD(sc, DBG_SP, "got FUNC_START ramrod\n");
            if (f_obj->complete_cmd(sc, f_obj, ECORE_F_CMD_START)) {
                break;
            }
            goto next_spqe;

        case EVENT_RING_OPCODE_FUNCTION_STOP:
            BLOGD(sc, DBG_SP, "got FUNC_STOP ramrod\n");
            if (f_obj->complete_cmd(sc, f_obj, ECORE_F_CMD_STOP)) {
                break;
            }
            goto next_spqe;
        }

        switch (opcode | sc->state) {
        case (EVENT_RING_OPCODE_RSS_UPDATE_RULES | BXE_STATE_OPEN):
        case (EVENT_RING_OPCODE_RSS_UPDATE_RULES | BXE_STATE_OPENING_WAITING_PORT):
            cid = elem->message.data.eth_event.echo & BXE_SWCID_MASK;
            BLOGD(sc, DBG_SP, "got RSS_UPDATE ramrod. CID %d\n", cid);
            rss_raw->clear_pending(rss_raw);
            break;

        case (EVENT_RING_OPCODE_SET_MAC | BXE_STATE_OPEN):
        case (EVENT_RING_OPCODE_SET_MAC | BXE_STATE_DIAG):
        case (EVENT_RING_OPCODE_SET_MAC | BXE_STATE_CLOSING_WAITING_HALT):
        case (EVENT_RING_OPCODE_CLASSIFICATION_RULES | BXE_STATE_OPEN):
        case (EVENT_RING_OPCODE_CLASSIFICATION_RULES | BXE_STATE_DIAG):
        case (EVENT_RING_OPCODE_CLASSIFICATION_RULES | BXE_STATE_CLOSING_WAITING_HALT):
            BLOGD(sc, DBG_SP, "got (un)set mac ramrod\n");
            bxe_handle_classification_eqe(sc, elem);
            break;

        case (EVENT_RING_OPCODE_MULTICAST_RULES | BXE_STATE_OPEN):
        case (EVENT_RING_OPCODE_MULTICAST_RULES | BXE_STATE_DIAG):
        case (EVENT_RING_OPCODE_MULTICAST_RULES | BXE_STATE_CLOSING_WAITING_HALT):
            BLOGD(sc, DBG_SP, "got mcast ramrod\n");
            bxe_handle_mcast_eqe(sc);
            break;

        case (EVENT_RING_OPCODE_FILTERS_RULES | BXE_STATE_OPEN):
        case (EVENT_RING_OPCODE_FILTERS_RULES | BXE_STATE_DIAG):
        case (EVENT_RING_OPCODE_FILTERS_RULES | BXE_STATE_CLOSING_WAITING_HALT):
            BLOGD(sc, DBG_SP, "got rx_mode ramrod\n");
            bxe_handle_rx_mode_eqe(sc, elem);
            break;

        default:
            /* unknown event log error and continue */
            BLOGE(sc, "Unknown EQ event %d, sc->state 0x%x\n",
                  elem->message.opcode, sc->state);
        }

next_spqe:
        spqe_cnt++;
    } /* for */

    mb();
    atomic_add_acq_long(&sc->eq_spq_left, spqe_cnt);

    sc->eq_cons = sw_cons;
    sc->eq_prod = sw_prod;

    /* make sure that above mem writes were issued towards the memory */
    wmb();

    /* update producer */
    bxe_update_eq_prod(sc, sc->eq_prod);
}

static void
bxe_handle_sp_tq(void *context,
                 int  pending)
{
    struct bxe_softc *sc = (struct bxe_softc *)context;
    uint16_t status;

    BLOGD(sc, DBG_SP, "---> SP TASK <---\n");

    /* what work needs to be performed? */
    status = bxe_update_dsb_idx(sc);

    BLOGD(sc, DBG_SP, "dsb status 0x%04x\n", status);

    /* HW attentions */
    if (status & BXE_DEF_SB_ATT_IDX) {
        BLOGD(sc, DBG_SP, "---> ATTN INTR <---\n");
        bxe_attn_int(sc);
        status &= ~BXE_DEF_SB_ATT_IDX;
    }

    /* SP events: STAT_QUERY and others */
    if (status & BXE_DEF_SB_IDX) {
        /* handle EQ completions */
        BLOGD(sc, DBG_SP, "---> EQ INTR <---\n");
        bxe_eq_int(sc);
        bxe_ack_sb(sc, sc->igu_dsb_id, USTORM_ID,
                   le16toh(sc->def_idx), IGU_INT_NOP, 1);
        status &= ~BXE_DEF_SB_IDX;
    }

    /* if status is non zero then something went wrong */
    if (__predict_false(status)) {
        BLOGE(sc, "Got an unknown SP interrupt! (0x%04x)\n", status);
    }

    /* ack status block only if something was actually handled */
    bxe_ack_sb(sc, sc->igu_dsb_id, ATTENTION_ID,
               le16toh(sc->def_att_idx), IGU_INT_ENABLE, 1);

    /*
     * Must be called after the EQ processing (since eq leads to sriov
     * ramrod completion flows).
     * This flow may have been scheduled by the arrival of a ramrod
     * completion, or by the sriov code rescheduling itself.
     */
    // XXX bxe_iov_sp_task(sc);

}

static void
bxe_handle_fp_tq(void *context,
                 int  pending)
{
    struct bxe_fastpath *fp = (struct bxe_fastpath *)context;
    struct bxe_softc *sc = fp->sc;
    uint8_t more_tx = FALSE;
    uint8_t more_rx = FALSE;

    BLOGD(sc, DBG_INTR, "---> FP TASK QUEUE (%d) <---\n", fp->index);

    /* XXX
     * IFF_DRV_RUNNING state can't be checked here since we process
     * slowpath events on a client queue during setup. Instead
     * we need to add a "process/continue" flag here that the driver
     * can use to tell the task here not to do anything.
     */
#if 0
    if (!(if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING)) {
        return;
    }
#endif

    /* update the fastpath index */
    bxe_update_fp_sb_idx(fp);

    /* XXX add loop here if ever support multiple tx CoS */
    /* fp->txdata[cos] */
    if (bxe_has_tx_work(fp)) {
        BXE_FP_TX_LOCK(fp);
        more_tx = bxe_txeof(sc, fp);
        BXE_FP_TX_UNLOCK(fp);
    }

    if (bxe_has_rx_work(fp)) {
        more_rx = bxe_rxeof(sc, fp);
    }

    if (more_rx /*|| more_tx*/) {
        /* still more work to do */
        taskqueue_enqueue(fp->tq, &fp->tq_task);
        return;
    }

    bxe_ack_sb(sc, fp->igu_sb_id, USTORM_ID,
               le16toh(fp->fp_hc_idx), IGU_INT_ENABLE, 1);
}

static void
bxe_task_fp(struct bxe_fastpath *fp)
{
    struct bxe_softc *sc = fp->sc;
    uint8_t more_tx = FALSE;
    uint8_t more_rx = FALSE;

    BLOGD(sc, DBG_INTR, "---> FP TASK ISR (%d) <---\n", fp->index);

    /* update the fastpath index */
    bxe_update_fp_sb_idx(fp);

    /* XXX add loop here if ever support multiple tx CoS */
    /* fp->txdata[cos] */
    if (bxe_has_tx_work(fp)) {
        BXE_FP_TX_LOCK(fp);
        more_tx = bxe_txeof(sc, fp);
        BXE_FP_TX_UNLOCK(fp);
    }

    if (bxe_has_rx_work(fp)) {
        more_rx = bxe_rxeof(sc, fp);
    }

    if (more_rx /*|| more_tx*/) {
        /* still more work to do, bail out if this ISR and process later */
        taskqueue_enqueue(fp->tq, &fp->tq_task);
        return;
    }

    /*
     * Here we write the fastpath index taken before doing any tx or rx work.
     * It is very well possible other hw events occurred up to this point and
     * they were actually processed accordingly above. Since we're going to
     * write an older fastpath index, an interrupt is coming which we might
     * not do any work in.
     */
    bxe_ack_sb(sc, fp->igu_sb_id, USTORM_ID,
               le16toh(fp->fp_hc_idx), IGU_INT_ENABLE, 1);
}

/*
 * Legacy interrupt entry point.
 *
 * Verifies that the controller generated the interrupt and
 * then calls a separate routine to handle the various
 * interrupt causes: link, RX, and TX.
 */
static void
bxe_intr_legacy(void *xsc)
{
    struct bxe_softc *sc = (struct bxe_softc *)xsc;
    struct bxe_fastpath *fp;
    uint16_t status, mask;
    int i;

    BLOGD(sc, DBG_INTR, "---> BXE INTx <---\n");

    /*
     * 0 for ustorm, 1 for cstorm
     * the bits returned from ack_int() are 0-15
     * bit 0 = attention status block
     * bit 1 = fast path status block
     * a mask of 0x2 or more = tx/rx event
     * a mask of 1 = slow path event
     */

    status = bxe_ack_int(sc);

    /* the interrupt is not for us */
    if (__predict_false(status == 0)) {
        BLOGD(sc, DBG_INTR, "Not our interrupt!\n");
        return;
    }

    BLOGD(sc, DBG_INTR, "Interrupt status 0x%04x\n", status);

    FOR_EACH_ETH_QUEUE(sc, i) {
        fp = &sc->fp[i];
        mask = (0x2 << (fp->index + CNIC_SUPPORT(sc)));
        if (status & mask) {
            /* acknowledge and disable further fastpath interrupts */
            bxe_ack_sb(sc, fp->igu_sb_id, USTORM_ID, 0, IGU_INT_DISABLE, 0);
            bxe_task_fp(fp);
            status &= ~mask;
        }
    }

    if (__predict_false(status & 0x1)) {
        /* acknowledge and disable further slowpath interrupts */
        bxe_ack_sb(sc, sc->igu_dsb_id, USTORM_ID, 0, IGU_INT_DISABLE, 0);

        /* schedule slowpath handler */
        taskqueue_enqueue(sc->sp_tq, &sc->sp_tq_task);

        status &= ~0x1;
    }

    if (__predict_false(status)) {
        BLOGW(sc, "Unexpected fastpath status (0x%08x)!\n", status);
    }
}

/* slowpath interrupt entry point */
static void
bxe_intr_sp(void *xsc)
{
    struct bxe_softc *sc = (struct bxe_softc *)xsc;

    BLOGD(sc, (DBG_INTR | DBG_SP), "---> SP INTR <---\n");

    /* acknowledge and disable further slowpath interrupts */
    bxe_ack_sb(sc, sc->igu_dsb_id, USTORM_ID, 0, IGU_INT_DISABLE, 0);

    /* schedule slowpath handler */
    taskqueue_enqueue(sc->sp_tq, &sc->sp_tq_task);
}

/* fastpath interrupt entry point */
static void
bxe_intr_fp(void *xfp)
{
    struct bxe_fastpath *fp = (struct bxe_fastpath *)xfp;
    struct bxe_softc *sc = fp->sc;

    BLOGD(sc, DBG_INTR, "---> FP INTR %d <---\n", fp->index);

    BLOGD(sc, DBG_INTR,
          "(cpu=%d) MSI-X fp=%d fw_sb=%d igu_sb=%d\n",
          curcpu, fp->index, fp->fw_sb_id, fp->igu_sb_id);

    /* acknowledge and disable further fastpath interrupts */
    bxe_ack_sb(sc, fp->igu_sb_id, USTORM_ID, 0, IGU_INT_DISABLE, 0);

    bxe_task_fp(fp);
}

/* Release all interrupts allocated by the driver. */
static void
bxe_interrupt_free(struct bxe_softc *sc)
{
    int i;

    switch (sc->interrupt_mode) {
    case INTR_MODE_INTX:
        BLOGD(sc, DBG_LOAD, "Releasing legacy INTx vector\n");
        if (sc->intr[0].resource != NULL) {
            bus_release_resource(sc->dev,
                                 SYS_RES_IRQ,
                                 sc->intr[0].rid,
                                 sc->intr[0].resource);
        }
        break;
    case INTR_MODE_MSI:
        for (i = 0; i < sc->intr_count; i++) {
            BLOGD(sc, DBG_LOAD, "Releasing MSI vector %d\n", i);
            if (sc->intr[i].resource && sc->intr[i].rid) {
                bus_release_resource(sc->dev,
                                     SYS_RES_IRQ,
                                     sc->intr[i].rid,
                                     sc->intr[i].resource);
            }
        }
        pci_release_msi(sc->dev);
        break;
    case INTR_MODE_MSIX:
        for (i = 0; i < sc->intr_count; i++) {
            BLOGD(sc, DBG_LOAD, "Releasing MSI-X vector %d\n", i);
            if (sc->intr[i].resource && sc->intr[i].rid) {
                bus_release_resource(sc->dev,
                                     SYS_RES_IRQ,
                                     sc->intr[i].rid,
                                     sc->intr[i].resource);
            }
        }
        pci_release_msi(sc->dev);
        break;
    default:
        /* nothing to do as initial allocation failed */
        break;
    }
}

/*
 * This function determines and allocates the appropriate
 * interrupt based on system capabilites and user request.
 *
 * The user may force a particular interrupt mode, specify
 * the number of receive queues, specify the method for
 * distribuitng received frames to receive queues, or use
 * the default settings which will automatically select the
 * best supported combination.  In addition, the OS may or
 * may not support certain combinations of these settings.
 * This routine attempts to reconcile the settings requested
 * by the user with the capabilites available from the system
 * to select the optimal combination of features.
 *
 * Returns:
 *   0 = Success, !0 = Failure.
 */
static int
bxe_interrupt_alloc(struct bxe_softc *sc)
{
    int msix_count = 0;
    int msi_count = 0;
    int num_requested = 0;
    int num_allocated = 0;
    int rid, i, j;
    int rc;

    /* get the number of available MSI/MSI-X interrupts from the OS */
    if (sc->interrupt_mode > 0) {
        if (sc->devinfo.pcie_cap_flags & BXE_MSIX_CAPABLE_FLAG) {
            msix_count = pci_msix_count(sc->dev);
        }

        if (sc->devinfo.pcie_cap_flags & BXE_MSI_CAPABLE_FLAG) {
            msi_count = pci_msi_count(sc->dev);
        }

        BLOGD(sc, DBG_LOAD, "%d MSI and %d MSI-X vectors available\n",
              msi_count, msix_count);
    }

    do { /* try allocating MSI-X interrupt resources (at least 2) */
        if (sc->interrupt_mode != INTR_MODE_MSIX) {
            break;
        }

        if (((sc->devinfo.pcie_cap_flags & BXE_MSIX_CAPABLE_FLAG) == 0) ||
            (msix_count < 2)) {
            sc->interrupt_mode = INTR_MODE_MSI; /* try MSI next */
            break;
        }

        /* ask for the necessary number of MSI-X vectors */
        num_requested = min((sc->num_queues + 1), msix_count);

        BLOGD(sc, DBG_LOAD, "Requesting %d MSI-X vectors\n", num_requested);

        num_allocated = num_requested;
        if ((rc = pci_alloc_msix(sc->dev, &num_allocated)) != 0) {
            BLOGE(sc, "MSI-X alloc failed! (%d)\n", rc);
            sc->interrupt_mode = INTR_MODE_MSI; /* try MSI next */
            break;
        }

        if (num_allocated < 2) { /* possible? */
            BLOGE(sc, "MSI-X allocation less than 2!\n");
            sc->interrupt_mode = INTR_MODE_MSI; /* try MSI next */
            pci_release_msi(sc->dev);
            break;
        }

        BLOGI(sc, "MSI-X vectors Requested %d and Allocated %d\n",
              num_requested, num_allocated);

        /* best effort so use the number of vectors allocated to us */
        sc->intr_count = num_allocated;
        sc->num_queues = num_allocated - 1;

        rid = 1; /* initial resource identifier */

        /* allocate the MSI-X vectors */
        for (i = 0; i < num_allocated; i++) {
            sc->intr[i].rid = (rid + i);

            if ((sc->intr[i].resource =
                 bus_alloc_resource_any(sc->dev,
                                        SYS_RES_IRQ,
                                        &sc->intr[i].rid,
                                        RF_ACTIVE)) == NULL) {
                BLOGE(sc, "Failed to map MSI-X[%d] (rid=%d)!\n",
                      i, (rid + i));

                for (j = (i - 1); j >= 0; j--) {
                    bus_release_resource(sc->dev,
                                         SYS_RES_IRQ,
                                         sc->intr[j].rid,
                                         sc->intr[j].resource);
                }

                sc->intr_count = 0;
                sc->num_queues = 0;
                sc->interrupt_mode = INTR_MODE_MSI; /* try MSI next */
                pci_release_msi(sc->dev);
                break;
            }

            BLOGD(sc, DBG_LOAD, "Mapped MSI-X[%d] (rid=%d)\n", i, (rid + i));
        }
    } while (0);

    do { /* try allocating MSI vector resources (at least 2) */
        if (sc->interrupt_mode != INTR_MODE_MSI) {
            break;
        }

        if (((sc->devinfo.pcie_cap_flags & BXE_MSI_CAPABLE_FLAG) == 0) ||
            (msi_count < 1)) {
            sc->interrupt_mode = INTR_MODE_INTX; /* try INTx next */
            break;
        }

        /* ask for a single MSI vector */
        num_requested = 1;

        BLOGD(sc, DBG_LOAD, "Requesting %d MSI vectors\n", num_requested);

        num_allocated = num_requested;
        if ((rc = pci_alloc_msi(sc->dev, &num_allocated)) != 0) {
            BLOGE(sc, "MSI alloc failed (%d)!\n", rc);
            sc->interrupt_mode = INTR_MODE_INTX; /* try INTx next */
            break;
        }

        if (num_allocated != 1) { /* possible? */
            BLOGE(sc, "MSI allocation is not 1!\n");
            sc->interrupt_mode = INTR_MODE_INTX; /* try INTx next */
            pci_release_msi(sc->dev);
            break;
        }

        BLOGI(sc, "MSI vectors Requested %d and Allocated %d\n",
              num_requested, num_allocated);

        /* best effort so use the number of vectors allocated to us */
        sc->intr_count = num_allocated;
        sc->num_queues = num_allocated;

        rid = 1; /* initial resource identifier */

        sc->intr[0].rid = rid;

        if ((sc->intr[0].resource =
             bus_alloc_resource_any(sc->dev,
                                    SYS_RES_IRQ,
                                    &sc->intr[0].rid,
                                    RF_ACTIVE)) == NULL) {
            BLOGE(sc, "Failed to map MSI[0] (rid=%d)!\n", rid);
            sc->intr_count = 0;
            sc->num_queues = 0;
            sc->interrupt_mode = INTR_MODE_INTX; /* try INTx next */
            pci_release_msi(sc->dev);
            break;
        }

        BLOGD(sc, DBG_LOAD, "Mapped MSI[0] (rid=%d)\n", rid);
    } while (0);

    do { /* try allocating INTx vector resources */
        if (sc->interrupt_mode != INTR_MODE_INTX) {
            break;
        }

        BLOGD(sc, DBG_LOAD, "Requesting legacy INTx interrupt\n");

        /* only one vector for INTx */
        sc->intr_count = 1;
        sc->num_queues = 1;

        rid = 0; /* initial resource identifier */

        sc->intr[0].rid = rid;

        if ((sc->intr[0].resource =
             bus_alloc_resource_any(sc->dev,
                                    SYS_RES_IRQ,
                                    &sc->intr[0].rid,
                                    (RF_ACTIVE | RF_SHAREABLE))) == NULL) {
            BLOGE(sc, "Failed to map INTx (rid=%d)!\n", rid);
            sc->intr_count = 0;
            sc->num_queues = 0;
            sc->interrupt_mode = -1; /* Failed! */
            break;
        }

        BLOGD(sc, DBG_LOAD, "Mapped INTx (rid=%d)\n", rid);
    } while (0);

    if (sc->interrupt_mode == -1) {
        BLOGE(sc, "Interrupt Allocation: FAILED!!!\n");
        rc = 1;
    } else {
        BLOGD(sc, DBG_LOAD,
              "Interrupt Allocation: interrupt_mode=%d, num_queues=%d\n",
              sc->interrupt_mode, sc->num_queues);
        rc = 0;
    }

    return (rc);
}

static void
bxe_interrupt_detach(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int i;

    /* release interrupt resources */
    for (i = 0; i < sc->intr_count; i++) {
        if (sc->intr[i].resource && sc->intr[i].tag) {
            BLOGD(sc, DBG_LOAD, "Disabling interrupt vector %d\n", i);
            bus_teardown_intr(sc->dev, sc->intr[i].resource, sc->intr[i].tag);
        }
    }

    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];
        if (fp->tq) {
            taskqueue_drain(fp->tq, &fp->tq_task);
            taskqueue_drain(fp->tq, &fp->tx_task);
            while (taskqueue_cancel_timeout(fp->tq, &fp->tx_timeout_task,
                NULL))
                taskqueue_drain_timeout(fp->tq, &fp->tx_timeout_task);
        }

        for (i = 0; i < sc->num_queues; i++) {
            fp = &sc->fp[i];
            if (fp->tq != NULL) {
                taskqueue_free(fp->tq);
                fp->tq = NULL;
            }
        }
    }

    if (sc->sp_tq) {
        taskqueue_drain(sc->sp_tq, &sc->sp_tq_task);
        taskqueue_free(sc->sp_tq);
        sc->sp_tq = NULL;
    }
}

/*
 * Enables interrupts and attach to the ISR.
 *
 * When using multiple MSI/MSI-X vectors the first vector
 * is used for slowpath operations while all remaining
 * vectors are used for fastpath operations.  If only a
 * single MSI/MSI-X vector is used (SINGLE_ISR) then the
 * ISR must look for both slowpath and fastpath completions.
 */
static int
bxe_interrupt_attach(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int rc = 0;
    int i;

    snprintf(sc->sp_tq_name, sizeof(sc->sp_tq_name),
             "bxe%d_sp_tq", sc->unit);
    TASK_INIT(&sc->sp_tq_task, 0, bxe_handle_sp_tq, sc);
    sc->sp_tq = taskqueue_create(sc->sp_tq_name, M_NOWAIT,
                                 taskqueue_thread_enqueue,
                                 &sc->sp_tq);
    taskqueue_start_threads(&sc->sp_tq, 1, PWAIT, /* lower priority */
                            "%s", sc->sp_tq_name);


    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];
        snprintf(fp->tq_name, sizeof(fp->tq_name),
                 "bxe%d_fp%d_tq", sc->unit, i);
        TASK_INIT(&fp->tq_task, 0, bxe_handle_fp_tq, fp);
        TASK_INIT(&fp->tx_task, 0, bxe_tx_mq_start_deferred, fp);
        fp->tq = taskqueue_create(fp->tq_name, M_NOWAIT,
                                  taskqueue_thread_enqueue,
                                  &fp->tq);
        TIMEOUT_TASK_INIT(fp->tq, &fp->tx_timeout_task, 0,
                          bxe_tx_mq_start_deferred, fp);
        taskqueue_start_threads(&fp->tq, 1, PI_NET, /* higher priority */
                                "%s", fp->tq_name);
    }

    /* setup interrupt handlers */
    if (sc->interrupt_mode == INTR_MODE_MSIX) {
        BLOGD(sc, DBG_LOAD, "Enabling slowpath MSI-X[0] vector\n");

        /*
         * Setup the interrupt handler. Note that we pass the driver instance
         * to the interrupt handler for the slowpath.
         */
        if ((rc = bus_setup_intr(sc->dev, sc->intr[0].resource,
                                 (INTR_TYPE_NET | INTR_MPSAFE),
                                 NULL, bxe_intr_sp, sc,
                                 &sc->intr[0].tag)) != 0) {
            BLOGE(sc, "Failed to allocate MSI-X[0] vector (%d)\n", rc);
            goto bxe_interrupt_attach_exit;
        }

        bus_describe_intr(sc->dev, sc->intr[0].resource,
                          sc->intr[0].tag, "sp");

        /* bus_bind_intr(sc->dev, sc->intr[0].resource, 0); */

        /* initialize the fastpath vectors (note the first was used for sp) */
        for (i = 0; i < sc->num_queues; i++) {
            fp = &sc->fp[i];
            BLOGD(sc, DBG_LOAD, "Enabling MSI-X[%d] vector\n", (i + 1));

            /*
             * Setup the interrupt handler. Note that we pass the
             * fastpath context to the interrupt handler in this
             * case.
             */
            if ((rc = bus_setup_intr(sc->dev, sc->intr[i + 1].resource,
                                     (INTR_TYPE_NET | INTR_MPSAFE),
                                     NULL, bxe_intr_fp, fp,
                                     &sc->intr[i + 1].tag)) != 0) {
                BLOGE(sc, "Failed to allocate MSI-X[%d] vector (%d)\n",
                      (i + 1), rc);
                goto bxe_interrupt_attach_exit;
            }

            bus_describe_intr(sc->dev, sc->intr[i + 1].resource,
                              sc->intr[i + 1].tag, "fp%02d", i);

            /* bind the fastpath instance to a cpu */
            if (sc->num_queues > 1) {
                bus_bind_intr(sc->dev, sc->intr[i + 1].resource, i);
            }

            fp->state = BXE_FP_STATE_IRQ;
        }
    } else if (sc->interrupt_mode == INTR_MODE_MSI) {
        BLOGD(sc, DBG_LOAD, "Enabling MSI[0] vector\n");

        /*
         * Setup the interrupt handler. Note that we pass the
         * driver instance to the interrupt handler which
         * will handle both the slowpath and fastpath.
         */
        if ((rc = bus_setup_intr(sc->dev, sc->intr[0].resource,
                                 (INTR_TYPE_NET | INTR_MPSAFE),
                                 NULL, bxe_intr_legacy, sc,
                                 &sc->intr[0].tag)) != 0) {
            BLOGE(sc, "Failed to allocate MSI[0] vector (%d)\n", rc);
            goto bxe_interrupt_attach_exit;
        }

    } else { /* (sc->interrupt_mode == INTR_MODE_INTX) */
        BLOGD(sc, DBG_LOAD, "Enabling INTx interrupts\n");

        /*
         * Setup the interrupt handler. Note that we pass the
         * driver instance to the interrupt handler which
         * will handle both the slowpath and fastpath.
         */
        if ((rc = bus_setup_intr(sc->dev, sc->intr[0].resource,
                                 (INTR_TYPE_NET | INTR_MPSAFE),
                                 NULL, bxe_intr_legacy, sc,
                                 &sc->intr[0].tag)) != 0) {
            BLOGE(sc, "Failed to allocate INTx interrupt (%d)\n", rc);
            goto bxe_interrupt_attach_exit;
        }
    }

bxe_interrupt_attach_exit:

    return (rc);
}

static int  bxe_init_hw_common_chip(struct bxe_softc *sc);
static int  bxe_init_hw_common(struct bxe_softc *sc);
static int  bxe_init_hw_port(struct bxe_softc *sc);
static int  bxe_init_hw_func(struct bxe_softc *sc);
static void bxe_reset_common(struct bxe_softc *sc);
static void bxe_reset_port(struct bxe_softc *sc);
static void bxe_reset_func(struct bxe_softc *sc);
static int  bxe_gunzip_init(struct bxe_softc *sc);
static void bxe_gunzip_end(struct bxe_softc *sc);
static int  bxe_init_firmware(struct bxe_softc *sc);
static void bxe_release_firmware(struct bxe_softc *sc);

static struct
ecore_func_sp_drv_ops bxe_func_sp_drv = {
    .init_hw_cmn_chip = bxe_init_hw_common_chip,
    .init_hw_cmn      = bxe_init_hw_common,
    .init_hw_port     = bxe_init_hw_port,
    .init_hw_func     = bxe_init_hw_func,

    .reset_hw_cmn     = bxe_reset_common,
    .reset_hw_port    = bxe_reset_port,
    .reset_hw_func    = bxe_reset_func,

    .gunzip_init      = bxe_gunzip_init,
    .gunzip_end       = bxe_gunzip_end,

    .init_fw          = bxe_init_firmware,
    .release_fw       = bxe_release_firmware,
};

static void
bxe_init_func_obj(struct bxe_softc *sc)
{
    sc->dmae_ready = 0;

    ecore_init_func_obj(sc,
                        &sc->func_obj,
                        BXE_SP(sc, func_rdata),
                        BXE_SP_MAPPING(sc, func_rdata),
                        BXE_SP(sc, func_afex_rdata),
                        BXE_SP_MAPPING(sc, func_afex_rdata),
                        &bxe_func_sp_drv);
}

static int
bxe_init_hw(struct bxe_softc *sc,
            uint32_t         load_code)
{
    struct ecore_func_state_params func_params = { NULL };
    int rc;

    /* prepare the parameters for function state transitions */
    bit_set(&func_params.ramrod_flags, RAMROD_COMP_WAIT);

    func_params.f_obj = &sc->func_obj;
    func_params.cmd = ECORE_F_CMD_HW_INIT;

    func_params.params.hw_init.load_phase = load_code;

    /*
     * Via a plethora of function pointers, we will eventually reach
     * bxe_init_hw_common(), bxe_init_hw_port(), or bxe_init_hw_func().
     */
    rc = ecore_func_state_change(sc, &func_params);

    return (rc);
}

static void
bxe_fill(struct bxe_softc *sc,
         uint32_t         addr,
         int              fill,
         uint32_t         len)
{
    uint32_t i;

    if (!(len % 4) && !(addr % 4)) {
        for (i = 0; i < len; i += 4) {
            REG_WR(sc, (addr + i), fill);
        }
    } else {
        for (i = 0; i < len; i++) {
            REG_WR8(sc, (addr + i), fill);
        }
    }
}

/* writes FP SP data to FW - data_size in dwords */
static void
bxe_wr_fp_sb_data(struct bxe_softc *sc,
                  int              fw_sb_id,
                  uint32_t         *sb_data_p,
                  uint32_t         data_size)
{
    int index;

    for (index = 0; index < data_size; index++) {
        REG_WR(sc,
               (BAR_CSTRORM_INTMEM +
                CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) +
                (sizeof(uint32_t) * index)),
               *(sb_data_p + index));
    }
}

static void
bxe_zero_fp_sb(struct bxe_softc *sc,
               int              fw_sb_id)
{
    struct hc_status_block_data_e2 sb_data_e2;
    struct hc_status_block_data_e1x sb_data_e1x;
    uint32_t *sb_data_p;
    uint32_t data_size = 0;

    if (!CHIP_IS_E1x(sc)) {
        memset(&sb_data_e2, 0, sizeof(struct hc_status_block_data_e2));
        sb_data_e2.common.state = SB_DISABLED;
        sb_data_e2.common.p_func.vf_valid = FALSE;
        sb_data_p = (uint32_t *)&sb_data_e2;
        data_size = (sizeof(struct hc_status_block_data_e2) /
                     sizeof(uint32_t));
    } else {
        memset(&sb_data_e1x, 0, sizeof(struct hc_status_block_data_e1x));
        sb_data_e1x.common.state = SB_DISABLED;
        sb_data_e1x.common.p_func.vf_valid = FALSE;
        sb_data_p = (uint32_t *)&sb_data_e1x;
        data_size = (sizeof(struct hc_status_block_data_e1x) /
                     sizeof(uint32_t));
    }

    bxe_wr_fp_sb_data(sc, fw_sb_id, sb_data_p, data_size);

    bxe_fill(sc, (BAR_CSTRORM_INTMEM + CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id)),
             0, CSTORM_STATUS_BLOCK_SIZE);
    bxe_fill(sc, (BAR_CSTRORM_INTMEM + CSTORM_SYNC_BLOCK_OFFSET(fw_sb_id)),
             0, CSTORM_SYNC_BLOCK_SIZE);
}

static void
bxe_wr_sp_sb_data(struct bxe_softc               *sc,
                  struct hc_sp_status_block_data *sp_sb_data)
{
    int i;

    for (i = 0;
         i < (sizeof(struct hc_sp_status_block_data) / sizeof(uint32_t));
         i++) {
        REG_WR(sc,
               (BAR_CSTRORM_INTMEM +
                CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(SC_FUNC(sc)) +
                (i * sizeof(uint32_t))),
               *((uint32_t *)sp_sb_data + i));
    }
}

static void
bxe_zero_sp_sb(struct bxe_softc *sc)
{
    struct hc_sp_status_block_data sp_sb_data;

    memset(&sp_sb_data, 0, sizeof(struct hc_sp_status_block_data));

    sp_sb_data.state           = SB_DISABLED;
    sp_sb_data.p_func.vf_valid = FALSE;

    bxe_wr_sp_sb_data(sc, &sp_sb_data);

    bxe_fill(sc,
             (BAR_CSTRORM_INTMEM +
              CSTORM_SP_STATUS_BLOCK_OFFSET(SC_FUNC(sc))),
              0, CSTORM_SP_STATUS_BLOCK_SIZE);
    bxe_fill(sc,
             (BAR_CSTRORM_INTMEM +
              CSTORM_SP_SYNC_BLOCK_OFFSET(SC_FUNC(sc))),
              0, CSTORM_SP_SYNC_BLOCK_SIZE);
}

static void
bxe_setup_ndsb_state_machine(struct hc_status_block_sm *hc_sm,
                             int                       igu_sb_id,
                             int                       igu_seg_id)
{
    hc_sm->igu_sb_id      = igu_sb_id;
    hc_sm->igu_seg_id     = igu_seg_id;
    hc_sm->timer_value    = 0xFF;
    hc_sm->time_to_expire = 0xFFFFFFFF;
}

static void
bxe_map_sb_state_machines(struct hc_index_data *index_data)
{
    /* zero out state machine indices */

    /* rx indices */
    index_data[HC_INDEX_ETH_RX_CQ_CONS].flags &= ~HC_INDEX_DATA_SM_ID;

    /* tx indices */
    index_data[HC_INDEX_OOO_TX_CQ_CONS].flags      &= ~HC_INDEX_DATA_SM_ID;
    index_data[HC_INDEX_ETH_TX_CQ_CONS_COS0].flags &= ~HC_INDEX_DATA_SM_ID;
    index_data[HC_INDEX_ETH_TX_CQ_CONS_COS1].flags &= ~HC_INDEX_DATA_SM_ID;
    index_data[HC_INDEX_ETH_TX_CQ_CONS_COS2].flags &= ~HC_INDEX_DATA_SM_ID;

    /* map indices */

    /* rx indices */
    index_data[HC_INDEX_ETH_RX_CQ_CONS].flags |=
        (SM_RX_ID << HC_INDEX_DATA_SM_ID_SHIFT);

    /* tx indices */
    index_data[HC_INDEX_OOO_TX_CQ_CONS].flags |=
        (SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT);
    index_data[HC_INDEX_ETH_TX_CQ_CONS_COS0].flags |=
        (SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT);
    index_data[HC_INDEX_ETH_TX_CQ_CONS_COS1].flags |=
        (SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT);
    index_data[HC_INDEX_ETH_TX_CQ_CONS_COS2].flags |=
        (SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT);
}

static void
bxe_init_sb(struct bxe_softc *sc,
            bus_addr_t       busaddr,
            int              vfid,
            uint8_t          vf_valid,
            int              fw_sb_id,
            int              igu_sb_id)
{
    struct hc_status_block_data_e2  sb_data_e2;
    struct hc_status_block_data_e1x sb_data_e1x;
    struct hc_status_block_sm       *hc_sm_p;
    uint32_t *sb_data_p;
    int igu_seg_id;
    int data_size;

    if (CHIP_INT_MODE_IS_BC(sc)) {
        igu_seg_id = HC_SEG_ACCESS_NORM;
    } else {
        igu_seg_id = IGU_SEG_ACCESS_NORM;
    }

    bxe_zero_fp_sb(sc, fw_sb_id);

    if (!CHIP_IS_E1x(sc)) {
        memset(&sb_data_e2, 0, sizeof(struct hc_status_block_data_e2));
        sb_data_e2.common.state = SB_ENABLED;
        sb_data_e2.common.p_func.pf_id = SC_FUNC(sc);
        sb_data_e2.common.p_func.vf_id = vfid;
        sb_data_e2.common.p_func.vf_valid = vf_valid;
        sb_data_e2.common.p_func.vnic_id = SC_VN(sc);
        sb_data_e2.common.same_igu_sb_1b = TRUE;
        sb_data_e2.common.host_sb_addr.hi = U64_HI(busaddr);
        sb_data_e2.common.host_sb_addr.lo = U64_LO(busaddr);
        hc_sm_p = sb_data_e2.common.state_machine;
        sb_data_p = (uint32_t *)&sb_data_e2;
        data_size = (sizeof(struct hc_status_block_data_e2) /
                     sizeof(uint32_t));
        bxe_map_sb_state_machines(sb_data_e2.index_data);
    } else {
        memset(&sb_data_e1x, 0, sizeof(struct hc_status_block_data_e1x));
        sb_data_e1x.common.state = SB_ENABLED;
        sb_data_e1x.common.p_func.pf_id = SC_FUNC(sc);
        sb_data_e1x.common.p_func.vf_id = 0xff;
        sb_data_e1x.common.p_func.vf_valid = FALSE;
        sb_data_e1x.common.p_func.vnic_id = SC_VN(sc);
        sb_data_e1x.common.same_igu_sb_1b = TRUE;
        sb_data_e1x.common.host_sb_addr.hi = U64_HI(busaddr);
        sb_data_e1x.common.host_sb_addr.lo = U64_LO(busaddr);
        hc_sm_p = sb_data_e1x.common.state_machine;
        sb_data_p = (uint32_t *)&sb_data_e1x;
        data_size = (sizeof(struct hc_status_block_data_e1x) /
                     sizeof(uint32_t));
        bxe_map_sb_state_machines(sb_data_e1x.index_data);
    }

    bxe_setup_ndsb_state_machine(&hc_sm_p[SM_RX_ID], igu_sb_id, igu_seg_id);
    bxe_setup_ndsb_state_machine(&hc_sm_p[SM_TX_ID], igu_sb_id, igu_seg_id);

    BLOGD(sc, DBG_LOAD, "Init FW SB %d\n", fw_sb_id);

    /* write indices to HW - PCI guarantees endianity of regpairs */
    bxe_wr_fp_sb_data(sc, fw_sb_id, sb_data_p, data_size);
}

static inline uint8_t
bxe_fp_qzone_id(struct bxe_fastpath *fp)
{
    if (CHIP_IS_E1x(fp->sc)) {
        return (fp->cl_id + SC_PORT(fp->sc) * ETH_MAX_RX_CLIENTS_E1H);
    } else {
        return (fp->cl_id);
    }
}

static inline uint32_t
bxe_rx_ustorm_prods_offset(struct bxe_softc    *sc,
                           struct bxe_fastpath *fp)
{
    uint32_t offset = BAR_USTRORM_INTMEM;

    if (!CHIP_IS_E1x(sc)) {
        offset += USTORM_RX_PRODS_E2_OFFSET(fp->cl_qzone_id);
    } else {
        offset += USTORM_RX_PRODS_E1X_OFFSET(SC_PORT(sc), fp->cl_id);
    }

    return (offset);
}

static void
bxe_init_eth_fp(struct bxe_softc *sc,
                int              idx)
{
    struct bxe_fastpath *fp = &sc->fp[idx];
    uint32_t cids[ECORE_MULTI_TX_COS] = { 0 };
    unsigned long q_type = 0;
    int cos;

    fp->sc    = sc;
    fp->index = idx;

    fp->igu_sb_id = (sc->igu_base_sb + idx + CNIC_SUPPORT(sc));
    fp->fw_sb_id = (sc->base_fw_ndsb + idx + CNIC_SUPPORT(sc));

    fp->cl_id = (CHIP_IS_E1x(sc)) ?
                    (SC_L_ID(sc) + idx) :
                    /* want client ID same as IGU SB ID for non-E1 */
                    fp->igu_sb_id;
    fp->cl_qzone_id = bxe_fp_qzone_id(fp);

    /* setup sb indices */
    if (!CHIP_IS_E1x(sc)) {
        fp->sb_index_values  = fp->status_block.e2_sb->sb.index_values;
        fp->sb_running_index = fp->status_block.e2_sb->sb.running_index;
    } else {
        fp->sb_index_values  = fp->status_block.e1x_sb->sb.index_values;
        fp->sb_running_index = fp->status_block.e1x_sb->sb.running_index;
    }

    /* init shortcut */
    fp->ustorm_rx_prods_offset = bxe_rx_ustorm_prods_offset(sc, fp);

    fp->rx_cq_cons_sb = &fp->sb_index_values[HC_INDEX_ETH_RX_CQ_CONS];

    /*
     * XXX If multiple CoS is ever supported then each fastpath structure
     * will need to maintain tx producer/consumer/dma/etc values *per* CoS.
     */
    for (cos = 0; cos < sc->max_cos; cos++) {
        cids[cos] = idx;
    }
    fp->tx_cons_sb = &fp->sb_index_values[HC_INDEX_ETH_TX_CQ_CONS_COS0];

    /* nothing more for a VF to do */
    if (IS_VF(sc)) {
        return;
    }

    bxe_init_sb(sc, fp->sb_dma.paddr, BXE_VF_ID_INVALID, FALSE,
                fp->fw_sb_id, fp->igu_sb_id);

    bxe_update_fp_sb_idx(fp);

    /* Configure Queue State object */
    bit_set(&q_type, ECORE_Q_TYPE_HAS_RX);
    bit_set(&q_type, ECORE_Q_TYPE_HAS_TX);

    ecore_init_queue_obj(sc,
                         &sc->sp_objs[idx].q_obj,
                         fp->cl_id,
                         cids,
                         sc->max_cos,
                         SC_FUNC(sc),
                         BXE_SP(sc, q_rdata),
                         BXE_SP_MAPPING(sc, q_rdata),
                         q_type);

    /* configure classification DBs */
    ecore_init_mac_obj(sc,
                       &sc->sp_objs[idx].mac_obj,
                       fp->cl_id,
                       idx,
                       SC_FUNC(sc),
                       BXE_SP(sc, mac_rdata),
                       BXE_SP_MAPPING(sc, mac_rdata),
                       ECORE_FILTER_MAC_PENDING,
                       &sc->sp_state,
                       ECORE_OBJ_TYPE_RX_TX,
                       &sc->macs_pool);

    BLOGD(sc, DBG_LOAD, "fp[%d]: sb=%p cl_id=%d fw_sb=%d igu_sb=%d\n",
          idx, fp->status_block.e2_sb, fp->cl_id, fp->fw_sb_id, fp->igu_sb_id);
}

static inline void
bxe_update_rx_prod(struct bxe_softc    *sc,
                   struct bxe_fastpath *fp,
                   uint16_t            rx_bd_prod,
                   uint16_t            rx_cq_prod,
                   uint16_t            rx_sge_prod)
{
    struct ustorm_eth_rx_producers rx_prods = { 0 };
    uint32_t i;

    /* update producers */
    rx_prods.bd_prod  = rx_bd_prod;
    rx_prods.cqe_prod = rx_cq_prod;
    rx_prods.sge_prod = rx_sge_prod;

    /*
     * Make sure that the BD and SGE data is updated before updating the
     * producers since FW might read the BD/SGE right after the producer
     * is updated.
     * This is only applicable for weak-ordered memory model archs such
     * as IA-64. The following barrier is also mandatory since FW will
     * assumes BDs must have buffers.
     */
    wmb();

    for (i = 0; i < (sizeof(rx_prods) / 4); i++) {
        REG_WR(sc,
               (fp->ustorm_rx_prods_offset + (i * 4)),
               ((uint32_t *)&rx_prods)[i]);
    }

    wmb(); /* keep prod updates ordered */

    BLOGD(sc, DBG_RX,
          "RX fp[%d]: wrote prods bd_prod=%u cqe_prod=%u sge_prod=%u\n",
          fp->index, rx_bd_prod, rx_cq_prod, rx_sge_prod);
}

static void
bxe_init_rx_rings(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int i;

    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];

        fp->rx_bd_cons = 0;

        /*
         * Activate the BD ring...
         * Warning, this will generate an interrupt (to the TSTORM)
         * so this can only be done after the chip is initialized
         */
        bxe_update_rx_prod(sc, fp,
                           fp->rx_bd_prod,
                           fp->rx_cq_prod,
                           fp->rx_sge_prod);

        if (i != 0) {
            continue;
        }

        if (CHIP_IS_E1(sc)) {
            REG_WR(sc,
                   (BAR_USTRORM_INTMEM +
                    USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(SC_FUNC(sc))),
                   U64_LO(fp->rcq_dma.paddr));
            REG_WR(sc,
                   (BAR_USTRORM_INTMEM +
                    USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(SC_FUNC(sc)) + 4),
                   U64_HI(fp->rcq_dma.paddr));
        }
    }
}

static void
bxe_init_tx_ring_one(struct bxe_fastpath *fp)
{
    SET_FLAG(fp->tx_db.data.header.data, DOORBELL_HDR_T_DB_TYPE, 1);
    fp->tx_db.data.zero_fill1 = 0;
    fp->tx_db.data.prod = 0;

    fp->tx_pkt_prod = 0;
    fp->tx_pkt_cons = 0;
    fp->tx_bd_prod = 0;
    fp->tx_bd_cons = 0;
    fp->eth_q_stats.tx_pkts = 0;
}

static inline void
bxe_init_tx_rings(struct bxe_softc *sc)
{
    int i;

    for (i = 0; i < sc->num_queues; i++) {
        bxe_init_tx_ring_one(&sc->fp[i]);
    }
}

static void
bxe_init_def_sb(struct bxe_softc *sc)
{
    struct host_sp_status_block *def_sb = sc->def_sb;
    bus_addr_t mapping = sc->def_sb_dma.paddr;
    int igu_sp_sb_index;
    int igu_seg_id;
    int port = SC_PORT(sc);
    int func = SC_FUNC(sc);
    int reg_offset, reg_offset_en5;
    uint64_t section;
    int index, sindex;
    struct hc_sp_status_block_data sp_sb_data;

    memset(&sp_sb_data, 0, sizeof(struct hc_sp_status_block_data));

    if (CHIP_INT_MODE_IS_BC(sc)) {
        igu_sp_sb_index = DEF_SB_IGU_ID;
        igu_seg_id = HC_SEG_ACCESS_DEF;
    } else {
        igu_sp_sb_index = sc->igu_dsb_id;
        igu_seg_id = IGU_SEG_ACCESS_DEF;
    }

    /* attentions */
    section = ((uint64_t)mapping +
               offsetof(struct host_sp_status_block, atten_status_block));
    def_sb->atten_status_block.status_block_id = igu_sp_sb_index;
    sc->attn_state = 0;

    reg_offset = (port) ?
                     MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
                     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0;
    reg_offset_en5 = (port) ?
                         MISC_REG_AEU_ENABLE5_FUNC_1_OUT_0 :
                         MISC_REG_AEU_ENABLE5_FUNC_0_OUT_0;

    for (index = 0; index < MAX_DYNAMIC_ATTN_GRPS; index++) {
        /* take care of sig[0]..sig[4] */
        for (sindex = 0; sindex < 4; sindex++) {
            sc->attn_group[index].sig[sindex] =
                REG_RD(sc, (reg_offset + (sindex * 0x4) + (0x10 * index)));
        }

        if (!CHIP_IS_E1x(sc)) {
            /*
             * enable5 is separate from the rest of the registers,
             * and the address skip is 4 and not 16 between the
             * different groups
             */
            sc->attn_group[index].sig[4] =
                REG_RD(sc, (reg_offset_en5 + (0x4 * index)));
        } else {
            sc->attn_group[index].sig[4] = 0;
        }
    }

    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        reg_offset = (port) ?
                         HC_REG_ATTN_MSG1_ADDR_L :
                         HC_REG_ATTN_MSG0_ADDR_L;
        REG_WR(sc, reg_offset, U64_LO(section));
        REG_WR(sc, (reg_offset + 4), U64_HI(section));
    } else if (!CHIP_IS_E1x(sc)) {
        REG_WR(sc, IGU_REG_ATTN_MSG_ADDR_L, U64_LO(section));
        REG_WR(sc, IGU_REG_ATTN_MSG_ADDR_H, U64_HI(section));
    }

    section = ((uint64_t)mapping +
               offsetof(struct host_sp_status_block, sp_sb));

    bxe_zero_sp_sb(sc);

    /* PCI guarantees endianity of regpair */
    sp_sb_data.state           = SB_ENABLED;
    sp_sb_data.host_sb_addr.lo = U64_LO(section);
    sp_sb_data.host_sb_addr.hi = U64_HI(section);
    sp_sb_data.igu_sb_id       = igu_sp_sb_index;
    sp_sb_data.igu_seg_id      = igu_seg_id;
    sp_sb_data.p_func.pf_id    = func;
    sp_sb_data.p_func.vnic_id  = SC_VN(sc);
    sp_sb_data.p_func.vf_id    = 0xff;

    bxe_wr_sp_sb_data(sc, &sp_sb_data);

    bxe_ack_sb(sc, sc->igu_dsb_id, USTORM_ID, 0, IGU_INT_ENABLE, 0);
}

static void
bxe_init_sp_ring(struct bxe_softc *sc)
{
    atomic_store_rel_long(&sc->cq_spq_left, MAX_SPQ_PENDING);
    sc->spq_prod_idx = 0;
    sc->dsb_sp_prod = &sc->def_sb->sp_sb.index_values[HC_SP_INDEX_ETH_DEF_CONS];
    sc->spq_prod_bd = sc->spq;
    sc->spq_last_bd = (sc->spq_prod_bd + MAX_SP_DESC_CNT);
}

static void
bxe_init_eq_ring(struct bxe_softc *sc)
{
    union event_ring_elem *elem;
    int i;

    for (i = 1; i <= NUM_EQ_PAGES; i++) {
        elem = &sc->eq[EQ_DESC_CNT_PAGE * i - 1];

        elem->next_page.addr.hi = htole32(U64_HI(sc->eq_dma.paddr +
                                                 BCM_PAGE_SIZE *
                                                 (i % NUM_EQ_PAGES)));
        elem->next_page.addr.lo = htole32(U64_LO(sc->eq_dma.paddr +
                                                 BCM_PAGE_SIZE *
                                                 (i % NUM_EQ_PAGES)));
    }

    sc->eq_cons    = 0;
    sc->eq_prod    = NUM_EQ_DESC;
    sc->eq_cons_sb = &sc->def_sb->sp_sb.index_values[HC_SP_INDEX_EQ_CONS];

    atomic_store_rel_long(&sc->eq_spq_left,
                          (min((MAX_SP_DESC_CNT - MAX_SPQ_PENDING),
                               NUM_EQ_DESC) - 1));
}

static void
bxe_init_internal_common(struct bxe_softc *sc)
{
    int i;

    /*
     * Zero this manually as its initialization is currently missing
     * in the initTool.
     */
    for (i = 0; i < (USTORM_AGG_DATA_SIZE >> 2); i++) {
        REG_WR(sc,
               (BAR_USTRORM_INTMEM + USTORM_AGG_DATA_OFFSET + (i * 4)),
               0);
    }

    if (!CHIP_IS_E1x(sc)) {
        REG_WR8(sc, (BAR_CSTRORM_INTMEM + CSTORM_IGU_MODE_OFFSET),
                CHIP_INT_MODE_IS_BC(sc) ? HC_IGU_BC_MODE : HC_IGU_NBC_MODE);
    }
}

static void
bxe_init_internal(struct bxe_softc *sc,
                  uint32_t         load_code)
{
    switch (load_code) {
    case FW_MSG_CODE_DRV_LOAD_COMMON:
    case FW_MSG_CODE_DRV_LOAD_COMMON_CHIP:
        bxe_init_internal_common(sc);
        /* no break */

    case FW_MSG_CODE_DRV_LOAD_PORT:
        /* nothing to do */
        /* no break */

    case FW_MSG_CODE_DRV_LOAD_FUNCTION:
        /* internal memory per function is initialized inside bxe_pf_init */
        break;

    default:
        BLOGE(sc, "Unknown load_code (0x%x) from MCP\n", load_code);
        break;
    }
}

static void
storm_memset_func_cfg(struct bxe_softc                         *sc,
                      struct tstorm_eth_function_common_config *tcfg,
                      uint16_t                                  abs_fid)
{
    uint32_t addr;
    size_t size;

    addr = (BAR_TSTRORM_INTMEM +
            TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(abs_fid));
    size = sizeof(struct tstorm_eth_function_common_config);
    ecore_storm_memset_struct(sc, addr, size, (uint32_t *)tcfg);
}

static void
bxe_func_init(struct bxe_softc            *sc,
              struct bxe_func_init_params *p)
{
    struct tstorm_eth_function_common_config tcfg = { 0 };

    if (CHIP_IS_E1x(sc)) {
        storm_memset_func_cfg(sc, &tcfg, p->func_id);
    }

    /* Enable the function in the FW */
    storm_memset_vf_to_pf(sc, p->func_id, p->pf_id);
    storm_memset_func_en(sc, p->func_id, 1);

    /* spq */
    if (p->func_flgs & FUNC_FLG_SPQ) {
        storm_memset_spq_addr(sc, p->spq_map, p->func_id);
        REG_WR(sc,
               (XSEM_REG_FAST_MEMORY + XSTORM_SPQ_PROD_OFFSET(p->func_id)),
               p->spq_prod);
    }
}

/*
 * Calculates the sum of vn_min_rates.
 * It's needed for further normalizing of the min_rates.
 * Returns:
 *   sum of vn_min_rates.
 *     or
 *   0 - if all the min_rates are 0.
 * In the later case fainess algorithm should be deactivated.
 * If all min rates are not zero then those that are zeroes will be set to 1.
 */
static void
bxe_calc_vn_min(struct bxe_softc       *sc,
                struct cmng_init_input *input)
{
    uint32_t vn_cfg;
    uint32_t vn_min_rate;
    int all_zero = 1;
    int vn;

    for (vn = VN_0; vn < SC_MAX_VN_NUM(sc); vn++) {
        vn_cfg = sc->devinfo.mf_info.mf_config[vn];
        vn_min_rate = (((vn_cfg & FUNC_MF_CFG_MIN_BW_MASK) >>
                        FUNC_MF_CFG_MIN_BW_SHIFT) * 100);

        if (vn_cfg & FUNC_MF_CFG_FUNC_HIDE) {
            /* skip hidden VNs */
            vn_min_rate = 0;
        } else if (!vn_min_rate) {
            /* If min rate is zero - set it to 100 */
            vn_min_rate = DEF_MIN_RATE;
        } else {
            all_zero = 0;
        }

        input->vnic_min_rate[vn] = vn_min_rate;
    }

    /* if ETS or all min rates are zeros - disable fairness */
    if (BXE_IS_ETS_ENABLED(sc)) {
        input->flags.cmng_enables &= ~CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
        BLOGD(sc, DBG_LOAD, "Fairness disabled (ETS)\n");
    } else if (all_zero) {
        input->flags.cmng_enables &= ~CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
        BLOGD(sc, DBG_LOAD,
              "Fariness disabled (all MIN values are zeroes)\n");
    } else {
        input->flags.cmng_enables |= CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
    }
}

static inline uint16_t
bxe_extract_max_cfg(struct bxe_softc *sc,
                    uint32_t         mf_cfg)
{
    uint16_t max_cfg = ((mf_cfg & FUNC_MF_CFG_MAX_BW_MASK) >>
                        FUNC_MF_CFG_MAX_BW_SHIFT);

    if (!max_cfg) {
        BLOGD(sc, DBG_LOAD, "Max BW configured to 0 - using 100 instead\n");
        max_cfg = 100;
    }

    return (max_cfg);
}

static void
bxe_calc_vn_max(struct bxe_softc       *sc,
                int                    vn,
                struct cmng_init_input *input)
{
    uint16_t vn_max_rate;
    uint32_t vn_cfg = sc->devinfo.mf_info.mf_config[vn];
    uint32_t max_cfg;

    if (vn_cfg & FUNC_MF_CFG_FUNC_HIDE) {
        vn_max_rate = 0;
    } else {
        max_cfg = bxe_extract_max_cfg(sc, vn_cfg);

        if (IS_MF_SI(sc)) {
            /* max_cfg in percents of linkspeed */
            vn_max_rate = ((sc->link_vars.line_speed * max_cfg) / 100);
        } else { /* SD modes */
            /* max_cfg is absolute in 100Mb units */
            vn_max_rate = (max_cfg * 100);
        }
    }

    BLOGD(sc, DBG_LOAD, "vn %d: vn_max_rate %d\n", vn, vn_max_rate);

    input->vnic_max_rate[vn] = vn_max_rate;
}

static void
bxe_cmng_fns_init(struct bxe_softc *sc,
                  uint8_t          read_cfg,
                  uint8_t          cmng_type)
{
    struct cmng_init_input input;
    int vn;

    memset(&input, 0, sizeof(struct cmng_init_input));

    input.port_rate = sc->link_vars.line_speed;

    if (cmng_type == CMNG_FNS_MINMAX) {
        /* read mf conf from shmem */
        if (read_cfg) {
            bxe_read_mf_cfg(sc);
        }

        /* get VN min rate and enable fairness if not 0 */
        bxe_calc_vn_min(sc, &input);

        /* get VN max rate */
        if (sc->port.pmf) {
            for (vn = VN_0; vn < SC_MAX_VN_NUM(sc); vn++) {
                bxe_calc_vn_max(sc, vn, &input);
            }
        }

        /* always enable rate shaping and fairness */
        input.flags.cmng_enables |= CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN;

        ecore_init_cmng(&input, &sc->cmng);
        return;
    }

    /* rate shaping and fairness are disabled */
    BLOGD(sc, DBG_LOAD, "rate shaping and fairness have been disabled\n");
}

static int
bxe_get_cmng_fns_mode(struct bxe_softc *sc)
{
    if (CHIP_REV_IS_SLOW(sc)) {
        return (CMNG_FNS_NONE);
    }

    if (IS_MF(sc)) {
        return (CMNG_FNS_MINMAX);
    }

    return (CMNG_FNS_NONE);
}

static void
storm_memset_cmng(struct bxe_softc *sc,
                  struct cmng_init *cmng,
                  uint8_t          port)
{
    int vn;
    int func;
    uint32_t addr;
    size_t size;

    addr = (BAR_XSTRORM_INTMEM +
            XSTORM_CMNG_PER_PORT_VARS_OFFSET(port));
    size = sizeof(struct cmng_struct_per_port);
    ecore_storm_memset_struct(sc, addr, size, (uint32_t *)&cmng->port);

    for (vn = VN_0; vn < SC_MAX_VN_NUM(sc); vn++) {
        func = func_by_vn(sc, vn);

        addr = (BAR_XSTRORM_INTMEM +
                XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(func));
        size = sizeof(struct rate_shaping_vars_per_vn);
        ecore_storm_memset_struct(sc, addr, size,
                                  (uint32_t *)&cmng->vnic.vnic_max_rate[vn]);

        addr = (BAR_XSTRORM_INTMEM +
                XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(func));
        size = sizeof(struct fairness_vars_per_vn);
        ecore_storm_memset_struct(sc, addr, size,
                                  (uint32_t *)&cmng->vnic.vnic_min_rate[vn]);
    }
}

static void
bxe_pf_init(struct bxe_softc *sc)
{
    struct bxe_func_init_params func_init = { 0 };
    struct event_ring_data eq_data = { { 0 } };
    uint16_t flags;

    if (!CHIP_IS_E1x(sc)) {
        /* reset IGU PF statistics: MSIX + ATTN */
        /* PF */
        REG_WR(sc,
               (IGU_REG_STATISTIC_NUM_MESSAGE_SENT +
                (BXE_IGU_STAS_MSG_VF_CNT * 4) +
                ((CHIP_IS_MODE_4_PORT(sc) ? SC_FUNC(sc) : SC_VN(sc)) * 4)),
               0);
        /* ATTN */
        REG_WR(sc,
               (IGU_REG_STATISTIC_NUM_MESSAGE_SENT +
                (BXE_IGU_STAS_MSG_VF_CNT * 4) +
                (BXE_IGU_STAS_MSG_PF_CNT * 4) +
                ((CHIP_IS_MODE_4_PORT(sc) ? SC_FUNC(sc) : SC_VN(sc)) * 4)),
               0);
    }

    /* function setup flags */
    flags = (FUNC_FLG_STATS | FUNC_FLG_LEADING | FUNC_FLG_SPQ);

    /*
     * This flag is relevant for E1x only.
     * E2 doesn't have a TPA configuration in a function level.
     */
    flags |= (if_getcapenable(sc->ifp) & IFCAP_LRO) ? FUNC_FLG_TPA : 0;

    func_init.func_flgs = flags;
    func_init.pf_id     = SC_FUNC(sc);
    func_init.func_id   = SC_FUNC(sc);
    func_init.spq_map   = sc->spq_dma.paddr;
    func_init.spq_prod  = sc->spq_prod_idx;

    bxe_func_init(sc, &func_init);

    memset(&sc->cmng, 0, sizeof(struct cmng_struct_per_port));

    /*
     * Congestion management values depend on the link rate.
     * There is no active link so initial link rate is set to 10Gbps.
     * When the link comes up the congestion management values are
     * re-calculated according to the actual link rate.
     */
    sc->link_vars.line_speed = SPEED_10000;
    bxe_cmng_fns_init(sc, TRUE, bxe_get_cmng_fns_mode(sc));

    /* Only the PMF sets the HW */
    if (sc->port.pmf) {
        storm_memset_cmng(sc, &sc->cmng, SC_PORT(sc));
    }

    /* init Event Queue - PCI bus guarantees correct endainity */
    eq_data.base_addr.hi = U64_HI(sc->eq_dma.paddr);
    eq_data.base_addr.lo = U64_LO(sc->eq_dma.paddr);
    eq_data.producer     = sc->eq_prod;
    eq_data.index_id     = HC_SP_INDEX_EQ_CONS;
    eq_data.sb_id        = DEF_SB_ID;
    storm_memset_eq_data(sc, &eq_data, SC_FUNC(sc));
}

static void
bxe_hc_int_enable(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    uint32_t addr = (port) ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
    uint32_t val = REG_RD(sc, addr);
    uint8_t msix = (sc->interrupt_mode == INTR_MODE_MSIX) ? TRUE : FALSE;
    uint8_t single_msix = ((sc->interrupt_mode == INTR_MODE_MSIX) &&
                           (sc->intr_count == 1)) ? TRUE : FALSE;
    uint8_t msi = (sc->interrupt_mode == INTR_MODE_MSI) ? TRUE : FALSE;

    if (msix) {
        val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
                 HC_CONFIG_0_REG_INT_LINE_EN_0);
        val |= (HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
                HC_CONFIG_0_REG_ATTN_BIT_EN_0);
        if (single_msix) {
            val |= HC_CONFIG_0_REG_SINGLE_ISR_EN_0;
        }
    } else if (msi) {
        val &= ~HC_CONFIG_0_REG_INT_LINE_EN_0;
        val |= (HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
                HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
                HC_CONFIG_0_REG_ATTN_BIT_EN_0);
    } else {
        val |= (HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
                HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
                HC_CONFIG_0_REG_INT_LINE_EN_0 |
                HC_CONFIG_0_REG_ATTN_BIT_EN_0);

        if (!CHIP_IS_E1(sc)) {
            BLOGD(sc, DBG_INTR, "write %x to HC %d (addr 0x%x)\n",
                  val, port, addr);

            REG_WR(sc, addr, val);

            val &= ~HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0;
        }
    }

    if (CHIP_IS_E1(sc)) {
        REG_WR(sc, (HC_REG_INT_MASK + port*4), 0x1FFFF);
    }

    BLOGD(sc, DBG_INTR, "write %x to HC %d (addr 0x%x) mode %s\n",
          val, port, addr, ((msix) ? "MSI-X" : ((msi) ? "MSI" : "INTx")));

    REG_WR(sc, addr, val);

    /* ensure that HC_CONFIG is written before leading/trailing edge config */
    mb();

    if (!CHIP_IS_E1(sc)) {
        /* init leading/trailing edge */
        if (IS_MF(sc)) {
            val = (0xee0f | (1 << (SC_VN(sc) + 4)));
            if (sc->port.pmf) {
                /* enable nig and gpio3 attention */
                val |= 0x1100;
            }
        } else {
            val = 0xffff;
        }

        REG_WR(sc, (HC_REG_TRAILING_EDGE_0 + port*8), val);
        REG_WR(sc, (HC_REG_LEADING_EDGE_0 + port*8), val);
    }

    /* make sure that interrupts are indeed enabled from here on */
    mb();
}

static void
bxe_igu_int_enable(struct bxe_softc *sc)
{
    uint32_t val;
    uint8_t msix = (sc->interrupt_mode == INTR_MODE_MSIX) ? TRUE : FALSE;
    uint8_t single_msix = ((sc->interrupt_mode == INTR_MODE_MSIX) &&
                           (sc->intr_count == 1)) ? TRUE : FALSE;
    uint8_t msi = (sc->interrupt_mode == INTR_MODE_MSI) ? TRUE : FALSE;

    val = REG_RD(sc, IGU_REG_PF_CONFIGURATION);

    if (msix) {
        val &= ~(IGU_PF_CONF_INT_LINE_EN |
                 IGU_PF_CONF_SINGLE_ISR_EN);
        val |= (IGU_PF_CONF_MSI_MSIX_EN |
                IGU_PF_CONF_ATTN_BIT_EN);
        if (single_msix) {
            val |= IGU_PF_CONF_SINGLE_ISR_EN;
        }
    } else if (msi) {
        val &= ~IGU_PF_CONF_INT_LINE_EN;
        val |= (IGU_PF_CONF_MSI_MSIX_EN |
                IGU_PF_CONF_ATTN_BIT_EN |
                IGU_PF_CONF_SINGLE_ISR_EN);
    } else {
        val &= ~IGU_PF_CONF_MSI_MSIX_EN;
        val |= (IGU_PF_CONF_INT_LINE_EN |
                IGU_PF_CONF_ATTN_BIT_EN |
                IGU_PF_CONF_SINGLE_ISR_EN);
    }

    /* clean previous status - need to configure igu prior to ack*/
    if ((!msix) || single_msix) {
        REG_WR(sc, IGU_REG_PF_CONFIGURATION, val);
        bxe_ack_int(sc);
    }

    val |= IGU_PF_CONF_FUNC_EN;

    BLOGD(sc, DBG_INTR, "write 0x%x to IGU mode %s\n",
          val, ((msix) ? "MSI-X" : ((msi) ? "MSI" : "INTx")));

    REG_WR(sc, IGU_REG_PF_CONFIGURATION, val);

    mb();

    /* init leading/trailing edge */
    if (IS_MF(sc)) {
        val = (0xee0f | (1 << (SC_VN(sc) + 4)));
        if (sc->port.pmf) {
            /* enable nig and gpio3 attention */
            val |= 0x1100;
        }
    } else {
        val = 0xffff;
    }

    REG_WR(sc, IGU_REG_TRAILING_EDGE_LATCH, val);
    REG_WR(sc, IGU_REG_LEADING_EDGE_LATCH, val);

    /* make sure that interrupts are indeed enabled from here on */
    mb();
}

static void
bxe_int_enable(struct bxe_softc *sc)
{
    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        bxe_hc_int_enable(sc);
    } else {
        bxe_igu_int_enable(sc);
    }
}

static void
bxe_hc_int_disable(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    uint32_t addr = (port) ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
    uint32_t val = REG_RD(sc, addr);

    /*
     * In E1 we must use only PCI configuration space to disable MSI/MSIX
     * capablility. It's forbidden to disable IGU_PF_CONF_MSI_MSIX_EN in HC
     * block
     */
    if (CHIP_IS_E1(sc)) {
        /*
         * Since IGU_PF_CONF_MSI_MSIX_EN still always on use mask register
         * to prevent from HC sending interrupts after we exit the function
         */
        REG_WR(sc, (HC_REG_INT_MASK + port*4), 0);

        val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
                 HC_CONFIG_0_REG_INT_LINE_EN_0 |
                 HC_CONFIG_0_REG_ATTN_BIT_EN_0);
    } else {
        val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
                 HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
                 HC_CONFIG_0_REG_INT_LINE_EN_0 |
                 HC_CONFIG_0_REG_ATTN_BIT_EN_0);
    }

    BLOGD(sc, DBG_INTR, "write %x to HC %d (addr 0x%x)\n", val, port, addr);

    /* flush all outstanding writes */
    mb();

    REG_WR(sc, addr, val);
    if (REG_RD(sc, addr) != val) {
        BLOGE(sc, "proper val not read from HC IGU!\n");
    }
}

static void
bxe_igu_int_disable(struct bxe_softc *sc)
{
    uint32_t val = REG_RD(sc, IGU_REG_PF_CONFIGURATION);

    val &= ~(IGU_PF_CONF_MSI_MSIX_EN |
             IGU_PF_CONF_INT_LINE_EN |
             IGU_PF_CONF_ATTN_BIT_EN);

    BLOGD(sc, DBG_INTR, "write %x to IGU\n", val);

    /* flush all outstanding writes */
    mb();

    REG_WR(sc, IGU_REG_PF_CONFIGURATION, val);
    if (REG_RD(sc, IGU_REG_PF_CONFIGURATION) != val) {
        BLOGE(sc, "proper val not read from IGU!\n");
    }
}

static void
bxe_int_disable(struct bxe_softc *sc)
{
    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        bxe_hc_int_disable(sc);
    } else {
        bxe_igu_int_disable(sc);
    }
}

static void
bxe_nic_init(struct bxe_softc *sc,
             int              load_code)
{
    int i;

    for (i = 0; i < sc->num_queues; i++) {
        bxe_init_eth_fp(sc, i);
    }

    rmb(); /* ensure status block indices were read */

    bxe_init_rx_rings(sc);
    bxe_init_tx_rings(sc);

    if (IS_VF(sc)) {
        return;
    }

    /* initialize MOD_ABS interrupts */
    elink_init_mod_abs_int(sc, &sc->link_vars,
                           sc->devinfo.chip_id,
                           sc->devinfo.shmem_base,
                           sc->devinfo.shmem2_base,
                           SC_PORT(sc));

    bxe_init_def_sb(sc);
    bxe_update_dsb_idx(sc);
    bxe_init_sp_ring(sc);
    bxe_init_eq_ring(sc);
    bxe_init_internal(sc, load_code);
    bxe_pf_init(sc);
    bxe_stats_init(sc);

    /* flush all before enabling interrupts */
    mb();

    bxe_int_enable(sc);

    /* check for SPIO5 */
    bxe_attn_int_deasserted0(sc,
                             REG_RD(sc,
                                    (MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 +
                                     SC_PORT(sc)*4)) &
                             AEU_INPUTS_ATTN_BITS_SPIO5);
}

static inline void
bxe_init_objs(struct bxe_softc *sc)
{
    /* mcast rules must be added to tx if tx switching is enabled */
    ecore_obj_type o_type =
        (sc->flags & BXE_TX_SWITCHING) ? ECORE_OBJ_TYPE_RX_TX :
                                         ECORE_OBJ_TYPE_RX;

    /* RX_MODE controlling object */
    ecore_init_rx_mode_obj(sc, &sc->rx_mode_obj);

    /* multicast configuration controlling object */
    ecore_init_mcast_obj(sc,
                         &sc->mcast_obj,
                         sc->fp[0].cl_id,
                         sc->fp[0].index,
                         SC_FUNC(sc),
                         SC_FUNC(sc),
                         BXE_SP(sc, mcast_rdata),
                         BXE_SP_MAPPING(sc, mcast_rdata),
                         ECORE_FILTER_MCAST_PENDING,
                         &sc->sp_state,
                         o_type);

    /* Setup CAM credit pools */
    ecore_init_mac_credit_pool(sc,
                               &sc->macs_pool,
                               SC_FUNC(sc),
                               CHIP_IS_E1x(sc) ? VNICS_PER_PORT(sc) :
                                                 VNICS_PER_PATH(sc));

    ecore_init_vlan_credit_pool(sc,
                                &sc->vlans_pool,
                                SC_ABS_FUNC(sc) >> 1,
                                CHIP_IS_E1x(sc) ? VNICS_PER_PORT(sc) :
                                                  VNICS_PER_PATH(sc));

    /* RSS configuration object */
    ecore_init_rss_config_obj(sc,
                              &sc->rss_conf_obj,
                              sc->fp[0].cl_id,
                              sc->fp[0].index,
                              SC_FUNC(sc),
                              SC_FUNC(sc),
                              BXE_SP(sc, rss_rdata),
                              BXE_SP_MAPPING(sc, rss_rdata),
                              ECORE_FILTER_RSS_CONF_PENDING,
                              &sc->sp_state, ECORE_OBJ_TYPE_RX);
}

/*
 * Initialize the function. This must be called before sending CLIENT_SETUP
 * for the first client.
 */
static inline int
bxe_func_start(struct bxe_softc *sc)
{
    struct ecore_func_state_params func_params = { NULL };
    struct ecore_func_start_params *start_params = &func_params.params.start;

    /* Prepare parameters for function state transitions */
    bit_set(&func_params.ramrod_flags, RAMROD_COMP_WAIT);

    func_params.f_obj = &sc->func_obj;
    func_params.cmd = ECORE_F_CMD_START;

    /* Function parameters */
    start_params->mf_mode     = sc->devinfo.mf_info.mf_mode;
    start_params->sd_vlan_tag = OVLAN(sc);

    if (CHIP_IS_E2(sc) || CHIP_IS_E3(sc)) {
        start_params->network_cos_mode = STATIC_COS;
    } else { /* CHIP_IS_E1X */
        start_params->network_cos_mode = FW_WRR;
    }

    //start_params->gre_tunnel_mode = 0;
    //start_params->gre_tunnel_rss  = 0;

    return (ecore_func_state_change(sc, &func_params));
}

static int
bxe_set_power_state(struct bxe_softc *sc,
                    uint8_t          state)
{
    uint16_t pmcsr;

    /* If there is no power capability, silently succeed */
    if (!(sc->devinfo.pcie_cap_flags & BXE_PM_CAPABLE_FLAG)) {
        BLOGW(sc, "No power capability\n");
        return (0);
    }

    pmcsr = pci_read_config(sc->dev,
                            (sc->devinfo.pcie_pm_cap_reg + PCIR_POWER_STATUS),
                            2);

    switch (state) {
    case PCI_PM_D0:
        pci_write_config(sc->dev,
                         (sc->devinfo.pcie_pm_cap_reg + PCIR_POWER_STATUS),
                         ((pmcsr & ~PCIM_PSTAT_DMASK) | PCIM_PSTAT_PME), 2);

        if (pmcsr & PCIM_PSTAT_DMASK) {
            /* delay required during transition out of D3hot */
            DELAY(20000);
        }

        break;

    case PCI_PM_D3hot:
        /* XXX if there are other clients above don't shut down the power */

        /* don't shut down the power for emulation and FPGA */
        if (CHIP_REV_IS_SLOW(sc)) {
            return (0);
        }

        pmcsr &= ~PCIM_PSTAT_DMASK;
        pmcsr |= PCIM_PSTAT_D3;

        if (sc->wol) {
            pmcsr |= PCIM_PSTAT_PMEENABLE;
        }

        pci_write_config(sc->dev,
                         (sc->devinfo.pcie_pm_cap_reg + PCIR_POWER_STATUS),
                         pmcsr, 4);

        /*
         * No more memory access after this point until device is brought back
         * to D0 state.
         */
        break;

    default:
        BLOGE(sc, "Can't support PCI power state = 0x%x pmcsr 0x%x\n",
            state, pmcsr);
        return (-1);
    }

    return (0);
}


/* return true if succeeded to acquire the lock */
static uint8_t
bxe_trylock_hw_lock(struct bxe_softc *sc,
                    uint32_t         resource)
{
    uint32_t lock_status;
    uint32_t resource_bit = (1 << resource);
    int func = SC_FUNC(sc);
    uint32_t hw_lock_control_reg;

    BLOGD(sc, DBG_LOAD, "Trying to take a resource lock 0x%x\n", resource);

    /* Validating that the resource is within range */
    if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
        BLOGD(sc, DBG_LOAD,
              "resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
              resource, HW_LOCK_MAX_RESOURCE_VALUE);
        return (FALSE);
    }

    if (func <= 5) {
        hw_lock_control_reg = (MISC_REG_DRIVER_CONTROL_1 + func*8);
    } else {
        hw_lock_control_reg = (MISC_REG_DRIVER_CONTROL_7 + (func - 6)*8);
    }

    /* try to acquire the lock */
    REG_WR(sc, hw_lock_control_reg + 4, resource_bit);
    lock_status = REG_RD(sc, hw_lock_control_reg);
    if (lock_status & resource_bit) {
        return (TRUE);
    }

    BLOGE(sc, "Failed to get a resource lock 0x%x func %d "
        "lock_status 0x%x resource_bit 0x%x\n", resource, func,
        lock_status, resource_bit);

    return (FALSE);
}

/*
 * Get the recovery leader resource id according to the engine this function
 * belongs to. Currently only only 2 engines is supported.
 */
static int
bxe_get_leader_lock_resource(struct bxe_softc *sc)
{
    if (SC_PATH(sc)) {
        return (HW_LOCK_RESOURCE_RECOVERY_LEADER_1);
    } else {
        return (HW_LOCK_RESOURCE_RECOVERY_LEADER_0);
    }
}

/* try to acquire a leader lock for current engine */
static uint8_t
bxe_trylock_leader_lock(struct bxe_softc *sc)
{
    return (bxe_trylock_hw_lock(sc, bxe_get_leader_lock_resource(sc)));
}

static int
bxe_release_leader_lock(struct bxe_softc *sc)
{
    return (bxe_release_hw_lock(sc, bxe_get_leader_lock_resource(sc)));
}

/* close gates #2, #3 and #4 */
static void
bxe_set_234_gates(struct bxe_softc *sc,
                  uint8_t          close)
{
    uint32_t val;

    /* gates #2 and #4a are closed/opened for "not E1" only */
    if (!CHIP_IS_E1(sc)) {
        /* #4 */
        REG_WR(sc, PXP_REG_HST_DISCARD_DOORBELLS, !!close);
        /* #2 */
        REG_WR(sc, PXP_REG_HST_DISCARD_INTERNAL_WRITES, !!close);
    }

    /* #3 */
    if (CHIP_IS_E1x(sc)) {
        /* prevent interrupts from HC on both ports */
        val = REG_RD(sc, HC_REG_CONFIG_1);
        REG_WR(sc, HC_REG_CONFIG_1,
               (!close) ? (val | HC_CONFIG_1_REG_BLOCK_DISABLE_1) :
               (val & ~(uint32_t)HC_CONFIG_1_REG_BLOCK_DISABLE_1));

        val = REG_RD(sc, HC_REG_CONFIG_0);
        REG_WR(sc, HC_REG_CONFIG_0,
               (!close) ? (val | HC_CONFIG_0_REG_BLOCK_DISABLE_0) :
               (val & ~(uint32_t)HC_CONFIG_0_REG_BLOCK_DISABLE_0));
    } else {
        /* Prevent incoming interrupts in IGU */
        val = REG_RD(sc, IGU_REG_BLOCK_CONFIGURATION);

        REG_WR(sc, IGU_REG_BLOCK_CONFIGURATION,
               (!close) ?
               (val | IGU_BLOCK_CONFIGURATION_REG_BLOCK_ENABLE) :
               (val & ~(uint32_t)IGU_BLOCK_CONFIGURATION_REG_BLOCK_ENABLE));
    }

    BLOGD(sc, DBG_LOAD, "%s gates #2, #3 and #4\n",
          close ? "closing" : "opening");

    wmb();
}

/* poll for pending writes bit, it should get cleared in no more than 1s */
static int
bxe_er_poll_igu_vq(struct bxe_softc *sc)
{
    uint32_t cnt = 1000;
    uint32_t pend_bits = 0;

    do {
        pend_bits = REG_RD(sc, IGU_REG_PENDING_BITS_STATUS);

        if (pend_bits == 0) {
            break;
        }

        DELAY(1000);
    } while (--cnt > 0);

    if (cnt == 0) {
        BLOGE(sc, "Still pending IGU requests bits=0x%08x!\n", pend_bits);
        return (-1);
    }

    return (0);
}

#define SHARED_MF_CLP_MAGIC  0x80000000 /* 'magic' bit */

static void
bxe_clp_reset_prep(struct bxe_softc *sc,
                   uint32_t         *magic_val)
{
    /* Do some magic... */
    uint32_t val = MFCFG_RD(sc, shared_mf_config.clp_mb);
    *magic_val = val & SHARED_MF_CLP_MAGIC;
    MFCFG_WR(sc, shared_mf_config.clp_mb, val | SHARED_MF_CLP_MAGIC);
}

/* restore the value of the 'magic' bit */
static void
bxe_clp_reset_done(struct bxe_softc *sc,
                   uint32_t         magic_val)
{
    /* Restore the 'magic' bit value... */
    uint32_t val = MFCFG_RD(sc, shared_mf_config.clp_mb);
    MFCFG_WR(sc, shared_mf_config.clp_mb,
              (val & (~SHARED_MF_CLP_MAGIC)) | magic_val);
}

/* prepare for MCP reset, takes care of CLP configurations */
static void
bxe_reset_mcp_prep(struct bxe_softc *sc,
                   uint32_t         *magic_val)
{
    uint32_t shmem;
    uint32_t validity_offset;

    /* set `magic' bit in order to save MF config */
    if (!CHIP_IS_E1(sc)) {
        bxe_clp_reset_prep(sc, magic_val);
    }

    /* get shmem offset */
    shmem = REG_RD(sc, MISC_REG_SHARED_MEM_ADDR);
    validity_offset =
        offsetof(struct shmem_region, validity_map[SC_PORT(sc)]);

    /* Clear validity map flags */
    if (shmem > 0) {
        REG_WR(sc, shmem + validity_offset, 0);
    }
}

#define MCP_TIMEOUT      5000   /* 5 seconds (in ms) */
#define MCP_ONE_TIMEOUT  100    /* 100 ms */

static void
bxe_mcp_wait_one(struct bxe_softc *sc)
{
    /* special handling for emulation and FPGA (10 times longer) */
    if (CHIP_REV_IS_SLOW(sc)) {
        DELAY((MCP_ONE_TIMEOUT*10) * 1000);
    } else {
        DELAY((MCP_ONE_TIMEOUT) * 1000);
    }
}

/* initialize shmem_base and waits for validity signature to appear */
static int
bxe_init_shmem(struct bxe_softc *sc)
{
    int cnt = 0;
    uint32_t val = 0;

    do {
        sc->devinfo.shmem_base     =
        sc->link_params.shmem_base =
            REG_RD(sc, MISC_REG_SHARED_MEM_ADDR);

        if (sc->devinfo.shmem_base) {
            val = SHMEM_RD(sc, validity_map[SC_PORT(sc)]);
            if (val & SHR_MEM_VALIDITY_MB)
                return (0);
        }

        bxe_mcp_wait_one(sc);

    } while (cnt++ < (MCP_TIMEOUT / MCP_ONE_TIMEOUT));

    BLOGE(sc, "BAD MCP validity signature\n");

    return (-1);
}

static int
bxe_reset_mcp_comp(struct bxe_softc *sc,
                   uint32_t         magic_val)
{
    int rc = bxe_init_shmem(sc);

    /* Restore the `magic' bit value */
    if (!CHIP_IS_E1(sc)) {
        bxe_clp_reset_done(sc, magic_val);
    }

    return (rc);
}

static void
bxe_pxp_prep(struct bxe_softc *sc)
{
    if (!CHIP_IS_E1(sc)) {
        REG_WR(sc, PXP2_REG_RD_START_INIT, 0);
        REG_WR(sc, PXP2_REG_RQ_RBC_DONE, 0);
        wmb();
    }
}

/*
 * Reset the whole chip except for:
 *      - PCIE core
 *      - PCI Glue, PSWHST, PXP/PXP2 RF (all controlled by one reset bit)
 *      - IGU
 *      - MISC (including AEU)
 *      - GRC
 *      - RBCN, RBCP
 */
static void
bxe_process_kill_chip_reset(struct bxe_softc *sc,
                            uint8_t          global)
{
    uint32_t not_reset_mask1, reset_mask1, not_reset_mask2, reset_mask2;
    uint32_t global_bits2, stay_reset2;

    /*
     * Bits that have to be set in reset_mask2 if we want to reset 'global'
     * (per chip) blocks.
     */
    global_bits2 =
        MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CPU |
        MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CORE;

    /*
     * Don't reset the following blocks.
     * Important: per port blocks (such as EMAC, BMAC, UMAC) can't be
     *            reset, as in 4 port device they might still be owned
     *            by the MCP (there is only one leader per path).
     */
    not_reset_mask1 =
        MISC_REGISTERS_RESET_REG_1_RST_HC |
        MISC_REGISTERS_RESET_REG_1_RST_PXPV |
        MISC_REGISTERS_RESET_REG_1_RST_PXP;

    not_reset_mask2 =
        MISC_REGISTERS_RESET_REG_2_RST_PCI_MDIO |
        MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE |
        MISC_REGISTERS_RESET_REG_2_RST_EMAC1_HARD_CORE |
        MISC_REGISTERS_RESET_REG_2_RST_MISC_CORE |
        MISC_REGISTERS_RESET_REG_2_RST_RBCN |
        MISC_REGISTERS_RESET_REG_2_RST_GRC  |
        MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_REG_HARD_CORE |
        MISC_REGISTERS_RESET_REG_2_RST_MCP_N_HARD_CORE_RST_B |
        MISC_REGISTERS_RESET_REG_2_RST_ATC |
        MISC_REGISTERS_RESET_REG_2_PGLC |
        MISC_REGISTERS_RESET_REG_2_RST_BMAC0 |
        MISC_REGISTERS_RESET_REG_2_RST_BMAC1 |
        MISC_REGISTERS_RESET_REG_2_RST_EMAC0 |
        MISC_REGISTERS_RESET_REG_2_RST_EMAC1 |
        MISC_REGISTERS_RESET_REG_2_UMAC0 |
        MISC_REGISTERS_RESET_REG_2_UMAC1;

    /*
     * Keep the following blocks in reset:
     *  - all xxMACs are handled by the elink code.
     */
    stay_reset2 =
        MISC_REGISTERS_RESET_REG_2_XMAC |
        MISC_REGISTERS_RESET_REG_2_XMAC_SOFT;

    /* Full reset masks according to the chip */
    reset_mask1 = 0xffffffff;

    if (CHIP_IS_E1(sc))
        reset_mask2 = 0xffff;
    else if (CHIP_IS_E1H(sc))
        reset_mask2 = 0x1ffff;
    else if (CHIP_IS_E2(sc))
        reset_mask2 = 0xfffff;
    else /* CHIP_IS_E3 */
        reset_mask2 = 0x3ffffff;

    /* Don't reset global blocks unless we need to */
    if (!global)
        reset_mask2 &= ~global_bits2;

    /*
     * In case of attention in the QM, we need to reset PXP
     * (MISC_REGISTERS_RESET_REG_2_RST_PXP_RQ_RD_WR) before QM
     * because otherwise QM reset would release 'close the gates' shortly
     * before resetting the PXP, then the PSWRQ would send a write
     * request to PGLUE. Then when PXP is reset, PGLUE would try to
     * read the payload data from PSWWR, but PSWWR would not
     * respond. The write queue in PGLUE would stuck, dmae commands
     * would not return. Therefore it's important to reset the second
     * reset register (containing the
     * MISC_REGISTERS_RESET_REG_2_RST_PXP_RQ_RD_WR bit) before the
     * first one (containing the MISC_REGISTERS_RESET_REG_1_RST_QM
     * bit).
     */
    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
           reset_mask2 & (~not_reset_mask2));

    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
           reset_mask1 & (~not_reset_mask1));

    mb();
    wmb();

    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
           reset_mask2 & (~stay_reset2));

    mb();
    wmb();

    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, reset_mask1);
    wmb();
}

static int
bxe_process_kill(struct bxe_softc *sc,
                 uint8_t          global)
{
    int cnt = 1000;
    uint32_t val = 0;
    uint32_t sr_cnt, blk_cnt, port_is_idle_0, port_is_idle_1, pgl_exp_rom2;
    uint32_t tags_63_32 = 0;

    /* Empty the Tetris buffer, wait for 1s */
    do {
        sr_cnt  = REG_RD(sc, PXP2_REG_RD_SR_CNT);
        blk_cnt = REG_RD(sc, PXP2_REG_RD_BLK_CNT);
        port_is_idle_0 = REG_RD(sc, PXP2_REG_RD_PORT_IS_IDLE_0);
        port_is_idle_1 = REG_RD(sc, PXP2_REG_RD_PORT_IS_IDLE_1);
        pgl_exp_rom2 = REG_RD(sc, PXP2_REG_PGL_EXP_ROM2);
        if (CHIP_IS_E3(sc)) {
            tags_63_32 = REG_RD(sc, PGLUE_B_REG_TAGS_63_32);
        }

        if ((sr_cnt == 0x7e) && (blk_cnt == 0xa0) &&
            ((port_is_idle_0 & 0x1) == 0x1) &&
            ((port_is_idle_1 & 0x1) == 0x1) &&
            (pgl_exp_rom2 == 0xffffffff) &&
            (!CHIP_IS_E3(sc) || (tags_63_32 == 0xffffffff)))
            break;
        DELAY(1000);
    } while (cnt-- > 0);

    if (cnt <= 0) {
        BLOGE(sc, "ERROR: Tetris buffer didn't get empty or there "
                  "are still outstanding read requests after 1s! "
                  "sr_cnt=0x%08x, blk_cnt=0x%08x, port_is_idle_0=0x%08x, "
                  "port_is_idle_1=0x%08x, pgl_exp_rom2=0x%08x\n",
              sr_cnt, blk_cnt, port_is_idle_0,
              port_is_idle_1, pgl_exp_rom2);
        return (-1);
    }

    mb();

    /* Close gates #2, #3 and #4 */
    bxe_set_234_gates(sc, TRUE);

    /* Poll for IGU VQs for 57712 and newer chips */
    if (!CHIP_IS_E1x(sc) && bxe_er_poll_igu_vq(sc)) {
        return (-1);
    }

    /* XXX indicate that "process kill" is in progress to MCP */

    /* clear "unprepared" bit */
    REG_WR(sc, MISC_REG_UNPREPARED, 0);
    mb();

    /* Make sure all is written to the chip before the reset */
    wmb();

    /*
     * Wait for 1ms to empty GLUE and PCI-E core queues,
     * PSWHST, GRC and PSWRD Tetris buffer.
     */
    DELAY(1000);

    /* Prepare to chip reset: */
    /* MCP */
    if (global) {
        bxe_reset_mcp_prep(sc, &val);
    }

    /* PXP */
    bxe_pxp_prep(sc);
    mb();

    /* reset the chip */
    bxe_process_kill_chip_reset(sc, global);
    mb();

    /* clear errors in PGB */
    if (!CHIP_IS_E1(sc))
        REG_WR(sc, PGLUE_B_REG_LATCHED_ERRORS_CLR, 0x7f);

    /* Recover after reset: */
    /* MCP */
    if (global && bxe_reset_mcp_comp(sc, val)) {
        return (-1);
    }

    /* XXX add resetting the NO_MCP mode DB here */

    /* Open the gates #2, #3 and #4 */
    bxe_set_234_gates(sc, FALSE);

    /* XXX
     * IGU/AEU preparation bring back the AEU/IGU to a reset state
     * re-enable attentions
     */

    return (0);
}

static int
bxe_leader_reset(struct bxe_softc *sc)
{
    int rc = 0;
    uint8_t global = bxe_reset_is_global(sc);
    uint32_t load_code;

    /*
     * If not going to reset MCP, load "fake" driver to reset HW while
     * driver is owner of the HW.
     */
    if (!global && !BXE_NOMCP(sc)) {
        load_code = bxe_fw_command(sc, DRV_MSG_CODE_LOAD_REQ,
                                   DRV_MSG_CODE_LOAD_REQ_WITH_LFA);
        if (!load_code) {
            BLOGE(sc, "MCP response failure, aborting\n");
            rc = -1;
            goto exit_leader_reset;
        }

        if ((load_code != FW_MSG_CODE_DRV_LOAD_COMMON_CHIP) &&
            (load_code != FW_MSG_CODE_DRV_LOAD_COMMON)) {
            BLOGE(sc, "MCP unexpected response, aborting\n");
            rc = -1;
            goto exit_leader_reset2;
        }

        load_code = bxe_fw_command(sc, DRV_MSG_CODE_LOAD_DONE, 0);
        if (!load_code) {
            BLOGE(sc, "MCP response failure, aborting\n");
            rc = -1;
            goto exit_leader_reset2;
        }
    }

    /* try to recover after the failure */
    if (bxe_process_kill(sc, global)) {
        BLOGE(sc, "Something bad occurred on engine %d!\n", SC_PATH(sc));
        rc = -1;
        goto exit_leader_reset2;
    }

    /*
     * Clear the RESET_IN_PROGRESS and RESET_GLOBAL bits and update the driver
     * state.
     */
    bxe_set_reset_done(sc);
    if (global) {
        bxe_clear_reset_global(sc);
    }

exit_leader_reset2:

    /* unload "fake driver" if it was loaded */
    if (!global && !BXE_NOMCP(sc)) {
        bxe_fw_command(sc, DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP, 0);
        bxe_fw_command(sc, DRV_MSG_CODE_UNLOAD_DONE, 0);
    }

exit_leader_reset:

    sc->is_leader = 0;
    bxe_release_leader_lock(sc);

    mb();
    return (rc);
}

/*
 * prepare INIT transition, parameters configured:
 *   - HC configuration
 *   - Queue's CDU context
 */
static void
bxe_pf_q_prep_init(struct bxe_softc               *sc,
                   struct bxe_fastpath            *fp,
                   struct ecore_queue_init_params *init_params)
{
    uint8_t cos;
    int cxt_index, cxt_offset;

    bxe_set_bit(ECORE_Q_FLG_HC, &init_params->rx.flags);
    bxe_set_bit(ECORE_Q_FLG_HC, &init_params->tx.flags);

    bxe_set_bit(ECORE_Q_FLG_HC_EN, &init_params->rx.flags);
    bxe_set_bit(ECORE_Q_FLG_HC_EN, &init_params->tx.flags);

    /* HC rate */
    init_params->rx.hc_rate =
        sc->hc_rx_ticks ? (1000000 / sc->hc_rx_ticks) : 0;
    init_params->tx.hc_rate =
        sc->hc_tx_ticks ? (1000000 / sc->hc_tx_ticks) : 0;

    /* FW SB ID */
    init_params->rx.fw_sb_id = init_params->tx.fw_sb_id = fp->fw_sb_id;

    /* CQ index among the SB indices */
    init_params->rx.sb_cq_index = HC_INDEX_ETH_RX_CQ_CONS;
    init_params->tx.sb_cq_index = HC_INDEX_ETH_FIRST_TX_CQ_CONS;

    /* set maximum number of COSs supported by this queue */
    init_params->max_cos = sc->max_cos;

    BLOGD(sc, DBG_LOAD, "fp %d setting queue params max cos to %d\n",
          fp->index, init_params->max_cos);

    /* set the context pointers queue object */
    for (cos = FIRST_TX_COS_INDEX; cos < init_params->max_cos; cos++) {
        /* XXX change index/cid here if ever support multiple tx CoS */
        /* fp->txdata[cos]->cid */
        cxt_index = fp->index / ILT_PAGE_CIDS;
        cxt_offset = fp->index - (cxt_index * ILT_PAGE_CIDS);
        init_params->cxts[cos] = &sc->context[cxt_index].vcxt[cxt_offset].eth;
    }
}

/* set flags that are common for the Tx-only and not normal connections */
static unsigned long
bxe_get_common_flags(struct bxe_softc    *sc,
                     struct bxe_fastpath *fp,
                     uint8_t             zero_stats)
{
    unsigned long flags = 0;

    /* PF driver will always initialize the Queue to an ACTIVE state */
    bxe_set_bit(ECORE_Q_FLG_ACTIVE, &flags);

    /*
     * tx only connections collect statistics (on the same index as the
     * parent connection). The statistics are zeroed when the parent
     * connection is initialized.
     */

    bxe_set_bit(ECORE_Q_FLG_STATS, &flags);
    if (zero_stats) {
        bxe_set_bit(ECORE_Q_FLG_ZERO_STATS, &flags);
    }

    /*
     * tx only connections can support tx-switching, though their
     * CoS-ness doesn't survive the loopback
     */
    if (sc->flags & BXE_TX_SWITCHING) {
        bxe_set_bit(ECORE_Q_FLG_TX_SWITCH, &flags);
    }

    bxe_set_bit(ECORE_Q_FLG_PCSUM_ON_PKT, &flags);

    return (flags);
}

static unsigned long
bxe_get_q_flags(struct bxe_softc    *sc,
                struct bxe_fastpath *fp,
                uint8_t             leading)
{
    unsigned long flags = 0;

    if (IS_MF_SD(sc)) {
        bxe_set_bit(ECORE_Q_FLG_OV, &flags);
    }

    if (if_getcapenable(sc->ifp) & IFCAP_LRO) {
        bxe_set_bit(ECORE_Q_FLG_TPA, &flags);
#if __FreeBSD_version >= 800000
        bxe_set_bit(ECORE_Q_FLG_TPA_IPV6, &flags);
#endif
    }

    if (leading) {
        bxe_set_bit(ECORE_Q_FLG_LEADING_RSS, &flags);
        bxe_set_bit(ECORE_Q_FLG_MCAST, &flags);
    }

    bxe_set_bit(ECORE_Q_FLG_VLAN, &flags);

    /* merge with common flags */
    return (flags | bxe_get_common_flags(sc, fp, TRUE));
}

static void
bxe_pf_q_prep_general(struct bxe_softc                  *sc,
                      struct bxe_fastpath               *fp,
                      struct ecore_general_setup_params *gen_init,
                      uint8_t                           cos)
{
    gen_init->stat_id = bxe_stats_id(fp);
    gen_init->spcl_id = fp->cl_id;
    gen_init->mtu = sc->mtu;
    gen_init->cos = cos;
}

static void
bxe_pf_rx_q_prep(struct bxe_softc              *sc,
                 struct bxe_fastpath           *fp,
                 struct rxq_pause_params       *pause,
                 struct ecore_rxq_setup_params *rxq_init)
{
    uint8_t max_sge = 0;
    uint16_t sge_sz = 0;
    uint16_t tpa_agg_size = 0;

    pause->sge_th_lo = SGE_TH_LO(sc);
    pause->sge_th_hi = SGE_TH_HI(sc);

    /* validate SGE ring has enough to cross high threshold */
    if (sc->dropless_fc &&
            (pause->sge_th_hi + FW_PREFETCH_CNT) >
            (RX_SGE_USABLE_PER_PAGE * RX_SGE_NUM_PAGES)) {
        BLOGW(sc, "sge ring threshold limit\n");
    }

    /* minimum max_aggregation_size is 2*MTU (two full buffers) */
    tpa_agg_size = (2 * sc->mtu);
    if (tpa_agg_size < sc->max_aggregation_size) {
        tpa_agg_size = sc->max_aggregation_size;
    }

    max_sge = SGE_PAGE_ALIGN(sc->mtu) >> SGE_PAGE_SHIFT;
    max_sge = ((max_sge + PAGES_PER_SGE - 1) &
                   (~(PAGES_PER_SGE - 1))) >> PAGES_PER_SGE_SHIFT;
    sge_sz = (uint16_t)min(SGE_PAGES, 0xffff);

    /* pause - not for e1 */
    if (!CHIP_IS_E1(sc)) {
        pause->bd_th_lo = BD_TH_LO(sc);
        pause->bd_th_hi = BD_TH_HI(sc);

        pause->rcq_th_lo = RCQ_TH_LO(sc);
        pause->rcq_th_hi = RCQ_TH_HI(sc);

        /* validate rings have enough entries to cross high thresholds */
        if (sc->dropless_fc &&
            pause->bd_th_hi + FW_PREFETCH_CNT >
            sc->rx_ring_size) {
            BLOGW(sc, "rx bd ring threshold limit\n");
        }

        if (sc->dropless_fc &&
            pause->rcq_th_hi + FW_PREFETCH_CNT >
            RCQ_NUM_PAGES * RCQ_USABLE_PER_PAGE) {
            BLOGW(sc, "rcq ring threshold limit\n");
        }

        pause->pri_map = 1;
    }

    /* rxq setup */
    rxq_init->dscr_map   = fp->rx_dma.paddr;
    rxq_init->sge_map    = fp->rx_sge_dma.paddr;
    rxq_init->rcq_map    = fp->rcq_dma.paddr;
    rxq_init->rcq_np_map = (fp->rcq_dma.paddr + BCM_PAGE_SIZE);

    /*
     * This should be a maximum number of data bytes that may be
     * placed on the BD (not including paddings).
     */
    rxq_init->buf_sz = (fp->rx_buf_size -
                        IP_HEADER_ALIGNMENT_PADDING);

    rxq_init->cl_qzone_id     = fp->cl_qzone_id;
    rxq_init->tpa_agg_sz      = tpa_agg_size;
    rxq_init->sge_buf_sz      = sge_sz;
    rxq_init->max_sges_pkt    = max_sge;
    rxq_init->rss_engine_id   = SC_FUNC(sc);
    rxq_init->mcast_engine_id = SC_FUNC(sc);

    /*
     * Maximum number or simultaneous TPA aggregation for this Queue.
     * For PF Clients it should be the maximum available number.
     * VF driver(s) may want to define it to a smaller value.
     */
    rxq_init->max_tpa_queues = MAX_AGG_QS(sc);

    rxq_init->cache_line_log = BXE_RX_ALIGN_SHIFT;
    rxq_init->fw_sb_id = fp->fw_sb_id;

    rxq_init->sb_cq_index = HC_INDEX_ETH_RX_CQ_CONS;

    /*
     * configure silent vlan removal
     * if multi function mode is afex, then mask default vlan
     */
    if (IS_MF_AFEX(sc)) {
        rxq_init->silent_removal_value =
            sc->devinfo.mf_info.afex_def_vlan_tag;
        rxq_init->silent_removal_mask = EVL_VLID_MASK;
    }
}

static void
bxe_pf_tx_q_prep(struct bxe_softc              *sc,
                 struct bxe_fastpath           *fp,
                 struct ecore_txq_setup_params *txq_init,
                 uint8_t                       cos)
{
    /*
     * XXX If multiple CoS is ever supported then each fastpath structure
     * will need to maintain tx producer/consumer/dma/etc values *per* CoS.
     * fp->txdata[cos]->tx_dma.paddr;
     */
    txq_init->dscr_map     = fp->tx_dma.paddr;
    txq_init->sb_cq_index  = HC_INDEX_ETH_FIRST_TX_CQ_CONS + cos;
    txq_init->traffic_type = LLFC_TRAFFIC_TYPE_NW;
    txq_init->fw_sb_id     = fp->fw_sb_id;

    /*
     * set the TSS leading client id for TX classfication to the
     * leading RSS client id
     */
    txq_init->tss_leading_cl_id = BXE_FP(sc, 0, cl_id);
}

/*
 * This function performs 2 steps in a queue state machine:
 *   1) RESET->INIT
 *   2) INIT->SETUP
 */
static int
bxe_setup_queue(struct bxe_softc    *sc,
                struct bxe_fastpath *fp,
                uint8_t             leading)
{
    struct ecore_queue_state_params q_params = { NULL };
    struct ecore_queue_setup_params *setup_params =
                        &q_params.params.setup;
    int rc;

    BLOGD(sc, DBG_LOAD, "setting up queue %d\n", fp->index);

    bxe_ack_sb(sc, fp->igu_sb_id, USTORM_ID, 0, IGU_INT_ENABLE, 0);

    q_params.q_obj = &BXE_SP_OBJ(sc, fp).q_obj;

    /* we want to wait for completion in this context */
    bxe_set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);

    /* prepare the INIT parameters */
    bxe_pf_q_prep_init(sc, fp, &q_params.params.init);

    /* Set the command */
    q_params.cmd = ECORE_Q_CMD_INIT;

    /* Change the state to INIT */
    rc = ecore_queue_state_change(sc, &q_params);
    if (rc) {
        BLOGE(sc, "Queue(%d) INIT failed rc = %d\n", fp->index, rc);
        return (rc);
    }

    BLOGD(sc, DBG_LOAD, "init complete\n");

    /* now move the Queue to the SETUP state */
    memset(setup_params, 0, sizeof(*setup_params));

    /* set Queue flags */
    setup_params->flags = bxe_get_q_flags(sc, fp, leading);

    /* set general SETUP parameters */
    bxe_pf_q_prep_general(sc, fp, &setup_params->gen_params,
                          FIRST_TX_COS_INDEX);

    bxe_pf_rx_q_prep(sc, fp,
                     &setup_params->pause_params,
                     &setup_params->rxq_params);

    bxe_pf_tx_q_prep(sc, fp,
                     &setup_params->txq_params,
                     FIRST_TX_COS_INDEX);

    /* Set the command */
    q_params.cmd = ECORE_Q_CMD_SETUP;

    /* change the state to SETUP */
    rc = ecore_queue_state_change(sc, &q_params);
    if (rc) {
        BLOGE(sc, "Queue(%d) SETUP failed (rc = %d)\n", fp->index, rc);
        return (rc);
    }

    return (rc);
}

static int
bxe_setup_leading(struct bxe_softc *sc)
{
    return (bxe_setup_queue(sc, &sc->fp[0], TRUE));
}

static int
bxe_config_rss_pf(struct bxe_softc            *sc,
                  struct ecore_rss_config_obj *rss_obj,
                  uint8_t                     config_hash)
{
    struct ecore_config_rss_params params = { NULL };
    int i;

    /*
     * Although RSS is meaningless when there is a single HW queue we
     * still need it enabled in order to have HW Rx hash generated.
     */

    params.rss_obj = rss_obj;

    bxe_set_bit(RAMROD_COMP_WAIT, &params.ramrod_flags);

    bxe_set_bit(ECORE_RSS_MODE_REGULAR, &params.rss_flags);

    /* RSS configuration */
    bxe_set_bit(ECORE_RSS_IPV4, &params.rss_flags);
    bxe_set_bit(ECORE_RSS_IPV4_TCP, &params.rss_flags);
    bxe_set_bit(ECORE_RSS_IPV6, &params.rss_flags);
    bxe_set_bit(ECORE_RSS_IPV6_TCP, &params.rss_flags);
    if (rss_obj->udp_rss_v4) {
        bxe_set_bit(ECORE_RSS_IPV4_UDP, &params.rss_flags);
    }
    if (rss_obj->udp_rss_v6) {
        bxe_set_bit(ECORE_RSS_IPV6_UDP, &params.rss_flags);
    }

    /* Hash bits */
    params.rss_result_mask = MULTI_MASK;

    memcpy(params.ind_table, rss_obj->ind_table, sizeof(params.ind_table));

    if (config_hash) {
        /* RSS keys */
        for (i = 0; i < sizeof(params.rss_key) / 4; i++) {
            params.rss_key[i] = arc4random();
        }

        bxe_set_bit(ECORE_RSS_SET_SRCH, &params.rss_flags);
    }

    return (ecore_config_rss(sc, &params));
}

static int
bxe_config_rss_eth(struct bxe_softc *sc,
                   uint8_t          config_hash)
{
    return (bxe_config_rss_pf(sc, &sc->rss_conf_obj, config_hash));
}

static int
bxe_init_rss_pf(struct bxe_softc *sc)
{
    uint8_t num_eth_queues = BXE_NUM_ETH_QUEUES(sc);
    int i;

    /*
     * Prepare the initial contents of the indirection table if
     * RSS is enabled
     */
    for (i = 0; i < sizeof(sc->rss_conf_obj.ind_table); i++) {
        sc->rss_conf_obj.ind_table[i] =
            (sc->fp->cl_id + (i % num_eth_queues));
    }

    if (sc->udp_rss) {
        sc->rss_conf_obj.udp_rss_v4 = sc->rss_conf_obj.udp_rss_v6 = 1;
    }

    /*
     * For 57710 and 57711 SEARCHER configuration (rss_keys) is
     * per-port, so if explicit configuration is needed, do it only
     * for a PMF.
     *
     * For 57712 and newer it's a per-function configuration.
     */
    return (bxe_config_rss_eth(sc, sc->port.pmf || !CHIP_IS_E1x(sc)));
}

static int
bxe_set_mac_one(struct bxe_softc          *sc,
                uint8_t                   *mac,
                struct ecore_vlan_mac_obj *obj,
                uint8_t                   set,
                int                       mac_type,
                unsigned long             *ramrod_flags)
{
    struct ecore_vlan_mac_ramrod_params ramrod_param;
    int rc;

    memset(&ramrod_param, 0, sizeof(ramrod_param));

    /* fill in general parameters */
    ramrod_param.vlan_mac_obj = obj;
    ramrod_param.ramrod_flags = *ramrod_flags;

    /* fill a user request section if needed */
    if (!bxe_test_bit(RAMROD_CONT, ramrod_flags)) {
        memcpy(ramrod_param.user_req.u.mac.mac, mac, ETH_ALEN);

        bxe_set_bit(mac_type, &ramrod_param.user_req.vlan_mac_flags);

        /* Set the command: ADD or DEL */
        ramrod_param.user_req.cmd = (set) ? ECORE_VLAN_MAC_ADD :
                                            ECORE_VLAN_MAC_DEL;
    }

    rc = ecore_config_vlan_mac(sc, &ramrod_param);

    if (rc == ECORE_EXISTS) {
        BLOGD(sc, DBG_SP, "Failed to schedule ADD operations (EEXIST)\n");
        /* do not treat adding same MAC as error */
        rc = 0;
    } else if (rc < 0) {
        BLOGE(sc, "%s MAC failed (%d)\n", (set ? "Set" : "Delete"), rc);
    }

    return (rc);
}

static int
bxe_set_eth_mac(struct bxe_softc *sc,
                uint8_t          set)
{
    unsigned long ramrod_flags = 0;

    BLOGD(sc, DBG_LOAD, "Adding Ethernet MAC\n");

    bxe_set_bit(RAMROD_COMP_WAIT, &ramrod_flags);

    /* Eth MAC is set on RSS leading client (fp[0]) */
    return (bxe_set_mac_one(sc, sc->link_params.mac_addr,
                            &sc->sp_objs->mac_obj,
                            set, ECORE_ETH_MAC, &ramrod_flags));
}

static int
bxe_get_cur_phy_idx(struct bxe_softc *sc)
{
    uint32_t sel_phy_idx = 0;

    if (sc->link_params.num_phys <= 1) {
        return (ELINK_INT_PHY);
    }

    if (sc->link_vars.link_up) {
        sel_phy_idx = ELINK_EXT_PHY1;
        /* In case link is SERDES, check if the ELINK_EXT_PHY2 is the one */
        if ((sc->link_vars.link_status & LINK_STATUS_SERDES_LINK) &&
            (sc->link_params.phy[ELINK_EXT_PHY2].supported &
             ELINK_SUPPORTED_FIBRE))
            sel_phy_idx = ELINK_EXT_PHY2;
    } else {
        switch (elink_phy_selection(&sc->link_params)) {
        case PORT_HW_CFG_PHY_SELECTION_HARDWARE_DEFAULT:
        case PORT_HW_CFG_PHY_SELECTION_FIRST_PHY:
        case PORT_HW_CFG_PHY_SELECTION_FIRST_PHY_PRIORITY:
               sel_phy_idx = ELINK_EXT_PHY1;
               break;
        case PORT_HW_CFG_PHY_SELECTION_SECOND_PHY:
        case PORT_HW_CFG_PHY_SELECTION_SECOND_PHY_PRIORITY:
               sel_phy_idx = ELINK_EXT_PHY2;
               break;
        }
    }

    return (sel_phy_idx);
}

static int
bxe_get_link_cfg_idx(struct bxe_softc *sc)
{
    uint32_t sel_phy_idx = bxe_get_cur_phy_idx(sc);

    /*
     * The selected activated PHY is always after swapping (in case PHY
     * swapping is enabled). So when swapping is enabled, we need to reverse
     * the configuration
     */

    if (sc->link_params.multi_phy_config & PORT_HW_CFG_PHY_SWAPPED_ENABLED) {
        if (sel_phy_idx == ELINK_EXT_PHY1)
            sel_phy_idx = ELINK_EXT_PHY2;
        else if (sel_phy_idx == ELINK_EXT_PHY2)
            sel_phy_idx = ELINK_EXT_PHY1;
    }

    return (ELINK_LINK_CONFIG_IDX(sel_phy_idx));
}

static void
bxe_set_requested_fc(struct bxe_softc *sc)
{
    /*
     * Initialize link parameters structure variables
     * It is recommended to turn off RX FC for jumbo frames
     * for better performance
     */
    if (CHIP_IS_E1x(sc) && (sc->mtu > 5000)) {
        sc->link_params.req_fc_auto_adv = ELINK_FLOW_CTRL_TX;
    } else {
        sc->link_params.req_fc_auto_adv = ELINK_FLOW_CTRL_BOTH;
    }
}

static void
bxe_calc_fc_adv(struct bxe_softc *sc)
{
    uint8_t cfg_idx = bxe_get_link_cfg_idx(sc);


    sc->port.advertising[cfg_idx] &= ~(ADVERTISED_Asym_Pause |
                                           ADVERTISED_Pause);

    switch (sc->link_vars.ieee_fc &
            MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK) {

    case MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH:
        sc->port.advertising[cfg_idx] |= (ADVERTISED_Asym_Pause |
                                          ADVERTISED_Pause);
        break;

    case MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC:
        sc->port.advertising[cfg_idx] |= ADVERTISED_Asym_Pause;
        break;

    default:
        break;

    }
}

static uint16_t
bxe_get_mf_speed(struct bxe_softc *sc)
{
    uint16_t line_speed = sc->link_vars.line_speed;
    if (IS_MF(sc)) {
        uint16_t maxCfg =
            bxe_extract_max_cfg(sc, sc->devinfo.mf_info.mf_config[SC_VN(sc)]);

        /* calculate the current MAX line speed limit for the MF devices */
        if (IS_MF_SI(sc)) {
            line_speed = (line_speed * maxCfg) / 100;
        } else { /* SD mode */
            uint16_t vn_max_rate = maxCfg * 100;

            if (vn_max_rate < line_speed) {
                line_speed = vn_max_rate;
            }
        }
    }

    return (line_speed);
}

static void
bxe_fill_report_data(struct bxe_softc            *sc,
                     struct bxe_link_report_data *data)
{
    uint16_t line_speed = bxe_get_mf_speed(sc);

    memset(data, 0, sizeof(*data));

    /* fill the report data with the effective line speed */
    data->line_speed = line_speed;

    /* Link is down */
    if (!sc->link_vars.link_up || (sc->flags & BXE_MF_FUNC_DIS)) {
        bxe_set_bit(BXE_LINK_REPORT_LINK_DOWN, &data->link_report_flags);
    }

    /* Full DUPLEX */
    if (sc->link_vars.duplex == DUPLEX_FULL) {
        bxe_set_bit(BXE_LINK_REPORT_FULL_DUPLEX, &data->link_report_flags);
    }

    /* Rx Flow Control is ON */
    if (sc->link_vars.flow_ctrl & ELINK_FLOW_CTRL_RX) {
        bxe_set_bit(BXE_LINK_REPORT_RX_FC_ON, &data->link_report_flags);
    }

    /* Tx Flow Control is ON */
    if (sc->link_vars.flow_ctrl & ELINK_FLOW_CTRL_TX) {
        bxe_set_bit(BXE_LINK_REPORT_TX_FC_ON, &data->link_report_flags);
    }
}

/* report link status to OS, should be called under phy_lock */
static void
bxe_link_report_locked(struct bxe_softc *sc)
{
    struct bxe_link_report_data cur_data;

    /* reread mf_cfg */
    if (IS_PF(sc) && !CHIP_IS_E1(sc)) {
        bxe_read_mf_cfg(sc);
    }

    /* Read the current link report info */
    bxe_fill_report_data(sc, &cur_data);

    /* Don't report link down or exactly the same link status twice */
    if (!memcmp(&cur_data, &sc->last_reported_link, sizeof(cur_data)) ||
        (bxe_test_bit(BXE_LINK_REPORT_LINK_DOWN,
                      &sc->last_reported_link.link_report_flags) &&
         bxe_test_bit(BXE_LINK_REPORT_LINK_DOWN,
                      &cur_data.link_report_flags))) {
        return;
    }

	ELINK_DEBUG_P2(sc, "Change in link status : cur_data = %x, last_reported_link = %x\n",
					cur_data.link_report_flags, sc->last_reported_link.link_report_flags);
    sc->link_cnt++;

	ELINK_DEBUG_P1(sc, "link status change count = %x\n", sc->link_cnt);
    /* report new link params and remember the state for the next time */
    memcpy(&sc->last_reported_link, &cur_data, sizeof(cur_data));

    if (bxe_test_bit(BXE_LINK_REPORT_LINK_DOWN,
                     &cur_data.link_report_flags)) {
        if_link_state_change(sc->ifp, LINK_STATE_DOWN);
    } else {
        const char *duplex;
        const char *flow;

        if (bxe_test_and_clear_bit(BXE_LINK_REPORT_FULL_DUPLEX,
                                   &cur_data.link_report_flags)) {
            duplex = "full";
			ELINK_DEBUG_P0(sc, "link set to full duplex\n");
        } else {
            duplex = "half";
			ELINK_DEBUG_P0(sc, "link set to half duplex\n");
        }

        /*
         * Handle the FC at the end so that only these flags would be
         * possibly set. This way we may easily check if there is no FC
         * enabled.
         */
        if (cur_data.link_report_flags) {
            if (bxe_test_bit(BXE_LINK_REPORT_RX_FC_ON,
                             &cur_data.link_report_flags) &&
                bxe_test_bit(BXE_LINK_REPORT_TX_FC_ON,
                             &cur_data.link_report_flags)) {
                flow = "ON - receive & transmit";
            } else if (bxe_test_bit(BXE_LINK_REPORT_RX_FC_ON,
                                    &cur_data.link_report_flags) &&
                       !bxe_test_bit(BXE_LINK_REPORT_TX_FC_ON,
                                     &cur_data.link_report_flags)) {
                flow = "ON - receive";
            } else if (!bxe_test_bit(BXE_LINK_REPORT_RX_FC_ON,
                                     &cur_data.link_report_flags) &&
                       bxe_test_bit(BXE_LINK_REPORT_TX_FC_ON,
                                    &cur_data.link_report_flags)) {
                flow = "ON - transmit";
            } else {
                flow = "none"; /* possible? */
            }
        } else {
            flow = "none";
        }

        if_link_state_change(sc->ifp, LINK_STATE_UP);
        BLOGI(sc, "NIC Link is Up, %d Mbps %s duplex, Flow control: %s\n",
              cur_data.line_speed, duplex, flow);
    }
}

static void
bxe_link_report(struct bxe_softc *sc)
{
    bxe_acquire_phy_lock(sc);
    bxe_link_report_locked(sc);
    bxe_release_phy_lock(sc);
}

static void
bxe_link_status_update(struct bxe_softc *sc)
{
    if (sc->state != BXE_STATE_OPEN) {
        return;
    }

    if (IS_PF(sc) && !CHIP_REV_IS_SLOW(sc)) {
        elink_link_status_update(&sc->link_params, &sc->link_vars);
    } else {
        sc->port.supported[0] |= (ELINK_SUPPORTED_10baseT_Half |
                                  ELINK_SUPPORTED_10baseT_Full |
                                  ELINK_SUPPORTED_100baseT_Half |
                                  ELINK_SUPPORTED_100baseT_Full |
                                  ELINK_SUPPORTED_1000baseT_Full |
                                  ELINK_SUPPORTED_2500baseX_Full |
                                  ELINK_SUPPORTED_10000baseT_Full |
                                  ELINK_SUPPORTED_TP |
                                  ELINK_SUPPORTED_FIBRE |
                                  ELINK_SUPPORTED_Autoneg |
                                  ELINK_SUPPORTED_Pause |
                                  ELINK_SUPPORTED_Asym_Pause);
        sc->port.advertising[0] = sc->port.supported[0];

        sc->link_params.sc                = sc;
        sc->link_params.port              = SC_PORT(sc);
        sc->link_params.req_duplex[0]     = DUPLEX_FULL;
        sc->link_params.req_flow_ctrl[0]  = ELINK_FLOW_CTRL_NONE;
        sc->link_params.req_line_speed[0] = SPEED_10000;
        sc->link_params.speed_cap_mask[0] = 0x7f0000;
        sc->link_params.switch_cfg        = ELINK_SWITCH_CFG_10G;

        if (CHIP_REV_IS_FPGA(sc)) {
            sc->link_vars.mac_type    = ELINK_MAC_TYPE_EMAC;
            sc->link_vars.line_speed  = ELINK_SPEED_1000;
            sc->link_vars.link_status = (LINK_STATUS_LINK_UP |
                                         LINK_STATUS_SPEED_AND_DUPLEX_1000TFD);
        } else {
            sc->link_vars.mac_type    = ELINK_MAC_TYPE_BMAC;
            sc->link_vars.line_speed  = ELINK_SPEED_10000;
            sc->link_vars.link_status = (LINK_STATUS_LINK_UP |
                                         LINK_STATUS_SPEED_AND_DUPLEX_10GTFD);
        }

        sc->link_vars.link_up = 1;

        sc->link_vars.duplex    = DUPLEX_FULL;
        sc->link_vars.flow_ctrl = ELINK_FLOW_CTRL_NONE;

        if (IS_PF(sc)) {
            REG_WR(sc, NIG_REG_EGRESS_DRAIN0_MODE + sc->link_params.port*4, 0);
            bxe_stats_handle(sc, STATS_EVENT_LINK_UP);
            bxe_link_report(sc);
        }
    }

    if (IS_PF(sc)) {
        if (sc->link_vars.link_up) {
            bxe_stats_handle(sc, STATS_EVENT_LINK_UP);
        } else {
            bxe_stats_handle(sc, STATS_EVENT_STOP);
        }
        bxe_link_report(sc);
    } else {
        bxe_link_report(sc);
        bxe_stats_handle(sc, STATS_EVENT_LINK_UP);
    }
}

static int
bxe_initial_phy_init(struct bxe_softc *sc,
                     int              load_mode)
{
    int rc, cfg_idx = bxe_get_link_cfg_idx(sc);
    uint16_t req_line_speed = sc->link_params.req_line_speed[cfg_idx];
    struct elink_params *lp = &sc->link_params;

    bxe_set_requested_fc(sc);

    if (CHIP_REV_IS_SLOW(sc)) {
        uint32_t bond = CHIP_BOND_ID(sc);
        uint32_t feat = 0;

        if (CHIP_IS_E2(sc) && CHIP_IS_MODE_4_PORT(sc)) {
            feat |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_BMAC;
        } else if (bond & 0x4) {
            if (CHIP_IS_E3(sc)) {
                feat |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_XMAC;
            } else {
                feat |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_BMAC;
            }
        } else if (bond & 0x8) {
            if (CHIP_IS_E3(sc)) {
                feat |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_UMAC;
            } else {
                feat |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_EMAC;
            }
        }

        /* disable EMAC for E3 and above */
        if (bond & 0x2) {
            feat |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_EMAC;
        }

        sc->link_params.feature_config_flags |= feat;
    }

    bxe_acquire_phy_lock(sc);

    if (load_mode == LOAD_DIAG) {
        lp->loopback_mode = ELINK_LOOPBACK_XGXS;
        /* Prefer doing PHY loopback at 10G speed, if possible */
        if (lp->req_line_speed[cfg_idx] < ELINK_SPEED_10000) {
            if (lp->speed_cap_mask[cfg_idx] &
                PORT_HW_CFG_SPEED_CAPABILITY_D0_10G) {
                lp->req_line_speed[cfg_idx] = ELINK_SPEED_10000;
            } else {
                lp->req_line_speed[cfg_idx] = ELINK_SPEED_1000;
            }
        }
    }

    if (load_mode == LOAD_LOOPBACK_EXT) {
        lp->loopback_mode = ELINK_LOOPBACK_EXT;
    }

    rc = elink_phy_init(&sc->link_params, &sc->link_vars);

    bxe_release_phy_lock(sc);

    bxe_calc_fc_adv(sc);

    if (sc->link_vars.link_up) {
        bxe_stats_handle(sc, STATS_EVENT_LINK_UP);
        bxe_link_report(sc);
    }

    if (!CHIP_REV_IS_SLOW(sc)) {
        bxe_periodic_start(sc);
    }

    sc->link_params.req_line_speed[cfg_idx] = req_line_speed;
    return (rc);
}

/* must be called under IF_ADDR_LOCK */
static int
bxe_init_mcast_macs_list(struct bxe_softc                 *sc,
                         struct ecore_mcast_ramrod_params *p)
{
    if_t ifp = sc->ifp;
    int mc_count = 0;
    struct ifmultiaddr *ifma;
    struct ecore_mcast_list_elem *mc_mac;

    CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
        if (ifma->ifma_addr->sa_family != AF_LINK) {
            continue;
        }

        mc_count++;
    }

    ECORE_LIST_INIT(&p->mcast_list);
    p->mcast_list_len = 0;

    if (!mc_count) {
        return (0);
    }

    mc_mac = malloc(sizeof(*mc_mac) * mc_count, M_DEVBUF,
                    (M_NOWAIT | M_ZERO));
    if (!mc_mac) {
        BLOGE(sc, "Failed to allocate temp mcast list\n");
        return (-1);
    }
    bzero(mc_mac, (sizeof(*mc_mac) * mc_count));

    CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
        if (ifma->ifma_addr->sa_family != AF_LINK) {
            continue;
        }

        mc_mac->mac = (uint8_t *)LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
        ECORE_LIST_PUSH_TAIL(&mc_mac->link, &p->mcast_list);

        BLOGD(sc, DBG_LOAD,
              "Setting MCAST %02X:%02X:%02X:%02X:%02X:%02X and mc_count %d\n",
              mc_mac->mac[0], mc_mac->mac[1], mc_mac->mac[2],
              mc_mac->mac[3], mc_mac->mac[4], mc_mac->mac[5], mc_count);
       mc_mac++;
    }

    p->mcast_list_len = mc_count;

    return (0);
}

static void
bxe_free_mcast_macs_list(struct ecore_mcast_ramrod_params *p)
{
    struct ecore_mcast_list_elem *mc_mac =
        ECORE_LIST_FIRST_ENTRY(&p->mcast_list,
                               struct ecore_mcast_list_elem,
                               link);

    if (mc_mac) {
        /* only a single free as all mc_macs are in the same heap array */
        free(mc_mac, M_DEVBUF);
    }
}
static int
bxe_set_mc_list(struct bxe_softc *sc)
{
    struct ecore_mcast_ramrod_params rparam = { NULL };
    int rc = 0;

    rparam.mcast_obj = &sc->mcast_obj;

    BXE_MCAST_LOCK(sc);

    /* first, clear all configured multicast MACs */
    rc = ecore_config_mcast(sc, &rparam, ECORE_MCAST_CMD_DEL);
    if (rc < 0) {
        BLOGE(sc, "Failed to clear multicast configuration: %d\n", rc);
        /* Manual backport parts of FreeBSD upstream r284470. */
        BXE_MCAST_UNLOCK(sc);
        return (rc);
    }

    /* configure a new MACs list */
    rc = bxe_init_mcast_macs_list(sc, &rparam);
    if (rc) {
        BLOGE(sc, "Failed to create mcast MACs list (%d)\n", rc);
        BXE_MCAST_UNLOCK(sc);
        return (rc);
    }

    /* Now add the new MACs */
    rc = ecore_config_mcast(sc, &rparam, ECORE_MCAST_CMD_ADD);
    if (rc < 0) {
        BLOGE(sc, "Failed to set new mcast config (%d)\n", rc);
    }

    bxe_free_mcast_macs_list(&rparam);

    BXE_MCAST_UNLOCK(sc);

    return (rc);
}

static int
bxe_set_uc_list(struct bxe_softc *sc)
{
    if_t ifp = sc->ifp;
    struct ecore_vlan_mac_obj *mac_obj = &sc->sp_objs->mac_obj;
    struct ifaddr *ifa;
    unsigned long ramrod_flags = 0;
    int rc;

#if __FreeBSD_version < 800000
    IF_ADDR_LOCK(ifp);
#else
    if_addr_rlock(ifp);
#endif

    /* first schedule a cleanup up of old configuration */
    rc = bxe_del_all_macs(sc, mac_obj, ECORE_UC_LIST_MAC, FALSE);
    if (rc < 0) {
        BLOGE(sc, "Failed to schedule delete of all ETH MACs (%d)\n", rc);
#if __FreeBSD_version < 800000
        IF_ADDR_UNLOCK(ifp);
#else
        if_addr_runlock(ifp);
#endif
        return (rc);
    }

    ifa = if_getifaddr(ifp); /* XXX Is this structure */
    while (ifa) {
        if (ifa->ifa_addr->sa_family != AF_LINK) {
            ifa = CK_STAILQ_NEXT(ifa, ifa_link);
            continue;
        }

        rc = bxe_set_mac_one(sc, (uint8_t *)LLADDR((struct sockaddr_dl *)ifa->ifa_addr),
                             mac_obj, TRUE, ECORE_UC_LIST_MAC, &ramrod_flags);
        if (rc == -EEXIST) {
            BLOGD(sc, DBG_SP, "Failed to schedule ADD operations (EEXIST)\n");
            /* do not treat adding same MAC as an error */
            rc = 0;
        } else if (rc < 0) {
            BLOGE(sc, "Failed to schedule ADD operations (%d)\n", rc);
#if __FreeBSD_version < 800000
            IF_ADDR_UNLOCK(ifp);
#else
            if_addr_runlock(ifp);
#endif
            return (rc);
        }

        ifa = CK_STAILQ_NEXT(ifa, ifa_link);
    }

#if __FreeBSD_version < 800000
    IF_ADDR_UNLOCK(ifp);
#else
    if_addr_runlock(ifp);
#endif

    /* Execute the pending commands */
    bit_set(&ramrod_flags, RAMROD_CONT);
    return (bxe_set_mac_one(sc, NULL, mac_obj, FALSE /* don't care */,
                            ECORE_UC_LIST_MAC, &ramrod_flags));
}

static void
bxe_set_rx_mode(struct bxe_softc *sc)
{
    if_t ifp = sc->ifp;
    uint32_t rx_mode = BXE_RX_MODE_NORMAL;

    if (sc->state != BXE_STATE_OPEN) {
        BLOGD(sc, DBG_SP, "state is %x, returning\n", sc->state);
        return;
    }

    BLOGD(sc, DBG_SP, "if_flags(ifp)=0x%x\n", if_getflags(sc->ifp));

    if (if_getflags(ifp) & IFF_PROMISC) {
        rx_mode = BXE_RX_MODE_PROMISC;
    } else if ((if_getflags(ifp) & IFF_ALLMULTI) ||
               ((if_getamcount(ifp) > BXE_MAX_MULTICAST) &&
                CHIP_IS_E1(sc))) {
        rx_mode = BXE_RX_MODE_ALLMULTI;
    } else {
        if (IS_PF(sc)) {
            /* some multicasts */
            if (bxe_set_mc_list(sc) < 0) {
                rx_mode = BXE_RX_MODE_ALLMULTI;
            }
            if (bxe_set_uc_list(sc) < 0) {
                rx_mode = BXE_RX_MODE_PROMISC;
            }
        }
    }

    sc->rx_mode = rx_mode;

    /* schedule the rx_mode command */
    if (bxe_test_bit(ECORE_FILTER_RX_MODE_PENDING, &sc->sp_state)) {
        BLOGD(sc, DBG_LOAD, "Scheduled setting rx_mode with ECORE...\n");
        bxe_set_bit(ECORE_FILTER_RX_MODE_SCHED, &sc->sp_state);
        return;
    }

    if (IS_PF(sc)) {
        bxe_set_storm_rx_mode(sc);
    }
}


/* update flags in shmem */
static void
bxe_update_drv_flags(struct bxe_softc *sc,
                     uint32_t         flags,
                     uint32_t         set)
{
    uint32_t drv_flags;

    if (SHMEM2_HAS(sc, drv_flags)) {
        bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_DRV_FLAGS);
        drv_flags = SHMEM2_RD(sc, drv_flags);

        if (set) {
            SET_FLAGS(drv_flags, flags);
        } else {
            RESET_FLAGS(drv_flags, flags);
        }

        SHMEM2_WR(sc, drv_flags, drv_flags);
        BLOGD(sc, DBG_LOAD, "drv_flags 0x%08x\n", drv_flags);

        bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_DRV_FLAGS);
    }
}

/* periodic timer callout routine, only runs when the interface is up */

static void
bxe_periodic_callout_func(void *xsc)
{
    struct bxe_softc *sc = (struct bxe_softc *)xsc;
    int i;

    if (!BXE_CORE_TRYLOCK(sc)) {
        /* just bail and try again next time */

        if ((sc->state == BXE_STATE_OPEN) &&
            (atomic_load_acq_long(&sc->periodic_flags) == PERIODIC_GO)) {
            /* schedule the next periodic callout */
            callout_reset(&sc->periodic_callout, hz,
                          bxe_periodic_callout_func, sc);
        }

        return;
    }

    if ((sc->state != BXE_STATE_OPEN) ||
        (atomic_load_acq_long(&sc->periodic_flags) == PERIODIC_STOP)) {
        BLOGW(sc, "periodic callout exit (state=0x%x)\n", sc->state);
        BXE_CORE_UNLOCK(sc);
        return;
        }


    /* Check for TX timeouts on any fastpath. */
    FOR_EACH_QUEUE(sc, i) {
        if (bxe_watchdog(sc, &sc->fp[i]) != 0) {
            /* Ruh-Roh, chip was reset! */
            break;
        }
    }

    if (!CHIP_REV_IS_SLOW(sc)) {
        /*
         * This barrier is needed to ensure the ordering between the writing
         * to the sc->port.pmf in the bxe_nic_load() or bxe_pmf_update() and
         * the reading here.
         */
        mb();
        if (sc->port.pmf) {
	    bxe_acquire_phy_lock(sc);
            elink_period_func(&sc->link_params, &sc->link_vars);
	    bxe_release_phy_lock(sc);
        }
    }

    if (IS_PF(sc) && !(sc->flags & BXE_NO_PULSE)) {
        int mb_idx = SC_FW_MB_IDX(sc);
        uint32_t drv_pulse;
        uint32_t mcp_pulse;

        ++sc->fw_drv_pulse_wr_seq;
        sc->fw_drv_pulse_wr_seq &= DRV_PULSE_SEQ_MASK;

        drv_pulse = sc->fw_drv_pulse_wr_seq;
        bxe_drv_pulse(sc);

        mcp_pulse = (SHMEM_RD(sc, func_mb[mb_idx].mcp_pulse_mb) &
                     MCP_PULSE_SEQ_MASK);

        /*
         * The delta between driver pulse and mcp response should
         * be 1 (before mcp response) or 0 (after mcp response).
         */
        if ((drv_pulse != mcp_pulse) &&
            (drv_pulse != ((mcp_pulse + 1) & MCP_PULSE_SEQ_MASK))) {
            /* someone lost a heartbeat... */
            BLOGE(sc, "drv_pulse (0x%x) != mcp_pulse (0x%x)\n",
                  drv_pulse, mcp_pulse);
        }
    }

    /* state is BXE_STATE_OPEN */
    bxe_stats_handle(sc, STATS_EVENT_UPDATE);

    BXE_CORE_UNLOCK(sc);

    if ((sc->state == BXE_STATE_OPEN) &&
        (atomic_load_acq_long(&sc->periodic_flags) == PERIODIC_GO)) {
        /* schedule the next periodic callout */
        callout_reset(&sc->periodic_callout, hz,
                      bxe_periodic_callout_func, sc);
    }
}

static void
bxe_periodic_start(struct bxe_softc *sc)
{
    atomic_store_rel_long(&sc->periodic_flags, PERIODIC_GO);
    callout_reset(&sc->periodic_callout, hz, bxe_periodic_callout_func, sc);
}

static void
bxe_periodic_stop(struct bxe_softc *sc)
{
    atomic_store_rel_long(&sc->periodic_flags, PERIODIC_STOP);
    callout_drain(&sc->periodic_callout);
}

void
bxe_parity_recover(struct bxe_softc *sc)
{
    uint8_t global = FALSE;
    uint32_t error_recovered, error_unrecovered;
    bool is_parity;


    if ((sc->recovery_state == BXE_RECOVERY_FAILED) &&
        (sc->state == BXE_STATE_ERROR)) {
        BLOGE(sc, "RECOVERY failed, "
            "stack notified driver is NOT running! "
            "Please reboot/power cycle the system.\n");
        return;
    }

    while (1) {
        BLOGD(sc, DBG_SP,
           "%s sc=%p state=0x%x rec_state=0x%x error_status=%x\n",
            __func__, sc, sc->state, sc->recovery_state, sc->error_status);

        switch(sc->recovery_state) {

        case BXE_RECOVERY_INIT:
            is_parity = bxe_chk_parity_attn(sc, &global, FALSE);

            if ((CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) ||
                (sc->error_status & BXE_ERR_MCP_ASSERT) ||
                (sc->error_status & BXE_ERR_GLOBAL)) {

                BXE_CORE_LOCK(sc);
                if (if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) {
                    bxe_periodic_stop(sc);
                }
                bxe_nic_unload(sc, UNLOAD_RECOVERY, false);
                sc->state = BXE_STATE_ERROR;
                sc->recovery_state = BXE_RECOVERY_FAILED;
                BLOGE(sc, " No Recovery tried for error 0x%x"
                    " stack notified driver is NOT running!"
                    " Please reboot/power cycle the system.\n",
                    sc->error_status);
                BXE_CORE_UNLOCK(sc);
                return;
            }


           /* Try to get a LEADER_LOCK HW lock */
            if (bxe_trylock_leader_lock(sc)) {

                bxe_set_reset_in_progress(sc);
                /*
                 * Check if there is a global attention and if
                 * there was a global attention, set the global
                 * reset bit.
                 */
                if (global) {
                    bxe_set_reset_global(sc);
                }
                sc->is_leader = 1;
            }

            /* If interface has been removed - break */

            if (if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) {
                bxe_periodic_stop(sc);
            }

            BXE_CORE_LOCK(sc);
            bxe_nic_unload(sc,UNLOAD_RECOVERY, false);
            sc->recovery_state = BXE_RECOVERY_WAIT;
            BXE_CORE_UNLOCK(sc);

            /*
             * Ensure "is_leader", MCP command sequence and
             * "recovery_state" update values are seen on other
             * CPUs.
             */
            mb();
            break;
        case BXE_RECOVERY_WAIT:

            if (sc->is_leader) {
                int other_engine = SC_PATH(sc) ? 0 : 1;
                bool other_load_status =
                    bxe_get_load_status(sc, other_engine);
                bool load_status =
                    bxe_get_load_status(sc, SC_PATH(sc));
                global = bxe_reset_is_global(sc);

                /*
                 * In case of a parity in a global block, let
                 * the first leader that performs a
                 * leader_reset() reset the global blocks in
                 * order to clear global attentions. Otherwise
                 * the gates will remain closed for that
                 * engine.
                 */
                if (load_status ||
                    (global && other_load_status)) {
                    /*
                     * Wait until all other functions get
                     * down.
                     */
                    taskqueue_enqueue_timeout(taskqueue_thread,
                        &sc->sp_err_timeout_task, hz/10);
                    return;
                } else {
                    /*
                     * If all other functions got down
                     * try to bring the chip back to
                     * normal. In any case it's an exit
                     * point for a leader.
                     */
                    if (bxe_leader_reset(sc)) {
                        BLOGE(sc, "RECOVERY failed, "
                            "stack notified driver is NOT running!\n");
                        sc->recovery_state = BXE_RECOVERY_FAILED;
                        sc->state = BXE_STATE_ERROR;
                        mb();
                        return;
                    }

                    /*
                     * If we are here, means that the
                     * leader has succeeded and doesn't
                     * want to be a leader any more. Try
                     * to continue as a none-leader.
                     */
                break;
                }

            } else { /* non-leader */
                if (!bxe_reset_is_done(sc, SC_PATH(sc))) {
                    /*
                     * Try to get a LEADER_LOCK HW lock as
                     * long as a former leader may have
                     * been unloaded by the user or
                     * released a leadership by another
                     * reason.
                     */
                    if (bxe_trylock_leader_lock(sc)) {
                        /*
                         * I'm a leader now! Restart a
                         * switch case.
                         */
                        sc->is_leader = 1;
                        break;
                    }

                    taskqueue_enqueue_timeout(taskqueue_thread,
                        &sc->sp_err_timeout_task, hz/10);
                    return;

                } else {
                    /*
                     * If there was a global attention, wait
                     * for it to be cleared.
                     */
                    if (bxe_reset_is_global(sc)) {
                        taskqueue_enqueue_timeout(taskqueue_thread,
                            &sc->sp_err_timeout_task, hz/10);
                        return;
                     }

                     error_recovered =
                         sc->eth_stats.recoverable_error;
                     error_unrecovered =
                         sc->eth_stats.unrecoverable_error;
                     BXE_CORE_LOCK(sc);
                     sc->recovery_state =
                         BXE_RECOVERY_NIC_LOADING;
                     if (bxe_nic_load(sc, LOAD_NORMAL)) {
                         error_unrecovered++;
                         sc->recovery_state = BXE_RECOVERY_FAILED;
                         sc->state = BXE_STATE_ERROR;
                         BLOGE(sc, "Recovery is NOT successfull, "
                            " state=0x%x recovery_state=0x%x error=%x\n",
                            sc->state, sc->recovery_state, sc->error_status);
                         sc->error_status = 0;
                     } else {
                         sc->recovery_state =
                             BXE_RECOVERY_DONE;
                         error_recovered++;
                         BLOGI(sc, "Recovery is successfull from errors %x,"
                            " state=0x%x"
                            " recovery_state=0x%x \n", sc->error_status,
                            sc->state, sc->recovery_state);
                         mb();
                     }
                     sc->error_status = 0;
                     BXE_CORE_UNLOCK(sc);
                     sc->eth_stats.recoverable_error =
                         error_recovered;
                     sc->eth_stats.unrecoverable_error =
                         error_unrecovered;

                     return;
                 }
             }
         default:
             return;
         }
    }
}
void
bxe_handle_error(struct bxe_softc * sc)
{

    if(sc->recovery_state == BXE_RECOVERY_WAIT) {
        return;
    }
    if(sc->error_status) {
        if (sc->state == BXE_STATE_OPEN)  {
            bxe_int_disable(sc);
        }
        if (sc->link_vars.link_up) {
            if_link_state_change(sc->ifp, LINK_STATE_DOWN);
        }
        sc->recovery_state = BXE_RECOVERY_INIT;
        BLOGI(sc, "bxe%d: Recovery started errors 0x%x recovery state 0x%x\n",
            sc->unit, sc->error_status, sc->recovery_state);
        bxe_parity_recover(sc);
   }
}

static void
bxe_sp_err_timeout_task(void *arg, int pending)
{

    struct bxe_softc *sc = (struct bxe_softc *)arg;

    BLOGD(sc, DBG_SP,
        "%s state = 0x%x rec state=0x%x error_status=%x\n",
        __func__, sc->state, sc->recovery_state, sc->error_status);

    if((sc->recovery_state == BXE_RECOVERY_FAILED) &&
       (sc->state == BXE_STATE_ERROR)) {
        return;
    }
    /* if can be taken */
    if ((sc->error_status) && (sc->trigger_grcdump)) {
        bxe_grc_dump(sc);
    }
    if (sc->recovery_state != BXE_RECOVERY_DONE) {
        bxe_handle_error(sc);
        bxe_parity_recover(sc);
    } else if (sc->error_status) {
        bxe_handle_error(sc);
    }

    return;
}

/* start the controller */
static __noinline int
bxe_nic_load(struct bxe_softc *sc,
             int              load_mode)
{
    uint32_t val;
    int load_code = 0;
    int i, rc = 0;

    BXE_CORE_LOCK_ASSERT(sc);

    BLOGD(sc, DBG_LOAD, "Starting NIC load...\n");

    sc->state = BXE_STATE_OPENING_WAITING_LOAD;

    if (IS_PF(sc)) {
        /* must be called before memory allocation and HW init */
        bxe_ilt_set_info(sc);
    }

    sc->last_reported_link_state = LINK_STATE_UNKNOWN;

    bxe_set_fp_rx_buf_size(sc);

    if (bxe_alloc_fp_buffers(sc) != 0) {
        BLOGE(sc, "Failed to allocate fastpath memory\n");
        sc->state = BXE_STATE_CLOSED;
        rc = ENOMEM;
        goto bxe_nic_load_error0;
    }

    if (bxe_alloc_mem(sc) != 0) {
        sc->state = BXE_STATE_CLOSED;
        rc = ENOMEM;
        goto bxe_nic_load_error0;
    }

    if (bxe_alloc_fw_stats_mem(sc) != 0) {
        sc->state = BXE_STATE_CLOSED;
        rc = ENOMEM;
        goto bxe_nic_load_error0;
    }

    if (IS_PF(sc)) {
        /* set pf load just before approaching the MCP */
        bxe_set_pf_load(sc);

        /* if MCP exists send load request and analyze response */
        if (!BXE_NOMCP(sc)) {
            /* attempt to load pf */
            if (bxe_nic_load_request(sc, &load_code) != 0) {
                sc->state = BXE_STATE_CLOSED;
                rc = ENXIO;
                goto bxe_nic_load_error1;
            }

            /* what did the MCP say? */
            if (bxe_nic_load_analyze_req(sc, load_code) != 0) {
                bxe_fw_command(sc, DRV_MSG_CODE_LOAD_DONE, 0);
                sc->state = BXE_STATE_CLOSED;
                rc = ENXIO;
                goto bxe_nic_load_error2;
            }
        } else {
            BLOGI(sc, "Device has no MCP!\n");
            load_code = bxe_nic_load_no_mcp(sc);
        }

        /* mark PMF if applicable */
        bxe_nic_load_pmf(sc, load_code);

        /* Init Function state controlling object */
        bxe_init_func_obj(sc);

        /* Initialize HW */
        if (bxe_init_hw(sc, load_code) != 0) {
            BLOGE(sc, "HW init failed\n");
            bxe_fw_command(sc, DRV_MSG_CODE_LOAD_DONE, 0);
            sc->state = BXE_STATE_CLOSED;
            rc = ENXIO;
            goto bxe_nic_load_error2;
        }
    }

    /* set ALWAYS_ALIVE bit in shmem */
    sc->fw_drv_pulse_wr_seq |= DRV_PULSE_ALWAYS_ALIVE;
    bxe_drv_pulse(sc);
    sc->flags |= BXE_NO_PULSE;

    /* attach interrupts */
    if (bxe_interrupt_attach(sc) != 0) {
        sc->state = BXE_STATE_CLOSED;
        rc = ENXIO;
        goto bxe_nic_load_error2;
    }

    bxe_nic_init(sc, load_code);

    /* Init per-function objects */
    if (IS_PF(sc)) {
        bxe_init_objs(sc);
        // XXX bxe_iov_nic_init(sc);

        /* set AFEX default VLAN tag to an invalid value */
        sc->devinfo.mf_info.afex_def_vlan_tag = -1;
        // XXX bxe_nic_load_afex_dcc(sc, load_code);

        sc->state = BXE_STATE_OPENING_WAITING_PORT;
        rc = bxe_func_start(sc);
        if (rc) {
            BLOGE(sc, "Function start failed! rc = %d\n", rc);
            bxe_fw_command(sc, DRV_MSG_CODE_LOAD_DONE, 0);
            sc->state = BXE_STATE_ERROR;
            goto bxe_nic_load_error3;
        }

        /* send LOAD_DONE command to MCP */
        if (!BXE_NOMCP(sc)) {
            load_code = bxe_fw_command(sc, DRV_MSG_CODE_LOAD_DONE, 0);
            if (!load_code) {
                BLOGE(sc, "MCP response failure, aborting\n");
                sc->state = BXE_STATE_ERROR;
                rc = ENXIO;
                goto bxe_nic_load_error3;
            }
        }

        rc = bxe_setup_leading(sc);
        if (rc) {
            BLOGE(sc, "Setup leading failed! rc = %d\n", rc);
            sc->state = BXE_STATE_ERROR;
            goto bxe_nic_load_error3;
        }

        FOR_EACH_NONDEFAULT_ETH_QUEUE(sc, i) {
            rc = bxe_setup_queue(sc, &sc->fp[i], FALSE);
            if (rc) {
                BLOGE(sc, "Queue(%d) setup failed rc = %d\n", i, rc);
                sc->state = BXE_STATE_ERROR;
                goto bxe_nic_load_error3;
            }
        }

        rc = bxe_init_rss_pf(sc);
        if (rc) {
            BLOGE(sc, "PF RSS init failed\n");
            sc->state = BXE_STATE_ERROR;
            goto bxe_nic_load_error3;
        }
    }
    /* XXX VF */

    /* now when Clients are configured we are ready to work */
    sc->state = BXE_STATE_OPEN;

    /* Configure a ucast MAC */
    if (IS_PF(sc)) {
        rc = bxe_set_eth_mac(sc, TRUE);
    }
    if (rc) {
        BLOGE(sc, "Setting Ethernet MAC failed rc = %d\n", rc);
        sc->state = BXE_STATE_ERROR;
        goto bxe_nic_load_error3;
    }

    if (sc->port.pmf) {
        rc = bxe_initial_phy_init(sc, /* XXX load_mode */LOAD_OPEN);
        if (rc) {
            sc->state = BXE_STATE_ERROR;
            goto bxe_nic_load_error3;
        }
    }

    sc->link_params.feature_config_flags &=
        ~ELINK_FEATURE_CONFIG_BOOT_FROM_SAN;

    /* start fast path */

    /* Initialize Rx filter */
    bxe_set_rx_mode(sc);

    /* start the Tx */
    switch (/* XXX load_mode */LOAD_OPEN) {
    case LOAD_NORMAL:
    case LOAD_OPEN:
        break;

    case LOAD_DIAG:
    case LOAD_LOOPBACK_EXT:
        sc->state = BXE_STATE_DIAG;
        break;

    default:
        break;
    }

    if (sc->port.pmf) {
        bxe_update_drv_flags(sc, 1 << DRV_FLAGS_PORT_MASK, 0);
    } else {
        bxe_link_status_update(sc);
    }

    /* start the periodic timer callout */
    bxe_periodic_start(sc);

    if (IS_PF(sc) && SHMEM2_HAS(sc, drv_capabilities_flag)) {
        /* mark driver is loaded in shmem2 */
        val = SHMEM2_RD(sc, drv_capabilities_flag[SC_FW_MB_IDX(sc)]);
        SHMEM2_WR(sc, drv_capabilities_flag[SC_FW_MB_IDX(sc)],
                  (val |
                   DRV_FLAGS_CAPABILITIES_LOADED_SUPPORTED |
                   DRV_FLAGS_CAPABILITIES_LOADED_L2));
    }

    /* wait for all pending SP commands to complete */
    if (IS_PF(sc) && !bxe_wait_sp_comp(sc, ~0x0UL)) {
        BLOGE(sc, "Timeout waiting for all SPs to complete!\n");
        bxe_periodic_stop(sc);
        bxe_nic_unload(sc, UNLOAD_CLOSE, FALSE);
        return (ENXIO);
    }

    /* Tell the stack the driver is running! */
    if_setdrvflags(sc->ifp, IFF_DRV_RUNNING);

    BLOGD(sc, DBG_LOAD, "NIC successfully loaded\n");

    return (0);

bxe_nic_load_error3:

    if (IS_PF(sc)) {
        bxe_int_disable_sync(sc, 1);

        /* clean out queued objects */
        bxe_squeeze_objects(sc);
    }

    bxe_interrupt_detach(sc);

bxe_nic_load_error2:

    if (IS_PF(sc) && !BXE_NOMCP(sc)) {
        bxe_fw_command(sc, DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP, 0);
        bxe_fw_command(sc, DRV_MSG_CODE_UNLOAD_DONE, 0);
    }

    sc->port.pmf = 0;

bxe_nic_load_error1:

    /* clear pf_load status, as it was already set */
    if (IS_PF(sc)) {
        bxe_clear_pf_load(sc);
    }

bxe_nic_load_error0:

    bxe_free_fw_stats_mem(sc);
    bxe_free_fp_buffers(sc);
    bxe_free_mem(sc);

    return (rc);
}

static int
bxe_init_locked(struct bxe_softc *sc)
{
    int other_engine = SC_PATH(sc) ? 0 : 1;
    uint8_t other_load_status, load_status;
    uint8_t global = FALSE;
    int rc;

    BXE_CORE_LOCK_ASSERT(sc);

    /* check if the driver is already running */
    if (if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) {
        BLOGD(sc, DBG_LOAD, "Init called while driver is running!\n");
        return (0);
    }

    if((sc->state == BXE_STATE_ERROR) &&
        (sc->recovery_state == BXE_RECOVERY_FAILED)) {
        BLOGE(sc, "Initialization not done, "
                  "as previous recovery failed."
                  "Reboot/Power-cycle the system\n" );
        return (ENXIO);
    }


    bxe_set_power_state(sc, PCI_PM_D0);

    /*
     * If parity occurred during the unload, then attentions and/or
     * RECOVERY_IN_PROGRES may still be set. If so we want the first function
     * loaded on the current engine to complete the recovery. Parity recovery
     * is only relevant for PF driver.
     */
    if (IS_PF(sc)) {
        other_load_status = bxe_get_load_status(sc, other_engine);
        load_status = bxe_get_load_status(sc, SC_PATH(sc));

        if (!bxe_reset_is_done(sc, SC_PATH(sc)) ||
            bxe_chk_parity_attn(sc, &global, TRUE)) {
            do {
                /*
                 * If there are attentions and they are in global blocks, set
                 * the GLOBAL_RESET bit regardless whether it will be this
                 * function that will complete the recovery or not.
                 */
                if (global) {
                    bxe_set_reset_global(sc);
                }

                /*
                 * Only the first function on the current engine should try
                 * to recover in open. In case of attentions in global blocks
                 * only the first in the chip should try to recover.
                 */
                if ((!load_status && (!global || !other_load_status)) &&
                    bxe_trylock_leader_lock(sc) && !bxe_leader_reset(sc)) {
                    BLOGI(sc, "Recovered during init\n");
                    break;
                }

                /* recovery has failed... */
                bxe_set_power_state(sc, PCI_PM_D3hot);
                sc->recovery_state = BXE_RECOVERY_FAILED;

                BLOGE(sc, "Recovery flow hasn't properly "
                          "completed yet, try again later. "
                          "If you still see this message after a "
                          "few retries then power cycle is required.\n");

                rc = ENXIO;
                goto bxe_init_locked_done;
            } while (0);
        }
    }

    sc->recovery_state = BXE_RECOVERY_DONE;

    rc = bxe_nic_load(sc, LOAD_OPEN);

bxe_init_locked_done:

    if (rc) {
        /* Tell the stack the driver is NOT running! */
        BLOGE(sc, "Initialization failed, "
                  "stack notified driver is NOT running!\n");
	if_setdrvflagbits(sc->ifp, 0, IFF_DRV_RUNNING);
    }

    return (rc);
}

static int
bxe_stop_locked(struct bxe_softc *sc)
{
    BXE_CORE_LOCK_ASSERT(sc);
    return (bxe_nic_unload(sc, UNLOAD_NORMAL, TRUE));
}

/*
 * Handles controller initialization when called from an unlocked routine.
 * ifconfig calls this function.
 *
 * Returns:
 *   void
 */
static void
bxe_init(void *xsc)
{
    struct bxe_softc *sc = (struct bxe_softc *)xsc;

    BXE_CORE_LOCK(sc);
    bxe_init_locked(sc);
    BXE_CORE_UNLOCK(sc);
}

static int
bxe_init_ifnet(struct bxe_softc *sc)
{
    if_t ifp;
    int capabilities;

    /* ifconfig entrypoint for media type/status reporting */
    ifmedia_init(&sc->ifmedia, IFM_IMASK,
                 bxe_ifmedia_update,
                 bxe_ifmedia_status);

    /* set the default interface values */
    ifmedia_add(&sc->ifmedia, (IFM_ETHER | IFM_FDX | sc->media), 0, NULL);
    ifmedia_add(&sc->ifmedia, (IFM_ETHER | IFM_AUTO), 0, NULL);
    ifmedia_set(&sc->ifmedia, (IFM_ETHER | IFM_AUTO));

    sc->ifmedia.ifm_media = sc->ifmedia.ifm_cur->ifm_media; /* XXX ? */
	BLOGI(sc, "IFMEDIA flags : %x\n", sc->ifmedia.ifm_media);

    /* allocate the ifnet structure */
    if ((ifp = if_gethandle(IFT_ETHER)) == NULL) {
        BLOGE(sc, "Interface allocation failed!\n");
        return (ENXIO);
    }

    if_setsoftc(ifp, sc);
    if_initname(ifp, device_get_name(sc->dev), device_get_unit(sc->dev));
    if_setflags(ifp, (IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST));
    if_setioctlfn(ifp, bxe_ioctl);
    if_setstartfn(ifp, bxe_tx_start);
    if_setgetcounterfn(ifp, bxe_get_counter);
#if __FreeBSD_version >= 901504
    if_settransmitfn(ifp, bxe_tx_mq_start);
    if_setqflushfn(ifp, bxe_mq_flush);
#endif
#ifdef FreeBSD8_0
    if_settimer(ifp, 0);
#endif
    if_setinitfn(ifp, bxe_init);
    if_setmtu(ifp, sc->mtu);
    if_sethwassist(ifp, (CSUM_IP      |
                        CSUM_TCP      |
                        CSUM_UDP      |
                        CSUM_TSO      |
                        CSUM_TCP_IPV6 |
                        CSUM_UDP_IPV6));

    capabilities =
#if __FreeBSD_version < 700000
        (IFCAP_VLAN_MTU       |
         IFCAP_VLAN_HWTAGGING |
         IFCAP_HWCSUM         |
         IFCAP_JUMBO_MTU      |
         IFCAP_LRO);
#else
        (IFCAP_VLAN_MTU       |
         IFCAP_VLAN_HWTAGGING |
         IFCAP_VLAN_HWTSO     |
         IFCAP_VLAN_HWFILTER  |
         IFCAP_VLAN_HWCSUM    |
         IFCAP_HWCSUM         |
         IFCAP_JUMBO_MTU      |
         IFCAP_LRO            |
         IFCAP_TSO4           |
         IFCAP_TSO6           |
         IFCAP_WOL_MAGIC);
#endif
    if_setcapabilitiesbit(ifp, capabilities, 0); /* XXX */
    if_setcapenable(ifp, if_getcapabilities(ifp));
    if_setbaudrate(ifp, IF_Gbps(10));
/* XXX */
    if_setsendqlen(ifp, sc->tx_ring_size);
    if_setsendqready(ifp);
/* XXX */

    sc->ifp = ifp;

    /* attach to the Ethernet interface list */
    ether_ifattach(ifp, sc->link_params.mac_addr);

    /* Attach driver netdump methods. */
    NETDUMP_SET(ifp, bxe);

    return (0);
}

static void
bxe_deallocate_bars(struct bxe_softc *sc)
{
    int i;

    for (i = 0; i < MAX_BARS; i++) {
        if (sc->bar[i].resource != NULL) {
            bus_release_resource(sc->dev,
                                 SYS_RES_MEMORY,
                                 sc->bar[i].rid,
                                 sc->bar[i].resource);
            BLOGD(sc, DBG_LOAD, "Released PCI BAR%d [%02x] memory\n",
                  i, PCIR_BAR(i));
        }
    }
}

static int
bxe_allocate_bars(struct bxe_softc *sc)
{
    u_int flags;
    int i;

    memset(sc->bar, 0, sizeof(sc->bar));

    for (i = 0; i < MAX_BARS; i++) {

        /* memory resources reside at BARs 0, 2, 4 */
        /* Run `pciconf -lb` to see mappings */
        if ((i != 0) && (i != 2) && (i != 4)) {
            continue;
        }

        sc->bar[i].rid = PCIR_BAR(i);

        flags = RF_ACTIVE;
        if (i == 0) {
            flags |= RF_SHAREABLE;
        }

        if ((sc->bar[i].resource =
             bus_alloc_resource_any(sc->dev,
                                    SYS_RES_MEMORY,
                                    &sc->bar[i].rid,
                                    flags)) == NULL) {
            return (0);
        }

        sc->bar[i].tag    = rman_get_bustag(sc->bar[i].resource);
        sc->bar[i].handle = rman_get_bushandle(sc->bar[i].resource);
        sc->bar[i].kva    = (vm_offset_t)rman_get_virtual(sc->bar[i].resource);

        BLOGI(sc, "PCI BAR%d [%02x] memory allocated: %#jx-%#jx (%jd) -> %#jx\n",
              i, PCIR_BAR(i),
              rman_get_start(sc->bar[i].resource),
              rman_get_end(sc->bar[i].resource),
              rman_get_size(sc->bar[i].resource),
              (uintmax_t)sc->bar[i].kva);
    }

    return (0);
}

static void
bxe_get_function_num(struct bxe_softc *sc)
{
    uint32_t val = 0;

    /*
     * Read the ME register to get the function number. The ME register
     * holds the relative-function number and absolute-function number. The
     * absolute-function number appears only in E2 and above. Before that
     * these bits always contained zero, therefore we cannot blindly use them.
     */

    val = REG_RD(sc, BAR_ME_REGISTER);

    sc->pfunc_rel =
        (uint8_t)((val & ME_REG_PF_NUM) >> ME_REG_PF_NUM_SHIFT);
    sc->path_id =
        (uint8_t)((val & ME_REG_ABS_PF_NUM) >> ME_REG_ABS_PF_NUM_SHIFT) & 1;

    if (CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) {
        sc->pfunc_abs = ((sc->pfunc_rel << 1) | sc->path_id);
    } else {
        sc->pfunc_abs = (sc->pfunc_rel | sc->path_id);
    }

    BLOGD(sc, DBG_LOAD,
          "Relative function %d, Absolute function %d, Path %d\n",
          sc->pfunc_rel, sc->pfunc_abs, sc->path_id);
}

static uint32_t
bxe_get_shmem_mf_cfg_base(struct bxe_softc *sc)
{
    uint32_t shmem2_size;
    uint32_t offset;
    uint32_t mf_cfg_offset_value;

    /* Non 57712 */
    offset = (SHMEM_RD(sc, func_mb) +
              (MAX_FUNC_NUM * sizeof(struct drv_func_mb)));

    /* 57712 plus */
    if (sc->devinfo.shmem2_base != 0) {
        shmem2_size = SHMEM2_RD(sc, size);
        if (shmem2_size > offsetof(struct shmem2_region, mf_cfg_addr)) {
            mf_cfg_offset_value = SHMEM2_RD(sc, mf_cfg_addr);
            if (SHMEM_MF_CFG_ADDR_NONE != mf_cfg_offset_value) {
                offset = mf_cfg_offset_value;
            }
        }
    }

    return (offset);
}

static uint32_t
bxe_pcie_capability_read(struct bxe_softc *sc,
                         int    reg,
                         int    width)
{
    int pcie_reg;

    /* ensure PCIe capability is enabled */
    if (pci_find_cap(sc->dev, PCIY_EXPRESS, &pcie_reg) == 0) {
        if (pcie_reg != 0) {
            BLOGD(sc, DBG_LOAD, "PCIe capability at 0x%04x\n", pcie_reg);
            return (pci_read_config(sc->dev, (pcie_reg + reg), width));
        }
    }

    BLOGE(sc, "PCIe capability NOT FOUND!!!\n");

    return (0);
}

static uint8_t
bxe_is_pcie_pending(struct bxe_softc *sc)
{
    return (bxe_pcie_capability_read(sc, PCIR_EXPRESS_DEVICE_STA, 2) &
            PCIM_EXP_STA_TRANSACTION_PND);
}

/*
 * Walk the PCI capabiites list for the device to find what features are
 * supported. These capabilites may be enabled/disabled by firmware so it's
 * best to walk the list rather than make assumptions.
 */
static void
bxe_probe_pci_caps(struct bxe_softc *sc)
{
    uint16_t link_status;
    int reg;

    /* check if PCI Power Management is enabled */
    if (pci_find_cap(sc->dev, PCIY_PMG, &reg) == 0) {
        if (reg != 0) {
            BLOGD(sc, DBG_LOAD, "Found PM capability at 0x%04x\n", reg);

            sc->devinfo.pcie_cap_flags |= BXE_PM_CAPABLE_FLAG;
            sc->devinfo.pcie_pm_cap_reg = (uint16_t)reg;
        }
    }

    link_status = bxe_pcie_capability_read(sc, PCIR_EXPRESS_LINK_STA, 2);

    /* handle PCIe 2.0 workarounds for 57710 */
    if (CHIP_IS_E1(sc)) {
        /* workaround for 57710 errata E4_57710_27462 */
        sc->devinfo.pcie_link_speed =
            (REG_RD(sc, 0x3d04) & (1 << 24)) ? 2 : 1;

        /* workaround for 57710 errata E4_57710_27488 */
        sc->devinfo.pcie_link_width =
            ((link_status & PCIM_LINK_STA_WIDTH) >> 4);
        if (sc->devinfo.pcie_link_speed > 1) {
            sc->devinfo.pcie_link_width =
                ((link_status & PCIM_LINK_STA_WIDTH) >> 4) >> 1;
        }
    } else {
        sc->devinfo.pcie_link_speed =
            (link_status & PCIM_LINK_STA_SPEED);
        sc->devinfo.pcie_link_width =
            ((link_status & PCIM_LINK_STA_WIDTH) >> 4);
    }

    BLOGD(sc, DBG_LOAD, "PCIe link speed=%d width=%d\n",
          sc->devinfo.pcie_link_speed, sc->devinfo.pcie_link_width);

    sc->devinfo.pcie_cap_flags |= BXE_PCIE_CAPABLE_FLAG;
    sc->devinfo.pcie_pcie_cap_reg = (uint16_t)reg;

    /* check if MSI capability is enabled */
    if (pci_find_cap(sc->dev, PCIY_MSI, &reg) == 0) {
        if (reg != 0) {
            BLOGD(sc, DBG_LOAD, "Found MSI capability at 0x%04x\n", reg);

            sc->devinfo.pcie_cap_flags |= BXE_MSI_CAPABLE_FLAG;
            sc->devinfo.pcie_msi_cap_reg = (uint16_t)reg;
        }
    }

    /* check if MSI-X capability is enabled */
    if (pci_find_cap(sc->dev, PCIY_MSIX, &reg) == 0) {
        if (reg != 0) {
            BLOGD(sc, DBG_LOAD, "Found MSI-X capability at 0x%04x\n", reg);

            sc->devinfo.pcie_cap_flags |= BXE_MSIX_CAPABLE_FLAG;
            sc->devinfo.pcie_msix_cap_reg = (uint16_t)reg;
        }
    }
}

static int
bxe_get_shmem_mf_cfg_info_sd(struct bxe_softc *sc)
{
    struct bxe_mf_info *mf_info = &sc->devinfo.mf_info;
    uint32_t val;

    /* get the outer vlan if we're in switch-dependent mode */

    val = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].e1hov_tag);
    mf_info->ext_id = (uint16_t)val;

    mf_info->multi_vnics_mode = 1;

    if (!VALID_OVLAN(mf_info->ext_id)) {
        BLOGE(sc, "Invalid VLAN (%d)\n", mf_info->ext_id);
        return (1);
    }

    /* get the capabilities */
    if ((mf_info->mf_config[SC_VN(sc)] & FUNC_MF_CFG_PROTOCOL_MASK) ==
        FUNC_MF_CFG_PROTOCOL_ISCSI) {
        mf_info->mf_protos_supported |= MF_PROTO_SUPPORT_ISCSI;
    } else if ((mf_info->mf_config[SC_VN(sc)] & FUNC_MF_CFG_PROTOCOL_MASK) ==
               FUNC_MF_CFG_PROTOCOL_FCOE) {
        mf_info->mf_protos_supported |= MF_PROTO_SUPPORT_FCOE;
    } else {
        mf_info->mf_protos_supported |= MF_PROTO_SUPPORT_ETHERNET;
    }

    mf_info->vnics_per_port =
        (CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) ? 2 : 4;

    return (0);
}

static uint32_t
bxe_get_shmem_ext_proto_support_flags(struct bxe_softc *sc)
{
    uint32_t retval = 0;
    uint32_t val;

    val = MFCFG_RD(sc, func_ext_config[SC_ABS_FUNC(sc)].func_cfg);

    if (val & MACP_FUNC_CFG_FLAGS_ENABLED) {
        if (val & MACP_FUNC_CFG_FLAGS_ETHERNET) {
            retval |= MF_PROTO_SUPPORT_ETHERNET;
        }
        if (val & MACP_FUNC_CFG_FLAGS_ISCSI_OFFLOAD) {
            retval |= MF_PROTO_SUPPORT_ISCSI;
        }
        if (val & MACP_FUNC_CFG_FLAGS_FCOE_OFFLOAD) {
            retval |= MF_PROTO_SUPPORT_FCOE;
        }
    }

    return (retval);
}

static int
bxe_get_shmem_mf_cfg_info_si(struct bxe_softc *sc)
{
    struct bxe_mf_info *mf_info = &sc->devinfo.mf_info;
    uint32_t val;

    /*
     * There is no outer vlan if we're in switch-independent mode.
     * If the mac is valid then assume multi-function.
     */

    val = MFCFG_RD(sc, func_ext_config[SC_ABS_FUNC(sc)].func_cfg);

    mf_info->multi_vnics_mode = ((val & MACP_FUNC_CFG_FLAGS_MASK) != 0);

    mf_info->mf_protos_supported = bxe_get_shmem_ext_proto_support_flags(sc);

    mf_info->vnics_per_port =
        (CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) ? 2 : 4;

    return (0);
}

static int
bxe_get_shmem_mf_cfg_info_niv(struct bxe_softc *sc)
{
    struct bxe_mf_info *mf_info = &sc->devinfo.mf_info;
    uint32_t e1hov_tag;
    uint32_t func_config;
    uint32_t niv_config;

    mf_info->multi_vnics_mode = 1;

    e1hov_tag   = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].e1hov_tag);
    func_config = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].config);
    niv_config  = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].afex_config);

    mf_info->ext_id =
        (uint16_t)((e1hov_tag & FUNC_MF_CFG_E1HOV_TAG_MASK) >>
                   FUNC_MF_CFG_E1HOV_TAG_SHIFT);

    mf_info->default_vlan =
        (uint16_t)((e1hov_tag & FUNC_MF_CFG_AFEX_VLAN_MASK) >>
                   FUNC_MF_CFG_AFEX_VLAN_SHIFT);

    mf_info->niv_allowed_priorities =
        (uint8_t)((niv_config & FUNC_MF_CFG_AFEX_COS_FILTER_MASK) >>
                  FUNC_MF_CFG_AFEX_COS_FILTER_SHIFT);

    mf_info->niv_default_cos =
        (uint8_t)((func_config & FUNC_MF_CFG_TRANSMIT_PRIORITY_MASK) >>
                  FUNC_MF_CFG_TRANSMIT_PRIORITY_SHIFT);

    mf_info->afex_vlan_mode =
        ((niv_config & FUNC_MF_CFG_AFEX_VLAN_MODE_MASK) >>
         FUNC_MF_CFG_AFEX_VLAN_MODE_SHIFT);

    mf_info->niv_mba_enabled =
        ((niv_config & FUNC_MF_CFG_AFEX_MBA_ENABLED_MASK) >>
         FUNC_MF_CFG_AFEX_MBA_ENABLED_SHIFT);

    mf_info->mf_protos_supported = bxe_get_shmem_ext_proto_support_flags(sc);

    mf_info->vnics_per_port =
        (CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) ? 2 : 4;

    return (0);
}

static int
bxe_check_valid_mf_cfg(struct bxe_softc *sc)
{
    struct bxe_mf_info *mf_info = &sc->devinfo.mf_info;
    uint32_t mf_cfg1;
    uint32_t mf_cfg2;
    uint32_t ovlan1;
    uint32_t ovlan2;
    uint8_t i, j;

    BLOGD(sc, DBG_LOAD, "MF config parameters for function %d\n",
          SC_PORT(sc));
    BLOGD(sc, DBG_LOAD, "\tmf_config=0x%x\n",
          mf_info->mf_config[SC_VN(sc)]);
    BLOGD(sc, DBG_LOAD, "\tmulti_vnics_mode=%d\n",
          mf_info->multi_vnics_mode);
    BLOGD(sc, DBG_LOAD, "\tvnics_per_port=%d\n",
          mf_info->vnics_per_port);
    BLOGD(sc, DBG_LOAD, "\tovlan/vifid=%d\n",
          mf_info->ext_id);
    BLOGD(sc, DBG_LOAD, "\tmin_bw=%d/%d/%d/%d\n",
          mf_info->min_bw[0], mf_info->min_bw[1],
          mf_info->min_bw[2], mf_info->min_bw[3]);
    BLOGD(sc, DBG_LOAD, "\tmax_bw=%d/%d/%d/%d\n",
          mf_info->max_bw[0], mf_info->max_bw[1],
          mf_info->max_bw[2], mf_info->max_bw[3]);
    BLOGD(sc, DBG_LOAD, "\tmac_addr: %s\n",
          sc->mac_addr_str);

    /* various MF mode sanity checks... */

    if (mf_info->mf_config[SC_VN(sc)] & FUNC_MF_CFG_FUNC_HIDE) {
        BLOGE(sc, "Enumerated function %d is marked as hidden\n",
              SC_PORT(sc));
        return (1);
    }

    if ((mf_info->vnics_per_port > 1) && !mf_info->multi_vnics_mode) {
        BLOGE(sc, "vnics_per_port=%d multi_vnics_mode=%d\n",
              mf_info->vnics_per_port, mf_info->multi_vnics_mode);
        return (1);
    }

    if (mf_info->mf_mode == MULTI_FUNCTION_SD) {
        /* vnic id > 0 must have valid ovlan in switch-dependent mode */
        if ((SC_VN(sc) > 0) && !VALID_OVLAN(OVLAN(sc))) {
            BLOGE(sc, "mf_mode=SD vnic_id=%d ovlan=%d\n",
                  SC_VN(sc), OVLAN(sc));
            return (1);
        }

        if (!VALID_OVLAN(OVLAN(sc)) && mf_info->multi_vnics_mode) {
            BLOGE(sc, "mf_mode=SD multi_vnics_mode=%d ovlan=%d\n",
                  mf_info->multi_vnics_mode, OVLAN(sc));
            return (1);
        }

        /*
         * Verify all functions are either MF or SF mode. If MF, make sure
         * sure that all non-hidden functions have a valid ovlan. If SF,
         * make sure that all non-hidden functions have an invalid ovlan.
         */
        FOREACH_ABS_FUNC_IN_PORT(sc, i) {
            mf_cfg1 = MFCFG_RD(sc, func_mf_config[i].config);
            ovlan1  = MFCFG_RD(sc, func_mf_config[i].e1hov_tag);
            if (!(mf_cfg1 & FUNC_MF_CFG_FUNC_HIDE) &&
                (((mf_info->multi_vnics_mode) && !VALID_OVLAN(ovlan1)) ||
                 ((!mf_info->multi_vnics_mode) && VALID_OVLAN(ovlan1)))) {
                BLOGE(sc, "mf_mode=SD function %d MF config "
                          "mismatch, multi_vnics_mode=%d ovlan=%d\n",
                      i, mf_info->multi_vnics_mode, ovlan1);
                return (1);
            }
        }

        /* Verify all funcs on the same port each have a different ovlan. */
        FOREACH_ABS_FUNC_IN_PORT(sc, i) {
            mf_cfg1 = MFCFG_RD(sc, func_mf_config[i].config);
            ovlan1  = MFCFG_RD(sc, func_mf_config[i].e1hov_tag);
            /* iterate from the next function on the port to the max func */
            for (j = i + 2; j < MAX_FUNC_NUM; j += 2) {
                mf_cfg2 = MFCFG_RD(sc, func_mf_config[j].config);
                ovlan2  = MFCFG_RD(sc, func_mf_config[j].e1hov_tag);
                if (!(mf_cfg1 & FUNC_MF_CFG_FUNC_HIDE) &&
                    VALID_OVLAN(ovlan1) &&
                    !(mf_cfg2 & FUNC_MF_CFG_FUNC_HIDE) &&
                    VALID_OVLAN(ovlan2) &&
                    (ovlan1 == ovlan2)) {
                    BLOGE(sc, "mf_mode=SD functions %d and %d "
                              "have the same ovlan (%d)\n",
                          i, j, ovlan1);
                    return (1);
                }
            }
        }
    } /* MULTI_FUNCTION_SD */

    return (0);
}

static int
bxe_get_mf_cfg_info(struct bxe_softc *sc)
{
    struct bxe_mf_info *mf_info = &sc->devinfo.mf_info;
    uint32_t val, mac_upper;
    uint8_t i, vnic;

    /* initialize mf_info defaults */
    mf_info->vnics_per_port   = 1;
    mf_info->multi_vnics_mode = FALSE;
    mf_info->path_has_ovlan   = FALSE;
    mf_info->mf_mode          = SINGLE_FUNCTION;

    if (!CHIP_IS_MF_CAP(sc)) {
        return (0);
    }

    if (sc->devinfo.mf_cfg_base == SHMEM_MF_CFG_ADDR_NONE) {
        BLOGE(sc, "Invalid mf_cfg_base!\n");
        return (1);
    }

    /* get the MF mode (switch dependent / independent / single-function) */

    val = SHMEM_RD(sc, dev_info.shared_feature_config.config);

    switch (val & SHARED_FEAT_CFG_FORCE_SF_MODE_MASK)
    {
    case SHARED_FEAT_CFG_FORCE_SF_MODE_SWITCH_INDEPT:

        mac_upper = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].mac_upper);

        /* check for legal upper mac bytes */
        if (mac_upper != FUNC_MF_CFG_UPPERMAC_DEFAULT) {
            mf_info->mf_mode = MULTI_FUNCTION_SI;
        } else {
            BLOGE(sc, "Invalid config for Switch Independent mode\n");
        }

        break;

    case SHARED_FEAT_CFG_FORCE_SF_MODE_MF_ALLOWED:
    case SHARED_FEAT_CFG_FORCE_SF_MODE_SPIO4:

        /* get outer vlan configuration */
        val = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].e1hov_tag);

        if ((val & FUNC_MF_CFG_E1HOV_TAG_MASK) !=
            FUNC_MF_CFG_E1HOV_TAG_DEFAULT) {
            mf_info->mf_mode = MULTI_FUNCTION_SD;
        } else {
            BLOGE(sc, "Invalid config for Switch Dependent mode\n");
        }

        break;

    case SHARED_FEAT_CFG_FORCE_SF_MODE_FORCED_SF:

        /* not in MF mode, vnics_per_port=1 and multi_vnics_mode=FALSE */
        return (0);

    case SHARED_FEAT_CFG_FORCE_SF_MODE_AFEX_MODE:

        /*
         * Mark MF mode as NIV if MCP version includes NPAR-SD support
         * and the MAC address is valid.
         */
        mac_upper = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].mac_upper);

        if ((SHMEM2_HAS(sc, afex_driver_support)) &&
            (mac_upper != FUNC_MF_CFG_UPPERMAC_DEFAULT)) {
            mf_info->mf_mode = MULTI_FUNCTION_AFEX;
        } else {
            BLOGE(sc, "Invalid config for AFEX mode\n");
        }

        break;

    default:

        BLOGE(sc, "Unknown MF mode (0x%08x)\n",
              (val & SHARED_FEAT_CFG_FORCE_SF_MODE_MASK));

        return (1);
    }

    /* set path mf_mode (which could be different than function mf_mode) */
    if (mf_info->mf_mode == MULTI_FUNCTION_SD) {
        mf_info->path_has_ovlan = TRUE;
    } else if (mf_info->mf_mode == SINGLE_FUNCTION) {
        /*
         * Decide on path multi vnics mode. If we're not in MF mode and in
         * 4-port mode, this is good enough to check vnic-0 of the other port
         * on the same path
         */
        if (CHIP_PORT_MODE(sc) == CHIP_4_PORT_MODE) {
            uint8_t other_port = !(PORT_ID(sc) & 1);
            uint8_t abs_func_other_port = (SC_PATH(sc) + (2 * other_port));

            val = MFCFG_RD(sc, func_mf_config[abs_func_other_port].e1hov_tag);

            mf_info->path_has_ovlan = VALID_OVLAN((uint16_t)val) ? 1 : 0;
        }
    }

    if (mf_info->mf_mode == SINGLE_FUNCTION) {
        /* invalid MF config */
        if (SC_VN(sc) >= 1) {
            BLOGE(sc, "VNIC ID >= 1 in SF mode\n");
            return (1);
        }

        return (0);
    }

    /* get the MF configuration */
    mf_info->mf_config[SC_VN(sc)] =
        MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].config);

    switch(mf_info->mf_mode)
    {
    case MULTI_FUNCTION_SD:

        bxe_get_shmem_mf_cfg_info_sd(sc);
        break;

    case MULTI_FUNCTION_SI:

        bxe_get_shmem_mf_cfg_info_si(sc);
        break;

    case MULTI_FUNCTION_AFEX:

        bxe_get_shmem_mf_cfg_info_niv(sc);
        break;

    default:

        BLOGE(sc, "Get MF config failed (mf_mode=0x%08x)\n",
              mf_info->mf_mode);
        return (1);
    }

    /* get the congestion management parameters */

    vnic = 0;
    FOREACH_ABS_FUNC_IN_PORT(sc, i) {
        /* get min/max bw */
        val = MFCFG_RD(sc, func_mf_config[i].config);
        mf_info->min_bw[vnic] =
            ((val & FUNC_MF_CFG_MIN_BW_MASK) >> FUNC_MF_CFG_MIN_BW_SHIFT);
        mf_info->max_bw[vnic] =
            ((val & FUNC_MF_CFG_MAX_BW_MASK) >> FUNC_MF_CFG_MAX_BW_SHIFT);
        vnic++;
    }

    return (bxe_check_valid_mf_cfg(sc));
}

static int
bxe_get_shmem_info(struct bxe_softc *sc)
{
    int port;
    uint32_t mac_hi, mac_lo, val;

    port = SC_PORT(sc);
    mac_hi = mac_lo = 0;

    sc->link_params.sc   = sc;
    sc->link_params.port = port;

    /* get the hardware config info */
    sc->devinfo.hw_config =
        SHMEM_RD(sc, dev_info.shared_hw_config.config);
    sc->devinfo.hw_config2 =
        SHMEM_RD(sc, dev_info.shared_hw_config.config2);

    sc->link_params.hw_led_mode =
        ((sc->devinfo.hw_config & SHARED_HW_CFG_LED_MODE_MASK) >>
         SHARED_HW_CFG_LED_MODE_SHIFT);

    /* get the port feature config */
    sc->port.config =
        SHMEM_RD(sc, dev_info.port_feature_config[port].config);

    /* get the link params */
    sc->link_params.speed_cap_mask[0] =
        SHMEM_RD(sc, dev_info.port_hw_config[port].speed_capability_mask);
    sc->link_params.speed_cap_mask[1] =
        SHMEM_RD(sc, dev_info.port_hw_config[port].speed_capability_mask2);

    /* get the lane config */
    sc->link_params.lane_config =
        SHMEM_RD(sc, dev_info.port_hw_config[port].lane_config);

    /* get the link config */
    val = SHMEM_RD(sc, dev_info.port_feature_config[port].link_config);
    sc->port.link_config[ELINK_INT_PHY] = val;
    sc->link_params.switch_cfg = (val & PORT_FEATURE_CONNECTED_SWITCH_MASK);
    sc->port.link_config[ELINK_EXT_PHY1] =
        SHMEM_RD(sc, dev_info.port_feature_config[port].link_config2);

    /* get the override preemphasis flag and enable it or turn it off */
    val = SHMEM_RD(sc, dev_info.shared_feature_config.config);
    if (val & SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_ENABLED) {
        sc->link_params.feature_config_flags |=
            ELINK_FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED;
    } else {
        sc->link_params.feature_config_flags &=
            ~ELINK_FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED;
    }

    /* get the initial value of the link params */
    sc->link_params.multi_phy_config =
        SHMEM_RD(sc, dev_info.port_hw_config[port].multi_phy_config);

    /* get external phy info */
    sc->port.ext_phy_config =
        SHMEM_RD(sc, dev_info.port_hw_config[port].external_phy_config);

    /* get the multifunction configuration */
    bxe_get_mf_cfg_info(sc);

    /* get the mac address */
    if (IS_MF(sc)) {
        mac_hi = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].mac_upper);
        mac_lo = MFCFG_RD(sc, func_mf_config[SC_ABS_FUNC(sc)].mac_lower);
    } else {
        mac_hi = SHMEM_RD(sc, dev_info.port_hw_config[port].mac_upper);
        mac_lo = SHMEM_RD(sc, dev_info.port_hw_config[port].mac_lower);
    }

    if ((mac_lo == 0) && (mac_hi == 0)) {
        *sc->mac_addr_str = 0;
        BLOGE(sc, "No Ethernet address programmed!\n");
    } else {
        sc->link_params.mac_addr[0] = (uint8_t)(mac_hi >> 8);
        sc->link_params.mac_addr[1] = (uint8_t)(mac_hi);
        sc->link_params.mac_addr[2] = (uint8_t)(mac_lo >> 24);
        sc->link_params.mac_addr[3] = (uint8_t)(mac_lo >> 16);
        sc->link_params.mac_addr[4] = (uint8_t)(mac_lo >> 8);
        sc->link_params.mac_addr[5] = (uint8_t)(mac_lo);
        snprintf(sc->mac_addr_str, sizeof(sc->mac_addr_str),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 sc->link_params.mac_addr[0], sc->link_params.mac_addr[1],
                 sc->link_params.mac_addr[2], sc->link_params.mac_addr[3],
                 sc->link_params.mac_addr[4], sc->link_params.mac_addr[5]);
        BLOGD(sc, DBG_LOAD, "Ethernet address: %s\n", sc->mac_addr_str);
    }

    return (0);
}

static void
bxe_get_tunable_params(struct bxe_softc *sc)
{
    /* sanity checks */

    if ((bxe_interrupt_mode != INTR_MODE_INTX) &&
        (bxe_interrupt_mode != INTR_MODE_MSI)  &&
        (bxe_interrupt_mode != INTR_MODE_MSIX)) {
        BLOGW(sc, "invalid interrupt_mode value (%d)\n", bxe_interrupt_mode);
        bxe_interrupt_mode = INTR_MODE_MSIX;
    }

    if ((bxe_queue_count < 0) || (bxe_queue_count > MAX_RSS_CHAINS)) {
        BLOGW(sc, "invalid queue_count value (%d)\n", bxe_queue_count);
        bxe_queue_count = 0;
    }

    if ((bxe_max_rx_bufs < 1) || (bxe_max_rx_bufs > RX_BD_USABLE)) {
        if (bxe_max_rx_bufs == 0) {
            bxe_max_rx_bufs = RX_BD_USABLE;
        } else {
            BLOGW(sc, "invalid max_rx_bufs (%d)\n", bxe_max_rx_bufs);
            bxe_max_rx_bufs = 2048;
        }
    }

    if ((bxe_hc_rx_ticks < 1) || (bxe_hc_rx_ticks > 100)) {
        BLOGW(sc, "invalid hc_rx_ticks (%d)\n", bxe_hc_rx_ticks);
        bxe_hc_rx_ticks = 25;
    }

    if ((bxe_hc_tx_ticks < 1) || (bxe_hc_tx_ticks > 100)) {
        BLOGW(sc, "invalid hc_tx_ticks (%d)\n", bxe_hc_tx_ticks);
        bxe_hc_tx_ticks = 50;
    }

    if (bxe_max_aggregation_size == 0) {
        bxe_max_aggregation_size = TPA_AGG_SIZE;
    }

    if (bxe_max_aggregation_size > 0xffff) {
        BLOGW(sc, "invalid max_aggregation_size (%d)\n",
              bxe_max_aggregation_size);
        bxe_max_aggregation_size = TPA_AGG_SIZE;
    }

    if ((bxe_mrrs < -1) || (bxe_mrrs > 3)) {
        BLOGW(sc, "invalid mrrs (%d)\n", bxe_mrrs);
        bxe_mrrs = -1;
    }

    if ((bxe_autogreeen < 0) || (bxe_autogreeen > 2)) {
        BLOGW(sc, "invalid autogreeen (%d)\n", bxe_autogreeen);
        bxe_autogreeen = 0;
    }

    if ((bxe_udp_rss < 0) || (bxe_udp_rss > 1)) {
        BLOGW(sc, "invalid udp_rss (%d)\n", bxe_udp_rss);
        bxe_udp_rss = 0;
    }

    /* pull in user settings */

    sc->interrupt_mode       = bxe_interrupt_mode;
    sc->max_rx_bufs          = bxe_max_rx_bufs;
    sc->hc_rx_ticks          = bxe_hc_rx_ticks;
    sc->hc_tx_ticks          = bxe_hc_tx_ticks;
    sc->max_aggregation_size = bxe_max_aggregation_size;
    sc->mrrs                 = bxe_mrrs;
    sc->autogreeen           = bxe_autogreeen;
    sc->udp_rss              = bxe_udp_rss;

    if (bxe_interrupt_mode == INTR_MODE_INTX) {
        sc->num_queues = 1;
    } else { /* INTR_MODE_MSI or INTR_MODE_MSIX */
        sc->num_queues =
            min((bxe_queue_count ? bxe_queue_count : mp_ncpus),
                MAX_RSS_CHAINS);
        if (sc->num_queues > mp_ncpus) {
            sc->num_queues = mp_ncpus;
        }
    }

    BLOGD(sc, DBG_LOAD,
          "User Config: "
          "debug=0x%lx "
          "interrupt_mode=%d "
          "queue_count=%d "
          "hc_rx_ticks=%d "
          "hc_tx_ticks=%d "
          "rx_budget=%d "
          "max_aggregation_size=%d "
          "mrrs=%d "
          "autogreeen=%d "
          "udp_rss=%d\n",
          bxe_debug,
          sc->interrupt_mode,
          sc->num_queues,
          sc->hc_rx_ticks,
          sc->hc_tx_ticks,
          bxe_rx_budget,
          sc->max_aggregation_size,
          sc->mrrs,
          sc->autogreeen,
          sc->udp_rss);
}

static int
bxe_media_detect(struct bxe_softc *sc)
{
    int port_type;
    uint32_t phy_idx = bxe_get_cur_phy_idx(sc);

    switch (sc->link_params.phy[phy_idx].media_type) {
    case ELINK_ETH_PHY_SFPP_10G_FIBER:
    case ELINK_ETH_PHY_XFP_FIBER:
        BLOGI(sc, "Found 10Gb Fiber media.\n");
        sc->media = IFM_10G_SR;
        port_type = PORT_FIBRE;
        break;
    case ELINK_ETH_PHY_SFP_1G_FIBER:
        BLOGI(sc, "Found 1Gb Fiber media.\n");
        sc->media = IFM_1000_SX;
        port_type = PORT_FIBRE;
        break;
    case ELINK_ETH_PHY_KR:
    case ELINK_ETH_PHY_CX4:
        BLOGI(sc, "Found 10GBase-CX4 media.\n");
        sc->media = IFM_10G_CX4;
        port_type = PORT_FIBRE;
        break;
    case ELINK_ETH_PHY_DA_TWINAX:
        BLOGI(sc, "Found 10Gb Twinax media.\n");
        sc->media = IFM_10G_TWINAX;
        port_type = PORT_DA;
        break;
    case ELINK_ETH_PHY_BASE_T:
        if (sc->link_params.speed_cap_mask[0] &
            PORT_HW_CFG_SPEED_CAPABILITY_D0_10G) {
            BLOGI(sc, "Found 10GBase-T media.\n");
            sc->media = IFM_10G_T;
            port_type = PORT_TP;
        } else {
            BLOGI(sc, "Found 1000Base-T media.\n");
            sc->media = IFM_1000_T;
            port_type = PORT_TP;
        }
        break;
    case ELINK_ETH_PHY_NOT_PRESENT:
        BLOGI(sc, "Media not present.\n");
        sc->media = 0;
        port_type = PORT_OTHER;
        break;
    case ELINK_ETH_PHY_UNSPECIFIED:
    default:
        BLOGI(sc, "Unknown media!\n");
        sc->media = 0;
        port_type = PORT_OTHER;
        break;
    }
    return port_type;
}

#define GET_FIELD(value, fname)                     \
    (((value) & (fname##_MASK)) >> (fname##_SHIFT))
#define IGU_FID(val) GET_FIELD((val), IGU_REG_MAPPING_MEMORY_FID)
#define IGU_VEC(val) GET_FIELD((val), IGU_REG_MAPPING_MEMORY_VECTOR)

static int
bxe_get_igu_cam_info(struct bxe_softc *sc)
{
    int pfid = SC_FUNC(sc);
    int igu_sb_id;
    uint32_t val;
    uint8_t fid, igu_sb_cnt = 0;

    sc->igu_base_sb = 0xff;

    if (CHIP_INT_MODE_IS_BC(sc)) {
        int vn = SC_VN(sc);
        igu_sb_cnt = sc->igu_sb_cnt;
        sc->igu_base_sb = ((CHIP_IS_MODE_4_PORT(sc) ? pfid : vn) *
                           FP_SB_MAX_E1x);
        sc->igu_dsb_id = (E1HVN_MAX * FP_SB_MAX_E1x +
                          (CHIP_IS_MODE_4_PORT(sc) ? pfid : vn));
        return (0);
    }

    /* IGU in normal mode - read CAM */
    for (igu_sb_id = 0;
         igu_sb_id < IGU_REG_MAPPING_MEMORY_SIZE;
         igu_sb_id++) {
        val = REG_RD(sc, IGU_REG_MAPPING_MEMORY + igu_sb_id * 4);
        if (!(val & IGU_REG_MAPPING_MEMORY_VALID)) {
            continue;
        }
        fid = IGU_FID(val);
        if ((fid & IGU_FID_ENCODE_IS_PF)) {
            if ((fid & IGU_FID_PF_NUM_MASK) != pfid) {
                continue;
            }
            if (IGU_VEC(val) == 0) {
                /* default status block */
                sc->igu_dsb_id = igu_sb_id;
            } else {
                if (sc->igu_base_sb == 0xff) {
                    sc->igu_base_sb = igu_sb_id;
                }
                igu_sb_cnt++;
            }
        }
    }

    /*
     * Due to new PF resource allocation by MFW T7.4 and above, it's optional
     * that number of CAM entries will not be equal to the value advertised in
     * PCI. Driver should use the minimal value of both as the actual status
     * block count
     */
    sc->igu_sb_cnt = min(sc->igu_sb_cnt, igu_sb_cnt);

    if (igu_sb_cnt == 0) {
        BLOGE(sc, "CAM configuration error\n");
        return (-1);
    }

    return (0);
}

/*
 * Gather various information from the device config space, the device itself,
 * shmem, and the user input.
 */
static int
bxe_get_device_info(struct bxe_softc *sc)
{
    uint32_t val;
    int rc;

    /* Get the data for the device */
    sc->devinfo.vendor_id    = pci_get_vendor(sc->dev);
    sc->devinfo.device_id    = pci_get_device(sc->dev);
    sc->devinfo.subvendor_id = pci_get_subvendor(sc->dev);
    sc->devinfo.subdevice_id = pci_get_subdevice(sc->dev);

    /* get the chip revision (chip metal comes from pci config space) */
    sc->devinfo.chip_id     =
    sc->link_params.chip_id =
        (((REG_RD(sc, MISC_REG_CHIP_NUM)                   & 0xffff) << 16) |
         ((REG_RD(sc, MISC_REG_CHIP_REV)                   & 0xf)    << 12) |
         (((REG_RD(sc, PCICFG_OFFSET + PCI_ID_VAL3) >> 24) & 0xf)    << 4)  |
         ((REG_RD(sc, MISC_REG_BOND_ID)                    & 0xf)    << 0));

    /* force 57811 according to MISC register */
    if (REG_RD(sc, MISC_REG_CHIP_TYPE) & MISC_REG_CHIP_TYPE_57811_MASK) {
        if (CHIP_IS_57810(sc)) {
            sc->devinfo.chip_id = ((CHIP_NUM_57811 << 16) |
                                   (sc->devinfo.chip_id & 0x0000ffff));
        } else if (CHIP_IS_57810_MF(sc)) {
            sc->devinfo.chip_id = ((CHIP_NUM_57811_MF << 16) |
                                   (sc->devinfo.chip_id & 0x0000ffff));
        }
        sc->devinfo.chip_id |= 0x1;
    }

    BLOGD(sc, DBG_LOAD,
          "chip_id=0x%08x (num=0x%04x rev=0x%01x metal=0x%02x bond=0x%01x)\n",
          sc->devinfo.chip_id,
          ((sc->devinfo.chip_id >> 16) & 0xffff),
          ((sc->devinfo.chip_id >> 12) & 0xf),
          ((sc->devinfo.chip_id >>  4) & 0xff),
          ((sc->devinfo.chip_id >>  0) & 0xf));

    val = (REG_RD(sc, 0x2874) & 0x55);
    if ((sc->devinfo.chip_id & 0x1) ||
        (CHIP_IS_E1(sc) && val) ||
        (CHIP_IS_E1H(sc) && (val == 0x55))) {
        sc->flags |= BXE_ONE_PORT_FLAG;
        BLOGD(sc, DBG_LOAD, "single port device\n");
    }

    /* set the doorbell size */
    sc->doorbell_size = (1 << BXE_DB_SHIFT);

    /* determine whether the device is in 2 port or 4 port mode */
    sc->devinfo.chip_port_mode = CHIP_PORT_MODE_NONE; /* E1 & E1h*/
    if (CHIP_IS_E2E3(sc)) {
        /*
         * Read port4mode_en_ovwr[0]:
         *   If 1, four port mode is in port4mode_en_ovwr[1].
         *   If 0, four port mode is in port4mode_en[0].
         */
        val = REG_RD(sc, MISC_REG_PORT4MODE_EN_OVWR);
        if (val & 1) {
            val = ((val >> 1) & 1);
        } else {
            val = REG_RD(sc, MISC_REG_PORT4MODE_EN);
        }

        sc->devinfo.chip_port_mode =
            (val) ? CHIP_4_PORT_MODE : CHIP_2_PORT_MODE;

        BLOGD(sc, DBG_LOAD, "Port mode = %s\n", (val) ? "4" : "2");
    }

    /* get the function and path info for the device */
    bxe_get_function_num(sc);

    /* get the shared memory base address */
    sc->devinfo.shmem_base     =
    sc->link_params.shmem_base =
        REG_RD(sc, MISC_REG_SHARED_MEM_ADDR);
    sc->devinfo.shmem2_base =
        REG_RD(sc, (SC_PATH(sc) ? MISC_REG_GENERIC_CR_1 :
                                  MISC_REG_GENERIC_CR_0));

    BLOGD(sc, DBG_LOAD, "shmem_base=0x%08x, shmem2_base=0x%08x\n",
          sc->devinfo.shmem_base, sc->devinfo.shmem2_base);

    if (!sc->devinfo.shmem_base) {
        /* this should ONLY prevent upcoming shmem reads */
        BLOGI(sc, "MCP not active\n");
        sc->flags |= BXE_NO_MCP_FLAG;
        return (0);
    }

    /* make sure the shared memory contents are valid */
    val = SHMEM_RD(sc, validity_map[SC_PORT(sc)]);
    if ((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB)) !=
        (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB)) {
        BLOGE(sc, "Invalid SHMEM validity signature: 0x%08x\n", val);
        return (0);
    }
    BLOGD(sc, DBG_LOAD, "Valid SHMEM validity signature: 0x%08x\n", val);

    /* get the bootcode version */
    sc->devinfo.bc_ver = SHMEM_RD(sc, dev_info.bc_rev);
    snprintf(sc->devinfo.bc_ver_str,
             sizeof(sc->devinfo.bc_ver_str),
             "%d.%d.%d",
             ((sc->devinfo.bc_ver >> 24) & 0xff),
             ((sc->devinfo.bc_ver >> 16) & 0xff),
             ((sc->devinfo.bc_ver >>  8) & 0xff));
    BLOGD(sc, DBG_LOAD, "Bootcode version: %s\n", sc->devinfo.bc_ver_str);

    /* get the bootcode shmem address */
    sc->devinfo.mf_cfg_base = bxe_get_shmem_mf_cfg_base(sc);
    BLOGD(sc, DBG_LOAD, "mf_cfg_base=0x08%x \n", sc->devinfo.mf_cfg_base);

    /* clean indirect addresses as they're not used */
    pci_write_config(sc->dev, PCICFG_GRC_ADDRESS, 0, 4);
    if (IS_PF(sc)) {
        REG_WR(sc, PXP2_REG_PGL_ADDR_88_F0, 0);
        REG_WR(sc, PXP2_REG_PGL_ADDR_8C_F0, 0);
        REG_WR(sc, PXP2_REG_PGL_ADDR_90_F0, 0);
        REG_WR(sc, PXP2_REG_PGL_ADDR_94_F0, 0);
        if (CHIP_IS_E1x(sc)) {
            REG_WR(sc, PXP2_REG_PGL_ADDR_88_F1, 0);
            REG_WR(sc, PXP2_REG_PGL_ADDR_8C_F1, 0);
            REG_WR(sc, PXP2_REG_PGL_ADDR_90_F1, 0);
            REG_WR(sc, PXP2_REG_PGL_ADDR_94_F1, 0);
        }

        /*
         * Enable internal target-read (in case we are probed after PF
         * FLR). Must be done prior to any BAR read access. Only for
         * 57712 and up
         */
        if (!CHIP_IS_E1x(sc)) {
            REG_WR(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);
        }
    }

    /* get the nvram size */
    val = REG_RD(sc, MCP_REG_MCPR_NVM_CFG4);
    sc->devinfo.flash_size =
        (NVRAM_1MB_SIZE << (val & MCPR_NVM_CFG4_FLASH_SIZE));
    BLOGD(sc, DBG_LOAD, "nvram flash size: %d\n", sc->devinfo.flash_size);

    /* get PCI capabilites */
    bxe_probe_pci_caps(sc);

    bxe_set_power_state(sc, PCI_PM_D0);

    /* get various configuration parameters from shmem */
    bxe_get_shmem_info(sc);

    if (sc->devinfo.pcie_msix_cap_reg != 0) {
        val = pci_read_config(sc->dev,
                              (sc->devinfo.pcie_msix_cap_reg +
                               PCIR_MSIX_CTRL),
                              2);
        sc->igu_sb_cnt = (val & PCIM_MSIXCTRL_TABLE_SIZE);
    } else {
        sc->igu_sb_cnt = 1;
    }

    sc->igu_base_addr = BAR_IGU_INTMEM;

    /* initialize IGU parameters */
    if (CHIP_IS_E1x(sc)) {
        sc->devinfo.int_block = INT_BLOCK_HC;
        sc->igu_dsb_id = DEF_SB_IGU_ID;
        sc->igu_base_sb = 0;
    } else {
        sc->devinfo.int_block = INT_BLOCK_IGU;

        /* do not allow device reset during IGU info preocessing */
        bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RESET);

        val = REG_RD(sc, IGU_REG_BLOCK_CONFIGURATION);

        if (val & IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN) {
            int tout = 5000;

            BLOGD(sc, DBG_LOAD, "FORCING IGU Normal Mode\n");

            val &= ~(IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN);
            REG_WR(sc, IGU_REG_BLOCK_CONFIGURATION, val);
            REG_WR(sc, IGU_REG_RESET_MEMORIES, 0x7f);

            while (tout && REG_RD(sc, IGU_REG_RESET_MEMORIES)) {
                tout--;
                DELAY(1000);
            }

            if (REG_RD(sc, IGU_REG_RESET_MEMORIES)) {
                BLOGD(sc, DBG_LOAD, "FORCING IGU Normal Mode failed!!!\n");
                bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RESET);
                return (-1);
            }
        }

        if (val & IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN) {
            BLOGD(sc, DBG_LOAD, "IGU Backward Compatible Mode\n");
            sc->devinfo.int_block |= INT_BLOCK_MODE_BW_COMP;
        } else {
            BLOGD(sc, DBG_LOAD, "IGU Normal Mode\n");
        }

        rc = bxe_get_igu_cam_info(sc);

        bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RESET);

        if (rc) {
            return (rc);
        }
    }

    /*
     * Get base FW non-default (fast path) status block ID. This value is
     * used to initialize the fw_sb_id saved on the fp/queue structure to
     * determine the id used by the FW.
     */
    if (CHIP_IS_E1x(sc)) {
        sc->base_fw_ndsb = ((SC_PORT(sc) * FP_SB_MAX_E1x) + SC_L_ID(sc));
    } else {
        /*
         * 57712+ - We currently use one FW SB per IGU SB (Rx and Tx of
         * the same queue are indicated on the same IGU SB). So we prefer
         * FW and IGU SBs to be the same value.
         */
        sc->base_fw_ndsb = sc->igu_base_sb;
    }

    BLOGD(sc, DBG_LOAD,
          "igu_dsb_id=%d igu_base_sb=%d igu_sb_cnt=%d base_fw_ndsb=%d\n",
          sc->igu_dsb_id, sc->igu_base_sb,
          sc->igu_sb_cnt, sc->base_fw_ndsb);

    elink_phy_probe(&sc->link_params);

    return (0);
}

static void
bxe_link_settings_supported(struct bxe_softc *sc,
                            uint32_t         switch_cfg)
{
    uint32_t cfg_size = 0;
    uint32_t idx;
    uint8_t port = SC_PORT(sc);

    /* aggregation of supported attributes of all external phys */
    sc->port.supported[0] = 0;
    sc->port.supported[1] = 0;

    switch (sc->link_params.num_phys) {
    case 1:
        sc->port.supported[0] = sc->link_params.phy[ELINK_INT_PHY].supported;
        cfg_size = 1;
        break;
    case 2:
        sc->port.supported[0] = sc->link_params.phy[ELINK_EXT_PHY1].supported;
        cfg_size = 1;
        break;
    case 3:
        if (sc->link_params.multi_phy_config &
            PORT_HW_CFG_PHY_SWAPPED_ENABLED) {
            sc->port.supported[1] =
                sc->link_params.phy[ELINK_EXT_PHY1].supported;
            sc->port.supported[0] =
                sc->link_params.phy[ELINK_EXT_PHY2].supported;
        } else {
            sc->port.supported[0] =
                sc->link_params.phy[ELINK_EXT_PHY1].supported;
            sc->port.supported[1] =
                sc->link_params.phy[ELINK_EXT_PHY2].supported;
        }
        cfg_size = 2;
        break;
    }

    if (!(sc->port.supported[0] || sc->port.supported[1])) {
        BLOGE(sc, "Invalid phy config in NVRAM (PHY1=0x%08x PHY2=0x%08x)\n",
              SHMEM_RD(sc,
                       dev_info.port_hw_config[port].external_phy_config),
              SHMEM_RD(sc,
                       dev_info.port_hw_config[port].external_phy_config2));
        return;
    }

    if (CHIP_IS_E3(sc))
        sc->port.phy_addr = REG_RD(sc, MISC_REG_WC0_CTRL_PHY_ADDR);
    else {
        switch (switch_cfg) {
        case ELINK_SWITCH_CFG_1G:
            sc->port.phy_addr =
                REG_RD(sc, NIG_REG_SERDES0_CTRL_PHY_ADDR + port*0x10);
            break;
        case ELINK_SWITCH_CFG_10G:
            sc->port.phy_addr =
                REG_RD(sc, NIG_REG_XGXS0_CTRL_PHY_ADDR + port*0x18);
            break;
        default:
            BLOGE(sc, "Invalid switch config in link_config=0x%08x\n",
                  sc->port.link_config[0]);
            return;
        }
    }

    BLOGD(sc, DBG_LOAD, "PHY addr 0x%08x\n", sc->port.phy_addr);

    /* mask what we support according to speed_cap_mask per configuration */
    for (idx = 0; idx < cfg_size; idx++) {
        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_10baseT_Half;
        }

        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_10baseT_Full;
        }

        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_100baseT_Half;
        }

        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_100baseT_Full;
        }

        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_1G)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_1000baseT_Full;
        }

        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_2500baseX_Full;
        }

        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_10G)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_10000baseT_Full;
        }

        if (!(sc->link_params.speed_cap_mask[idx] &
              PORT_HW_CFG_SPEED_CAPABILITY_D0_20G)) {
            sc->port.supported[idx] &= ~ELINK_SUPPORTED_20000baseKR2_Full;
        }
    }

    BLOGD(sc, DBG_LOAD, "PHY supported 0=0x%08x 1=0x%08x\n",
          sc->port.supported[0], sc->port.supported[1]);
	ELINK_DEBUG_P2(sc, "PHY supported 0=0x%08x 1=0x%08x\n",
					sc->port.supported[0], sc->port.supported[1]);
}

static void
bxe_link_settings_requested(struct bxe_softc *sc)
{
    uint32_t link_config;
    uint32_t idx;
    uint32_t cfg_size = 0;

    sc->port.advertising[0] = 0;
    sc->port.advertising[1] = 0;

    switch (sc->link_params.num_phys) {
    case 1:
    case 2:
        cfg_size = 1;
        break;
    case 3:
        cfg_size = 2;
        break;
    }

    for (idx = 0; idx < cfg_size; idx++) {
        sc->link_params.req_duplex[idx] = DUPLEX_FULL;
        link_config = sc->port.link_config[idx];

        switch (link_config & PORT_FEATURE_LINK_SPEED_MASK) {
        case PORT_FEATURE_LINK_SPEED_AUTO:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_Autoneg) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_AUTO_NEG;
                sc->port.advertising[idx] |= sc->port.supported[idx];
                if (sc->link_params.phy[ELINK_EXT_PHY1].type ==
                    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84833)
                    sc->port.advertising[idx] |=
                        (ELINK_SUPPORTED_100baseT_Half |
                         ELINK_SUPPORTED_100baseT_Full);
            } else {
                /* force 10G, no AN */
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_10000;
                sc->port.advertising[idx] |=
                    (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
                continue;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_10M_FULL:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_10baseT_Full) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_10;
                sc->port.advertising[idx] |= (ADVERTISED_10baseT_Full |
                                              ADVERTISED_TP);
            } else {
                BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                          "speed_cap_mask=0x%08x\n",
                      link_config, sc->link_params.speed_cap_mask[idx]);
                return;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_10M_HALF:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_10baseT_Half) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_10;
                sc->link_params.req_duplex[idx] = DUPLEX_HALF;
                sc->port.advertising[idx] |= (ADVERTISED_10baseT_Half |
                                              ADVERTISED_TP);
				ELINK_DEBUG_P1(sc, "driver requesting DUPLEX_HALF req_duplex = %x!\n",
								sc->link_params.req_duplex[idx]);
            } else {
                BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                          "speed_cap_mask=0x%08x\n",
                      link_config, sc->link_params.speed_cap_mask[idx]);
                return;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_100M_FULL:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_100baseT_Full) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_100;
                sc->port.advertising[idx] |= (ADVERTISED_100baseT_Full |
                                              ADVERTISED_TP);
            } else {
                BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                          "speed_cap_mask=0x%08x\n",
                      link_config, sc->link_params.speed_cap_mask[idx]);
                return;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_100M_HALF:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_100baseT_Half) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_100;
                sc->link_params.req_duplex[idx] = DUPLEX_HALF;
                sc->port.advertising[idx] |= (ADVERTISED_100baseT_Half |
                                              ADVERTISED_TP);
            } else {
                BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                          "speed_cap_mask=0x%08x\n",
                      link_config, sc->link_params.speed_cap_mask[idx]);
                return;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_1G:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_1000baseT_Full) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_1000;
                sc->port.advertising[idx] |= (ADVERTISED_1000baseT_Full |
                                              ADVERTISED_TP);
            } else {
                BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                          "speed_cap_mask=0x%08x\n",
                      link_config, sc->link_params.speed_cap_mask[idx]);
                return;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_2_5G:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_2500baseX_Full) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_2500;
                sc->port.advertising[idx] |= (ADVERTISED_2500baseX_Full |
                                              ADVERTISED_TP);
            } else {
                BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                          "speed_cap_mask=0x%08x\n",
                      link_config, sc->link_params.speed_cap_mask[idx]);
                return;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_10G_CX4:
            if (sc->port.supported[idx] & ELINK_SUPPORTED_10000baseT_Full) {
                sc->link_params.req_line_speed[idx] = ELINK_SPEED_10000;
                sc->port.advertising[idx] |= (ADVERTISED_10000baseT_Full |
                                              ADVERTISED_FIBRE);
            } else {
                BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                          "speed_cap_mask=0x%08x\n",
                      link_config, sc->link_params.speed_cap_mask[idx]);
                return;
            }
            break;

        case PORT_FEATURE_LINK_SPEED_20G:
            sc->link_params.req_line_speed[idx] = ELINK_SPEED_20000;
            break;

        default:
            BLOGE(sc, "Invalid NVRAM config link_config=0x%08x "
                      "speed_cap_mask=0x%08x\n",
                  link_config, sc->link_params.speed_cap_mask[idx]);
            sc->link_params.req_line_speed[idx] = ELINK_SPEED_AUTO_NEG;
            sc->port.advertising[idx] = sc->port.supported[idx];
            break;
        }

        sc->link_params.req_flow_ctrl[idx] =
            (link_config & PORT_FEATURE_FLOW_CONTROL_MASK);

        if (sc->link_params.req_flow_ctrl[idx] == ELINK_FLOW_CTRL_AUTO) {
            if (!(sc->port.supported[idx] & ELINK_SUPPORTED_Autoneg)) {
                sc->link_params.req_flow_ctrl[idx] = ELINK_FLOW_CTRL_NONE;
            } else {
                bxe_set_requested_fc(sc);
            }
        }

        BLOGD(sc, DBG_LOAD, "req_line_speed=%d req_duplex=%d "
                            "req_flow_ctrl=0x%x advertising=0x%x\n",
              sc->link_params.req_line_speed[idx],
              sc->link_params.req_duplex[idx],
              sc->link_params.req_flow_ctrl[idx],
              sc->port.advertising[idx]);
		ELINK_DEBUG_P3(sc, "req_line_speed=%d req_duplex=%d "
						"advertising=0x%x\n",
						sc->link_params.req_line_speed[idx],
						sc->link_params.req_duplex[idx],
						sc->port.advertising[idx]);
    }
}

static void
bxe_get_phy_info(struct bxe_softc *sc)
{
    uint8_t port = SC_PORT(sc);
    uint32_t config = sc->port.config;
    uint32_t eee_mode;

    /* shmem data already read in bxe_get_shmem_info() */

    ELINK_DEBUG_P3(sc, "lane_config=0x%08x speed_cap_mask0=0x%08x "
                        "link_config0=0x%08x\n",
               sc->link_params.lane_config,
               sc->link_params.speed_cap_mask[0],
               sc->port.link_config[0]);
     

    bxe_link_settings_supported(sc, sc->link_params.switch_cfg);
    bxe_link_settings_requested(sc);

    if (sc->autogreeen == AUTO_GREEN_FORCE_ON) {
        sc->link_params.feature_config_flags |=
            ELINK_FEATURE_CONFIG_AUTOGREEEN_ENABLED;
    } else if (sc->autogreeen == AUTO_GREEN_FORCE_OFF) {
        sc->link_params.feature_config_flags &=
            ~ELINK_FEATURE_CONFIG_AUTOGREEEN_ENABLED;
    } else if (config & PORT_FEAT_CFG_AUTOGREEEN_ENABLED) {
        sc->link_params.feature_config_flags |=
            ELINK_FEATURE_CONFIG_AUTOGREEEN_ENABLED;
    }

    /* configure link feature according to nvram value */
    eee_mode =
        (((SHMEM_RD(sc, dev_info.port_feature_config[port].eee_power_mode)) &
          PORT_FEAT_CFG_EEE_POWER_MODE_MASK) >>
         PORT_FEAT_CFG_EEE_POWER_MODE_SHIFT);
    if (eee_mode != PORT_FEAT_CFG_EEE_POWER_MODE_DISABLED) {
        sc->link_params.eee_mode = (ELINK_EEE_MODE_ADV_LPI |
                                    ELINK_EEE_MODE_ENABLE_LPI |
                                    ELINK_EEE_MODE_OUTPUT_TIME);
    } else {
        sc->link_params.eee_mode = 0;
    }

    /* get the media type */
    bxe_media_detect(sc);
	ELINK_DEBUG_P1(sc, "detected media type\n", sc->media);
}

static void
bxe_get_params(struct bxe_softc *sc)
{
    /* get user tunable params */
    bxe_get_tunable_params(sc);

    /* select the RX and TX ring sizes */
    sc->tx_ring_size = TX_BD_USABLE;
    sc->rx_ring_size = RX_BD_USABLE;

    /* XXX disable WoL */
    sc->wol = 0;
}

static void
bxe_set_modes_bitmap(struct bxe_softc *sc)
{
    uint32_t flags = 0;

    if (CHIP_REV_IS_FPGA(sc)) {
        SET_FLAGS(flags, MODE_FPGA);
    } else if (CHIP_REV_IS_EMUL(sc)) {
        SET_FLAGS(flags, MODE_EMUL);
    } else {
        SET_FLAGS(flags, MODE_ASIC);
    }

    if (CHIP_IS_MODE_4_PORT(sc)) {
        SET_FLAGS(flags, MODE_PORT4);
    } else {
        SET_FLAGS(flags, MODE_PORT2);
    }

    if (CHIP_IS_E2(sc)) {
        SET_FLAGS(flags, MODE_E2);
    } else if (CHIP_IS_E3(sc)) {
        SET_FLAGS(flags, MODE_E3);
        if (CHIP_REV(sc) == CHIP_REV_Ax) {
            SET_FLAGS(flags, MODE_E3_A0);
        } else /*if (CHIP_REV(sc) == CHIP_REV_Bx)*/ {
            SET_FLAGS(flags, MODE_E3_B0 | MODE_COS3);
        }
    }

    if (IS_MF(sc)) {
        SET_FLAGS(flags, MODE_MF);
        switch (sc->devinfo.mf_info.mf_mode) {
        case MULTI_FUNCTION_SD:
            SET_FLAGS(flags, MODE_MF_SD);
            break;
        case MULTI_FUNCTION_SI:
            SET_FLAGS(flags, MODE_MF_SI);
            break;
        case MULTI_FUNCTION_AFEX:
            SET_FLAGS(flags, MODE_MF_AFEX);
            break;
        }
    } else {
        SET_FLAGS(flags, MODE_SF);
    }

#if defined(__LITTLE_ENDIAN)
    SET_FLAGS(flags, MODE_LITTLE_ENDIAN);
#else /* __BIG_ENDIAN */
    SET_FLAGS(flags, MODE_BIG_ENDIAN);
#endif

    INIT_MODE_FLAGS(sc) = flags;
}

static int
bxe_alloc_hsi_mem(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    bus_addr_t busaddr;
    int max_agg_queues;
    int max_segments;
    bus_size_t max_size;
    bus_size_t max_seg_size;
    char buf[32];
    int rc;
    int i, j;

    /* XXX zero out all vars here and call bxe_alloc_hsi_mem on error */

    /* allocate the parent bus DMA tag */
    rc = bus_dma_tag_create(bus_get_dma_tag(sc->dev), /* parent tag */
                            1,                        /* alignment */
                            0,                        /* boundary limit */
                            BUS_SPACE_MAXADDR,        /* restricted low */
                            BUS_SPACE_MAXADDR,        /* restricted hi */
                            NULL,                     /* addr filter() */
                            NULL,                     /* addr filter() arg */
                            BUS_SPACE_MAXSIZE_32BIT,  /* max map size */
                            BUS_SPACE_UNRESTRICTED,   /* num discontinuous */
                            BUS_SPACE_MAXSIZE_32BIT,  /* max seg size */
                            0,                        /* flags */
                            NULL,                     /* lock() */
                            NULL,                     /* lock() arg */
                            &sc->parent_dma_tag);     /* returned dma tag */
    if (rc != 0) {
        BLOGE(sc, "Failed to alloc parent DMA tag (%d)!\n", rc);
        return (1);
    }

    /************************/
    /* DEFAULT STATUS BLOCK */
    /************************/

    if (bxe_dma_alloc(sc, sizeof(struct host_sp_status_block),
                      &sc->def_sb_dma, "default status block") != 0) {
        /* XXX */
        bus_dma_tag_destroy(sc->parent_dma_tag);
        return (1);
    }

    sc->def_sb = (struct host_sp_status_block *)sc->def_sb_dma.vaddr;

    /***************/
    /* EVENT QUEUE */
    /***************/

    if (bxe_dma_alloc(sc, BCM_PAGE_SIZE,
                      &sc->eq_dma, "event queue") != 0) {
        /* XXX */
        bxe_dma_free(sc, &sc->def_sb_dma);
        sc->def_sb = NULL;
        bus_dma_tag_destroy(sc->parent_dma_tag);
        return (1);
    }

    sc->eq = (union event_ring_elem * )sc->eq_dma.vaddr;

    /*************/
    /* SLOW PATH */
    /*************/

    if (bxe_dma_alloc(sc, sizeof(struct bxe_slowpath),
                      &sc->sp_dma, "slow path") != 0) {
        /* XXX */
        bxe_dma_free(sc, &sc->eq_dma);
        sc->eq = NULL;
        bxe_dma_free(sc, &sc->def_sb_dma);
        sc->def_sb = NULL;
        bus_dma_tag_destroy(sc->parent_dma_tag);
        return (1);
    }

    sc->sp = (struct bxe_slowpath *)sc->sp_dma.vaddr;

    /*******************/
    /* SLOW PATH QUEUE */
    /*******************/

    if (bxe_dma_alloc(sc, BCM_PAGE_SIZE,
                      &sc->spq_dma, "slow path queue") != 0) {
        /* XXX */
        bxe_dma_free(sc, &sc->sp_dma);
        sc->sp = NULL;
        bxe_dma_free(sc, &sc->eq_dma);
        sc->eq = NULL;
        bxe_dma_free(sc, &sc->def_sb_dma);
        sc->def_sb = NULL;
        bus_dma_tag_destroy(sc->parent_dma_tag);
        return (1);
    }

    sc->spq = (struct eth_spe *)sc->spq_dma.vaddr;

    /***************************/
    /* FW DECOMPRESSION BUFFER */
    /***************************/

    if (bxe_dma_alloc(sc, FW_BUF_SIZE, &sc->gz_buf_dma,
                      "fw decompression buffer") != 0) {
        /* XXX */
        bxe_dma_free(sc, &sc->spq_dma);
        sc->spq = NULL;
        bxe_dma_free(sc, &sc->sp_dma);
        sc->sp = NULL;
        bxe_dma_free(sc, &sc->eq_dma);
        sc->eq = NULL;
        bxe_dma_free(sc, &sc->def_sb_dma);
        sc->def_sb = NULL;
        bus_dma_tag_destroy(sc->parent_dma_tag);
        return (1);
    }

    sc->gz_buf = (void *)sc->gz_buf_dma.vaddr;

    if ((sc->gz_strm =
         malloc(sizeof(*sc->gz_strm), M_DEVBUF, M_NOWAIT)) == NULL) {
        /* XXX */
        bxe_dma_free(sc, &sc->gz_buf_dma);
        sc->gz_buf = NULL;
        bxe_dma_free(sc, &sc->spq_dma);
        sc->spq = NULL;
        bxe_dma_free(sc, &sc->sp_dma);
        sc->sp = NULL;
        bxe_dma_free(sc, &sc->eq_dma);
        sc->eq = NULL;
        bxe_dma_free(sc, &sc->def_sb_dma);
        sc->def_sb = NULL;
        bus_dma_tag_destroy(sc->parent_dma_tag);
        return (1);
    }

    /*************/
    /* FASTPATHS */
    /*************/

    /* allocate DMA memory for each fastpath structure */
    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];
        fp->sc    = sc;
        fp->index = i;

        /*******************/
        /* FP STATUS BLOCK */
        /*******************/

        snprintf(buf, sizeof(buf), "fp %d status block", i);
        if (bxe_dma_alloc(sc, sizeof(union bxe_host_hc_status_block),
                          &fp->sb_dma, buf) != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to alloc %s\n", buf);
            return (1);
        } else {
            if (CHIP_IS_E2E3(sc)) {
                fp->status_block.e2_sb =
                    (struct host_hc_status_block_e2 *)fp->sb_dma.vaddr;
            } else {
                fp->status_block.e1x_sb =
                    (struct host_hc_status_block_e1x *)fp->sb_dma.vaddr;
            }
        }

        /******************/
        /* FP TX BD CHAIN */
        /******************/

        snprintf(buf, sizeof(buf), "fp %d tx bd chain", i);
        if (bxe_dma_alloc(sc, (BCM_PAGE_SIZE * TX_BD_NUM_PAGES),
                          &fp->tx_dma, buf) != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to alloc %s\n", buf);
            return (1);
        } else {
            fp->tx_chain = (union eth_tx_bd_types *)fp->tx_dma.vaddr;
        }

        /* link together the tx bd chain pages */
        for (j = 1; j <= TX_BD_NUM_PAGES; j++) {
            /* index into the tx bd chain array to last entry per page */
            struct eth_tx_next_bd *tx_next_bd =
                &fp->tx_chain[TX_BD_TOTAL_PER_PAGE * j - 1].next_bd;
            /* point to the next page and wrap from last page */
            busaddr = (fp->tx_dma.paddr +
                       (BCM_PAGE_SIZE * (j % TX_BD_NUM_PAGES)));
            tx_next_bd->addr_hi = htole32(U64_HI(busaddr));
            tx_next_bd->addr_lo = htole32(U64_LO(busaddr));
        }

        /******************/
        /* FP RX BD CHAIN */
        /******************/

        snprintf(buf, sizeof(buf), "fp %d rx bd chain", i);
        if (bxe_dma_alloc(sc, (BCM_PAGE_SIZE * RX_BD_NUM_PAGES),
                          &fp->rx_dma, buf) != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to alloc %s\n", buf);
            return (1);
        } else {
            fp->rx_chain = (struct eth_rx_bd *)fp->rx_dma.vaddr;
        }

        /* link together the rx bd chain pages */
        for (j = 1; j <= RX_BD_NUM_PAGES; j++) {
            /* index into the rx bd chain array to last entry per page */
            struct eth_rx_bd *rx_bd =
                &fp->rx_chain[RX_BD_TOTAL_PER_PAGE * j - 2];
            /* point to the next page and wrap from last page */
            busaddr = (fp->rx_dma.paddr +
                       (BCM_PAGE_SIZE * (j % RX_BD_NUM_PAGES)));
            rx_bd->addr_hi = htole32(U64_HI(busaddr));
            rx_bd->addr_lo = htole32(U64_LO(busaddr));
        }

        /*******************/
        /* FP RX RCQ CHAIN */
        /*******************/

        snprintf(buf, sizeof(buf), "fp %d rcq chain", i);
        if (bxe_dma_alloc(sc, (BCM_PAGE_SIZE * RCQ_NUM_PAGES),
                          &fp->rcq_dma, buf) != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to alloc %s\n", buf);
            return (1);
        } else {
            fp->rcq_chain = (union eth_rx_cqe *)fp->rcq_dma.vaddr;
        }

        /* link together the rcq chain pages */
        for (j = 1; j <= RCQ_NUM_PAGES; j++) {
            /* index into the rcq chain array to last entry per page */
            struct eth_rx_cqe_next_page *rx_cqe_next =
                (struct eth_rx_cqe_next_page *)
                &fp->rcq_chain[RCQ_TOTAL_PER_PAGE * j - 1];
            /* point to the next page and wrap from last page */
            busaddr = (fp->rcq_dma.paddr +
                       (BCM_PAGE_SIZE * (j % RCQ_NUM_PAGES)));
            rx_cqe_next->addr_hi = htole32(U64_HI(busaddr));
            rx_cqe_next->addr_lo = htole32(U64_LO(busaddr));
        }

        /*******************/
        /* FP RX SGE CHAIN */
        /*******************/

        snprintf(buf, sizeof(buf), "fp %d sge chain", i);
        if (bxe_dma_alloc(sc, (BCM_PAGE_SIZE * RX_SGE_NUM_PAGES),
                          &fp->rx_sge_dma, buf) != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to alloc %s\n", buf);
            return (1);
        } else {
            fp->rx_sge_chain = (struct eth_rx_sge *)fp->rx_sge_dma.vaddr;
        }

        /* link together the sge chain pages */
        for (j = 1; j <= RX_SGE_NUM_PAGES; j++) {
            /* index into the rcq chain array to last entry per page */
            struct eth_rx_sge *rx_sge =
                &fp->rx_sge_chain[RX_SGE_TOTAL_PER_PAGE * j - 2];
            /* point to the next page and wrap from last page */
            busaddr = (fp->rx_sge_dma.paddr +
                       (BCM_PAGE_SIZE * (j % RX_SGE_NUM_PAGES)));
            rx_sge->addr_hi = htole32(U64_HI(busaddr));
            rx_sge->addr_lo = htole32(U64_LO(busaddr));
        }

        /***********************/
        /* FP TX MBUF DMA MAPS */
        /***********************/

        /* set required sizes before mapping to conserve resources */
        if (if_getcapenable(sc->ifp) & (IFCAP_TSO4 | IFCAP_TSO6)) {
            max_size     = BXE_TSO_MAX_SIZE;
            max_segments = BXE_TSO_MAX_SEGMENTS;
            max_seg_size = BXE_TSO_MAX_SEG_SIZE;
        } else {
            max_size     = (MCLBYTES * BXE_MAX_SEGMENTS);
            max_segments = BXE_MAX_SEGMENTS;
            max_seg_size = MCLBYTES;
        }

        /* create a dma tag for the tx mbufs */
        rc = bus_dma_tag_create(sc->parent_dma_tag, /* parent tag */
                                1,                  /* alignment */
                                0,                  /* boundary limit */
                                BUS_SPACE_MAXADDR,  /* restricted low */
                                BUS_SPACE_MAXADDR,  /* restricted hi */
                                NULL,               /* addr filter() */
                                NULL,               /* addr filter() arg */
                                max_size,           /* max map size */
                                max_segments,       /* num discontinuous */
                                max_seg_size,       /* max seg size */
                                0,                  /* flags */
                                NULL,               /* lock() */
                                NULL,               /* lock() arg */
                                &fp->tx_mbuf_tag);  /* returned dma tag */
        if (rc != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to create dma tag for "
                      "'fp %d tx mbufs' (%d)\n", i, rc);
            return (1);
        }

        /* create dma maps for each of the tx mbuf clusters */
        for (j = 0; j < TX_BD_TOTAL; j++) {
            if (bus_dmamap_create(fp->tx_mbuf_tag,
                                  BUS_DMA_NOWAIT,
                                  &fp->tx_mbuf_chain[j].m_map)) {
                /* XXX unwind and free previous fastpath allocations */
                BLOGE(sc, "Failed to create dma map for "
                          "'fp %d tx mbuf %d' (%d)\n", i, j, rc);
                return (1);
            }
        }

        /***********************/
        /* FP RX MBUF DMA MAPS */
        /***********************/

        /* create a dma tag for the rx mbufs */
        rc = bus_dma_tag_create(sc->parent_dma_tag, /* parent tag */
                                1,                  /* alignment */
                                0,                  /* boundary limit */
                                BUS_SPACE_MAXADDR,  /* restricted low */
                                BUS_SPACE_MAXADDR,  /* restricted hi */
                                NULL,               /* addr filter() */
                                NULL,               /* addr filter() arg */
                                MJUM9BYTES,         /* max map size */
                                1,                  /* num discontinuous */
                                MJUM9BYTES,         /* max seg size */
                                0,                  /* flags */
                                NULL,               /* lock() */
                                NULL,               /* lock() arg */
                                &fp->rx_mbuf_tag);  /* returned dma tag */
        if (rc != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to create dma tag for "
                      "'fp %d rx mbufs' (%d)\n", i, rc);
            return (1);
        }

        /* create dma maps for each of the rx mbuf clusters */
        for (j = 0; j < RX_BD_TOTAL; j++) {
            if (bus_dmamap_create(fp->rx_mbuf_tag,
                                  BUS_DMA_NOWAIT,
                                  &fp->rx_mbuf_chain[j].m_map)) {
                /* XXX unwind and free previous fastpath allocations */
                BLOGE(sc, "Failed to create dma map for "
                          "'fp %d rx mbuf %d' (%d)\n", i, j, rc);
                return (1);
            }
        }

        /* create dma map for the spare rx mbuf cluster */
        if (bus_dmamap_create(fp->rx_mbuf_tag,
                              BUS_DMA_NOWAIT,
                              &fp->rx_mbuf_spare_map)) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to create dma map for "
                      "'fp %d spare rx mbuf' (%d)\n", i, rc);
            return (1);
        }

        /***************************/
        /* FP RX SGE MBUF DMA MAPS */
        /***************************/

        /* create a dma tag for the rx sge mbufs */
        rc = bus_dma_tag_create(sc->parent_dma_tag, /* parent tag */
                                1,                  /* alignment */
                                0,                  /* boundary limit */
                                BUS_SPACE_MAXADDR,  /* restricted low */
                                BUS_SPACE_MAXADDR,  /* restricted hi */
                                NULL,               /* addr filter() */
                                NULL,               /* addr filter() arg */
                                BCM_PAGE_SIZE,      /* max map size */
                                1,                  /* num discontinuous */
                                BCM_PAGE_SIZE,      /* max seg size */
                                0,                  /* flags */
                                NULL,               /* lock() */
                                NULL,               /* lock() arg */
                                &fp->rx_sge_mbuf_tag); /* returned dma tag */
        if (rc != 0) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to create dma tag for "
                      "'fp %d rx sge mbufs' (%d)\n", i, rc);
            return (1);
        }

        /* create dma maps for the rx sge mbuf clusters */
        for (j = 0; j < RX_SGE_TOTAL; j++) {
            if (bus_dmamap_create(fp->rx_sge_mbuf_tag,
                                  BUS_DMA_NOWAIT,
                                  &fp->rx_sge_mbuf_chain[j].m_map)) {
                /* XXX unwind and free previous fastpath allocations */
                BLOGE(sc, "Failed to create dma map for "
                          "'fp %d rx sge mbuf %d' (%d)\n", i, j, rc);
                return (1);
            }
        }

        /* create dma map for the spare rx sge mbuf cluster */
        if (bus_dmamap_create(fp->rx_sge_mbuf_tag,
                              BUS_DMA_NOWAIT,
                              &fp->rx_sge_mbuf_spare_map)) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to create dma map for "
                      "'fp %d spare rx sge mbuf' (%d)\n", i, rc);
            return (1);
        }

        /***************************/
        /* FP RX TPA MBUF DMA MAPS */
        /***************************/

        /* create dma maps for the rx tpa mbuf clusters */
        max_agg_queues = MAX_AGG_QS(sc);

        for (j = 0; j < max_agg_queues; j++) {
            if (bus_dmamap_create(fp->rx_mbuf_tag,
                                  BUS_DMA_NOWAIT,
                                  &fp->rx_tpa_info[j].bd.m_map)) {
                /* XXX unwind and free previous fastpath allocations */
                BLOGE(sc, "Failed to create dma map for "
                          "'fp %d rx tpa mbuf %d' (%d)\n", i, j, rc);
                return (1);
            }
        }

        /* create dma map for the spare rx tpa mbuf cluster */
        if (bus_dmamap_create(fp->rx_mbuf_tag,
                              BUS_DMA_NOWAIT,
                              &fp->rx_tpa_info_mbuf_spare_map)) {
            /* XXX unwind and free previous fastpath allocations */
            BLOGE(sc, "Failed to create dma map for "
                      "'fp %d spare rx tpa mbuf' (%d)\n", i, rc);
            return (1);
        }

        bxe_init_sge_ring_bit_mask(fp);
    }

    return (0);
}

static void
bxe_free_hsi_mem(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int max_agg_queues;
    int i, j;

    if (sc->parent_dma_tag == NULL) {
        return; /* assume nothing was allocated */
    }

    for (i = 0; i < sc->num_queues; i++) {
        fp = &sc->fp[i];

        /*******************/
        /* FP STATUS BLOCK */
        /*******************/

        bxe_dma_free(sc, &fp->sb_dma);
        memset(&fp->status_block, 0, sizeof(fp->status_block));

        /******************/
        /* FP TX BD CHAIN */
        /******************/

        bxe_dma_free(sc, &fp->tx_dma);
        fp->tx_chain = NULL;

        /******************/
        /* FP RX BD CHAIN */
        /******************/

        bxe_dma_free(sc, &fp->rx_dma);
        fp->rx_chain = NULL;

        /*******************/
        /* FP RX RCQ CHAIN */
        /*******************/

        bxe_dma_free(sc, &fp->rcq_dma);
        fp->rcq_chain = NULL;

        /*******************/
        /* FP RX SGE CHAIN */
        /*******************/

        bxe_dma_free(sc, &fp->rx_sge_dma);
        fp->rx_sge_chain = NULL;

        /***********************/
        /* FP TX MBUF DMA MAPS */
        /***********************/

        if (fp->tx_mbuf_tag != NULL) {
            for (j = 0; j < TX_BD_TOTAL; j++) {
                if (fp->tx_mbuf_chain[j].m_map != NULL) {
                    bus_dmamap_unload(fp->tx_mbuf_tag,
                                      fp->tx_mbuf_chain[j].m_map);
                    bus_dmamap_destroy(fp->tx_mbuf_tag,
                                       fp->tx_mbuf_chain[j].m_map);
                }
            }

            bus_dma_tag_destroy(fp->tx_mbuf_tag);
            fp->tx_mbuf_tag = NULL;
        }

        /***********************/
        /* FP RX MBUF DMA MAPS */
        /***********************/

        if (fp->rx_mbuf_tag != NULL) {
            for (j = 0; j < RX_BD_TOTAL; j++) {
                if (fp->rx_mbuf_chain[j].m_map != NULL) {
                    bus_dmamap_unload(fp->rx_mbuf_tag,
                                      fp->rx_mbuf_chain[j].m_map);
                    bus_dmamap_destroy(fp->rx_mbuf_tag,
                                       fp->rx_mbuf_chain[j].m_map);
                }
            }

            if (fp->rx_mbuf_spare_map != NULL) {
                bus_dmamap_unload(fp->rx_mbuf_tag, fp->rx_mbuf_spare_map);
                bus_dmamap_destroy(fp->rx_mbuf_tag, fp->rx_mbuf_spare_map);
            }

            /***************************/
            /* FP RX TPA MBUF DMA MAPS */
            /***************************/

            max_agg_queues = MAX_AGG_QS(sc);

            for (j = 0; j < max_agg_queues; j++) {
                if (fp->rx_tpa_info[j].bd.m_map != NULL) {
                    bus_dmamap_unload(fp->rx_mbuf_tag,
                                      fp->rx_tpa_info[j].bd.m_map);
                    bus_dmamap_destroy(fp->rx_mbuf_tag,
                                       fp->rx_tpa_info[j].bd.m_map);
                }
            }

            if (fp->rx_tpa_info_mbuf_spare_map != NULL) {
                bus_dmamap_unload(fp->rx_mbuf_tag,
                                  fp->rx_tpa_info_mbuf_spare_map);
                bus_dmamap_destroy(fp->rx_mbuf_tag,
                                   fp->rx_tpa_info_mbuf_spare_map);
            }

            bus_dma_tag_destroy(fp->rx_mbuf_tag);
            fp->rx_mbuf_tag = NULL;
        }

        /***************************/
        /* FP RX SGE MBUF DMA MAPS */
        /***************************/

        if (fp->rx_sge_mbuf_tag != NULL) {
            for (j = 0; j < RX_SGE_TOTAL; j++) {
                if (fp->rx_sge_mbuf_chain[j].m_map != NULL) {
                    bus_dmamap_unload(fp->rx_sge_mbuf_tag,
                                      fp->rx_sge_mbuf_chain[j].m_map);
                    bus_dmamap_destroy(fp->rx_sge_mbuf_tag,
                                       fp->rx_sge_mbuf_chain[j].m_map);
                }
            }

            if (fp->rx_sge_mbuf_spare_map != NULL) {
                bus_dmamap_unload(fp->rx_sge_mbuf_tag,
                                  fp->rx_sge_mbuf_spare_map);
                bus_dmamap_destroy(fp->rx_sge_mbuf_tag,
                                   fp->rx_sge_mbuf_spare_map);
            }

            bus_dma_tag_destroy(fp->rx_sge_mbuf_tag);
            fp->rx_sge_mbuf_tag = NULL;
        }
    }

    /***************************/
    /* FW DECOMPRESSION BUFFER */
    /***************************/

    bxe_dma_free(sc, &sc->gz_buf_dma);
    sc->gz_buf = NULL;
    free(sc->gz_strm, M_DEVBUF);
    sc->gz_strm = NULL;

    /*******************/
    /* SLOW PATH QUEUE */
    /*******************/

    bxe_dma_free(sc, &sc->spq_dma);
    sc->spq = NULL;

    /*************/
    /* SLOW PATH */
    /*************/

    bxe_dma_free(sc, &sc->sp_dma);
    sc->sp = NULL;

    /***************/
    /* EVENT QUEUE */
    /***************/

    bxe_dma_free(sc, &sc->eq_dma);
    sc->eq = NULL;

    /************************/
    /* DEFAULT STATUS BLOCK */
    /************************/

    bxe_dma_free(sc, &sc->def_sb_dma);
    sc->def_sb = NULL;

    bus_dma_tag_destroy(sc->parent_dma_tag);
    sc->parent_dma_tag = NULL;
}

/*
 * Previous driver DMAE transaction may have occurred when pre-boot stage
 * ended and boot began. This would invalidate the addresses of the
 * transaction, resulting in was-error bit set in the PCI causing all
 * hw-to-host PCIe transactions to timeout. If this happened we want to clear
 * the interrupt which detected this from the pglueb and the was-done bit
 */
static void
bxe_prev_interrupted_dmae(struct bxe_softc *sc)
{
    uint32_t val;

    if (!CHIP_IS_E1x(sc)) {
        val = REG_RD(sc, PGLUE_B_REG_PGLUE_B_INT_STS);
        if (val & PGLUE_B_PGLUE_B_INT_STS_REG_WAS_ERROR_ATTN) {
            BLOGD(sc, DBG_LOAD,
                  "Clearing 'was-error' bit that was set in pglueb");
            REG_WR(sc, PGLUE_B_REG_WAS_ERROR_PF_7_0_CLR, 1 << SC_FUNC(sc));
        }
    }
}

static int
bxe_prev_mcp_done(struct bxe_softc *sc)
{
    uint32_t rc = bxe_fw_command(sc, DRV_MSG_CODE_UNLOAD_DONE,
                                 DRV_MSG_CODE_UNLOAD_SKIP_LINK_RESET);
    if (!rc) {
        BLOGE(sc, "MCP response failure, aborting\n");
        return (-1);
    }

    return (0);
}

static struct bxe_prev_list_node *
bxe_prev_path_get_entry(struct bxe_softc *sc)
{
    struct bxe_prev_list_node *tmp;

    LIST_FOREACH(tmp, &bxe_prev_list, node) {
        if ((sc->pcie_bus == tmp->bus) &&
            (sc->pcie_device == tmp->slot) &&
            (SC_PATH(sc) == tmp->path)) {
            return (tmp);
        }
    }

    return (NULL);
}

static uint8_t
bxe_prev_is_path_marked(struct bxe_softc *sc)
{
    struct bxe_prev_list_node *tmp;
    int rc = FALSE;

    mtx_lock(&bxe_prev_mtx);

    tmp = bxe_prev_path_get_entry(sc);
    if (tmp) {
        if (tmp->aer) {
            BLOGD(sc, DBG_LOAD,
                  "Path %d/%d/%d was marked by AER\n",
                  sc->pcie_bus, sc->pcie_device, SC_PATH(sc));
        } else {
            rc = TRUE;
            BLOGD(sc, DBG_LOAD,
                  "Path %d/%d/%d was already cleaned from previous drivers\n",
                  sc->pcie_bus, sc->pcie_device, SC_PATH(sc));
        }
    }

    mtx_unlock(&bxe_prev_mtx);

    return (rc);
}

static int
bxe_prev_mark_path(struct bxe_softc *sc,
                   uint8_t          after_undi)
{
    struct bxe_prev_list_node *tmp;

    mtx_lock(&bxe_prev_mtx);

    /* Check whether the entry for this path already exists */
    tmp = bxe_prev_path_get_entry(sc);
    if (tmp) {
        if (!tmp->aer) {
            BLOGD(sc, DBG_LOAD,
                  "Re-marking AER in path %d/%d/%d\n",
                  sc->pcie_bus, sc->pcie_device, SC_PATH(sc));
        } else {
            BLOGD(sc, DBG_LOAD,
                  "Removing AER indication from path %d/%d/%d\n",
                  sc->pcie_bus, sc->pcie_device, SC_PATH(sc));
            tmp->aer = 0;
        }

        mtx_unlock(&bxe_prev_mtx);
        return (0);
    }

    mtx_unlock(&bxe_prev_mtx);

    /* Create an entry for this path and add it */
    tmp = malloc(sizeof(struct bxe_prev_list_node), M_DEVBUF,
                 (M_NOWAIT | M_ZERO));
    if (!tmp) {
        BLOGE(sc, "Failed to allocate 'bxe_prev_list_node'\n");
        return (-1);
    }

    tmp->bus  = sc->pcie_bus;
    tmp->slot = sc->pcie_device;
    tmp->path = SC_PATH(sc);
    tmp->aer  = 0;
    tmp->undi = after_undi ? (1 << SC_PORT(sc)) : 0;

    mtx_lock(&bxe_prev_mtx);

    BLOGD(sc, DBG_LOAD,
          "Marked path %d/%d/%d - finished previous unload\n",
          sc->pcie_bus, sc->pcie_device, SC_PATH(sc));
    LIST_INSERT_HEAD(&bxe_prev_list, tmp, node);

    mtx_unlock(&bxe_prev_mtx);

    return (0);
}

static int
bxe_do_flr(struct bxe_softc *sc)
{
    int i;

    /* only E2 and onwards support FLR */
    if (CHIP_IS_E1x(sc)) {
        BLOGD(sc, DBG_LOAD, "FLR not supported in E1/E1H\n");
        return (-1);
    }

    /* only bootcode REQ_BC_VER_4_INITIATE_FLR and onwards support flr */
    if (sc->devinfo.bc_ver < REQ_BC_VER_4_INITIATE_FLR) {
        BLOGD(sc, DBG_LOAD, "FLR not supported by BC_VER: 0x%08x\n",
              sc->devinfo.bc_ver);
        return (-1);
    }

    /* Wait for Transaction Pending bit clean */
    for (i = 0; i < 4; i++) {
        if (i) {
            DELAY(((1 << (i - 1)) * 100) * 1000);
        }

        if (!bxe_is_pcie_pending(sc)) {
            goto clear;
        }
    }

    BLOGE(sc, "PCIE transaction is not cleared, "
              "proceeding with reset anyway\n");

clear:

    BLOGD(sc, DBG_LOAD, "Initiating FLR\n");
    bxe_fw_command(sc, DRV_MSG_CODE_INITIATE_FLR, 0);

    return (0);
}

struct bxe_mac_vals {
    uint32_t xmac_addr;
    uint32_t xmac_val;
    uint32_t emac_addr;
    uint32_t emac_val;
    uint32_t umac_addr;
    uint32_t umac_val;
    uint32_t bmac_addr;
    uint32_t bmac_val[2];
};

static void
bxe_prev_unload_close_mac(struct bxe_softc *sc,
                          struct bxe_mac_vals *vals)
{
    uint32_t val, base_addr, offset, mask, reset_reg;
    uint8_t mac_stopped = FALSE;
    uint8_t port = SC_PORT(sc);
    uint32_t wb_data[2];

    /* reset addresses as they also mark which values were changed */
    vals->bmac_addr = 0;
    vals->umac_addr = 0;
    vals->xmac_addr = 0;
    vals->emac_addr = 0;

    reset_reg = REG_RD(sc, MISC_REG_RESET_REG_2);

    if (!CHIP_IS_E3(sc)) {
        val = REG_RD(sc, NIG_REG_BMAC0_REGS_OUT_EN + port * 4);
        mask = MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port;
        if ((mask & reset_reg) && val) {
            BLOGD(sc, DBG_LOAD, "Disable BMAC Rx\n");
            base_addr = SC_PORT(sc) ? NIG_REG_INGRESS_BMAC1_MEM
                                    : NIG_REG_INGRESS_BMAC0_MEM;
            offset = CHIP_IS_E2(sc) ? BIGMAC2_REGISTER_BMAC_CONTROL
                                    : BIGMAC_REGISTER_BMAC_CONTROL;

            /*
             * use rd/wr since we cannot use dmae. This is safe
             * since MCP won't access the bus due to the request
             * to unload, and no function on the path can be
             * loaded at this time.
             */
            wb_data[0] = REG_RD(sc, base_addr + offset);
            wb_data[1] = REG_RD(sc, base_addr + offset + 0x4);
            vals->bmac_addr = base_addr + offset;
            vals->bmac_val[0] = wb_data[0];
            vals->bmac_val[1] = wb_data[1];
            wb_data[0] &= ~ELINK_BMAC_CONTROL_RX_ENABLE;
            REG_WR(sc, vals->bmac_addr, wb_data[0]);
            REG_WR(sc, vals->bmac_addr + 0x4, wb_data[1]);
        }

        BLOGD(sc, DBG_LOAD, "Disable EMAC Rx\n");
        vals->emac_addr = NIG_REG_NIG_EMAC0_EN + SC_PORT(sc)*4;
        vals->emac_val = REG_RD(sc, vals->emac_addr);
        REG_WR(sc, vals->emac_addr, 0);
        mac_stopped = TRUE;
    } else {
        if (reset_reg & MISC_REGISTERS_RESET_REG_2_XMAC) {
            BLOGD(sc, DBG_LOAD, "Disable XMAC Rx\n");
            base_addr = SC_PORT(sc) ? GRCBASE_XMAC1 : GRCBASE_XMAC0;
            val = REG_RD(sc, base_addr + XMAC_REG_PFC_CTRL_HI);
            REG_WR(sc, base_addr + XMAC_REG_PFC_CTRL_HI, val & ~(1 << 1));
            REG_WR(sc, base_addr + XMAC_REG_PFC_CTRL_HI, val | (1 << 1));
            vals->xmac_addr = base_addr + XMAC_REG_CTRL;
            vals->xmac_val = REG_RD(sc, vals->xmac_addr);
            REG_WR(sc, vals->xmac_addr, 0);
            mac_stopped = TRUE;
        }

        mask = MISC_REGISTERS_RESET_REG_2_UMAC0 << port;
        if (mask & reset_reg) {
            BLOGD(sc, DBG_LOAD, "Disable UMAC Rx\n");
            base_addr = SC_PORT(sc) ? GRCBASE_UMAC1 : GRCBASE_UMAC0;
            vals->umac_addr = base_addr + UMAC_REG_COMMAND_CONFIG;
            vals->umac_val = REG_RD(sc, vals->umac_addr);
            REG_WR(sc, vals->umac_addr, 0);
            mac_stopped = TRUE;
        }
    }

    if (mac_stopped) {
        DELAY(20000);
    }
}

#define BXE_PREV_UNDI_PROD_ADDR(p)  (BAR_TSTRORM_INTMEM + 0x1508 + ((p) << 4))
#define BXE_PREV_UNDI_RCQ(val)      ((val) & 0xffff)
#define BXE_PREV_UNDI_BD(val)       ((val) >> 16 & 0xffff)
#define BXE_PREV_UNDI_PROD(rcq, bd) ((bd) << 16 | (rcq))

static void
bxe_prev_unload_undi_inc(struct bxe_softc *sc,
                         uint8_t          port,
                         uint8_t          inc)
{
    uint16_t rcq, bd;
    uint32_t tmp_reg = REG_RD(sc, BXE_PREV_UNDI_PROD_ADDR(port));

    rcq = BXE_PREV_UNDI_RCQ(tmp_reg) + inc;
    bd = BXE_PREV_UNDI_BD(tmp_reg) + inc;

    tmp_reg = BXE_PREV_UNDI_PROD(rcq, bd);
    REG_WR(sc, BXE_PREV_UNDI_PROD_ADDR(port), tmp_reg);

    BLOGD(sc, DBG_LOAD,
          "UNDI producer [%d] rings bd -> 0x%04x, rcq -> 0x%04x\n",
          port, bd, rcq);
}

static int
bxe_prev_unload_common(struct bxe_softc *sc)
{
    uint32_t reset_reg, tmp_reg = 0, rc;
    uint8_t prev_undi = FALSE;
    struct bxe_mac_vals mac_vals;
    uint32_t timer_count = 1000;
    uint32_t prev_brb;

    /*
     * It is possible a previous function received 'common' answer,
     * but hasn't loaded yet, therefore creating a scenario of
     * multiple functions receiving 'common' on the same path.
     */
    BLOGD(sc, DBG_LOAD, "Common unload Flow\n");

    memset(&mac_vals, 0, sizeof(mac_vals));

    if (bxe_prev_is_path_marked(sc)) {
        return (bxe_prev_mcp_done(sc));
    }

    reset_reg = REG_RD(sc, MISC_REG_RESET_REG_1);

    /* Reset should be performed after BRB is emptied */
    if (reset_reg & MISC_REGISTERS_RESET_REG_1_RST_BRB1) {
        /* Close the MAC Rx to prevent BRB from filling up */
        bxe_prev_unload_close_mac(sc, &mac_vals);

        /* close LLH filters towards the BRB */
        elink_set_rx_filter(&sc->link_params, 0);

        /*
         * Check if the UNDI driver was previously loaded.
         * UNDI driver initializes CID offset for normal bell to 0x7
         */
        if (reset_reg & MISC_REGISTERS_RESET_REG_1_RST_DORQ) {
            tmp_reg = REG_RD(sc, DORQ_REG_NORM_CID_OFST);
            if (tmp_reg == 0x7) {
                BLOGD(sc, DBG_LOAD, "UNDI previously loaded\n");
                prev_undi = TRUE;
                /* clear the UNDI indication */
                REG_WR(sc, DORQ_REG_NORM_CID_OFST, 0);
                /* clear possible idle check errors */
                REG_RD(sc, NIG_REG_NIG_INT_STS_CLR_0);
            }
        }

        /* wait until BRB is empty */
        tmp_reg = REG_RD(sc, BRB1_REG_NUM_OF_FULL_BLOCKS);
        while (timer_count) {
            prev_brb = tmp_reg;

            tmp_reg = REG_RD(sc, BRB1_REG_NUM_OF_FULL_BLOCKS);
            if (!tmp_reg) {
                break;
            }

            BLOGD(sc, DBG_LOAD, "BRB still has 0x%08x\n", tmp_reg);

            /* reset timer as long as BRB actually gets emptied */
            if (prev_brb > tmp_reg) {
                timer_count = 1000;
            } else {
                timer_count--;
            }

            /* If UNDI resides in memory, manually increment it */
            if (prev_undi) {
                bxe_prev_unload_undi_inc(sc, SC_PORT(sc), 1);
            }

            DELAY(10);
        }

        if (!timer_count) {
            BLOGE(sc, "Failed to empty BRB\n");
        }
    }

    /* No packets are in the pipeline, path is ready for reset */
    bxe_reset_common(sc);

    if (mac_vals.xmac_addr) {
        REG_WR(sc, mac_vals.xmac_addr, mac_vals.xmac_val);
    }
    if (mac_vals.umac_addr) {
        REG_WR(sc, mac_vals.umac_addr, mac_vals.umac_val);
    }
    if (mac_vals.emac_addr) {
        REG_WR(sc, mac_vals.emac_addr, mac_vals.emac_val);
    }
    if (mac_vals.bmac_addr) {
        REG_WR(sc, mac_vals.bmac_addr, mac_vals.bmac_val[0]);
        REG_WR(sc, mac_vals.bmac_addr + 4, mac_vals.bmac_val[1]);
    }

    rc = bxe_prev_mark_path(sc, prev_undi);
    if (rc) {
        bxe_prev_mcp_done(sc);
        return (rc);
    }

    return (bxe_prev_mcp_done(sc));
}

static int
bxe_prev_unload_uncommon(struct bxe_softc *sc)
{
    int rc;

    BLOGD(sc, DBG_LOAD, "Uncommon unload Flow\n");

    /* Test if previous unload process was already finished for this path */
    if (bxe_prev_is_path_marked(sc)) {
        return (bxe_prev_mcp_done(sc));
    }

    BLOGD(sc, DBG_LOAD, "Path is unmarked\n");

    /*
     * If function has FLR capabilities, and existing FW version matches
     * the one required, then FLR will be sufficient to clean any residue
     * left by previous driver
     */
    rc = bxe_nic_load_analyze_req(sc, FW_MSG_CODE_DRV_LOAD_FUNCTION);
    if (!rc) {
        /* fw version is good */
        BLOGD(sc, DBG_LOAD, "FW version matches our own, attempting FLR\n");
        rc = bxe_do_flr(sc);
    }

    if (!rc) {
        /* FLR was performed */
        BLOGD(sc, DBG_LOAD, "FLR successful\n");
        return (0);
    }

    BLOGD(sc, DBG_LOAD, "Could not FLR\n");

    /* Close the MCP request, return failure*/
    rc = bxe_prev_mcp_done(sc);
    if (!rc) {
        rc = BXE_PREV_WAIT_NEEDED;
    }

    return (rc);
}

static int
bxe_prev_unload(struct bxe_softc *sc)
{
    int time_counter = 10;
    uint32_t fw, hw_lock_reg, hw_lock_val;
    uint32_t rc = 0;

    /*
     * Clear HW from errors which may have resulted from an interrupted
     * DMAE transaction.
     */
    bxe_prev_interrupted_dmae(sc);

    /* Release previously held locks */
    hw_lock_reg =
        (SC_FUNC(sc) <= 5) ?
            (MISC_REG_DRIVER_CONTROL_1 + SC_FUNC(sc) * 8) :
            (MISC_REG_DRIVER_CONTROL_7 + (SC_FUNC(sc) - 6) * 8);

    hw_lock_val = (REG_RD(sc, hw_lock_reg));
    if (hw_lock_val) {
        if (hw_lock_val & HW_LOCK_RESOURCE_NVRAM) {
            BLOGD(sc, DBG_LOAD, "Releasing previously held NVRAM lock\n");
            REG_WR(sc, MCP_REG_MCPR_NVM_SW_ARB,
                   (MCPR_NVM_SW_ARB_ARB_REQ_CLR1 << SC_PORT(sc)));
        }
        BLOGD(sc, DBG_LOAD, "Releasing previously held HW lock\n");
        REG_WR(sc, hw_lock_reg, 0xffffffff);
    } else {
        BLOGD(sc, DBG_LOAD, "No need to release HW/NVRAM locks\n");
    }

    if (MCPR_ACCESS_LOCK_LOCK & REG_RD(sc, MCP_REG_MCPR_ACCESS_LOCK)) {
        BLOGD(sc, DBG_LOAD, "Releasing previously held ALR\n");
        REG_WR(sc, MCP_REG_MCPR_ACCESS_LOCK, 0);
    }

    do {
        /* Lock MCP using an unload request */
        fw = bxe_fw_command(sc, DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS, 0);
        if (!fw) {
            BLOGE(sc, "MCP response failure, aborting\n");
            rc = -1;
            break;
        }

        if (fw == FW_MSG_CODE_DRV_UNLOAD_COMMON) {
            rc = bxe_prev_unload_common(sc);
            break;
        }

        /* non-common reply from MCP night require looping */
        rc = bxe_prev_unload_uncommon(sc);
        if (rc != BXE_PREV_WAIT_NEEDED) {
            break;
        }

        DELAY(20000);
    } while (--time_counter);

    if (!time_counter || rc) {
        BLOGE(sc, "Failed to unload previous driver!"
            " time_counter %d rc %d\n", time_counter, rc);
        rc = -1;
    }

    return (rc);
}

void
bxe_dcbx_set_state(struct bxe_softc *sc,
                   uint8_t          dcb_on,
                   uint32_t         dcbx_enabled)
{
    if (!CHIP_IS_E1x(sc)) {
        sc->dcb_state = dcb_on;
        sc->dcbx_enabled = dcbx_enabled;
    } else {
        sc->dcb_state = FALSE;
        sc->dcbx_enabled = BXE_DCBX_ENABLED_INVALID;
    }
    BLOGD(sc, DBG_LOAD,
          "DCB state [%s:%s]\n",
          dcb_on ? "ON" : "OFF",
          (dcbx_enabled == BXE_DCBX_ENABLED_OFF) ? "user-mode" :
          (dcbx_enabled == BXE_DCBX_ENABLED_ON_NEG_OFF) ? "on-chip static" :
          (dcbx_enabled == BXE_DCBX_ENABLED_ON_NEG_ON) ?
          "on-chip with negotiation" : "invalid");
}

/* must be called after sriov-enable */
static int
bxe_set_qm_cid_count(struct bxe_softc *sc)
{
    int cid_count = BXE_L2_MAX_CID(sc);

    if (IS_SRIOV(sc)) {
        cid_count += BXE_VF_CIDS;
    }

    if (CNIC_SUPPORT(sc)) {
        cid_count += CNIC_CID_MAX;
    }

    return (roundup(cid_count, QM_CID_ROUND));
}

static void
bxe_init_multi_cos(struct bxe_softc *sc)
{
    int pri, cos;

    uint32_t pri_map = 0; /* XXX change to user config */

    for (pri = 0; pri < BXE_MAX_PRIORITY; pri++) {
        cos = ((pri_map & (0xf << (pri * 4))) >> (pri * 4));
        if (cos < sc->max_cos) {
            sc->prio_to_cos[pri] = cos;
        } else {
            BLOGW(sc, "Invalid COS %d for priority %d "
                      "(max COS is %d), setting to 0\n",
                  cos, pri, (sc->max_cos - 1));
            sc->prio_to_cos[pri] = 0;
        }
    }
}

static int
bxe_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct bxe_softc *sc;
    int error, result;

    result = 0;
    error = sysctl_handle_int(oidp, &result, 0, req);

    if (error || !req->newptr) {
        return (error);
    }

    if (result == 1) {
        uint32_t  temp;
        sc = (struct bxe_softc *)arg1;

        BLOGI(sc, "... dumping driver state ...\n");
        temp = SHMEM2_RD(sc, temperature_in_half_celsius);
        BLOGI(sc, "\t Device Temperature = %d Celsius\n", (temp/2));
    }

    return (error);
}

static int
bxe_sysctl_eth_stat(SYSCTL_HANDLER_ARGS)
{
    struct bxe_softc *sc = (struct bxe_softc *)arg1;
    uint32_t *eth_stats = (uint32_t *)&sc->eth_stats;
    uint32_t *offset;
    uint64_t value = 0;
    int index = (int)arg2;

    if (index >= BXE_NUM_ETH_STATS) {
        BLOGE(sc, "bxe_eth_stats index out of range (%d)\n", index);
        return (-1);
    }

    offset = (eth_stats + bxe_eth_stats_arr[index].offset);

    switch (bxe_eth_stats_arr[index].size) {
    case 4:
        value = (uint64_t)*offset;
        break;
    case 8:
        value = HILO_U64(*offset, *(offset + 1));
        break;
    default:
        BLOGE(sc, "Invalid bxe_eth_stats size (index=%d size=%d)\n",
              index, bxe_eth_stats_arr[index].size);
        return (-1);
    }

    return (sysctl_handle_64(oidp, &value, 0, req));
}

static int
bxe_sysctl_eth_q_stat(SYSCTL_HANDLER_ARGS)
{
    struct bxe_softc *sc = (struct bxe_softc *)arg1;
    uint32_t *eth_stats;
    uint32_t *offset;
    uint64_t value = 0;
    uint32_t q_stat = (uint32_t)arg2;
    uint32_t fp_index = ((q_stat >> 16) & 0xffff);
    uint32_t index = (q_stat & 0xffff);

    eth_stats = (uint32_t *)&sc->fp[fp_index].eth_q_stats;

    if (index >= BXE_NUM_ETH_Q_STATS) {
        BLOGE(sc, "bxe_eth_q_stats index out of range (%d)\n", index);
        return (-1);
    }

    offset = (eth_stats + bxe_eth_q_stats_arr[index].offset);

    switch (bxe_eth_q_stats_arr[index].size) {
    case 4:
        value = (uint64_t)*offset;
        break;
    case 8:
        value = HILO_U64(*offset, *(offset + 1));
        break;
    default:
        BLOGE(sc, "Invalid bxe_eth_q_stats size (index=%d size=%d)\n",
              index, bxe_eth_q_stats_arr[index].size);
        return (-1);
    }

    return (sysctl_handle_64(oidp, &value, 0, req));
}

static void bxe_force_link_reset(struct bxe_softc *sc)
{

        bxe_acquire_phy_lock(sc);
        elink_link_reset(&sc->link_params, &sc->link_vars, 1);
        bxe_release_phy_lock(sc);
}

static int
bxe_sysctl_pauseparam(SYSCTL_HANDLER_ARGS)
{
        struct bxe_softc *sc = (struct bxe_softc *)arg1;;
        uint32_t cfg_idx = bxe_get_link_cfg_idx(sc);
        int rc = 0;
        int error;
        int result;


        error = sysctl_handle_int(oidp, &sc->bxe_pause_param, 0, req);

        if (error || !req->newptr) {
                return (error);
        }
        if ((sc->bxe_pause_param < 0) ||  (sc->bxe_pause_param > 8)) {
                BLOGW(sc, "invalid pause param (%d) - use intergers between 1 & 8\n",sc->bxe_pause_param);
                sc->bxe_pause_param = 8;
        }

        result = (sc->bxe_pause_param << PORT_FEATURE_FLOW_CONTROL_SHIFT);


        if((result & 0x400) && !(sc->port.supported[cfg_idx] & ELINK_SUPPORTED_Autoneg))  {
                        BLOGW(sc, "Does not support Autoneg pause_param %d\n", sc->bxe_pause_param);
                        return -EINVAL;
        }

        if(IS_MF(sc))
                return 0;
       sc->link_params.req_flow_ctrl[cfg_idx] = ELINK_FLOW_CTRL_AUTO;
        if(result & ELINK_FLOW_CTRL_RX)
                sc->link_params.req_flow_ctrl[cfg_idx] |= ELINK_FLOW_CTRL_RX;

        if(result & ELINK_FLOW_CTRL_TX)
                sc->link_params.req_flow_ctrl[cfg_idx] |= ELINK_FLOW_CTRL_TX;
        if(sc->link_params.req_flow_ctrl[cfg_idx] == ELINK_FLOW_CTRL_AUTO)
                sc->link_params.req_flow_ctrl[cfg_idx] = ELINK_FLOW_CTRL_NONE;

        if(result & 0x400) {
                if (sc->link_params.req_line_speed[cfg_idx] == ELINK_SPEED_AUTO_NEG) {
                        sc->link_params.req_flow_ctrl[cfg_idx] =
                                ELINK_FLOW_CTRL_AUTO;
                }
                sc->link_params.req_fc_auto_adv = 0;
                if (result & ELINK_FLOW_CTRL_RX)
                        sc->link_params.req_fc_auto_adv |= ELINK_FLOW_CTRL_RX;

                if (result & ELINK_FLOW_CTRL_TX)
                        sc->link_params.req_fc_auto_adv |= ELINK_FLOW_CTRL_TX;
                if (!sc->link_params.req_fc_auto_adv)
                        sc->link_params.req_fc_auto_adv |= ELINK_FLOW_CTRL_NONE;
        }
         if (IS_PF(sc)) {
                        if (sc->link_vars.link_up) {
                                bxe_stats_handle(sc, STATS_EVENT_STOP);
                        }
			if (if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) {
                        bxe_force_link_reset(sc);
                        bxe_acquire_phy_lock(sc);

                        rc = elink_phy_init(&sc->link_params, &sc->link_vars);

                        bxe_release_phy_lock(sc);

                        bxe_calc_fc_adv(sc);
                        }
        }
        return rc;
}


static void
bxe_add_sysctls(struct bxe_softc *sc)
{
    struct sysctl_ctx_list *ctx;
    struct sysctl_oid_list *children;
    struct sysctl_oid *queue_top, *queue;
    struct sysctl_oid_list *queue_top_children, *queue_children;
    char queue_num_buf[32];
    uint32_t q_stat;
    int i, j;

    ctx = device_get_sysctl_ctx(sc->dev);
    children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));

    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "version",
                      CTLFLAG_RD, BXE_DRIVER_VERSION, 0,
                      "version");

    snprintf(sc->fw_ver_str, sizeof(sc->fw_ver_str), "%d.%d.%d.%d",
             BCM_5710_FW_MAJOR_VERSION,
             BCM_5710_FW_MINOR_VERSION,
             BCM_5710_FW_REVISION_VERSION,
             BCM_5710_FW_ENGINEERING_VERSION);

    snprintf(sc->mf_mode_str, sizeof(sc->mf_mode_str), "%s",
        ((sc->devinfo.mf_info.mf_mode == SINGLE_FUNCTION)     ? "Single"  :
         (sc->devinfo.mf_info.mf_mode == MULTI_FUNCTION_SD)   ? "MF-SD"   :
         (sc->devinfo.mf_info.mf_mode == MULTI_FUNCTION_SI)   ? "MF-SI"   :
         (sc->devinfo.mf_info.mf_mode == MULTI_FUNCTION_AFEX) ? "MF-AFEX" :
                                                                "Unknown"));
    SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "mf_vnics",
                    CTLFLAG_RD, &sc->devinfo.mf_info.vnics_per_port, 0,
                    "multifunction vnics per port");

    snprintf(sc->pci_link_str, sizeof(sc->pci_link_str), "%s x%d",
        ((sc->devinfo.pcie_link_speed == 1) ? "2.5GT/s" :
         (sc->devinfo.pcie_link_speed == 2) ? "5.0GT/s" :
         (sc->devinfo.pcie_link_speed == 4) ? "8.0GT/s" :
                                              "???GT/s"),
        sc->devinfo.pcie_link_width);

    sc->debug = bxe_debug;

#if __FreeBSD_version >= 900000
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "bc_version",
                      CTLFLAG_RD, sc->devinfo.bc_ver_str, 0,
                      "bootcode version");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "fw_version",
                      CTLFLAG_RD, sc->fw_ver_str, 0,
                      "firmware version");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "mf_mode",
                      CTLFLAG_RD, sc->mf_mode_str, 0,
                      "multifunction mode");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "mac_addr",
                      CTLFLAG_RD, sc->mac_addr_str, 0,
                      "mac address");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "pci_link",
                      CTLFLAG_RD, sc->pci_link_str, 0,
                      "pci link status");
    SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, "debug",
                    CTLFLAG_RW, &sc->debug,
                    "debug logging mode");
#else
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "bc_version",
                      CTLFLAG_RD, &sc->devinfo.bc_ver_str, 0,
                      "bootcode version");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "fw_version",
                      CTLFLAG_RD, &sc->fw_ver_str, 0,
                      "firmware version");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "mf_mode",
                      CTLFLAG_RD, &sc->mf_mode_str, 0,
                      "multifunction mode");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "mac_addr",
                      CTLFLAG_RD, &sc->mac_addr_str, 0,
                      "mac address");
    SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "pci_link",
                      CTLFLAG_RD, &sc->pci_link_str, 0,
                      "pci link status");
    SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "debug",
                    CTLFLAG_RW, &sc->debug, 0,
                    "debug logging mode");
#endif /* #if __FreeBSD_version >= 900000 */

    sc->trigger_grcdump = 0;
    SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "trigger_grcdump",
                   CTLFLAG_RW, &sc->trigger_grcdump, 0,
                   "trigger grcdump should be invoked"
                   "  before collecting grcdump");

    sc->grcdump_started = 0;
    sc->grcdump_done = 0;
    SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "grcdump_done",
                   CTLFLAG_RD, &sc->grcdump_done, 0,
                   "set by driver when grcdump is done");

    sc->rx_budget = bxe_rx_budget;
    SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "rx_budget",
                    CTLFLAG_RW, &sc->rx_budget, 0,
                    "rx processing budget");

   SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "pause_param",
                    CTLTYPE_UINT | CTLFLAG_RW, sc, 0,
                    bxe_sysctl_pauseparam, "IU",
                    "need pause frames- DEF:0/TX:1/RX:2/BOTH:3/AUTO:4/AUTOTX:5/AUTORX:6/AUTORXTX:7/NONE:8");


    SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "state",
                    CTLTYPE_UINT | CTLFLAG_RW, sc, 0,
                    bxe_sysctl_state, "IU", "dump driver state");

    for (i = 0; i < BXE_NUM_ETH_STATS; i++) {
        SYSCTL_ADD_PROC(ctx, children, OID_AUTO,
                        bxe_eth_stats_arr[i].string,
                        CTLTYPE_U64 | CTLFLAG_RD, sc, i,
                        bxe_sysctl_eth_stat, "LU",
                        bxe_eth_stats_arr[i].string);
    }

    /* add a new parent node for all queues "dev.bxe.#.queue" */
    queue_top = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "queue",
                                CTLFLAG_RD, NULL, "queue");
    queue_top_children = SYSCTL_CHILDREN(queue_top);

    for (i = 0; i < sc->num_queues; i++) {
        /* add a new parent node for a single queue "dev.bxe.#.queue.#" */
        snprintf(queue_num_buf, sizeof(queue_num_buf), "%d", i);
        queue = SYSCTL_ADD_NODE(ctx, queue_top_children, OID_AUTO,
                                queue_num_buf, CTLFLAG_RD, NULL,
                                "single queue");
        queue_children = SYSCTL_CHILDREN(queue);

        for (j = 0; j < BXE_NUM_ETH_Q_STATS; j++) {
            q_stat = ((i << 16) | j);
            SYSCTL_ADD_PROC(ctx, queue_children, OID_AUTO,
                            bxe_eth_q_stats_arr[j].string,
                            CTLTYPE_U64 | CTLFLAG_RD, sc, q_stat,
                            bxe_sysctl_eth_q_stat, "LU",
                            bxe_eth_q_stats_arr[j].string);
        }
    }
}

static int
bxe_alloc_buf_rings(struct bxe_softc *sc)
{
#if __FreeBSD_version >= 901504

    int i;
    struct bxe_fastpath *fp;

    for (i = 0; i < sc->num_queues; i++) {

        fp = &sc->fp[i];

        fp->tx_br = buf_ring_alloc(BXE_BR_SIZE, M_DEVBUF,
                                   M_NOWAIT, &fp->tx_mtx);
        if (fp->tx_br == NULL)
            return (-1);
    }
#endif
    return (0);
}

static void
bxe_free_buf_rings(struct bxe_softc *sc)
{
#if __FreeBSD_version >= 901504

    int i;
    struct bxe_fastpath *fp;

    for (i = 0; i < sc->num_queues; i++) {

        fp = &sc->fp[i];

        if (fp->tx_br) {
            buf_ring_free(fp->tx_br, M_DEVBUF);
            fp->tx_br = NULL;
        }
    }

#endif
}

static void
bxe_init_fp_mutexs(struct bxe_softc *sc)
{
    int i;
    struct bxe_fastpath *fp;

    for (i = 0; i < sc->num_queues; i++) {

        fp = &sc->fp[i];

        snprintf(fp->tx_mtx_name, sizeof(fp->tx_mtx_name),
            "bxe%d_fp%d_tx_lock", sc->unit, i);
        mtx_init(&fp->tx_mtx, fp->tx_mtx_name, NULL, MTX_DEF);

        snprintf(fp->rx_mtx_name, sizeof(fp->rx_mtx_name),
            "bxe%d_fp%d_rx_lock", sc->unit, i);
        mtx_init(&fp->rx_mtx, fp->rx_mtx_name, NULL, MTX_DEF);
    }
}

static void
bxe_destroy_fp_mutexs(struct bxe_softc *sc)
{
    int i;
    struct bxe_fastpath *fp;

    for (i = 0; i < sc->num_queues; i++) {

        fp = &sc->fp[i];

        if (mtx_initialized(&fp->tx_mtx)) {
            mtx_destroy(&fp->tx_mtx);
        }

        if (mtx_initialized(&fp->rx_mtx)) {
            mtx_destroy(&fp->rx_mtx);
        }
    }
}


/*
 * Device attach function.
 *
 * Allocates device resources, performs secondary chip identification, and
 * initializes driver instance variables. This function is called from driver
 * load after a successful probe.
 *
 * Returns:
 *   0 = Success, >0 = Failure
 */
static int
bxe_attach(device_t dev)
{
    struct bxe_softc *sc;

    sc = device_get_softc(dev);

    BLOGD(sc, DBG_LOAD, "Starting attach...\n");

    sc->state = BXE_STATE_CLOSED;

    sc->dev  = dev;
    sc->unit = device_get_unit(dev);

    BLOGD(sc, DBG_LOAD, "softc = %p\n", sc);

    sc->pcie_bus    = pci_get_bus(dev);
    sc->pcie_device = pci_get_slot(dev);
    sc->pcie_func   = pci_get_function(dev);

    /* enable bus master capability */
    pci_enable_busmaster(dev);

    /* get the BARs */
    if (bxe_allocate_bars(sc) != 0) {
        return (ENXIO);
    }

    /* initialize the mutexes */
    bxe_init_mutexes(sc);

    /* prepare the periodic callout */
    callout_init(&sc->periodic_callout, 0);

    /* prepare the chip taskqueue */
    sc->chip_tq_flags = CHIP_TQ_NONE;
    snprintf(sc->chip_tq_name, sizeof(sc->chip_tq_name),
             "bxe%d_chip_tq", sc->unit);
    TASK_INIT(&sc->chip_tq_task, 0, bxe_handle_chip_tq, sc);
    sc->chip_tq = taskqueue_create(sc->chip_tq_name, M_NOWAIT,
                                   taskqueue_thread_enqueue,
                                   &sc->chip_tq);
    taskqueue_start_threads(&sc->chip_tq, 1, PWAIT, /* lower priority */
                            "%s", sc->chip_tq_name);

    TIMEOUT_TASK_INIT(taskqueue_thread,
        &sc->sp_err_timeout_task, 0, bxe_sp_err_timeout_task,  sc);


    /* get device info and set params */
    if (bxe_get_device_info(sc) != 0) {
        BLOGE(sc, "getting device info\n");
        bxe_deallocate_bars(sc);
        pci_disable_busmaster(dev);
        return (ENXIO);
    }

    /* get final misc params */
    bxe_get_params(sc);

    /* set the default MTU (changed via ifconfig) */
    sc->mtu = ETHERMTU;

    bxe_set_modes_bitmap(sc);

    /* XXX
     * If in AFEX mode and the function is configured for FCoE
     * then bail... no L2 allowed.
     */

    /* get phy settings from shmem and 'and' against admin settings */
    bxe_get_phy_info(sc);

    /* initialize the FreeBSD ifnet interface */
    if (bxe_init_ifnet(sc) != 0) {
        bxe_release_mutexes(sc);
        bxe_deallocate_bars(sc);
        pci_disable_busmaster(dev);
        return (ENXIO);
    }

    if (bxe_add_cdev(sc) != 0) {
        if (sc->ifp != NULL) {
            ether_ifdetach(sc->ifp);
        }
        ifmedia_removeall(&sc->ifmedia);
        bxe_release_mutexes(sc);
        bxe_deallocate_bars(sc);
        pci_disable_busmaster(dev);
        return (ENXIO);
    }

    /* allocate device interrupts */
    if (bxe_interrupt_alloc(sc) != 0) {
        bxe_del_cdev(sc);
        if (sc->ifp != NULL) {
            ether_ifdetach(sc->ifp);
        }
        ifmedia_removeall(&sc->ifmedia);
        bxe_release_mutexes(sc);
        bxe_deallocate_bars(sc);
        pci_disable_busmaster(dev);
        return (ENXIO);
    }

    bxe_init_fp_mutexs(sc);

    if (bxe_alloc_buf_rings(sc) != 0) {
	bxe_free_buf_rings(sc);
        bxe_interrupt_free(sc);
        bxe_del_cdev(sc);
        if (sc->ifp != NULL) {
            ether_ifdetach(sc->ifp);
        }
        ifmedia_removeall(&sc->ifmedia);
        bxe_release_mutexes(sc);
        bxe_deallocate_bars(sc);
        pci_disable_busmaster(dev);
        return (ENXIO);
    }

    /* allocate ilt */
    if (bxe_alloc_ilt_mem(sc) != 0) {
	bxe_free_buf_rings(sc);
        bxe_interrupt_free(sc);
        bxe_del_cdev(sc);
        if (sc->ifp != NULL) {
            ether_ifdetach(sc->ifp);
        }
        ifmedia_removeall(&sc->ifmedia);
        bxe_release_mutexes(sc);
        bxe_deallocate_bars(sc);
        pci_disable_busmaster(dev);
        return (ENXIO);
    }

    /* allocate the host hardware/software hsi structures */
    if (bxe_alloc_hsi_mem(sc) != 0) {
        bxe_free_ilt_mem(sc);
	bxe_free_buf_rings(sc);
        bxe_interrupt_free(sc);
        bxe_del_cdev(sc);
        if (sc->ifp != NULL) {
            ether_ifdetach(sc->ifp);
        }
        ifmedia_removeall(&sc->ifmedia);
        bxe_release_mutexes(sc);
        bxe_deallocate_bars(sc);
        pci_disable_busmaster(dev);
        return (ENXIO);
    }

    /* need to reset chip if UNDI was active */
    if (IS_PF(sc) && !BXE_NOMCP(sc)) {
        /* init fw_seq */
        sc->fw_seq =
            (SHMEM_RD(sc, func_mb[SC_FW_MB_IDX(sc)].drv_mb_header) &
             DRV_MSG_SEQ_NUMBER_MASK);
        BLOGD(sc, DBG_LOAD, "prev unload fw_seq 0x%04x\n", sc->fw_seq);
        bxe_prev_unload(sc);
    }

#if 1
    /* XXX */
    bxe_dcbx_set_state(sc, FALSE, BXE_DCBX_ENABLED_OFF);
#else
    if (SHMEM2_HAS(sc, dcbx_lldp_params_offset) &&
        SHMEM2_HAS(sc, dcbx_lldp_dcbx_stat_offset) &&
        SHMEM2_RD(sc, dcbx_lldp_params_offset) &&
        SHMEM2_RD(sc, dcbx_lldp_dcbx_stat_offset)) {
        bxe_dcbx_set_state(sc, TRUE, BXE_DCBX_ENABLED_ON_NEG_ON);
        bxe_dcbx_init_params(sc);
    } else {
        bxe_dcbx_set_state(sc, FALSE, BXE_DCBX_ENABLED_OFF);
    }
#endif

    /* calculate qm_cid_count */
    sc->qm_cid_count = bxe_set_qm_cid_count(sc);
    BLOGD(sc, DBG_LOAD, "qm_cid_count=%d\n", sc->qm_cid_count);

    sc->max_cos = 1;
    bxe_init_multi_cos(sc);

    bxe_add_sysctls(sc);

    return (0);
}

/*
 * Device detach function.
 *
 * Stops the controller, resets the controller, and releases resources.
 *
 * Returns:
 *   0 = Success, >0 = Failure
 */
static int
bxe_detach(device_t dev)
{
    struct bxe_softc *sc;
    if_t ifp;

    sc = device_get_softc(dev);

    BLOGD(sc, DBG_LOAD, "Starting detach...\n");

    ifp = sc->ifp;
    if (ifp != NULL && if_vlantrunkinuse(ifp)) {
        BLOGE(sc, "Cannot detach while VLANs are in use.\n");
        return(EBUSY);
    }

    bxe_del_cdev(sc);

    /* stop the periodic callout */
    bxe_periodic_stop(sc);

    /* stop the chip taskqueue */
    atomic_store_rel_long(&sc->chip_tq_flags, CHIP_TQ_NONE);
    if (sc->chip_tq) {
        taskqueue_drain(sc->chip_tq, &sc->chip_tq_task);
        taskqueue_free(sc->chip_tq);
        sc->chip_tq = NULL;
        taskqueue_drain_timeout(taskqueue_thread,
            &sc->sp_err_timeout_task);
    }

    /* stop and reset the controller if it was open */
    if (sc->state != BXE_STATE_CLOSED) {
        BXE_CORE_LOCK(sc);
        bxe_nic_unload(sc, UNLOAD_CLOSE, TRUE);
        sc->state = BXE_STATE_DISABLED;
        BXE_CORE_UNLOCK(sc);
    }

    /* release the network interface */
    if (ifp != NULL) {
        ether_ifdetach(ifp);
    }
    ifmedia_removeall(&sc->ifmedia);

    /* XXX do the following based on driver state... */

    /* free the host hardware/software hsi structures */
    bxe_free_hsi_mem(sc);

    /* free ilt */
    bxe_free_ilt_mem(sc);

    bxe_free_buf_rings(sc);

    /* release the interrupts */
    bxe_interrupt_free(sc);

    /* Release the mutexes*/
    bxe_destroy_fp_mutexs(sc);
    bxe_release_mutexes(sc);


    /* Release the PCIe BAR mapped memory */
    bxe_deallocate_bars(sc);

    /* Release the FreeBSD interface. */
    if (sc->ifp != NULL) {
        if_free(sc->ifp);
    }

    pci_disable_busmaster(dev);

    return (0);
}

/*
 * Device shutdown function.
 *
 * Stops and resets the controller.
 *
 * Returns:
 *   Nothing
 */
static int
bxe_shutdown(device_t dev)
{
    struct bxe_softc *sc;

    sc = device_get_softc(dev);

    BLOGD(sc, DBG_LOAD, "Starting shutdown...\n");

    /* stop the periodic callout */
    bxe_periodic_stop(sc);

    if (sc->state != BXE_STATE_CLOSED) {
    	BXE_CORE_LOCK(sc);
    	bxe_nic_unload(sc, UNLOAD_NORMAL, FALSE);
    	BXE_CORE_UNLOCK(sc);
    }

    return (0);
}

void
bxe_igu_ack_sb(struct bxe_softc *sc,
               uint8_t          igu_sb_id,
               uint8_t          segment,
               uint16_t         index,
               uint8_t          op,
               uint8_t          update)
{
    uint32_t igu_addr = sc->igu_base_addr;
    igu_addr += (IGU_CMD_INT_ACK_BASE + igu_sb_id)*8;
    bxe_igu_ack_sb_gen(sc, igu_sb_id, segment, index, op, update, igu_addr);
}

static void
bxe_igu_clear_sb_gen(struct bxe_softc *sc,
                     uint8_t          func,
                     uint8_t          idu_sb_id,
                     uint8_t          is_pf)
{
    uint32_t data, ctl, cnt = 100;
    uint32_t igu_addr_data = IGU_REG_COMMAND_REG_32LSB_DATA;
    uint32_t igu_addr_ctl = IGU_REG_COMMAND_REG_CTRL;
    uint32_t igu_addr_ack = IGU_REG_CSTORM_TYPE_0_SB_CLEANUP + (idu_sb_id/32)*4;
    uint32_t sb_bit =  1 << (idu_sb_id%32);
    uint32_t func_encode = func | (is_pf ? 1 : 0) << IGU_FID_ENCODE_IS_PF_SHIFT;
    uint32_t addr_encode = IGU_CMD_E2_PROD_UPD_BASE + idu_sb_id;

    /* Not supported in BC mode */
    if (CHIP_INT_MODE_IS_BC(sc)) {
        return;
    }

    data = ((IGU_USE_REGISTER_cstorm_type_0_sb_cleanup <<
             IGU_REGULAR_CLEANUP_TYPE_SHIFT) |
            IGU_REGULAR_CLEANUP_SET |
            IGU_REGULAR_BCLEANUP);

    ctl = ((addr_encode << IGU_CTRL_REG_ADDRESS_SHIFT) |
           (func_encode << IGU_CTRL_REG_FID_SHIFT) |
           (IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT));

    BLOGD(sc, DBG_LOAD, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
            data, igu_addr_data);
    REG_WR(sc, igu_addr_data, data);

    bus_space_barrier(sc->bar[BAR0].tag, sc->bar[BAR0].handle, 0, 0,
                      BUS_SPACE_BARRIER_WRITE);
    mb();

    BLOGD(sc, DBG_LOAD, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
            ctl, igu_addr_ctl);
    REG_WR(sc, igu_addr_ctl, ctl);

    bus_space_barrier(sc->bar[BAR0].tag, sc->bar[BAR0].handle, 0, 0,
                      BUS_SPACE_BARRIER_WRITE);
    mb();

    /* wait for clean up to finish */
    while (!(REG_RD(sc, igu_addr_ack) & sb_bit) && --cnt) {
        DELAY(20000);
    }

    if (!(REG_RD(sc, igu_addr_ack) & sb_bit)) {
        BLOGD(sc, DBG_LOAD,
              "Unable to finish IGU cleanup: "
              "idu_sb_id %d offset %d bit %d (cnt %d)\n",
              idu_sb_id, idu_sb_id/32, idu_sb_id%32, cnt);
    }
}

static void
bxe_igu_clear_sb(struct bxe_softc *sc,
                 uint8_t          idu_sb_id)
{
    bxe_igu_clear_sb_gen(sc, SC_FUNC(sc), idu_sb_id, TRUE /*PF*/);
}







/*******************/
/* ECORE CALLBACKS */
/*******************/

static void
bxe_reset_common(struct bxe_softc *sc)
{
    uint32_t val = 0x1400;

    /* reset_common */
    REG_WR(sc, (GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR), 0xd3ffff7f);

    if (CHIP_IS_E3(sc)) {
        val |= MISC_REGISTERS_RESET_REG_2_MSTAT0;
        val |= MISC_REGISTERS_RESET_REG_2_MSTAT1;
    }

    REG_WR(sc, (GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR), val);
}

static void
bxe_common_init_phy(struct bxe_softc *sc)
{
    uint32_t shmem_base[2];
    uint32_t shmem2_base[2];

    /* Avoid common init in case MFW supports LFA */
    if (SHMEM2_RD(sc, size) >
        (uint32_t)offsetof(struct shmem2_region,
                           lfa_host_addr[SC_PORT(sc)])) {
        return;
    }

    shmem_base[0]  = sc->devinfo.shmem_base;
    shmem2_base[0] = sc->devinfo.shmem2_base;

    if (!CHIP_IS_E1x(sc)) {
        shmem_base[1]  = SHMEM2_RD(sc, other_shmem_base_addr);
        shmem2_base[1] = SHMEM2_RD(sc, other_shmem2_base_addr);
    }

    bxe_acquire_phy_lock(sc);
    elink_common_init_phy(sc, shmem_base, shmem2_base,
                          sc->devinfo.chip_id, 0);
    bxe_release_phy_lock(sc);
}

static void
bxe_pf_disable(struct bxe_softc *sc)
{
    uint32_t val = REG_RD(sc, IGU_REG_PF_CONFIGURATION);

    val &= ~IGU_PF_CONF_FUNC_EN;

    REG_WR(sc, IGU_REG_PF_CONFIGURATION, val);
    REG_WR(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);
    REG_WR(sc, CFC_REG_WEAK_ENABLE_PF, 0);
}

static void
bxe_init_pxp(struct bxe_softc *sc)
{
    uint16_t devctl;
    int r_order, w_order;

    devctl = bxe_pcie_capability_read(sc, PCIR_EXPRESS_DEVICE_CTL, 2);

    BLOGD(sc, DBG_LOAD, "read 0x%08x from devctl\n", devctl);

    w_order = ((devctl & PCIM_EXP_CTL_MAX_PAYLOAD) >> 5);

    if (sc->mrrs == -1) {
        r_order = ((devctl & PCIM_EXP_CTL_MAX_READ_REQUEST) >> 12);
    } else {
        BLOGD(sc, DBG_LOAD, "forcing read order to %d\n", sc->mrrs);
        r_order = sc->mrrs;
    }

    ecore_init_pxp_arb(sc, r_order, w_order);
}

static uint32_t
bxe_get_pretend_reg(struct bxe_softc *sc)
{
    uint32_t base = PXP2_REG_PGL_PRETEND_FUNC_F0;
    uint32_t stride = (PXP2_REG_PGL_PRETEND_FUNC_F1 - base);
    return (base + (SC_ABS_FUNC(sc)) * stride);
}

/*
 * Called only on E1H or E2.
 * When pretending to be PF, the pretend value is the function number 0..7.
 * When pretending to be VF, the pretend val is the PF-num:VF-valid:ABS-VFID
 * combination.
 */
static int
bxe_pretend_func(struct bxe_softc *sc,
                 uint16_t         pretend_func_val)
{
    uint32_t pretend_reg;

    if (CHIP_IS_E1H(sc) && (pretend_func_val > E1H_FUNC_MAX)) {
        return (-1);
    }

    /* get my own pretend register */
    pretend_reg = bxe_get_pretend_reg(sc);
    REG_WR(sc, pretend_reg, pretend_func_val);
    REG_RD(sc, pretend_reg);
    return (0);
}

static void
bxe_iov_init_dmae(struct bxe_softc *sc)
{
    return;
}

static void
bxe_iov_init_dq(struct bxe_softc *sc)
{
    return;
}

/* send a NIG loopback debug packet */
static void
bxe_lb_pckt(struct bxe_softc *sc)
{
    uint32_t wb_write[3];

    /* Ethernet source and destination addresses */
    wb_write[0] = 0x55555555;
    wb_write[1] = 0x55555555;
    wb_write[2] = 0x20;     /* SOP */
    REG_WR_DMAE(sc, NIG_REG_DEBUG_PACKET_LB, wb_write, 3);

    /* NON-IP protocol */
    wb_write[0] = 0x09000000;
    wb_write[1] = 0x55555555;
    wb_write[2] = 0x10;     /* EOP, eop_bvalid = 0 */
    REG_WR_DMAE(sc, NIG_REG_DEBUG_PACKET_LB, wb_write, 3);
}

/*
 * Some of the internal memories are not directly readable from the driver.
 * To test them we send debug packets.
 */
static int
bxe_int_mem_test(struct bxe_softc *sc)
{
    int factor;
    int count, i;
    uint32_t val = 0;

    if (CHIP_REV_IS_FPGA(sc)) {
        factor = 120;
    } else if (CHIP_REV_IS_EMUL(sc)) {
        factor = 200;
    } else {
        factor = 1;
    }

    /* disable inputs of parser neighbor blocks */
    REG_WR(sc, TSDM_REG_ENABLE_IN1, 0x0);
    REG_WR(sc, TCM_REG_PRS_IFEN, 0x0);
    REG_WR(sc, CFC_REG_DEBUG0, 0x1);
    REG_WR(sc, NIG_REG_PRS_REQ_IN_EN, 0x0);

    /*  write 0 to parser credits for CFC search request */
    REG_WR(sc, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x0);

    /* send Ethernet packet */
    bxe_lb_pckt(sc);

    /* TODO do i reset NIG statistic? */
    /* Wait until NIG register shows 1 packet of size 0x10 */
    count = 1000 * factor;
    while (count) {
        bxe_read_dmae(sc, NIG_REG_STAT2_BRB_OCTET, 2);
        val = *BXE_SP(sc, wb_data[0]);
        if (val == 0x10) {
            break;
        }

        DELAY(10000);
        count--;
    }

    if (val != 0x10) {
        BLOGE(sc, "NIG timeout val=0x%x\n", val);
        return (-1);
    }

    /* wait until PRS register shows 1 packet */
    count = (1000 * factor);
    while (count) {
        val = REG_RD(sc, PRS_REG_NUM_OF_PACKETS);
        if (val == 1) {
            break;
        }

        DELAY(10000);
        count--;
    }

    if (val != 0x1) {
        BLOGE(sc, "PRS timeout val=0x%x\n", val);
        return (-2);
    }

    /* Reset and init BRB, PRS */
    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR, 0x03);
    DELAY(50000);
    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, 0x03);
    DELAY(50000);
    ecore_init_block(sc, BLOCK_BRB1, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_PRS, PHASE_COMMON);

    /* Disable inputs of parser neighbor blocks */
    REG_WR(sc, TSDM_REG_ENABLE_IN1, 0x0);
    REG_WR(sc, TCM_REG_PRS_IFEN, 0x0);
    REG_WR(sc, CFC_REG_DEBUG0, 0x1);
    REG_WR(sc, NIG_REG_PRS_REQ_IN_EN, 0x0);

    /* Write 0 to parser credits for CFC search request */
    REG_WR(sc, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x0);

    /* send 10 Ethernet packets */
    for (i = 0; i < 10; i++) {
        bxe_lb_pckt(sc);
    }

    /* Wait until NIG register shows 10+1 packets of size 11*0x10 = 0xb0 */
    count = (1000 * factor);
    while (count) {
        bxe_read_dmae(sc, NIG_REG_STAT2_BRB_OCTET, 2);
        val = *BXE_SP(sc, wb_data[0]);
        if (val == 0xb0) {
            break;
        }

        DELAY(10000);
        count--;
    }

    if (val != 0xb0) {
        BLOGE(sc, "NIG timeout val=0x%x\n", val);
        return (-3);
    }

    /* Wait until PRS register shows 2 packets */
    val = REG_RD(sc, PRS_REG_NUM_OF_PACKETS);
    if (val != 2) {
        BLOGE(sc, "PRS timeout val=0x%x\n", val);
    }

    /* Write 1 to parser credits for CFC search request */
    REG_WR(sc, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x1);

    /* Wait until PRS register shows 3 packets */
    DELAY(10000 * factor);

    /* Wait until NIG register shows 1 packet of size 0x10 */
    val = REG_RD(sc, PRS_REG_NUM_OF_PACKETS);
    if (val != 3) {
        BLOGE(sc, "PRS timeout val=0x%x\n", val);
    }

    /* clear NIG EOP FIFO */
    for (i = 0; i < 11; i++) {
        REG_RD(sc, NIG_REG_INGRESS_EOP_LB_FIFO);
    }

    val = REG_RD(sc, NIG_REG_INGRESS_EOP_LB_EMPTY);
    if (val != 1) {
        BLOGE(sc, "clear of NIG failed val=0x%x\n", val);
        return (-4);
    }

    /* Reset and init BRB, PRS, NIG */
    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR, 0x03);
    DELAY(50000);
    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, 0x03);
    DELAY(50000);
    ecore_init_block(sc, BLOCK_BRB1, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_PRS, PHASE_COMMON);
    if (!CNIC_SUPPORT(sc)) {
        /* set NIC mode */
        REG_WR(sc, PRS_REG_NIC_MODE, 1);
    }

    /* Enable inputs of parser neighbor blocks */
    REG_WR(sc, TSDM_REG_ENABLE_IN1, 0x7fffffff);
    REG_WR(sc, TCM_REG_PRS_IFEN, 0x1);
    REG_WR(sc, CFC_REG_DEBUG0, 0x0);
    REG_WR(sc, NIG_REG_PRS_REQ_IN_EN, 0x1);

    return (0);
}

static void
bxe_setup_fan_failure_detection(struct bxe_softc *sc)
{
    int is_required;
    uint32_t val;
    int port;

    is_required = 0;
    val = (SHMEM_RD(sc, dev_info.shared_hw_config.config2) &
           SHARED_HW_CFG_FAN_FAILURE_MASK);

    if (val == SHARED_HW_CFG_FAN_FAILURE_ENABLED) {
        is_required = 1;
    }
    /*
     * The fan failure mechanism is usually related to the PHY type since
     * the power consumption of the board is affected by the PHY. Currently,
     * fan is required for most designs with SFX7101, BCM8727 and BCM8481.
     */
    else if (val == SHARED_HW_CFG_FAN_FAILURE_PHY_TYPE) {
        for (port = PORT_0; port < PORT_MAX; port++) {
            is_required |= elink_fan_failure_det_req(sc,
                                                     sc->devinfo.shmem_base,
                                                     sc->devinfo.shmem2_base,
                                                     port);
        }
    }

    BLOGD(sc, DBG_LOAD, "fan detection setting: %d\n", is_required);

    if (is_required == 0) {
        return;
    }

    /* Fan failure is indicated by SPIO 5 */
    bxe_set_spio(sc, MISC_SPIO_SPIO5, MISC_SPIO_INPUT_HI_Z);

    /* set to active low mode */
    val = REG_RD(sc, MISC_REG_SPIO_INT);
    val |= (MISC_SPIO_SPIO5 << MISC_SPIO_INT_OLD_SET_POS);
    REG_WR(sc, MISC_REG_SPIO_INT, val);

    /* enable interrupt to signal the IGU */
    val = REG_RD(sc, MISC_REG_SPIO_EVENT_EN);
    val |= MISC_SPIO_SPIO5;
    REG_WR(sc, MISC_REG_SPIO_EVENT_EN, val);
}

static void
bxe_enable_blocks_attention(struct bxe_softc *sc)
{
    uint32_t val;

    REG_WR(sc, PXP_REG_PXP_INT_MASK_0, 0);
    if (!CHIP_IS_E1x(sc)) {
        REG_WR(sc, PXP_REG_PXP_INT_MASK_1, 0x40);
    } else {
        REG_WR(sc, PXP_REG_PXP_INT_MASK_1, 0);
    }
    REG_WR(sc, DORQ_REG_DORQ_INT_MASK, 0);
    REG_WR(sc, CFC_REG_CFC_INT_MASK, 0);
    /*
     * mask read length error interrupts in brb for parser
     * (parsing unit and 'checksum and crc' unit)
     * these errors are legal (PU reads fixed length and CAC can cause
     * read length error on truncated packets)
     */
    REG_WR(sc, BRB1_REG_BRB1_INT_MASK, 0xFC00);
    REG_WR(sc, QM_REG_QM_INT_MASK, 0);
    REG_WR(sc, TM_REG_TM_INT_MASK, 0);
    REG_WR(sc, XSDM_REG_XSDM_INT_MASK_0, 0);
    REG_WR(sc, XSDM_REG_XSDM_INT_MASK_1, 0);
    REG_WR(sc, XCM_REG_XCM_INT_MASK, 0);
/*      REG_WR(sc, XSEM_REG_XSEM_INT_MASK_0, 0); */
/*      REG_WR(sc, XSEM_REG_XSEM_INT_MASK_1, 0); */
    REG_WR(sc, USDM_REG_USDM_INT_MASK_0, 0);
    REG_WR(sc, USDM_REG_USDM_INT_MASK_1, 0);
    REG_WR(sc, UCM_REG_UCM_INT_MASK, 0);
/*      REG_WR(sc, USEM_REG_USEM_INT_MASK_0, 0); */
/*      REG_WR(sc, USEM_REG_USEM_INT_MASK_1, 0); */
    REG_WR(sc, GRCBASE_UPB + PB_REG_PB_INT_MASK, 0);
    REG_WR(sc, CSDM_REG_CSDM_INT_MASK_0, 0);
    REG_WR(sc, CSDM_REG_CSDM_INT_MASK_1, 0);
    REG_WR(sc, CCM_REG_CCM_INT_MASK, 0);
/*      REG_WR(sc, CSEM_REG_CSEM_INT_MASK_0, 0); */
/*      REG_WR(sc, CSEM_REG_CSEM_INT_MASK_1, 0); */

    val = (PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_AFT |
           PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_OF |
           PXP2_PXP2_INT_MASK_0_REG_PGL_PCIE_ATTN);
    if (!CHIP_IS_E1x(sc)) {
        val |= (PXP2_PXP2_INT_MASK_0_REG_PGL_READ_BLOCKED |
                PXP2_PXP2_INT_MASK_0_REG_PGL_WRITE_BLOCKED);
    }
    REG_WR(sc, PXP2_REG_PXP2_INT_MASK_0, val);

    REG_WR(sc, TSDM_REG_TSDM_INT_MASK_0, 0);
    REG_WR(sc, TSDM_REG_TSDM_INT_MASK_1, 0);
    REG_WR(sc, TCM_REG_TCM_INT_MASK, 0);
/*      REG_WR(sc, TSEM_REG_TSEM_INT_MASK_0, 0); */

    if (!CHIP_IS_E1x(sc)) {
        /* enable VFC attentions: bits 11 and 12, bits 31:13 reserved */
        REG_WR(sc, TSEM_REG_TSEM_INT_MASK_1, 0x07ff);
    }

    REG_WR(sc, CDU_REG_CDU_INT_MASK, 0);
    REG_WR(sc, DMAE_REG_DMAE_INT_MASK, 0);
/*      REG_WR(sc, MISC_REG_MISC_INT_MASK, 0); */
    REG_WR(sc, PBF_REG_PBF_INT_MASK, 0x18);     /* bit 3,4 masked */
}

/**
 * bxe_init_hw_common - initialize the HW at the COMMON phase.
 *
 * @sc:     driver handle
 */
static int
bxe_init_hw_common(struct bxe_softc *sc)
{
    uint8_t abs_func_id;
    uint32_t val;

    BLOGD(sc, DBG_LOAD, "starting common init for func %d\n",
          SC_ABS_FUNC(sc));

    /*
     * take the RESET lock to protect undi_unload flow from accessing
     * registers while we are resetting the chip
     */
    bxe_acquire_hw_lock(sc, HW_LOCK_RESOURCE_RESET);

    bxe_reset_common(sc);

    REG_WR(sc, (GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET), 0xffffffff);

    val = 0xfffc;
    if (CHIP_IS_E3(sc)) {
        val |= MISC_REGISTERS_RESET_REG_2_MSTAT0;
        val |= MISC_REGISTERS_RESET_REG_2_MSTAT1;
    }

    REG_WR(sc, (GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET), val);

    bxe_release_hw_lock(sc, HW_LOCK_RESOURCE_RESET);

    ecore_init_block(sc, BLOCK_MISC, PHASE_COMMON);
    BLOGD(sc, DBG_LOAD, "after misc block init\n");

    if (!CHIP_IS_E1x(sc)) {
        /*
         * 4-port mode or 2-port mode we need to turn off master-enable for
         * everyone. After that we turn it back on for self. So, we disregard
         * multi-function, and always disable all functions on the given path,
         * this means 0,2,4,6 for path 0 and 1,3,5,7 for path 1
         */
        for (abs_func_id = SC_PATH(sc);
             abs_func_id < (E2_FUNC_MAX * 2);
             abs_func_id += 2) {
            if (abs_func_id == SC_ABS_FUNC(sc)) {
                REG_WR(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
                continue;
            }

            bxe_pretend_func(sc, abs_func_id);

            /* clear pf enable */
            bxe_pf_disable(sc);

            bxe_pretend_func(sc, SC_ABS_FUNC(sc));
        }
    }

    BLOGD(sc, DBG_LOAD, "after pf disable\n");

    ecore_init_block(sc, BLOCK_PXP, PHASE_COMMON);

    if (CHIP_IS_E1(sc)) {
        /*
         * enable HW interrupt from PXP on USDM overflow
         * bit 16 on INT_MASK_0
         */
        REG_WR(sc, PXP_REG_PXP_INT_MASK_0, 0);
    }

    ecore_init_block(sc, BLOCK_PXP2, PHASE_COMMON);
    bxe_init_pxp(sc);

#ifdef __BIG_ENDIAN
    REG_WR(sc, PXP2_REG_RQ_QM_ENDIAN_M, 1);
    REG_WR(sc, PXP2_REG_RQ_TM_ENDIAN_M, 1);
    REG_WR(sc, PXP2_REG_RQ_SRC_ENDIAN_M, 1);
    REG_WR(sc, PXP2_REG_RQ_CDU_ENDIAN_M, 1);
    REG_WR(sc, PXP2_REG_RQ_DBG_ENDIAN_M, 1);
    /* make sure this value is 0 */
    REG_WR(sc, PXP2_REG_RQ_HC_ENDIAN_M, 0);

    //REG_WR(sc, PXP2_REG_RD_PBF_SWAP_MODE, 1);
    REG_WR(sc, PXP2_REG_RD_QM_SWAP_MODE, 1);
    REG_WR(sc, PXP2_REG_RD_TM_SWAP_MODE, 1);
    REG_WR(sc, PXP2_REG_RD_SRC_SWAP_MODE, 1);
    REG_WR(sc, PXP2_REG_RD_CDURD_SWAP_MODE, 1);
#endif

    ecore_ilt_init_page_size(sc, INITOP_SET);

    if (CHIP_REV_IS_FPGA(sc) && CHIP_IS_E1H(sc)) {
        REG_WR(sc, PXP2_REG_PGL_TAGS_LIMIT, 0x1);
    }

    /* let the HW do it's magic... */
    DELAY(100000);

    /* finish PXP init */
    val = REG_RD(sc, PXP2_REG_RQ_CFG_DONE);
    if (val != 1) {
        BLOGE(sc, "PXP2 CFG failed PXP2_REG_RQ_CFG_DONE val = 0x%x\n",
            val);
        return (-1);
    }
    val = REG_RD(sc, PXP2_REG_RD_INIT_DONE);
    if (val != 1) {
        BLOGE(sc, "PXP2 RD_INIT failed val = 0x%x\n", val);
        return (-1);
    }

    BLOGD(sc, DBG_LOAD, "after pxp init\n");

    /*
     * Timer bug workaround for E2 only. We need to set the entire ILT to have
     * entries with value "0" and valid bit on. This needs to be done by the
     * first PF that is loaded in a path (i.e. common phase)
     */
    if (!CHIP_IS_E1x(sc)) {
/*
 * In E2 there is a bug in the timers block that can cause function 6 / 7
 * (i.e. vnic3) to start even if it is marked as "scan-off".
 * This occurs when a different function (func2,3) is being marked
 * as "scan-off". Real-life scenario for example: if a driver is being
 * load-unloaded while func6,7 are down. This will cause the timer to access
 * the ilt, translate to a logical address and send a request to read/write.
 * Since the ilt for the function that is down is not valid, this will cause
 * a translation error which is unrecoverable.
 * The Workaround is intended to make sure that when this happens nothing
 * fatal will occur. The workaround:
 *  1.  First PF driver which loads on a path will:
 *      a.  After taking the chip out of reset, by using pretend,
 *          it will write "0" to the following registers of
 *          the other vnics.
 *          REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);
 *          REG_WR(pdev, CFC_REG_WEAK_ENABLE_PF,0);
 *          REG_WR(pdev, CFC_REG_STRONG_ENABLE_PF,0);
 *          And for itself it will write '1' to
 *          PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER to enable
 *          dmae-operations (writing to pram for example.)
 *          note: can be done for only function 6,7 but cleaner this
 *            way.
 *      b.  Write zero+valid to the entire ILT.
 *      c.  Init the first_timers_ilt_entry, last_timers_ilt_entry of
 *          VNIC3 (of that port). The range allocated will be the
 *          entire ILT. This is needed to prevent  ILT range error.
 *  2.  Any PF driver load flow:
 *      a.  ILT update with the physical addresses of the allocated
 *          logical pages.
 *      b.  Wait 20msec. - note that this timeout is needed to make
 *          sure there are no requests in one of the PXP internal
 *          queues with "old" ILT addresses.
 *      c.  PF enable in the PGLC.
 *      d.  Clear the was_error of the PF in the PGLC. (could have
 *          occurred while driver was down)
 *      e.  PF enable in the CFC (WEAK + STRONG)
 *      f.  Timers scan enable
 *  3.  PF driver unload flow:
 *      a.  Clear the Timers scan_en.
 *      b.  Polling for scan_on=0 for that PF.
 *      c.  Clear the PF enable bit in the PXP.
 *      d.  Clear the PF enable in the CFC (WEAK + STRONG)
 *      e.  Write zero+valid to all ILT entries (The valid bit must
 *          stay set)
 *      f.  If this is VNIC 3 of a port then also init
 *          first_timers_ilt_entry to zero and last_timers_ilt_entry
 *          to the last enrty in the ILT.
 *
 *      Notes:
 *      Currently the PF error in the PGLC is non recoverable.
 *      In the future the there will be a recovery routine for this error.
 *      Currently attention is masked.
 *      Having an MCP lock on the load/unload process does not guarantee that
 *      there is no Timer disable during Func6/7 enable. This is because the
 *      Timers scan is currently being cleared by the MCP on FLR.
 *      Step 2.d can be done only for PF6/7 and the driver can also check if
 *      there is error before clearing it. But the flow above is simpler and
 *      more general.
 *      All ILT entries are written by zero+valid and not just PF6/7
 *      ILT entries since in the future the ILT entries allocation for
 *      PF-s might be dynamic.
 */
        struct ilt_client_info ilt_cli;
        struct ecore_ilt ilt;

        memset(&ilt_cli, 0, sizeof(struct ilt_client_info));
        memset(&ilt, 0, sizeof(struct ecore_ilt));

        /* initialize dummy TM client */
        ilt_cli.start      = 0;
        ilt_cli.end        = ILT_NUM_PAGE_ENTRIES - 1;
        ilt_cli.client_num = ILT_CLIENT_TM;

        /*
         * Step 1: set zeroes to all ilt page entries with valid bit on
         * Step 2: set the timers first/last ilt entry to point
         * to the entire range to prevent ILT range error for 3rd/4th
         * vnic (this code assumes existence of the vnic)
         *
         * both steps performed by call to ecore_ilt_client_init_op()
         * with dummy TM client
         *
         * we must use pretend since PXP2_REG_RQ_##blk##_FIRST_ILT
         * and his brother are split registers
         */

        bxe_pretend_func(sc, (SC_PATH(sc) + 6));
        ecore_ilt_client_init_op_ilt(sc, &ilt, &ilt_cli, INITOP_CLEAR);
        bxe_pretend_func(sc, SC_ABS_FUNC(sc));

        REG_WR(sc, PXP2_REG_RQ_DRAM_ALIGN, BXE_PXP_DRAM_ALIGN);
        REG_WR(sc, PXP2_REG_RQ_DRAM_ALIGN_RD, BXE_PXP_DRAM_ALIGN);
        REG_WR(sc, PXP2_REG_RQ_DRAM_ALIGN_SEL, 1);
    }

    REG_WR(sc, PXP2_REG_RQ_DISABLE_INPUTS, 0);
    REG_WR(sc, PXP2_REG_RD_DISABLE_INPUTS, 0);

    if (!CHIP_IS_E1x(sc)) {
        int factor = CHIP_REV_IS_EMUL(sc) ? 1000 :
                     (CHIP_REV_IS_FPGA(sc) ? 400 : 0);

        ecore_init_block(sc, BLOCK_PGLUE_B, PHASE_COMMON);
        ecore_init_block(sc, BLOCK_ATC, PHASE_COMMON);

        /* let the HW do it's magic... */
        do {
            DELAY(200000);
            val = REG_RD(sc, ATC_REG_ATC_INIT_DONE);
        } while (factor-- && (val != 1));

        if (val != 1) {
            BLOGE(sc, "ATC_INIT failed val = 0x%x\n", val);
            return (-1);
        }
    }

    BLOGD(sc, DBG_LOAD, "after pglue and atc init\n");

    ecore_init_block(sc, BLOCK_DMAE, PHASE_COMMON);

    bxe_iov_init_dmae(sc);

    /* clean the DMAE memory */
    sc->dmae_ready = 1;
    ecore_init_fill(sc, TSEM_REG_PRAM, 0, 8, 1);

    ecore_init_block(sc, BLOCK_TCM, PHASE_COMMON);

    ecore_init_block(sc, BLOCK_UCM, PHASE_COMMON);

    ecore_init_block(sc, BLOCK_CCM, PHASE_COMMON);

    ecore_init_block(sc, BLOCK_XCM, PHASE_COMMON);

    bxe_read_dmae(sc, XSEM_REG_PASSIVE_BUFFER, 3);
    bxe_read_dmae(sc, CSEM_REG_PASSIVE_BUFFER, 3);
    bxe_read_dmae(sc, TSEM_REG_PASSIVE_BUFFER, 3);
    bxe_read_dmae(sc, USEM_REG_PASSIVE_BUFFER, 3);

    ecore_init_block(sc, BLOCK_QM, PHASE_COMMON);

    /* QM queues pointers table */
    ecore_qm_init_ptr_table(sc, sc->qm_cid_count, INITOP_SET);

    /* soft reset pulse */
    REG_WR(sc, QM_REG_SOFT_RESET, 1);
    REG_WR(sc, QM_REG_SOFT_RESET, 0);

    if (CNIC_SUPPORT(sc))
        ecore_init_block(sc, BLOCK_TM, PHASE_COMMON);

    ecore_init_block(sc, BLOCK_DORQ, PHASE_COMMON);
    REG_WR(sc, DORQ_REG_DPM_CID_OFST, BXE_DB_SHIFT);
    if (!CHIP_REV_IS_SLOW(sc)) {
        /* enable hw interrupt from doorbell Q */
        REG_WR(sc, DORQ_REG_DORQ_INT_MASK, 0);
    }

    ecore_init_block(sc, BLOCK_BRB1, PHASE_COMMON);

    ecore_init_block(sc, BLOCK_PRS, PHASE_COMMON);
    REG_WR(sc, PRS_REG_A_PRSU_20, 0xf);

    if (!CHIP_IS_E1(sc)) {
        REG_WR(sc, PRS_REG_E1HOV_MODE, sc->devinfo.mf_info.path_has_ovlan);
    }

    if (!CHIP_IS_E1x(sc) && !CHIP_IS_E3B0(sc)) {
        if (IS_MF_AFEX(sc)) {
            /*
             * configure that AFEX and VLAN headers must be
             * received in AFEX mode
             */
            REG_WR(sc, PRS_REG_HDRS_AFTER_BASIC, 0xE);
            REG_WR(sc, PRS_REG_MUST_HAVE_HDRS, 0xA);
            REG_WR(sc, PRS_REG_HDRS_AFTER_TAG_0, 0x6);
            REG_WR(sc, PRS_REG_TAG_ETHERTYPE_0, 0x8926);
            REG_WR(sc, PRS_REG_TAG_LEN_0, 0x4);
        } else {
            /*
             * Bit-map indicating which L2 hdrs may appear
             * after the basic Ethernet header
             */
            REG_WR(sc, PRS_REG_HDRS_AFTER_BASIC,
                   sc->devinfo.mf_info.path_has_ovlan ? 7 : 6);
        }
    }

    ecore_init_block(sc, BLOCK_TSDM, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_CSDM, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_USDM, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_XSDM, PHASE_COMMON);

    if (!CHIP_IS_E1x(sc)) {
        /* reset VFC memories */
        REG_WR(sc, TSEM_REG_FAST_MEMORY + VFC_REG_MEMORIES_RST,
               VFC_MEMORIES_RST_REG_CAM_RST |
               VFC_MEMORIES_RST_REG_RAM_RST);
        REG_WR(sc, XSEM_REG_FAST_MEMORY + VFC_REG_MEMORIES_RST,
               VFC_MEMORIES_RST_REG_CAM_RST |
               VFC_MEMORIES_RST_REG_RAM_RST);

        DELAY(20000);
    }

    ecore_init_block(sc, BLOCK_TSEM, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_USEM, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_CSEM, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_XSEM, PHASE_COMMON);

    /* sync semi rtc */
    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
           0x80000000);
    REG_WR(sc, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET,
           0x80000000);

    ecore_init_block(sc, BLOCK_UPB, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_XPB, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_PBF, PHASE_COMMON);

    if (!CHIP_IS_E1x(sc)) {
        if (IS_MF_AFEX(sc)) {
            /*
             * configure that AFEX and VLAN headers must be
             * sent in AFEX mode
             */
            REG_WR(sc, PBF_REG_HDRS_AFTER_BASIC, 0xE);
            REG_WR(sc, PBF_REG_MUST_HAVE_HDRS, 0xA);
            REG_WR(sc, PBF_REG_HDRS_AFTER_TAG_0, 0x6);
            REG_WR(sc, PBF_REG_TAG_ETHERTYPE_0, 0x8926);
            REG_WR(sc, PBF_REG_TAG_LEN_0, 0x4);
        } else {
            REG_WR(sc, PBF_REG_HDRS_AFTER_BASIC,
                   sc->devinfo.mf_info.path_has_ovlan ? 7 : 6);
        }
    }

    REG_WR(sc, SRC_REG_SOFT_RST, 1);

    ecore_init_block(sc, BLOCK_SRC, PHASE_COMMON);

    if (CNIC_SUPPORT(sc)) {
        REG_WR(sc, SRC_REG_KEYSEARCH_0, 0x63285672);
        REG_WR(sc, SRC_REG_KEYSEARCH_1, 0x24b8f2cc);
        REG_WR(sc, SRC_REG_KEYSEARCH_2, 0x223aef9b);
        REG_WR(sc, SRC_REG_KEYSEARCH_3, 0x26001e3a);
        REG_WR(sc, SRC_REG_KEYSEARCH_4, 0x7ae91116);
        REG_WR(sc, SRC_REG_KEYSEARCH_5, 0x5ce5230b);
        REG_WR(sc, SRC_REG_KEYSEARCH_6, 0x298d8adf);
        REG_WR(sc, SRC_REG_KEYSEARCH_7, 0x6eb0ff09);
        REG_WR(sc, SRC_REG_KEYSEARCH_8, 0x1830f82f);
        REG_WR(sc, SRC_REG_KEYSEARCH_9, 0x01e46be7);
    }
    REG_WR(sc, SRC_REG_SOFT_RST, 0);

    if (sizeof(union cdu_context) != 1024) {
        /* we currently assume that a context is 1024 bytes */
        BLOGE(sc, "please adjust the size of cdu_context(%ld)\n",
              (long)sizeof(union cdu_context));
    }

    ecore_init_block(sc, BLOCK_CDU, PHASE_COMMON);
    val = (4 << 24) + (0 << 12) + 1024;
    REG_WR(sc, CDU_REG_CDU_GLOBAL_PARAMS, val);

    ecore_init_block(sc, BLOCK_CFC, PHASE_COMMON);

    REG_WR(sc, CFC_REG_INIT_REG, 0x7FF);
    /* enable context validation interrupt from CFC */
    REG_WR(sc, CFC_REG_CFC_INT_MASK, 0);

    /* set the thresholds to prevent CFC/CDU race */
    REG_WR(sc, CFC_REG_DEBUG0, 0x20020000);
    ecore_init_block(sc, BLOCK_HC, PHASE_COMMON);

    if (!CHIP_IS_E1x(sc) && BXE_NOMCP(sc)) {
        REG_WR(sc, IGU_REG_RESET_MEMORIES, 0x36);
    }

    ecore_init_block(sc, BLOCK_IGU, PHASE_COMMON);
    ecore_init_block(sc, BLOCK_MISC_AEU, PHASE_COMMON);

    /* Reset PCIE errors for debug */
    REG_WR(sc, 0x2814, 0xffffffff);
    REG_WR(sc, 0x3820, 0xffffffff);

    if (!CHIP_IS_E1x(sc)) {
        REG_WR(sc, PCICFG_OFFSET + PXPCS_TL_CONTROL_5,
               (PXPCS_TL_CONTROL_5_ERR_UNSPPORT1 |
                PXPCS_TL_CONTROL_5_ERR_UNSPPORT));
        REG_WR(sc, PCICFG_OFFSET + PXPCS_TL_FUNC345_STAT,
               (PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT4 |
                PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT3 |
                PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT2));
        REG_WR(sc, PCICFG_OFFSET + PXPCS_TL_FUNC678_STAT,
               (PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT7 |
                PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT6 |
                PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT5));
    }

    ecore_init_block(sc, BLOCK_NIG, PHASE_COMMON);

    if (!CHIP_IS_E1(sc)) {
        /* in E3 this done in per-port section */
        if (!CHIP_IS_E3(sc))
            REG_WR(sc, NIG_REG_LLH_MF_MODE, IS_MF(sc));
    }

    if (CHIP_IS_E1H(sc)) {
        /* not applicable for E2 (and above ...) */
        REG_WR(sc, NIG_REG_LLH_E1HOV_MODE, IS_MF_SD(sc));
    }

    if (CHIP_REV_IS_SLOW(sc)) {
        DELAY(200000);
    }

    /* finish CFC init */
    val = reg_poll(sc, CFC_REG_LL_INIT_DONE, 1, 100, 10);
    if (val != 1) {
        BLOGE(sc, "CFC LL_INIT failed val=0x%x\n", val);
        return (-1);
    }
    val = reg_poll(sc, CFC_REG_AC_INIT_DONE, 1, 100, 10);
    if (val != 1) {
        BLOGE(sc, "CFC AC_INIT failed val=0x%x\n", val);
        return (-1);
    }
    val = reg_poll(sc, CFC_REG_CAM_INIT_DONE, 1, 100, 10);
    if (val != 1) {
        BLOGE(sc, "CFC CAM_INIT failed val=0x%x\n", val);
        return (-1);
    }
    REG_WR(sc, CFC_REG_DEBUG0, 0);

    if (CHIP_IS_E1(sc)) {
        /* read NIG statistic to see if this is our first up since powerup */
        bxe_read_dmae(sc, NIG_REG_STAT2_BRB_OCTET, 2);
        val = *BXE_SP(sc, wb_data[0]);

        /* do internal memory self test */
        if ((val == 0) && bxe_int_mem_test(sc)) {
            BLOGE(sc, "internal mem self test failed val=0x%x\n", val);
            return (-1);
        }
    }

    bxe_setup_fan_failure_detection(sc);

    /* clear PXP2 attentions */
    REG_RD(sc, PXP2_REG_PXP2_INT_STS_CLR_0);

    bxe_enable_blocks_attention(sc);

    if (!CHIP_REV_IS_SLOW(sc)) {
        ecore_enable_blocks_parity(sc);
    }

    if (!BXE_NOMCP(sc)) {
        if (CHIP_IS_E1x(sc)) {
            bxe_common_init_phy(sc);
        }
    }

    return (0);
}

/**
 * bxe_init_hw_common_chip - init HW at the COMMON_CHIP phase.
 *
 * @sc:     driver handle
 */
static int
bxe_init_hw_common_chip(struct bxe_softc *sc)
{
    int rc = bxe_init_hw_common(sc);

    if (rc) {
        BLOGE(sc, "bxe_init_hw_common failed rc=%d\n", rc);
        return (rc);
    }

    /* In E2 2-PORT mode, same ext phy is used for the two paths */
    if (!BXE_NOMCP(sc)) {
        bxe_common_init_phy(sc);
    }

    return (0);
}

static int
bxe_init_hw_port(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    int init_phase = port ? PHASE_PORT1 : PHASE_PORT0;
    uint32_t low, high;
    uint32_t val;

    BLOGD(sc, DBG_LOAD, "starting port init for port %d\n", port);

    REG_WR(sc, NIG_REG_MASK_INTERRUPT_PORT0 + port*4, 0);

    ecore_init_block(sc, BLOCK_MISC, init_phase);
    ecore_init_block(sc, BLOCK_PXP, init_phase);
    ecore_init_block(sc, BLOCK_PXP2, init_phase);

    /*
     * Timers bug workaround: disables the pf_master bit in pglue at
     * common phase, we need to enable it here before any dmae access are
     * attempted. Therefore we manually added the enable-master to the
     * port phase (it also happens in the function phase)
     */
    if (!CHIP_IS_E1x(sc)) {
        REG_WR(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
    }

    ecore_init_block(sc, BLOCK_ATC, init_phase);
    ecore_init_block(sc, BLOCK_DMAE, init_phase);
    ecore_init_block(sc, BLOCK_PGLUE_B, init_phase);
    ecore_init_block(sc, BLOCK_QM, init_phase);

    ecore_init_block(sc, BLOCK_TCM, init_phase);
    ecore_init_block(sc, BLOCK_UCM, init_phase);
    ecore_init_block(sc, BLOCK_CCM, init_phase);
    ecore_init_block(sc, BLOCK_XCM, init_phase);

    /* QM cid (connection) count */
    ecore_qm_init_cid_count(sc, sc->qm_cid_count, INITOP_SET);

    if (CNIC_SUPPORT(sc)) {
        ecore_init_block(sc, BLOCK_TM, init_phase);
        REG_WR(sc, TM_REG_LIN0_SCAN_TIME + port*4, 20);
        REG_WR(sc, TM_REG_LIN0_MAX_ACTIVE_CID + port*4, 31);
    }

    ecore_init_block(sc, BLOCK_DORQ, init_phase);

    ecore_init_block(sc, BLOCK_BRB1, init_phase);

    if (CHIP_IS_E1(sc) || CHIP_IS_E1H(sc)) {
        if (IS_MF(sc)) {
            low = (BXE_ONE_PORT(sc) ? 160 : 246);
        } else if (sc->mtu > 4096) {
            if (BXE_ONE_PORT(sc)) {
                low = 160;
            } else {
                val = sc->mtu;
                /* (24*1024 + val*4)/256 */
                low = (96 + (val / 64) + ((val % 64) ? 1 : 0));
            }
        } else {
            low = (BXE_ONE_PORT(sc) ? 80 : 160);
        }
        high = (low + 56); /* 14*1024/256 */
        REG_WR(sc, BRB1_REG_PAUSE_LOW_THRESHOLD_0 + port*4, low);
        REG_WR(sc, BRB1_REG_PAUSE_HIGH_THRESHOLD_0 + port*4, high);
    }

    if (CHIP_IS_MODE_4_PORT(sc)) {
        REG_WR(sc, SC_PORT(sc) ?
               BRB1_REG_MAC_GUARANTIED_1 :
               BRB1_REG_MAC_GUARANTIED_0, 40);
    }

    ecore_init_block(sc, BLOCK_PRS, init_phase);
    if (CHIP_IS_E3B0(sc)) {
        if (IS_MF_AFEX(sc)) {
            /* configure headers for AFEX mode */
            REG_WR(sc, SC_PORT(sc) ?
                   PRS_REG_HDRS_AFTER_BASIC_PORT_1 :
                   PRS_REG_HDRS_AFTER_BASIC_PORT_0, 0xE);
            REG_WR(sc, SC_PORT(sc) ?
                   PRS_REG_HDRS_AFTER_TAG_0_PORT_1 :
                   PRS_REG_HDRS_AFTER_TAG_0_PORT_0, 0x6);
            REG_WR(sc, SC_PORT(sc) ?
                   PRS_REG_MUST_HAVE_HDRS_PORT_1 :
                   PRS_REG_MUST_HAVE_HDRS_PORT_0, 0xA);
        } else {
            /* Ovlan exists only if we are in multi-function +
             * switch-dependent mode, in switch-independent there
             * is no ovlan headers
             */
            REG_WR(sc, SC_PORT(sc) ?
                   PRS_REG_HDRS_AFTER_BASIC_PORT_1 :
                   PRS_REG_HDRS_AFTER_BASIC_PORT_0,
                   (sc->devinfo.mf_info.path_has_ovlan ? 7 : 6));
        }
    }

    ecore_init_block(sc, BLOCK_TSDM, init_phase);
    ecore_init_block(sc, BLOCK_CSDM, init_phase);
    ecore_init_block(sc, BLOCK_USDM, init_phase);
    ecore_init_block(sc, BLOCK_XSDM, init_phase);

    ecore_init_block(sc, BLOCK_TSEM, init_phase);
    ecore_init_block(sc, BLOCK_USEM, init_phase);
    ecore_init_block(sc, BLOCK_CSEM, init_phase);
    ecore_init_block(sc, BLOCK_XSEM, init_phase);

    ecore_init_block(sc, BLOCK_UPB, init_phase);
    ecore_init_block(sc, BLOCK_XPB, init_phase);

    ecore_init_block(sc, BLOCK_PBF, init_phase);

    if (CHIP_IS_E1x(sc)) {
        /* configure PBF to work without PAUSE mtu 9000 */
        REG_WR(sc, PBF_REG_P0_PAUSE_ENABLE + port*4, 0);

        /* update threshold */
        REG_WR(sc, PBF_REG_P0_ARB_THRSH + port*4, (9040/16));
        /* update init credit */
        REG_WR(sc, PBF_REG_P0_INIT_CRD + port*4, (9040/16) + 553 - 22);

        /* probe changes */
        REG_WR(sc, PBF_REG_INIT_P0 + port*4, 1);
        DELAY(50);
        REG_WR(sc, PBF_REG_INIT_P0 + port*4, 0);
    }

    if (CNIC_SUPPORT(sc)) {
        ecore_init_block(sc, BLOCK_SRC, init_phase);
    }

    ecore_init_block(sc, BLOCK_CDU, init_phase);
    ecore_init_block(sc, BLOCK_CFC, init_phase);

    if (CHIP_IS_E1(sc)) {
        REG_WR(sc, HC_REG_LEADING_EDGE_0 + port*8, 0);
        REG_WR(sc, HC_REG_TRAILING_EDGE_0 + port*8, 0);
    }
    ecore_init_block(sc, BLOCK_HC, init_phase);

    ecore_init_block(sc, BLOCK_IGU, init_phase);

    ecore_init_block(sc, BLOCK_MISC_AEU, init_phase);
    /* init aeu_mask_attn_func_0/1:
     *  - SF mode: bits 3-7 are masked. only bits 0-2 are in use
     *  - MF mode: bit 3 is masked. bits 0-2 are in use as in SF
     *             bits 4-7 are used for "per vn group attention" */
    val = IS_MF(sc) ? 0xF7 : 0x7;
    /* Enable DCBX attention for all but E1 */
    val |= CHIP_IS_E1(sc) ? 0 : 0x10;
    REG_WR(sc, MISC_REG_AEU_MASK_ATTN_FUNC_0 + port*4, val);

    ecore_init_block(sc, BLOCK_NIG, init_phase);

    if (!CHIP_IS_E1x(sc)) {
        /* Bit-map indicating which L2 hdrs may appear after the
         * basic Ethernet header
         */
        if (IS_MF_AFEX(sc)) {
            REG_WR(sc, SC_PORT(sc) ?
                   NIG_REG_P1_HDRS_AFTER_BASIC :
                   NIG_REG_P0_HDRS_AFTER_BASIC, 0xE);
        } else {
            REG_WR(sc, SC_PORT(sc) ?
                   NIG_REG_P1_HDRS_AFTER_BASIC :
                   NIG_REG_P0_HDRS_AFTER_BASIC,
                   IS_MF_SD(sc) ? 7 : 6);
        }

        if (CHIP_IS_E3(sc)) {
            REG_WR(sc, SC_PORT(sc) ?
                   NIG_REG_LLH1_MF_MODE :
                   NIG_REG_LLH_MF_MODE, IS_MF(sc));
        }
    }
    if (!CHIP_IS_E3(sc)) {
        REG_WR(sc, NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 1);
    }

    if (!CHIP_IS_E1(sc)) {
        /* 0x2 disable mf_ov, 0x1 enable */
        REG_WR(sc, NIG_REG_LLH0_BRB1_DRV_MASK_MF + port*4,
               (IS_MF_SD(sc) ? 0x1 : 0x2));

        if (!CHIP_IS_E1x(sc)) {
            val = 0;
            switch (sc->devinfo.mf_info.mf_mode) {
            case MULTI_FUNCTION_SD:
                val = 1;
                break;
            case MULTI_FUNCTION_SI:
            case MULTI_FUNCTION_AFEX:
                val = 2;
                break;
            }

            REG_WR(sc, (SC_PORT(sc) ? NIG_REG_LLH1_CLS_TYPE :
                        NIG_REG_LLH0_CLS_TYPE), val);
        }
        REG_WR(sc, NIG_REG_LLFC_ENABLE_0 + port*4, 0);
        REG_WR(sc, NIG_REG_LLFC_OUT_EN_0 + port*4, 0);
        REG_WR(sc, NIG_REG_PAUSE_ENABLE_0 + port*4, 1);
    }

    /* If SPIO5 is set to generate interrupts, enable it for this port */
    val = REG_RD(sc, MISC_REG_SPIO_EVENT_EN);
    if (val & MISC_SPIO_SPIO5) {
        uint32_t reg_addr = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
                                    MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);
        val = REG_RD(sc, reg_addr);
        val |= AEU_INPUTS_ATTN_BITS_SPIO5;
        REG_WR(sc, reg_addr, val);
    }

    return (0);
}

static uint32_t
bxe_flr_clnup_reg_poll(struct bxe_softc *sc,
                       uint32_t         reg,
                       uint32_t         expected,
                       uint32_t         poll_count)
{
    uint32_t cur_cnt = poll_count;
    uint32_t val;

    while ((val = REG_RD(sc, reg)) != expected && cur_cnt--) {
        DELAY(FLR_WAIT_INTERVAL);
    }

    return (val);
}

static int
bxe_flr_clnup_poll_hw_counter(struct bxe_softc *sc,
                              uint32_t         reg,
                              char             *msg,
                              uint32_t         poll_cnt)
{
    uint32_t val = bxe_flr_clnup_reg_poll(sc, reg, 0, poll_cnt);

    if (val != 0) {
        BLOGE(sc, "%s usage count=%d\n", msg, val);
        return (1);
    }

    return (0);
}

/* Common routines with VF FLR cleanup */
static uint32_t
bxe_flr_clnup_poll_count(struct bxe_softc *sc)
{
    /* adjust polling timeout */
    if (CHIP_REV_IS_EMUL(sc)) {
        return (FLR_POLL_CNT * 2000);
    }

    if (CHIP_REV_IS_FPGA(sc)) {
        return (FLR_POLL_CNT * 120);
    }

    return (FLR_POLL_CNT);
}

static int
bxe_poll_hw_usage_counters(struct bxe_softc *sc,
                           uint32_t         poll_cnt)
{
    /* wait for CFC PF usage-counter to zero (includes all the VFs) */
    if (bxe_flr_clnup_poll_hw_counter(sc,
                                      CFC_REG_NUM_LCIDS_INSIDE_PF,
                                      "CFC PF usage counter timed out",
                                      poll_cnt)) {
        return (1);
    }

    /* Wait for DQ PF usage-counter to zero (until DQ cleanup) */
    if (bxe_flr_clnup_poll_hw_counter(sc,
                                      DORQ_REG_PF_USAGE_CNT,
                                      "DQ PF usage counter timed out",
                                      poll_cnt)) {
        return (1);
    }

    /* Wait for QM PF usage-counter to zero (until DQ cleanup) */
    if (bxe_flr_clnup_poll_hw_counter(sc,
                                      QM_REG_PF_USG_CNT_0 + 4*SC_FUNC(sc),
                                      "QM PF usage counter timed out",
                                      poll_cnt)) {
        return (1);
    }

    /* Wait for Timer PF usage-counters to zero (until DQ cleanup) */
    if (bxe_flr_clnup_poll_hw_counter(sc,
                                      TM_REG_LIN0_VNIC_UC + 4*SC_PORT(sc),
                                      "Timers VNIC usage counter timed out",
                                      poll_cnt)) {
        return (1);
    }

    if (bxe_flr_clnup_poll_hw_counter(sc,
                                      TM_REG_LIN0_NUM_SCANS + 4*SC_PORT(sc),
                                      "Timers NUM_SCANS usage counter timed out",
                                      poll_cnt)) {
        return (1);
    }

    /* Wait DMAE PF usage counter to zero */
    if (bxe_flr_clnup_poll_hw_counter(sc,
                                      dmae_reg_go_c[INIT_DMAE_C(sc)],
                                      "DMAE dommand register timed out",
                                      poll_cnt)) {
        return (1);
    }

    return (0);
}

#define OP_GEN_PARAM(param)                                            \
    (((param) << SDM_OP_GEN_COMP_PARAM_SHIFT) & SDM_OP_GEN_COMP_PARAM)
#define OP_GEN_TYPE(type)                                           \
    (((type) << SDM_OP_GEN_COMP_TYPE_SHIFT) & SDM_OP_GEN_COMP_TYPE)
#define OP_GEN_AGG_VECT(index)                                             \
    (((index) << SDM_OP_GEN_AGG_VECT_IDX_SHIFT) & SDM_OP_GEN_AGG_VECT_IDX)

static int
bxe_send_final_clnup(struct bxe_softc *sc,
                     uint8_t          clnup_func,
                     uint32_t         poll_cnt)
{
    uint32_t op_gen_command = 0;
    uint32_t comp_addr = (BAR_CSTRORM_INTMEM +
                          CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(clnup_func));
    int ret = 0;

    if (REG_RD(sc, comp_addr)) {
        BLOGE(sc, "Cleanup complete was not 0 before sending\n");
        return (1);
    }

    op_gen_command |= OP_GEN_PARAM(XSTORM_AGG_INT_FINAL_CLEANUP_INDEX);
    op_gen_command |= OP_GEN_TYPE(XSTORM_AGG_INT_FINAL_CLEANUP_COMP_TYPE);
    op_gen_command |= OP_GEN_AGG_VECT(clnup_func);
    op_gen_command |= 1 << SDM_OP_GEN_AGG_VECT_IDX_VALID_SHIFT;

    BLOGD(sc, DBG_LOAD, "sending FW Final cleanup\n");
    REG_WR(sc, XSDM_REG_OPERATION_GEN, op_gen_command);

    if (bxe_flr_clnup_reg_poll(sc, comp_addr, 1, poll_cnt) != 1) {
        BLOGE(sc, "FW final cleanup did not succeed\n");
        BLOGD(sc, DBG_LOAD, "At timeout completion address contained %x\n",
              (REG_RD(sc, comp_addr)));
        bxe_panic(sc, ("FLR cleanup failed\n"));
        return (1);
    }

    /* Zero completion for nxt FLR */
    REG_WR(sc, comp_addr, 0);

    return (ret);
}

static void
bxe_pbf_pN_buf_flushed(struct bxe_softc       *sc,
                       struct pbf_pN_buf_regs *regs,
                       uint32_t               poll_count)
{
    uint32_t init_crd, crd, crd_start, crd_freed, crd_freed_start;
    uint32_t cur_cnt = poll_count;

    crd_freed = crd_freed_start = REG_RD(sc, regs->crd_freed);
    crd = crd_start = REG_RD(sc, regs->crd);
    init_crd = REG_RD(sc, regs->init_crd);

    BLOGD(sc, DBG_LOAD, "INIT CREDIT[%d] : %x\n", regs->pN, init_crd);
    BLOGD(sc, DBG_LOAD, "CREDIT[%d]      : s:%x\n", regs->pN, crd);
    BLOGD(sc, DBG_LOAD, "CREDIT_FREED[%d]: s:%x\n", regs->pN, crd_freed);

    while ((crd != init_crd) &&
           ((uint32_t)((int32_t)crd_freed - (int32_t)crd_freed_start) <
            (init_crd - crd_start))) {
        if (cur_cnt--) {
            DELAY(FLR_WAIT_INTERVAL);
            crd = REG_RD(sc, regs->crd);
            crd_freed = REG_RD(sc, regs->crd_freed);
        } else {
            BLOGD(sc, DBG_LOAD, "PBF tx buffer[%d] timed out\n", regs->pN);
            BLOGD(sc, DBG_LOAD, "CREDIT[%d]      : c:%x\n", regs->pN, crd);
            BLOGD(sc, DBG_LOAD, "CREDIT_FREED[%d]: c:%x\n", regs->pN, crd_freed);
            break;
        }
    }

    BLOGD(sc, DBG_LOAD, "Waited %d*%d usec for PBF tx buffer[%d]\n",
          poll_count-cur_cnt, FLR_WAIT_INTERVAL, regs->pN);
}

static void
bxe_pbf_pN_cmd_flushed(struct bxe_softc       *sc,
                       struct pbf_pN_cmd_regs *regs,
                       uint32_t               poll_count)
{
    uint32_t occup, to_free, freed, freed_start;
    uint32_t cur_cnt = poll_count;

    occup = to_free = REG_RD(sc, regs->lines_occup);
    freed = freed_start = REG_RD(sc, regs->lines_freed);

    BLOGD(sc, DBG_LOAD, "OCCUPANCY[%d]   : s:%x\n", regs->pN, occup);
    BLOGD(sc, DBG_LOAD, "LINES_FREED[%d] : s:%x\n", regs->pN, freed);

    while (occup &&
           ((uint32_t)((int32_t)freed - (int32_t)freed_start) < to_free)) {
        if (cur_cnt--) {
            DELAY(FLR_WAIT_INTERVAL);
            occup = REG_RD(sc, regs->lines_occup);
            freed = REG_RD(sc, regs->lines_freed);
        } else {
            BLOGD(sc, DBG_LOAD, "PBF cmd queue[%d] timed out\n", regs->pN);
            BLOGD(sc, DBG_LOAD, "OCCUPANCY[%d]   : s:%x\n", regs->pN, occup);
            BLOGD(sc, DBG_LOAD, "LINES_FREED[%d] : s:%x\n", regs->pN, freed);
            break;
        }
    }

    BLOGD(sc, DBG_LOAD, "Waited %d*%d usec for PBF cmd queue[%d]\n",
          poll_count - cur_cnt, FLR_WAIT_INTERVAL, regs->pN);
}

static void
bxe_tx_hw_flushed(struct bxe_softc *sc, uint32_t poll_count)
{
    struct pbf_pN_cmd_regs cmd_regs[] = {
        {0, (CHIP_IS_E3B0(sc)) ?
            PBF_REG_TQ_OCCUPANCY_Q0 :
            PBF_REG_P0_TQ_OCCUPANCY,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_TQ_LINES_FREED_CNT_Q0 :
            PBF_REG_P0_TQ_LINES_FREED_CNT},
        {1, (CHIP_IS_E3B0(sc)) ?
            PBF_REG_TQ_OCCUPANCY_Q1 :
            PBF_REG_P1_TQ_OCCUPANCY,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_TQ_LINES_FREED_CNT_Q1 :
            PBF_REG_P1_TQ_LINES_FREED_CNT},
        {4, (CHIP_IS_E3B0(sc)) ?
            PBF_REG_TQ_OCCUPANCY_LB_Q :
            PBF_REG_P4_TQ_OCCUPANCY,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_TQ_LINES_FREED_CNT_LB_Q :
            PBF_REG_P4_TQ_LINES_FREED_CNT}
    };

    struct pbf_pN_buf_regs buf_regs[] = {
        {0, (CHIP_IS_E3B0(sc)) ?
            PBF_REG_INIT_CRD_Q0 :
            PBF_REG_P0_INIT_CRD ,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_CREDIT_Q0 :
            PBF_REG_P0_CREDIT,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_INTERNAL_CRD_FREED_CNT_Q0 :
            PBF_REG_P0_INTERNAL_CRD_FREED_CNT},
        {1, (CHIP_IS_E3B0(sc)) ?
            PBF_REG_INIT_CRD_Q1 :
            PBF_REG_P1_INIT_CRD,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_CREDIT_Q1 :
            PBF_REG_P1_CREDIT,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_INTERNAL_CRD_FREED_CNT_Q1 :
            PBF_REG_P1_INTERNAL_CRD_FREED_CNT},
        {4, (CHIP_IS_E3B0(sc)) ?
            PBF_REG_INIT_CRD_LB_Q :
            PBF_REG_P4_INIT_CRD,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_CREDIT_LB_Q :
            PBF_REG_P4_CREDIT,
            (CHIP_IS_E3B0(sc)) ?
            PBF_REG_INTERNAL_CRD_FREED_CNT_LB_Q :
            PBF_REG_P4_INTERNAL_CRD_FREED_CNT},
    };

    int i;

    /* Verify the command queues are flushed P0, P1, P4 */
    for (i = 0; i < ARRAY_SIZE(cmd_regs); i++) {
        bxe_pbf_pN_cmd_flushed(sc, &cmd_regs[i], poll_count);
    }

    /* Verify the transmission buffers are flushed P0, P1, P4 */
    for (i = 0; i < ARRAY_SIZE(buf_regs); i++) {
        bxe_pbf_pN_buf_flushed(sc, &buf_regs[i], poll_count);
    }
}

static void
bxe_hw_enable_status(struct bxe_softc *sc)
{
    uint32_t val;

    val = REG_RD(sc, CFC_REG_WEAK_ENABLE_PF);
    BLOGD(sc, DBG_LOAD, "CFC_REG_WEAK_ENABLE_PF is 0x%x\n", val);

    val = REG_RD(sc, PBF_REG_DISABLE_PF);
    BLOGD(sc, DBG_LOAD, "PBF_REG_DISABLE_PF is 0x%x\n", val);

    val = REG_RD(sc, IGU_REG_PCI_PF_MSI_EN);
    BLOGD(sc, DBG_LOAD, "IGU_REG_PCI_PF_MSI_EN is 0x%x\n", val);

    val = REG_RD(sc, IGU_REG_PCI_PF_MSIX_EN);
    BLOGD(sc, DBG_LOAD, "IGU_REG_PCI_PF_MSIX_EN is 0x%x\n", val);

    val = REG_RD(sc, IGU_REG_PCI_PF_MSIX_FUNC_MASK);
    BLOGD(sc, DBG_LOAD, "IGU_REG_PCI_PF_MSIX_FUNC_MASK is 0x%x\n", val);

    val = REG_RD(sc, PGLUE_B_REG_SHADOW_BME_PF_7_0_CLR);
    BLOGD(sc, DBG_LOAD, "PGLUE_B_REG_SHADOW_BME_PF_7_0_CLR is 0x%x\n", val);

    val = REG_RD(sc, PGLUE_B_REG_FLR_REQUEST_PF_7_0_CLR);
    BLOGD(sc, DBG_LOAD, "PGLUE_B_REG_FLR_REQUEST_PF_7_0_CLR is 0x%x\n", val);

    val = REG_RD(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER);
    BLOGD(sc, DBG_LOAD, "PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER is 0x%x\n", val);
}

static int
bxe_pf_flr_clnup(struct bxe_softc *sc)
{
    uint32_t poll_cnt = bxe_flr_clnup_poll_count(sc);

    BLOGD(sc, DBG_LOAD, "Cleanup after FLR PF[%d]\n", SC_ABS_FUNC(sc));

    /* Re-enable PF target read access */
    REG_WR(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);

    /* Poll HW usage counters */
    BLOGD(sc, DBG_LOAD, "Polling usage counters\n");
    if (bxe_poll_hw_usage_counters(sc, poll_cnt)) {
        return (-1);
    }

    /* Zero the igu 'trailing edge' and 'leading edge' */

    /* Send the FW cleanup command */
    if (bxe_send_final_clnup(sc, (uint8_t)SC_FUNC(sc), poll_cnt)) {
        return (-1);
    }

    /* ATC cleanup */

    /* Verify TX hw is flushed */
    bxe_tx_hw_flushed(sc, poll_cnt);

    /* Wait 100ms (not adjusted according to platform) */
    DELAY(100000);

    /* Verify no pending pci transactions */
    if (bxe_is_pcie_pending(sc)) {
        BLOGE(sc, "PCIE Transactions still pending\n");
    }

    /* Debug */
    bxe_hw_enable_status(sc);

    /*
     * Master enable - Due to WB DMAE writes performed before this
     * register is re-initialized as part of the regular function init
     */
    REG_WR(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);

    return (0);
}

static int
bxe_init_hw_func(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    int func = SC_FUNC(sc);
    int init_phase = PHASE_PF0 + func;
    struct ecore_ilt *ilt = sc->ilt;
    uint16_t cdu_ilt_start;
    uint32_t addr, val;
    uint32_t main_mem_base, main_mem_size, main_mem_prty_clr;
    int i, main_mem_width, rc;

    BLOGD(sc, DBG_LOAD, "starting func init for func %d\n", func);

    /* FLR cleanup */
    if (!CHIP_IS_E1x(sc)) {
        rc = bxe_pf_flr_clnup(sc);
        if (rc) {
            BLOGE(sc, "FLR cleanup failed!\n");
            // XXX bxe_fw_dump(sc);
            // XXX bxe_idle_chk(sc);
            return (rc);
        }
    }

    /* set MSI reconfigure capability */
    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        addr = (port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0);
        val = REG_RD(sc, addr);
        val |= HC_CONFIG_0_REG_MSI_ATTN_EN_0;
        REG_WR(sc, addr, val);
    }

    ecore_init_block(sc, BLOCK_PXP, init_phase);
    ecore_init_block(sc, BLOCK_PXP2, init_phase);

    ilt = sc->ilt;
    cdu_ilt_start = ilt->clients[ILT_CLIENT_CDU].start;

    for (i = 0; i < L2_ILT_LINES(sc); i++) {
        ilt->lines[cdu_ilt_start + i].page = sc->context[i].vcxt;
        ilt->lines[cdu_ilt_start + i].page_mapping =
            sc->context[i].vcxt_dma.paddr;
        ilt->lines[cdu_ilt_start + i].size = sc->context[i].size;
    }
    ecore_ilt_init_op(sc, INITOP_SET);

    /* Set NIC mode */
    REG_WR(sc, PRS_REG_NIC_MODE, 1);
    BLOGD(sc, DBG_LOAD, "NIC MODE configured\n");

    if (!CHIP_IS_E1x(sc)) {
        uint32_t pf_conf = IGU_PF_CONF_FUNC_EN;

        /* Turn on a single ISR mode in IGU if driver is going to use
         * INT#x or MSI
         */
        if (sc->interrupt_mode != INTR_MODE_MSIX) {
            pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
        }

        /*
         * Timers workaround bug: function init part.
         * Need to wait 20msec after initializing ILT,
         * needed to make sure there are no requests in
         * one of the PXP internal queues with "old" ILT addresses
         */
        DELAY(20000);

        /*
         * Master enable - Due to WB DMAE writes performed before this
         * register is re-initialized as part of the regular function
         * init
         */
        REG_WR(sc, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
        /* Enable the function in IGU */
        REG_WR(sc, IGU_REG_PF_CONFIGURATION, pf_conf);
    }

    sc->dmae_ready = 1;

    ecore_init_block(sc, BLOCK_PGLUE_B, init_phase);

    if (!CHIP_IS_E1x(sc))
        REG_WR(sc, PGLUE_B_REG_WAS_ERROR_PF_7_0_CLR, func);

    ecore_init_block(sc, BLOCK_ATC, init_phase);
    ecore_init_block(sc, BLOCK_DMAE, init_phase);
    ecore_init_block(sc, BLOCK_NIG, init_phase);
    ecore_init_block(sc, BLOCK_SRC, init_phase);
    ecore_init_block(sc, BLOCK_MISC, init_phase);
    ecore_init_block(sc, BLOCK_TCM, init_phase);
    ecore_init_block(sc, BLOCK_UCM, init_phase);
    ecore_init_block(sc, BLOCK_CCM, init_phase);
    ecore_init_block(sc, BLOCK_XCM, init_phase);
    ecore_init_block(sc, BLOCK_TSEM, init_phase);
    ecore_init_block(sc, BLOCK_USEM, init_phase);
    ecore_init_block(sc, BLOCK_CSEM, init_phase);
    ecore_init_block(sc, BLOCK_XSEM, init_phase);

    if (!CHIP_IS_E1x(sc))
        REG_WR(sc, QM_REG_PF_EN, 1);

    if (!CHIP_IS_E1x(sc)) {
        REG_WR(sc, TSEM_REG_VFPF_ERR_NUM, BXE_MAX_NUM_OF_VFS + func);
        REG_WR(sc, USEM_REG_VFPF_ERR_NUM, BXE_MAX_NUM_OF_VFS + func);
        REG_WR(sc, CSEM_REG_VFPF_ERR_NUM, BXE_MAX_NUM_OF_VFS + func);
        REG_WR(sc, XSEM_REG_VFPF_ERR_NUM, BXE_MAX_NUM_OF_VFS + func);
    }
    ecore_init_block(sc, BLOCK_QM, init_phase);

    ecore_init_block(sc, BLOCK_TM, init_phase);
    ecore_init_block(sc, BLOCK_DORQ, init_phase);

    bxe_iov_init_dq(sc);

    ecore_init_block(sc, BLOCK_BRB1, init_phase);
    ecore_init_block(sc, BLOCK_PRS, init_phase);
    ecore_init_block(sc, BLOCK_TSDM, init_phase);
    ecore_init_block(sc, BLOCK_CSDM, init_phase);
    ecore_init_block(sc, BLOCK_USDM, init_phase);
    ecore_init_block(sc, BLOCK_XSDM, init_phase);
    ecore_init_block(sc, BLOCK_UPB, init_phase);
    ecore_init_block(sc, BLOCK_XPB, init_phase);
    ecore_init_block(sc, BLOCK_PBF, init_phase);
    if (!CHIP_IS_E1x(sc))
        REG_WR(sc, PBF_REG_DISABLE_PF, 0);

    ecore_init_block(sc, BLOCK_CDU, init_phase);

    ecore_init_block(sc, BLOCK_CFC, init_phase);

    if (!CHIP_IS_E1x(sc))
        REG_WR(sc, CFC_REG_WEAK_ENABLE_PF, 1);

    if (IS_MF(sc)) {
        REG_WR(sc, NIG_REG_LLH0_FUNC_EN + port*8, 1);
        REG_WR(sc, NIG_REG_LLH0_FUNC_VLAN_ID + port*8, OVLAN(sc));
    }

    ecore_init_block(sc, BLOCK_MISC_AEU, init_phase);

    /* HC init per function */
    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        if (CHIP_IS_E1H(sc)) {
            REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);

            REG_WR(sc, HC_REG_LEADING_EDGE_0 + port*8, 0);
            REG_WR(sc, HC_REG_TRAILING_EDGE_0 + port*8, 0);
        }
        ecore_init_block(sc, BLOCK_HC, init_phase);

    } else {
        int num_segs, sb_idx, prod_offset;

        REG_WR(sc, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);

        if (!CHIP_IS_E1x(sc)) {
            REG_WR(sc, IGU_REG_LEADING_EDGE_LATCH, 0);
            REG_WR(sc, IGU_REG_TRAILING_EDGE_LATCH, 0);
        }

        ecore_init_block(sc, BLOCK_IGU, init_phase);

        if (!CHIP_IS_E1x(sc)) {
            int dsb_idx = 0;
            /**
             * Producer memory:
             * E2 mode: address 0-135 match to the mapping memory;
             * 136 - PF0 default prod; 137 - PF1 default prod;
             * 138 - PF2 default prod; 139 - PF3 default prod;
             * 140 - PF0 attn prod;    141 - PF1 attn prod;
             * 142 - PF2 attn prod;    143 - PF3 attn prod;
             * 144-147 reserved.
             *
             * E1.5 mode - In backward compatible mode;
             * for non default SB; each even line in the memory
             * holds the U producer and each odd line hold
             * the C producer. The first 128 producers are for
             * NDSB (PF0 - 0-31; PF1 - 32-63 and so on). The last 20
             * producers are for the DSB for each PF.
             * Each PF has five segments: (the order inside each
             * segment is PF0; PF1; PF2; PF3) - 128-131 U prods;
             * 132-135 C prods; 136-139 X prods; 140-143 T prods;
             * 144-147 attn prods;
             */
            /* non-default-status-blocks */
            num_segs = CHIP_INT_MODE_IS_BC(sc) ?
                IGU_BC_NDSB_NUM_SEGS : IGU_NORM_NDSB_NUM_SEGS;
            for (sb_idx = 0; sb_idx < sc->igu_sb_cnt; sb_idx++) {
                prod_offset = (sc->igu_base_sb + sb_idx) *
                    num_segs;

                for (i = 0; i < num_segs; i++) {
                    addr = IGU_REG_PROD_CONS_MEMORY +
                            (prod_offset + i) * 4;
                    REG_WR(sc, addr, 0);
                }
                /* send consumer update with value 0 */
                bxe_ack_sb(sc, sc->igu_base_sb + sb_idx,
                           USTORM_ID, 0, IGU_INT_NOP, 1);
                bxe_igu_clear_sb(sc, sc->igu_base_sb + sb_idx);
            }

            /* default-status-blocks */
            num_segs = CHIP_INT_MODE_IS_BC(sc) ?
                IGU_BC_DSB_NUM_SEGS : IGU_NORM_DSB_NUM_SEGS;

            if (CHIP_IS_MODE_4_PORT(sc))
                dsb_idx = SC_FUNC(sc);
            else
                dsb_idx = SC_VN(sc);

            prod_offset = (CHIP_INT_MODE_IS_BC(sc) ?
                       IGU_BC_BASE_DSB_PROD + dsb_idx :
                       IGU_NORM_BASE_DSB_PROD + dsb_idx);

            /*
             * igu prods come in chunks of E1HVN_MAX (4) -
             * does not matters what is the current chip mode
             */
            for (i = 0; i < (num_segs * E1HVN_MAX);
                 i += E1HVN_MAX) {
                addr = IGU_REG_PROD_CONS_MEMORY +
                            (prod_offset + i)*4;
                REG_WR(sc, addr, 0);
            }
            /* send consumer update with 0 */
            if (CHIP_INT_MODE_IS_BC(sc)) {
                bxe_ack_sb(sc, sc->igu_dsb_id,
                           USTORM_ID, 0, IGU_INT_NOP, 1);
                bxe_ack_sb(sc, sc->igu_dsb_id,
                           CSTORM_ID, 0, IGU_INT_NOP, 1);
                bxe_ack_sb(sc, sc->igu_dsb_id,
                           XSTORM_ID, 0, IGU_INT_NOP, 1);
                bxe_ack_sb(sc, sc->igu_dsb_id,
                           TSTORM_ID, 0, IGU_INT_NOP, 1);
                bxe_ack_sb(sc, sc->igu_dsb_id,
                           ATTENTION_ID, 0, IGU_INT_NOP, 1);
            } else {
                bxe_ack_sb(sc, sc->igu_dsb_id,
                           USTORM_ID, 0, IGU_INT_NOP, 1);
                bxe_ack_sb(sc, sc->igu_dsb_id,
                           ATTENTION_ID, 0, IGU_INT_NOP, 1);
            }
            bxe_igu_clear_sb(sc, sc->igu_dsb_id);

            /* !!! these should become driver const once
               rf-tool supports split-68 const */
            REG_WR(sc, IGU_REG_SB_INT_BEFORE_MASK_LSB, 0);
            REG_WR(sc, IGU_REG_SB_INT_BEFORE_MASK_MSB, 0);
            REG_WR(sc, IGU_REG_SB_MASK_LSB, 0);
            REG_WR(sc, IGU_REG_SB_MASK_MSB, 0);
            REG_WR(sc, IGU_REG_PBA_STATUS_LSB, 0);
            REG_WR(sc, IGU_REG_PBA_STATUS_MSB, 0);
        }
    }

    /* Reset PCIE errors for debug */
    REG_WR(sc, 0x2114, 0xffffffff);
    REG_WR(sc, 0x2120, 0xffffffff);

    if (CHIP_IS_E1x(sc)) {
        main_mem_size = HC_REG_MAIN_MEMORY_SIZE / 2; /*dwords*/
        main_mem_base = HC_REG_MAIN_MEMORY +
                SC_PORT(sc) * (main_mem_size * 4);
        main_mem_prty_clr = HC_REG_HC_PRTY_STS_CLR;
        main_mem_width = 8;

        val = REG_RD(sc, main_mem_prty_clr);
        if (val) {
            BLOGD(sc, DBG_LOAD,
                  "Parity errors in HC block during function init (0x%x)!\n",
                  val);
        }

        /* Clear "false" parity errors in MSI-X table */
        for (i = main_mem_base;
             i < main_mem_base + main_mem_size * 4;
             i += main_mem_width) {
            bxe_read_dmae(sc, i, main_mem_width / 4);
            bxe_write_dmae(sc, BXE_SP_MAPPING(sc, wb_data),
                           i, main_mem_width / 4);
        }
        /* Clear HC parity attention */
        REG_RD(sc, main_mem_prty_clr);
    }

#if 1
    /* Enable STORMs SP logging */
    REG_WR8(sc, BAR_USTRORM_INTMEM +
           USTORM_RECORD_SLOW_PATH_OFFSET(SC_FUNC(sc)), 1);
    REG_WR8(sc, BAR_TSTRORM_INTMEM +
           TSTORM_RECORD_SLOW_PATH_OFFSET(SC_FUNC(sc)), 1);
    REG_WR8(sc, BAR_CSTRORM_INTMEM +
           CSTORM_RECORD_SLOW_PATH_OFFSET(SC_FUNC(sc)), 1);
    REG_WR8(sc, BAR_XSTRORM_INTMEM +
           XSTORM_RECORD_SLOW_PATH_OFFSET(SC_FUNC(sc)), 1);
#endif

    elink_phy_probe(&sc->link_params);

    return (0);
}

static void
bxe_link_reset(struct bxe_softc *sc)
{
    if (!BXE_NOMCP(sc)) {
	bxe_acquire_phy_lock(sc);
        elink_lfa_reset(&sc->link_params, &sc->link_vars);
	bxe_release_phy_lock(sc);
    } else {
        if (!CHIP_REV_IS_SLOW(sc)) {
            BLOGW(sc, "Bootcode is missing - cannot reset link\n");
        }
    }
}

static void
bxe_reset_port(struct bxe_softc *sc)
{
    int port = SC_PORT(sc);
    uint32_t val;

	ELINK_DEBUG_P0(sc, "bxe_reset_port called\n");
    /* reset physical Link */
    bxe_link_reset(sc);

    REG_WR(sc, NIG_REG_MASK_INTERRUPT_PORT0 + port*4, 0);

    /* Do not rcv packets to BRB */
    REG_WR(sc, NIG_REG_LLH0_BRB1_DRV_MASK + port*4, 0x0);
    /* Do not direct rcv packets that are not for MCP to the BRB */
    REG_WR(sc, (port ? NIG_REG_LLH1_BRB1_NOT_MCP :
               NIG_REG_LLH0_BRB1_NOT_MCP), 0x0);

    /* Configure AEU */
    REG_WR(sc, MISC_REG_AEU_MASK_ATTN_FUNC_0 + port*4, 0);

    DELAY(100000);

    /* Check for BRB port occupancy */
    val = REG_RD(sc, BRB1_REG_PORT_NUM_OCC_BLOCKS_0 + port*4);
    if (val) {
        BLOGD(sc, DBG_LOAD,
              "BRB1 is not empty, %d blocks are occupied\n", val);
    }

    /* TODO: Close Doorbell port? */
}

static void
bxe_ilt_wr(struct bxe_softc *sc,
           uint32_t         index,
           bus_addr_t       addr)
{
    int reg;
    uint32_t wb_write[2];

    if (CHIP_IS_E1(sc)) {
        reg = PXP2_REG_RQ_ONCHIP_AT + index*8;
    } else {
        reg = PXP2_REG_RQ_ONCHIP_AT_B0 + index*8;
    }

    wb_write[0] = ONCHIP_ADDR1(addr);
    wb_write[1] = ONCHIP_ADDR2(addr);
    REG_WR_DMAE(sc, reg, wb_write, 2);
}

static void
bxe_clear_func_ilt(struct bxe_softc *sc,
                   uint32_t         func)
{
    uint32_t i, base = FUNC_ILT_BASE(func);
    for (i = base; i < base + ILT_PER_FUNC; i++) {
        bxe_ilt_wr(sc, i, 0);
    }
}

static void
bxe_reset_func(struct bxe_softc *sc)
{
    struct bxe_fastpath *fp;
    int port = SC_PORT(sc);
    int func = SC_FUNC(sc);
    int i;

    /* Disable the function in the FW */
    REG_WR8(sc, BAR_XSTRORM_INTMEM + XSTORM_FUNC_EN_OFFSET(func), 0);
    REG_WR8(sc, BAR_CSTRORM_INTMEM + CSTORM_FUNC_EN_OFFSET(func), 0);
    REG_WR8(sc, BAR_TSTRORM_INTMEM + TSTORM_FUNC_EN_OFFSET(func), 0);
    REG_WR8(sc, BAR_USTRORM_INTMEM + USTORM_FUNC_EN_OFFSET(func), 0);

    /* FP SBs */
    FOR_EACH_ETH_QUEUE(sc, i) {
        fp = &sc->fp[i];
        REG_WR8(sc, BAR_CSTRORM_INTMEM +
                CSTORM_STATUS_BLOCK_DATA_STATE_OFFSET(fp->fw_sb_id),
                SB_DISABLED);
    }

    /* SP SB */
    REG_WR8(sc, BAR_CSTRORM_INTMEM +
            CSTORM_SP_STATUS_BLOCK_DATA_STATE_OFFSET(func),
            SB_DISABLED);

    for (i = 0; i < XSTORM_SPQ_DATA_SIZE / 4; i++) {
        REG_WR(sc, BAR_XSTRORM_INTMEM + XSTORM_SPQ_DATA_OFFSET(func), 0);
    }

    /* Configure IGU */
    if (sc->devinfo.int_block == INT_BLOCK_HC) {
        REG_WR(sc, HC_REG_LEADING_EDGE_0 + port*8, 0);
        REG_WR(sc, HC_REG_TRAILING_EDGE_0 + port*8, 0);
    } else {
        REG_WR(sc, IGU_REG_LEADING_EDGE_LATCH, 0);
        REG_WR(sc, IGU_REG_TRAILING_EDGE_LATCH, 0);
    }

    if (CNIC_LOADED(sc)) {
        /* Disable Timer scan */
        REG_WR(sc, TM_REG_EN_LINEAR0_TIMER + port*4, 0);
        /*
         * Wait for at least 10ms and up to 2 second for the timers
         * scan to complete
         */
        for (i = 0; i < 200; i++) {
            DELAY(10000);
            if (!REG_RD(sc, TM_REG_LIN0_SCAN_ON + port*4))
                break;
        }
    }

    /* Clear ILT */
    bxe_clear_func_ilt(sc, func);

    /*
     * Timers workaround bug for E2: if this is vnic-3,
     * we need to set the entire ilt range for this timers.
     */
    if (!CHIP_IS_E1x(sc) && SC_VN(sc) == 3) {
        struct ilt_client_info ilt_cli;
        /* use dummy TM client */
        memset(&ilt_cli, 0, sizeof(struct ilt_client_info));
        ilt_cli.start = 0;
        ilt_cli.end = ILT_NUM_PAGE_ENTRIES - 1;
        ilt_cli.client_num = ILT_CLIENT_TM;

        ecore_ilt_boundry_init_op(sc, &ilt_cli, 0, INITOP_CLEAR);
    }

    /* this assumes that reset_port() called before reset_func()*/
    if (!CHIP_IS_E1x(sc)) {
        bxe_pf_disable(sc);
    }

    sc->dmae_ready = 0;
}

static int
bxe_gunzip_init(struct bxe_softc *sc)
{
    return (0);
}

static void
bxe_gunzip_end(struct bxe_softc *sc)
{
    return;
}

static int
bxe_init_firmware(struct bxe_softc *sc)
{
    if (CHIP_IS_E1(sc)) {
        ecore_init_e1_firmware(sc);
        sc->iro_array = e1_iro_arr;
    } else if (CHIP_IS_E1H(sc)) {
        ecore_init_e1h_firmware(sc);
        sc->iro_array = e1h_iro_arr;
    } else if (!CHIP_IS_E1x(sc)) {
        ecore_init_e2_firmware(sc);
        sc->iro_array = e2_iro_arr;
    } else {
        BLOGE(sc, "Unsupported chip revision\n");
        return (-1);
    }

    return (0);
}

static void
bxe_release_firmware(struct bxe_softc *sc)
{
    /* Do nothing */
    return;
}

static int
ecore_gunzip(struct bxe_softc *sc,
             const uint8_t    *zbuf,
             int              len)
{
    /* XXX : Implement... */
    BLOGD(sc, DBG_LOAD, "ECORE_GUNZIP NOT IMPLEMENTED\n");
    return (FALSE);
}

static void
ecore_reg_wr_ind(struct bxe_softc *sc,
                 uint32_t         addr,
                 uint32_t         val)
{
    bxe_reg_wr_ind(sc, addr, val);
}

static void
ecore_write_dmae_phys_len(struct bxe_softc *sc,
                          bus_addr_t       phys_addr,
                          uint32_t         addr,
                          uint32_t         len)
{
    bxe_write_dmae_phys_len(sc, phys_addr, addr, len);
}

void
ecore_storm_memset_struct(struct bxe_softc *sc,
                          uint32_t         addr,
                          size_t           size,
                          uint32_t         *data)
{
    uint8_t i;
    for (i = 0; i < size/4; i++) {
        REG_WR(sc, addr + (i * 4), data[i]);
    }
}


/*
 * character device - ioctl interface definitions
 */


#include "bxe_dump.h"
#include "bxe_ioctl.h"
#include <sys/conf.h>

static int bxe_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
                struct thread *td);

static struct cdevsw bxe_cdevsw = {
    .d_version = D_VERSION,
    .d_ioctl = bxe_eioctl,
    .d_name = "bxecnic",
};

#define BXE_PATH(sc)    (CHIP_IS_E1x(sc) ? 0 : (sc->pcie_func & 1))


#define DUMP_ALL_PRESETS        0x1FFF
#define DUMP_MAX_PRESETS        13
#define IS_E1_REG(chips)        ((chips & DUMP_CHIP_E1) == DUMP_CHIP_E1)
#define IS_E1H_REG(chips)       ((chips & DUMP_CHIP_E1H) == DUMP_CHIP_E1H)
#define IS_E2_REG(chips)        ((chips & DUMP_CHIP_E2) == DUMP_CHIP_E2)
#define IS_E3A0_REG(chips)      ((chips & DUMP_CHIP_E3A0) == DUMP_CHIP_E3A0)
#define IS_E3B0_REG(chips)      ((chips & DUMP_CHIP_E3B0) == DUMP_CHIP_E3B0)

#define IS_REG_IN_PRESET(presets, idx)  \
                ((presets & (1 << (idx-1))) == (1 << (idx-1)))


static int
bxe_get_preset_regs_len(struct bxe_softc *sc, uint32_t preset)
{
    if (CHIP_IS_E1(sc))
        return dump_num_registers[0][preset-1];
    else if (CHIP_IS_E1H(sc))
        return dump_num_registers[1][preset-1];
    else if (CHIP_IS_E2(sc))
        return dump_num_registers[2][preset-1];
    else if (CHIP_IS_E3A0(sc))
        return dump_num_registers[3][preset-1];
    else if (CHIP_IS_E3B0(sc))
        return dump_num_registers[4][preset-1];
    else
        return 0;
}

static int
bxe_get_total_regs_len32(struct bxe_softc *sc)
{
    uint32_t preset_idx;
    int regdump_len32 = 0;


    /* Calculate the total preset regs length */
    for (preset_idx = 1; preset_idx <= DUMP_MAX_PRESETS; preset_idx++) {
        regdump_len32 += bxe_get_preset_regs_len(sc, preset_idx);
    }

    return regdump_len32;
}

static const uint32_t *
__bxe_get_page_addr_ar(struct bxe_softc *sc)
{
    if (CHIP_IS_E2(sc))
        return page_vals_e2;
    else if (CHIP_IS_E3(sc))
        return page_vals_e3;
    else
        return NULL;
}

static uint32_t
__bxe_get_page_reg_num(struct bxe_softc *sc)
{
    if (CHIP_IS_E2(sc))
        return PAGE_MODE_VALUES_E2;
    else if (CHIP_IS_E3(sc))
        return PAGE_MODE_VALUES_E3;
    else
        return 0;
}

static const uint32_t *
__bxe_get_page_write_ar(struct bxe_softc *sc)
{
    if (CHIP_IS_E2(sc))
        return page_write_regs_e2;
    else if (CHIP_IS_E3(sc))
        return page_write_regs_e3;
    else
        return NULL;
}

static uint32_t
__bxe_get_page_write_num(struct bxe_softc *sc)
{
    if (CHIP_IS_E2(sc))
        return PAGE_WRITE_REGS_E2;
    else if (CHIP_IS_E3(sc))
        return PAGE_WRITE_REGS_E3;
    else
        return 0;
}

static const struct reg_addr *
__bxe_get_page_read_ar(struct bxe_softc *sc)
{
    if (CHIP_IS_E2(sc))
        return page_read_regs_e2;
    else if (CHIP_IS_E3(sc))
        return page_read_regs_e3;
    else
        return NULL;
}

static uint32_t
__bxe_get_page_read_num(struct bxe_softc *sc)
{
    if (CHIP_IS_E2(sc))
        return PAGE_READ_REGS_E2;
    else if (CHIP_IS_E3(sc))
        return PAGE_READ_REGS_E3;
    else
        return 0;
}

static bool
bxe_is_reg_in_chip(struct bxe_softc *sc, const struct reg_addr *reg_info)
{
    if (CHIP_IS_E1(sc))
        return IS_E1_REG(reg_info->chips);
    else if (CHIP_IS_E1H(sc))
        return IS_E1H_REG(reg_info->chips);
    else if (CHIP_IS_E2(sc))
        return IS_E2_REG(reg_info->chips);
    else if (CHIP_IS_E3A0(sc))
        return IS_E3A0_REG(reg_info->chips);
    else if (CHIP_IS_E3B0(sc))
        return IS_E3B0_REG(reg_info->chips);
    else
        return 0;
}

static bool
bxe_is_wreg_in_chip(struct bxe_softc *sc, const struct wreg_addr *wreg_info)
{
    if (CHIP_IS_E1(sc))
        return IS_E1_REG(wreg_info->chips);
    else if (CHIP_IS_E1H(sc))
        return IS_E1H_REG(wreg_info->chips);
    else if (CHIP_IS_E2(sc))
        return IS_E2_REG(wreg_info->chips);
    else if (CHIP_IS_E3A0(sc))
        return IS_E3A0_REG(wreg_info->chips);
    else if (CHIP_IS_E3B0(sc))
        return IS_E3B0_REG(wreg_info->chips);
    else
        return 0;
}

/**
 * bxe_read_pages_regs - read "paged" registers
 *
 * @bp          device handle
 * @p           output buffer
 *
 * Reads "paged" memories: memories that may only be read by first writing to a
 * specific address ("write address") and then reading from a specific address
 * ("read address"). There may be more than one write address per "page" and
 * more than one read address per write address.
 */
static void
bxe_read_pages_regs(struct bxe_softc *sc, uint32_t *p, uint32_t preset)
{
    uint32_t i, j, k, n;

    /* addresses of the paged registers */
    const uint32_t *page_addr = __bxe_get_page_addr_ar(sc);
    /* number of paged registers */
    int num_pages = __bxe_get_page_reg_num(sc);
    /* write addresses */
    const uint32_t *write_addr = __bxe_get_page_write_ar(sc);
    /* number of write addresses */
    int write_num = __bxe_get_page_write_num(sc);
    /* read addresses info */
    const struct reg_addr *read_addr = __bxe_get_page_read_ar(sc);
    /* number of read addresses */
    int read_num = __bxe_get_page_read_num(sc);
    uint32_t addr, size;

    for (i = 0; i < num_pages; i++) {
        for (j = 0; j < write_num; j++) {
            REG_WR(sc, write_addr[j], page_addr[i]);

            for (k = 0; k < read_num; k++) {
                if (IS_REG_IN_PRESET(read_addr[k].presets, preset)) {
                    size = read_addr[k].size;
                    for (n = 0; n < size; n++) {
                        addr = read_addr[k].addr + n*4;
                        *p++ = REG_RD(sc, addr);
                    }
                }
            }
        }
    }
    return;
}


static int
bxe_get_preset_regs(struct bxe_softc *sc, uint32_t *p, uint32_t preset)
{
    uint32_t i, j, addr;
    const struct wreg_addr *wreg_addr_p = NULL;

    if (CHIP_IS_E1(sc))
        wreg_addr_p = &wreg_addr_e1;
    else if (CHIP_IS_E1H(sc))
        wreg_addr_p = &wreg_addr_e1h;
    else if (CHIP_IS_E2(sc))
        wreg_addr_p = &wreg_addr_e2;
    else if (CHIP_IS_E3A0(sc))
        wreg_addr_p = &wreg_addr_e3;
    else if (CHIP_IS_E3B0(sc))
        wreg_addr_p = &wreg_addr_e3b0;
    else
        return (-1);

    /* Read the idle_chk registers */
    for (i = 0; i < IDLE_REGS_COUNT; i++) {
        if (bxe_is_reg_in_chip(sc, &idle_reg_addrs[i]) &&
            IS_REG_IN_PRESET(idle_reg_addrs[i].presets, preset)) {
            for (j = 0; j < idle_reg_addrs[i].size; j++)
                *p++ = REG_RD(sc, idle_reg_addrs[i].addr + j*4);
        }
    }

    /* Read the regular registers */
    for (i = 0; i < REGS_COUNT; i++) {
        if (bxe_is_reg_in_chip(sc, &reg_addrs[i]) &&
            IS_REG_IN_PRESET(reg_addrs[i].presets, preset)) {
            for (j = 0; j < reg_addrs[i].size; j++)
                *p++ = REG_RD(sc, reg_addrs[i].addr + j*4);
        }
    }

    /* Read the CAM registers */
    if (bxe_is_wreg_in_chip(sc, wreg_addr_p) &&
        IS_REG_IN_PRESET(wreg_addr_p->presets, preset)) {
        for (i = 0; i < wreg_addr_p->size; i++) {
            *p++ = REG_RD(sc, wreg_addr_p->addr + i*4);

            /* In case of wreg_addr register, read additional
               registers from read_regs array
             */
            for (j = 0; j < wreg_addr_p->read_regs_count; j++) {
                addr = *(wreg_addr_p->read_regs);
                *p++ = REG_RD(sc, addr + j*4);
            }
        }
    }

    /* Paged registers are supported in E2 & E3 only */
    if (CHIP_IS_E2(sc) || CHIP_IS_E3(sc)) {
        /* Read "paged" registers */
        bxe_read_pages_regs(sc, p, preset);
    }

    return 0;
}

int
bxe_grc_dump(struct bxe_softc *sc)
{
    int rval = 0;
    uint32_t preset_idx;
    uint8_t *buf;
    uint32_t size;
    struct  dump_header *d_hdr;
    uint32_t i;
    uint32_t reg_val;
    uint32_t reg_addr;
    uint32_t cmd_offset;
    struct ecore_ilt *ilt = SC_ILT(sc);
    struct bxe_fastpath *fp;
    struct ilt_client_info *ilt_cli;
    int grc_dump_size;


    if (sc->grcdump_done || sc->grcdump_started)
	return (rval);
    
    sc->grcdump_started = 1;
    BLOGI(sc, "Started collecting grcdump\n");

    grc_dump_size = (bxe_get_total_regs_len32(sc) * sizeof(uint32_t)) +
                sizeof(struct  dump_header);

    sc->grc_dump = malloc(grc_dump_size, M_DEVBUF, M_NOWAIT);

    if (sc->grc_dump == NULL) {
        BLOGW(sc, "Unable to allocate memory for grcdump collection\n");
        return(ENOMEM);
    }



    /* Disable parity attentions as long as following dump may
     * cause false alarms by reading never written registers. We
     * will re-enable parity attentions right after the dump.
     */

    /* Disable parity on path 0 */
    bxe_pretend_func(sc, 0);

    ecore_disable_blocks_parity(sc);

    /* Disable parity on path 1 */
    bxe_pretend_func(sc, 1);
    ecore_disable_blocks_parity(sc);

    /* Return to current function */
    bxe_pretend_func(sc, SC_ABS_FUNC(sc));

    buf = sc->grc_dump;
    d_hdr = sc->grc_dump;

    d_hdr->header_size = (sizeof(struct  dump_header) >> 2) - 1;
    d_hdr->version = BNX2X_DUMP_VERSION;
    d_hdr->preset = DUMP_ALL_PRESETS;

    if (CHIP_IS_E1(sc)) {
        d_hdr->dump_meta_data = DUMP_CHIP_E1;
    } else if (CHIP_IS_E1H(sc)) {
        d_hdr->dump_meta_data = DUMP_CHIP_E1H;
    } else if (CHIP_IS_E2(sc)) {
        d_hdr->dump_meta_data = DUMP_CHIP_E2 |
                (BXE_PATH(sc) ? DUMP_PATH_1 : DUMP_PATH_0);
    } else if (CHIP_IS_E3A0(sc)) {
        d_hdr->dump_meta_data = DUMP_CHIP_E3A0 |
                (BXE_PATH(sc) ? DUMP_PATH_1 : DUMP_PATH_0);
    } else if (CHIP_IS_E3B0(sc)) {
        d_hdr->dump_meta_data = DUMP_CHIP_E3B0 |
                (BXE_PATH(sc) ? DUMP_PATH_1 : DUMP_PATH_0);
    }

    buf += sizeof(struct  dump_header);

    for (preset_idx = 1; preset_idx <= DUMP_MAX_PRESETS; preset_idx++) {

        /* Skip presets with IOR */
        if ((preset_idx == 2) || (preset_idx == 5) || (preset_idx == 8) ||
            (preset_idx == 11))
            continue;

        rval = bxe_get_preset_regs(sc, (uint32_t *)buf, preset_idx);

	if (rval)
            break;

        size = bxe_get_preset_regs_len(sc, preset_idx) * (sizeof (uint32_t));

        buf += size;
    }

    bxe_pretend_func(sc, 0);
    ecore_clear_blocks_parity(sc);
    ecore_enable_blocks_parity(sc);

    bxe_pretend_func(sc, 1);
    ecore_clear_blocks_parity(sc);
    ecore_enable_blocks_parity(sc);

    /* Return to current function */
    bxe_pretend_func(sc, SC_ABS_FUNC(sc));



    if(sc->state == BXE_STATE_OPEN) {
        if(sc->fw_stats_req  != NULL) {
    		BLOGI(sc, "fw stats start_paddr %#jx end_paddr %#jx vaddr %p size 0x%x\n",
        			(uintmax_t)sc->fw_stats_req_mapping,
        			(uintmax_t)sc->fw_stats_data_mapping,
        			sc->fw_stats_req, (sc->fw_stats_req_size + sc->fw_stats_data_size));
		}	
		if(sc->def_sb != NULL) {
			BLOGI(sc, "def_status_block paddr %p vaddr %p size 0x%zx\n",
        			(void *)sc->def_sb_dma.paddr, sc->def_sb,
        			sizeof(struct host_sp_status_block));
		}
		if(sc->eq_dma.vaddr != NULL) {
    		BLOGI(sc, "event_queue paddr %#jx vaddr %p size 0x%x\n",
        			(uintmax_t)sc->eq_dma.paddr, sc->eq_dma.vaddr, BCM_PAGE_SIZE);
		}
		if(sc->sp_dma.vaddr != NULL) {
    		BLOGI(sc, "slow path paddr %#jx vaddr %p size 0x%zx\n",
        			(uintmax_t)sc->sp_dma.paddr, sc->sp_dma.vaddr,
        			sizeof(struct bxe_slowpath));
		}
		if(sc->spq_dma.vaddr != NULL) {
    		BLOGI(sc, "slow path queue paddr %#jx vaddr %p size 0x%x\n",
        			(uintmax_t)sc->spq_dma.paddr, sc->spq_dma.vaddr, BCM_PAGE_SIZE);
		}
		if(sc->gz_buf_dma.vaddr != NULL) {
    		BLOGI(sc, "fw_buf paddr %#jx vaddr %p size 0x%x\n",
        			(uintmax_t)sc->gz_buf_dma.paddr, sc->gz_buf_dma.vaddr,
        			FW_BUF_SIZE);
		}
    	for (i = 0; i < sc->num_queues; i++) {
        	fp = &sc->fp[i];
			if(fp->sb_dma.vaddr != NULL && fp->tx_dma.vaddr != NULL &&
                        fp->rx_dma.vaddr != NULL && fp->rcq_dma.vaddr != NULL &&
                        fp->rx_sge_dma.vaddr != NULL) {

				BLOGI(sc, "FP status block fp %d paddr %#jx vaddr %p size 0x%zx\n", i,
            			(uintmax_t)fp->sb_dma.paddr, fp->sb_dma.vaddr,
            			sizeof(union bxe_host_hc_status_block));
				BLOGI(sc, "TX BD CHAIN fp %d paddr %#jx vaddr %p size 0x%x\n", i,
            			(uintmax_t)fp->tx_dma.paddr, fp->tx_dma.vaddr,
            			(BCM_PAGE_SIZE * TX_BD_NUM_PAGES));
        		BLOGI(sc, "RX BD CHAIN fp %d paddr %#jx vaddr %p size 0x%x\n", i,
            			(uintmax_t)fp->rx_dma.paddr, fp->rx_dma.vaddr,
            			(BCM_PAGE_SIZE * RX_BD_NUM_PAGES));
        		BLOGI(sc, "RX RCQ CHAIN fp %d paddr %#jx vaddr %p size 0x%zx\n", i,
            			(uintmax_t)fp->rcq_dma.paddr, fp->rcq_dma.vaddr,
            			(BCM_PAGE_SIZE * RCQ_NUM_PAGES));
        		BLOGI(sc, "RX SGE CHAIN fp %d paddr %#jx vaddr %p size 0x%x\n", i,
            			(uintmax_t)fp->rx_sge_dma.paddr, fp->rx_sge_dma.vaddr,
            			(BCM_PAGE_SIZE * RX_SGE_NUM_PAGES));
    		}
		}
		if(ilt != NULL ) {
    		ilt_cli = &ilt->clients[1];
			if(ilt->lines != NULL) {
    		for (i = ilt_cli->start; i <= ilt_cli->end; i++) {
        		BLOGI(sc, "ECORE_ILT paddr %#jx vaddr %p size 0x%x\n",
            			(uintmax_t)(((struct bxe_dma *)((&ilt->lines[i])->page))->paddr),
            			((struct bxe_dma *)((&ilt->lines[i])->page))->vaddr, BCM_PAGE_SIZE);
    		}
			}
		}


    	cmd_offset = DMAE_REG_CMD_MEM;
    	for (i = 0; i < 224; i++) {
        	reg_addr = (cmd_offset +(i * 4));
        	reg_val = REG_RD(sc, reg_addr);
        	BLOGI(sc, "DMAE_REG_CMD_MEM i=%d reg_addr 0x%x reg_val 0x%08x\n",i,
            			reg_addr, reg_val);
    	}
	}

    BLOGI(sc, "Collection of grcdump done\n");
    sc->grcdump_done = 1;
    return(rval);
}

static int
bxe_add_cdev(struct bxe_softc *sc)
{
    sc->eeprom = malloc(BXE_EEPROM_MAX_DATA_LEN, M_DEVBUF, M_NOWAIT);

    if (sc->eeprom == NULL) {
        BLOGW(sc, "Unable to alloc for eeprom size buffer\n");
        return (-1);
    }

    sc->ioctl_dev = make_dev(&bxe_cdevsw,
                            sc->ifp->if_dunit,
                            UID_ROOT,
                            GID_WHEEL,
                            0600,
                            "%s",
                            if_name(sc->ifp));

    if (sc->ioctl_dev == NULL) {
        free(sc->eeprom, M_DEVBUF);
        sc->eeprom = NULL;
        return (-1);
    }

    sc->ioctl_dev->si_drv1 = sc;

    return (0);
}

static void
bxe_del_cdev(struct bxe_softc *sc)
{
    if (sc->ioctl_dev != NULL)
        destroy_dev(sc->ioctl_dev);

    if (sc->eeprom != NULL) {
        free(sc->eeprom, M_DEVBUF);
        sc->eeprom = NULL;
    }
    sc->ioctl_dev = NULL;

    return;
}

static bool bxe_is_nvram_accessible(struct bxe_softc *sc)
{

    if ((if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) == 0)
        return FALSE;

    return TRUE;
}


static int
bxe_wr_eeprom(struct bxe_softc *sc, void *data, uint32_t offset, uint32_t len)
{
    int rval = 0;

    if(!bxe_is_nvram_accessible(sc)) {
        BLOGW(sc, "Cannot access eeprom when interface is down\n");
        return (-EAGAIN);
    }
    rval = bxe_nvram_write(sc, offset, (uint8_t *)data, len);


   return (rval);
}

static int
bxe_rd_eeprom(struct bxe_softc *sc, void *data, uint32_t offset, uint32_t len)
{
    int rval = 0;

    if(!bxe_is_nvram_accessible(sc)) {
        BLOGW(sc, "Cannot access eeprom when interface is down\n");
        return (-EAGAIN);
    }
    rval = bxe_nvram_read(sc, offset, (uint8_t *)data, len);

   return (rval);
}

static int
bxe_eeprom_rd_wr(struct bxe_softc *sc, bxe_eeprom_t *eeprom)
{
    int rval = 0;

    switch (eeprom->eeprom_cmd) {

    case BXE_EEPROM_CMD_SET_EEPROM:

        rval = copyin(eeprom->eeprom_data, sc->eeprom,
                       eeprom->eeprom_data_len);

        if (rval)
            break;

        rval = bxe_wr_eeprom(sc, sc->eeprom, eeprom->eeprom_offset,
                       eeprom->eeprom_data_len);
        break;

    case BXE_EEPROM_CMD_GET_EEPROM:

        rval = bxe_rd_eeprom(sc, sc->eeprom, eeprom->eeprom_offset,
                       eeprom->eeprom_data_len);

        if (rval) {
            break;
        }

        rval = copyout(sc->eeprom, eeprom->eeprom_data,
                       eeprom->eeprom_data_len);
        break;

    default:
            rval = EINVAL;
            break;
    }

    if (rval) {
        BLOGW(sc, "ioctl cmd %d  failed rval %d\n", eeprom->eeprom_cmd, rval);
    }

    return (rval);
}

static int
bxe_get_settings(struct bxe_softc *sc, bxe_dev_setting_t *dev_p)
{
    uint32_t ext_phy_config;
    int port = SC_PORT(sc);
    int cfg_idx = bxe_get_link_cfg_idx(sc);

    dev_p->supported = sc->port.supported[cfg_idx] |
            (sc->port.supported[cfg_idx ^ 1] &
            (ELINK_SUPPORTED_TP | ELINK_SUPPORTED_FIBRE));
    dev_p->advertising = sc->port.advertising[cfg_idx];
    if(sc->link_params.phy[bxe_get_cur_phy_idx(sc)].media_type ==
        ELINK_ETH_PHY_SFP_1G_FIBER) {
        dev_p->supported = ~(ELINK_SUPPORTED_10000baseT_Full);
        dev_p->advertising &= ~(ADVERTISED_10000baseT_Full);
    }
    if ((sc->state == BXE_STATE_OPEN) && sc->link_vars.link_up &&
        !(sc->flags & BXE_MF_FUNC_DIS)) {
        dev_p->duplex = sc->link_vars.duplex;
        if (IS_MF(sc) && !BXE_NOMCP(sc))
            dev_p->speed = bxe_get_mf_speed(sc);
        else
            dev_p->speed = sc->link_vars.line_speed;
    } else {
        dev_p->duplex = DUPLEX_UNKNOWN;
        dev_p->speed = SPEED_UNKNOWN;
    }

    dev_p->port = bxe_media_detect(sc);

    ext_phy_config = SHMEM_RD(sc,
                         dev_info.port_hw_config[port].external_phy_config);
    if((ext_phy_config & PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK) ==
        PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT)
        dev_p->phy_address =  sc->port.phy_addr;
    else if(((ext_phy_config & PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK) !=
            PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE) &&
        ((ext_phy_config & PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK) !=
            PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN))
        dev_p->phy_address = ELINK_XGXS_EXT_PHY_ADDR(ext_phy_config);
    else
        dev_p->phy_address = 0;

    if(sc->link_params.req_line_speed[cfg_idx] == ELINK_SPEED_AUTO_NEG)
        dev_p->autoneg = AUTONEG_ENABLE;
    else
       dev_p->autoneg = AUTONEG_DISABLE;


    return 0;
}

static int
bxe_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
        struct thread *td)
{
    struct bxe_softc    *sc;
    int                 rval = 0;
    device_t            pci_dev;
    bxe_grcdump_t       *dump = NULL;
    int grc_dump_size;
    bxe_drvinfo_t   *drv_infop = NULL;
    bxe_dev_setting_t  *dev_p;
    bxe_dev_setting_t  dev_set;
    bxe_get_regs_t  *reg_p;
    bxe_reg_rdw_t *reg_rdw_p;
    bxe_pcicfg_rdw_t *cfg_rdw_p;
    bxe_perm_mac_addr_t *mac_addr_p;


    if ((sc = (struct bxe_softc *)dev->si_drv1) == NULL)
        return ENXIO;

    pci_dev= sc->dev;

    dump = (bxe_grcdump_t *)data;

    switch(cmd) {

        case BXE_GRC_DUMP_SIZE:
            dump->pci_func = sc->pcie_func;
            dump->grcdump_size =
                (bxe_get_total_regs_len32(sc) * sizeof(uint32_t)) +
                     sizeof(struct  dump_header);
            break;

        case BXE_GRC_DUMP:
            
            grc_dump_size = (bxe_get_total_regs_len32(sc) * sizeof(uint32_t)) +
                                sizeof(struct  dump_header);
            if ((!sc->trigger_grcdump) || (dump->grcdump == NULL) ||
                (dump->grcdump_size < grc_dump_size)) {
                rval = EINVAL;
                break;
            }

            if((sc->trigger_grcdump) && (!sc->grcdump_done) &&
                (!sc->grcdump_started)) {
                rval =  bxe_grc_dump(sc);
            }

            if((!rval) && (sc->grcdump_done) && (sc->grcdump_started) &&
                (sc->grc_dump != NULL))  {
                dump->grcdump_dwords = grc_dump_size >> 2;
                rval = copyout(sc->grc_dump, dump->grcdump, grc_dump_size);
                free(sc->grc_dump, M_DEVBUF);
                sc->grc_dump = NULL;
                sc->grcdump_started = 0;
                sc->grcdump_done = 0;
            }

            break;

        case BXE_DRV_INFO:
            drv_infop = (bxe_drvinfo_t *)data;
            snprintf(drv_infop->drv_name, BXE_DRV_NAME_LENGTH, "%s", "bxe");
            snprintf(drv_infop->drv_version, BXE_DRV_VERSION_LENGTH, "v:%s",
                BXE_DRIVER_VERSION);
            snprintf(drv_infop->mfw_version, BXE_MFW_VERSION_LENGTH, "%s",
                sc->devinfo.bc_ver_str);
            snprintf(drv_infop->stormfw_version, BXE_STORMFW_VERSION_LENGTH,
                "%s", sc->fw_ver_str);
            drv_infop->eeprom_dump_len = sc->devinfo.flash_size;
            drv_infop->reg_dump_len =
                (bxe_get_total_regs_len32(sc) * sizeof(uint32_t))
                    + sizeof(struct  dump_header);
            snprintf(drv_infop->bus_info, BXE_BUS_INFO_LENGTH, "%d:%d:%d",
                sc->pcie_bus, sc->pcie_device, sc->pcie_func);
            break;

        case BXE_DEV_SETTING:
            dev_p = (bxe_dev_setting_t *)data;
            bxe_get_settings(sc, &dev_set);
            dev_p->supported = dev_set.supported;
            dev_p->advertising = dev_set.advertising;
            dev_p->speed = dev_set.speed;
            dev_p->duplex = dev_set.duplex;
            dev_p->port = dev_set.port;
            dev_p->phy_address = dev_set.phy_address;
            dev_p->autoneg = dev_set.autoneg;

            break;

        case BXE_GET_REGS:

            reg_p = (bxe_get_regs_t *)data;
            grc_dump_size = reg_p->reg_buf_len;

            if((!sc->grcdump_done) && (!sc->grcdump_started)) {
                bxe_grc_dump(sc);
            }
            if((sc->grcdump_done) && (sc->grcdump_started) &&
                (sc->grc_dump != NULL))  {
                rval = copyout(sc->grc_dump, reg_p->reg_buf, grc_dump_size);
                free(sc->grc_dump, M_DEVBUF);
                sc->grc_dump = NULL;
                sc->grcdump_started = 0;
                sc->grcdump_done = 0;
            }

            break;

        case BXE_RDW_REG:
            reg_rdw_p = (bxe_reg_rdw_t *)data;
            if((reg_rdw_p->reg_cmd == BXE_READ_REG_CMD) &&
                (reg_rdw_p->reg_access_type == BXE_REG_ACCESS_DIRECT))
                reg_rdw_p->reg_val = REG_RD(sc, reg_rdw_p->reg_id);

            if((reg_rdw_p->reg_cmd == BXE_WRITE_REG_CMD) &&
                (reg_rdw_p->reg_access_type == BXE_REG_ACCESS_DIRECT))
                REG_WR(sc, reg_rdw_p->reg_id, reg_rdw_p->reg_val);

            break;

        case BXE_RDW_PCICFG:
            cfg_rdw_p = (bxe_pcicfg_rdw_t *)data;
            if(cfg_rdw_p->cfg_cmd == BXE_READ_PCICFG) {

                cfg_rdw_p->cfg_val = pci_read_config(sc->dev, cfg_rdw_p->cfg_id,
                                         cfg_rdw_p->cfg_width);

            } else if(cfg_rdw_p->cfg_cmd == BXE_WRITE_PCICFG) {
                pci_write_config(sc->dev, cfg_rdw_p->cfg_id, cfg_rdw_p->cfg_val,
                            cfg_rdw_p->cfg_width);
            } else {
                BLOGW(sc, "BXE_RDW_PCICFG ioctl wrong cmd passed\n");
            }
            break;

        case BXE_MAC_ADDR:
            mac_addr_p = (bxe_perm_mac_addr_t *)data;
            snprintf(mac_addr_p->mac_addr_str, sizeof(sc->mac_addr_str), "%s",
                sc->mac_addr_str);
            break;

        case BXE_EEPROM:
            rval = bxe_eeprom_rd_wr(sc, (bxe_eeprom_t *)data);
            break;


        default:
            break;
    }

    return (rval);
}

#ifdef NETDUMP
static void
bxe_netdump_init(struct ifnet *ifp, int *nrxr, int *ncl, int *clsize)
{
	struct bxe_softc *sc;

	sc = if_getsoftc(ifp);
	BXE_CORE_LOCK(sc);
	*nrxr = sc->num_queues;
	*ncl = NETDUMP_MAX_IN_FLIGHT;
	*clsize = sc->fp[0].mbuf_alloc_size;
	BXE_CORE_UNLOCK(sc);
}

static void
bxe_netdump_event(struct ifnet *ifp __unused, enum netdump_ev event __unused)
{
}

static int
bxe_netdump_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct bxe_softc *sc;
	int error;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || !sc->link_vars.link_up)
		return (ENOENT);

	error = bxe_tx_encap(&sc->fp[0], &m);
	if (error != 0 && m != NULL)
		m_freem(m);
	return (error);
}

static int
bxe_netdump_poll(struct ifnet *ifp, int count)
{
	struct bxe_softc *sc;
	int i;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0 ||
	    !sc->link_vars.link_up)
		return (ENOENT);

	for (i = 0; i < sc->num_queues; i++)
		(void)bxe_rxeof(sc, &sc->fp[i]);
	(void)bxe_txeof(sc, &sc->fp[0]);
	return (0);
}
#endif /* NETDUMP */
