/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SERIAL_H
#define _ASM_SERIAL_H

#include <linux/config.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD (1843200 / 16)

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
#define RS_TABLE_SIZE	64
#else
#define RS_TABLE_SIZE
#endif

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

#ifdef CONFIG_MACH_JAZZ
#include <asm/jazz.h>

#ifndef CONFIG_OLIVETTI_M700
   /* Some Jazz machines seem to have an 8MHz crystal clock but I don't know
      exactly which ones ... XXX */
#define JAZZ_BASE_BAUD ( 8000000 / 16 ) /* ( 3072000 / 16) */
#else
/* but the M700 isn't such a strange beast */
#define JAZZ_BASE_BAUD BASE_BAUD
#endif

#define _JAZZ_SERIAL_INIT(int, base)					\
	{ .baud_base = JAZZ_BASE_BAUD, .irq = int, .flags = STD_COM_FLAGS,	\
	  .iomem_base = (u8 *) base, .iomem_reg_shift = 0,			\
	  .io_type = SERIAL_IO_MEM }
#define JAZZ_SERIAL_PORT_DEFNS						\
	_JAZZ_SERIAL_INIT(JAZZ_SERIAL1_IRQ, JAZZ_SERIAL1_BASE),		\
	_JAZZ_SERIAL_INIT(JAZZ_SERIAL2_IRQ, JAZZ_SERIAL2_BASE),
#else
#define JAZZ_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_MIPS_COBALT
#include <asm/cobalt/cobalt.h>
#define COBALT_BASE_BAUD  (18432000 / 16)
#define COBALT_SERIAL_PORT_DEFNS		\
	/* UART CLK   PORT  IRQ  FLAGS    */ 		\
	{ 0, COBALT_BASE_BAUD, 0xc800000, COBALT_SERIAL_IRQ, STD_COM_FLAGS },   /* ttyS0 */
#else
#define COBALT_SERIAL_PORT_DEFNS
#endif

/*
 * Both Galileo boards have the same UART mappings.
 */
#if defined (CONFIG_MIPS_EV96100) || defined (CONFIG_MIPS_EV64120)
#include <asm/galileo-boards/ev96100.h>
#include <asm/galileo-boards/ev96100int.h>
#define EV96100_SERIAL_PORT_DEFNS                                  \
    { .baud_base = EV96100_BASE_BAUD, .irq = EV96100INT_UART_0, \
      .flags = STD_COM_FLAGS,  \
      .iomem_base = EV96100_UART0_REGS_BASE, .iomem_reg_shift = 2, \
      .io_type = SERIAL_IO_MEM }, \
    { .baud_base = EV96100_BASE_BAUD, .irq = EV96100INT_UART_0, \
      .flags = STD_COM_FLAGS, \
      .iomem_base = EV96100_UART1_REGS_BASE, .iomem_reg_shift = 2, \
      .io_type = SERIAL_IO_MEM },
#else
#define EV96100_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_MIPS_ITE8172
#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_int.h>
#include <asm/it8712.h>
#define ITE_SERIAL_PORT_DEFNS                                  \
    { .baud_base = BASE_BAUD, .port = (IT8172_PCI_IO_BASE + IT_UART_BASE), \
      .irq = IT8172_UART_IRQ, .flags = STD_COM_FLAGS, .type = 0x3 }, \
    { .baud_base = (24000000/(16*13)), .port = (IT8172_PCI_IO_BASE + IT8712_UART1_PORT), \
      .irq = IT8172_SERIRQ_4, .flags = STD_COM_FLAGS, .type = 0x3 }, \
    /* Smart Card Reader 0 */ \
    { .baud_base = BASE_BAUD, .port = (IT8172_PCI_IO_BASE + IT_SCR0_BASE), \
      .irq = IT8172_SCR0_IRQ, .flags = STD_COM_FLAGS, .type = 0x3 }, \
    /* Smart Card Reader 1 */ \
    { .baud_base = BASE_BAUD, .port = (IT8172_PCI_IO_BASE + IT_SCR1_BASE), \
      .irq = IT8172_SCR1_IRQ, .flags = STD_COM_FLAGS, .type = 0x3 },
