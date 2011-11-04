#include <newt.h>
#include <signal.h>
#include <stdbool.h>

#include "../cache.h"
#include "../debug.h"
#include "browser.h"
#include "helpline.h"
#include "ui.h"
#include "libslang.h"

pthread_mutex_t ui__lock = PTHREAD_MUTEX_INITIALIZER;

static void newt_suspend(void *d __used)
{
	newtSuspend();
	raise(SIGTSTP);
	newtResume();
}

static int ui__init(void)
{
	int err = SLkp_init();

	if (err < 0)
		goto out;

	SLkp_define_keysym((char *)"^(kB)", SL_KEY_UNTAB);
out:
	return err;
}

static void ui__exit(void)
{
	SLtt_set_cursor_visibility(1);
	SLsmg_refresh();
	SLsmg_reset_smg();
	SLang_reset_tty();
}

static void ui__signal(int sig)
{
	ui__exit();
	psignal(sig, "perf");
	exit(0);
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
	ui__init();
	newtSetSuspendCallback(newt_suspend, NULL);
	ui_helpline__init();
	ui_browser__init();

	signal(SIGSEGV, ui__signal);
	signal(SIGFPE, ui__signal);
	signal(SIGINT, ui__signal);
	signal(SIGQUIT, ui__signal);
	signal(SIGTERM, ui__signal);
}

void exit_browser(bool wait_for_ok)
{
	if (use_browser > 0) {
		if (wait_for_ok) {
			char title[] = "Fatal Error", ok[] = "Ok";
			newtWinMessage(title, ok, ui_helpline__last_msg);
		}
		ui__exit();
	}
}
