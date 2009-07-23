#ifndef __ASM_GENERIC_DELAY_H
#define __ASM_GENERIC_DELAY_H

extern void __udelay(unsigned long usecs);
extern void __delay(unsigned long loops);

#define udelay(n) __udelay(n)

#endif /* __ASM_GENERIC_DELAY_H */