#else
#define ITE_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_MIPS_IVR
#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_int.h>
#define IVR_SERIAL_PORT_DEFNS                                  \
    { .baud_base = BASE_BAUD, .port = (IT8172_PCI_IO_BASE + IT_UART_BASE), \
      .irq = IT8172_UART_IRQ, .flags = STD_COM_FLAGS, .type = 0x3 },         \
    /* Smart Card Reader 1 */ \
    { .baud_base = BASE_BAUD, .port = (IT8172_PCI_IO_BASE + IT_SCR1_BASE), \
      .irq = IT8172_SCR1_IRQ, .flags = STD_COM_FLAGS, .type = 0x3 },
#else
#define IVR_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_TOSHIBA_JMR3927
#include <asm/jmr3927/jmr3927.h>
#define TXX927_SERIAL_PORT_DEFNS                              \
    { .baud_base = JMR3927_BASE_BAUD, .port = UART0_ADDR, .irq = UART0_INT,  \
      .flags = UART0_FLAGS, .type = 1 },                        \
    { .baud_base = JMR3927_BASE_BAUD, .port = UART1_ADDR, .irq = UART1_INT,  \
      .flags = UART1_FLAGS, .type = 1 },
#else
#define TXX927_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_SERIAL_AU1X00
#include <asm/mach-au1x00/au1000.h>
#ifdef CONFIG_SOC_AU1000
#define AU1000_SERIAL_PORT_DEFNS                       \
    { .baud_base = 0, .port = UART0_ADDR,              \
      .iomem_base = (unsigned char *)UART0_ADDR,       \
      .irq = AU1000_UART0_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART1_ADDR,              \
      .iomem_base = (unsigned char *)UART1_ADDR,       \
      .irq = AU1000_UART1_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART2_ADDR,              \
      .iomem_base = (unsigned char *)UART2_ADDR,       \
      .irq = AU1000_UART2_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART3_ADDR,              \
      .iomem_base = (unsigned char *)UART3_ADDR,       \
      .irq = AU1000_UART3_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },
#endif

#ifdef CONFIG_SOC_AU1500
#define AU1000_SERIAL_PORT_DEFNS                       \
    { .baud_base = 0, .port = UART0_ADDR,              \
      .iomem_base = (unsigned char *)UART0_ADDR,       \
      .irq = AU1500_UART0_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART3_ADDR,              \
      .iomem_base = (unsigned char *)UART3_ADDR,       \
      .irq = AU1500_UART3_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },
#endif

#ifdef CONFIG_SOC_AU1100
#define AU1000_SERIAL_PORT_DEFNS                       \
    { .baud_base = 0, .port = UART0_ADDR,              \
      .iomem_base = (unsigned char *)UART0_ADDR,       \
      .irq = AU1100_UART0_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART1_ADDR,              \
      .iomem_base = (unsigned char *)UART1_ADDR,       \
      .irq = AU1100_UART1_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART3_ADDR,              \
      .iomem_base = (unsigned char *)UART3_ADDR,       \
      .irq = AU1100_UART3_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },
#endif

#ifdef CONFIG_SOC_AU1550
#define AU1000_SERIAL_PORT_DEFNS                       \
    { .baud_base = 0, .port = UART0_ADDR,              \
      .iomem_base = (unsigned char *)UART0_ADDR,       \
      .irq = AU1550_UART0_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART1_ADDR,              \
      .iomem_base = (unsigned char *)UART1_ADDR,       \
      .irq = AU1550_UART1_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART3_ADDR,              \
      .iomem_base = (unsigned char *)UART3_ADDR,       \
      .irq = AU1550_UART3_INT,  .flags = STD_COM_FLAGS,\
      .iomem_reg_shift = 2 },
#endif

#ifdef CONFIG_SOC_AU1200
#define AU1000_SERIAL_PORT_DEFNS                       \
    { .baud_base = 0, .port = UART0_ADDR,              \
      .iomem_base = (unsigned char *)UART0_ADDR,       \
      .irq = AU1200_UART0_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },                          \
    { .baud_base = 0, .port = UART1_ADDR,              \
      .iomem_base = (unsigned char *)UART1_ADDR,       \
      .irq = AU1200_UART1_INT, .flags = STD_COM_FLAGS, \
      .iomem_reg_shift = 2 },
