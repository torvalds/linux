/*
 * include/asm-m68k/serial.h
 *
 * currently this seems useful only for a Q40,
 * it's an almost exact copy of ../asm-alpha/serial.h
 *
 */

#include <linux/config.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ( 1843200 / 16 )

/* Standard COM flags (except for COM4, because of the 8514 problem) */
#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)
#define STD_COM4_FLAGS ASYNC_BOOT_AUTOCONF
#endif

#ifdef CONFIG_SERIAL_MANY_PORTS
#define FOURPORT_FLAGS ASYNC_FOURPORT
#define ACCENT_FLAGS 0
#define BOCA_FLAGS 0
#endif

#define STD_SERIAL_PORT_DEFNS			\
	/* UART CLK   PORT IRQ     FLAGS        */			\
	{ 0, BASE_BAUD, 0x3F8, 4, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, 3, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0x3E8, 4, STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0x2E8, 3, STD_COM4_FLAGS },	/* ttyS3 */


#ifdef CONFIG_SERIAL_MANY_PORTS
#define EXTRA_SERIAL_PORT_DEFNS			\
	{ 0, BASE_BAUD, 0x1A0, 9, FOURPORT_FLAGS },	/* ttyS4 */	\
	{ 0, BASE_BAUD, 0x1A8, 9, FOURPORT_FLAGS },	/* ttyS5 */	\
	{ 0, BASE_BAUD, 0x1B0, 9, FOURPORT_FLAGS },	/* ttyS6 */	\
	{ 0, BASE_BAUD, 0x1B8, 9, FOURPORT_FLAGS },	/* ttyS7 */	\
	{ 0, BASE_BAUD, 0x2A0, 5, FOURPORT_FLAGS },	/* ttyS8 */	\
	{ 0, BASE_BAUD, 0x2A8, 5, FOURPORT_FLAGS },	/* ttyS9 */	\
	{ 0, BASE_BAUD, 0x2B0, 5, FOURPORT_FLAGS },	/* ttyS10 */	\
	{ 0, BASE_BAUD, 0x2B8, 5, FOURPORT_FLAGS },	/* ttyS11 */	\
	{ 0, BASE_BAUD, 0x330, 4, ACCENT_FLAGS },	/* ttyS12 */	\
	{ 0, BASE_BAUD, 0x338, 4, ACCENT_FLAGS },	/* ttyS13 */	\
	{ 0, BASE_BAUD, 0x000, 0, 0 },	/* ttyS14 (spare) */		\
	{ 0, BASE_BAUD, 0x000, 0, 0 },	/* ttyS15 (spare) */		\
	{ 0, BASE_BAUD, 0x100, 12, BOCA_FLAGS },	/* ttyS16 */	\
	{ 0, BASE_BAUD, 0x108, 12, BOCA_FLAGS },	/* ttyS17 */	\
	{ 0, BASE_BAUD, 0x110, 12, BOCA_FLAGS },	/* ttyS18 */	\
	{ 0, BASE_BAUD, 0x118, 12, BOCA_FLAGS },	/* ttyS19 */	\
	{ 0, BASE_BAUD, 0x120, 12, BOCA_FLAGS },	/* ttyS20 */	\
	{ 0, BASE_BAUD, 0x128, 12, BOCA_FLAGS },	/* ttyS21 */	\
	{ 0, BASE_BAUD, 0x130, 12, BOCA_FLAGS },	/* ttyS22 */	\
	{ 0, BASE_BAUD, 0x138, 12, BOCA_FLAGS },	/* ttyS23 */	\
	{ 0, BASE_BAUD, 0x140, 12, BOCA_FLAGS },	/* ttyS24 */	\
	{ 0, BASE_BAUD, 0x148, 12, BOCA_FLAGS },	/* ttyS25 */	\
	{ 0, BASE_BAUD, 0x150, 12, BOCA_FLAGS },	/* ttyS26 */	\
	{ 0, BASE_BAUD, 0x158, 12, BOCA_FLAGS },	/* ttyS27 */	\
	{ 0, BASE_BAUD, 0x160, 12, BOCA_FLAGS },	/* ttyS28 */	\
	{ 0, BASE_BAUD, 0x168, 12, BOCA_FLAGS },	/* ttyS29 */	\
	{ 0, BASE_BAUD, 0x170, 12, BOCA_FLAGS },	/* ttyS30 */	\
	{ 0, BASE_BAUD, 0x178, 12, BOCA_FLAGS },	/* ttyS31 */
#else
#define EXTRA_SERIAL_PORT_DEFNS
#endif

#define SERIAL_PORT_DFNS		\
	STD_SERIAL_PORT_DEFNS		\
	EXTRA_SERIAL_PORT_DEFNS
