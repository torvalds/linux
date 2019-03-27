/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __CUDBG_LIB_H__
#define __CUDBG_LIB_H__

#ifndef min_t
#define min_t(type, _a, _b)   (((type)(_a) < (type)(_b)) ? (type)(_a) : (type)(_b))
#endif

static int collect_reg_dump(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_fw_devlog(struct cudbg_init *, struct cudbg_buffer *,
			     struct cudbg_error *);
static int collect_cim_qcfg(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_cim_la(struct cudbg_init *, struct cudbg_buffer *,
			  struct cudbg_error *);
static int collect_cim_ma_la(struct cudbg_init *, struct cudbg_buffer *,
			     struct cudbg_error *);
static int collect_cim_obq_ulp0(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cim_obq_ulp1(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cim_obq_ulp2(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cim_obq_ulp3(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cim_obq_sge(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_cim_obq_ncsi(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cim_ibq_tp0(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_cim_ibq_tp1(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_cim_ibq_ulp(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_cim_ibq_sge0(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cim_ibq_sge1(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cim_ibq_ncsi(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_edc0_meminfo(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_edc1_meminfo(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_mc0_meminfo(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_mc1_meminfo(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_rss(struct cudbg_init *, struct cudbg_buffer *,
		       struct cudbg_error *);
static int collect_rss_key(struct cudbg_init *, struct cudbg_buffer *,
			   struct cudbg_error *);
static int collect_rss_pf_config(struct cudbg_init *, struct cudbg_buffer *,
				 struct cudbg_error *);
static int collect_rss_vf_config(struct cudbg_init *, struct cudbg_buffer *,
				 struct cudbg_error *);
static int collect_rss_config(struct cudbg_init *, struct cudbg_buffer *,
			      struct cudbg_error *);
static int collect_path_mtu(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_sw_state(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
int collect_wtp_data(struct cudbg_init *, struct cudbg_buffer *,
		     struct cudbg_error *);
static int collect_pm_stats(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_hw_sched(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_tcp_stats(struct cudbg_init *, struct cudbg_buffer *,
			     struct cudbg_error *);
static int collect_tp_err_stats(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_fcoe_stats(struct cudbg_init *, struct cudbg_buffer *,
			      struct cudbg_error *);
static int collect_rdma_stats(struct cudbg_init *, struct cudbg_buffer *,
			      struct cudbg_error *);
static int collect_tp_indirect(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_sge_indirect(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_cpl_stats(struct cudbg_init *, struct cudbg_buffer *,
			     struct cudbg_error *);
static int collect_ddp_stats(struct cudbg_init *, struct cudbg_buffer *,
			     struct cudbg_error *);
static int collect_wc_stats(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_ulprx_la(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_lb_stats(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_tp_la(struct cudbg_init *, struct cudbg_buffer *,
			 struct cudbg_error *);
static int collect_meminfo(struct cudbg_init *, struct cudbg_buffer *,
			   struct cudbg_error *);
static int collect_cim_pif_la(struct cudbg_init *, struct cudbg_buffer *,
			      struct cudbg_error *);
static int collect_clk_info(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_obq_sge_rx_q0(struct cudbg_init *, struct cudbg_buffer *,
				 struct cudbg_error *);
static int collect_obq_sge_rx_q1(struct cudbg_init *, struct cudbg_buffer *,
				 struct cudbg_error *);
static int collect_macstats(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_pcie_indirect(struct cudbg_init *, struct cudbg_buffer *,
				 struct cudbg_error *);
static int collect_pm_indirect(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_full(struct cudbg_init *, struct cudbg_buffer *,
			struct cudbg_error *);
static int collect_tx_rate(struct cudbg_init *, struct cudbg_buffer *,
			   struct cudbg_error *);
static int collect_tid(struct cudbg_init *, struct cudbg_buffer *,
		       struct cudbg_error *);
static int collect_pcie_config(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_dump_context(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);
static int collect_mps_tcam(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_vpd_data(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_le_tcam(struct cudbg_init *, struct cudbg_buffer *,
			   struct cudbg_error *);
static int collect_cctrl(struct cudbg_init *, struct cudbg_buffer *,
			 struct cudbg_error *);
static int collect_ma_indirect(struct cudbg_init *, struct cudbg_buffer *,
			       struct cudbg_error *);
static int collect_ulptx_la(struct cudbg_init *, struct cudbg_buffer *,
		struct cudbg_error *);
static int collect_up_cim_indirect(struct cudbg_init *, struct cudbg_buffer *,
				   struct cudbg_error *);
static int collect_pbt_tables(struct cudbg_init *, struct cudbg_buffer *,
			      struct cudbg_error *);
static int collect_mbox_log(struct cudbg_init *, struct cudbg_buffer *,
			    struct cudbg_error *);
static int collect_hma_indirect(struct cudbg_init *, struct cudbg_buffer *,
				struct cudbg_error *);

static int (*process_entity[])
	(struct cudbg_init *, struct cudbg_buffer *, struct cudbg_error *) = {
		collect_reg_dump,
		collect_fw_devlog,
		collect_cim_la,		/*3*/
		collect_cim_ma_la,
		collect_cim_qcfg,
		collect_cim_ibq_tp0,
		collect_cim_ibq_tp1,
		collect_cim_ibq_ulp,
		collect_cim_ibq_sge0,
		collect_cim_ibq_sge1,
		collect_cim_ibq_ncsi,
		collect_cim_obq_ulp0,
		collect_cim_obq_ulp1,	/*13*/
		collect_cim_obq_ulp2,
		collect_cim_obq_ulp3,
		collect_cim_obq_sge,
		collect_cim_obq_ncsi,
		collect_edc0_meminfo,
		collect_edc1_meminfo,
		collect_mc0_meminfo,
		collect_mc1_meminfo,
		collect_rss,		/*22*/
		collect_rss_pf_config,
		collect_rss_key,
		collect_rss_vf_config,
		collect_rss_config,	/*26*/
		collect_path_mtu,	/*27*/
		collect_sw_state,
		collect_wtp_data,
		collect_pm_stats,
		collect_hw_sched,
		collect_tcp_stats,
		collect_tp_err_stats,
		collect_fcoe_stats,
		collect_rdma_stats,
		collect_tp_indirect,
		collect_sge_indirect,
		collect_cpl_stats,
		collect_ddp_stats,
		collect_wc_stats,
		collect_ulprx_la,
		collect_lb_stats,
		collect_tp_la,
		collect_meminfo,
		collect_cim_pif_la,
		collect_clk_info,
		collect_obq_sge_rx_q0,
		collect_obq_sge_rx_q1,
		collect_macstats,
		collect_pcie_indirect,
		collect_pm_indirect,
		collect_full,
		collect_tx_rate,
		collect_tid,
		collect_pcie_config,
		collect_dump_context,
		collect_mps_tcam,
		collect_vpd_data,
		collect_le_tcam,
		collect_cctrl,
		collect_ma_indirect,
		collect_ulptx_la,
		NULL,			/* ext entity */
		collect_up_cim_indirect,
		collect_pbt_tables,
		collect_mbox_log,
		collect_hma_indirect,
	};

struct large_entity {
	int entity_code;
	int skip_flag;
	int priority; /* 1 is high priority */
};

static int read_cim_ibq(struct cudbg_init *, struct cudbg_buffer *,
			struct cudbg_error * , int);
static int read_cim_obq(struct cudbg_init *, struct cudbg_buffer *,
			struct cudbg_error *, int);
int get_entity_hdr(void *outbuf, int i, u32 size, struct cudbg_entity_hdr **);
void skip_entity(int entity_code);
void reset_skip_entity(void);
int is_large_entity(int entity_code);
#endif
