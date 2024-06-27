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
