#include <asm/ec3104.h>
/* Naturally we don't know the exact value but 115200 baud has a divisor
 * of 9 and 19200 baud has a divisor of 52, so this seems like a good
 * guess.  */
#define BASE_BAUD (16800000 / 16)

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

/* there is a fourth serial port with the expected values as well, but
 * it's got the keyboard controller behind it so we can't really use it
 * (without moving the keyboard driver to userspace, which doesn't sound
 * like a very good idea) */
#define STD_SERIAL_PORT_DEFNS			\
	/* UART CLK   PORT IRQ     FLAGS        */			\
	{ 0, BASE_BAUD, 0x11C00, EC3104_IRQBASE+7, STD_COM_FLAGS }, /* ttyS0 */	\
	{ 0, BASE_BAUD, 0x12000, EC3104_IRQBASE+8, STD_COM_FLAGS }, /* ttyS1 */	\
	{ 0, BASE_BAUD, 0x12400, EC3104_IRQBASE+9, STD_COM_FLAGS }, /* ttyS2 */

#define SERIAL_PORT_DFNS STD_SERIAL_PORT_DEFNS

/* XXX: This should be moved ino irq.h */
#define irq_cannonicalize(x) (x)
