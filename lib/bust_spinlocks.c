/*
 * lib/bust_spinlocks.c
 *
 * Provides a minimal bust_spinlocks for architectures which don't have one of their own.
 *
 * bust_spinlocks() clears any spinlocks which would prevent oops, die(), BUG()
 * and panic() information from reaching the user.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/tty.h>
#include <linux/wait.h>
#include <linux/vt_kern.h>


void bust_spinlocks(int yes)
{
	if (yes) {
		oops_in_progress = 1;
	} else {
		int loglevel_save = console_loglevel;
#ifdef CONFIG_VT
		unblank_screen();
#endif
		oops_in_progress = 0;
		/*
		 * OK, the message is on the console.  Now we call printk()
		 * without oops_in_progress set so that printk() will give klogd
		 * and the blanked console a poke.  Hold onto your hats...
		 */
		console_loglevel = 15;		/* NMI oopser may have shut the console up */
		printk(" ");
		console_loglevel = loglevel_save;
	}
}


