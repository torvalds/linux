/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Chelsio Communications, Inc.
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

#ifndef __CHELSIO_COMMON_H
#define __CHELSIO_COMMON_H

#include "t4_hw.h"

enum {
	MAX_NPORTS     = 4,     /* max # of ports */
	SERNUM_LEN     = 24,    /* Serial # length */
	EC_LEN         = 16,    /* E/C length */
	ID_LEN         = 16,    /* ID length */
	PN_LEN         = 16,    /* Part Number length */
	MD_LEN         = 16,    /* MFG diags version length */
	MACADDR_LEN    = 12,    /* MAC Address length */
};

enum {
	T4_REGMAP_SIZE = (160 * 1024),
	T5_REGMAP_SIZE = (332 * 1024),
};

enum { MEM_EDC0, MEM_EDC1, MEM_MC, MEM_MC0 = MEM_MC, MEM_MC1 };

enum dev_master { MASTER_CANT, MASTER_MAY, MASTER_MUST };

enum dev_state { DEV_STATE_UNINIT, DEV_STATE_INIT, DEV_STATE_ERR };

enum {
	PAUSE_RX      = 1 << 0,
	PAUSE_TX      = 1 << 1,
	PAUSE_AUTONEG = 1 << 2
};

enum {
	FEC_NONE      = 0,
	FEC_RS        = 1 << 0,
	FEC_BASER_RS  = 1 << 1,
	FEC_AUTO      = 1 << 5,		/* M_FW_PORT_CAP32_FEC + 1 */
};

enum t4_bar2_qtype { T4_BAR2_QTYPE_EGRESS, T4_BAR2_QTYPE_INGRESS };

struct port_stats {
	u64 tx_octets;            /* total # of octets in good frames */
	u64 tx_frames;            /* all good frames */
	u64 tx_bcast_frames;      /* all broadcast frames */
	u64 tx_mcast_frames;      /* all multicast frames */
	u64 tx_ucast_frames;      /* all unicast frames */
	u64 tx_error_frames;      /* all error frames */

	u64 tx_frames_64;         /* # of Tx frames in a particular range */
	u64 tx_frames_65_127;
	u64 tx_frames_128_255;
	u64 tx_frames_256_511;
	u64 tx_frames_512_1023;
	u64 tx_frames_1024_1518;
	u64 tx_frames_1519_max;

	u64 tx_drop;              /* # of dropped Tx frames */
	u64 tx_pause;             /* # of transmitted pause frames */
	u64 tx_ppp0;              /* # of transmitted PPP prio 0 frames */
	u64 tx_ppp1;              /* # of transmitted PPP prio 1 frames */
	u64 tx_ppp2;              /* # of transmitted PPP prio 2 frames */
	u64 tx_ppp3;              /* # of transmitted PPP prio 3 frames */
	u64 tx_ppp4;              /* # of transmitted PPP prio 4 frames */
	u64 tx_ppp5;              /* # of transmitted PPP prio 5 frames */
	u64 tx_ppp6;              /* # of transmitted PPP prio 6 frames */
	u64 tx_ppp7;              /* # of transmitted PPP prio 7 frames */

	u64 rx_octets;            /* total # of octets in good frames */
	u64 rx_frames;            /* all good frames */
	u64 rx_bcast_frames;      /* all broadcast frames */
	u64 rx_mcast_frames;      /* all multicast frames */
	u64 rx_ucast_frames;      /* all unicast frames */
	u64 rx_too_long;          /* # of frames exceeding MTU */
	u64 rx_jabber;            /* # of jabber frames */
	u64 rx_fcs_err;           /* # of received frames with bad FCS */
	u64 rx_len_err;           /* # of received frames with length error */
	u64 rx_symbol_err;        /* symbol errors */
	u64 rx_runt;              /* # of short frames */

	u64 rx_frames_64;         /* # of Rx frames in a particular range */
	u64 rx_frames_65_127;
	u64 rx_frames_128_255;
	u64 rx_frames_256_511;
	u64 rx_frames_512_1023;
	u64 rx_frames_1024_1518;
	u64 rx_frames_1519_max;

	u64 rx_pause;             /* # of received pause frames */
	u64 rx_ppp0;              /* # of received PPP prio 0 frames */
	u64 rx_ppp1;              /* # of received PPP prio 1 frames */
	u64 rx_ppp2;              /* # of received PPP prio 2 frames */
	u64 rx_ppp3;              /* # of received PPP prio 3 frames */
	u64 rx_ppp4;              /* # of received PPP prio 4 frames */
	u64 rx_ppp5;              /* # of received PPP prio 5 frames */
	u64 rx_ppp6;              /* # of received PPP prio 6 frames */
	u64 rx_ppp7;              /* # of received PPP prio 7 frames */

