/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2025 NXP */
#ifndef __NETC_NTMP_H
#define __NETC_NTMP_H

#include <linux/bitops.h>
#include <linux/if_ether.h>

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

	/* Serialize the order of command BD ring */
	spinlock_t ring_lock;
};

struct ntmp_user {
	int cbdr_num;	/* number of control BD ring */
	struct device *dev;
	struct netc_cbdr *ring;
	struct netc_tbl_vers tbl;
};

struct maft_entry_data {
	struct maft_keye_data keye;
	struct maft_cfge_data cfge;
};

#if IS_ENABLED(CONFIG_NXP_NETC_LIB)
int ntmp_init_cbdr(struct netc_cbdr *cbdr, struct device *dev,
		   const struct netc_cbdr_regs *regs);
void ntmp_free_cbdr(struct netc_cbdr *cbdr);

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
