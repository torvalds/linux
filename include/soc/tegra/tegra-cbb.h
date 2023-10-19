/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved
 */

#ifndef TEGRA_CBB_H
#define TEGRA_CBB_H

#include <linux/list.h>

struct tegra_cbb_error {
	const char *code;
	const char *source;
	const char *desc;
};

struct tegra_cbb {
	struct device *dev;
	const struct tegra_cbb_ops *ops;
	struct list_head node;
};

struct tegra_cbb_ops {
	int (*debugfs_show)(struct tegra_cbb *cbb, struct seq_file *s, void *v);
	int (*interrupt_enable)(struct tegra_cbb *cbb);
	void (*error_enable)(struct tegra_cbb *cbb);
	void (*fault_enable)(struct tegra_cbb *cbb);
	void (*stall_enable)(struct tegra_cbb *cbb);
	void (*error_clear)(struct tegra_cbb *cbb);
	u32 (*get_status)(struct tegra_cbb *cbb);
};

int tegra_cbb_get_irq(struct platform_device *pdev, unsigned int *nonsec_irq,
		      unsigned int *sec_irq);
__printf(2, 3)
void tegra_cbb_print_err(struct seq_file *file, const char *fmt, ...);

void tegra_cbb_print_cache(struct seq_file *file, u32 cache);
void tegra_cbb_print_prot(struct seq_file *file, u32 prot);
int tegra_cbb_register(struct tegra_cbb *cbb);

void tegra_cbb_fault_enable(struct tegra_cbb *cbb);
void tegra_cbb_stall_enable(struct tegra_cbb *cbb);
void tegra_cbb_error_clear(struct tegra_cbb *cbb);
u32 tegra_cbb_get_status(struct tegra_cbb *cbb);

#endif /* TEGRA_CBB_H */
