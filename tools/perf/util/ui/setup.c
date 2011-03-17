#include <newt.h>
#include <signal.h>
#include <stdbool.h>

#include "../cache.h"
#include "../debug.h"
#include "browser.h"
#include "helpline.h"
#include "ui.h"

pthread_mutex_t ui__lock = PTHREAD_MUTEX_INITIALIZER;

static void newt_suspend(void *d __used)
{
	newtSuspend();
	raise(SIGTSTP);
	newtResume();
}

void setup_browser(bool fallback_to_pager)
{
	if (!isatty(1) || !use_browser || dump_trace) {
		use_browser = 0;
		if (fallback_to_pager)
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
