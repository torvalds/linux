// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <linux/kernel.h>
#ifdef HAVE_BACKTRACE_SUPPORT
#include <execinfo.h>
#endif

#include "../../util/cache.h"
#include "../../util/debug.h"
#include "../../util/util.h"
#include "../browser.h"
#include "../helpline.h"
#include "../ui.h"
#include "../util.h"
#include "../libslang.h"
#include "../keysyms.h"
#include "tui.h"

static volatile int ui__need_resize;

extern struct perf_error_ops perf_tui_eops;
extern bool tui_helpline__set;

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

#ifdef HAVE_BACKTRACE_SUPPORT
static void ui__signal_backtrace(int sig)
{
	void *stackdump[32];
	size_t size;

	ui__exit(false);
	psignal(sig, "perf");

	printf("-------- backtrace --------\n");
	size = backtrace(stackdump, ARRAY_SIZE(stackdump));
	backtrace_symbols_fd(stackdump, size, STDOUT_FILENO);

	exit(0);
}
#else
# define ui__signal_backtrace  ui__signal
#endif

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
	err = SLang_init_tty(-1, 0, 0);
	if (err < 0)
		goto out;

	err = SLkp_init();
	if (err < 0) {
		pr_err("TUI initialization failed.\n");
		goto out;
	}

	SLkp_define_keysym((char *)"^(kB)", SL_KEY_UNTAB);

	signal(SIGSEGV, ui__signal_backtrace);
	signal(SIGFPE, ui__signal_backtrace);
	signal(SIGINT, ui__signal);
	signal(SIGQUIT, ui__signal);
	signal(SIGTERM, ui__signal);

	perf_error__register(&perf_tui_eops);

	ui_helpline__init();
	ui_browser__init();
	tui_progress__init();

	hist_browser__init_hpp();
out:
	return err;
}

void ui__exit(bool wait_for_ok)
{
	if (wait_for_ok && tui_helpline__set)
		ui__question_window("Fatal Error",
				    ui_helpline__last_msg,
				    "Press any key...", 0);

	SLtt_set_cursor_visibility(1);
	SLsmg_refresh();
	SLsmg_reset_smg();
	SLang_reset_tty();

	perf_error__unregister(&perf_tui_eops);
}
