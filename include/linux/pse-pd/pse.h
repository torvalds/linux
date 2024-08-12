// SPDX-License-Identifier: GPL-2.0-only
/*
// Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */
#ifndef _LINUX_PSE_CONTROLLER_H
#define _LINUX_PSE_CONTROLLER_H

#include <linux/ethtool.h>
#include <linux/list.h>
#include <uapi/linux/ethtool.h>

/* Maximum current in uA according to IEEE 802.3-2022 Table 145-1 */
#define MAX_PI_CURRENT 1920000

struct phy_device;
struct pse_controller_dev;

/**
 * struct pse_control_config - PSE control/channel configuration.
 *
 * @podl_admin_control: set PoDL PSE admin control as described in
 *	IEEE 802.3-2018 30.15.1.2.1 acPoDLPSEAdminControl
 * @c33_admin_control: set PSE admin control as described in
 *	IEEE 802.3-2022 30.9.1.2.1 acPSEAdminControl
 */
struct pse_control_config {
	enum ethtool_podl_pse_admin_state podl_admin_control;
	enum ethtool_c33_pse_admin_state c33_admin_control;
};

/**
 * struct pse_control_status - PSE control/channel status.
 *
 * @podl_admin_state: operational state of the PoDL PSE
 *	functions. IEEE 802.3-2018 30.15.1.1.2 aPoDLPSEAdminState
 * @podl_pw_status: power detection status of the PoDL PSE.
 *	IEEE 802.3-2018 30.15.1.1.3 aPoDLPSEPowerDetectionStatus:
 * @c33_admin_state: operational state of the PSE
 *	functions. IEEE 802.3-2022 30.9.1.1.2 aPSEAdminState
 * @c33_pw_status: power detection status of the PSE.
 *	IEEE 802.3-2022 30.9.1.1.5 aPSEPowerDetectionStatus:
 * @c33_pw_class: detected class of a powered PD
 *	IEEE 802.3-2022 30.9.1.1.8 aPSEPowerClassification
 * @c33_actual_pw: power currently delivered by the PSE in mW
 *	IEEE 802.3-2022 30.9.1.1.23 aPSEActualPower
 * @c33_ext_state_info: extended state information of the PSE
 * @c33_avail_pw_limit: available power limit of the PSE in mW
 *	IEEE 802.3-2022 145.2.5.4 pse_avail_pwr
 * @c33_pw_limit_ranges: supported power limit configuration range. The driver
 *	is in charge of the memory allocation.
 * @c33_pw_limit_nb_ranges: number of supported power limit configuration
 *	ranges
 */
struct pse_control_status {
	enum ethtool_podl_pse_admin_state podl_admin_state;
	enum ethtool_podl_pse_pw_d_status podl_pw_status;
	enum ethtool_c33_pse_admin_state c33_admin_state;
	enum ethtool_c33_pse_pw_d_status c33_pw_status;
	u32 c33_pw_class;
	u32 c33_actual_pw;
	struct ethtool_c33_pse_ext_state_info c33_ext_state_info;
	u32 c33_avail_pw_limit;
	struct ethtool_c33_pse_pw_limit_range *c33_pw_limit_ranges;
	u32 c33_pw_limit_nb_ranges;
};

/**
 * struct pse_controller_ops - PSE controller driver callbacks
 *
 * @ethtool_get_status: get PSE control status for ethtool interface
 * @setup_pi_matrix: setup PI matrix of the PSE controller
 * @pi_is_enabled: Return 1 if the PSE PI is enabled, 0 if not.
 *		   May also return negative errno.
 * @pi_enable: Configure the PSE PI as enabled.
 * @pi_disable: Configure the PSE PI as disabled.
 * @pi_get_voltage: Return voltage similarly to get_voltage regulator
 *		    callback.
 * @pi_get_current_limit: Get the configured current limit similarly to
 *			  get_current_limit regulator callback.
 * @pi_set_current_limit: Configure the current limit similarly to
 *			  set_current_limit regulator callback.
 *			  Should not return an error in case of MAX_PI_CURRENT
 *			  current value set.
 */
