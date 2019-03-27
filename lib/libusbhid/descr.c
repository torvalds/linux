/*	$NetBSD: descr.c,v 1.9 2000/09/24 02:13:24 augustss Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 Lennart Augustsson <augustss@netbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <dev/usb/usb_ioctl.h>

#include "usbhid.h"
#include "usbvar.h"

int
hid_set_immed(int fd, int enable)
{
	int ret;
	ret = ioctl(fd, USB_SET_IMMED, &enable);
#ifdef HID_COMPAT7
	if (ret < 0)
		ret = hid_set_immed_compat7(fd, enable);
#endif
	return (ret);
}

int
hid_get_report_id(int fd)
{
	report_desc_t rep;
	hid_data_t d;
	hid_item_t h;
	int kindset;
	int temp = -1;
	int ret;

	if ((rep = hid_get_report_desc(fd)) == NULL)
		goto use_ioctl;
	kindset = 1 << hid_input | 1 << hid_output | 1 << hid_feature;
	for (d = hid_start_parse(rep, kindset, -1); hid_get_item(d, &h); ) {
		/* Return the first report ID we met. */
		if (h.report_ID != 0) {
			temp = h.report_ID;
			break;
		}
	}
	hid_end_parse(d);
	hid_dispose_report_desc(rep);

	if (temp > 0)
		return (temp);

use_ioctl:
	ret = ioctl(fd, USB_GET_REPORT_ID, &temp);
#ifdef HID_COMPAT7
	if (ret < 0)
		ret = hid_get_report_id_compat7(fd);
	else
#endif
		ret = temp;

	return (ret);
}

report_desc_t
hid_get_report_desc(int fd)
{
	struct usb_gen_descriptor ugd;
	report_desc_t rep;
	void *data;

	memset(&ugd, 0, sizeof(ugd));

	/* get actual length first */
	ugd.ugd_data = hid_pass_ptr(NULL);
	ugd.ugd_maxlen = 65535;
	if (ioctl(fd, USB_GET_REPORT_DESC, &ugd) < 0) {
#ifdef HID_COMPAT7
		/* could not read descriptor */
		/* try FreeBSD 7 compat code */
		return (hid_get_report_desc_compat7(fd));
#else
		return (NULL);
#endif
	}

	/*
	 * NOTE: The kernel will return a failure if 
	 * "ugd_actlen" is zero.
	 */
	data = malloc(ugd.ugd_actlen);
	if (data == NULL)
		return (NULL);

	/* fetch actual descriptor */
	ugd.ugd_data = hid_pass_ptr(data);
	ugd.ugd_maxlen = ugd.ugd_actlen;
	if (ioctl(fd, USB_GET_REPORT_DESC, &ugd) < 0) {
		/* could not read descriptor */
		free(data);
		return (NULL);
	}

	/* sanity check */
	if (ugd.ugd_actlen < 1) {
		/* invalid report descriptor */
		free(data);
		return (NULL);
	}

	/* check END_COLLECTION */
	if (((unsigned char *)data)[ugd.ugd_actlen -1] != 0xC0) {
		/* invalid end byte */
		free(data);
		return (NULL);
	}

	rep = hid_use_report_desc(data, ugd.ugd_actlen);

	free(data);

	return (rep);
}

report_desc_t
hid_use_report_desc(unsigned char *data, unsigned int size)
{
	report_desc_t r;

	r = malloc(sizeof(*r) + size);
	if (r == NULL) {
		errno = ENOMEM;
		return (NULL);
	}
	r->size = size;
	memcpy(r->data, data, size);
	return (r);
}

void
hid_dispose_report_desc(report_desc_t r)
{

	free(r);
}
