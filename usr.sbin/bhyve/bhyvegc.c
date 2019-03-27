/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bhyvegc.h"

struct bhyvegc {
	struct bhyvegc_image	*gc_image;
	int raw;
};

struct bhyvegc *
bhyvegc_init(int width, int height, void *fbaddr)
{
	struct bhyvegc *gc;
	struct bhyvegc_image *gc_image;

	gc = calloc(1, sizeof (struct bhyvegc));

	gc_image = calloc(1, sizeof(struct bhyvegc_image));
	gc_image->width = width;
	gc_image->height = height;
	if (fbaddr) {
		gc_image->data = fbaddr;
		gc->raw = 1;
	} else {
		gc_image->data = calloc(width * height, sizeof (uint32_t));
		gc->raw = 0;
	}

	gc->gc_image = gc_image;

	return (gc);
}

void
bhyvegc_set_fbaddr(struct bhyvegc *gc, void *fbaddr)
{
	gc->raw = 1;
	if (gc->gc_image->data && gc->gc_image->data != fbaddr)
		free(gc->gc_image->data);
	gc->gc_image->data = fbaddr;
}

void
bhyvegc_resize(struct bhyvegc *gc, int width, int height)
{
	struct bhyvegc_image *gc_image;

	gc_image = gc->gc_image;

	gc_image->width = width;
	gc_image->height = height;
	if (!gc->raw) {
		gc_image->data = reallocarray(gc_image->data, width * height,
		    sizeof (uint32_t));
		if (gc_image->data != NULL)
			memset(gc_image->data, 0, width * height *
			    sizeof (uint32_t));
	}
}

struct bhyvegc_image *
bhyvegc_get_image(struct bhyvegc *gc)
{
	if (gc == NULL)
		return (NULL);

	return (gc->gc_image);
}
