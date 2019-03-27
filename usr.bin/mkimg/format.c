/*-
 * Copyright (c) 2014 Juniper Networks, Inc.
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

#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "image.h"
#include "format.h"

static struct mkimg_format *first;
static struct mkimg_format *format;

struct mkimg_format *
format_iterate(struct mkimg_format *f)
{

	return ((f == NULL) ? first : f->next);
}

void
format_register(struct mkimg_format *f)
{

	f->next = first;
	first = f;
}

int
format_resize(lba_t end)
{

	if (format == NULL)
		return (ENOSYS);
	return (format->resize(end));
}

int
format_select(const char *spec)
{
	struct mkimg_format *f;

	f = NULL;
	while ((f = format_iterate(f)) != NULL) {
		if (strcasecmp(spec, f->name) == 0) {
			format = f;
			return (0);
		}
	}
	return (EINVAL);
}

struct mkimg_format *
format_selected(void)
{

	return (format);
}

int
format_write(int fd)
{
	int error;

	if (format == NULL)
		return (ENOSYS);
	error = format->write(fd);
	return (error);
}
