/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2012-2014, 2018-2021 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __VENDOR_CMD_INTEL_H__
#define __VENDOR_CMD_INTEL_H__

#define INTEL_OUI	0x001735

/**
 * enum iwl_mvm_vendor_cmd - supported vendor commands
 * @IWL_MVM_VENDOR_CMD_GET_CSME_CONN_INFO: reports CSME connection info.
 * @IWL_MVM_VENDOR_CMD_HOST_GET_OWNERSHIP: asks for ownership on the device.
 * @IWL_MVM_VENDOR_CMD_ROAMING_FORBIDDEN_EVENT: notifies if roaming is allowed.
 *	It contains a &IWL_MVM_VENDOR_ATTR_ROAMING_FORBIDDEN and a
 *	&IWL_MVM_VENDOR_ATTR_VIF_ADDR attributes.
 */

enum iwl_mvm_vendor_cmd {
	IWL_MVM_VENDOR_CMD_GET_CSME_CONN_INFO			= 0x2d,
	IWL_MVM_VENDOR_CMD_HOST_GET_OWNERSHIP			= 0x30,
	IWL_MVM_VENDOR_CMD_ROAMING_FORBIDDEN_EVENT		= 0x32,
};

enum iwl_vendor_auth_akm_mode {
	IWL_VENDOR_AUTH_OPEN,
	IWL_VENDOR_AUTH_RSNA = 0x6,
	IWL_VENDOR_AUTH_RSNA_PSK,
	IWL_VENDOR_AUTH_SAE = 0x9,
	IWL_VENDOR_AUTH_MAX,
};

/**
 * enum iwl_mvm_vendor_attr - attributes used in vendor commands
 * @__IWL_MVM_VENDOR_ATTR_INVALID: attribute 0 is invalid
 * @IWL_MVM_VENDOR_ATTR_VIF_ADDR: interface MAC address
 * @IWL_MVM_VENDOR_ATTR_ADDR: MAC address
 * @IWL_MVM_VENDOR_ATTR_SSID: SSID (binary attribute, 0..32 octets)
 * @IWL_MVM_VENDOR_ATTR_STA_CIPHER: the cipher to use for the station with the
 *	mac address specified in &IWL_MVM_VENDOR_ATTR_ADDR.
 * @IWL_MVM_VENDOR_ATTR_ROAMING_FORBIDDEN: u8 attribute. Indicates whether
 *	roaming is forbidden or not. Value 1 means roaming is forbidden,
 *	0 mean roaming is allowed.
 * @IWL_MVM_VENDOR_ATTR_AUTH_MODE: u32 attribute. Authentication mode type
 *	as specified in &enum iwl_vendor_auth_akm_mode.
 * @IWL_MVM_VENDOR_ATTR_CHANNEL_NUM: u8 attribute. Contains channel number.
 * @IWL_MVM_VENDOR_ATTR_BAND: u8 attribute.
 *	0 for 2.4 GHz band, 1 for 5.2GHz band and 2 for 6GHz band.
 * @IWL_MVM_VENDOR_ATTR_COLLOC_CHANNEL: u32 attribute. Channel number of
 *	collocated AP. Relevant for 6GHz AP info.
 * @IWL_MVM_VENDOR_ATTR_COLLOC_ADDR: MAC address of a collocated AP.
 *	Relevant for 6GHz AP info.
 *
 * @NUM_IWL_MVM_VENDOR_ATTR: number of vendor attributes
 * @MAX_IWL_MVM_VENDOR_ATTR: highest vendor attribute number

 */
enum iwl_mvm_vendor_attr {
	__IWL_MVM_VENDOR_ATTR_INVALID				= 0x00,
	IWL_MVM_VENDOR_ATTR_VIF_ADDR				= 0x02,
	IWL_MVM_VENDOR_ATTR_ADDR				= 0x0a,
	IWL_MVM_VENDOR_ATTR_SSID				= 0x3d,
	IWL_MVM_VENDOR_ATTR_STA_CIPHER				= 0x51,
	IWL_MVM_VENDOR_ATTR_ROAMING_FORBIDDEN			= 0x64,
	IWL_MVM_VENDOR_ATTR_AUTH_MODE				= 0x65,
	IWL_MVM_VENDOR_ATTR_CHANNEL_NUM				= 0x66,
	IWL_MVM_VENDOR_ATTR_BAND				= 0x69,
	IWL_MVM_VENDOR_ATTR_COLLOC_CHANNEL			= 0x70,
	IWL_MVM_VENDOR_ATTR_COLLOC_ADDR				= 0x71,

	NUM_IWL_MVM_VENDOR_ATTR,
	MAX_IWL_MVM_VENDOR_ATTR = NUM_IWL_MVM_VENDOR_ATTR - 1,
};

#endif /* __VENDOR_CMD_INTEL_H__ */
