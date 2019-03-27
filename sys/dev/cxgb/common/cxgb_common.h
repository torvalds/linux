/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007-2009, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

$FreeBSD$

***************************************************************************/
#ifndef __CHELSIO_COMMON_H
#define __CHELSIO_COMMON_H

#include <cxgb_osdep.h>

enum {
	MAX_FRAME_SIZE = 10240, /* max MAC frame size, includes header + FCS */
	EEPROMSIZE     = 8192,  /* Serial EEPROM size */
	SERNUM_LEN     = 16,    /* Serial # length */
	ECNUM_LEN      = 16,    /* EC # length */
	RSS_TABLE_SIZE = 64,    /* size of RSS lookup and mapping tables */
	TCB_SIZE       = 128,   /* TCB size */
	NMTUS          = 16,    /* size of MTU table */
	NCCTRL_WIN     = 32,    /* # of congestion control windows */
	NTX_SCHED      = 8,     /* # of HW Tx scheduling queues */
	PROTO_SRAM_LINES = 128, /* size of protocol sram */
	EXACT_ADDR_FILTERS = 8,	/* # of HW exact match filters */
};

#define MAX_RX_COALESCING_LEN 12288U

enum {
	PAUSE_RX      = 1 << 0,
	PAUSE_TX      = 1 << 1,
	PAUSE_AUTONEG = 1 << 2
};

enum {
	SUPPORTED_LINK_IRQ = 1 << 24,
	/* skip 25 */
	SUPPORTED_MISC_IRQ = 1 << 26,
	SUPPORTED_IRQ      = (SUPPORTED_LINK_IRQ | SUPPORTED_MISC_IRQ),
};

enum {                            /* adapter interrupt-maintained statistics */
	STAT_ULP_CH0_PBL_OOB,
	STAT_ULP_CH1_PBL_OOB,
	STAT_PCI_CORR_ECC,

	IRQ_NUM_STATS             /* keep last */
};

enum {
	TP_VERSION_MAJOR	= 1,
	TP_VERSION_MINOR	= 1,
	TP_VERSION_MICRO	= 0
};

#define S_TP_VERSION_MAJOR		16
#define M_TP_VERSION_MAJOR		0xFF
#define V_TP_VERSION_MAJOR(x)		((x) << S_TP_VERSION_MAJOR)
#define G_TP_VERSION_MAJOR(x)		\
	    (((x) >> S_TP_VERSION_MAJOR) & M_TP_VERSION_MAJOR)

#define S_TP_VERSION_MINOR		8
#define M_TP_VERSION_MINOR		0xFF
#define V_TP_VERSION_MINOR(x)		((x) << S_TP_VERSION_MINOR)
#define G_TP_VERSION_MINOR(x)		\
	    (((x) >> S_TP_VERSION_MINOR) & M_TP_VERSION_MINOR)

#define S_TP_VERSION_MICRO		0
#define M_TP_VERSION_MICRO		0xFF
#define V_TP_VERSION_MICRO(x)		((x) << S_TP_VERSION_MICRO)
#define G_TP_VERSION_MICRO(x)		\
	    (((x) >> S_TP_VERSION_MICRO) & M_TP_VERSION_MICRO)

enum {
	FW_VERSION_MAJOR = 7,
	FW_VERSION_MINOR = 11,
	FW_VERSION_MICRO = 0
};

enum {
	LA_CTRL = 0x80,
	LA_DATA = 0x84,
	LA_ENTRIES = 512
};

enum {
	IOQ_ENTRIES = 7
};

enum {
	SGE_QSETS = 8,            /* # of SGE Tx/Rx/RspQ sets */
	SGE_RXQ_PER_SET = 2,      /* # of Rx queues per set */
	SGE_TXQ_PER_SET = 3       /* # of Tx queues per set */
};

enum sge_context_type {           /* SGE egress context types */
	SGE_CNTXT_RDMA = 0,
	SGE_CNTXT_ETH  = 2,
	SGE_CNTXT_OFLD = 4,
	SGE_CNTXT_CTRL = 5
};

enum {
	AN_PKT_SIZE    = 32,      /* async notification packet size */
	IMMED_PKT_SIZE = 48       /* packet size for immediate data */
};

struct sg_ent {                   /* SGE scatter/gather entry */
	__be32 len[2];
	__be64 addr[2];
};

#ifndef SGE_NUM_GENBITS
/* Must be 1 or 2 */
# define SGE_NUM_GENBITS 2
#endif

#define TX_DESC_FLITS 16U
#define WR_FLITS (TX_DESC_FLITS + 1 - SGE_NUM_GENBITS)