struct pse_controller_ops {
	int (*ethtool_get_status)(struct pse_controller_dev *pcdev,
		unsigned long id, struct netlink_ext_ack *extack,
		struct pse_control_status *status);
	int (*setup_pi_matrix)(struct pse_controller_dev *pcdev);
	int (*pi_is_enabled)(struct pse_controller_dev *pcdev, int id);
	int (*pi_enable)(struct pse_controller_dev *pcdev, int id);
	int (*pi_disable)(struct pse_controller_dev *pcdev, int id);
	int (*pi_get_voltage)(struct pse_controller_dev *pcdev, int id);
	int (*pi_get_current_limit)(struct pse_controller_dev *pcdev,
				    int id);
	int (*pi_set_current_limit)(struct pse_controller_dev *pcdev,
				    int id, int max_uA);
};

struct module;
struct device_node;
struct of_phandle_args;
struct pse_control;

/* PSE PI pairset pinout can either be Alternative A or Alternative B */
enum pse_pi_pairset_pinout {
	ALTERNATIVE_A,
	ALTERNATIVE_B,
};

/**
 * struct pse_pi_pairset - PSE PI pairset entity describing the pinout
 *			   alternative ant its phandle
 *
 * @pinout: description of the pinout alternative
 * @np: device node pointer describing the pairset phandle
 */
struct pse_pi_pairset {
	enum pse_pi_pairset_pinout pinout;
	struct device_node *np;
};

/**
 * struct pse_pi - PSE PI (Power Interface) entity as described in
 *		   IEEE 802.3-2022 145.2.4
 *
 * @pairset: table of the PSE PI pinout alternative for the two pairset
 * @np: device node pointer of the PSE PI node
 * @rdev: regulator represented by the PSE PI
 * @admin_state_enabled: PI enabled state
 */
struct pse_pi {
	struct pse_pi_pairset pairset[2];
	struct device_node *np;
	struct regulator_dev *rdev;
	bool admin_state_enabled;
};

/**
 * struct pse_controller_dev - PSE controller entity that might
 *                             provide multiple PSE controls
 * @ops: a pointer to device specific struct pse_controller_ops
 * @owner: kernel module of the PSE controller driver
 * @list: internal list of PSE controller devices
 * @pse_control_head: head of internal list of requested PSE controls
 * @dev: corresponding driver model device struct
 * @of_pse_n_cells: number of cells in PSE line specifiers
 * @nr_lines: number of PSE controls in this controller device
 * @lock: Mutex for serialization access to the PSE controller
 * @types: types of the PSE controller
 * @pi: table of PSE PIs described in this controller device
 * @no_of_pse_pi: flag set if the pse_pis devicetree node is not used
 */
struct pse_controller_dev {
	const struct pse_controller_ops *ops;
	struct module *owner;
	struct list_head list;
	struct list_head pse_control_head;
	struct device *dev;
	int of_pse_n_cells;
	unsigned int nr_lines;
	struct mutex lock;
	enum ethtool_pse_types types;
	struct pse_pi *pi;
	bool no_of_pse_pi;
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
int pse_ethtool_set_pw_limit(struct pse_control *psec,
			     struct netlink_ext_ack *extack,
			     const unsigned int pw_limit);
int pse_ethtool_get_pw_limit(struct pse_control *psec,
			     struct netlink_ext_ack *extack);

bool pse_has_podl(struct pse_control *psec);
bool pse_has_c33(struct pse_control *psec);

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

static inline int pse_ethtool_set_pw_limit(struct pse_control *psec,
					   struct netlink_ext_ack *extack,
					   const unsigned int pw_limit)
{
	return -EOPNOTSUPP;
}

static inline int pse_ethtool_get_pw_limit(struct pse_control *psec,
					   struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline bool pse_has_podl(struct pse_control *psec)
{
	return false;
}

static inline bool pse_has_c33(struct pse_control *psec)
{
	return false;
}

#endif

#endif
