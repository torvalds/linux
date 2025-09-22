/*	$OpenBSD: descr.c,v 1.8 2021/05/30 19:54:52 jcs Exp $	*/
/*	$NetBSD: descr.c,v 1.2 2002/02/20 20:31:07 christos Exp $	*/

/*
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

#include <sys/types.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <dev/usb/usb.h>

#include "usbhid.h"
#include "usbvar.h"

report_desc_t
hid_get_report_desc(int fd)
{
	struct usb_ctl_report_desc rep;

	rep.ucrd_size = 0;
	if (ioctl(fd, USB_GET_REPORT_DESC, &rep) == -1)
		return (NULL);

	return hid_use_report_desc(rep.ucrd_data, (unsigned int)rep.ucrd_size);
}

report_desc_t
hid_use_report_desc(unsigned char *data, unsigned int size)
{
	report_desc_t r;

	r = malloc(sizeof(*r) + size);
	if (r == NULL)
		return (NULL);
	r->size = size;
	memcpy(r->data, data, size);
	return (r);
}

void
hid_dispose_report_desc(report_desc_t r)
{

	free(r);
}

void
hid_get_report_desc_data(report_desc_t d, uint8_t **data, uint32_t *size)
{
	*data = d->data;
	*size = d->size;
}
