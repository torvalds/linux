#ifndef __UDBG_HDR
#define __UDBG_HDR

#include <linux/compiler.h>
#include <linux/init.h>

/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

extern void (*udbg_putc)(unsigned char c);
extern unsigned char (*udbg_getc)(void);
extern int (*udbg_getc_poll)(void);

extern void udbg_puts(const char *s);
extern int udbg_write(const char *s, int n);
extern int udbg_read(char *buf, int buflen);

extern void register_early_udbg_console(void);
extern void udbg_printf(const char *fmt, ...);
extern void udbg_ppcdbg(unsigned long flags, const char *fmt, ...);
extern unsigned long udbg_ifdebug(unsigned long flags);
extern void __init ppcdbg_initialize(void);

extern void udbg_init_uart(void __iomem *comport, unsigned int speed);
#endif
