/*
 * drivers/power/process.c - Functions for saving/restoring console.
 *
 * Originally from swsusp.
 */

#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>
#include "power.h"

static int new_loglevel = 10;
static int orig_loglevel;
#ifdef SUSPEND_CONSOLE
static int orig_fgconsole, orig_kmsg;
#endif

int pm_prepare_console(void)
{
	orig_loglevel = console_loglevel;
	console_loglevel = new_loglevel;

#ifdef SUSPEND_CONSOLE
	acquire_console_sem();

	orig_fgconsole = fg_console;

	if (vc_allocate(SUSPEND_CONSOLE)) {
	  /* we can't have a free VC for now. Too bad,
	   * we don't want to mess the screen for now. */
		release_console_sem();
		return 1;
	}

	set_console(SUSPEND_CONSOLE);
	release_console_sem();

	if (vt_waitactive(SUSPEND_CONSOLE)) {
		pr_debug("Suspend: Can't switch VCs.");
		return 1;
	}
	orig_kmsg = kmsg_redirect;
	kmsg_redirect = SUSPEND_CONSOLE;
#endif
	return 0;
}

void pm_restore_console(void)
{
	console_loglevel = orig_loglevel;
#ifdef SUSPEND_CONSOLE
	acquire_console_sem();
	set_console(orig_fgconsole);
	release_console_sem();
	kmsg_redirect = orig_kmsg;
#endif
	return;
}
