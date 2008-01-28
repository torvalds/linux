/*
 * include/asm-arm/arch-orion/uncompress.h
 *
 * Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/arch/orion.h>

#define MV_UART_LSR 	((volatile unsigned char *)(UART0_BASE + 0x14))
#define MV_UART_THR	((volatile unsigned char *)(UART0_BASE + 0x0))

#define LSR_THRE	0x20

static void putc(const char c)
{
	int j = 0x1000;
	while (--j && !(*MV_UART_LSR & LSR_THRE))
		barrier();
	*MV_UART_THR = c;
}

static void flush(void)
{
}

static void orion_early_putstr(const char *ptr)
{
	char c;
	while ((c = *ptr++) != '\0') {
		if (c == '\n')
			putc('\r');
		putc(c);
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
