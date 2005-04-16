/* calibrate.c: default delay calibration
 *
 * Excised from init/main.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/init.h>

static unsigned long preset_lpj;
static int __init lpj_setup(char *str)
{
	preset_lpj = simple_strtoul(str,NULL,0);
	return 1;
}

__setup("lpj=", lpj_setup);

/*
 * This is the number of bits of precision for the loops_per_jiffy.  Each
 * bit takes on average 1.5/HZ seconds.  This (like the original) is a little
 * better than 1%
 */
#define LPS_PREC 8

void __devinit calibrate_delay(void)
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;

	if (preset_lpj) {
		loops_per_jiffy = preset_lpj;
		printk("Calibrating delay loop (skipped)... "
			"%lu.%02lu BogoMIPS preset\n",
			loops_per_jiffy/(500000/HZ),
			(loops_per_jiffy/(5000/HZ)) % 100);
	} else {
		loops_per_jiffy = (1<<12);

		printk(KERN_DEBUG "Calibrating delay loop... ");
		while ((loops_per_jiffy <<= 1) != 0) {
			/* wait for "start of" clock tick */
			ticks = jiffies;
			while (ticks == jiffies)
				/* nothing */;
			/* Go .. */
			ticks = jiffies;
			__delay(loops_per_jiffy);
			ticks = jiffies - ticks;
			if (ticks)
				break;
		}

		/*
		 * Do a binary approximation to get loops_per_jiffy set to
		 * equal one clock (up to lps_precision bits)
		 */
		loops_per_jiffy >>= 1;
		loopbit = loops_per_jiffy;
		while (lps_precision-- && (loopbit >>= 1)) {
			loops_per_jiffy |= loopbit;
			ticks = jiffies;
			while (ticks == jiffies)
				/* nothing */;
			ticks = jiffies;
			__delay(loops_per_jiffy);
			if (jiffies != ticks)	/* longer than 1 tick */
				loops_per_jiffy &= ~loopbit;
		}

		/* Round the value and print it */
		printk("%lu.%02lu BogoMIPS (lpj=%lu)\n",
			loops_per_jiffy/(500000/HZ),
			(loops_per_jiffy/(5000/HZ)) % 100,
			loops_per_jiffy);
	}

}
