/*
 * include/asm-sh/saturn/io.h
 *
 * I/O functions for use on the Sega Saturn.
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#ifndef __ASM_SH_SATURN_IO_H
#define __ASM_SH_SATURN_IO_H

/* arch/sh/boards/saturn/io.c */
extern unsigned long saturn_isa_port2addr(unsigned long offset);
extern void *saturn_ioremap(unsigned long offset, unsigned long size);
extern void saturn_iounmap(void *addr);

#endif /* __ASM_SH_SATURN_IO_H */

