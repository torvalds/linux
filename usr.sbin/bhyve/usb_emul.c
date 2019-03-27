/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Nahanni Systems Inc.
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
#include <sys/queue.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "usb_emul.h"

SET_DECLARE(usb_emu_set, struct usb_devemu);

struct usb_devemu *
usb_emu_finddev(char *name)
{
	struct usb_devemu **udpp, *udp;

	SET_FOREACH(udpp, usb_emu_set) {
		udp = *udpp;
		if (!strcmp(udp->ue_emu, name))
			return (udp);
	}

	return (NULL);
}

struct usb_data_xfer_block *
usb_data_xfer_append(struct usb_data_xfer *xfer, void *buf, int blen,
                     void *hci_data, int ccs)
{
	struct usb_data_xfer_block *xb;

	if (xfer->ndata >= USB_MAX_XFER_BLOCKS)
		return (NULL);

	xb = &xfer->data[xfer->tail];
	xb->buf = buf;
	xb->blen = blen;
	xb->hci_data = hci_data;
	xb->ccs = ccs;
	xb->processed = 0;
	xb->bdone = 0;
	xfer->ndata++;
	xfer->tail = (xfer->tail + 1) % USB_MAX_XFER_BLOCKS;
	return (xb);
}
