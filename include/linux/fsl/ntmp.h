/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2025-2026 NXP */
#ifndef __NETC_NTMP_H
#define __NETC_NTMP_H

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/if_ether.h>

#define NTMP_NULL_ENTRY_ID		0xffffffffU
#define IPFT_MAX_PLD_LEN		24

struct maft_keye_data {
	u8 mac_addr[ETH_ALEN];
	__le16 resv;
};

struct maft_cfge_data {
	__le16 si_bitmap;
	__le16 resv;
};

struct netc_cbdr_regs {
	void __iomem *pir;
	void __iomem *cir;
	void __iomem *mr;

	void __iomem *bar0;
	void __iomem *bar1;
	void __iomem *lenr;
};

struct netc_tbl_vers {
	u8 maft_ver;
	u8 rsst_ver;
	u8 fdbt_ver;
	u8 vft_ver;
	u8 bpt_ver;
	u8 ipft_ver;
	u8 ett_ver;
	u8 ect_ver;
};

struct netc_swcbd {
	void *buf;
	dma_addr_t dma;
	size_t size;
};

struct netc_cbdr {
	struct device *dev;
	struct netc_cbdr_regs regs;

	int bd_num;
	int next_to_use;
	int next_to_clean;

	int dma_size;
	void *addr_base;
	void *addr_base_align;
	dma_addr_t dma_base;
	dma_addr_t dma_base_align;
	struct netc_swcbd *swcbd;

	/* Serialize the order of command BD ring */
	struct mutex ring_lock;
};

struct ntmp_user {
	int cbdr_num;	/* number of control BD ring */
	struct device *dev;
	struct netc_cbdr *ring;
	struct netc_tbl_vers tbl;

	/* NTMP table bitmaps for resource management */
	u32 ett_bitmap_size;
	u32 ect_bitmap_size;
	unsigned long *ett_gid_bitmap; /* only valid for switch */
	unsigned long *ect_gid_bitmap; /* only valid for switch */
};

struct maft_entry_data {
	struct maft_keye_data keye;
	struct maft_cfge_data cfge;
};

struct ipft_pld_byte {
	u8 data;
	u8 mask;
};

struct ipft_keye_data {
	__le16 precedence;
	__le16 resv0[3];
	__le16 frm_attr_flags;
#define IPFT_FAF_OVLAN		BIT(2)
#define IPFT_FAF_IVLAN		BIT(3)
#define IPFT_FAF_IP_HDR		BIT(7)
#define IPFT_FAF_IP_VER6	BIT(8)
#define IPFT_FAF_L4_CODE	GENMASK(11, 10)
#define  IPFT_FAF_TCP_HDR	1
#define  IPFT_FAF_UDP_HDR	2
#define  IPFT_FAF_SCTP_HDR	3
#define IPFT_FAF_WOL_MAGIC	BIT(12)
	__le16 frm_attr_flags_mask;
	__le16 dscp;
#define IPFT_DSCP		GENMASK(5, 0)
#define IPFT_DSCP_MASK		GENMASK(11, 6)
#define IPFT_DSCP_MASK_ALL	0x3f
	__le16 src_port; /* This field is reserved for ENETC */
#define IPFT_SRC_PORT		GENMASK(4, 0)
#define IPFT_SRC_PORT_MASK	GENMASK(9, 5)
#define IPFT_SRC_PORT_MASK_ALL	0x1f
	__be16 outer_vlan_tci;
	__be16 outer_vlan_tci_mask;
	u8 dmac[ETH_ALEN];
	u8 dmac_mask[ETH_ALEN];
	u8 smac[ETH_ALEN];
	u8 smac_mask[ETH_ALEN];
	__be16 inner_vlan_tci;
	__be16 inner_vlan_tci_mask;
	__be16 ethertype;
	__be16 ethertype_mask;
	u8 ip_protocol;
	u8 ip_protocol_mask;
	__le16 resv1[7];
	__be32 ip_src[4];
	__le32 resv2[2];
	__be32 ip_src_mask[4];
	__be16 l4_src_port;
	__be16 l4_src_port_mask;
	__le32 resv3;
	__be32 ip_dst[4];
	__le32 resv4[2];
	__be32 ip_dst_mask[4];
	__be16 l4_dst_port;
	__be16 l4_dst_port_mask;
	__le32 resv5;
	struct ipft_pld_byte byte[IPFT_MAX_PLD_LEN];
};