#define MAX_PHYINTRS 4

struct cphy;

struct mdio_ops {
	int  (*read)(adapter_t *adapter, int phy_addr, int mmd_addr,
		     int reg_addr, unsigned int *val);
	int  (*write)(adapter_t *adapter, int phy_addr, int mmd_addr,
		      int reg_addr, unsigned int val);
};

struct adapter_info {
	unsigned char          nports0;        /* # of ports on channel 0 */
	unsigned char          nports1;        /* # of ports on channel 1 */
	unsigned char          phy_base_addr;  /* MDIO PHY base address */
	unsigned int           gpio_out;       /* GPIO output settings */
	unsigned char gpio_intr[MAX_PHYINTRS]; /* GPIO PHY IRQ pins */
	unsigned long          caps;           /* adapter capabilities */
	const struct mdio_ops *mdio_ops;       /* MDIO operations */
	const char            *desc;           /* product description */
};

struct mc5_stats {
	unsigned long parity_err;
	unsigned long active_rgn_full;
	unsigned long nfa_srch_err;
	unsigned long unknown_cmd;
	unsigned long reqq_parity_err;
	unsigned long dispq_parity_err;
	unsigned long del_act_empty;
};

struct mc7_stats {
	unsigned long corr_err;
	unsigned long uncorr_err;
	unsigned long parity_err;
	unsigned long addr_err;
};

struct mac_stats {
	u64 tx_octets;            /* total # of octets in good frames */
	u64 tx_octets_bad;        /* total # of octets in error frames */
	u64 tx_frames;            /* all good frames */
	u64 tx_mcast_frames;      /* good multicast frames */
	u64 tx_bcast_frames;      /* good broadcast frames */
	u64 tx_pause;             /* # of transmitted pause frames */
	u64 tx_deferred;          /* frames with deferred transmissions */
	u64 tx_late_collisions;   /* # of late collisions */
	u64 tx_total_collisions;  /* # of total collisions */
	u64 tx_excess_collisions; /* frame errors from excessive collissions */
	u64 tx_underrun;          /* # of Tx FIFO underruns */
	u64 tx_len_errs;          /* # of Tx length errors */
	u64 tx_mac_internal_errs; /* # of internal MAC errors on Tx */
	u64 tx_excess_deferral;   /* # of frames with excessive deferral */
	u64 tx_fcs_errs;          /* # of frames with bad FCS */

	u64 tx_frames_64;         /* # of Tx frames in a particular range */
	u64 tx_frames_65_127;
	u64 tx_frames_128_255;
	u64 tx_frames_256_511;
	u64 tx_frames_512_1023;
	u64 tx_frames_1024_1518;
	u64 tx_frames_1519_max;

	u64 rx_octets;            /* total # of octets in good frames */
	u64 rx_octets_bad;        /* total # of octets in error frames */
	u64 rx_frames;            /* all good frames */
	u64 rx_mcast_frames;      /* good multicast frames */
	u64 rx_bcast_frames;      /* good broadcast frames */
	u64 rx_pause;             /* # of received pause frames */
	u64 rx_fcs_errs;          /* # of received frames with bad FCS */
	u64 rx_align_errs;        /* alignment errors */
	u64 rx_symbol_errs;       /* symbol errors */
	u64 rx_data_errs;         /* data errors */
	u64 rx_sequence_errs;     /* sequence errors */
	u64 rx_runt;              /* # of runt frames */
	u64 rx_jabber;            /* # of jabber frames */
	u64 rx_short;             /* # of short frames */
	u64 rx_too_long;          /* # of oversized frames */
	u64 rx_mac_internal_errs; /* # of internal MAC errors on Rx */

	u64 rx_frames_64;         /* # of Rx frames in a particular range */
	u64 rx_frames_65_127;
	u64 rx_frames_128_255;
	u64 rx_frames_256_511;
	u64 rx_frames_512_1023;
	u64 rx_frames_1024_1518;
	u64 rx_frames_1519_max;

	u64 rx_cong_drops;        /* # of Rx drops due to SGE congestion */

	unsigned long tx_fifo_parity_err;
	unsigned long rx_fifo_parity_err;
	unsigned long tx_fifo_urun;
	unsigned long rx_fifo_ovfl;
	unsigned long serdes_signal_loss;
	unsigned long xaui_pcs_ctc_err;
	unsigned long xaui_pcs_align_change;

	unsigned long num_toggled; /* # times toggled TxEn due to stuck TX */
	unsigned long num_resets;  /* # times reset due to stuck TX */