	u64 rx_ovflow0;           /* drops due to buffer-group 0 overflows */
	u64 rx_ovflow1;           /* drops due to buffer-group 1 overflows */
	u64 rx_ovflow2;           /* drops due to buffer-group 2 overflows */
	u64 rx_ovflow3;           /* drops due to buffer-group 3 overflows */
	u64 rx_trunc0;            /* buffer-group 0 truncated packets */
	u64 rx_trunc1;            /* buffer-group 1 truncated packets */
	u64 rx_trunc2;            /* buffer-group 2 truncated packets */
	u64 rx_trunc3;            /* buffer-group 3 truncated packets */
};

struct lb_port_stats {
	u64 octets;
	u64 frames;
	u64 bcast_frames;
	u64 mcast_frames;
	u64 ucast_frames;
	u64 error_frames;

	u64 frames_64;
	u64 frames_65_127;
	u64 frames_128_255;
	u64 frames_256_511;
	u64 frames_512_1023;
	u64 frames_1024_1518;
	u64 frames_1519_max;

	u64 drop;

	u64 ovflow0;
	u64 ovflow1;
	u64 ovflow2;
	u64 ovflow3;
	u64 trunc0;
	u64 trunc1;
	u64 trunc2;
	u64 trunc3;
};

struct tp_tcp_stats {
	u32 tcp_out_rsts;
	u64 tcp_in_segs;
	u64 tcp_out_segs;
	u64 tcp_retrans_segs;
};

struct tp_usm_stats {
	u32 frames;
	u32 drops;
	u64 octets;
};

struct tp_fcoe_stats {
	u32 frames_ddp;
	u32 frames_drop;
	u64 octets_ddp;
};

struct tp_err_stats {
	u32 mac_in_errs[MAX_NCHAN];
	u32 hdr_in_errs[MAX_NCHAN];
	u32 tcp_in_errs[MAX_NCHAN];
	u32 tnl_cong_drops[MAX_NCHAN];
	u32 ofld_chan_drops[MAX_NCHAN];
	u32 tnl_tx_drops[MAX_NCHAN];
	u32 ofld_vlan_drops[MAX_NCHAN];
	u32 tcp6_in_errs[MAX_NCHAN];
	u32 ofld_no_neigh;
	u32 ofld_cong_defer;
};

struct tp_proxy_stats {
	u32 proxy[MAX_NCHAN];
};

struct tp_cpl_stats {
	u32 req[MAX_NCHAN];
	u32 rsp[MAX_NCHAN];
};

struct tp_rdma_stats {
	u32 rqe_dfr_pkt;
	u32 rqe_dfr_mod;
};

struct sge_params {
	int timer_val[SGE_NTIMERS];	/* final, scaled values */
	int counter_val[SGE_NCOUNTERS];
	int fl_starve_threshold;
	int fl_starve_threshold2;
	int page_shift;
	int eq_s_qpp;
	int iq_s_qpp;
	int spg_len;
	int pad_boundary;
	int pack_boundary;
	int fl_pktshift;
	u32 sge_control;
	u32 sge_fl_buffer_size[SGE_FLBUF_SIZES];
};

struct tp_params {
	unsigned int tre;            /* log2 of core clocks per TP tick */
	unsigned int dack_re;        /* DACK timer resolution */
	unsigned int la_mask;        /* what events are recorded by TP LA */
	unsigned short tx_modq[MAX_NCHAN];  /* channel to modulation queue map */

	uint32_t vlan_pri_map;
	uint32_t ingress_config;
	uint64_t hash_filter_mask;
	__be16 err_vec_mask;

	int8_t fcoe_shift;
	int8_t port_shift;
	int8_t vnic_shift;
	int8_t vlan_shift;
	int8_t tos_shift;
	int8_t protocol_shift;
	int8_t ethertype_shift;
	int8_t macmatch_shift;
	int8_t matchtype_shift;
	int8_t frag_shift;
};

struct vpd_params {
	unsigned int cclk;
	u8 ec[EC_LEN + 1];
	u8 sn[SERNUM_LEN + 1];
	u8 id[ID_LEN + 1];
	u8 pn[PN_LEN + 1];
	u8 na[MACADDR_LEN + 1];
	u8 md[MD_LEN + 1];
};

struct pci_params {
	unsigned int vpd_cap_addr;
	unsigned int mps;
	unsigned short speed;
	unsigned short width;
};

/*
 * Firmware device log.
 */
struct devlog_params {
	u32 memtype;			/* which memory (FW_MEMTYPE_* ) */
	u32 start;			/* start of log in firmware memory */
	u32 size;			/* size of log */
	u32 addr;			/* start address in flat addr space */
};

/* Stores chip specific parameters */
struct chip_params {
	u8 nchan;
	u8 pm_stats_cnt;
	u8 cng_ch_bits_log;		/* congestion channel map bits width */
	u8 nsched_cls;
	u8 cim_num_obq;
	u16 mps_rplc_size;
	u16 vfcount;
	u32 sge_fl_db;
	u16 mps_tcam_size;
};

