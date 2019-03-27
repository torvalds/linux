/*-
 * Copyright (C) 2014 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include "bootstrap.h"
#include "host_syscall.h"

static void hostcons_probe(struct console *cp);
static int hostcons_init(int arg);
static void hostcons_putchar(int c);
static int hostcons_getchar();
static int hostcons_poll();

struct console hostconsole = {
	"host",
	"Host Console",
	0,
	hostcons_probe,
	hostcons_init,
	hostcons_putchar,
	hostcons_getchar,
	hostcons_poll,
};

static void
hostcons_probe(struct console *cp)
{

	cp->c_flags |= C_PRESENTIN|C_PRESENTOUT;
}

static int
hostcons_init(int arg)
{

	/* XXX: set nonblocking */
	/* tcsetattr(~(ICANON | ECHO)) */

	return (0);
}

static void
hostcons_putchar(int c)
{
	uint8_t ch = c;

	host_write(1, &ch, 1);
}

static int
hostcons_getchar()
{
	uint8_t ch;
	int rv;

	rv = host_read(0, &ch, 1);
	if (rv == 1)
		return (ch);
	return (-1);
}

static int
hostcons_poll()
{
	struct host_timeval tv = {0,0};
	long fds = 1 << 0;
	int ret;

	ret = host_select(32, &fds, NULL, NULL, &tv);
	return (ret > 0);
}

