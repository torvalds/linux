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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <sys/endian.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libusb.h>

#include "ath3k_fw.h"
#include "ath3k_hw.h"
#include "ath3k_dbg.h"

#define	XMIN(x, y)	((x) < (y) ? (x) : (y))

int
ath3k_load_fwfile(struct libusb_device_handle *hdl,
    const struct ath3k_firmware *fw)
{
	int size, count, sent = 0;
	int ret, r;

	count = fw->len;

	size = XMIN(count, FW_HDR_SIZE);

	ath3k_debug("%s: file=%s, size=%d\n",
	    __func__, fw->fwname, count);

	/*
	 * Flip the device over to configuration mode.
	 */
	ret = libusb_control_transfer(hdl,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
	    ATH3K_DNLOAD,
	    0,
	    0,
	    fw->buf + sent,
	    size,
	    1000);	/* XXX timeout */

	if (ret != size) {
		fprintf(stderr, "Can't switch to config mode; ret=%d\n",
		    ret);
		return (-1);
	}

	sent += size;
	count -= size;

	/* Load in the rest of the data */
	while (count) {
		size = XMIN(count, BULK_SIZE);
		ath3k_debug("%s: transferring %d bytes, offset %d\n",
		    __func__,
		    sent,
		    size);

		ret = libusb_bulk_transfer(hdl,
		    0x2,
		    fw->buf + sent,
		    size,
		    &r,
		    1000);

		if (ret < 0 || r != size) {
			fprintf(stderr, "Can't load firmware: err=%s, size=%d\n",
			    libusb_strerror(ret),
			    size);
			return (-1);
		}
		sent  += size;
		count -= size;
	}
	return (0);
}

int
ath3k_get_state(struct libusb_device_handle *hdl, unsigned char *state)
{
	int ret;

	ret = libusb_control_transfer(hdl,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
	    ATH3K_GETSTATE,
	    0,
	    0,
	    state,
	    1,
	    1000);	/* XXX timeout */

	if (ret < 0) {
		fprintf(stderr,
		    "%s: libusb_control_transfer() failed: code=%d\n",
		    __func__,
		    ret);
		return (0);
	}

	return (ret == 1);
}

int
ath3k_get_version(struct libusb_device_handle *hdl,
    struct ath3k_version *version)
{
	int ret;

	ret = libusb_control_transfer(hdl,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
	    ATH3K_GETVERSION,
	    0,
	    0,
	    (unsigned char *) version,
	    sizeof(struct ath3k_version),
	    1000);	/* XXX timeout */

	if (ret < 0) {
		fprintf(stderr,
		    "%s: libusb_control_transfer() failed: code=%d\n",
		    __func__,
		    ret);
		return (0);
	}

	/* XXX endian fix! */

	return (ret == sizeof(struct ath3k_version));
}

int
ath3k_load_patch(libusb_device_handle *hdl, const char *fw_path)
{
	int ret;
	unsigned char fw_state;
	struct ath3k_version fw_ver, pt_ver;
	char fwname[FILENAME_MAX];
	struct ath3k_firmware fw;
	uint32_t tmp;

	ret = ath3k_get_state(hdl, &fw_state);
	if (ret < 0) {
		ath3k_err("%s: Can't get state\n", __func__);
		return (ret);
	}

	if (fw_state & ATH3K_PATCH_UPDATE) {
		ath3k_info("%s: Patch already downloaded\n",
		    __func__);
		return (0);
	}

	ret = ath3k_get_version(hdl, &fw_ver);
	if (ret < 0) {
		ath3k_debug("%s: Can't get version\n", __func__);
		return (ret);
	}

	/* XXX path info? */
	snprintf(fwname, FILENAME_MAX, "%s/ar3k/AthrBT_0x%08x.dfu",
	    fw_path,
	    fw_ver.rom_version);

	/* Read in the firmware */
	if (ath3k_fw_read(&fw, fwname) <= 0) {
		ath3k_debug("%s: ath3k_fw_read() failed\n",
		    __func__);
		return (-1);
	}

	/*
	 * Extract the ROM/build version from the patch file.
	 */
	memcpy(&tmp, fw.buf + fw.len - 8, sizeof(tmp));
	pt_ver.rom_version = le32toh(tmp);
	memcpy(&tmp, fw.buf + fw.len - 4, sizeof(tmp));
	pt_ver.build_version = le32toh(tmp);

	ath3k_info("%s: file %s: rom_ver=%d, build_ver=%d\n",
	    __func__,
	    fwname,
	    (int) pt_ver.rom_version,
	    (int) pt_ver.build_version);

	/* Check the ROM/build version against the firmware */
	if ((pt_ver.rom_version != fw_ver.rom_version) ||
	    (pt_ver.build_version <= fw_ver.build_version)) {
		ath3k_debug("Patch file version mismatch!\n");
		ath3k_fw_free(&fw);
		return (-1);
	}

	/* Load in the firmware */
	ret = ath3k_load_fwfile(hdl, &fw);

	/* free it */
	ath3k_fw_free(&fw);

	return (ret);
}

