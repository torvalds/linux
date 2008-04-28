/*
 * drivers/power/process.c - Functions for saving/restoring console.
 *
 * Originally from swsusp.
 */

#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>
#include <linux/module.h>
#include "power.h"

#if defined(CONFIG_VT) && defined(CONFIG_VT_CONSOLE)
#define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1)

static int orig_fgconsole, orig_kmsg;
static int disable_vt_switch;

/*
 * Normally during a suspend, we allocate a new console and switch to it.
 * When we resume, we switch back to the original console.  This switch
 * can be slow, so on systems where the framebuffer can handle restoration
 * of video registers anyways, there's little point in doing the console
 * switch.  This function allows you to disable it by passing it '0'.
 */
void pm_set_vt_switch(int do_switch)
{
	acquire_console_sem();
	disable_vt_switch = !do_switch;
	release_console_sem();
}
EXPORT_SYMBOL(pm_set_vt_switch);

int pm_prepare_console(void)
{
	acquire_console_sem();

	if (disable_vt_switch) {
		release_console_sem();
		return 0;
	}

	orig_fgconsole = fg_console;

	if (vc_allocate(SUSPEND_CONSOLE)) {
	  /* we can't have a free VC for now. Too bad,
	   * we don't want to mess the screen for now. */
		release_console_sem();
		return 1;
	}

	if (set_console(SUSPEND_CONSOLE)) {
		/*
		 * We're unable to switch to the SUSPEND_CONSOLE.
		 * Let the calling function know so it can decide
		 * what to do.
		 */
		release_console_sem();
		return 1;
	}
	release_console_sem();

	if (vt_waitactive(SUSPEND_CONSOLE)) {
		pr_debug("Suspend: Can't switch VCs.");
		return 1;
	}
	orig_kmsg = kmsg_redirect;
	kmsg_redirect = SUSPEND_CONSOLE;
	return 0;
}

void pm_restore_console(void)
{
	acquire_console_sem();
	if (disable_vt_switch) {
		release_console_sem();
		return;
	}
	set_console(orig_fgconsole);
	release_console_sem();
	kmsg_redirect = orig_kmsg;
}
#endif
