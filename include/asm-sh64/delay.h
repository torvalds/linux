#ifndef __ASM_SH64_DELAY_H
#define __ASM_SH64_DELAY_H

extern void __delay(int loops);
extern void __udelay(unsigned long long usecs, unsigned long lpj);
extern void __ndelay(unsigned long long nsecs, unsigned long lpj);
extern void udelay(unsigned long usecs);
extern void ndelay(unsigned long nsecs);

#endif /* __ASM_SH64_DELAY_H */

