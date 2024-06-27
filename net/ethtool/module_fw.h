/* SPDX-License-Identifier: GPL-2.0-only */

#include <uapi/linux/ethtool.h>

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
