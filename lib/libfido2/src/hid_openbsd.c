/*
 * Copyright (c) 2019 Google LLC. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <sys/types.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <usbhid.h>
#include <poll.h>

#include "fido.h"

#define MAX_UHID	64
#define MAX_U2FHID_LEN	64

struct hid_openbsd {
	int fd;
	size_t report_in_len;
	size_t report_out_len;
	sigset_t sigmask;
	const sigset_t *sigmaskp;
};

int
fido_hid_manifest(fido_dev_info_t *devlist, size_t ilen, size_t *olen)
{
	size_t i;
	char path[64];
	int fd;
	struct usb_device_info udi;
	fido_dev_info_t *di;

	if (ilen == 0)
		return (FIDO_OK); /* nothing to do */

	if (devlist == NULL || olen == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	for (i = *olen = 0; i < MAX_UHID && *olen < ilen; i++) {
		snprintf(path, sizeof(path), "/dev/fido/%zu", i);
		if ((fd = fido_hid_unix_open(path)) == -1)
			continue;
		if (close(fd) == -1)
			fido_log_error(errno, "%s: close", __func__);

		memset(&udi, 0, sizeof(udi));
		strlcpy(udi.udi_vendor, "OpenBSD", sizeof(udi.udi_vendor));
		strlcpy(udi.udi_product, "fido(4)", sizeof(udi.udi_product));
		udi.udi_vendorNo = 0x0b5d; /* stolen from PCI_VENDOR_OPENBSD */

		fido_log_debug("%s: %s: vendor = \"%s\", product = \"%s\"",
		    __func__, path, udi.udi_vendor, udi.udi_product);

		di = &devlist[*olen];
		memset(di, 0, sizeof(*di));
		di->io = (fido_dev_io_t) {
			fido_hid_open,
			fido_hid_close,
			fido_hid_read,
			fido_hid_write,
		};
		if ((di->path = strdup(path)) == NULL ||
		    (di->manufacturer = strdup(udi.udi_vendor)) == NULL ||
		    (di->product = strdup(udi.udi_product)) == NULL) {
			free(di->path);
			free(di->manufacturer);
			free(di->product);
			explicit_bzero(di, sizeof(*di));
			return FIDO_ERR_INTERNAL;
		}
		di->vendor_id = (int16_t)udi.udi_vendorNo;
		di->product_id = (int16_t)udi.udi_productNo;
		(*olen)++;
	}

	return FIDO_OK;
}

/*
 * Workaround for OpenBSD <=6.6-current (as of 201910) bug that loses
 * sync of DATA0/DATA1 sequence bit across uhid open/close.
 * Send pings until we get a response - early pings with incorrect
 * sequence bits will be ignored as duplicate packets by the device.
 */
static int
terrible_ping_kludge(struct hid_openbsd *ctx)
{
	u_char data[256];
	int i, n;
	struct pollfd pfd;

	if (sizeof(data) < ctx->report_out_len + 1)
		return -1;
	for (i = 0; i < 4; i++) {
		memset(data, 0, sizeof(data));
		/* broadcast channel ID */
		data[1] = 0xff;
		data[2] = 0xff;
		data[3] = 0xff;
		data[4] = 0xff;
		/* Ping command */
		data[5] = 0x81;
		/* One byte ping only, Vasili */
		data[6] = 0;
		data[7] = 1;
		fido_log_debug("%s: send ping %d", __func__, i);
		if (fido_hid_write(ctx, data, ctx->report_out_len + 1) == -1)
			return -1;
		fido_log_debug("%s: wait reply", __func__);
		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = ctx->fd;
		pfd.events = POLLIN;
		if ((n = poll(&pfd, 1, 100)) == -1) {
			fido_log_debug("%s: poll: %s", __func__, strerror(errno));
			return -1;
		} else if (n == 0) {
			fido_log_debug("%s: timed out", __func__);
			continue;
		}
		if (fido_hid_read(ctx, data, ctx->report_out_len, 250) == -1)
			return -1;
		/*
		 * Ping isn't always supported on the broadcast channel,
		 * so we might get an error, but we don't care - we're
		 * synched now.
		 */
		fido_log_debug("%s: got reply", __func__);
		fido_log_xxd(data, ctx->report_out_len, "%s", __func__);
		return 0;
	}
	fido_log_debug("%s: no response", __func__);
	return -1;
}

void *
fido_hid_open(const char *path)
{
	struct hid_openbsd *ret = NULL;

	if ((ret = calloc(1, sizeof(*ret))) == NULL ||
	    (ret->fd = fido_hid_unix_open(path)) == -1) {
		free(ret);
		return (NULL);
	}
	ret->report_in_len = ret->report_out_len = CTAP_MAX_REPORT_LEN;
	fido_log_debug("%s: inlen = %zu outlen = %zu", __func__,
	    ret->report_in_len, ret->report_out_len);

	/*
	 * OpenBSD (as of 201910) has a bug that causes it to lose
	 * track of the DATA0/DATA1 sequence toggle across uhid device
	 * open and close. This is a terrible hack to work around it.
	 */
	if (terrible_ping_kludge(ret) != 0) {
		fido_hid_close(ret);
		return NULL;
	}

	return (ret);
}

void
fido_hid_close(void *handle)
{
	struct hid_openbsd *ctx = (struct hid_openbsd *)handle;

	if (close(ctx->fd) == -1)
		fido_log_error(errno, "%s: close", __func__);

	free(ctx);
}

int
fido_hid_set_sigmask(void *handle, const fido_sigset_t *sigmask)
{
	struct hid_openbsd *ctx = handle;

	ctx->sigmask = *sigmask;
	ctx->sigmaskp = &ctx->sigmask;

	return (FIDO_OK);
}

int
fido_hid_read(void *handle, unsigned char *buf, size_t len, int ms)
{
	struct hid_openbsd *ctx = (struct hid_openbsd *)handle;
	ssize_t r;

	if (len != ctx->report_in_len) {
		fido_log_debug("%s: invalid len: got %zu, want %zu", __func__,
		    len, ctx->report_in_len);
		return (-1);
	}

	if (fido_hid_unix_wait(ctx->fd, ms, ctx->sigmaskp) < 0) {
		fido_log_debug("%s: fd not ready", __func__);
		return (-1);
	}

	if ((r = read(ctx->fd, buf, len)) == -1) {
		fido_log_error(errno, "%s: read", __func__);
		return (-1);
	}

	if (r < 0 || (size_t)r != len) {
		fido_log_debug("%s: %zd != %zu", __func__, r, len);
		return (-1);
	}

	return ((int)len);
}

int
fido_hid_write(void *handle, const unsigned char *buf, size_t len)
{
	struct hid_openbsd *ctx = (struct hid_openbsd *)handle;
	ssize_t r;

	if (len != ctx->report_out_len + 1) {
		fido_log_debug("%s: invalid len: got %zu, want %zu", __func__,
		    len, ctx->report_out_len);
		return (-1);
	}

	if ((r = write(ctx->fd, buf + 1, len - 1)) == -1) {
		fido_log_error(errno, "%s: write", __func__);
		return (-1);
	}

	if (r < 0 || (size_t)r != len - 1) {
		fido_log_debug("%s: %zd != %zu", __func__, r, len - 1);
		return (-1);
	}

	return ((int)len);
}

size_t
fido_hid_report_in_len(void *handle)
{
	struct hid_openbsd *ctx = handle;

	return (ctx->report_in_len);
}

size_t
fido_hid_report_out_len(void *handle)
{
	struct hid_openbsd *ctx = handle;

	return (ctx->report_out_len);
}
