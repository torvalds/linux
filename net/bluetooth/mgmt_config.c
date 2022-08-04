// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Google Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "mgmt_util.h"
#include "mgmt_config.h"

#define HDEV_PARAM_U16(_param_name_) \
	struct {\
		struct mgmt_tlv entry; \
		__le16 value; \
	} __packed _param_name_

#define HDEV_PARAM_U8(_param_name_) \
	struct {\
		struct mgmt_tlv entry; \
		__u8 value; \
	} __packed _param_name_

#define TLV_SET_U16(_param_code_, _param_name_) \
	{ \
		{ cpu_to_le16(_param_code_), sizeof(__u16) }, \
		cpu_to_le16(hdev->_param_name_) \
	}

#define TLV_SET_U8(_param_code_, _param_name_) \
	{ \
		{ cpu_to_le16(_param_code_), sizeof(__u8) }, \
		hdev->_param_name_ \
	}

#define TLV_SET_U16_JIFFIES_TO_MSECS(_param_code_, _param_name_) \
	{ \
		{ cpu_to_le16(_param_code_), sizeof(__u16) }, \
		cpu_to_le16(jiffies_to_msecs(hdev->_param_name_)) \
	}

int read_def_system_config(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 data_len)
{
	int ret;
	struct mgmt_rp_read_def_system_config {
		/* Please see mgmt-api.txt for documentation of these values */
		HDEV_PARAM_U16(def_page_scan_type);
		HDEV_PARAM_U16(def_page_scan_int);
		HDEV_PARAM_U16(def_page_scan_window);
		HDEV_PARAM_U16(def_inq_scan_type);
		HDEV_PARAM_U16(def_inq_scan_int);
		HDEV_PARAM_U16(def_inq_scan_window);
		HDEV_PARAM_U16(def_br_lsto);
		HDEV_PARAM_U16(def_page_timeout);
		HDEV_PARAM_U16(sniff_min_interval);
		HDEV_PARAM_U16(sniff_max_interval);
		HDEV_PARAM_U16(le_adv_min_interval);
		HDEV_PARAM_U16(le_adv_max_interval);
		HDEV_PARAM_U16(def_multi_adv_rotation_duration);
		HDEV_PARAM_U16(le_scan_interval);
		HDEV_PARAM_U16(le_scan_window);
		HDEV_PARAM_U16(le_scan_int_suspend);
		HDEV_PARAM_U16(le_scan_window_suspend);
		HDEV_PARAM_U16(le_scan_int_discovery);
		HDEV_PARAM_U16(le_scan_window_discovery);
		HDEV_PARAM_U16(le_scan_int_adv_monitor);
		HDEV_PARAM_U16(le_scan_window_adv_monitor);
		HDEV_PARAM_U16(le_scan_int_connect);
		HDEV_PARAM_U16(le_scan_window_connect);
		HDEV_PARAM_U16(le_conn_min_interval);
		HDEV_PARAM_U16(le_conn_max_interval);
		HDEV_PARAM_U16(le_conn_latency);
		HDEV_PARAM_U16(le_supv_timeout);
		HDEV_PARAM_U16(def_le_autoconnect_timeout);
		HDEV_PARAM_U16(advmon_allowlist_duration);
		HDEV_PARAM_U16(advmon_no_filter_duration);
		HDEV_PARAM_U8(enable_advmon_interleave_scan);
	} __packed rp = {
		TLV_SET_U16(0x0000, def_page_scan_type),
		TLV_SET_U16(0x0001, def_page_scan_int),
		TLV_SET_U16(0x0002, def_page_scan_window),
		TLV_SET_U16(0x0003, def_inq_scan_type),
		TLV_SET_U16(0x0004, def_inq_scan_int),
		TLV_SET_U16(0x0005, def_inq_scan_window),
		TLV_SET_U16(0x0006, def_br_lsto),
		TLV_SET_U16(0x0007, def_page_timeout),
		TLV_SET_U16(0x0008, sniff_min_interval),
		TLV_SET_U16(0x0009, sniff_max_interval),
		TLV_SET_U16(0x000a, le_adv_min_interval),
		TLV_SET_U16(0x000b, le_adv_max_interval),
		TLV_SET_U16(0x000c, def_multi_adv_rotation_duration),
		TLV_SET_U16(0x000d, le_scan_interval),
		TLV_SET_U16(0x000e, le_scan_window),
		TLV_SET_U16(0x000f, le_scan_int_suspend),
		TLV_SET_U16(0x0010, le_scan_window_suspend),
		TLV_SET_U16(0x0011, le_scan_int_discovery),
		TLV_SET_U16(0x0012, le_scan_window_discovery),
		TLV_SET_U16(0x0013, le_scan_int_adv_monitor),
		TLV_SET_U16(0x0014, le_scan_window_adv_monitor),
		TLV_SET_U16(0x0015, le_scan_int_connect),
		TLV_SET_U16(0x0016, le_scan_window_connect),
		TLV_SET_U16(0x0017, le_conn_min_interval),
		TLV_SET_U16(0x0018, le_conn_max_interval),
		TLV_SET_U16(0x0019, le_conn_latency),
		TLV_SET_U16(0x001a, le_supv_timeout),
		TLV_SET_U16_JIFFIES_TO_MSECS(0x001b,
					     def_le_autoconnect_timeout),
		TLV_SET_U16(0x001d, advmon_allowlist_duration),
		TLV_SET_U16(0x001e, advmon_no_filter_duration),
		TLV_SET_U8(0x001f, enable_advmon_interleave_scan),
	};

	bt_dev_dbg(hdev, "sock %p", sk);

	ret = mgmt_cmd_complete(sk, hdev->id,
				MGMT_OP_READ_DEF_SYSTEM_CONFIG,
				0, &rp, sizeof(rp));
	return ret;
}