/* VF-only parameters. */

/*
 * Global Receive Side Scaling (RSS) parameters in host-native format.
 */
struct rss_params {
	unsigned int mode;		/* RSS mode */
	union {
	    struct {
		u_int synmapen:1;	/* SYN Map Enable */
		u_int syn4tupenipv6:1;	/* enable hashing 4-tuple IPv6 SYNs */
		u_int syn2tupenipv6:1;	/* enable hashing 2-tuple IPv6 SYNs */
		u_int syn4tupenipv4:1;	/* enable hashing 4-tuple IPv4 SYNs */
		u_int syn2tupenipv4:1;	/* enable hashing 2-tuple IPv4 SYNs */
		u_int ofdmapen:1;	/* Offload Map Enable */
		u_int tnlmapen:1;	/* Tunnel Map Enable */
		u_int tnlalllookup:1;	/* Tunnel All Lookup */
		u_int hashtoeplitz:1;	/* use Toeplitz hash */
	    } basicvirtual;
	} u;
};

/*
 * Maximum resources provisioned for a PCI VF.
 */
struct vf_resources {
	unsigned int nvi;		/* N virtual interfaces */
	unsigned int neq;		/* N egress Qs */
	unsigned int nethctrl;		/* N egress ETH or CTRL Qs */
	unsigned int niqflint;		/* N ingress Qs/w free list(s) & intr */
	unsigned int niq;		/* N ingress Qs */
	unsigned int tc;		/* PCI-E traffic class */
	unsigned int pmask;		/* port access rights mask */
	unsigned int nexactf;		/* N exact MPS filters */
	unsigned int r_caps;		/* read capabilities */
	unsigned int wx_caps;		/* write/execute capabilities */
};

struct adapter_params {
	struct sge_params sge;
	struct tp_params  tp;		/* PF-only */
	struct vpd_params vpd;
	struct pci_params pci;
	struct devlog_params devlog;	/* PF-only */
	struct rss_params rss;		/* VF-only */
	struct vf_resources vfres;	/* VF-only */
	unsigned int core_vdd;

	unsigned int sf_size;             /* serial flash size in bytes */
	unsigned int sf_nsec;             /* # of flash sectors */

	unsigned int fw_vers;		/* firmware version */
	unsigned int bs_vers;		/* bootstrap version */
	unsigned int tp_vers;		/* TP microcode version */
	unsigned int er_vers;		/* expansion ROM version */
	unsigned int scfg_vers;		/* Serial Configuration version */
	unsigned int vpd_vers;		/* VPD version */

	unsigned short mtus[NMTUS];
	unsigned short a_wnd[NCCTRL_WIN];
	unsigned short b_wnd[NCCTRL_WIN];

	unsigned int cim_la_size;

	uint8_t nports;		/* # of ethernet ports */
	uint8_t portvec;
	unsigned int chipid:4;	/* chip ID.  T4 = 4, T5 = 5, ... */
	unsigned int rev:4;	/* chip revision */
	unsigned int fpga:1;	/* this is an FPGA */
	unsigned int offload:1;	/* hw is TOE capable, fw has divvied up card
				   resources for TOE operation. */
	unsigned int bypass:1;	/* this is a bypass card */
	unsigned int ethoffload:1;
	unsigned int hash_filter:1;
	unsigned int filter2_wr_support:1;
	unsigned int port_caps32:1;

	unsigned int ofldq_wr_cred;
	unsigned int eo_wr_cred;

	unsigned int max_ordird_qp;
	unsigned int max_ird_adapter;

	uint32_t mps_bg_map;	/* rx buffer group map for all ports (upto 4) */

	bool ulptx_memwrite_dsgl;	/* use of T5 DSGL allowed */
	bool fr_nsmr_tpte_wr_support;	/* FW support for FR_NSMR_TPTE_WR */
	bool viid_smt_extn_support;	/* FW returns vin, vfvld & smt index? */
};

#define CHELSIO_T4		0x4
#define CHELSIO_T5		0x5
#define CHELSIO_T6		0x6

/*
 * State needed to monitor the forward progress of SGE Ingress DMA activities
 * and possible hangs.
 */
struct sge_idma_monitor_state {
	unsigned int idma_1s_thresh;	/* 1s threshold in Core Clock ticks */
	unsigned int idma_stalled[2];	/* synthesized stalled timers in HZ */
	unsigned int idma_state[2];	/* IDMA Hang detect state */
	unsigned int idma_qid[2];	/* IDMA Hung Ingress Queue ID */
	unsigned int idma_warn[2];	/* time to warning in HZ */
};

struct trace_params {
	u32 data[TRACE_LEN / 4];
	u32 mask[TRACE_LEN / 4];
	unsigned short snap_len;
	unsigned short min_len;
	unsigned char skip_ofst;
	unsigned char skip_len;
	unsigned char invert;
	unsigned char port;
};

