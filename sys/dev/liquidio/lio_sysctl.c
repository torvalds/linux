/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#include <sys/types.h>

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_network.h"
#include "lio_ctrl.h"
#include "cn23xx_pf_device.h"
#include "lio_image.h"
#include "lio_main.h"
#include "lio_rxtx.h"
#include "lio_ioctl.h"

#define LIO_OFF_PAUSE	0
#define LIO_RX_PAUSE	1
#define LIO_TX_PAUSE	2

#define LIO_REGDUMP_LEN		4096
#define LIO_REGDUMP_LEN_23XX	49248

#define LIO_REGDUMP_LEN_XXXX	LIO_REGDUMP_LEN_23XX

#define LIO_USE_ADAPTIVE_RX_COALESCE		1
#define LIO_USE_ADAPTIVE_TX_COALESCE		2
#define LIO_RX_COALESCE_USECS			3
#define LIO_RX_MAX_COALESCED_FRAMES		4
#define LIO_TX_MAX_COALESCED_FRAMES		8
#define LIO_PKT_RATE_LOW			12
#define LIO_RX_COALESCE_USECS_LOW		13
#define LIO_RX_MAX_COALESCED_FRAMES_LOW		14
#define LIO_TX_MAX_COALESCED_FRAMES_LOW		16
#define LIO_PKT_RATE_HIGH			17
#define LIO_RX_COALESCE_USECS_HIGH		18
#define LIO_RX_MAX_COALESCED_FRAMES_HIGH	19
#define LIO_TX_MAX_COALESCED_FRAMES_HIGH	21
#define LIO_RATE_SAMPLE_INTERVAL		22

#define LIO_SET_RING_RX				1
#define LIO_SET_RING_TX				2

static int	lio_get_eeprom(SYSCTL_HANDLER_ARGS);
static int	lio_get_set_pauseparam(SYSCTL_HANDLER_ARGS);
static int	lio_get_regs(SYSCTL_HANDLER_ARGS);
static int	lio_cn23xx_pf_read_csr_reg(char *s, struct octeon_device *oct);
static int	lio_get_set_fwmsglevel(SYSCTL_HANDLER_ARGS);
static int	lio_set_stats_interval(SYSCTL_HANDLER_ARGS);
static void	lio_get_fw_stats(void *arg);
static int	lio_get_set_intr_coalesce(SYSCTL_HANDLER_ARGS);
static int	lio_get_intrmod_cfg(struct lio *lio,
				    struct octeon_intrmod_cfg *intr_cfg);
static int	lio_get_ringparam(SYSCTL_HANDLER_ARGS);
static int	lio_set_ringparam(SYSCTL_HANDLER_ARGS);
static int	lio_get_channels(SYSCTL_HANDLER_ARGS);
static int	lio_set_channels(SYSCTL_HANDLER_ARGS);
static int	lio_irq_reallocate_irqs(struct octeon_device *oct,
					uint32_t num_ioqs);

struct lio_intrmod_context {
	int	octeon_id;
	volatile int cond;
	int	status;
};

struct lio_intrmod_resp {
	uint64_t	rh;
	struct octeon_intrmod_cfg intrmod;
	uint64_t	status;
};

static int
lio_send_queue_count_update(struct ifnet *ifp, uint32_t num_queues)
{
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int ret = 0;

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = LIO_CMD_QUEUE_COUNT_CTL;
	nctrl.ncmd.s.param1 = num_queues;
	nctrl.ncmd.s.param2 = num_queues;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct, "Failed to send Queue reset command (ret: 0x%x)\n",
			    ret);
		return (-1);
	}

	return (0);
}

