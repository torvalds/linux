#ifndef _BLACKFIN_DELAY_H
#define _BLACKFIN_DELAY_H

static inline void __delay(unsigned long loops)
{

/* FIXME: Currently the assembler doesn't recognize Loop Register Clobbers,
   uncomment this as soon those are implemented */
/*
      __asm__ __volatile__ (  "\t LSETUP (1f,1f) LC0= %0\n\t"
                              "1:\t NOP;\n\t"
                              : :"a" (loops)
                              : "LT0","LB0","LC0");

*/

	__asm__ __volatile__("[--SP] = LC0;\n\t"
			     "[--SP] = LT0;\n\t"
			     "[--SP] = LB0;\n\t"
			     "LSETUP (1f,1f) LC0 = %0;\n\t"
			     "1:\t NOP;\n\t"
			     "LB0 = [SP++];\n\t"
				"LT0 = [SP++];\n\t"
				"LC0 = [SP++];\n"
				:
				:"a" (loops));
}

#include <linux/param.h>	/* needed for HZ */

/*
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */
static inline void udelay(unsigned long usecs)
{
	extern unsigned long loops_per_jiffy;
	__delay(usecs * loops_per_jiffy / (1000000 / HZ));
}

#endif				/* defined(_BLACKFIN_DELAY_H) */