struct link_config {
	/* OS-specific code owns all the requested_* fields. */
	int8_t requested_aneg;		/* link autonegotiation */
	int8_t requested_fc;		/* flow control */
	int8_t requested_fec;		/* FEC */
	u_int requested_speed;		/* speed (Mbps) */

	uint32_t supported;		/* link capabilities */
	uint32_t advertising;		/* advertised capabilities */
	uint32_t lp_advertising;	/* peer advertised capabilities */
	uint32_t fec_hint;		/* use this fec */
	u_int speed;			/* actual link speed (Mbps) */
	int8_t fc;			/* actual link flow control */
	int8_t fec;			/* actual FEC */
	bool link_ok;			/* link up? */
	uint8_t link_down_rc;		/* link down reason */
};

#include "adapter.h"

#ifndef PCI_VENDOR_ID_CHELSIO
# define PCI_VENDOR_ID_CHELSIO 0x1425
#endif

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; ++iter)

static inline int is_ftid(const struct adapter *sc, u_int tid)
{

	return (sc->tids.nftids > 0 && tid >= sc->tids.ftid_base &&
	    tid <= sc->tids.ftid_end);
}

static inline int is_hpftid(const struct adapter *sc, u_int tid)
{

	return (sc->tids.nhpftids > 0 && tid >= sc->tids.hpftid_base &&
	    tid <= sc->tids.hpftid_end);
}

static inline int is_etid(const struct adapter *sc, u_int tid)
{

	return (sc->tids.netids > 0 && tid >= sc->tids.etid_base &&
	    tid <= sc->tids.etid_end);
}

static inline int is_offload(const struct adapter *adap)
{
	return adap->params.offload;
}

static inline int is_ethoffload(const struct adapter *adap)
{
	return adap->params.ethoffload;
}

static inline int is_hashfilter(const struct adapter *adap)
{
	return adap->params.hash_filter;
}

static inline int chip_id(struct adapter *adap)
{
	return adap->params.chipid;
}

static inline int chip_rev(struct adapter *adap)
{
	return adap->params.rev;
}

static inline int is_t4(struct adapter *adap)
{
	return adap->params.chipid == CHELSIO_T4;
}

static inline int is_t5(struct adapter *adap)
{
	return adap->params.chipid == CHELSIO_T5;
}

static inline int is_t6(struct adapter *adap)
{
	return adap->params.chipid == CHELSIO_T6;
}

static inline int is_fpga(struct adapter *adap)
{
	 return adap->params.fpga;
}

static inline unsigned int core_ticks_per_usec(const struct adapter *adap)
{
	return adap->params.vpd.cclk / 1000;
}

static inline unsigned int us_to_core_ticks(const struct adapter *adap,
					    unsigned int us)
{
	return (us * adap->params.vpd.cclk) / 1000;
}

static inline unsigned int core_ticks_to_us(const struct adapter *adapter,
					    unsigned int ticks)
{
	/* add Core Clock / 2 to round ticks to nearest uS */
	return ((ticks * 1000 + adapter->params.vpd.cclk/2) /
		adapter->params.vpd.cclk);
}

static inline unsigned int dack_ticks_to_usec(const struct adapter *adap,
					      unsigned int ticks)
{
	return (ticks << adap->params.tp.dack_re) / core_ticks_per_usec(adap);
}

static inline u_int us_to_tcp_ticks(const struct adapter *adap, u_long us)
{

	return (us * adap->params.vpd.cclk / 1000 >> adap->params.tp.tre);
}

static inline u_int tcp_ticks_to_us(const struct adapter *adap, u_int ticks)
{
	return ((uint64_t)ticks << adap->params.tp.tre) /
	    core_ticks_per_usec(adap);
}

void t4_set_reg_field(struct adapter *adap, unsigned int addr, u32 mask, u32 val);

int t4_wr_mbox_meat_timeout(struct adapter *adap, int mbox, const void *cmd,
			    int size, void *rpl, bool sleep_ok, int timeout);
int t4_wr_mbox_meat(struct adapter *adap, int mbox, const void *cmd, int size,
		    void *rpl, bool sleep_ok);

static inline int t4_wr_mbox_timeout(struct adapter *adap, int mbox,
				     const void *cmd, int size, void *rpl,
				     int timeout)
{
	return t4_wr_mbox_meat_timeout(adap, mbox, cmd, size, rpl, true,
				       timeout);
}

static inline int t4_wr_mbox(struct adapter *adap, int mbox, const void *cmd,
			     int size, void *rpl)
{
	return t4_wr_mbox_meat(adap, mbox, cmd, size, rpl, true);
}

static inline int t4_wr_mbox_ns(struct adapter *adap, int mbox, const void *cmd,
				int size, void *rpl)
{
	return t4_wr_mbox_meat(adap, mbox, cmd, size, rpl, false);
}