#endif

#else
#define AU1000_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_HAVE_STD_PC_SERIAL_PORT
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
	{ 0, BASE_BAUD, 0x000, 0, 0 },			/* ttyS14 (spare) */ \
	{ 0, BASE_BAUD, 0x000, 0, 0 },			/* ttyS15 (spare) */ \
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
#else /* CONFIG_SERIAL_MANY_PORTS */
#define EXTRA_SERIAL_PORT_DEFNS
#endif /* CONFIG_SERIAL_MANY_PORTS */

#else /* CONFIG_HAVE_STD_PC_SERIAL_PORTS */
#define STD_SERIAL_PORT_DEFNS
#define EXTRA_SERIAL_PORT_DEFNS
#endif /* CONFIG_HAVE_STD_PC_SERIAL_PORTS */

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

#ifdef CONFIG_MOMENCO_JAGUAR_ATX
/* Ordinary NS16552 duart with a 20MHz crystal.  */
#define JAGUAR_ATX_UART_CLK	20000000
#define JAGUAR_ATX_BASE_BAUD	(JAGUAR_ATX_UART_CLK / 16)

#define JAGUAR_ATX_SERIAL1_IRQ	6
#define JAGUAR_ATX_SERIAL1_BASE	0xfd000023L

#define _JAGUAR_ATX_SERIAL_INIT(int, base)				\
	{ baud_base: JAGUAR_ATX_BASE_BAUD, irq: int,			\
	  flags: (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),		\
	  iomem_base: (u8 *) base, iomem_reg_shift: 2,			\
	  io_type: SERIAL_IO_MEM }
#define MOMENCO_JAGUAR_ATX_SERIAL_PORT_DEFNS				\
	_JAGUAR_ATX_SERIAL_INIT(JAGUAR_ATX_SERIAL1_IRQ, JAGUAR_ATX_SERIAL1_BASE)
#else
#define MOMENCO_JAGUAR_ATX_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_MOMENCO_OCELOT_3
#define OCELOT_3_BASE_BAUD	( 20000000 / 16 )
#define OCELOT_3_SERIAL_IRQ	6
#define OCELOT_3_SERIAL_BASE	(signed)0xfd000020

#define _OCELOT_3_SERIAL_INIT(int, base)				\
	{ baud_base: OCELOT_3_BASE_BAUD, irq: int, 			\
	  flags: STD_COM_FLAGS,						\
	  iomem_base: (u8 *) base, iomem_reg_shift: 2,			\
	  io_type: SERIAL_IO_MEM }

#define MOMENCO_OCELOT_3_SERIAL_PORT_DEFNS				\
	_OCELOT_3_SERIAL_INIT(OCELOT_3_SERIAL_IRQ, OCELOT_3_SERIAL_BASE)
#else
#define MOMENCO_OCELOT_3_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_MOMENCO_OCELOT
/* Ordinary NS16552 duart with a 20MHz crystal.  */
#define OCELOT_BASE_BAUD ( 20000000 / 16 )

#define OCELOT_SERIAL1_IRQ	4
#define OCELOT_SERIAL1_BASE	0xe0001020

#define _OCELOT_SERIAL_INIT(int, base)					\
	{ .baud_base = OCELOT_BASE_BAUD, .irq = int, .flags = STD_COM_FLAGS,	\
	  .iomem_base = (u8 *) base, .iomem_reg_shift = 2,			\
	  .io_type = SERIAL_IO_MEM }
#define MOMENCO_OCELOT_SERIAL_PORT_DEFNS				\
	_OCELOT_SERIAL_INIT(OCELOT_SERIAL1_IRQ, OCELOT_SERIAL1_BASE)
#else
#define MOMENCO_OCELOT_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_MOMENCO_OCELOT_G
/* Ordinary NS16552 duart with a 20MHz crystal.  */
#define OCELOT_G_BASE_BAUD ( 20000000 / 16 )

#define OCELOT_G_SERIAL1_IRQ	4
#if 0
#define OCELOT_G_SERIAL1_BASE	0xe0001020
#else
#define OCELOT_G_SERIAL1_BASE	0xfd000020
#endif

