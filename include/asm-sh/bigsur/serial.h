/*
 * include/asm-sh/bigsur/serial.h
 *
 * Configuration details for Big Sur 16550 based serial ports 
 * i.e. HD64465, PCMCIA, etc.
 */

#ifndef _ASM_SERIAL_BIGSUR_H
#define _ASM_SERIAL_BIGSUR_H
#include <asm/hd64465.h>

#define BASE_BAUD (3379200 / 16)

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)


#define SERIAL_PORT_DFNS                   \
        /* UART CLK   PORT IRQ     FLAGS        */                      \
        { 0, BASE_BAUD, 0x3F8, HD64465_IRQ_UART, STD_COM_FLAGS } /* ttyS0 */ 

/* XXX: This should be moved ino irq.h */
#define irq_cannonicalize(x) (x)

#endif /* _ASM_SERIAL_BIGSUR_H */
