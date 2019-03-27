/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/nand/nand.h>

struct nand_params nand_ids[] = {
	{ { NAND_MAN_SAMSUNG, 0x75 }, "Samsung K9F5608U0B NAND 32MiB 8-bit",
	    0x20, 0x200, 0x10, 0x20, 0 },
	{ { NAND_MAN_SAMSUNG, 0xf1 }, "Samsung K9F1G08U0A NAND 128MiB 3,3V 8-bit",
	    0x80, 0x800, 0x40, 0x40, 0 },
	{ { NAND_MAN_SAMSUNG, 0xda }, "Samsung K9F2G08U0A NAND 256MiB 3,3V 8-bit",
	    0x100, 0x800, 0x40, 0x40, 0 },
	{ { NAND_MAN_SAMSUNG, 0xdc }, "Samsung NAND 512MiB 3,3V 8-bit",
	    0x200, 0x800, 0x40, 0x40, 0 },
	{ { NAND_MAN_SAMSUNG, 0xd3 }, "Samsung NAND 1GiB 3,3V 8-bit",
	    0x400, 0x800, 0x40, 0x40, 0 },
	{ { NAND_MAN_HYNIX, 0x76 }, "Hynix NAND 64MiB 3,3V 8-bit",
	    0x40, 0x200, 0x10, 0x20, 0 },
	{ { NAND_MAN_HYNIX, 0xdc }, "Hynix NAND 512MiB 3,3V 8-bit",
	    0x200, 0x800, 0x40, 0x40, 0 },
	{ { NAND_MAN_HYNIX, 0x79 }, "Hynix NAND 128MB 3,3V 8-bit",
	    0x80, 0x200, 0x10, 0x20, 0 },
	{ { NAND_MAN_STMICRO, 0xf1 }, "STMicro 128MB 3,3V 8-bit",
	    0x80, 2048, 64, 0x40, 0 },
	{ { NAND_MAN_MICRON, 0xcc }, "Micron NAND 512MiB 3,3V 16-bit",
	    0x200, 2048, 64, 0x40, 0 },
};

struct nand_params *nand_get_params(struct nand_id *id)
{
	int i;

	for (i = 0; i < nitems(nand_ids); i++)
		if (nand_ids[i].id.man_id == id->man_id &&
		    nand_ids[i].id.dev_id == id->dev_id)
			return (&nand_ids[i]);

	return (NULL);
}