/* Add sysctl variables to the system, one per statistic. */
void
lio_add_hw_stats(struct lio *lio)
{
	struct octeon_device	*oct_dev = lio->oct_dev;
	device_t dev = oct_dev->device;

	struct sysctl_ctx_list	*ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid	*tree = device_get_sysctl_tree(dev);
	struct sysctl_oid_list	*child = SYSCTL_CHILDREN(tree);
	struct sysctl_oid	*stat_node, *queue_node, *root_node;
	struct sysctl_oid_list	*stat_list, *queue_list, *root_list;
#define QUEUE_NAME_LEN 32
	char namebuf[QUEUE_NAME_LEN];

	callout_reset(&lio->stats_timer, lio_ms_to_ticks(lio->stats_interval),
		      lio_get_fw_stats, lio);

	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "fwversion", CTLFLAG_RD,
			  oct_dev->fw_info.lio_firmware_version, 0,
			  "Firmware version");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "stats_interval",
			CTLTYPE_INT | CTLFLAG_RW, lio, 0,
			lio_set_stats_interval,	"I",
			"Set Stats Updation Timer in milli seconds");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "link_state_changes",
			 CTLFLAG_RD, &lio->link_changes, "Link Change Counter");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "eeprom-dump",
			CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, lio, 0,
			lio_get_eeprom, "A", "EEPROM information");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "fc",
			CTLTYPE_INT | CTLFLAG_RW, lio, 0,
			lio_get_set_pauseparam, "I",
			"Get and set pause parameters.\n" \
			"0 - off\n" \
			"1 - rx pause\n" \
			"2 - tx pause \n" \
			"3 - rx and tx pause");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "register-dump",
			CTLTYPE_STRING | CTLFLAG_RD,
			lio, 0, lio_get_regs, "A",
			"Dump registers in raw format");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "fwmsglevel",
			CTLTYPE_INT | CTLFLAG_RW, lio, 0,
			lio_get_set_fwmsglevel,
			"I", "Get or set firmware message level");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rxq_descriptors",
			CTLTYPE_INT | CTLFLAG_RW, lio, LIO_SET_RING_RX,
			lio_set_ringparam, "I", "Set RX ring parameter");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "txq_descriptors",
			CTLTYPE_INT | CTLFLAG_RW, lio, LIO_SET_RING_TX,
			lio_set_ringparam, "I", "Set TX ring parameter");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "max_rxq_descriptors",
			CTLTYPE_INT | CTLFLAG_RD, lio, LIO_SET_RING_RX,
			lio_get_ringparam, "I", "Max RX descriptors");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "max_txq_descriptors",
			CTLTYPE_INT | CTLFLAG_RD, lio, LIO_SET_RING_TX,
			lio_get_ringparam, "I", "Max TX descriptors");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "active_queues",
			CTLTYPE_INT | CTLFLAG_RW, lio, 0, lio_set_channels,
			"I", "Set channels information");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "max_queues",
			CTLTYPE_INT | CTLFLAG_RD, lio, 0, lio_get_channels,
			"I", "Get channels information");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_budget",
			CTLFLAG_RW, &oct_dev->tx_budget,
			0, "TX process pkt budget");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_budget",
			CTLFLAG_RW, &oct_dev->rx_budget,
			0, "RX process pkt budget");

	/* IRQ Coalescing Parameters */
	root_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "coalesce",
				    CTLFLAG_RD, NULL, "Get and Set Coalesce");

	root_list = SYSCTL_CHILDREN(root_node);

	if (lio_get_intrmod_cfg(lio, &lio->intrmod_cfg))
		lio_dev_info(oct_dev, "Coalescing driver update failed!\n");

	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "sample-interval",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_RATE_SAMPLE_INTERVAL, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "tx-frame-high",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_TX_MAX_COALESCED_FRAMES_HIGH,
			lio_get_set_intr_coalesce, "QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "rx-frame-high",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_RX_MAX_COALESCED_FRAMES_HIGH,
			lio_get_set_intr_coalesce, "QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "rx-usecs-high",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_RX_COALESCE_USECS_HIGH, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "pkt-rate-high",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_PKT_RATE_HIGH, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "tx-frame-low",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_TX_MAX_COALESCED_FRAMES_LOW,
			lio_get_set_intr_coalesce, "QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "rx-frame-low",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_RX_MAX_COALESCED_FRAMES_LOW,
			lio_get_set_intr_coalesce, "QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "rx-usecs-low",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_RX_COALESCE_USECS_LOW, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "pkt-rate-low",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_PKT_RATE_LOW, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "tx-frames",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_TX_MAX_COALESCED_FRAMES, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "rx-frames",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_RX_MAX_COALESCED_FRAMES, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "rx-usecs",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_RX_COALESCE_USECS, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "adaptive-tx",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_USE_ADAPTIVE_TX_COALESCE, lio_get_set_intr_coalesce,
			"QU", NULL);
	SYSCTL_ADD_PROC(ctx, root_list, OID_AUTO, "adaptive-rx",
			CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE, lio,
			LIO_USE_ADAPTIVE_RX_COALESCE, lio_get_set_intr_coalesce,
			"QU", NULL);

	/* Root Node of all the Stats */
	root_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
				    NULL, "Root Node of all the Stats");
	root_list = SYSCTL_CHILDREN(root_node);

	/* Firmware Tx Stats */
	stat_node = SYSCTL_ADD_NODE(ctx, root_list, OID_AUTO, "fwtx",CTLFLAG_RD,
				    NULL, "Firmware Tx Statistics");
	stat_list = SYSCTL_CHILDREN(stat_node);

	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_total_sent", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_total_sent,
			 "Firmware Total Packets Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_total_fwd", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_total_fwd,
			 "Firmware Total Packets Forwarded");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_total_fwd_bytes",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_total_fwd_bytes,
			 "Firmware Total Bytes Forwarded");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_err_pko", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_err_pko,
			 "Firmware Tx PKO Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_err_pki", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_err_pki,
			 "Firmware Tx PKI Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_err_link", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_err_link,
			 "Firmware Tx Link Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_err_drop", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_err_drop,
			 "Firmware Tx Packets Dropped");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "fw_tso", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_tso,
			 "Firmware Tx TSO");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_tso_packets", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_tso_fwd,
			 "Firmware Tx TSO Packets");
	//SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_tso_err", CTLFLAG_RD,
			   //&oct_dev->link_stats.fromhost.fw_tso_err,
			   //"Firmware Tx TSO Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "tx_vxlan", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fw_tx_vxlan,
			 "Firmware Tx VXLAN");

	/* MAC Tx Stats */
	stat_node = SYSCTL_ADD_NODE(ctx, root_list, OID_AUTO, "mactx",
				    CTLFLAG_RD, NULL, "MAC Tx Statistics");
	stat_list = SYSCTL_CHILDREN(stat_node);

	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_total_pkts",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.total_pkts_sent,
			 "Link-Level Total Packets Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_total_bytes",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.total_bytes_sent,
			 "Link-Level Total Bytes Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_mcast_pkts",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.mcast_pkts_sent,
			 "Link-Level Multicast Packets Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_bcast_pkts",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.bcast_pkts_sent,
			 "Link-Level Broadcast Packets Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_ctl_packets",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.ctl_sent,
			 "Link-Level Control Packets Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_total_collisions",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.total_collisions,
			 "Link-Level Tx Total Collisions");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_one_collision",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.one_collision_sent,
			 "Link-Level Tx One Collision Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_multi_collison",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.multi_collision_sent,
			 "Link-Level Tx Multi-Collision Sent");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_max_collision_fail",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.max_collision_fail,
			 "Link-Level Tx Max Collision Failed");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_max_deferal_fail",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.max_deferral_fail,
			 "Link-Level Tx Max Deferral Failed");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_fifo_err",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.fifo_err,
			 "Link-Level Tx FIFO Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_tx_runts", CTLFLAG_RD,
			 &oct_dev->link_stats.fromhost.runts,
			 "Link-Level Tx Runts");

	/* Firmware Rx Stats */
	stat_node = SYSCTL_ADD_NODE(ctx, root_list, OID_AUTO, "fwrx",
				    CTLFLAG_RD, NULL, "Firmware Rx Statistics");
	stat_list = SYSCTL_CHILDREN(stat_node);

	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_total_rcvd", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_total_rcvd,
			 "Firmware Total Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_total_fwd", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_total_fwd,
			 "Firmware Total Packets Forwarded");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_jabber_err", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.jabber_err,
			 "Firmware Rx Jabber Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_l2_err", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.l2_err,
			 "Firmware Rx L2 Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_frame_err", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.frame_err,
			 "Firmware Rx Frame Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_err_pko", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_err_pko,
			 "Firmware Rx PKO Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_err_link", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_err_link,
			 "Firmware Rx Link Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_err_drop", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_err_drop,
			 "Firmware Rx Dropped");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_vxlan", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_rx_vxlan,
			 "Firmware Rx VXLAN");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_vxlan_err", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_rx_vxlan_err,
			 "Firmware Rx VXLAN Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_lro_pkts", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_lro_pkts,
			 "Firmware Rx LRO Packets");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_lro_bytes", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_lro_octs,
			 "Firmware Rx LRO Bytes");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_total_lro", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_total_lro,
			 "Firmware Rx Total LRO");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_lro_aborts", CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_lro_aborts,
			 "Firmware Rx LRO Aborts");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_lro_aborts_port",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_lro_aborts_port,
			 "Firmware Rx LRO Aborts Port");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_lro_aborts_seq",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_lro_aborts_seq,
			 "Firmware Rx LRO Aborts Sequence");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_lro_aborts_tsval",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_lro_aborts_tsval,
			 "Firmware Rx LRO Aborts tsval");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_lro_aborts_timer",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fw_lro_aborts_timer,
			 "Firmware Rx LRO Aborts Timer");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "rx_fwd_rate",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fwd_rate,
			 "Firmware Rx Packets Forward Rate");
	/* MAC Rx Stats */
	stat_node = SYSCTL_ADD_NODE(ctx, root_list, OID_AUTO, "macrx",
				    CTLFLAG_RD, NULL, "MAC Rx Statistics");
	stat_list = SYSCTL_CHILDREN(stat_node);

	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_total_rcvd",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.total_rcvd,
			 "Link-Level Total Packets Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_bytes",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.bytes_rcvd,
			 "Link-Level Total Bytes Received");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_total_bcst",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.total_bcst,
			 "Link-Level Total Broadcast");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_total_mcst",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.total_mcst,
			 "Link-Level Total Multicast");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_runts",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.runts,
			 "Link-Level Rx Runts");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_ctl_packets",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.ctl_rcvd,
			 "Link-Level Rx Control Packets");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_fifo_err",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fifo_err,
			 "Link-Level Rx FIFO Errors");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_dma_drop",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.dmac_drop,
			 "Link-Level Rx DMA Dropped");
	SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, "mac_rx_fcs_err",
			 CTLFLAG_RD,
			 &oct_dev->link_stats.fromwire.fcs_err,
			 "Link-Level Rx FCS Errors");

	/* TX */
	for (int i = 0; i < oct_dev->num_iqs; i++) {
		if (!(oct_dev->io_qmask.iq & BIT_ULL(i)))
			continue;

		snprintf(namebuf, QUEUE_NAME_LEN, "tx-%d", i);
		queue_node = SYSCTL_ADD_NODE(ctx, root_list, OID_AUTO, namebuf,
					     CTLFLAG_RD, NULL, "Input Queue Name");
		queue_list = SYSCTL_CHILDREN(queue_node);

		/* packets to network port */
		/* # of packets tx to network */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "packets",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.tx_done,
				 "Number of Packets Tx to Network");
		/* # of bytes tx to network */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "bytes",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.tx_tot_bytes,
				 "Number of Bytes Tx to Network");
		/* # of packets dropped */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "dropped",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.tx_dropped,
				 "Number of Tx Packets Dropped");
		/* # of tx fails due to queue full */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "iq_busy",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.tx_iq_busy,
				 "Number of Tx Fails Due to Queue Full");
		/* scatter gather entries sent */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "sgentry_sent",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.sgentry_sent,
				 "Scatter Gather Entries Sent");

		/* instruction to firmware: data and control */
		/* # of instructions to the queue */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "fw_instr_posted",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.instr_posted,
				 "Number of Instructions to The Queue");
		/* # of instructions processed */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO,
				 "fw_instr_processed", CTLFLAG_RD,
			      &oct_dev->instr_queue[i]->stats.instr_processed,
				 "Number of Instructions Processed");
		/* # of instructions could not be processed */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "fw_instr_dropped",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.instr_dropped,
				 "Number of Instructions Dropped");
		/* bytes sent through the queue */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "fw_bytes_sent",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.bytes_sent,
				 "Bytes Sent Through The Queue");
		/* tso request */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "tso",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.tx_gso,
				 "TSO Request");
		/* vxlan request */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "vxlan",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.tx_vxlan,
				 "VXLAN Request");
		/* txq restart */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "txq_restart",
				 CTLFLAG_RD,
				 &oct_dev->instr_queue[i]->stats.tx_restart,
				 "TxQ Restart");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "tx_dmamap_fail",
				 CTLFLAG_RD,
			       &oct_dev->instr_queue[i]->stats.tx_dmamap_fail,
				 "TxQ DMA Map Failed");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO,
				 "mbuf_defrag_failed", CTLFLAG_RD,
			   &oct_dev->instr_queue[i]->stats.mbuf_defrag_failed,
				 "TxQ defrag Failed");
	}

	/* RX */
	for (int i = 0; i < oct_dev->num_oqs; i++) {
		if (!(oct_dev->io_qmask.oq & BIT_ULL(i)))
			continue;

		snprintf(namebuf, QUEUE_NAME_LEN, "rx-%d", i);
		queue_node = SYSCTL_ADD_NODE(ctx, root_list, OID_AUTO, namebuf,
					     CTLFLAG_RD, NULL,
					     "Output Queue Name");
		queue_list = SYSCTL_CHILDREN(queue_node);

		/* packets send to TCP/IP network stack */
		/* # of packets to network stack */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "packets",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.rx_pkts_received,
				 "Number of Packets to Network Stack");
		/* # of bytes to network stack */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "bytes",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.rx_bytes_received,
				 "Number of Bytes to Network Stack");
		/* # of packets dropped */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "dropped_nomem",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.dropped_nomem,
				 "Packets Dropped Due to No Memory");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "dropped_toomany",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.dropped_toomany,
				 "Packets dropped, Too Many Pkts to Process");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "fw_dropped",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.rx_dropped,
				"Packets Dropped due to Receive path failures");
		/* control and data path */
		/* # packets  sent to stack from this queue. */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "fw_pkts_received",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.pkts_received,
				 "Number of Packets Received");
		/* # Bytes sent to stack from this queue. */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "fw_bytes_received",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.bytes_received,
				 "Number of Bytes Received");
		/* Packets dropped due to no dispatch function. */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO,
				 "fw_dropped_nodispatch", CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.dropped_nodispatch,
				 "Packets Dropped, No Dispatch Function");
		/* Rx VXLAN */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "vxlan",
				 CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.rx_vxlan,
				 "Rx VXLAN");
		/* # failures of lio_recv_buffer_alloc */
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO,
				 "buffer_alloc_failure", CTLFLAG_RD,
				 &oct_dev->droq[i]->stats.rx_alloc_failure,
			       "Number of Failures of lio_recv_buffer_alloc");
	}
}