#define _OCELOT_G_SERIAL_INIT(int, base)				\
	{ .baud_base = OCELOT_G_BASE_BAUD, .irq = int, .flags = STD_COM_FLAGS,\
	  .iomem_base = (u8 *) base, .iomem_reg_shift = 2,			\
	  .io_type = SERIAL_IO_MEM }
#define MOMENCO_OCELOT_G_SERIAL_PORT_DEFNS				\
	_OCELOT_G_SERIAL_INIT(OCELOT_G_SERIAL1_IRQ, OCELOT_G_SERIAL1_BASE)
#else
#define MOMENCO_OCELOT_G_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_MOMENCO_OCELOT_C
/* Ordinary NS16552 duart with a 20MHz crystal.  */
#define OCELOT_C_BASE_BAUD ( 20000000 / 16 )

#define OCELOT_C_SERIAL1_IRQ	80
#define OCELOT_C_SERIAL1_BASE	0xfd000020

#define OCELOT_C_SERIAL2_IRQ	81
#define OCELOT_C_SERIAL2_BASE	0xfd000000

#define _OCELOT_C_SERIAL_INIT(int, base)				\
	{ .baud_base		= OCELOT_C_BASE_BAUD,			\
	  .irq			= (int),				\
	  .flags		= STD_COM_FLAGS,			\
	  .iomem_base		= (u8 *) base,				\
	  .iomem_reg_shift	= 2,					\
	  .io_type		= SERIAL_IO_MEM				\
	 }
#define MOMENCO_OCELOT_C_SERIAL_PORT_DEFNS				\
	_OCELOT_C_SERIAL_INIT(OCELOT_C_SERIAL1_IRQ, OCELOT_C_SERIAL1_BASE), \
	_OCELOT_C_SERIAL_INIT(OCELOT_C_SERIAL2_IRQ, OCELOT_C_SERIAL2_BASE)
#else
#define MOMENCO_OCELOT_C_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_DDB5477
#include <asm/ddb5xxx/ddb5477.h>
#define DDB5477_SERIAL_PORT_DEFNS                                       \
        { .baud_base = BASE_BAUD, .irq = VRC5477_IRQ_UART0, 		\
	  .flags = STD_COM_FLAGS, .iomem_base = (u8*)0xbfa04200, 	\
	  .iomem_reg_shift = 3, .io_type = SERIAL_IO_MEM},		\
        { .baud_base = BASE_BAUD, .irq = VRC5477_IRQ_UART1, 		\
	  .flags = STD_COM_FLAGS, .iomem_base = (u8*)0xbfa04240, 	\
	  .iomem_reg_shift = 3, .io_type = SERIAL_IO_MEM},
#else
#define DDB5477_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_SGI_IP32
/*
 * The IP32 (SGI O2) has standard serial ports (UART 16550A) mapped in memory
 * They are initialized in ip32_setup
 */
#define IP32_SERIAL_PORT_DEFNS				\
        {},{},
#else
#define IP32_SERIAL_PORT_DEFNS
#endif /* CONFIG_SGI_IP32 */

#define SERIAL_PORT_DFNS				\
	COBALT_SERIAL_PORT_DEFNS			\
	DDB5477_SERIAL_PORT_DEFNS			\
	EV96100_SERIAL_PORT_DEFNS			\
	EXTRA_SERIAL_PORT_DEFNS				\
	HUB6_SERIAL_PORT_DFNS				\
	IP32_SERIAL_PORT_DEFNS                          \
	ITE_SERIAL_PORT_DEFNS           		\
	IVR_SERIAL_PORT_DEFNS           		\
	JAZZ_SERIAL_PORT_DEFNS				\
	STD_SERIAL_PORT_DEFNS				\
	MOMENCO_OCELOT_G_SERIAL_PORT_DEFNS		\
	MOMENCO_OCELOT_C_SERIAL_PORT_DEFNS		\
	MOMENCO_OCELOT_SERIAL_PORT_DEFNS		\
	MOMENCO_OCELOT_3_SERIAL_PORT_DEFNS		\
	TXX927_SERIAL_PORT_DEFNS                        \
	AU1000_SERIAL_PORT_DEFNS

#endif /* _ASM_SERIAL_H */
