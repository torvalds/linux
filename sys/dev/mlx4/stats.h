/*
 * Copyright (c) 2014 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MLX4_STATS_
#define _MLX4_STATS_

#define NUM_PRIORITIES	9
#define NUM_PRIORITY_STATS 2

struct mlx4_en_pkt_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_multicast_packets;
	u64 rx_broadcast_packets;
	u64 rx_errors;
	u64 rx_dropped;
	u64 rx_length_errors;
	u64 rx_over_errors;
	u64 rx_crc_errors;
	u64 rx_jabbers;
	u64 rx_in_range_length_error;
	u64 rx_out_range_length_error;
	u64 rx_lt_64_bytes_packets;
	u64 rx_127_bytes_packets;
	u64 rx_255_bytes_packets;
	u64 rx_511_bytes_packets;
	u64 rx_1023_bytes_packets;
	u64 rx_1518_bytes_packets;
	u64 rx_1522_bytes_packets;
	u64 rx_1548_bytes_packets;
	u64 rx_gt_1548_bytes_packets;
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_multicast_packets;
	u64 tx_broadcast_packets;
	u64 tx_errors;
	u64 tx_dropped;
	u64 tx_lt_64_bytes_packets;
	u64 tx_127_bytes_packets;
	u64 tx_255_bytes_packets;
	u64 tx_511_bytes_packets;
	u64 tx_1023_bytes_packets;
	u64 tx_1518_bytes_packets;
	u64 tx_1522_bytes_packets;
	u64 tx_1548_bytes_packets;
	u64 tx_gt_1548_bytes_packets;
	u64 rx_prio[NUM_PRIORITIES][NUM_PRIORITY_STATS];
	u64 tx_prio[NUM_PRIORITIES][NUM_PRIORITY_STATS];
};

struct mlx4_en_vf_stats {
	u64 rx_frames;
	u64 rx_bytes;
	u64 tx_frames;
	u64 tx_bytes;
};

struct mlx4_en_vport_stats {
	u64 rx_frames;
	u64 rx_bytes;
	u64 tx_frames;
	u64 tx_bytes;
};

struct mlx4_en_port_stats {
	u64 tso_packets;
	u64 queue_stopped;
	u64 wake_queue;
	u64 tx_timeout;
	u64 oversized_packets;
	u64 rx_alloc_failed;
	u64 rx_chksum_good;
	u64 rx_chksum_none;
	u64 tx_chksum_offload;
	u64 defrag_attempts;
};

struct mlx4_en_perf_stats {
	u32 tx_poll;
	u64 tx_pktsz_avg;
	u32 inflight_avg;
	u16 tx_coal_avg;
	u16 rx_coal_avg;
	u32 napi_quota;
};

#define MLX4_NUM_PRIORITIES	8

struct mlx4_en_flow_stats_rx {
	u64 rx_pause;
	u64 rx_pause_duration;
	u64 rx_pause_transition;
};

struct mlx4_en_flow_stats_tx {
	u64 tx_pause;
	u64 tx_pause_duration;
	u64 tx_pause_transition;
};

struct mlx4_en_stat_out_flow_control_mbox {
	/* Total number of PAUSE frames received from the far-end port */
	__be64 rx_pause;
	/* Total number of microseconds that far-end port requested to pause
	 * transmission of packets
	 */
	__be64 rx_pause_duration;
	/* Number of received transmission from XOFF state to XON state */
	__be64 rx_pause_transition;
	/* Total number of PAUSE frames sent from the far-end port */
	__be64 tx_pause;
	/* Total time in microseconds that transmission of packets has been
	 * paused
	 */
	__be64 tx_pause_duration;
	/* Number of transmitter transitions from XOFF state to XON state */
	__be64 tx_pause_transition;
	/* Reserverd */
	__be64 reserved[2];
};

enum {
	MLX4_DUMP_ETH_STATS_FLOW_CONTROL = 1 << 12
};

int mlx4_get_vport_ethtool_stats(struct mlx4_dev *dev, int port,
    struct mlx4_en_vport_stats *vport_stats,
    int reset, int *read_counters);

#endif
