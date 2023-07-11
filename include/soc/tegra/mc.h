/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 NVIDIA Corporation
 */

#ifndef __SOC_TEGRA_MC_H__
#define __SOC_TEGRA_MC_H__

#include <linux/bits.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/interconnect-provider.h>
#include <linux/irq.h>
#include <linux/reset-controller.h>
#include <linux/types.h>
#include <linux/tegra-icc.h>

struct clk;
struct device;
struct page;

struct tegra_mc_timing {
	unsigned long rate;

	u32 *emem_data;
};

struct tegra_mc_client {
	unsigned int id;
	unsigned int bpmp_id;
	enum tegra_icc_client_type type;
	const char *name;
	/*
	 * For Tegra210 and earlier, this is the SWGROUP ID used for IOVA translations in the
	 * Tegra SMMU, whereas on Tegra186 and later this is the ID used to override the ARM SMMU
	 * stream ID used for IOVA translations for the given memory client.
	 */
	union {
		unsigned int swgroup;
		unsigned int sid;
	};

	unsigned int fifo_size;

	struct {
		/* Tegra SMMU enable (Tegra210 and earlier) */
		struct {
			unsigned int reg;
			unsigned int bit;
		} smmu;

		/* latency allowance */
		struct {
			unsigned int reg;
			unsigned int shift;
			unsigned int mask;
			unsigned int def;
		} la;

		/* stream ID overrides (Tegra186 and later) */
		struct {
			unsigned int override;
			unsigned int security;
		} sid;
	} regs;
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

#define TEGRA_MC_ICC_TAG_DEFAULT				0
#define TEGRA_MC_ICC_TAG_ISO					BIT(0)

struct tegra_mc_icc_ops {
	int (*set)(struct icc_node *src, struct icc_node *dst);
	int (*aggregate)(struct icc_node *node, u32 tag, u32 avg_bw,
			 u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
	struct icc_node* (*xlate)(struct of_phandle_args *spec, void *data);
	struct icc_node_data *(*xlate_extended)(struct of_phandle_args *spec,
						void *data);
	int (*get_bw)(struct icc_node *node, u32 *avg, u32 *peak);
};

struct tegra_mc_ops {
	/*
	 * @probe: Callback to set up SoC-specific bits of the memory controller. This is called
	 * after basic, common set up that is done by the SoC-agnostic bits.
	 */
	int (*probe)(struct tegra_mc *mc);
	void (*remove)(struct tegra_mc *mc);
	int (*suspend)(struct tegra_mc *mc);
	int (*resume)(struct tegra_mc *mc);
	irqreturn_t (*handle_irq)(int irq, void *data);
	int (*probe_device)(struct tegra_mc *mc, struct device *dev);
};

struct tegra_mc_soc {
	const struct tegra_mc_client *clients;
	unsigned int num_clients;

	const unsigned long *emem_regs;
	unsigned int num_emem_regs;

	unsigned int num_address_bits;
	unsigned int atom_size;

	unsigned int num_carveouts;

	u16 client_id_mask;
	u8 num_channels;

	const struct tegra_smmu_soc *smmu;

	u32 intmask;
	u32 ch_intmask;
	u32 global_intstatus_channel_shift;
	bool has_addr_hi_reg;

	const struct tegra_mc_reset_ops *reset_ops;
	const struct tegra_mc_reset *resets;
	unsigned int num_resets;

	const struct tegra_mc_icc_ops *icc_ops;
	const struct tegra_mc_ops *ops;
};

struct tegra_mc {
	struct tegra_bpmp *bpmp;
	struct device *dev;
	struct tegra_smmu *smmu;
	struct gart_device *gart;
	void __iomem *regs;
	void __iomem *bcast_ch_regs;
	void __iomem **ch_regs;
	struct clk *clk;
	int irq;

	const struct tegra_mc_soc *soc;
	unsigned long tick;

	struct tegra_mc_timing *timings;
	unsigned int num_timings;
	unsigned int num_channels;

	bool bwmgr_mrq_supported;
	struct reset_controller_dev reset;

	struct icc_provider provider;

	spinlock_t lock;

	struct {
		struct dentry *root;
	} debugfs;
};

int tegra_mc_write_emem_configuration(struct tegra_mc *mc, unsigned long rate);
unsigned int tegra_mc_get_emem_device_count(struct tegra_mc *mc);

#ifdef CONFIG_TEGRA_MC
struct tegra_mc *devm_tegra_memory_controller_get(struct device *dev);
int tegra_mc_probe_device(struct tegra_mc *mc, struct device *dev);
int tegra_mc_get_carveout_info(struct tegra_mc *mc, unsigned int id,
                               phys_addr_t *base, u64 *size);
#else
static inline struct tegra_mc *
devm_tegra_memory_controller_get(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

static inline int
tegra_mc_probe_device(struct tegra_mc *mc, struct device *dev)
{
	return -ENODEV;
}

static inline int
tegra_mc_get_carveout_info(struct tegra_mc *mc, unsigned int id,
                           phys_addr_t *base, u64 *size)
{
	return -ENODEV;
}
#endif

#endif /* __SOC_TEGRA_MC_H__ */