struct ipft_cfge_data {
	__le32 cfg;
#define IPFT_IPV		GENMASK(3, 0)
#define IPFT_OIPV		BIT(4)
#define IPFT_DR			GENMASK(6, 5)
#define IPFT_ODR		BIT(7)
#define IPFT_FLTFA		GENMASK(10, 8)
#define  IPFT_FLTFA_DISCARD	0
#define  IPFT_FLTFA_PERMIT	1
/* Redirect is only for switch */
#define  IPFT_FLTFA_REDIRECT	2
#define IPFT_IMIRE		BIT(11)
#define IPFT_WOLTE		BIT(12)
#define IPFT_FLTA		GENMASK(14, 13)
#define  IPFT_FLTA_RP		1
#define  IPFT_FLTA_IS		2
#define  IPFT_FLTA_SI_BITMAP	3
#define IPFT_RPR		GENMASK(16, 15)
#define IPFT_CTD		BIT(17)
#define IPFT_HR			GENMASK(21, 18)
#define IPFT_TIMECAPE		BIT(22)
#define IPFT_RRT		BIT(23)
#define IPFT_BL2F		BIT(24)
#define IPFT_EVMEID		GENMASK(31, 28)
	__le32 flta_tgt;
};

struct ipft_entry_data {
	u32 entry_id; /* hardware assigns entry ID */
	struct ipft_keye_data keye;
	struct ipft_cfge_data cfge;
};

struct fdbt_keye_data {
	u8 mac_addr[ETH_ALEN]; /* big-endian */
	__le16 resv0;
	__le16 fid;
#define FDBT_FID		GENMASK(11, 0)
	__le16 resv1;
};

struct fdbt_cfge_data {
	__le32 port_bitmap;
#define FDBT_PORT_BITMAP	GENMASK(23, 0)
	__le32 cfg;
#define FDBT_OETEID		GENMASK(1, 0)
#define FDBT_EPORT		GENMASK(6, 2)
#define FDBT_IMIRE		BIT(7)
#define FDBT_CTD		GENMASK(10, 9)
#define FDBT_DYNAMIC		BIT(11)
#define FDBT_TIMECAPE		BIT(12)
	__le32 et_eid;
};

struct fdbt_entry_data {
	u32 entry_id;
	struct fdbt_keye_data keye;
	struct fdbt_cfge_data cfge;
	u8 acte;
#define FDBT_ACT_CNT		GENMASK(6, 0)
#define FDBT_ACT_FLAG		BIT(7)
};

struct vft_cfge_data {
	__le32 bitmap_stg;
#define VFT_PORT_MEMBERSHIP	GENMASK(23, 0)
#define VFT_STG_ID_MASK		GENMASK(27, 24)
#define VFT_STG_ID(g)		FIELD_PREP(VFT_STG_ID_MASK, (g))
	__le16 fid;
#define VFT_FID			GENMASK(11, 0)
	__le16 cfg;
#define VFT_MLO			GENMASK(2, 0)
#define VFT_MFO			GENMASK(4, 3)
#define VFT_IPMFE		BIT(6)
#define VFT_IPMFLE		BIT(7)
#define VFT_PGA			BIT(8)
#define VFT_SFDA		BIT(10)
#define VFT_OSFDA		BIT(11)
#define VFT_FDBAFSS		BIT(12)
	__le32 eta_port_bitmap;
#define VFT_ETA_PORT_BITMAP	GENMASK(23, 0)
	__le32 et_eid;
};

struct ett_cfge_data {
	__le16 efm_cfg;
#define ETT_EFM_MODE		GENMASK(1, 0)
#define ETT_ESQA		GENMASK(5, 4)
#define ETT_ECA			GENMASK(8, 6)
#define ETT_ECA_INC		1
#define ETT_EFM_LEN_CHANGE	GENMASK(15, 9)
#define ETT_FRM_LEN_DEL_VLAN	0x7c
#define ETT_FRM_LEN_DEL_RTAG	0x7a
#define ETT_FRM_LEN_DEL_VLAN_RTAG	0x76
	__le16 efm_data_len;
#define ETT_EFM_DATA_LEN	GENMASK(10, 0)
	__le32 efm_eid;
	__le32 ec_eid;
	__le32 esqa_tgt_eid;
};

struct bpt_bpse_data {
	__le32 amount_used;
	__le32 amount_used_hwm;
	u8 bpd_fc_state;
#define BPT_FC_STATE		BIT(0)
#define BPT_BPD			BIT(1)
} __packed;

struct bpt_cfge_data {
	u8 fccfg_sbpen;
#define BPT_FC_CFG		GENMASK(2, 1)
#define  BPT_FC_CFG_EN_BPFC	1
	u8 pfc_vector;
	__le16 max_thresh;
	__le16 fc_on_thresh;
	__le16 fc_off_thresh;
	__le16 sbp_thresh;
	__le16 resv;
	__le32 sbp_eid;
	__le32 fc_ports;
};

