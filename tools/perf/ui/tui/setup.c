#include <signal.h>
#include <stdbool.h>

#include "../../util/cache.h"
#include "../../util/debug.h"
#include "../browser.h"
#include "../helpline.h"
#include "../ui.h"
#include "../util.h"
#include "../libslang.h"
#include "../keysyms.h"

static volatile int ui__need_resize;

extern struct perf_error_ops perf_tui_eops;

extern void hist_browser__init_hpp(void);

void ui__refresh_dimensions(bool force)
{
	if (force || ui__need_resize) {
		ui__need_resize = 0;
		pthread_mutex_lock(&ui__lock);
		SLtt_get_screen_size();
		SLsmg_reinit_smg();
		pthread_mutex_unlock(&ui__lock);
	}
}

static void ui__sigwinch(int sig __maybe_unused)
{
	ui__need_resize = 1;
}

static void ui__setup_sigwinch(void)
{
	static bool done;

	if (done)
		return;

	done = true;
	pthread__unblock_sigwinch();
	signal(SIGWINCH, ui__sigwinch);
}

int ui__getch(int delay_secs)
{
	struct timeval timeout, *ptimeout = delay_secs ? &timeout : NULL;
	fd_set read_set;
	int err, key;

	ui__setup_sigwinch();

	FD_ZERO(&read_set);
	FD_SET(0, &read_set);

	if (delay_secs) {
		timeout.tv_sec = delay_secs;
		timeout.tv_usec = 0;
	}

        err = select(1, &read_set, NULL, NULL, ptimeout);

	if (err == 0)
		return K_TIMER;

	if (err == -1) {
		if (errno == EINTR)
			return K_RESIZE;
		return K_ERROR;
	}

	key = SLang_getkey();
	if (key != K_ESC)
		return key;

	FD_ZERO(&read_set);
	FD_SET(0, &read_set);
	timeout.tv_sec = 0;
	timeout.tv_usec = 20;
        err = select(1, &read_set, NULL, NULL, &timeout);
	if (err == 0)
		return K_ESC;

	SLang_ungetkey(key);
	return SLkp_getkey();
}

static void ui__signal(int sig)
{
	ui__exit(false);
	psignal(sig, "perf");
	exit(0);
}

int ui__init(void)
{
	int err;

	SLutf8_enable(-1);
	SLtt_get_terminfo();
	SLtt_get_screen_size();

	err = SLsmg_init_smg();
	if (err < 0)
		goto out;
	err = SLang_init_tty(0, 0, 0);
	if (err < 0)
		goto out;

	err = SLkp_init();
	if (err < 0) {
		pr_err("TUI initialization failed.\n");
		goto out;
	}

	SLkp_define_keysym((char *)"^(kB)", SL_KEY_UNTAB);

	ui_helpline__init();
	ui_browser__init();
	ui_progress__init();

	signal(SIGSEGV, ui__signal);
	signal(SIGFPE, ui__signal);
	signal(SIGINT, ui__signal);
	signal(SIGQUIT, ui__signal);
	signal(SIGTERM, ui__signal);

	perf_error__register(&perf_tui_eops);

	hist_browser__init_hpp();
out:
	return err;
}

void ui__exit(bool wait_for_ok)
{
	if (wait_for_ok)
		ui__question_window("Fatal Error",
				    ui_helpline__last_msg,
				    "Press any key...", 0);

	SLtt_set_cursor_visibility(1);
	SLsmg_refresh();
	SLsmg_reset_smg();
	SLang_reset_tty();

	perf_error__unregister(&perf_tui_eops);
}