	unsigned long link_faults;  /* # detected link faults */
};

struct tp_mib_stats {
	u32 ipInReceive_hi;
	u32 ipInReceive_lo;
	u32 ipInHdrErrors_hi;
	u32 ipInHdrErrors_lo;
	u32 ipInAddrErrors_hi;
	u32 ipInAddrErrors_lo;
	u32 ipInUnknownProtos_hi;
	u32 ipInUnknownProtos_lo;
	u32 ipInDiscards_hi;
	u32 ipInDiscards_lo;
	u32 ipInDelivers_hi;
	u32 ipInDelivers_lo;
	u32 ipOutRequests_hi;
	u32 ipOutRequests_lo;
	u32 ipOutDiscards_hi;
	u32 ipOutDiscards_lo;
	u32 ipOutNoRoutes_hi;
	u32 ipOutNoRoutes_lo;
	u32 ipReasmTimeout;
	u32 ipReasmReqds;
	u32 ipReasmOKs;
	u32 ipReasmFails;

	u32 reserved[8];

	u32 tcpActiveOpens;
	u32 tcpPassiveOpens;
	u32 tcpAttemptFails;
	u32 tcpEstabResets;
	u32 tcpOutRsts;
	u32 tcpCurrEstab;
	u32 tcpInSegs_hi;
	u32 tcpInSegs_lo;
	u32 tcpOutSegs_hi;
	u32 tcpOutSegs_lo;
	u32 tcpRetransSeg_hi;
	u32 tcpRetransSeg_lo;
	u32 tcpInErrs_hi;
	u32 tcpInErrs_lo;
	u32 tcpRtoMin;
	u32 tcpRtoMax;
};

struct tp_params {
	unsigned int nchan;          /* # of channels */
	unsigned int pmrx_size;      /* total PMRX capacity */
	unsigned int pmtx_size;      /* total PMTX capacity */
	unsigned int cm_size;        /* total CM capacity */
	unsigned int chan_rx_size;   /* per channel Rx size */
	unsigned int chan_tx_size;   /* per channel Tx size */
	unsigned int rx_pg_size;     /* Rx page size */
	unsigned int tx_pg_size;     /* Tx page size */
	unsigned int rx_num_pgs;     /* # of Rx pages */
	unsigned int tx_num_pgs;     /* # of Tx pages */
	unsigned int ntimer_qs;      /* # of timer queues */
	unsigned int tre;            /* log2 of core clocks per TP tick */
	unsigned int dack_re;        /* DACK timer resolution */
};

struct qset_params {                   /* SGE queue set parameters */
	unsigned int polling;          /* polling/interrupt service for rspq */
	unsigned int lro;              /* large receive offload */
	unsigned int coalesce_usecs;   /* irq coalescing timer */
	unsigned int rspq_size;        /* # of entries in response queue */
	unsigned int fl_size;          /* # of entries in regular free list */
	unsigned int jumbo_size;       /* # of entries in jumbo free list */
	unsigned int jumbo_buf_size;   /* buffer size of jumbo entry */
	unsigned int txq_size[SGE_TXQ_PER_SET];  /* Tx queue sizes */
	unsigned int cong_thres;       /* FL congestion threshold */
	unsigned int vector;           /* Interrupt (line or vector) number */
};

struct sge_params {
	unsigned int max_pkt_size;     /* max offload pkt size */
	struct qset_params qset[SGE_QSETS];
};

struct mc5_params {
	unsigned int mode;       /* selects MC5 width */
	unsigned int nservers;   /* size of server region */
	unsigned int nfilters;   /* size of filter region */
	unsigned int nroutes;    /* size of routing region */
};

/* Default MC5 region sizes */
enum {
	DEFAULT_NSERVERS = 512,
	DEFAULT_NFILTERS = 128
};

/* MC5 modes, these must be non-0 */
enum {
	MC5_MODE_144_BIT = 1,
	MC5_MODE_72_BIT  = 2
};

/* MC5 min active region size */
enum { MC5_MIN_TIDS = 16 };

struct vpd_params {
	unsigned int cclk;
	unsigned int mclk;
	unsigned int uclk;
	unsigned int mdc;
	unsigned int mem_timing;
	u8 sn[SERNUM_LEN + 1];
	u8 ec[ECNUM_LEN + 1];
	u8 eth_base[6];
	u8 port_type[MAX_NPORTS];
	unsigned short xauicfg[2];
};

struct generic_vpd {
	u32 offset;
	u32 len;
	u8 *data;
};

