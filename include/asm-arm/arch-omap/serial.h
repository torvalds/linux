/*
 *  linux/include/asm-arm/arch-omap/serial.h
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#if defined(CONFIG_ARCH_OMAP1)
/* OMAP1 serial ports */
#define OMAP_UART1_BASE		0xfffb0000
#define OMAP_UART2_BASE		0xfffb0800
#define OMAP_UART3_BASE		0xfffb9800
#elif defined(CONFIG_ARCH_OMAP2)
/* OMAP2 serial ports */
#define OMAP_UART1_BASE		0x4806a000
#define OMAP_UART2_BASE		0x4806c000
#define OMAP_UART3_BASE		0x4806e000
#endif

#define OMAP_MAX_NR_PORTS	3
#define OMAP1510_BASE_BAUD	(12000000/16)
#define OMAP16XX_BASE_BAUD	(48000000/16)

#define is_omap_port(p)	({int __ret = 0;			\
			if (p == IO_ADDRESS(OMAP_UART1_BASE) ||	\
			    p == IO_ADDRESS(OMAP_UART2_BASE) ||	\
			    p == IO_ADDRESS(OMAP_UART3_BASE))	\
				__ret = 1;			\
			__ret;					\
			})

#endif
