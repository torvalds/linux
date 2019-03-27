/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994, 1995 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: elink.c,v 1.6 1995/01/07 21:37:54 mycroft Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Common code for dealing with 3COM ethernet cards.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/cpufunc.h>

#include <dev/ep/elink.h>

/*
 * Issue a `global reset' to all cards, and reset the ID state machines.  We
 * have to be careful to do the global reset only once during autoconfig, to
 * prevent resetting boards that have already been configured.
 */
void
elink_reset()
{
	static int x = 0;

	if (x == 0) {
		x = 1;
		outb(ELINK_ID_PORT, ELINK_RESET);
	}
	outb(ELINK_ID_PORT, 0x00);
	outb(ELINK_ID_PORT, 0x00);

	return;
}

/*
 * The `ID sequence' is really just snapshots of an 8-bit CRC register as 0
 * bits are shifted in.  Different board types use different polynomials.
 */
void
elink_idseq(u_char p)
{
	int i;
	u_char c;

	c = 0xff;
	for (i = 255; i; i--) {
		outb(ELINK_ID_PORT, c);
		if (c & 0x80) {
			c <<= 1;
			c ^= p;
		} else
			c <<= 1;
	}
}

static moduledata_t elink_mod = {
	"elink",/* module name */
	NULL,	/* event handler */
	0	/* extra data */
};

DECLARE_MODULE(elink, elink_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(elink, 1);