enum { MAX_VPD_BYTES = 32000 };

struct pci_params {
	unsigned int   vpd_cap_addr;
	unsigned int   pcie_cap_addr;
	unsigned short speed;
	unsigned char  width;
	unsigned char  variant;
};

enum {
	PCI_VARIANT_PCI,
	PCI_VARIANT_PCIX_MODE1_PARITY,
	PCI_VARIANT_PCIX_MODE1_ECC,
	PCI_VARIANT_PCIX_266_MODE2,
	PCI_VARIANT_PCIE
};

struct adapter_params {
	struct sge_params sge;
	struct mc5_params mc5;
	struct tp_params  tp;
	struct vpd_params vpd;
	struct pci_params pci;

	const struct adapter_info *info;

	unsigned short mtus[NMTUS];
	unsigned short a_wnd[NCCTRL_WIN];
	unsigned short b_wnd[NCCTRL_WIN];
	unsigned int   nports;              /* # of ethernet ports */
	unsigned int   chan_map;            /* bitmap of in-use Tx channels */
	unsigned int   stats_update_period; /* MAC stats accumulation period */
	unsigned int   linkpoll_period;     /* link poll period in 0.1s */
	unsigned int   rev;                 /* chip revision */
	unsigned int   offload;
};

enum {					    /* chip revisions */
	T3_REV_A  = 0,
	T3_REV_B  = 2,
	T3_REV_B2 = 3,
	T3_REV_C  = 4,
};

struct trace_params {
	u32 sip;
	u32 sip_mask;
	u32 dip;
	u32 dip_mask;
	u16 sport;
	u16 sport_mask;
	u16 dport;
	u16 dport_mask;
	u32 vlan:12;
	u32 vlan_mask:12;
	u32 intf:4;
	u32 intf_mask:4;
	u8  proto;
	u8  proto_mask;
};

struct link_config {
	unsigned int   supported;        /* link capabilities */
	unsigned int   advertising;      /* advertised capabilities */
	unsigned short requested_speed;  /* speed user has requested */
	unsigned short speed;            /* actual link speed */
	unsigned char  requested_duplex; /* duplex user has requested */
	unsigned char  duplex;           /* actual link duplex */
	unsigned char  requested_fc;     /* flow control user has requested */
	unsigned char  fc;               /* actual link flow control */
	unsigned char  autoneg;          /* autonegotiating? */
	unsigned int   link_ok;          /* link up? */
};

#define SPEED_INVALID   0xffff
#define DUPLEX_INVALID  0xff

struct mc5 {
	adapter_t *adapter;
	unsigned int tcam_size;
	unsigned char part_type;
	unsigned char parity_enabled;
	unsigned char mode;
	struct mc5_stats stats;
};

static inline unsigned int t3_mc5_size(const struct mc5 *p)
{
	return p->tcam_size;
}

struct mc7 {
	adapter_t *adapter;     /* backpointer to adapter */
	unsigned int size;      /* memory size in bytes */
	unsigned int width;     /* MC7 interface width */
	unsigned int offset;    /* register address offset for MC7 instance */
	const char *name;       /* name of MC7 instance */
	struct mc7_stats stats; /* MC7 statistics */
};

static inline unsigned int t3_mc7_size(const struct mc7 *p)
{
	return p->size;
}

struct cmac {
	adapter_t *adapter;
	unsigned int offset;
	unsigned char nucast;    /* # of address filters for unicast MACs */
	unsigned char multiport; /* multiple ports connected to this MAC */
	unsigned char ext_port;  /* external MAC port */
	unsigned char promisc_map;  /* which external ports are promiscuous */
	unsigned int tx_tcnt;
	unsigned int tx_xcnt;
	u64 tx_mcnt;
	unsigned int rx_xcnt;
	unsigned int rx_ocnt;
	u64 rx_mcnt;
	unsigned int toggle_cnt;
	unsigned int txen;
	unsigned int was_reset;
	u64 rx_pause;
	struct mac_stats stats;
};

enum {
	MAC_DIRECTION_RX = 1,
	MAC_DIRECTION_TX = 2,
	MAC_RXFIFO_SIZE  = 32768
};

/* IEEE 802.3 specified MDIO devices */
enum {
	MDIO_DEV_PMA_PMD = 1,
	MDIO_DEV_WIS     = 2,
	MDIO_DEV_PCS     = 3,
	MDIO_DEV_XGXS    = 4,
	MDIO_DEV_ANEG    = 7,
	MDIO_DEV_VEND1   = 30,
	MDIO_DEV_VEND2   = 31
};

