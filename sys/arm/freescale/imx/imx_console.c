/*-
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Simple UART console driver for Freescale i.MX515 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <sys/consio.h>
#include <sys/kernel.h>

/* Allow it to be predefined, to be able to use another UART for console */
#ifndef	IMX_UART_BASE
#define	IMX_UART_BASE	0xe3fbc000 /* imx51 UART1 */
#endif

#define	IMX_RXD			0x00
#define	IMX_TXD			0x40

#define	IMX_UFCR		0x90
#define	IMX_USR1		0x94
#define	IMX_USR1_TRDY		(1 << 13)

#define	IMX_USR2		0x98
#define	IMX_USR2_RDR		(1 << 0)
#define	IMX_USR2_TXFE		(1 << 14)
#define	IMX_USR2_TXDC		(1 << 3)

#define	IMX_UTS			0xb4
#define	IMX_UTS_TXFULL		(1 << 4)

/*
 * The base address of the uart registers.
 *
 * This is global so that it can be changed on the fly from the outside.  For
 * example, set imx_uart_base=physaddr and then call cninit() as the first two
 * lines of initarm() and enjoy printf() availability through the tricky bits of
 * startup.  After initarm() switches from physical to virtual addressing, just
 * set imx_uart_base=virtaddr and printf keeps working.
 */
uint32_t imx_uart_base = IMX_UART_BASE;

/*
 * uart related funcs
 */
static uint32_t
ub_getreg(uint32_t off)
{

	return *((volatile uint32_t *)(imx_uart_base + off));
}

static void
ub_setreg(uint32_t off, uint32_t val)
{

	*((volatile uint32_t *)(imx_uart_base + off)) = val;
}

static int
ub_tstc(void)
{

	return ((ub_getreg(IMX_USR2) & IMX_USR2_RDR) ? 1 : 0);
}

static int
ub_getc(void)
{

	while (!ub_tstc());
		__asm __volatile("nop");

	return (ub_getreg(IMX_RXD) & 0xff);
}

static void
ub_putc(unsigned char c)
{

	if (c == '\n')
		ub_putc('\r');

	while (ub_getreg(IMX_UTS) & IMX_UTS_TXFULL)
		__asm __volatile("nop");

	ub_setreg(IMX_TXD, c);
}

static cn_probe_t	uart_cnprobe;
static cn_init_t	uart_cninit;
static cn_term_t	uart_cnterm;
static cn_getc_t	uart_cngetc;
static cn_putc_t	uart_cnputc;
static cn_grab_t	uart_cngrab;
static cn_ungrab_t	uart_cnungrab;

static void
uart_cngrab(struct consdev *cp)
{

}

static void
uart_cnungrab(struct consdev *cp)
{

}


static void
uart_cnprobe(struct consdev *cp)
{

        sprintf(cp->cn_name, "uart");
        cp->cn_pri = CN_NORMAL;
}

static void
uart_cninit(struct consdev *cp)
{

        /* Init fifo trigger levels to 32 bytes, refclock div to 2. */
	ub_setreg(IMX_UFCR, 0x00004210);
}

static void
uart_cnputc(struct consdev *cp, int c)
{

	ub_putc(c);
}

static int
uart_cngetc(struct consdev * cp)
{

	return ub_getc();
}

static void
uart_cnterm(struct consdev * cp)
{

}

CONSOLE_DRIVER(uart);
