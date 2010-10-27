#include <newt.h>
#include <signal.h>
#include <stdbool.h>

#include "../cache.h"
#include "../debug.h"
#include "browser.h"
#include "helpline.h"

static void newt_suspend(void *d __used)
{
	newtSuspend();
	raise(SIGTSTP);
	newtResume();
}

void setup_browser(void)
{
	if (!isatty(1) || !use_browser || dump_trace) {
		use_browser = 0;
		setup_pager();
		return;
	}

	use_browser = 1;
	newtInit();
	newtCls();
	newtSetSuspendCallback(newt_suspend, NULL);
	ui_helpline__init();
	ui_browser__init();
}

void exit_browser(bool wait_for_ok)
{
	if (use_browser > 0) {
		if (wait_for_ok) {
			char title[] = "Fatal Error", ok[] = "Ok";
			newtWinMessage(title, ok, ui_helpline__last_msg);
		}
		newtFinished();
	}
}
