/* SPDX-License-Identifier: GPL-2.0 */
/* sunserialcore.h
 *
 * Generic SUN serial/kbd/ms layer.  Based entirely
 * upon drivers/sbus/char/sunserial.h which is:
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 *
 * Port to new UART layer is:
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 */

#ifndef _SERIAL_SUN_H
#define _SERIAL_SUN_H

#include <linux/device.h>
#include <linux/serial_core.h>
#include <linux/console.h>

/* Serial keyboard defines for L1-A processing... */
#define SUNKBD_RESET		0xff
#define SUNKBD_L1		0x01
#define SUNKBD_UP		0x80
#define SUNKBD_A		0x4d

extern unsigned int suncore_mouse_baud_cflag_next(unsigned int, int *);
extern int suncore_mouse_baud_detection(unsigned char, int);

extern int sunserial_register_minors(struct uart_driver *, int);
extern void sunserial_unregister_minors(struct uart_driver *, int);

extern int sunserial_console_match(struct console *, struct device_node *,
				   struct uart_driver *, int, bool);
extern void sunserial_console_termios(struct console *,
				      struct device_node *);

#endif /* !(_SERIAL_SUN_H) */
