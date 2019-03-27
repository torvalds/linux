/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *      Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _USBD_VAR_H_
#define _USBD_VAR_H_

#define	IOCTL_INTERNAL_USB_SUBMIT_URB			0x00220003

#define	URB_FUNCTION_SELECT_CONFIGURATION		0x0000
#define	URB_FUNCTION_ABORT_PIPE				0x0002
#define	URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER		0x0009
#define	URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE		0x000B
#define	URB_FUNCTION_VENDOR_DEVICE			0x0017
#define	URB_FUNCTION_VENDOR_INTERFACE			0x0018
#define	URB_FUNCTION_VENDOR_ENDPOINT			0x0019
#define	URB_FUNCTION_CLASS_DEVICE			0x001A
#define	URB_FUNCTION_CLASS_INTERFACE			0x001B
#define	URB_FUNCTION_CLASS_ENDPOINT			0x001C
#define	URB_FUNCTION_CLASS_OTHER			0x001F
#define	URB_FUNCTION_VENDOR_OTHER			0x0020

#define	USBD_STATUS_SUCCESS				0x00000000
#define	USBD_STATUS_CANCELED				0x00010000
#define	USBD_STATUS_PENDING				0x40000000
#define	USBD_STATUS_NO_MEMORY				0x80000100
#define	USBD_STATUS_REQUEST_FAILED			0x80000500
#define	USBD_STATUS_INVALID_PIPE_HANDLE			0x80000600
#define	USBD_STATUS_ERROR_SHORT_TRANSFER		0x80000900
#define	USBD_STATUS_CRC					0xC0000001
#define	USBD_STATUS_BTSTUFF				0xC0000002
#define	USBD_STATUS_DATA_TOGGLE_MISMATCH		0xC0000003
#define	USBD_STATUS_STALL_PID				0xC0000004
#define	USBD_STATUS_DEV_NOT_RESPONDING			0xC0000005
#define	USBD_STATUS_PID_CHECK_FAILURE			0xC0000006
#define	USBD_STATUS_UNEXPECTED_PID			0xC0000007
#define	USBD_STATUS_DATA_OVERRUN			0xC0000008
#define	USBD_STATUS_DATA_UNDERRUN			0xC0000009
#define	USBD_STATUS_RESERVED1				0xC000000A
#define	USBD_STATUS_RESERVED2				0xC000000B
#define	USBD_STATUS_BUFFER_OVERRUN			0xC000000C
#define	USBD_STATUS_BUFFER_UNDERRUN			0xC000000D
#define	USBD_STATUS_NOT_ACCESSED			0xC000000F
#define	USBD_STATUS_FIFO				0xC0000010
#define	USBD_STATUS_XACT_ERROR				0xC0000011
#define	USBD_STATUS_BABBLE_DETECTED			0xC0000012
#define	USBD_STATUS_DATA_BUFFER_ERROR			0xC0000013
#define	USBD_STATUS_NOT_SUPPORTED			0xC0000E00
#define	USBD_STATUS_TIMEOUT				0xC0006000
#define	USBD_STATUS_DEVICE_GONE				0xC0007000

struct usbd_urb_header {
	uint16_t		uuh_len;
	uint16_t		uuh_func;
	int32_t			uuh_status;
	void			*uuh_handle;
	uint32_t		uuh_flags;
};

enum usbd_pipe_type {
	UsbdPipeTypeControl	= UE_CONTROL,
	UsbdPipeTypeIsochronous	= UE_ISOCHRONOUS,
	UsbdPipeTypeBulk	= UE_BULK,
	UsbdPipeTypeInterrupt	= UE_INTERRUPT
};

struct usbd_pipe_information {
	uint16_t		upi_maxpktsize;
	uint8_t			upi_epaddr;
	uint8_t			upi_interval;
	enum usbd_pipe_type	upi_type;
	usb_endpoint_descriptor_t *upi_handle;
	uint32_t		upi_maxtxsize;
#define	USBD_DEFAULT_MAXIMUM_TRANSFER_SIZE		PAGE_SIZE
	uint32_t		upi_flags;
};