void t4_read_indirect(struct adapter *adap, unsigned int addr_reg,
		      unsigned int data_reg, u32 *vals, unsigned int nregs,
		      unsigned int start_idx);
void t4_write_indirect(struct adapter *adap, unsigned int addr_reg,
		       unsigned int data_reg, const u32 *vals,
		       unsigned int nregs, unsigned int start_idx);

u32 t4_hw_pci_read_cfg4(adapter_t *adapter, int reg);

struct fw_filter_wr;

void t4_intr_enable(struct adapter *adapter);
void t4_intr_disable(struct adapter *adapter);
void t4_intr_clear(struct adapter *adapter);
int t4_slow_intr_handler(struct adapter *adapter, bool verbose);

int t4_hash_mac_addr(const u8 *addr);
int t4_link_l1cfg(struct adapter *adap, unsigned int mbox, unsigned int port,
		  struct link_config *lc);
int t4_restart_aneg(struct adapter *adap, unsigned int mbox, unsigned int port);
int t4_seeprom_read(struct adapter *adapter, u32 addr, u32 *data);
int t4_seeprom_write(struct adapter *adapter, u32 addr, u32 data);
int t4_eeprom_ptov(unsigned int phys_addr, unsigned int fn, unsigned int sz);
int t4_seeprom_wp(struct adapter *adapter, int enable);
int t4_read_flash(struct adapter *adapter, unsigned int addr, unsigned int nwords,
		  u32 *data, int byte_oriented);
int t4_write_flash(struct adapter *adapter, unsigned int addr,
		   unsigned int n, const u8 *data, int byte_oriented);
int t4_load_fw(struct adapter *adapter, const u8 *fw_data, unsigned int size);
int t4_fwcache(struct adapter *adap, enum fw_params_param_dev_fwcache op);
int t5_fw_init_extern_mem(struct adapter *adap);
int t4_load_bootcfg(struct adapter *adapter, const u8 *cfg_data, unsigned int size);
int t4_load_boot(struct adapter *adap, u8 *boot_data,
                 unsigned int boot_addr, unsigned int size);
int t4_flash_erase_sectors(struct adapter *adapter, int start, int end);
int t4_flash_cfg_addr(struct adapter *adapter);
int t4_load_cfg(struct adapter *adapter, const u8 *cfg_data, unsigned int size);
int t4_get_fw_version(struct adapter *adapter, u32 *vers);
int t4_get_fw_hdr(struct adapter *adapter, struct fw_hdr *hdr);
int t4_get_bs_version(struct adapter *adapter, u32 *vers);
int t4_get_tp_version(struct adapter *adapter, u32 *vers);
int t4_get_exprom_version(struct adapter *adapter, u32 *vers);
int t4_get_scfg_version(struct adapter *adapter, u32 *vers);
int t4_get_vpd_version(struct adapter *adapter, u32 *vers);
int t4_get_version_info(struct adapter *adapter);
int t4_init_hw(struct adapter *adapter, u32 fw_params);
const struct chip_params *t4_get_chip_params(int chipid);
int t4_prep_adapter(struct adapter *adapter, u32 *buf);
int t4_shutdown_adapter(struct adapter *adapter);
int t4_init_devlog_params(struct adapter *adapter, int fw_attach);
int t4_init_sge_params(struct adapter *adapter);
int t4_init_tp_params(struct adapter *adap, bool sleep_ok);
int t4_filter_field_shift(const struct adapter *adap, int filter_sel);
int t4_port_init(struct adapter *adap, int mbox, int pf, int vf, int port_id);
void t4_fatal_err(struct adapter *adapter, bool fw_error);
int t4_set_trace_filter(struct adapter *adapter, const struct trace_params *tp,
			int filter_index, int enable);
void t4_get_trace_filter(struct adapter *adapter, struct trace_params *tp,
			 int filter_index, int *enabled);
int t4_config_rss_range(struct adapter *adapter, int mbox, unsigned int viid,
			int start, int n, const u16 *rspq, unsigned int nrspq);
int t4_config_glbl_rss(struct adapter *adapter, int mbox, unsigned int mode,
		       unsigned int flags);
int t4_config_vi_rss(struct adapter *adapter, int mbox, unsigned int viid,
		     unsigned int flags, unsigned int defq, unsigned int skeyidx,
		     unsigned int skey);
int t4_read_rss(struct adapter *adapter, u16 *entries);
void t4_read_rss_key(struct adapter *adapter, u32 *key, bool sleep_ok);
void t4_write_rss_key(struct adapter *adap, const u32 *key, int idx,
		      bool sleep_ok);
void t4_read_rss_pf_config(struct adapter *adapter, unsigned int index,
			   u32 *valp, bool sleep_ok);
