// SPDX-License-Identifier: GPL-2.0-only
/*
// Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */
#ifndef _LINUX_PSE_CONTROLLER_H
#define _LINUX_PSE_CONTROLLER_H

#include <linux/ethtool.h>
#include <linux/list.h>
#include <uapi/linux/ethtool.h>

struct phy_device;
struct pse_controller_dev;

/**
 * struct pse_control_config - PSE control/channel configuration.
 *
 * @admin_cotrol: set PoDL PSE admin control as described in
 *	IEEE 802.3-2018 30.15.1.2.1 acPoDLPSEAdminControl
 */
struct pse_control_config {
	enum ethtool_podl_pse_admin_state admin_cotrol;
};

/**
 * struct pse_control_status - PSE control/channel status.
 *
 * @podl_admin_state: operational state of the PoDL PSE
 *	functions. IEEE 802.3-2018 30.15.1.1.2 aPoDLPSEAdminState
 * @podl_pw_status: power detection status of the PoDL PSE.
 *	IEEE 802.3-2018 30.15.1.1.3 aPoDLPSEPowerDetectionStatus:
 */
struct pse_control_status {
	enum ethtool_podl_pse_admin_state podl_admin_state;
	enum ethtool_podl_pse_pw_d_status podl_pw_status;
};

/**
 * struct pse_controller_ops - PSE controller driver callbacks
 *
 * @ethtool_get_status: get PSE control status for ethtool interface
 * @ethtool_set_config: set PSE control configuration over ethtool interface
 */
struct pse_controller_ops {
	int (*ethtool_get_status)(struct pse_controller_dev *pcdev,
		unsigned long id, struct netlink_ext_ack *extack,
		struct pse_control_status *status);
	int (*ethtool_set_config)(struct pse_controller_dev *pcdev,
		unsigned long id, struct netlink_ext_ack *extack,
		const struct pse_control_config *config);
};

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

int pse_ethtool_get_status(struct pse_control *psec,
			   struct netlink_ext_ack *extack,
			   struct pse_control_status *status);
int pse_ethtool_set_config(struct pse_control *psec,
			   struct netlink_ext_ack *extack,
			   const struct pse_control_config *config);

#else

static inline struct pse_control *of_pse_control_get(struct device_node *node)
{
	return ERR_PTR(-ENOENT);
}

static inline void pse_control_put(struct pse_control *psec)
{
}

static inline int pse_ethtool_get_status(struct pse_control *psec,
					 struct netlink_ext_ack *extack,
					 struct pse_control_status *status)
{
	return -EOPNOTSUPP;
}

static inline int pse_ethtool_set_config(struct pse_control *psec,
					 struct netlink_ext_ack *extack,
					 const struct pse_control_config *config)
{
	return -EOPNOTSUPP;
}

#endif

#endif
