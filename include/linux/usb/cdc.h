// SPDX-License-Identifier: GPL-2.0
/*
 * USB CDC common helpers
 *
 * Copyright (c) 2015 Oliver Neukum <oneukum@suse.com>
 */
#ifndef __LINUX_USB_CDC_H
#define __LINUX_USB_CDC_H

#include <uapi/linux/usb/cdc.h>

/*
 * inofficial magic numbers
 */

#define CDC_PHONET_MAGIC_NUMBER		0xAB

/*
 * parsing CDC headers
 */

struct usb_cdc_parsed_header {
	struct usb_cdc_union_desc *usb_cdc_union_desc;
	struct usb_cdc_header_desc *usb_cdc_header_desc;

	struct usb_cdc_call_mgmt_descriptor *usb_cdc_call_mgmt_descriptor;
	struct usb_cdc_acm_descriptor *usb_cdc_acm_descriptor;
	struct usb_cdc_country_functional_desc *usb_cdc_country_functional_desc;
	struct usb_cdc_network_terminal_desc *usb_cdc_network_terminal_desc;
	struct usb_cdc_ether_desc *usb_cdc_ether_desc;
	struct usb_cdc_dmm_desc *usb_cdc_dmm_desc;
	struct usb_cdc_mdlm_desc *usb_cdc_mdlm_desc;
	struct usb_cdc_mdlm_detail_desc *usb_cdc_mdlm_detail_desc;
	struct usb_cdc_obex_desc *usb_cdc_obex_desc;
	struct usb_cdc_ncm_desc *usb_cdc_ncm_desc;
	struct usb_cdc_mbim_desc *usb_cdc_mbim_desc;
	struct usb_cdc_mbim_extended_desc *usb_cdc_mbim_extended_desc;

	bool phonet_magic_present;
};

struct usb_interface;
int cdc_parse_cdc_header(struct usb_cdc_parsed_header *hdr,
				struct usb_interface *intf,
				u8 *buffer,
				int buflen);

#endif /* __LINUX_USB_CDC_H */
