// SPDX-License-Identifier: GPL-2.0-only
/*
// Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */
#ifndef _LINUX_PSE_CONTROLLER_H
#define _LINUX_PSE_CONTROLLER_H

#include <linux/ethtool.h>
#include <linux/list.h>
#include <uapi/linux/ethtool.h>

struct module;
struct device_node;
struct of_phandle_args;
struct pse_control;

/**
 * struct pse_controller_dev - PSE controller entity that might
 *                             provide multiple PSE controls
 * @ops: a pointer to device specific struct pse_controller_ops
 * @owner: kernel module of the PSE controller driver
 * @list: internal list of PSE controller devices
 * @pse_control_head: head of internal list of requested PSE controls
 * @dev: corresponding driver model device struct
 * @of_pse_n_cells: number of cells in PSE line specifiers
 * @of_xlate: translation function to translate from specifier as found in the
 *            device tree to id as given to the PSE control ops
 * @nr_lines: number of PSE controls in this controller device
 * @lock: Mutex for serialization access to the PSE controller
 */
struct pse_controller_dev {
	const struct pse_controller_ops *ops;
	struct module *owner;
	struct list_head list;
	struct list_head pse_control_head;
	struct device *dev;
	int of_pse_n_cells;
	int (*of_xlate)(struct pse_controller_dev *pcdev,
			const struct of_phandle_args *pse_spec);
	unsigned int nr_lines;
	struct mutex lock;
};

#if IS_ENABLED(CONFIG_PSE_CONTROLLER)
int pse_controller_register(struct pse_controller_dev *pcdev);
void pse_controller_unregister(struct pse_controller_dev *pcdev);
struct device;
int devm_pse_controller_register(struct device *dev,
				 struct pse_controller_dev *pcdev);

struct pse_control *of_pse_control_get(struct device_node *node);
void pse_control_put(struct pse_control *psec);

#else

static inline struct pse_control *of_pse_control_get(struct device_node *node)
{
	return ERR_PTR(-ENOENT);
}

static inline void pse_control_put(struct pse_control *psec)
{
}

#endif

#endif
