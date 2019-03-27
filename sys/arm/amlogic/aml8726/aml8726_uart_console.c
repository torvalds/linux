/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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

/*
 * Amlogic aml8726 UART console driver.
 *
 * This is only necessary to use when debugging early boot code.
 * The standard uart driver is available for use later in the boot.
 *
 * It's assumed the SoC uart is mapped into AML_UART_KVM_BASE meaning
 * when using EARLY_PRINTF you'll need to define SOCDEV_VA to be
 * 0xd8100000 and SOCDEV_PA to be 0xc8100000 in your config file.
 */

#include "opt_global.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/consio.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <arm/amlogic/aml8726/aml8726_machdep.h>
#include <arm/amlogic/aml8726/aml8726_uart.h>

#define	AML_UART_KVM_BASE	(aml8726_aobus_kva_base + 0x130 * 4)

static uint32_t
ub_getreg(uint32_t off)
{

	return *((volatile uint32_t *)(AML_UART_KVM_BASE + off));
}

static void
ub_setreg(uint32_t off, uint32_t val)
{

	*((volatile uint32_t *)(AML_UART_KVM_BASE + off)) = val;
}

static void
uart_cnprobe(struct consdev *cp)
{

	sprintf(cp->cn_name, "uart");
	cp->cn_pri = CN_REMOTE;
}

static void
uart_cngrab(struct consdev *cp)
{
}

static void
uart_cnungrab(struct consdev *cp)
{
}

static void
uart_cninit(struct consdev *cp)
{
	uint32_t cr;
	uint32_t mr;

#ifdef EARLY_PRINTF
	if (early_putc != NULL) {
		printf("Early printf yielding control to the real console.\n");
		early_putc = NULL;
	}

	/*
	 * Give pending characters a chance to drain.
	 */
	DELAY(4000);
#endif

	cr = ub_getreg(AML_UART_CONTROL_REG);
	/* Disable all interrupt sources. */
	cr &= ~(AML_UART_CONTROL_TX_INT_EN | AML_UART_CONTROL_RX_INT_EN);
	/* Reset the transmitter and receiver. */
	cr |= (AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);
	/* Use two wire mode. */
	cr |= AML_UART_CONTROL_TWO_WIRE_EN;
	/* Enable the transmitter and receiver. */
	cr |= (AML_UART_CONTROL_TX_EN | AML_UART_CONTROL_RX_EN);
	ub_setreg(AML_UART_CONTROL_REG, cr);

	/* Clear RX FIFO level for generating interrupts. */
	mr = ub_getreg(AML_UART_MISC_REG);
	mr &= ~AML_UART_MISC_RECV_IRQ_CNT_MASK;
	ub_setreg(AML_UART_MISC_REG, mr);

	/* Ensure the reset bits are clear. */
	cr &= ~(AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);
	ub_setreg(AML_UART_CONTROL_REG, cr);
}

static void
uart_cnterm(struct consdev * cp)
{
}

static void
uart_cnputc(struct consdev *cp, int c)
{

	while ((ub_getreg(AML_UART_STATUS_REG) &
	    AML_UART_STATUS_TX_FIFO_FULL) != 0)
		cpu_spinwait();

	ub_setreg(AML_UART_WFIFO_REG, c);
}

static int
uart_cngetc(struct consdev * cp)
{
	int c;

	if ((ub_getreg(AML_UART_STATUS_REG) &
	    AML_UART_STATUS_RX_FIFO_EMPTY) != 0)
		return (-1);

	c = ub_getreg(AML_UART_RFIFO_REG) & 0xff;

	return (c);
}

CONSOLE_DRIVER(uart);

#ifdef EARLY_PRINTF

#if !(defined(SOCDEV_PA) && defined(SOCDEV_VA))
#error SOCDEV_PA and SOCDEV_VA must be defined.
#endif

static void
eputc(int c)
{

	if (c == '\n')
		eputc('\r');

	uart_cnputc(NULL, c);
}

early_putc_t *early_putc = eputc;
#endif
