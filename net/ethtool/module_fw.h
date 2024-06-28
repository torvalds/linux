/* SPDX-License-Identifier: GPL-2.0-only */

#include <uapi/linux/ethtool.h>
#include "netlink.h"

/**
 * struct ethnl_module_fw_flash_ntf_params - module firmware flashing
 *						notifications parameters
 * @portid: Netlink portid of sender.
 * @seq: Sequence number of sender.
 * @closed_sock: Indicates whether the socket was closed from user space.
 */
struct ethnl_module_fw_flash_ntf_params {
	u32 portid;
	u32 seq;
	bool closed_sock;
};

/**
 * struct ethtool_module_fw_flash_params - module firmware flashing parameters
 * @password: Module password. Only valid when @pass_valid is set.
 * @password_valid: Whether the module password is valid or not.
 */
struct ethtool_module_fw_flash_params {
	__be32 password;
	u8 password_valid:1;
};

/**
 * struct ethtool_cmis_fw_update_params - CMIS firmware update specific
 *						parameters
 * @dev: Pointer to the net_device to be flashed.
 * @params: Module firmware flashing parameters.
 * @ntf_params: Module firmware flashing notification parameters.
 * @fw: Firmware to flash.
 */
struct ethtool_cmis_fw_update_params {
	struct net_device *dev;
	struct ethtool_module_fw_flash_params params;
	struct ethnl_module_fw_flash_ntf_params ntf_params;
	const struct firmware *fw;
};

/**
 * struct ethtool_module_fw_flash - module firmware flashing
 * @list: List node for &module_fw_flash_work_list.
 * @dev_tracker: Refcount tracker for @dev.
 * @work: The flashing firmware work.
 * @fw_update: CMIS firmware update specific parameters.
 */
struct ethtool_module_fw_flash {
	struct list_head list;
	netdevice_tracker dev_tracker;
	struct work_struct work;
	struct ethtool_cmis_fw_update_params fw_update;
};

void ethnl_module_fw_flash_sock_destroy(struct ethnl_sock_priv *sk_priv);

void
ethnl_module_fw_flash_ntf_err(struct net_device *dev,
			      struct ethnl_module_fw_flash_ntf_params *params,
			      char *err_msg, char *sub_err_msg);
void
ethnl_module_fw_flash_ntf_start(struct net_device *dev,
				struct ethnl_module_fw_flash_ntf_params *params);
void
ethnl_module_fw_flash_ntf_complete(struct net_device *dev,
				   struct ethnl_module_fw_flash_ntf_params *params);
void
ethnl_module_fw_flash_ntf_in_progress(struct net_device *dev,
				      struct ethnl_module_fw_flash_ntf_params *params,
				      u64 done, u64 total);

void ethtool_cmis_fw_update(struct ethtool_cmis_fw_update_params *params);
