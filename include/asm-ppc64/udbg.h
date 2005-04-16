#ifndef __UDBG_HDR
#define __UDBG_HDR

#include <linux/compiler.h>

/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

void udbg_init_uart(void __iomem *comport, unsigned int speed);
void udbg_putc(unsigned char c);
unsigned char udbg_getc(void);
int udbg_getc_poll(void);
void udbg_puts(const char *s);
int udbg_write(const char *s, int n);
int udbg_read(char *buf, int buflen);
struct console;
void udbg_console_write(struct console *con, const char *s, unsigned int n);
void udbg_printf(const char *fmt, ...);
void udbg_ppcdbg(unsigned long flags, const char *fmt, ...);
unsigned long udbg_ifdebug(unsigned long flags);

#endif
