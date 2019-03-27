/*-
 * Copyright (c) 2007 Semihalf, Rafal Jaworowski <raj@semihalf.com>
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

#include <stand.h>
#include "bootstrap.h"
#include "glue.h"

int console;

static void uboot_cons_probe(struct console *cp);
static int uboot_cons_init(int);
static void uboot_cons_putchar(int);
static int uboot_cons_getchar(void);
static int uboot_cons_poll(void);

struct console uboot_console = {
	"uboot",
	"U-Boot console",
	0,
	uboot_cons_probe,
	uboot_cons_init,
	uboot_cons_putchar,
	uboot_cons_getchar,
	uboot_cons_poll,
};

static void
uboot_cons_probe(struct console *cp)
{

	cp->c_flags |= (C_PRESENTIN | C_PRESENTOUT);
}

static int
uboot_cons_init(int arg)
{

	return (0);
}

static void
uboot_cons_putchar(int c)
{

	if (c == '\n')
		ub_putc('\r');

	ub_putc(c);
}

static int
uboot_cons_getchar()
{

	return (ub_getc());
}

static int
uboot_cons_poll()
{

	return (ub_tstc());
}
