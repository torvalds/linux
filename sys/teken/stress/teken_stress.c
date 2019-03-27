/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Ed Schouten <ed@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <teken.h>

static tf_bell_t	stress_bell;
static tf_cursor_t	stress_cursor;
static tf_putchar_t	stress_putchar;
static tf_fill_t	stress_fill;
static tf_copy_t	stress_copy;
static tf_param_t	stress_param;
static tf_respond_t	stress_respond;

static teken_funcs_t tf = {
	.tf_bell	= stress_bell,
	.tf_cursor	= stress_cursor,
	.tf_putchar	= stress_putchar,
	.tf_fill	= stress_fill,
	.tf_copy	= stress_copy,
	.tf_param	= stress_param,
	.tf_respond	= stress_respond,
};

static void
stress_bell(void *s __unused)
{
}

static void
stress_cursor(void *s __unused, const teken_pos_t *p __unused)
{
}

static void
stress_putchar(void *s __unused, const teken_pos_t *p __unused,
    teken_char_t c __unused, const teken_attr_t *a __unused)
{
}

static void
stress_fill(void *s __unused, const teken_rect_t *r __unused,
    teken_char_t c __unused, const teken_attr_t *a __unused)
{
}

static void
stress_copy(void *s __unused, const teken_rect_t *r __unused,
    const teken_pos_t *p __unused)
{
}

static void
stress_param(void *s __unused, int cmd __unused, unsigned int value __unused)
{
}

static void
stress_respond(void *s __unused, const void *buf __unused, size_t len __unused)
{
}

static const char replacement[] =
    { 0x1b, '[', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ';' };

int
main(int argc __unused, char *argv[] __unused)
{
	teken_t t;
	unsigned int i, iteration = 0;
	unsigned char buf[2048];


	teken_init(&t, &tf, NULL);

	for (;;) {
		arc4random_buf(buf, sizeof buf);
		for (i = 0; i < sizeof buf; i++) {
			if (buf[i] >= 0x80)
				buf[i] =
				    replacement[buf[i] % sizeof replacement];
		}

		teken_input(&t, buf, sizeof buf);

		iteration++;
		if ((iteration % 10000) == 0)
			printf("Processed %u frames\n", iteration);
	}
}
