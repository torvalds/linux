/*	$OpenBSD: cmd_luna88k.c,v 1.1 2023/03/13 11:59:39 aoyama Exp $	*/
/*
 * Copyright (c) 2023 Kenji Aoyama
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <machine/board.h>

#include <luna88k/stand/boot/samachdep.h>
#include <stand/boot/cmd.h>

static int Xpoweroff(void);

const struct cmd_table cmd_machine[] = {
	{ "poweroff",	CMDT_CMD, Xpoweroff },
	{ NULL, 0 }
};

struct pio {
	volatile u_int8_t portA;
	volatile unsigned : 24;
	volatile u_int8_t portB;
	volatile unsigned : 24;
	volatile u_int8_t portC;
	volatile unsigned : 24;
	volatile u_int8_t cntrl;
	volatile unsigned : 24;
};

#define	PIO1_POWER 0x04
#define PIO1_DISABLE 0x00

static int
Xpoweroff(void)
{
	struct pio *p1 = (struct pio *)OBIO_PIO1_BASE;

	printf("attempting to power down...\n");

	p1->cntrl = (PIO1_POWER << 1) | PIO1_DISABLE;
	*(volatile u_int8_t *)&p1->portC;

	DELAY(1000000);	/* wait for a while */

	return 0;
}
