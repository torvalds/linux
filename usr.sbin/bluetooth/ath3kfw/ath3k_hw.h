/*-
 * Copyright (c) 2013 Adrian Chadd <adrian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#ifndef	__ATH3K_HW_H__
#define	__ATH3K_HW_H__

#define	ATH3K_DNLOAD			0x01
#define	ATH3K_GETSTATE			0x05
#define	ATH3K_SET_NORMAL_MODE		0x07
#define	ATH3K_GETVERSION		0x09
#define	USB_REG_SWITCH_VID_PID		0x0a

#define	ATH3K_MODE_MASK			0x3F
#define	ATH3K_NORMAL_MODE		0x0E

#define	ATH3K_PATCH_UPDATE		0x80
#define	ATH3K_SYSCFG_UPDATE		0x40

#define	ATH3K_XTAL_FREQ_26M		0x00
#define	ATH3K_XTAL_FREQ_40M		0x01
#define	ATH3K_XTAL_FREQ_19P2		0x02
#define	ATH3K_NAME_LEN			0xFF

#define	USB_REQ_DFU_DNLOAD		1
#define	BULK_SIZE			4096
#define	FW_HDR_SIZE			20

extern	int ath3k_load_fwfile(struct libusb_device_handle *hdl,
	    const struct ath3k_firmware *fw);
extern	int ath3k_get_state(struct libusb_device_handle *hdl,
	    unsigned char *state);
extern	int ath3k_get_version(struct libusb_device_handle *hdl,
	    struct ath3k_version *version);
extern	int ath3k_load_patch(libusb_device_handle *hdl, const char *fw_path);
extern	int ath3k_load_syscfg(libusb_device_handle *hdl, const char *fw_path);
extern	int ath3k_set_normal_mode(libusb_device_handle *hdl);
extern	int ath3k_switch_pid(libusb_device_handle *hdl);

#endif
