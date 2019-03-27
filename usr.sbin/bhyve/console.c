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

#include "bhyvegc.h"
#include "console.h"

static struct {
	struct bhyvegc		*gc;

	fb_render_func_t	fb_render_cb;
	void			*fb_arg;

	kbd_event_func_t	kbd_event_cb;
	void			*kbd_arg;
	int			kbd_priority;

	ptr_event_func_t	ptr_event_cb;
	void			*ptr_arg;
	int			ptr_priority;
} console;

void
console_init(int w, int h, void *fbaddr)
{
	console.gc = bhyvegc_init(w, h, fbaddr);
}

void
console_set_fbaddr(void *fbaddr)
{
	bhyvegc_set_fbaddr(console.gc, fbaddr);
}

struct bhyvegc_image *
console_get_image(void)
{
	struct bhyvegc_image *bhyvegc_image;

	bhyvegc_image = bhyvegc_get_image(console.gc);

	return (bhyvegc_image);
}

void
console_fb_register(fb_render_func_t render_cb, void *arg)
{
	console.fb_render_cb = render_cb;
	console.fb_arg = arg;
}

void
console_refresh(void)
{
	if (console.fb_render_cb)
		(*console.fb_render_cb)(console.gc, console.fb_arg);
}

void
console_kbd_register(kbd_event_func_t event_cb, void *arg, int pri)
{
	if (pri > console.kbd_priority) {
		console.kbd_event_cb = event_cb;
		console.kbd_arg = arg;
		console.kbd_priority = pri;
	}
}

void
console_ptr_register(ptr_event_func_t event_cb, void *arg, int pri)
{
	if (pri > console.ptr_priority) {
		console.ptr_event_cb = event_cb;
		console.ptr_arg = arg;
		console.ptr_priority = pri;
	}
}

void
console_key_event(int down, uint32_t keysym)
{
	if (console.kbd_event_cb)
		(*console.kbd_event_cb)(down, keysym, console.kbd_arg);
}

void
console_ptr_event(uint8_t button, int x, int y)
{
	if (console.ptr_event_cb)
		(*console.ptr_event_cb)(button, x, y, console.ptr_arg);
}
