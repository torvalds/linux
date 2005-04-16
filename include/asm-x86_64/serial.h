/*
 * include/asm-x86_64/serial.h
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
#define HUB6_FLAGS 0
#endif

#define MCA_COM_FLAGS	(STD_COM_FLAGS|ASYNC_BOOT_ONLYMCA)

/*
 * The following define the access methods for the HUB6 card. All
 * access is through two ports for all 24 possible chips. The card is
 * selected through the high 2 bits, the port on that card with the
 * "middle" 3 bits, and the register on that port with the bottom
 * 3 bits.
 *
 * While the access port and interrupt is configurable, the default
 * port locations are 0x302 for the port control register, and 0x303
 * for the data read/write register. Normally, the interrupt is at irq3
 * but can be anything from 3 to 7 inclusive. Note that using 3 will
 * require disabling com2.
 */

#define C_P(card,port) (((card)<<6|(port)<<3) + 1)

#define STD_SERIAL_PORT_DEFNS			\
	/* UART CLK   PORT IRQ     FLAGS        */			\
	{ 0, BASE_BAUD, 0x3F8, 4, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, 3, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0x3E8, 4, STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0x2E8, 3, STD_COM4_FLAGS },	/* ttyS3 */


#ifdef CONFIG_SERIAL_MANY_PORTS
#define EXTRA_SERIAL_PORT_DEFNS			\
	{ 0, BASE_BAUD, 0x1A0, 9, FOURPORT_FLAGS }, 	/* ttyS4 */	\
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

/* You can have up to four HUB6's in the system, but I've only
 * included two cards here for a total of twelve ports.
 */
#if (defined(CONFIG_HUB6) && defined(CONFIG_SERIAL_MANY_PORTS))
#define HUB6_SERIAL_PORT_DFNS		\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(0,0) },  /* ttyS32 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(0,1) },  /* ttyS33 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(0,2) },  /* ttyS34 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(0,3) },  /* ttyS35 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(0,4) },  /* ttyS36 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(0,5) },  /* ttyS37 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(1,0) },  /* ttyS38 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(1,1) },  /* ttyS39 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(1,2) },  /* ttyS40 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(1,3) },  /* ttyS41 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(1,4) },  /* ttyS42 */	\
	{ 0, BASE_BAUD, 0x302, 3, HUB6_FLAGS, C_P(1,5) },  /* ttyS43 */
#else
#define HUB6_SERIAL_PORT_DFNS
#endif

#ifdef CONFIG_MCA
#define MCA_SERIAL_PORT_DFNS			\
	{ 0, BASE_BAUD, 0x3220, 3, MCA_COM_FLAGS },	\
	{ 0, BASE_BAUD, 0x3228, 3, MCA_COM_FLAGS },	\
	{ 0, BASE_BAUD, 0x4220, 3, MCA_COM_FLAGS },	\
	{ 0, BASE_BAUD, 0x4228, 3, MCA_COM_FLAGS },	\
	{ 0, BASE_BAUD, 0x5220, 3, MCA_COM_FLAGS },	\
	{ 0, BASE_BAUD, 0x5228, 3, MCA_COM_FLAGS },
#else
#define MCA_SERIAL_PORT_DFNS
#endif

#define SERIAL_PORT_DFNS		\
	STD_SERIAL_PORT_DEFNS		\
	EXTRA_SERIAL_PORT_DEFNS		\
	HUB6_SERIAL_PORT_DFNS		\
	MCA_SERIAL_PORT_DFNS