/* LASI control and status registers */
enum {
	RX_ALARM_CTRL = 0x9000,
	TX_ALARM_CTRL = 0x9001,
	LASI_CTRL     = 0x9002,
	RX_ALARM_STAT = 0x9003,
	TX_ALARM_STAT = 0x9004,
	LASI_STAT     = 0x9005
};

/* PHY loopback direction */
enum {
	PHY_LOOPBACK_TX = 1,
	PHY_LOOPBACK_RX = 2
};

/* PHY interrupt types */
enum {
	cphy_cause_link_change = 1,
	cphy_cause_fifo_error = 2,
	cphy_cause_module_change = 4,
	cphy_cause_alarm = 8,
};

/* PHY module types */
enum {
	phy_modtype_none,
	phy_modtype_sr,
	phy_modtype_lr,
	phy_modtype_lrm,
	phy_modtype_twinax,
	phy_modtype_twinax_long,
	phy_modtype_unknown
};

enum {
	PHY_LINK_DOWN = 0,
	PHY_LINK_UP,
	PHY_LINK_PARTIAL
};

/* PHY operations */
struct cphy_ops {
	int (*reset)(struct cphy *phy, int wait);

	int (*intr_enable)(struct cphy *phy);
	int (*intr_disable)(struct cphy *phy);
	int (*intr_clear)(struct cphy *phy);
	int (*intr_handler)(struct cphy *phy);

	int (*autoneg_enable)(struct cphy *phy);
	int (*autoneg_restart)(struct cphy *phy);

	int (*advertise)(struct cphy *phy, unsigned int advertise_map);
	int (*set_loopback)(struct cphy *phy, int mmd, int dir, int enable);
	int (*set_speed_duplex)(struct cphy *phy, int speed, int duplex);
	int (*get_link_status)(struct cphy *phy, int *link_state, int *speed,
			       int *duplex, int *fc);
	int (*power_down)(struct cphy *phy, int enable);
};

/* A PHY instance */
struct cphy {
	u8 addr;                             /* PHY address */
	u8 modtype;                          /* PHY module type */
	u8 rst;
	unsigned int priv;                   /* scratch pad */
	unsigned int caps;                   /* PHY capabilities */
	adapter_t *adapter;                  /* associated adapter */
	pinfo_t *pinfo;                      /* associated port */
	const char *desc;                    /* PHY description */
	unsigned long fifo_errors;           /* FIFO over/under-flows */
	const struct cphy_ops *ops;          /* PHY operations */
	int (*mdio_read)(adapter_t *adapter, int phy_addr, int mmd_addr,
			 int reg_addr, unsigned int *val);
	int (*mdio_write)(adapter_t *adapter, int phy_addr, int mmd_addr,
			  int reg_addr, unsigned int val);
};

/* Convenience MDIO read/write wrappers */
static inline int mdio_read(struct cphy *phy, int mmd, int reg,
			    unsigned int *valp)
{
	return phy->mdio_read(phy->adapter, phy->addr, mmd, reg, valp);
}

static inline int mdio_write(struct cphy *phy, int mmd, int reg,
			     unsigned int val)
{
	return phy->mdio_write(phy->adapter, phy->addr, mmd, reg, val);
}

/* Convenience initializer */
static inline void cphy_init(struct cphy *phy, adapter_t *adapter, pinfo_t *pinfo,
			     int phy_addr, struct cphy_ops *phy_ops,
			     const struct mdio_ops *mdio_ops, unsigned int caps,
			     const char *desc)
{
	phy->addr    = (u8)phy_addr;
	phy->caps    = caps;
	phy->adapter = adapter;
	phy->pinfo   = pinfo;
	phy->desc    = desc;
	phy->ops     = phy_ops;
	if (mdio_ops) {
		phy->mdio_read  = mdio_ops->read;
		phy->mdio_write = mdio_ops->write;
	}
}

/* Accumulate MAC statistics every 180 seconds.  For 1G we multiply by 10. */
#define MAC_STATS_ACCUM_SECS 180

/* The external MAC needs accumulation every 30 seconds */
#define VSC_STATS_ACCUM_SECS 30

#define XGM_REG(reg_addr, idx) \
	((reg_addr) + (idx) * (XGMAC0_1_BASE_ADDR - XGMAC0_0_BASE_ADDR))

struct addr_val_pair {
	unsigned int reg_addr;
	unsigned int val;
};

#include <cxgb_adapter.h>

#ifndef PCI_VENDOR_ID_CHELSIO
# define PCI_VENDOR_ID_CHELSIO 0x1425
#endif

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; ++iter)