struct usbd_interface_information {
	uint16_t		uii_len;
	uint8_t			uii_intfnum;
	uint8_t			uii_altset;
	uint8_t			uii_intfclass;
	uint8_t			uii_intfsubclass;
	uint8_t			uii_intfproto;
	uint8_t			uii_reserved;
	void			*uii_handle;
	uint32_t		uii_numeps;
	struct usbd_pipe_information uii_pipes[1];
};

struct usbd_urb_select_interface {
	struct usbd_urb_header	usi_hdr;
	void			*usi_handle;
	struct usbd_interface_information uusi_intf;
};

struct usbd_urb_select_configuration {
	struct usbd_urb_header	usc_hdr;
	usb_config_descriptor_t *usc_conf;
	void			*usc_handle;
	struct usbd_interface_information usc_intf;
};

struct usbd_urb_pipe_request {
	struct usbd_urb_header		upr_hdr;
	usb_endpoint_descriptor_t	*upr_handle;
};

struct usbd_hcd_area {
	void			*reserved8[8];
};

struct usbd_urb_bulk_or_intr_transfer {
	struct usbd_urb_header	ubi_hdr;
	usb_endpoint_descriptor_t *ubi_epdesc;
	uint32_t		ubi_trans_flags;
#define	USBD_SHORT_TRANSFER_OK		0x00000002
	uint32_t		ubi_trans_buflen;
	void			*ubi_trans_buf;
	struct mdl		*ubi_mdl;
	union usbd_urb		*ubi_urblink;
	struct usbd_hcd_area	ubi_hca;
};

struct usbd_urb_control_descriptor_request {
	struct usbd_urb_header	ucd_hdr;
	void			*ucd_reserved0;
	uint32_t		ucd_reserved1;
	uint32_t		ucd_trans_buflen;
	void			*ucd_trans_buf;
	struct mdl		*ucd_mdl;
	union nt_urb		*ucd_urblink;
	struct usbd_hcd_area	ucd_hca;
	uint16_t		ucd_reserved2;
	uint8_t			ucd_idx;
	uint8_t			ucd_desctype;
	uint16_t		ucd_langid;
	uint16_t		ucd_reserved3;
};

struct usbd_urb_vendor_or_class_request {
	struct usbd_urb_header	uvc_hdr;
	void			*uvc_reserved0;
	uint32_t		uvc_trans_flags;
#define	USBD_TRANSFER_DIRECTION_IN	1
	uint32_t		uvc_trans_buflen;
	void			*uvc_trans_buf;
	struct mdl		*uvc_mdl;
	union nt_urb		*uvc_urblink;
	struct usbd_hcd_area	uvc_hca;
	uint8_t			uvc_reserved1;
	uint8_t			uvc_req;
	uint16_t		uvc_value;
	uint16_t		uvc_idx;
	uint16_t		uvc_reserved2;
};

struct usbd_interface_list_entry {
	usb_interface_descriptor_t		*uil_intfdesc;
	struct usbd_interface_information	*uil_intf;
};

union usbd_urb {
	struct usbd_urb_header			uu_hdr;
	struct usbd_urb_select_configuration	uu_selconf;
	struct usbd_urb_bulk_or_intr_transfer	uu_bulkintr;
	struct usbd_urb_control_descriptor_request	uu_ctldesc;
	struct usbd_urb_vendor_or_class_request	uu_vcreq;
	struct usbd_urb_pipe_request		uu_pipe;
};

#define	USBD_URB_STATUS(urb)	((urb)->uu_hdr.uuh_status)

#define	USBDI_VERSION		0x00000500
#define	USB_VER_1_1		0x00000110
#define	USB_VER_2_0		0x00000200

struct usbd_version_info {
	uint32_t		uvi_usbdi_vers;
	uint32_t		uvi_supported_vers;
};

typedef struct usbd_version_info usbd_version_info;

extern image_patch_table usbd_functbl[];

__BEGIN_DECLS
extern int usbd_libinit(void);
extern int usbd_libfini(void);
__END_DECLS

#endif /* _USBD_VAR_H_ */
