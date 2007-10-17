/*
 * lib/bust_spinlocks.c
 *
 * Provides a minimal bust_spinlocks for architectures which don't have one of their own.
 *
 * bust_spinlocks() clears any spinlocks which would prevent oops, die(), BUG()
 * and panic() information from reaching the user.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/tty.h>
#include <linux/wait.h>
#include <linux/vt_kern.h>


void __attribute__((weak)) bust_spinlocks(int yes)
{
	if (yes) {
		++oops_in_progress;
	} else {
#ifdef CONFIG_VT
		unblank_screen();
#endif
		if (--oops_in_progress == 0)
			wake_up_klogd();
	}
}