#define adapter_info(adap) ((adap)->params.info)

static inline int uses_xaui(const adapter_t *adap)
{
	return adapter_info(adap)->caps & SUPPORTED_AUI;
}

static inline int is_10G(const adapter_t *adap)
{
	return adapter_info(adap)->caps & SUPPORTED_10000baseT_Full;
}

static inline int is_offload(const adapter_t *adap)
{
	return adap->params.offload;
}

static inline unsigned int core_ticks_per_usec(const adapter_t *adap)
{
	return adap->params.vpd.cclk / 1000;
}

static inline unsigned int dack_ticks_to_usec(const adapter_t *adap,
					      unsigned int ticks)
{
	return (ticks << adap->params.tp.dack_re) / core_ticks_per_usec(adap);
}

static inline unsigned int is_pcie(const adapter_t *adap)
{
	return adap->params.pci.variant == PCI_VARIANT_PCIE;
}

void t3_set_reg_field(adapter_t *adap, unsigned int addr, u32 mask, u32 val);
void t3_write_regs(adapter_t *adapter, const struct addr_val_pair *p, int n,
		   unsigned int offset);
int t3_wait_op_done_val(adapter_t *adapter, int reg, u32 mask, int polarity,
			int attempts, int delay, u32 *valp);

static inline int t3_wait_op_done(adapter_t *adapter, int reg, u32 mask,
				  int polarity, int attempts, int delay)
{
	return t3_wait_op_done_val(adapter, reg, mask, polarity, attempts,
				   delay, NULL);
}

int t3_mdio_change_bits(struct cphy *phy, int mmd, int reg, unsigned int clear,
			unsigned int set);
int t3_phy_reset(struct cphy *phy, int mmd, int wait);
int t3_phy_advertise(struct cphy *phy, unsigned int advert);
int t3_phy_advertise_fiber(struct cphy *phy, unsigned int advert);
int t3_set_phy_speed_duplex(struct cphy *phy, int speed, int duplex);
int t3_phy_lasi_intr_enable(struct cphy *phy);
int t3_phy_lasi_intr_disable(struct cphy *phy);
int t3_phy_lasi_intr_clear(struct cphy *phy);
int t3_phy_lasi_intr_handler(struct cphy *phy);

void t3_intr_enable(adapter_t *adapter);
void t3_intr_disable(adapter_t *adapter);
void t3_intr_clear(adapter_t *adapter);
void t3_xgm_intr_enable(adapter_t *adapter, int idx);
void t3_xgm_intr_disable(adapter_t *adapter, int idx);
void t3_port_intr_enable(adapter_t *adapter, int idx);
void t3_port_intr_disable(adapter_t *adapter, int idx);
void t3_port_intr_clear(adapter_t *adapter, int idx);
int t3_slow_intr_handler(adapter_t *adapter);

void t3_link_changed(adapter_t *adapter, int port_id);
int t3_link_start(struct cphy *phy, struct cmac *mac, struct link_config *lc);
const struct adapter_info *t3_get_adapter_info(unsigned int board_id);
int t3_seeprom_read(adapter_t *adapter, u32 addr, u32 *data);
int t3_seeprom_write(adapter_t *adapter, u32 addr, u32 data);
int t3_seeprom_wp(adapter_t *adapter, int enable);
int t3_get_vpd_len(adapter_t *adapter, struct generic_vpd *vpd);
int t3_read_vpd(adapter_t *adapter, struct generic_vpd *vpd);
int t3_read_flash(adapter_t *adapter, unsigned int addr, unsigned int nwords,
		  u32 *data, int byte_oriented);
int t3_get_tp_version(adapter_t *adapter, u32 *vers);
int t3_check_tpsram_version(adapter_t *adapter);
int t3_check_tpsram(adapter_t *adapter, const u8 *tp_ram, unsigned int size);
int t3_load_fw(adapter_t *adapter, const u8 *fw_data, unsigned int size);
int t3_get_fw_version(adapter_t *adapter, u32 *vers);
int t3_check_fw_version(adapter_t *adapter);
int t3_load_boot(adapter_t *adapter, u8 *fw_data, unsigned int size);
int t3_init_hw(adapter_t *adapter, u32 fw_params);
void mac_prep(struct cmac *mac, adapter_t *adapter, int index);
void early_hw_init(adapter_t *adapter, const struct adapter_info *ai);
int t3_reset_adapter(adapter_t *adapter);
int t3_prep_adapter(adapter_t *adapter, const struct adapter_info *ai, int reset);
int t3_reinit_adapter(adapter_t *adap);
void t3_led_ready(adapter_t *adapter);
void t3_fatal_err(adapter_t *adapter);
void t3_set_vlan_accel(adapter_t *adapter, unsigned int ports, int on);
void t3_enable_filters(adapter_t *adap);
void t3_disable_filters(adapter_t *adap);
void t3_tp_set_offload_mode(adapter_t *adap, int enable);
void t3_config_rss(adapter_t *adapter, unsigned int rss_config, const u8 *cpus,
		   const u16 *rspq);
