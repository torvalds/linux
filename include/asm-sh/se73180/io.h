/*
 * include/asm-sh/se73180/io.h
 *
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 * Based on include/asm-sh/se7300/io.h
 *
 * IO functions for SH-Mobile3(SH73180) SolutionEngine
 *
 */

#ifndef _ASM_SH_IO_73180SE_H
#define _ASM_SH_IO_73180SE_H

extern unsigned char sh73180se_inb(unsigned long port);
extern unsigned short sh73180se_inw(unsigned long port);
extern unsigned int sh73180se_inl(unsigned long port);

extern void sh73180se_outb(unsigned char value, unsigned long port);
extern void sh73180se_outw(unsigned short value, unsigned long port);
extern void sh73180se_outl(unsigned int value, unsigned long port);

extern unsigned char sh73180se_inb_p(unsigned long port);
extern void sh73180se_outb_p(unsigned char value, unsigned long port);

extern void sh73180se_insb(unsigned long port, void *addr, unsigned long count);
extern void sh73180se_insw(unsigned long port, void *addr, unsigned long count);
extern void sh73180se_insl(unsigned long port, void *addr, unsigned long count);
extern void sh73180se_outsb(unsigned long port, const void *addr, unsigned long count);
extern void sh73180se_outsw(unsigned long port, const void *addr, unsigned long count);
extern void sh73180se_outsl(unsigned long port, const void *addr, unsigned long count);

#endif /* _ASM_SH_IO_73180SE_H */