void t4_write_rss_pf_config(struct adapter *adapter, unsigned int index,
			    u32 val, bool sleep_ok);
void t4_read_rss_vf_config(struct adapter *adapter, unsigned int index,
			   u32 *vfl, u32 *vfh, bool sleep_ok);
void t4_write_rss_vf_config(struct adapter *adapter, unsigned int index,
			    u32 vfl, u32 vfh, bool sleep_ok);
u32 t4_read_rss_pf_map(struct adapter *adapter, bool sleep_ok);
void t4_write_rss_pf_map(struct adapter *adapter, u32 pfmap, bool sleep_ok);
u32 t4_read_rss_pf_mask(struct adapter *adapter, bool sleep_ok);
void t4_write_rss_pf_mask(struct adapter *adapter, u32 pfmask, bool sleep_ok);
int t4_mps_set_active_ports(struct adapter *adap, unsigned int port_mask);
void t4_pmtx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[]);
void t4_pmrx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[]);
void t4_read_cimq_cfg(struct adapter *adap, u16 *base, u16 *size, u16 *thres);
int t4_read_cim_ibq(struct adapter *adap, unsigned int qid, u32 *data, size_t n);
int t4_read_cim_obq(struct adapter *adap, unsigned int qid, u32 *data, size_t n);
int t4_cim_read(struct adapter *adap, unsigned int addr, unsigned int n,
		unsigned int *valp);
int t4_cim_write(struct adapter *adap, unsigned int addr, unsigned int n,
		 const unsigned int *valp);
int t4_cim_ctl_read(struct adapter *adap, unsigned int addr, unsigned int n,
		    unsigned int *valp);
int t4_cim_read_la(struct adapter *adap, u32 *la_buf, unsigned int *wrptr);
void t4_cim_read_pif_la(struct adapter *adap, u32 *pif_req, u32 *pif_rsp,
		unsigned int *pif_req_wrptr, unsigned int *pif_rsp_wrptr);
void t4_cim_read_ma_la(struct adapter *adap, u32 *ma_req, u32 *ma_rsp);
int t4_get_flash_params(struct adapter *adapter);

u32 t4_read_pcie_cfg4(struct adapter *adap, int reg, int drv_fw_attach);
int t4_mc_read(struct adapter *adap, int idx, u32 addr,
	       __be32 *data, u64 *parity);
int t4_edc_read(struct adapter *adap, int idx, u32 addr, __be32 *data, u64 *parity);
int t4_mem_read(struct adapter *adap, int mtype, u32 addr, u32 size,
		__be32 *data);
void t4_idma_monitor_init(struct adapter *adapter,
			  struct sge_idma_monitor_state *idma);
void t4_idma_monitor(struct adapter *adapter,
		     struct sge_idma_monitor_state *idma,
		     int hz, int ticks);

unsigned int t4_get_regs_len(struct adapter *adapter);
void t4_get_regs(struct adapter *adap, u8 *buf, size_t buf_size);

const char *t4_get_port_type_description(enum fw_port_type port_type);
void t4_get_port_stats(struct adapter *adap, int idx, struct port_stats *p);
void t4_get_port_stats_offset(struct adapter *adap, int idx,
		struct port_stats *stats,
		struct port_stats *offset);
void t4_get_lb_stats(struct adapter *adap, int idx, struct lb_port_stats *p);
void t4_clr_port_stats(struct adapter *adap, int idx);

void t4_read_mtu_tbl(struct adapter *adap, u16 *mtus, u8 *mtu_log);
void t4_read_cong_tbl(struct adapter *adap, u16 incr[NMTUS][NCCTRL_WIN]);
void t4_read_pace_tbl(struct adapter *adap, unsigned int pace_vals[NTX_SCHED]);
void t4_get_tx_sched(struct adapter *adap, unsigned int sched, unsigned int *kbps,
		     unsigned int *ipg, bool sleep_ok);
void t4_tp_wr_bits_indirect(struct adapter *adap, unsigned int addr,
			    unsigned int mask, unsigned int val);
void t4_tp_read_la(struct adapter *adap, u64 *la_buf, unsigned int *wrptr);
void t4_tp_get_err_stats(struct adapter *adap, struct tp_err_stats *st,
			 bool sleep_ok);
void t4_tp_get_proxy_stats(struct adapter *adap, struct tp_proxy_stats *st,
    			   bool sleep_ok);
void t4_tp_get_cpl_stats(struct adapter *adap, struct tp_cpl_stats *st,
			 bool sleep_ok);
void t4_tp_get_rdma_stats(struct adapter *adap, struct tp_rdma_stats *st,
			  bool sleep_ok);
void t4_get_usm_stats(struct adapter *adap, struct tp_usm_stats *st,
		      bool sleep_ok);
void t4_tp_get_tcp_stats(struct adapter *adap, struct tp_tcp_stats *v4,
			 struct tp_tcp_stats *v6, bool sleep_ok);