int t3_read_rss(adapter_t *adapter, u8 *lkup, u16 *map);
int t3_set_proto_sram(adapter_t *adap, const u8 *data);
int t3_mps_set_active_ports(adapter_t *adap, unsigned int port_mask);
void t3_port_failover(adapter_t *adapter, int port);
void t3_failover_done(adapter_t *adapter, int port);
void t3_failover_clear(adapter_t *adapter);
int t3_cim_ctl_blk_read(adapter_t *adap, unsigned int addr, unsigned int n,
			unsigned int *valp);
int t3_mc7_bd_read(struct mc7 *mc7, unsigned int start, unsigned int n,
		   u64 *buf);

int t3_mac_init(struct cmac *mac);
void t3b_pcs_reset(struct cmac *mac);
void t3c_pcs_force_los(struct cmac *mac);
void t3_mac_disable_exact_filters(struct cmac *mac);
void t3_mac_enable_exact_filters(struct cmac *mac);
int t3_mac_enable(struct cmac *mac, int which);
int t3_mac_disable(struct cmac *mac, int which);
int t3_mac_set_mtu(struct cmac *mac, unsigned int mtu);
int t3_mac_set_rx_mode(struct cmac *mac, struct t3_rx_mode *rm);
int t3_mac_set_address(struct cmac *mac, unsigned int idx, u8 addr[6]);
int t3_mac_set_num_ucast(struct cmac *mac, unsigned char n);
const struct mac_stats *t3_mac_update_stats(struct cmac *mac);
int t3_mac_set_speed_duplex_fc(struct cmac *mac, int speed, int duplex,
			       int fc);
int t3b2_mac_watchdog_task(struct cmac *mac);

void t3_mc5_prep(adapter_t *adapter, struct mc5 *mc5, int mode);
int t3_mc5_init(struct mc5 *mc5, unsigned int nservers, unsigned int nfilters,
		unsigned int nroutes);
void t3_mc5_intr_handler(struct mc5 *mc5);
int t3_read_mc5_range(const struct mc5 *mc5, unsigned int start, unsigned int n,
		      u32 *buf);

int t3_tp_set_coalescing_size(adapter_t *adap, unsigned int size, int psh);
void t3_tp_set_max_rxsize(adapter_t *adap, unsigned int size);
void t3_tp_get_mib_stats(adapter_t *adap, struct tp_mib_stats *tps);
void t3_load_mtus(adapter_t *adap, unsigned short mtus[NMTUS],
		  unsigned short alpha[NCCTRL_WIN],
		  unsigned short beta[NCCTRL_WIN], unsigned short mtu_cap);
void t3_read_hw_mtus(adapter_t *adap, unsigned short mtus[NMTUS]);
void t3_get_cong_cntl_tab(adapter_t *adap,
			  unsigned short incr[NMTUS][NCCTRL_WIN]);
void t3_config_trace_filter(adapter_t *adapter, const struct trace_params *tp,
			    int filter_index, int invert, int enable);
void t3_query_trace_filter(adapter_t *adapter, struct trace_params *tp,
			   int filter_index, int *inverted, int *enabled);
int t3_config_sched(adapter_t *adap, unsigned int kbps, int sched);
int t3_set_sched_ipg(adapter_t *adap, int sched, unsigned int ipg);
void t3_get_tx_sched(adapter_t *adap, unsigned int sched, unsigned int *kbps,
		     unsigned int *ipg);
void t3_read_pace_tbl(adapter_t *adap, unsigned int pace_vals[NTX_SCHED]);
void t3_set_pace_tbl(adapter_t *adap, unsigned int *pace_vals,
		     unsigned int start, unsigned int n);

int t3_get_up_la(adapter_t *adapter, u32 *stopped, u32 *index,
		 u32 *size, void *data);
int t3_get_up_ioqs(adapter_t *adapter, u32 *size, void *data);