int
ath3k_load_syscfg(libusb_device_handle *hdl, const char *fw_path)
{
	unsigned char fw_state;
	char filename[FILENAME_MAX];
	struct ath3k_firmware fw;
	struct ath3k_version fw_ver;
	int clk_value, ret;

	ret = ath3k_get_state(hdl, &fw_state);
	if (ret < 0) {
		ath3k_err("Can't get state to change to load configuration err");
		return (-EBUSY);
	}

	ret = ath3k_get_version(hdl, &fw_ver);
	if (ret < 0) {
		ath3k_err("Can't get version to change to load ram patch err");
		return (ret);
	}

	switch (fw_ver.ref_clock) {
	case ATH3K_XTAL_FREQ_26M:
		clk_value = 26;
		break;
	case ATH3K_XTAL_FREQ_40M:
		clk_value = 40;
		break;
	case ATH3K_XTAL_FREQ_19P2:
		clk_value = 19;
		break;
	default:
		clk_value = 0;
		break;
}

	snprintf(filename, FILENAME_MAX, "%s/ar3k/ramps_0x%08x_%d%s",
	    fw_path,
	    fw_ver.rom_version,
	    clk_value,
	    ".dfu");

	ath3k_info("%s: syscfg file = %s\n",
	    __func__,
	    filename);

	/* Read in the firmware */
	if (ath3k_fw_read(&fw, filename) <= 0) {
		ath3k_err("%s: ath3k_fw_read() failed\n",
		    __func__);
		return (-1);
	}

	ret = ath3k_load_fwfile(hdl, &fw);

	ath3k_fw_free(&fw);
	return (ret);
}

int
ath3k_set_normal_mode(libusb_device_handle *hdl)
{
	int ret;
	unsigned char fw_state;

	ret = ath3k_get_state(hdl, &fw_state);
	if (ret < 0) {
		ath3k_err("%s: can't get state\n", __func__);
		return (ret);
	}

	/*
	 * This isn't a fatal error - the device may have detached
	 * already.
	 */
	if ((fw_state & ATH3K_MODE_MASK) == ATH3K_NORMAL_MODE) {
		ath3k_debug("%s: firmware is already in normal mode\n",
		    __func__);
		return (0);
	}

	ret = libusb_control_transfer(hdl,
	    LIBUSB_REQUEST_TYPE_VENDOR,		/* XXX out direction? */
	    ATH3K_SET_NORMAL_MODE,
	    0,
	    0,
	    NULL,
	    0,
	    1000);	/* XXX timeout */

	if (ret < 0) {
		ath3k_err("%s: libusb_control_transfer() failed: code=%d\n",
		    __func__,
		    ret);
		return (0);
	}

	return (ret == 0);
}

int
ath3k_switch_pid(libusb_device_handle *hdl)
{
	int ret;
	ret = libusb_control_transfer(hdl,
	    LIBUSB_REQUEST_TYPE_VENDOR,		/* XXX set an out flag? */
	    USB_REG_SWITCH_VID_PID,
	    0,
	    0,
	    NULL,
	    0,
	    1000);	/* XXX timeout */

	if (ret < 0) {
		ath3k_debug("%s: libusb_control_transfer() failed: code=%d\n",
		    __func__,
		    ret);
		return (0);
	}

	return (ret == 0);
}
