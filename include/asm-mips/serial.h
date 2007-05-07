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

/*
 * Galileo EV64120 evaluation board
 */
#ifdef CONFIG_MIPS_EV64120
#include <mach-gt64120.h>
#define EV64120_SERIAL_PORT_DEFNS                                  \
    { .baud_base = EV64120_BASE_BAUD, .irq = EV64120_UART_IRQ, \
      .flags = STD_COM_FLAGS,  \
      .iomem_base = EV64120_UART0_REGS_BASE, .iomem_reg_shift = 2, \
      .io_type = SERIAL_IO_MEM }, \
    { .baud_base = EV64120_BASE_BAUD, .irq = EV64120_UART_IRQ, \
      .flags = STD_COM_FLAGS, \
      .iomem_base = EV64120_UART1_REGS_BASE, .iomem_reg_shift = 2, \
      .io_type = SERIAL_IO_MEM },
#else
#define EV64120_SERIAL_PORT_DEFNS
#endif

#ifdef CONFIG_HAVE_STD_PC_SERIAL_PORT
#define STD_SERIAL_PORT_DEFNS			\
	/* UART CLK   PORT IRQ     FLAGS        */			\
	{ 0, BASE_BAUD, 0x3F8, 4, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, 3, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0x3E8, 4, STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0x2E8, 3, STD_COM4_FLAGS },	/* ttyS3 */

#else /* CONFIG_HAVE_STD_PC_SERIAL_PORTS */
#define STD_SERIAL_PORT_DEFNS
#endif /* CONFIG_HAVE_STD_PC_SERIAL_PORTS */

#ifdef CONFIG_MOMENCO_OCELOT_3
#define OCELOT_3_BASE_BAUD	( 20000000 / 16 )
#define OCELOT_3_SERIAL_IRQ	6
#define OCELOT_3_SERIAL_BASE	(signed)0xfd000020

#define _OCELOT_3_SERIAL_INIT(int, base)				\
	{ .baud_base = OCELOT_3_BASE_BAUD, irq: int, 			\
	  .flags = STD_COM_FLAGS,						\
	  .iomem_base = (u8 *) base, iomem_reg_shift: 2,			\
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
	DDB5477_SERIAL_PORT_DEFNS			\
	EV64120_SERIAL_PORT_DEFNS			\
	IP32_SERIAL_PORT_DEFNS                          \
	JAZZ_SERIAL_PORT_DEFNS				\
	STD_SERIAL_PORT_DEFNS				\
	MOMENCO_OCELOT_C_SERIAL_PORT_DEFNS		\
	MOMENCO_OCELOT_SERIAL_PORT_DEFNS		\
	MOMENCO_OCELOT_3_SERIAL_PORT_DEFNS

#endif /* _ASM_SERIAL_H */
