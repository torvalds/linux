/*
 * Copyright (C) 2014 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOC_TEGRA_MC_H__
#define __SOC_TEGRA_MC_H__

#include <linux/err.h>
#include <linux/reset-controller.h>
#include <linux/types.h>

struct clk;
struct device;
struct page;

struct tegra_smmu_enable {
	unsigned int reg;
	unsigned int bit;
};

struct tegra_mc_timing {
	unsigned long rate;

	u32 *emem_data;
};

/* latency allowance */
struct tegra_mc_la {
	unsigned int reg;
	unsigned int shift;
	unsigned int mask;
	unsigned int def;
};

struct tegra_mc_client {
	unsigned int id;
	const char *name;
	unsigned int swgroup;

	unsigned int fifo_size;

	struct tegra_smmu_enable smmu;
	struct tegra_mc_la la;
};

struct tegra_smmu_swgroup {
	const char *name;
	unsigned int swgroup;
	unsigned int reg;
};

struct tegra_smmu_group_soc {
	const char *name;
	const unsigned int *swgroups;
	unsigned int num_swgroups;
};

struct tegra_smmu_soc {
	const struct tegra_mc_client *clients;
	unsigned int num_clients;

	const struct tegra_smmu_swgroup *swgroups;
	unsigned int num_swgroups;

	const struct tegra_smmu_group_soc *groups;
	unsigned int num_groups;

	bool supports_round_robin_arbitration;
	bool supports_request_limit;

	unsigned int num_tlb_lines;
	unsigned int num_asids;
};

struct tegra_mc;
struct tegra_smmu;
struct gart_device;

#ifdef CONFIG_TEGRA_IOMMU_SMMU
struct tegra_smmu *tegra_smmu_probe(struct device *dev,
				    const struct tegra_smmu_soc *soc,
				    struct tegra_mc *mc);
void tegra_smmu_remove(struct tegra_smmu *smmu);
#else
static inline struct tegra_smmu *
tegra_smmu_probe(struct device *dev, const struct tegra_smmu_soc *soc,
		 struct tegra_mc *mc)
{
	return NULL;
}

static inline void tegra_smmu_remove(struct tegra_smmu *smmu)
{
}
#endif

#ifdef CONFIG_TEGRA_IOMMU_GART
struct gart_device *tegra_gart_probe(struct device *dev, struct tegra_mc *mc);
int tegra_gart_suspend(struct gart_device *gart);
int tegra_gart_resume(struct gart_device *gart);
#else
static inline struct gart_device *
tegra_gart_probe(struct device *dev, struct tegra_mc *mc)
{
	return ERR_PTR(-ENODEV);
}

static inline int tegra_gart_suspend(struct gart_device *gart)
{
	return -ENODEV;
}

static inline int tegra_gart_resume(struct gart_device *gart)
{
	return -ENODEV;
}
#endif

struct tegra_mc_reset {
	const char *name;
	unsigned long id;
	unsigned int control;
	unsigned int status;
	unsigned int reset;
	unsigned int bit;
};

struct tegra_mc_reset_ops {
	int (*hotreset_assert)(struct tegra_mc *mc,
			       const struct tegra_mc_reset *rst);
	int (*hotreset_deassert)(struct tegra_mc *mc,
				 const struct tegra_mc_reset *rst);
	int (*block_dma)(struct tegra_mc *mc,
			 const struct tegra_mc_reset *rst);
	bool (*dma_idling)(struct tegra_mc *mc,
			   const struct tegra_mc_reset *rst);
	int (*unblock_dma)(struct tegra_mc *mc,
			   const struct tegra_mc_reset *rst);
	int (*reset_status)(struct tegra_mc *mc,
			    const struct tegra_mc_reset *rst);
};

struct tegra_mc_soc {
	const struct tegra_mc_client *clients;
	unsigned int num_clients;

	const unsigned long *emem_regs;
	unsigned int num_emem_regs;

	unsigned int num_address_bits;
	unsigned int atom_size;

	u8 client_id_mask;

	const struct tegra_smmu_soc *smmu;

	u32 intmask;

	const struct tegra_mc_reset_ops *reset_ops;
	const struct tegra_mc_reset *resets;
	unsigned int num_resets;
};

struct tegra_mc {
	struct device *dev;
	struct tegra_smmu *smmu;
	struct gart_device *gart;
	void __iomem *regs;
	struct clk *clk;
	int irq;

	const struct tegra_mc_soc *soc;
	unsigned long tick;

	struct tegra_mc_timing *timings;
	unsigned int num_timings;

	struct reset_controller_dev reset;

	spinlock_t lock;
};

void tegra_mc_write_emem_configuration(struct tegra_mc *mc, unsigned long rate);
unsigned int tegra_mc_get_emem_device_count(struct tegra_mc *mc);

#endif /* __SOC_TEGRA_MC_H__ */
