/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef USB_QMI_V01_H
#define USB_QMI_V01_H

#define UAUDIO_STREAM_SERVICE_ID_V01 0x41D
#define UAUDIO_STREAM_SERVICE_VERS_V01 0x01

#define QMI_UAUDIO_STREAM_RESP_V01 0x0001
#define QMI_UAUDIO_STREAM_REQ_V01 0x0001
#define QMI_UAUDIO_STREAM_IND_V01 0x0001


struct mem_info_v01 {
	u64 va;
	u64 pa;
	u32 size;
};

struct apps_mem_info_v01 {
	struct mem_info_v01 evt_ring;
	struct mem_info_v01 tr_data;
	struct mem_info_v01 tr_sync;
	struct mem_info_v01 xfer_buff;
	struct mem_info_v01 dcba;
};

struct usb_endpoint_descriptor_v01 {
	u8 bLength;
	u8 bDescriptorType;
	u8 bEndpointAddress;
	u8 bmAttributes;
	u16 wMaxPacketSize;
	u8 bInterval;
	u8 bRefresh;
	u8 bSynchAddress;
};

struct usb_interface_descriptor_v01 {
	u8 bLength;
	u8 bDescriptorType;
	u8 bInterfaceNumber;
	u8 bAlternateSetting;
	u8 bNumEndpoints;
	u8 bInterfaceClass;
	u8 bInterfaceSubClass;
	u8 bInterfaceProtocol;
	u8 iInterface;
};

enum usb_audio_stream_status_enum_v01 {
	USB_AUDIO_STREAM_STATUS_ENUM_MIN_VAL_V01 = INT_MIN,
	USB_AUDIO_STREAM_REQ_SUCCESS_V01 = 0,
	USB_AUDIO_STREAM_REQ_FAILURE_V01 = 1,
	USB_AUDIO_STREAM_REQ_FAILURE_NOT_FOUND_V01 = 2,
	USB_AUDIO_STREAM_REQ_FAILURE_INVALID_PARAM_V01 = 3,
	USB_AUDIO_STREAM_REQ_FAILURE_MEMALLOC_V01 = 4,
	USB_AUDIO_STREAM_STATUS_ENUM_MAX_VAL_V01 = INT_MAX,
};

enum usb_audio_device_indication_enum_v01 {
	USB_AUDIO_DEVICE_INDICATION_ENUM_MIN_VAL_V01 = INT_MIN,
	USB_AUDIO_DEV_CONNECT_V01 = 0,
	USB_AUDIO_DEV_DISCONNECT_V01 = 1,
	USB_AUDIO_DEV_SUSPEND_V01 = 2,
	USB_AUDIO_DEV_RESUME_V01 = 3,
	USB_AUDIO_DEVICE_INDICATION_ENUM_MAX_VAL_V01 = INT_MAX,
};

enum usb_audio_device_speed_enum_v01 {
	USB_AUDIO_DEVICE_SPEED_ENUM_MIN_VAL_V01 = INT_MIN,
	USB_AUDIO_DEVICE_SPEED_INVALID_V01 = 0,
	USB_AUDIO_DEVICE_SPEED_LOW_V01 = 1,
	USB_AUDIO_DEVICE_SPEED_FULL_V01 = 2,
	USB_AUDIO_DEVICE_SPEED_HIGH_V01 = 3,
	USB_AUDIO_DEVICE_SPEED_SUPER_V01 = 4,
	USB_AUDIO_DEVICE_SPEED_SUPER_PLUS_V01 = 5,
	USB_AUDIO_DEVICE_SPEED_ENUM_MAX_VAL_V01 = INT_MAX,
};

struct qmi_uaudio_stream_req_msg_v01 {
	u8 enable;
	u32 usb_token;
	u8 audio_format_valid;
	u32 audio_format;
	u8 number_of_ch_valid;
	u32 number_of_ch;
	u8 bit_rate_valid;
	u32 bit_rate;
	u8 xfer_buff_size_valid;
	u32 xfer_buff_size;
	u8 service_interval_valid;
	u32 service_interval;
};
#define QMI_UAUDIO_STREAM_REQ_MSG_V01_MAX_MSG_LEN 46
extern struct qmi_elem_info qmi_uaudio_stream_req_msg_v01_ei[];

struct qmi_uaudio_stream_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	u8 status_valid;
	enum usb_audio_stream_status_enum_v01 status;
	u8 internal_status_valid;
	u32 internal_status;
	u8 slot_id_valid;
	u32 slot_id;
	u8 usb_token_valid;
	u32 usb_token;
	u8 std_as_opr_intf_desc_valid;
	struct usb_interface_descriptor_v01 std_as_opr_intf_desc;
	u8 std_as_data_ep_desc_valid;
	struct usb_endpoint_descriptor_v01 std_as_data_ep_desc;
	u8 std_as_sync_ep_desc_valid;
	struct usb_endpoint_descriptor_v01 std_as_sync_ep_desc;
	u8 usb_audio_spec_revision_valid;
	u16 usb_audio_spec_revision;
	u8 data_path_delay_valid;
	u8 data_path_delay;
	u8 usb_audio_subslot_size_valid;
	u8 usb_audio_subslot_size;
	u8 xhci_mem_info_valid;
	struct apps_mem_info_v01 xhci_mem_info;
	u8 interrupter_num_valid;
	u8 interrupter_num;
	u8 speed_info_valid;
	enum usb_audio_device_speed_enum_v01 speed_info;
	u8 controller_num_valid;
	u8 controller_num;
};
#define QMI_UAUDIO_STREAM_RESP_MSG_V01_MAX_MSG_LEN 202
extern struct qmi_elem_info qmi_uaudio_stream_resp_msg_v01_ei[];

struct qmi_uaudio_stream_ind_msg_v01 {
	enum usb_audio_device_indication_enum_v01 dev_event;
	u32 slot_id;
	u8 usb_token_valid;
	u32 usb_token;
	u8 std_as_opr_intf_desc_valid;
	struct usb_interface_descriptor_v01 std_as_opr_intf_desc;
	u8 std_as_data_ep_desc_valid;
	struct usb_endpoint_descriptor_v01 std_as_data_ep_desc;
	u8 std_as_sync_ep_desc_valid;
	struct usb_endpoint_descriptor_v01 std_as_sync_ep_desc;
	u8 usb_audio_spec_revision_valid;
	u16 usb_audio_spec_revision;
	u8 data_path_delay_valid;
	u8 data_path_delay;
	u8 usb_audio_subslot_size_valid;
	u8 usb_audio_subslot_size;
	u8 xhci_mem_info_valid;
	struct apps_mem_info_v01 xhci_mem_info;
	u8 interrupter_num_valid;
	u8 interrupter_num;
	u8 controller_num_valid;
	u8 controller_num;
};
#define QMI_UAUDIO_STREAM_IND_MSG_V01_MAX_MSG_LEN 181
extern struct qmi_elem_info qmi_uaudio_stream_ind_msg_v01_ei[];

#endif