void t3_sge_prep(adapter_t *adap, struct sge_params *p);
void t3_sge_init(adapter_t *adap, struct sge_params *p);
int t3_sge_init_ecntxt(adapter_t *adapter, unsigned int id, int gts_enable,
		       enum sge_context_type type, int respq, u64 base_addr,
		       unsigned int size, unsigned int token, int gen,
		       unsigned int cidx);
int t3_sge_init_flcntxt(adapter_t *adapter, unsigned int id, int gts_enable,
			u64 base_addr, unsigned int size, unsigned int esize,
			unsigned int cong_thres, int gen, unsigned int cidx);
int t3_sge_init_rspcntxt(adapter_t *adapter, unsigned int id, int irq_vec_idx,
			 u64 base_addr, unsigned int size,
			 unsigned int fl_thres, int gen, unsigned int cidx);
int t3_sge_init_cqcntxt(adapter_t *adapter, unsigned int id, u64 base_addr,
 			unsigned int size, int rspq, int ovfl_mode,
			unsigned int credits, unsigned int credit_thres);
int t3_sge_enable_ecntxt(adapter_t *adapter, unsigned int id, int enable);
int t3_sge_disable_fl(adapter_t *adapter, unsigned int id);
int t3_sge_disable_rspcntxt(adapter_t *adapter, unsigned int id);
int t3_sge_disable_cqcntxt(adapter_t *adapter, unsigned int id);
int t3_sge_read_ecntxt(adapter_t *adapter, unsigned int id, u32 data[4]);
int t3_sge_read_fl(adapter_t *adapter, unsigned int id, u32 data[4]);
int t3_sge_read_cq(adapter_t *adapter, unsigned int id, u32 data[4]);
int t3_sge_read_rspq(adapter_t *adapter, unsigned int id, u32 data[4]);
int t3_sge_cqcntxt_op(adapter_t *adapter, unsigned int id, unsigned int op,
		      unsigned int credits);

int t3_elmr_blk_write(adapter_t *adap, int start, const u32 *vals, int n);
int t3_elmr_blk_read(adapter_t *adap, int start, u32 *vals, int n);
int t3_vsc7323_init(adapter_t *adap, int nports);
int t3_vsc7323_set_speed_fc(adapter_t *adap, int speed, int fc, int port);
int t3_vsc7323_set_mtu(adapter_t *adap, unsigned int mtu, int port);
int t3_vsc7323_set_addr(adapter_t *adap, u8 addr[6], int port);
int t3_vsc7323_enable(adapter_t *adap, int port, int which);
int t3_vsc7323_disable(adapter_t *adap, int port, int which);
const struct mac_stats *t3_vsc7323_update_stats(struct cmac *mac);

int t3_i2c_read8(adapter_t *adapter, int chained, u8 *valp);
int t3_i2c_write8(adapter_t *adapter, int chained, u8 val);

int t3_mi1_read(adapter_t *adapter, int phy_addr, int mmd_addr, int reg_addr,
		unsigned int *valp);
int t3_mi1_write(adapter_t *adapter, int phy_addr, int mmd_addr, int reg_addr,
		 unsigned int val);

int t3_mv88e1xxx_phy_prep(pinfo_t *pinfo, int phy_addr,
			  const struct mdio_ops *mdio_ops);
int t3_vsc8211_phy_prep(pinfo_t *pinfo, int phy_addr,
			const struct mdio_ops *mdio_ops);
int t3_vsc8211_fifo_depth(adapter_t *adap, unsigned int mtu, int port);
int t3_ael1002_phy_prep(pinfo_t *pinfo, int phy_addr,
			const struct mdio_ops *mdio_ops);
int t3_ael1006_phy_prep(pinfo_t *pinfo, int phy_addr,
			const struct mdio_ops *mdio_ops);
int t3_ael2005_phy_prep(pinfo_t *pinfo, int phy_addr,
			const struct mdio_ops *mdio_ops);
int t3_ael2020_phy_prep(pinfo_t *pinfo, int phy_addr,
			const struct mdio_ops *mdio_ops);
int t3_qt2045_phy_prep(pinfo_t *pinfo, int phy_addr,
		       const struct mdio_ops *mdio_ops);
int t3_tn1010_phy_prep(pinfo_t *pinfo, int phy_addr,
		       const struct mdio_ops *mdio_ops);
int t3_xaui_direct_phy_prep(pinfo_t *pinfo, int phy_addr,
			    const struct mdio_ops *mdio_ops);
int t3_aq100x_phy_prep(pinfo_t *pinfo, int phy_addr,
		       const struct mdio_ops *mdio_ops);
#endif /* __CHELSIO_COMMON_H */