static int
lio_get_eeprom(SYSCTL_HANDLER_ARGS)
{
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct_dev = lio->oct_dev;
	struct lio_board_info	*board_info;
	char	buf[512];

	board_info = (struct lio_board_info *)(&oct_dev->boardinfo);
	if (oct_dev->uboot_len == 0)
		sprintf(buf, "boardname:%s serialnum:%s maj:%lld min:%lld",
			board_info->name, board_info->serial_number,
			LIO_CAST64(board_info->major),
		       	LIO_CAST64(board_info->minor));
	else {
		sprintf(buf, "boardname:%s serialnum:%s maj:%lld min:%lld\n%s",
			board_info->name, board_info->serial_number,
			LIO_CAST64(board_info->major),
		        LIO_CAST64(board_info->minor),
			&oct_dev->uboot_version[oct_dev->uboot_sidx]);
	}

	return (sysctl_handle_string(oidp, buf, strlen(buf), req));
}

/*
 * Get and set pause parameters or flow control using sysctl:
 * 0 - off
 * 1 - rx pause
 * 2 - tx pause
 * 3 - full
 */
static int
lio_get_set_pauseparam(SYSCTL_HANDLER_ARGS)
{
	/* Notes: Not supporting any auto negotiation in these drivers. */
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct = lio->oct_dev;
	struct octeon_link_info	*linfo = &lio->linfo;

	int	err, new_pause = LIO_OFF_PAUSE, old_pause = LIO_OFF_PAUSE;
	int	ret = 0;

	if (oct->chip_id != LIO_CN23XX_PF_VID)
		return (EINVAL);

	if (oct->rx_pause)
		old_pause |= LIO_RX_PAUSE;

	if (oct->tx_pause)
		old_pause |= LIO_TX_PAUSE;

	new_pause = old_pause;
	err = sysctl_handle_int(oidp, &new_pause, 0, req);

	if ((err) || (req->newptr == NULL))
		return (err);

	if (old_pause == new_pause)
		return (0);

	if (linfo->link.s.duplex == 0) {
		/* no flow control for half duplex */
		if (new_pause)
			return (EINVAL);
	}

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = LIO_CMD_SET_FLOW_CTL;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	if (new_pause & LIO_RX_PAUSE) {
		/* enable rx pause */
		nctrl.ncmd.s.param1 = 1;
	} else {
		/* disable rx pause */
		nctrl.ncmd.s.param1 = 0;
	}

	if (new_pause & LIO_TX_PAUSE) {
		/* enable tx pause */
		nctrl.ncmd.s.param2 = 1;
	} else {
		/* disable tx pause */
		nctrl.ncmd.s.param2 = 0;
	}

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct, "Failed to set pause parameter\n");
		return (EINVAL);
	}

	oct->rx_pause = new_pause & LIO_RX_PAUSE;
	oct->tx_pause = new_pause & LIO_TX_PAUSE;

	return (0);
}

/*  Return register dump user app.  */
static int
lio_get_regs(SYSCTL_HANDLER_ARGS)
{
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct = lio->oct_dev;
	struct ifnet		*ifp = lio->ifp;
	char	*regbuf;
	int	error = EINVAL, len = 0;

	if (!(if_getflags(ifp) & IFF_DEBUG)) {
		char debug_info[30] = "Debugging is disabled";

		return (sysctl_handle_string(oidp, debug_info,
					     strlen(debug_info), req));
	}
	regbuf = malloc(sizeof(char) * LIO_REGDUMP_LEN_XXXX, M_DEVBUF,
			M_WAITOK | M_ZERO);

	if (regbuf == NULL)
		return (error);

	switch (oct->chip_id) {
	case LIO_CN23XX_PF_VID:
		len += lio_cn23xx_pf_read_csr_reg(regbuf, oct);
		break;
	default:
		len += sprintf(regbuf, "%s Unknown chipid: %d\n",
			       __func__, oct->chip_id);
	}

	error = sysctl_handle_string(oidp, regbuf, len, req);
	free(regbuf, M_DEVBUF);

	return (error);
}