void t4_get_fcoe_stats(struct adapter *adap, unsigned int idx,
		       struct tp_fcoe_stats *st, bool sleep_ok);
void t4_load_mtus(struct adapter *adap, const unsigned short *mtus,
		  const unsigned short *alpha, const unsigned short *beta);

void t4_ulprx_read_la(struct adapter *adap, u32 *la_buf);

int t4_set_sched_bps(struct adapter *adap, int sched, unsigned int kbps);
int t4_set_sched_ipg(struct adapter *adap, int sched, unsigned int ipg);
int t4_set_pace_tbl(struct adapter *adap, const unsigned int *pace_vals,
		    unsigned int start, unsigned int n);
void t4_get_chan_txrate(struct adapter *adap, u64 *nic_rate, u64 *ofld_rate);
int t4_set_filter_mode(struct adapter *adap, unsigned int mode_map,
    bool sleep_ok);
void t4_mk_filtdelwr(unsigned int ftid, struct fw_filter_wr *wr, int qid);

void t4_wol_magic_enable(struct adapter *adap, unsigned int port, const u8 *addr);
int t4_wol_pat_enable(struct adapter *adap, unsigned int port, unsigned int map,
		      u64 mask0, u64 mask1, unsigned int crc, bool enable);

int t4_fw_hello(struct adapter *adap, unsigned int mbox, unsigned int evt_mbox,
		enum dev_master master, enum dev_state *state);
int t4_fw_bye(struct adapter *adap, unsigned int mbox);
int t4_fw_reset(struct adapter *adap, unsigned int mbox, int reset);
int t4_fw_halt(struct adapter *adap, unsigned int mbox, int force);
int t4_fw_restart(struct adapter *adap, unsigned int mbox);
int t4_fw_upgrade(struct adapter *adap, unsigned int mbox,
		  const u8 *fw_data, unsigned int size, int force);
int t4_fw_initialize(struct adapter *adap, unsigned int mbox);
int t4_query_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int nparams, const u32 *params,
		    u32 *val);
int t4_query_params_rw(struct adapter *adap, unsigned int mbox, unsigned int pf,
		       unsigned int vf, unsigned int nparams, const u32 *params,
		       u32 *val, int rw);
int t4_set_params_timeout(struct adapter *adap, unsigned int mbox,
			  unsigned int pf, unsigned int vf,
			  unsigned int nparams, const u32 *params,
			  const u32 *val, int timeout);
int t4_set_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		  unsigned int vf, unsigned int nparams, const u32 *params,
		  const u32 *val);
int t4_cfg_pfvf(struct adapter *adap, unsigned int mbox, unsigned int pf,
		unsigned int vf, unsigned int txq, unsigned int txq_eth_ctrl,
		unsigned int rxqi, unsigned int rxq, unsigned int tc,
		unsigned int vi, unsigned int cmask, unsigned int pmask,
		unsigned int exactf, unsigned int rcaps, unsigned int wxcaps);
int t4_alloc_vi_func(struct adapter *adap, unsigned int mbox,
		     unsigned int port, unsigned int pf, unsigned int vf,
		     unsigned int nmac, u8 *mac, u16 *rss_size,
		     uint8_t *vfvld, uint16_t *vin,
		     unsigned int portfunc, unsigned int idstype);
int t4_alloc_vi(struct adapter *adap, unsigned int mbox, unsigned int port,
		unsigned int pf, unsigned int vf, unsigned int nmac, u8 *mac,
		u16 *rss_size, uint8_t *vfvld, uint16_t *vin);
int t4_free_vi(struct adapter *adap, unsigned int mbox,
	       unsigned int pf, unsigned int vf,
	       unsigned int viid);
int t4_set_rxmode(struct adapter *adap, unsigned int mbox, unsigned int viid,
		  int mtu, int promisc, int all_multi, int bcast, int vlanex,
		  bool sleep_ok);
int t4_alloc_mac_filt(struct adapter *adap, unsigned int mbox, unsigned int viid,
		      bool free, unsigned int naddr, const u8 **addr, u16 *idx,
		      u64 *hash, bool sleep_ok);
int t4_change_mac(struct adapter *adap, unsigned int mbox, unsigned int viid,
		  int idx, const u8 *addr, bool persist, uint16_t *smt_idx);
int t4_set_addr_hash(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     bool ucast, u64 vec, bool sleep_ok);
int t4_enable_vi_params(struct adapter *adap, unsigned int mbox,
			unsigned int viid, bool rx_en, bool tx_en, bool dcb_en);
int t4_enable_vi(struct adapter *adap, unsigned int mbox, unsigned int viid,
		 bool rx_en, bool tx_en);