#define TO_TLV(x)		((struct mgmt_tlv *)(x))
#define TLV_GET_LE16(tlv)	le16_to_cpu(*((__le16 *)(TO_TLV(tlv)->value)))
#define TLV_GET_U8(tlv)		(*((__u8 *)(TO_TLV(tlv)->value)))

int set_def_system_config(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 data_len)
{
	u16 buffer_left = data_len;
	u8 *buffer = data;

	if (buffer_left < sizeof(struct mgmt_tlv)) {
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_DEF_SYSTEM_CONFIG,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	/* First pass to validate the tlv */
	while (buffer_left >= sizeof(struct mgmt_tlv)) {
		const u8 len = TO_TLV(buffer)->length;
		size_t exp_type_len;
		const u16 exp_len = sizeof(struct mgmt_tlv) +
				    len;
		const u16 type = le16_to_cpu(TO_TLV(buffer)->type);

		if (buffer_left < exp_len) {
			bt_dev_warn(hdev, "invalid len left %d, exp >= %d",
				    buffer_left, exp_len);

			return mgmt_cmd_status(sk, hdev->id,
					MGMT_OP_SET_DEF_SYSTEM_CONFIG,
					MGMT_STATUS_INVALID_PARAMS);
		}

		/* Please see mgmt-api.txt for documentation of these values */
		switch (type) {
		case 0x0000:
		case 0x0001:
		case 0x0002:
		case 0x0003:
		case 0x0004:
		case 0x0005:
		case 0x0006:
		case 0x0007:
		case 0x0008:
		case 0x0009:
		case 0x000a:
		case 0x000b:
		case 0x000c:
		case 0x000d:
		case 0x000e:
		case 0x000f:
		case 0x0010:
		case 0x0011:
		case 0x0012:
		case 0x0013:
		case 0x0014:
		case 0x0015:
		case 0x0016:
		case 0x0017:
		case 0x0018:
		case 0x0019:
		case 0x001a:
		case 0x001b:
		case 0x001d:
		case 0x001e:
			exp_type_len = sizeof(u16);
			break;
		case 0x001f:
			exp_type_len = sizeof(u8);
			break;
		default:
			exp_type_len = 0;
			bt_dev_warn(hdev, "unsupported parameter %u", type);
			break;
		}

		if (exp_type_len && len != exp_type_len) {
			bt_dev_warn(hdev, "invalid length %d, exp %zu for type %d",
				    len, exp_type_len, type);

			return mgmt_cmd_status(sk, hdev->id,
				MGMT_OP_SET_DEF_SYSTEM_CONFIG,
				MGMT_STATUS_INVALID_PARAMS);
		}

		buffer_left -= exp_len;
		buffer += exp_len;
	}

	buffer_left = data_len;
	buffer = data;
	while (buffer_left >= sizeof(struct mgmt_tlv)) {
		const u8 len = TO_TLV(buffer)->length;
		const u16 exp_len = sizeof(struct mgmt_tlv) +
				    len;
		const u16 type = le16_to_cpu(TO_TLV(buffer)->type);

		switch (type) {
		case 0x0000:
			hdev->def_page_scan_type = TLV_GET_LE16(buffer);
			break;
		case 0x0001:
			hdev->def_page_scan_int = TLV_GET_LE16(buffer);
			break;
		case 0x0002:
			hdev->def_page_scan_window = TLV_GET_LE16(buffer);
			break;
		case 0x0003:
			hdev->def_inq_scan_type = TLV_GET_LE16(buffer);
			break;
		case 0x0004:
			hdev->def_inq_scan_int = TLV_GET_LE16(buffer);
			break;
		case 0x0005:
			hdev->def_inq_scan_window = TLV_GET_LE16(buffer);
			break;
		case 0x0006:
			hdev->def_br_lsto = TLV_GET_LE16(buffer);
			break;
		case 0x0007:
			hdev->def_page_timeout = TLV_GET_LE16(buffer);
			break;
		case 0x0008:
			hdev->sniff_min_interval = TLV_GET_LE16(buffer);
			break;
		case 0x0009:
			hdev->sniff_max_interval = TLV_GET_LE16(buffer);
			break;
		case 0x000a:
			hdev->le_adv_min_interval = TLV_GET_LE16(buffer);
			break;
		case 0x000b:
			hdev->le_adv_max_interval = TLV_GET_LE16(buffer);
			break;
		case 0x000c:
			hdev->def_multi_adv_rotation_duration =
							   TLV_GET_LE16(buffer);
			break;
		case 0x000d:
			hdev->le_scan_interval = TLV_GET_LE16(buffer);
			break;
		case 0x000e:
			hdev->le_scan_window = TLV_GET_LE16(buffer);
			break;
		case 0x000f:
			hdev->le_scan_int_suspend = TLV_GET_LE16(buffer);
			break;
		case 0x0010:
			hdev->le_scan_window_suspend = TLV_GET_LE16(buffer);
			break;
		case 0x0011:
			hdev->le_scan_int_discovery = TLV_GET_LE16(buffer);
			break;
		case 0x00012:
			hdev->le_scan_window_discovery = TLV_GET_LE16(buffer);
			break;
		case 0x00013:
			hdev->le_scan_int_adv_monitor = TLV_GET_LE16(buffer);
			break;
		case 0x00014:
			hdev->le_scan_window_adv_monitor = TLV_GET_LE16(buffer);
			break;
		case 0x00015:
			hdev->le_scan_int_connect = TLV_GET_LE16(buffer);
			break;
		case 0x00016:
			hdev->le_scan_window_connect = TLV_GET_LE16(buffer);
			break;
		case 0x00017:
			hdev->le_conn_min_interval = TLV_GET_LE16(buffer);
			break;
		case 0x00018:
			hdev->le_conn_max_interval = TLV_GET_LE16(buffer);
			break;
		case 0x00019:
			hdev->le_conn_latency = TLV_GET_LE16(buffer);
			break;
		case 0x0001a:
			hdev->le_supv_timeout = TLV_GET_LE16(buffer);
			break;
		case 0x0001b:
			hdev->def_le_autoconnect_timeout =
					msecs_to_jiffies(TLV_GET_LE16(buffer));
			break;
		case 0x0001d:
			hdev->advmon_allowlist_duration = TLV_GET_LE16(buffer);
			break;
		case 0x0001e:
			hdev->advmon_no_filter_duration = TLV_GET_LE16(buffer);
			break;
		case 0x0001f:
			hdev->enable_advmon_interleave_scan = TLV_GET_U8(buffer);
			break;
		default:
			bt_dev_warn(hdev, "unsupported parameter %u", type);
			break;
		}

		buffer_left -= exp_len;
		buffer += exp_len;
	}

	return mgmt_cmd_complete(sk, hdev->id,
				 MGMT_OP_SET_DEF_SYSTEM_CONFIG, 0, NULL, 0);
}

int read_def_runtime_config(struct sock *sk, struct hci_dev *hdev, void *data,
			    u16 data_len)
{
	bt_dev_dbg(hdev, "sock %p", sk);

	return mgmt_cmd_complete(sk, hdev->id,
				 MGMT_OP_READ_DEF_RUNTIME_CONFIG, 0, NULL, 0);
}

int set_def_runtime_config(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 data_len)
{
	bt_dev_dbg(hdev, "sock %p", sk);

	return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DEF_SYSTEM_CONFIG,
			       MGMT_STATUS_INVALID_PARAMS);
}
