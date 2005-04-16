#ifndef _ASM_SH_IO_EC3104_H
#define _ASM_SH_IO_EC3104_H

#include <linux/types.h>

extern unsigned char ec3104_inb(unsigned long port);
extern unsigned short ec3104_inw(unsigned long port);
extern unsigned long ec3104_inl(unsigned long port);

extern void ec3104_outb(unsigned char value, unsigned long port);
extern void ec3104_outw(unsigned short value, unsigned long port);
extern void ec3104_outl(unsigned long value, unsigned long port);

extern int ec3104_irq_demux(int irq);

#endif /* _ASM_SH_IO_EC3104_H */