int t4_identify_port(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     unsigned int nblinks);
int t4_mdio_rd(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, unsigned int *valp);
int t4_mdio_wr(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, unsigned int val);
int t4_i2c_rd(struct adapter *adap, unsigned int mbox,
	      int port, unsigned int devid,
	      unsigned int offset, unsigned int len,
	      u8 *buf);
int t4_i2c_wr(struct adapter *adap, unsigned int mbox,
	      int port, unsigned int devid,
	      unsigned int offset, unsigned int len,
	      u8 *buf);
int t4_iq_stop(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int iqtype, unsigned int iqid,
	       unsigned int fl0id, unsigned int fl1id);
int t4_iq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int iqtype, unsigned int iqid,
	       unsigned int fl0id, unsigned int fl1id);
int t4_eth_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		   unsigned int vf, unsigned int eqid);
int t4_ctrl_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid);
int t4_ofld_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid);
int t4_sge_ctxt_rd(struct adapter *adap, unsigned int mbox, unsigned int cid,
		   enum ctxt_type ctype, u32 *data);
int t4_sge_ctxt_rd_bd(struct adapter *adap, unsigned int cid, enum ctxt_type ctype,
		      u32 *data);
int t4_sge_ctxt_flush(struct adapter *adap, unsigned int mbox);
const char *t4_link_down_rc_str(unsigned char link_down_rc);
int t4_update_port_info(struct port_info *pi);
int t4_handle_fw_rpl(struct adapter *adap, const __be64 *rpl);
int t4_fwaddrspace_write(struct adapter *adap, unsigned int mbox, u32 addr, u32 val);
int t4_sched_config(struct adapter *adapter, int type, int minmaxen,
		    int sleep_ok);
int t4_sched_params(struct adapter *adapter, int type, int level, int mode,
		    int rateunit, int ratemode, int channel, int cl,
		    int minrate, int maxrate, int weight, int pktsize,
		    int burstsize, int sleep_ok);
int t4_sched_params_ch_rl(struct adapter *adapter, int channel, int ratemode,
			  unsigned int maxrate, int sleep_ok);
int t4_sched_params_cl_wrr(struct adapter *adapter, int channel, int cl,
			   int weight, int sleep_ok);
int t4_sched_params_cl_rl_kbps(struct adapter *adapter, int channel, int cl,
			       int mode, unsigned int maxrate, int pktsize,
			       int sleep_ok);
int t4_config_watchdog(struct adapter *adapter, unsigned int mbox,
		       unsigned int pf, unsigned int vf,
		       unsigned int timeout, unsigned int action);
int t4_get_devlog_level(struct adapter *adapter, unsigned int *level);
int t4_set_devlog_level(struct adapter *adapter, unsigned int level);
void t4_sge_decode_idma_state(struct adapter *adapter, int state);

void t4_tp_pio_read(struct adapter *adap, u32 *buff, u32 nregs,
		    u32 start_index, bool sleep_ok);
void t4_tp_pio_write(struct adapter *adap, const u32 *buff, u32 nregs,
		     u32 start_index, bool sleep_ok);
void t4_tp_tm_pio_read(struct adapter *adap, u32 *buff, u32 nregs,
		       u32 start_index, bool sleep_ok);
void t4_tp_mib_read(struct adapter *adap, u32 *buff, u32 nregs,
		    u32 start_index, bool sleep_ok);

static inline int t4vf_query_params(struct adapter *adapter,
				    unsigned int nparams, const u32 *params,
				    u32 *vals)
{
	return t4_query_params(adapter, 0, 0, 0, nparams, params, vals);
}

static inline int t4vf_set_params(struct adapter *adapter,
				  unsigned int nparams, const u32 *params,
				  const u32 *vals)
{
	return t4_set_params(adapter, 0, 0, 0, nparams, params, vals);
}

static inline int t4vf_wr_mbox(struct adapter *adap, const void *cmd,
			       int size, void *rpl)
{
	return t4_wr_mbox(adap, adap->mbox, cmd, size, rpl);
}

int t4vf_wait_dev_ready(struct adapter *adapter);
int t4vf_fw_reset(struct adapter *adapter);
int t4vf_get_sge_params(struct adapter *adapter);
int t4vf_get_rss_glb_config(struct adapter *adapter);
int t4vf_get_vfres(struct adapter *adapter);
int t4vf_prep_adapter(struct adapter *adapter);
int t4_bar2_sge_qregs(struct adapter *adapter, unsigned int qid,
		enum t4_bar2_qtype qtype, int user, u64 *pbar2_qoffset,
		unsigned int *pbar2_qid);
unsigned int fwcap_to_speed(uint32_t caps);
uint32_t speed_to_fwcap(unsigned int speed);
uint32_t fwcap_top_speed(uint32_t caps);

static inline int
port_top_speed(const struct port_info *pi)
{

	/* Mbps -> Gbps */
	return (fwcap_to_speed(pi->link_cfg.supported) / 1000);
}

#endif /* __CHELSIO_COMMON_H */
