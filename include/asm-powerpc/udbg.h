/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_UDBG_H
#define _ASM_POWERPC_UDBG_H

#include <linux/compiler.h>
#include <linux/init.h>

extern void (*udbg_putc)(unsigned char c);
extern unsigned char (*udbg_getc)(void);
extern int (*udbg_getc_poll)(void);

extern void udbg_puts(const char *s);
extern int udbg_write(const char *s, int n);
extern int udbg_read(char *buf, int buflen);

extern void register_early_udbg_console(void);
extern void udbg_printf(const char *fmt, ...);

extern void udbg_init_uart(void __iomem *comport, unsigned int speed);

struct device_node;
extern void udbg_init_scc(struct device_node *np);
#endif /* _ASM_POWERPC_UDBG_H */
