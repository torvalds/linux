/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#ifndef WITHOUT_CAPSICUM
#include <sys/capsicum.h>
#endif
#include <sys/select.h>

#ifndef WITHOUT_CAPSICUM
#include <capsicum_helpers.h>
#endif
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>
#include <sysexits.h>

#include "inout.h"
#include "pci_lpc.h"

#define	BVM_CONSOLE_PORT	0x220
#define	BVM_CONS_SIG		('b' << 8 | 'v')

static struct termios tio_orig, tio_new;

static void
ttyclose(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &tio_orig);
}

static void
ttyopen(void)
{
	tcgetattr(STDIN_FILENO, &tio_orig);

	cfmakeraw(&tio_new);
	tcsetattr(STDIN_FILENO, TCSANOW, &tio_new);	

	atexit(ttyclose);
}

static bool
tty_char_available(void)
{
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
		return (true);
	} else {
		return (false);
	}
}

static int
ttyread(void)
{
	char rb;

	if (tty_char_available()) {
		read(STDIN_FILENO, &rb, 1);
		return (rb & 0xff);
	} else {
		return (-1);
	}
}

static void
ttywrite(unsigned char wb)
{
	(void) write(STDOUT_FILENO, &wb, 1);
}

static int
console_handler(struct vmctx *ctx, int vcpu, int in, int port, int bytes,
		uint32_t *eax, void *arg)
{
	static int opened;
#ifndef WITHOUT_CAPSICUM
	cap_rights_t rights;
	cap_ioctl_t cmds[] = { TIOCGETA, TIOCSETA, TIOCGWINSZ };
#endif

	if (bytes == 2 && in) {
		*eax = BVM_CONS_SIG;
		return (0);
	}

	/*
	 * Guests might probe this port to look for old ISA devices
	 * using single-byte reads.  Return 0xff for those.
	 */
	if (bytes == 1 && in) {
		*eax = 0xff;
		return (0);
	}

	if (bytes != 4)
		return (-1);

	if (!opened) {
#ifndef WITHOUT_CAPSICUM
		cap_rights_init(&rights, CAP_EVENT, CAP_IOCTL, CAP_READ,
		    CAP_WRITE);
		if (caph_rights_limit(STDIN_FILENO, &rights) == -1)
			errx(EX_OSERR, "Unable to apply rights for sandbox");
		if (caph_ioctls_limit(STDIN_FILENO, cmds, nitems(cmds)) == -1)
			errx(EX_OSERR, "Unable to apply rights for sandbox");
#endif
		ttyopen();
		opened = 1;
	}
	
	if (in)
		*eax = ttyread();
	else
		ttywrite(*eax);

	return (0);
}

SYSRES_IO(BVM_CONSOLE_PORT, 4);

static struct inout_port consport = {
	"bvmcons",
	BVM_CONSOLE_PORT,
	1,
	IOPORT_F_INOUT,
	console_handler
};

void
init_bvmcons(void)
{

	register_inout(&consport);
}
