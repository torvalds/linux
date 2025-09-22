/*	$OpenBSD: bioscons.c,v 1.11 2016/05/27 05:37:51 beck Exp $	*/

/*
 * Copyright (c) 1997-1999 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <machine/biosvar.h>
#include <machine/pio.h>

#include <dev/cons.h>
#include <dev/ic/mc146818reg.h>
#include <dev/ic/comreg.h>
#include <dev/ic/ns16450reg.h>
#include <dev/isa/isareg.h>

#include <lib/libsa/stand.h>

#include "biosdev.h"

/* XXX cannot trust NVRAM on this.  Maybe later we make a real probe.  */
#if 0
#define PRESENT_MASK (NVRAM_EQUIPMENT_KBD|NVRAM_EQUIPMENT_DISPLAY)
#else
#define PRESENT_MASK 0
#endif

void
pc_probe(struct consdev *cn)
{
	cn->cn_pri = CN_MIDPRI;
	cn->cn_dev = makedev(12, 0);
	printf(" pc%d", minor(cn->cn_dev));

#if 0
	outb(IO_RTC, NVRAM_EQUIPMENT);
	if ((inb(IO_RTC+1) & PRESENT_MASK) == PRESENT_MASK) {
		cn->cn_pri = CN_MIDPRI;
		/* XXX from i386/conf.c */
		cn->cn_dev = makedev(12, 0);
		printf(" pc%d", minor(cn->cn_dev));
	}
#endif
}

void
pc_init(struct consdev *cn)
{
}

int
pc_getc(dev_t dev)
{
	register int rv;

	if (dev & 0x80) {
		__asm volatile(DOINT(0x16) "; setnz %b0" : "=a" (rv) :
		    "0" (0x100) : "%ecx", "%edx", "cc" );
		return (rv & 0xff);
	}

	/*
	 * Wait for a character to actually become available.  Appears to
	 * be necessary on (at least) the Intel Mac Mini.
	 */
	do {
		__asm volatile(DOINT(0x16) "; setnz %b0" : "=a" (rv) :
		    "0" (0x100) : "%ecx", "%edx", "cc" );
	} while ((rv & 0xff) == 0);

	__asm volatile(DOINT(0x16) : "=a" (rv) : "0" (0x000) :
	    "%ecx", "%edx", "cc" );

	return (rv & 0xff);
}

int
pc_getshifts(dev_t dev)
{
	register int rv;

	__asm volatile(DOINT(0x16) : "=a" (rv) : "0" (0x200) :
	    "%ecx", "%edx", "cc" );

	return (rv & 0xff);
}

void
pc_putc(dev_t dev, int c)
{
	__asm volatile(DOINT(0x10) : : "a" (c | 0xe00), "b" (1) :
	    "%ecx", "%edx", "cc" );
}

const int comports[4] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };

void
com_probe(struct consdev *cn)
{
	register int i, n;

	/* get equip. (9-11 # of coms) */
	__asm volatile(DOINT(0x11) : "=a" (n) : : "%ecx", "%edx", "cc");
	n >>= 9;
	n &= 7;
	for (i = 0; i < n; i++)
		printf(" com%d", i);

	cn->cn_pri = CN_LOWPRI;
	/* XXX from i386/conf.c */
	cn->cn_dev = makedev(8, 0);
}

int com_speed = -1;
int com_addr = -1;

void
com_init(struct consdev *cn)
{
	int port = (com_addr == -1) ? comports[minor(cn->cn_dev)] : com_addr;
	time_t tt = getsecs() + 1;
	u_long i = 1;

	outb(port + com_ier, 0);
	if (com_speed == -1)
		comspeed(cn->cn_dev, 9600); /* default speed is 9600 baud */
	outb(port + com_mcr, MCR_DTR | MCR_RTS);
	outb(port + com_ier, 0);
	outb(port + com_fifo, FIFO_ENABLE | FIFO_RCV_RST | FIFO_XMT_RST |
	    FIFO_TRIGGER_1);
	(void) inb(port + com_iir);

	/* A few ms delay for the chip, using the getsecs() API */
	while (!(i++ % 1000) && getsecs() < tt)
		;

	/* drain the input buffer */
	while (inb(port + com_lsr) & LSR_RXRDY)
		(void)inb(port + com_data);
}

int
com_getc(dev_t dev)
{
	int port = (com_addr == -1) ? comports[minor(dev & 0x7f)] : com_addr;

	if (dev & 0x80)
		return (inb(port + com_lsr) & LSR_RXRDY);

	while ((inb(port + com_lsr) & LSR_RXRDY) == 0)
		;

	return (inb(port + com_data) & 0xff);
}

/* call with sp == 0 to query the current speed */
int
comspeed(dev_t dev, int sp)
{
	int port = (com_addr == -1) ? comports[minor(dev)] : com_addr;
	int i, newsp;
	int err;

	if (sp <= 0)
		return com_speed;
	/* valid baud rate? */
	if (115200 < sp || sp < 75)
		return -1;

	/*
	 * Accepted speeds:
	 *   75 150 300 600 1200 2400 4800 9600 19200 38400 76800 and
	 *   14400 28800 57600 115200
	 */
	for (i = sp; i != 75 && i != 14400; i >>= 1)
		if (i & 1)
			return -1;

/* ripped screaming from dev/ic/com.c */
#define divrnd(n, q)    (((n)*2/(q)+1)/2)       /* divide and round off */
	newsp = divrnd((COM_FREQ / 16), sp);
	if (newsp <= 0)
		return -1;
	err = divrnd((COM_FREQ / 16) * 1000, sp * newsp) - 1000;
	if (err < 0)
		err = -err;
	if (err > COM_TOLERANCE)
		return -1;
#undef  divrnd

	if (com_speed != -1 && cn_tab && cn_tab->cn_dev == dev &&
	    com_speed != sp) {
		printf("com%d: changing speed to %d baud in 5 seconds, "
		    "change your terminal to match!\n\a",
		    minor(dev), sp);
		sleep(5);
	}

	outb(port + com_cfcr, LCR_DLAB);
	outb(port + com_dlbl, newsp);
	outb(port + com_dlbh, newsp>>8);
	outb(port + com_cfcr, LCR_8BITS);
	if (com_speed != -1)
		printf("\ncom%d: %d baud\n", minor(dev), sp);

	newsp = com_speed;
	com_speed = sp;
	return newsp;
}

void
com_putc(dev_t dev, int c)
{
	int port = (com_addr == -1) ? comports[minor(dev)] : com_addr;

	while ((inb(port + com_lsr) & LSR_TXRDY) == 0)
		;

	outb(port + com_data, c);
}