union ntmp_fmt_eid {
	__le32 index;
#define	FMTEID_INDEX		GENMASK(12, 0)
	__le32 vuda_sqta;
#define FMTEID_VUDA		GENMASK(1, 0)
#define FMTEID_VUDA_DEL_OTAG	2
#define FMTEID_SQTA		GENMASK(4, 2)
#define FMTEID_SQTA_DEL		2
#define FMTEID_VUDA_SQTA	BIT(13)
	__le32 vara_vid;
#define FMTEID_VID		GENMASK(11, 0)
#define FMTEID_VARA		GENMASK(13, 12)
#define FMTEID_VARA_VID		BIT(14)
};

#if IS_ENABLED(CONFIG_NXP_NETC_LIB)
int ntmp_init_cbdr(struct netc_cbdr *cbdr, struct device *dev,
		   const struct netc_cbdr_regs *regs);
void ntmp_free_cbdr(struct netc_cbdr *cbdr);
u32 ntmp_lookup_free_eid(unsigned long *bitmap, u32 size);
void ntmp_clear_eid_bitmap(unsigned long *bitmap, u32 entry_id);

/* NTMP APIs */
int ntmp_maft_add_entry(struct ntmp_user *user, u32 entry_id,
			struct maft_entry_data *maft);
int ntmp_maft_query_entry(struct ntmp_user *user, u32 entry_id,
			  struct maft_entry_data *maft);
int ntmp_maft_delete_entry(struct ntmp_user *user, u32 entry_id);
int ntmp_rsst_update_entry(struct ntmp_user *user, const u32 *table,
			   int count);
int ntmp_rsst_query_entry(struct ntmp_user *user,
			  u32 *table, int count);
int ntmp_ipft_add_entry(struct ntmp_user *user,
			struct ipft_entry_data *entry);
int ntmp_ipft_delete_entry(struct ntmp_user *user, u32 entry_id);
int ntmp_fdbt_add_entry(struct ntmp_user *user, u32 *entry_id,
			const struct fdbt_keye_data *keye,
			const struct fdbt_cfge_data *cfge);
int ntmp_fdbt_update_entry(struct ntmp_user *user, u32 entry_id,
			   const struct fdbt_cfge_data *cfge);
int ntmp_fdbt_delete_entry(struct ntmp_user *user, u32 entry_id);
int ntmp_fdbt_search_port_entry(struct ntmp_user *user, int port,
				u32 *resume_entry_id,
				struct fdbt_entry_data *entry);
int ntmp_fdbt_update_activity_element(struct ntmp_user *user);
int ntmp_fdbt_delete_ageing_entries(struct ntmp_user *user, u8 act_cnt);
int ntmp_fdbt_delete_port_dynamic_entries(struct ntmp_user *user, int port);
int ntmp_vft_add_entry(struct ntmp_user *user, u16 vid,
		       const struct vft_cfge_data *cfge);
int ntmp_vft_update_entry(struct ntmp_user *user, u16 vid,
			  const struct vft_cfge_data *cfge);
int ntmp_vft_delete_entry(struct ntmp_user *user, u16 vid);
int ntmp_ett_add_entry(struct ntmp_user *user, u32 entry_id,
		       const struct ett_cfge_data *cfge);
int ntmp_ett_update_entry(struct ntmp_user *user, u32 entry_id,
			  const struct ett_cfge_data *cfge);
int ntmp_ett_delete_entry(struct ntmp_user *user, u32 entry_id);
int ntmp_ect_update_entry(struct ntmp_user *user, u32 entry_id);
int ntmp_bpt_update_entry(struct ntmp_user *user, u32 entry_id,
			  const struct bpt_cfge_data *cfge);
#else
static inline int ntmp_init_cbdr(struct netc_cbdr *cbdr, struct device *dev,
				 const struct netc_cbdr_regs *regs)
{
	return 0;
}

static inline void ntmp_free_cbdr(struct netc_cbdr *cbdr)
{
}

static inline int ntmp_maft_add_entry(struct ntmp_user *user, u32 entry_id,
				      struct maft_entry_data *maft)
{
	return 0;
}

static inline int ntmp_maft_query_entry(struct ntmp_user *user, u32 entry_id,
					struct maft_entry_data *maft)
{
	return 0;
}

static inline int ntmp_maft_delete_entry(struct ntmp_user *user, u32 entry_id)
{
	return 0;
}

static inline int ntmp_rsst_update_entry(struct ntmp_user *user,
					 const u32 *table, int count)
{
	return 0;
}

static inline int ntmp_rsst_query_entry(struct ntmp_user *user,
					u32 *table, int count)
{
	return 0;
}

#endif

#endif