static int
lio_cn23xx_pf_read_csr_reg(char *s, struct octeon_device *oct)
{
	uint32_t	reg;
	int	i, len = 0;
	uint8_t	pf_num = oct->pf_num;

	/* PCI  Window Registers */

	len += sprintf(s + len, "\t Octeon CSR Registers\n\n");

	/* 0x29030 or 0x29040 */
	reg = LIO_CN23XX_SLI_PKT_MAC_RINFO64(oct->pcie_port, oct->pf_num);
	len += sprintf(s + len, "[%08x] (SLI_PKT_MAC%d_PF%d_RINFO): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x27080 or 0x27090 */
	reg = LIO_CN23XX_SLI_MAC_PF_INT_ENB64(oct->pcie_port, oct->pf_num);
	len += sprintf(s + len, "[%08x] (SLI_MAC%d_PF%d_INT_ENB): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x27000 or 0x27010 */
	reg = LIO_CN23XX_SLI_MAC_PF_INT_SUM64(oct->pcie_port, oct->pf_num);
	len += sprintf(s + len, "[%08x] (SLI_MAC%d_PF%d_INT_SUM): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x29120 */
	reg = 0x29120;
	len += sprintf(s + len, "[%08x] (SLI_PKT_MEM_CTL): %016llx\n", reg,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x27300 */
	reg = 0x27300 + oct->pcie_port * LIO_CN23XX_MAC_INT_OFFSET +
	    (oct->pf_num) * LIO_CN23XX_PF_INT_OFFSET;
	len += sprintf(s + len, "[%08x] (SLI_MAC%d_PF%d_PKT_VF_INT): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x27200 */
	reg = 0x27200 + oct->pcie_port * LIO_CN23XX_MAC_INT_OFFSET +
	    (oct->pf_num) * LIO_CN23XX_PF_INT_OFFSET;
	len += sprintf(s + len, "[%08x] (SLI_MAC%d_PF%d_PP_VF_INT): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 29130 */
	reg = LIO_CN23XX_SLI_PKT_CNT_INT;
	len += sprintf(s + len, "[%08x] (SLI_PKT_CNT_INT): %016llx\n", reg,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x29140 */
	reg = LIO_CN23XX_SLI_PKT_TIME_INT;
	len += sprintf(s + len, "[%08x] (SLI_PKT_TIME_INT): %016llx\n", reg,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x29160 */
	reg = 0x29160;
	len += sprintf(s + len, "[%08x] (SLI_PKT_INT): %016llx\n", reg,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x29180 */
	reg = LIO_CN23XX_SLI_OQ_WMARK;
	len += sprintf(s + len, "[%08x] (SLI_PKT_OUTPUT_WMARK): %016llx\n",
		       reg, LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x291E0 */
	reg = LIO_CN23XX_SLI_PKT_IOQ_RING_RST;
	len += sprintf(s + len, "[%08x] (SLI_PKT_RING_RST): %016llx\n", reg,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x29210 */
	reg = LIO_CN23XX_SLI_GBL_CONTROL;
	len += sprintf(s + len, "[%08x] (SLI_PKT_GBL_CONTROL): %016llx\n", reg,
		       LIO_CAST64(lio_read_csr64(oct, reg)));

	/* 0x29220 */
	reg = 0x29220;
	len += sprintf(s + len, "[%08x] (SLI_PKT_BIST_STATUS): %016llx\n",
		       reg, LIO_CAST64(lio_read_csr64(oct, reg)));

	/* PF only */
	if (pf_num == 0) {
		/* 0x29260 */
		reg = LIO_CN23XX_SLI_OUT_BP_EN_W1S;
		len += sprintf(s + len, "[%08x] (SLI_PKT_OUT_BP_EN_W1S):  %016llx\n",
			       reg, LIO_CAST64(lio_read_csr64(oct, reg)));
	} else if (pf_num == 1) {
		/* 0x29270 */
		reg = LIO_CN23XX_SLI_OUT_BP_EN2_W1S;
		len += sprintf(s + len, "[%08x] (SLI_PKT_OUT_BP_EN2_W1S): %016llx\n",
			       reg, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_OQ_BUFF_INFO_SIZE(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_OUT_SIZE): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10040 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_INPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10080 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_OQ_PKTS_CREDIT(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_SLIST_BAOFF_DBELL): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10090 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_OQ_SIZE(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_SLIST_FIFO_RSIZE): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10050 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_OQ_PKT_CONTROL(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d__OUTPUT_CONTROL): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10070 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_OQ_BASE_ADDR64(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_SLIST_BADDR): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x100a0 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_OQ_PKT_INT_LEVELS(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_INT_LEVELS): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x100b0 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_OQ_PKTS_SENT(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_CNTS): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x100c0 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_OUTPUT_QUEUES; i++) {
		reg = 0x100c0 + i * LIO_CN23XX_OQ_OFFSET;
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_ERROR_INFO): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10000 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_INPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_IQ_PKT_CONTROL64(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_INPUT_CONTROL): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10010 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_INPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_IQ_BASE_ADDR64(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_INSTR_BADDR): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10020 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_INPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_IQ_DOORBELL(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_INSTR_BAOFF_DBELL): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10030 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_INPUT_QUEUES; i++) {
		reg = LIO_CN23XX_SLI_IQ_SIZE(i);
		len += sprintf(s + len, "[%08x] (SLI_PKT%d_INSTR_FIFO_RSIZE): %016llx\n",
			       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));
	}

	/* 0x10040 */
	for (i = 0; i < LIO_CN23XX_PF_MAX_INPUT_QUEUES; i++)
		reg = LIO_CN23XX_SLI_IQ_INSTR_COUNT64(i);
	len += sprintf(s + len, "[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
		       reg, i, LIO_CAST64(lio_read_csr64(oct, reg)));

	return (len);
}

static int
lio_get_ringparam(SYSCTL_HANDLER_ARGS)
{
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct = lio->oct_dev;
	uint32_t		rx_max_pending = 0, tx_max_pending = 0;
	int	err;

	if (LIO_CN23XX_PF(oct)) {
		tx_max_pending = LIO_CN23XX_MAX_IQ_DESCRIPTORS;
		rx_max_pending = LIO_CN23XX_MAX_OQ_DESCRIPTORS;
	}

	switch (arg2) {
	case LIO_SET_RING_RX:
		err = sysctl_handle_int(oidp, &rx_max_pending, 0, req);
		break;
	case LIO_SET_RING_TX:
		err = sysctl_handle_int(oidp, &tx_max_pending, 0, req);
		break;
	}

	return (err);
}

static int
lio_reset_queues(struct ifnet *ifp, uint32_t num_qs)
{
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int	i, update = 0;

	if (lio_wait_for_pending_requests(oct))
		lio_dev_err(oct, "There were pending requests\n");

	if (lio_wait_for_instr_fetch(oct))
		lio_dev_err(oct, "IQ had pending instructions\n");


	/*
	 * Disable the input and output queues now. No more packets will
	 * arrive from Octeon.
	 */
	oct->fn_list.disable_io_queues(oct);

	if (num_qs != oct->num_iqs)
		update = 1;

	for (i = 0; i < LIO_MAX_OUTPUT_QUEUES(oct); i++) {
		if (!(oct->io_qmask.oq & BIT_ULL(i)))
			continue;

		lio_delete_droq(oct, i);
	}

	for (i = 0; i < LIO_MAX_INSTR_QUEUES(oct); i++) {
		if (!(oct->io_qmask.iq & BIT_ULL(i)))
			continue;

		lio_delete_instr_queue(oct, i);
	}

	if (oct->fn_list.setup_device_regs(oct)) {
		lio_dev_err(oct, "Failed to configure device registers\n");
		return (-1);
	}

	if (lio_setup_io_queues(oct, 0, num_qs, num_qs)) {
		lio_dev_err(oct, "IO queues initialization failed\n");
		return (-1);
	}

	if (update && lio_send_queue_count_update(ifp, num_qs))
		return (-1);

	return (0);
}

static int
lio_set_ringparam(SYSCTL_HANDLER_ARGS)
{
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct = lio->oct_dev;
	uint32_t		rx_count, rx_count_old, tx_count, tx_count_old;
	int	err, stopped = 0;

	if (!LIO_CN23XX_PF(oct))
		return (EINVAL);

	switch (arg2) {
	case LIO_SET_RING_RX:
		rx_count = rx_count_old = oct->droq[0]->max_count;
		err = sysctl_handle_int(oidp, &rx_count, 0, req);

		if ((err) || (req->newptr == NULL))
			return (err);

		rx_count = min(max(rx_count, LIO_CN23XX_MIN_OQ_DESCRIPTORS),
			       LIO_CN23XX_MAX_OQ_DESCRIPTORS);

		if (rx_count == rx_count_old)
			return (0);

		lio_ifstate_set(lio, LIO_IFSTATE_RESETTING);

		if (if_getdrvflags(lio->ifp) & IFF_DRV_RUNNING) {
			lio_stop(lio->ifp);
			stopped = 1;
		}

		/* Change RX DESCS  count */
		LIO_SET_NUM_RX_DESCS_NIC_IF(lio_get_conf(oct),
					    lio->ifidx, rx_count);
		break;
	case LIO_SET_RING_TX:
		tx_count = tx_count_old = oct->instr_queue[0]->max_count;
		err = sysctl_handle_int(oidp, &tx_count, 0, req);

		if ((err) || (req->newptr == NULL))
			return (err);

		tx_count = min(max(tx_count, LIO_CN23XX_MIN_IQ_DESCRIPTORS),
			       LIO_CN23XX_MAX_IQ_DESCRIPTORS);

		if (tx_count == tx_count_old)
			return (0);

		lio_ifstate_set(lio, LIO_IFSTATE_RESETTING);

		if (if_getdrvflags(lio->ifp) & IFF_DRV_RUNNING) {
			lio_stop(lio->ifp);
			stopped = 1;
		}

		/* Change TX DESCS  count */
		LIO_SET_NUM_TX_DESCS_NIC_IF(lio_get_conf(oct),
					    lio->ifidx, tx_count);
		break;
	}

	if (lio_reset_queues(lio->ifp, lio->linfo.num_txpciq))
		goto err_lio_reset_queues;

	lio_irq_reallocate_irqs(oct, lio->linfo.num_txpciq);
	if (stopped)
		lio_open(lio);

	lio_ifstate_reset(lio, LIO_IFSTATE_RESETTING);

	return (0);

err_lio_reset_queues:
	if (arg2 == LIO_SET_RING_RX && rx_count != rx_count_old)
		LIO_SET_NUM_RX_DESCS_NIC_IF(lio_get_conf(oct), lio->ifidx,
					    rx_count_old);

	if (arg2 == LIO_SET_RING_TX && tx_count != tx_count_old)
		LIO_SET_NUM_TX_DESCS_NIC_IF(lio_get_conf(oct), lio->ifidx,
					    tx_count_old);

	return (EINVAL);
}

static int
lio_get_channels(SYSCTL_HANDLER_ARGS)
{
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct = lio->oct_dev;
	uint32_t	max_combined = 0;

		if (LIO_CN23XX_PF(oct))
			max_combined = lio->linfo.num_txpciq;
	return (sysctl_handle_int(oidp, &max_combined, 0, req));
}

static int
lio_irq_reallocate_irqs(struct octeon_device *oct, uint32_t num_ioqs)
{
	int	i, num_msix_irqs = 0;

	if (!oct->msix_on)
		return (0);

	/*
	 * Disable the input and output queues now. No more packets will
	 * arrive from Octeon.
	 */
	oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

	if (oct->msix_on) {
		if (LIO_CN23XX_PF(oct))
			num_msix_irqs = oct->num_msix_irqs - 1;

		for (i = 0; i < num_msix_irqs; i++) {
			if (oct->ioq_vector[i].tag != NULL) {
				bus_teardown_intr(oct->device,
						  oct->ioq_vector[i].msix_res,
						  oct->ioq_vector[i].tag);
				oct->ioq_vector[i].tag = NULL;
			}

			if (oct->ioq_vector[i].msix_res != NULL) {
				bus_release_resource(oct->device, SYS_RES_IRQ,
						     oct->ioq_vector[i].vector,
						 oct->ioq_vector[i].msix_res);
				oct->ioq_vector[i].msix_res = NULL;
			}
		}


		if (oct->tag != NULL) {
			bus_teardown_intr(oct->device, oct->msix_res, oct->tag);
			oct->tag = NULL;
		}

		if (oct->msix_res != NULL) {
			bus_release_resource(oct->device, SYS_RES_IRQ,
					     oct->aux_vector,
					     oct->msix_res);
			oct->msix_res = NULL;
		}

		pci_release_msi(oct->device);

	}

	if (lio_setup_interrupt(oct, num_ioqs)) {
		lio_dev_info(oct, "Setup interuupt failed\n");
		return (1);
	}

	/* Enable Octeon device interrupts */
	oct->fn_list.enable_interrupt(oct, OCTEON_ALL_INTR);

	return (0);
}

static int
lio_set_channels(SYSCTL_HANDLER_ARGS)
{
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct = lio->oct_dev;
	uint32_t		combined_count, max_combined;
	int	err, stopped = 0;

	if (strcmp(oct->fw_info.lio_firmware_version, "1.6.1") < 0) {
		lio_dev_err(oct,
			    "Minimum firmware version required is 1.6.1\n");
		return (EINVAL);
	}

	combined_count = oct->num_iqs;
	err = sysctl_handle_int(oidp, &combined_count, 0, req);

	if ((err) || (req->newptr == NULL))
		return (err);

	if (!combined_count)
		return (EINVAL);

	if (LIO_CN23XX_PF(oct)) {
		max_combined = lio->linfo.num_txpciq;
	} else {
		return (EINVAL);
	}

	if ((combined_count > max_combined) || (combined_count < 1))
		return (EINVAL);

	if (combined_count == oct->num_iqs)
		return (0);

	lio_ifstate_set(lio, LIO_IFSTATE_RESETTING);

	if (if_getdrvflags(lio->ifp) & IFF_DRV_RUNNING) {
		lio_stop(lio->ifp);
		stopped = 1;
	}

	if (lio_reset_queues(lio->ifp, combined_count))
		return (EINVAL);

	lio_irq_reallocate_irqs(oct, combined_count);
	if (stopped)
		lio_open(lio);

	lio_ifstate_reset(lio, LIO_IFSTATE_RESETTING);

	return (0);
}


static int
lio_get_set_fwmsglevel(SYSCTL_HANDLER_ARGS)
{
	struct lio	*lio = (struct lio *)arg1;
	struct ifnet	*ifp = lio->ifp;
	int	err, new_msglvl = 0, old_msglvl = 0;

	if (lio_ifstate_check(lio, LIO_IFSTATE_RESETTING))
		return (ENXIO);

	old_msglvl = new_msglvl = lio->msg_enable;
	err = sysctl_handle_int(oidp, &new_msglvl, 0, req);

	if ((err) || (req->newptr == NULL))
		return (err);

	if (old_msglvl == new_msglvl)
		return (0);

	if (new_msglvl ^ lio->msg_enable) {
		if (new_msglvl)
			err = lio_set_feature(ifp, LIO_CMD_VERBOSE_ENABLE, 0);
		else
			err = lio_set_feature(ifp, LIO_CMD_VERBOSE_DISABLE, 0);
	}

	lio->msg_enable = new_msglvl;

	return ((err) ? EINVAL : 0);
}

static int
lio_set_stats_interval(SYSCTL_HANDLER_ARGS)
{
	struct lio	*lio = (struct lio *)arg1;
	int	err, new_time = 0, old_time = 0;

	old_time = new_time = lio->stats_interval;
	err = sysctl_handle_int(oidp, &new_time, 0, req);

	if ((err) || (req->newptr == NULL))
		return (err);

	if (old_time == new_time)
		return (0);

	lio->stats_interval = new_time;

	return (0);
}

static void
lio_fw_stats_callback(struct octeon_device *oct_dev, uint32_t status, void *ptr)
{
	struct lio_soft_command	*sc = (struct lio_soft_command *)ptr;
	struct lio_fw_stats_resp *resp =
		(struct lio_fw_stats_resp *)sc->virtrptr;
	struct octeon_rx_stats	*rsp_rstats = &resp->stats.fromwire;
	struct octeon_tx_stats	*rsp_tstats = &resp->stats.fromhost;
	struct octeon_rx_stats	*rstats = &oct_dev->link_stats.fromwire;
	struct octeon_tx_stats	*tstats = &oct_dev->link_stats.fromhost;
	struct ifnet		*ifp = oct_dev->props.ifp;
	struct lio		*lio = if_getsoftc(ifp);

	if ((status != LIO_REQUEST_TIMEOUT) && !resp->status) {
		lio_swap_8B_data((uint64_t *)&resp->stats,
				 (sizeof(struct octeon_link_stats)) >> 3);

		/* RX link-level stats */
		rstats->total_rcvd = rsp_rstats->total_rcvd;
		rstats->bytes_rcvd = rsp_rstats->bytes_rcvd;
		rstats->total_bcst = rsp_rstats->total_bcst;
		rstats->total_mcst = rsp_rstats->total_mcst;
		rstats->runts = rsp_rstats->runts;
		rstats->ctl_rcvd = rsp_rstats->ctl_rcvd;
		/* Accounts for over/under-run of buffers */
		rstats->fifo_err = rsp_rstats->fifo_err;
		rstats->dmac_drop = rsp_rstats->dmac_drop;
		rstats->fcs_err = rsp_rstats->fcs_err;
		rstats->jabber_err = rsp_rstats->jabber_err;
		rstats->l2_err = rsp_rstats->l2_err;
		rstats->frame_err = rsp_rstats->frame_err;

		/* RX firmware stats */
		rstats->fw_total_rcvd = rsp_rstats->fw_total_rcvd;
		rstats->fw_total_fwd = rsp_rstats->fw_total_fwd;
		rstats->fw_err_pko = rsp_rstats->fw_err_pko;
		rstats->fw_err_link = rsp_rstats->fw_err_link;
		rstats->fw_err_drop = rsp_rstats->fw_err_drop;
		rstats->fw_rx_vxlan = rsp_rstats->fw_rx_vxlan;
		rstats->fw_rx_vxlan_err = rsp_rstats->fw_rx_vxlan_err;

		/* Number of packets that are LROed      */
		rstats->fw_lro_pkts = rsp_rstats->fw_lro_pkts;
		/* Number of octets that are LROed       */
		rstats->fw_lro_octs = rsp_rstats->fw_lro_octs;
		/* Number of LRO packets formed          */
		rstats->fw_total_lro = rsp_rstats->fw_total_lro;
		/* Number of times lRO of packet aborted */
		rstats->fw_lro_aborts = rsp_rstats->fw_lro_aborts;
		rstats->fw_lro_aborts_port = rsp_rstats->fw_lro_aborts_port;
		rstats->fw_lro_aborts_seq = rsp_rstats->fw_lro_aborts_seq;
		rstats->fw_lro_aborts_tsval = rsp_rstats->fw_lro_aborts_tsval;
		rstats->fw_lro_aborts_timer = rsp_rstats->fw_lro_aborts_timer;
		/* intrmod: packet forward rate */
		rstats->fwd_rate = rsp_rstats->fwd_rate;

		/* TX link-level stats */
		tstats->total_pkts_sent = rsp_tstats->total_pkts_sent;
		tstats->total_bytes_sent = rsp_tstats->total_bytes_sent;
		tstats->mcast_pkts_sent = rsp_tstats->mcast_pkts_sent;
		tstats->bcast_pkts_sent = rsp_tstats->bcast_pkts_sent;
		tstats->ctl_sent = rsp_tstats->ctl_sent;
		/* Packets sent after one collision */
		tstats->one_collision_sent = rsp_tstats->one_collision_sent;
		/* Packets sent after multiple collision */
		tstats->multi_collision_sent = rsp_tstats->multi_collision_sent;
		/* Packets not sent due to max collisions */
		tstats->max_collision_fail = rsp_tstats->max_collision_fail;
		/* Packets not sent due to max deferrals */
		tstats->max_deferral_fail = rsp_tstats->max_deferral_fail;
		/* Accounts for over/under-run of buffers */
		tstats->fifo_err = rsp_tstats->fifo_err;
		tstats->runts = rsp_tstats->runts;
		/* Total number of collisions detected */
		tstats->total_collisions = rsp_tstats->total_collisions;

		/* firmware stats */
		tstats->fw_total_sent = rsp_tstats->fw_total_sent;
		tstats->fw_total_fwd = rsp_tstats->fw_total_fwd;
		tstats->fw_err_pko = rsp_tstats->fw_err_pko;
		tstats->fw_err_pki = rsp_tstats->fw_err_pki;
		tstats->fw_err_link = rsp_tstats->fw_err_link;
		tstats->fw_err_drop = rsp_tstats->fw_err_drop;
		tstats->fw_tso = rsp_tstats->fw_tso;
		tstats->fw_tso_fwd = rsp_tstats->fw_tso_fwd;
		tstats->fw_err_tso = rsp_tstats->fw_err_tso;
		tstats->fw_tx_vxlan = rsp_tstats->fw_tx_vxlan;
	}
	lio_free_soft_command(oct_dev, sc);
	callout_schedule(&lio->stats_timer,
			 lio_ms_to_ticks(lio->stats_interval));
}

/*  Configure interrupt moderation parameters */
static void
lio_get_fw_stats(void *arg)
{
	struct lio		*lio = arg;
	struct octeon_device	*oct_dev = lio->oct_dev;
	struct lio_soft_command	*sc;
	struct lio_fw_stats_resp *resp;
	int	retval;

	if (callout_pending(&lio->stats_timer) ||
	    callout_active(&lio->stats_timer) == 0)
		return;

	/* Alloc soft command */
	sc = lio_alloc_soft_command(oct_dev, 0,
				    sizeof(struct lio_fw_stats_resp), 0);

	if (sc == NULL)
		goto alloc_sc_failed;

	resp = (struct lio_fw_stats_resp *)sc->virtrptr;
	bzero(resp, sizeof(struct lio_fw_stats_resp));

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	lio_prepare_soft_command(oct_dev, sc, LIO_OPCODE_NIC,
				 LIO_OPCODE_NIC_PORT_STATS, 0, 0, 0);

	sc->callback = lio_fw_stats_callback;
	sc->callback_arg = sc;
	sc->wait_time = 500;		/* in milli seconds */

	retval = lio_send_soft_command(oct_dev, sc);
	if (retval == LIO_IQ_SEND_FAILED)
		goto send_sc_failed;

	return;

send_sc_failed:
	lio_free_soft_command(oct_dev, sc);
alloc_sc_failed:
	callout_schedule(&lio->stats_timer,
			 lio_ms_to_ticks(lio->stats_interval));
}

/* Callback function for intrmod */
static void
lio_get_intrmod_callback(struct octeon_device *oct_dev, uint32_t status,
			 void *ptr)
{
	struct lio_soft_command	*sc = (struct lio_soft_command *)ptr;
	struct ifnet		*ifp = oct_dev->props.ifp;
	struct lio		*lio = if_getsoftc(ifp);
	struct lio_intrmod_resp	*resp;

	if (status) {
		lio_dev_err(oct_dev, "Failed to get intrmod\n");
	} else {
		resp = (struct lio_intrmod_resp *)sc->virtrptr;
		lio_swap_8B_data((uint64_t *)&resp->intrmod,
				 (sizeof(struct octeon_intrmod_cfg)) / 8);
		memcpy(&lio->intrmod_cfg, &resp->intrmod,
		       sizeof(struct octeon_intrmod_cfg));
	}

	lio_free_soft_command(oct_dev, sc);
}

/*  get interrupt moderation parameters */
static int
lio_get_intrmod_cfg(struct lio *lio, struct octeon_intrmod_cfg *intr_cfg)
{
	struct lio_soft_command	*sc;
	struct lio_intrmod_resp	*resp;
	struct octeon_device	*oct_dev = lio->oct_dev;
	int	retval;

	/* Alloc soft command */
	sc = lio_alloc_soft_command(oct_dev, 0, sizeof(struct lio_intrmod_resp),
				    0);

	if (sc == NULL)
		return (ENOMEM);

	resp = (struct lio_intrmod_resp *)sc->virtrptr;
	bzero(resp, sizeof(struct lio_intrmod_resp));
	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	lio_prepare_soft_command(oct_dev, sc, LIO_OPCODE_NIC,
				 LIO_OPCODE_NIC_INTRMOD_PARAMS, 0, 0, 0);

	sc->callback = lio_get_intrmod_callback;
	sc->callback_arg = sc;
	sc->wait_time = 1000;

	retval = lio_send_soft_command(oct_dev, sc);
	if (retval == LIO_IQ_SEND_FAILED) {
		lio_free_soft_command(oct_dev, sc);
		return (EINVAL);
	}

	return (0);
}

static void
lio_set_intrmod_callback(struct octeon_device *oct_dev, uint32_t status,
			 void *ptr)
{
	struct lio_soft_command		*sc = (struct lio_soft_command *)ptr;
	struct lio_intrmod_context	*ctx;

	ctx = (struct lio_intrmod_context *)sc->ctxptr;

	ctx->status = status;

	ctx->cond = 1;

	/*
	 * This barrier is required to be sure that the response has been
	 * written fully before waking up the handler
	 */
	wmb();
}

/*  Configure interrupt moderation parameters */
static int
lio_set_intrmod_cfg(struct lio *lio, struct octeon_intrmod_cfg *intr_cfg)
{
	struct lio_soft_command		*sc;
	struct lio_intrmod_context	*ctx;
	struct octeon_intrmod_cfg	*cfg;
	struct octeon_device		*oct_dev = lio->oct_dev;
	int	retval;

	/* Alloc soft command */
	sc = lio_alloc_soft_command(oct_dev, sizeof(struct octeon_intrmod_cfg),
				    0, sizeof(struct lio_intrmod_context));

	if (sc == NULL)
		return (ENOMEM);

	ctx = (struct lio_intrmod_context *)sc->ctxptr;

	ctx->cond = 0;
	ctx->octeon_id = lio_get_device_id(oct_dev);

	cfg = (struct octeon_intrmod_cfg *)sc->virtdptr;

	memcpy(cfg, intr_cfg, sizeof(struct octeon_intrmod_cfg));
	lio_swap_8B_data((uint64_t *)cfg,
			 (sizeof(struct octeon_intrmod_cfg)) / 8);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	lio_prepare_soft_command(oct_dev, sc, LIO_OPCODE_NIC,
				 LIO_OPCODE_NIC_INTRMOD_CFG, 0, 0, 0);

	sc->callback = lio_set_intrmod_callback;
	sc->callback_arg = sc;
	sc->wait_time = 1000;

	retval = lio_send_soft_command(oct_dev, sc);
	if (retval == LIO_IQ_SEND_FAILED) {
		lio_free_soft_command(oct_dev, sc);
		return (EINVAL);
	}

	/*
	 * Sleep on a wait queue till the cond flag indicates that the
	 * response arrived or timed-out.
	 */
	lio_sleep_cond(oct_dev, &ctx->cond);

	retval = ctx->status;
	if (retval)
		lio_dev_err(oct_dev, "intrmod config failed. Status: %llx\n",
			    LIO_CAST64(retval));
	else
		lio_dev_info(oct_dev, "Rx-Adaptive Interrupt moderation enabled:%llx\n",
			     LIO_CAST64(intr_cfg->rx_enable));

	lio_free_soft_command(oct_dev, sc);

	return ((retval) ? ETIMEDOUT : 0);
}

static int
lio_intrmod_cfg_rx_intrcnt(struct lio *lio, struct octeon_intrmod_cfg *intrmod,
			   uint32_t rx_max_frames)
{
	struct octeon_device	*oct = lio->oct_dev;
	uint32_t		rx_max_coalesced_frames;

	/* Config Cnt based interrupt values */
	switch (oct->chip_id) {
	case LIO_CN23XX_PF_VID:{
			int	q_no;

			if (!rx_max_frames)
				rx_max_coalesced_frames = intrmod->rx_frames;
			else
				rx_max_coalesced_frames = rx_max_frames;

			for (q_no = 0; q_no < oct->num_oqs; q_no++) {
				q_no += oct->sriov_info.pf_srn;
				lio_write_csr64(oct,
					LIO_CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
						(lio_read_csr64(oct,
				     LIO_CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no)) &
						 (0x3fffff00000000UL)) |
						(rx_max_coalesced_frames - 1));
				/* consider setting resend bit */
			}

			intrmod->rx_frames = rx_max_coalesced_frames;
			oct->rx_max_coalesced_frames = rx_max_coalesced_frames;
			break;
		}
	default:
		return (EINVAL);
	}
	return (0);
}

static int
lio_intrmod_cfg_rx_intrtime(struct lio *lio, struct octeon_intrmod_cfg *intrmod,
			    uint32_t rx_usecs)
{
	struct octeon_device	*oct = lio->oct_dev;
	uint32_t		rx_coalesce_usecs;

	/* Config Time based interrupt values */
	switch (oct->chip_id) {
	case LIO_CN23XX_PF_VID:{
			uint64_t	time_threshold;
			int	q_no;

			if (!rx_usecs)
				rx_coalesce_usecs = intrmod->rx_usecs;
			else
				rx_coalesce_usecs = rx_usecs;

			time_threshold =
			    lio_cn23xx_pf_get_oq_ticks(oct, rx_coalesce_usecs);
			for (q_no = 0; q_no < oct->num_oqs; q_no++) {
				q_no += oct->sriov_info.pf_srn;
				lio_write_csr64(oct,
				       LIO_CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
						(intrmod->rx_frames |
					   ((uint64_t)time_threshold << 32)));
				/* consider writing to resend bit here */
			}

			intrmod->rx_usecs = rx_coalesce_usecs;
			oct->rx_coalesce_usecs = rx_coalesce_usecs;
			break;
		}
	default:
		return (EINVAL);
	}

	return (0);
}

static int
lio_intrmod_cfg_tx_intrcnt(struct lio *lio, struct octeon_intrmod_cfg *intrmod,
			   uint32_t tx_max_frames)
{
	struct octeon_device	*oct = lio->oct_dev;
	uint64_t	val;
	uint32_t	iq_intr_pkt;
	uint32_t	inst_cnt_reg;

	/* Config Cnt based interrupt values */
	switch (oct->chip_id) {
	case LIO_CN23XX_PF_VID:{
			int	q_no;

			if (!tx_max_frames)
				iq_intr_pkt = LIO_CN23XX_DEF_IQ_INTR_THRESHOLD &
				    LIO_CN23XX_PKT_IN_DONE_WMARK_MASK;
			else
				iq_intr_pkt = tx_max_frames &
				    LIO_CN23XX_PKT_IN_DONE_WMARK_MASK;
			for (q_no = 0; q_no < oct->num_iqs; q_no++) {
				inst_cnt_reg =
					(oct->instr_queue[q_no])->inst_cnt_reg;
				val = lio_read_csr64(oct, inst_cnt_reg);
				/*
				 * clear wmark and count.dont want to write
				 * count back
				 */
				val = (val & 0xFFFF000000000000ULL) |
				    ((uint64_t)(iq_intr_pkt - 1)
				     << LIO_CN23XX_PKT_IN_DONE_WMARK_BIT_POS);
				lio_write_csr64(oct, inst_cnt_reg, val);
				/* consider setting resend bit */
			}

			intrmod->tx_frames = iq_intr_pkt;
			oct->tx_max_coalesced_frames = iq_intr_pkt;
			break;
		}
	default:
		return (-EINVAL);
	}
	return (0);
}

static int
lio_get_set_intr_coalesce(SYSCTL_HANDLER_ARGS)
{
	struct lio		*lio = (struct lio *)arg1;
	struct octeon_device	*oct = lio->oct_dev;
	uint64_t	new_val = 0, old_val = 0;
	uint32_t	rx_coalesce_usecs = 0;
	uint32_t	rx_max_coalesced_frames = 0;
	uint32_t	tx_coalesce_usecs = 0;
	int		err, ret;

	switch (arg2) {
	case LIO_USE_ADAPTIVE_RX_COALESCE:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.rx_enable;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		lio->intrmod_cfg.rx_enable = new_val ? 1 : 0;
		break;

	case LIO_USE_ADAPTIVE_TX_COALESCE:
		if (lio->intrmod_cfg.tx_enable)
			new_val = old_val = lio->intrmod_cfg.tx_enable;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		lio->intrmod_cfg.tx_enable = new_val ? 1 : 0;
		break;

	case LIO_RX_COALESCE_USECS:
		if (!lio->intrmod_cfg.rx_enable)
			new_val = old_val = oct->rx_coalesce_usecs;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		rx_coalesce_usecs = new_val;
		break;

	case LIO_RX_MAX_COALESCED_FRAMES:
		if (!lio->intrmod_cfg.rx_enable)
			new_val = old_val = oct->rx_max_coalesced_frames;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		rx_max_coalesced_frames = new_val;
		break;

	case LIO_TX_MAX_COALESCED_FRAMES:
		if (!lio->intrmod_cfg.tx_enable)
			new_val = old_val = oct->tx_max_coalesced_frames;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		tx_coalesce_usecs = new_val;
		break;

	case LIO_PKT_RATE_LOW:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.minpkt_ratethr;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.rx_enable || lio->intrmod_cfg.tx_enable)
			lio->intrmod_cfg.minpkt_ratethr = new_val;
		break;

	case LIO_RX_COALESCE_USECS_LOW:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.rx_mintmr_trigger;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.rx_enable)
			lio->intrmod_cfg.rx_mintmr_trigger = new_val;
		break;

	case LIO_RX_MAX_COALESCED_FRAMES_LOW:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.rx_mincnt_trigger;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.rx_enable)
			lio->intrmod_cfg.rx_mincnt_trigger = new_val;
		break;

	case LIO_TX_MAX_COALESCED_FRAMES_LOW:
		if (lio->intrmod_cfg.tx_enable)
			new_val = old_val = lio->intrmod_cfg.tx_mincnt_trigger;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.tx_enable)
			lio->intrmod_cfg.tx_mincnt_trigger = new_val;
		break;

	case LIO_PKT_RATE_HIGH:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.maxpkt_ratethr;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.rx_enable || lio->intrmod_cfg.tx_enable)
			lio->intrmod_cfg.maxpkt_ratethr = new_val;
		break;

	case LIO_RX_COALESCE_USECS_HIGH:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.rx_maxtmr_trigger;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.rx_enable)
			lio->intrmod_cfg.rx_maxtmr_trigger = new_val;
		break;

	case LIO_RX_MAX_COALESCED_FRAMES_HIGH:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.rx_maxcnt_trigger;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.rx_enable)
			lio->intrmod_cfg.rx_maxcnt_trigger = new_val;
		break;

	case LIO_TX_MAX_COALESCED_FRAMES_HIGH:
		if (lio->intrmod_cfg.tx_enable)
			new_val = old_val = lio->intrmod_cfg.tx_maxcnt_trigger;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.tx_enable)
			lio->intrmod_cfg.tx_maxcnt_trigger = new_val;
		break;

	case LIO_RATE_SAMPLE_INTERVAL:
		if (lio->intrmod_cfg.rx_enable)
			new_val = old_val = lio->intrmod_cfg.check_intrvl;

		err = sysctl_handle_64(oidp, &new_val, 0, req);
		if ((err) || (req->newptr == NULL))
			return (err);

		if (old_val == new_val)
			return (0);

		if (lio->intrmod_cfg.rx_enable || lio->intrmod_cfg.tx_enable)
			lio->intrmod_cfg.check_intrvl = new_val;
		break;

	default:
		return (EINVAL);
	}

	lio->intrmod_cfg.rx_usecs = LIO_GET_OQ_INTR_TIME_CFG(lio_get_conf(oct));
	lio->intrmod_cfg.rx_frames = LIO_GET_OQ_INTR_PKT_CFG(lio_get_conf(oct));
	lio->intrmod_cfg.tx_frames = LIO_GET_IQ_INTR_PKT_CFG(lio_get_conf(oct));

	ret = lio_set_intrmod_cfg(lio, &lio->intrmod_cfg);
	if (ret)
		lio_dev_err(oct, "Interrupt coalescing updation to Firmware failed!\n");

	if (!lio->intrmod_cfg.rx_enable) {
		if (!rx_coalesce_usecs)
			rx_coalesce_usecs = oct->rx_coalesce_usecs;

		if (!rx_max_coalesced_frames)
			rx_max_coalesced_frames = oct->rx_max_coalesced_frames;

		ret = lio_intrmod_cfg_rx_intrtime(lio, &lio->intrmod_cfg,
						  rx_coalesce_usecs);
		if (ret)
			return (ret);

		ret = lio_intrmod_cfg_rx_intrcnt(lio, &lio->intrmod_cfg,
						 rx_max_coalesced_frames);
		if (ret)
			return (ret);
	} else {
		oct->rx_coalesce_usecs =
		    LIO_GET_OQ_INTR_TIME_CFG(lio_get_conf(oct));
		oct->rx_max_coalesced_frames =
		    LIO_GET_OQ_INTR_PKT_CFG(lio_get_conf(oct));
	}

	if (!lio->intrmod_cfg.tx_enable) {
		if (!tx_coalesce_usecs)
			tx_coalesce_usecs = oct->tx_max_coalesced_frames;

		ret = lio_intrmod_cfg_tx_intrcnt(lio, &lio->intrmod_cfg,
						 tx_coalesce_usecs);
		if (ret)
			return (ret);
	} else {
		oct->tx_max_coalesced_frames =
			LIO_GET_IQ_INTR_PKT_CFG(lio_get_conf(oct));
	}

	return (0);
}
