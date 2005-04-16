/*
 * include/asm-sh/se7300/io.h
 *
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 * IO functions for SH-Mobile(SH7300) SolutionEngine
 */

#ifndef _ASM_SH_IO_7300SE_H
#define _ASM_SH_IO_7300SE_H

extern unsigned char sh7300se_inb(unsigned long port);
extern unsigned short sh7300se_inw(unsigned long port);
extern unsigned int sh7300se_inl(unsigned long port);

extern void sh7300se_outb(unsigned char value, unsigned long port);
extern void sh7300se_outw(unsigned short value, unsigned long port);
extern void sh7300se_outl(unsigned int value, unsigned long port);

extern unsigned char sh7300se_inb_p(unsigned long port);
extern void sh7300se_outb_p(unsigned char value, unsigned long port);

extern void sh7300se_insb(unsigned long port, void *addr, unsigned long count);
extern void sh7300se_insw(unsigned long port, void *addr, unsigned long count);
extern void sh7300se_insl(unsigned long port, void *addr, unsigned long count);
extern void sh7300se_outsb(unsigned long port, const void *addr, unsigned long count);
extern void sh7300se_outsw(unsigned long port, const void *addr, unsigned long count);
extern void sh7300se_outsl(unsigned long port, const void *addr, unsigned long count);

#endif /* _ASM_SH_IO_7300SE_H */
