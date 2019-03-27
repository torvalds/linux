/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef MLX5_CORE_DIAGNOSTICS_H
#define	MLX5_CORE_DIAGNOSTICS_H

#define	MLX5_CORE_DIAGNOSTICS_NUM(n, s, t) n
#define	MLX5_CORE_DIAGNOSTICS_STRUCT(n, s, t) s,
#define	MLX5_CORE_DIAGNOSTICS_ENTRY(n, s, t) { #s, (t) },

struct mlx5_core_diagnostics_entry {
	const char *desc;
	u16	counter_id;
};

#define	MLX5_CORE_PCI_DIAGNOSTICS(m) \
m(+1, pxd_ready_bp, 0x0401) \
m(+1, pci_write_bp, 0x0402) \
m(+1, pci_read_bp, 0x0403) \
m(+1, pci_read_stuck_no_completion_buffer, 0x0404) \
m(+1, max_pci_bw, 0x0405) \
m(+1, used_pci_bw, 0x0406) \
m(+1, rx_pci_errors, 0) \
m(+1, tx_pci_errors, 0) \
m(+1, tx_pci_correctable_errors, 0) \
m(+1, tx_pci_non_fatal_errors, 0) \
m(+1, tx_pci_fatal_errors, 0)

#define	MLX5_CORE_PCI_DIAGNOSTICS_NUM \
	(0 MLX5_CORE_PCI_DIAGNOSTICS(MLX5_CORE_DIAGNOSTICS_NUM))

union mlx5_core_pci_diagnostics {
	u64	array[MLX5_CORE_PCI_DIAGNOSTICS_NUM];
	struct {
		u64	MLX5_CORE_PCI_DIAGNOSTICS(
			MLX5_CORE_DIAGNOSTICS_STRUCT) dummy[0];
	}	counter;
};

extern const struct mlx5_core_diagnostics_entry
	mlx5_core_pci_diagnostics_table[MLX5_CORE_PCI_DIAGNOSTICS_NUM];

#define	MLX5_CORE_GENERAL_DIAGNOSTICS(m) \
m(+1, l0_mtt_miss, 0x0801) \
m(+1, l0_mtt_hit, 0x0802) \
m(+1, l1_mtt_miss, 0x0803) \
m(+1, l1_mtt_hit, 0x0804) \
m(+1, l0_mpt_miss, 0x0805) \
m(+1, l0_mpt_hit, 0x0806) \
m(+1, l1_mpt_miss, 0x0807) \
m(+1, l1_mpt_hit, 0x0808) \
m(+1, rxb_no_slow_path_credits, 0x0c01) \
m(+1, rxb_no_fast_path_credits, 0x0c02) \
m(+1, rxb_rxt_no_slow_path_cred_perf_count, 0x0c03) \
m(+1, rxb_rxt_no_fast_path_cred_perf_count, 0x0c04) \
m(+1, rxt_ctrl_perf_slice_load_slow, 0x1001) \
m(+1, rxt_ctrl_perf_slice_load_fast, 0x1002) \
m(+1, rxt_steering_perf_count_steering0_rse_work_rate, 0x1003) \
m(+1, rxt_steering_perf_count_steering1_rse_work_rate, 0x1004) \
m(+1, perf_count_tpt_credit, 0x1401) \
m(+1, perf_wb_miss, 0x1402) \
m(+1, perf_wb_hit, 0x1403) \
m(+1, rxw_perf_rx_l1_slow_miss_ldb, 0x1404) \
m(+1, rxw_perf_rx_l1_slow_hit_ldb, 0x1405) \
m(+1, rxw_perf_rx_l1_fast_miss_ldb, 0x1406) \
m(+1, rxw_perf_rx_l1_fast_hit_ldb, 0x1407) \
m(+1, rxw_perf_l2_cache_read_miss_ldb, 0x1408) \
m(+1, rxw_perf_l2_cache_read_hit_ldb, 0x1409) \
m(+1, rxw_perf_rx_l1_slow_miss_reqsl, 0x140a) \
m(+1, rxw_perf_rx_l1_slow_hit_reqsl, 0x140b) \
m(+1, rxw_perf_rx_l1_fast_miss_reqsl, 0x140c) \
m(+1, rxw_perf_rx_l1_fast_hit_reqsl, 0x140d) \
m(+1, rxw_perf_l2_cache_read_miss_reqsl, 0x140e) \
m(+1, rxw_perf_l2_cache_read_hit_reqsl, 0x140f) \
m(+1, rxs_no_pxt_credits, 0x1801) \
m(+1, rxc_eq_all_slices_busy, 0x1c01) \
m(+1, rxc_cq_all_slices_busy, 0x1c02) \
m(+1, rxc_msix_all_slices_busy, 0x1c03) \
m(+1, sxw_qp_done_due_to_vl_limited, 0x2001) \
m(+1, sxw_qp_done_due_to_desched, 0x2002) \
m(+1, sxw_qp_done_due_to_work_done, 0x2003) \
m(+1, sxw_qp_done_due_to_limited, 0x2004) \
m(+1, sxw_qp_done_due_to_e2e_credits, 0x2005) \
m(+1, sxw_packet_send_sxw2sxp_go_vld, 0x2006) \
m(+1, sxw_perf_count_steering_hit, 0x2007) \
m(+1, sxw_perf_count_steering_miss, 0x2008) \
m(+1, sxw_perf_count_steering_rse_0, 0x2009) \
m(+1, sxd_no_sched_credits, 0x2401) \
m(+1, sxd_no_slow_path_sched_credits, 0x2402) \
m(+1, tpt_indirect_mem_key, 0x2801)

#define	MLX5_CORE_GENERAL_DIAGNOSTICS_NUM \
	(0 MLX5_CORE_GENERAL_DIAGNOSTICS(MLX5_CORE_DIAGNOSTICS_NUM))

union mlx5_core_general_diagnostics {
	u64	array[MLX5_CORE_GENERAL_DIAGNOSTICS_NUM];
	struct {
		u64	MLX5_CORE_GENERAL_DIAGNOSTICS(
			MLX5_CORE_DIAGNOSTICS_STRUCT) dummy[0];
	}	counter;
};

extern const struct mlx5_core_diagnostics_entry
	mlx5_core_general_diagnostics_table[MLX5_CORE_GENERAL_DIAGNOSTICS_NUM];

/* function prototypes */
int mlx5_core_set_diagnostics_full(struct mlx5_core_dev *mdev,
				   u8 enable_pci, u8 enable_general);
int mlx5_core_get_diagnostics_full(struct mlx5_core_dev *mdev,
				   union mlx5_core_pci_diagnostics *ppci,
				   union mlx5_core_general_diagnostics *pgen);
int mlx5_core_supports_diagnostics(struct mlx5_core_dev *mdev, u16 counter_id);

#endif					/* MLX5_CORE_DIAGNOSTICS_H */
