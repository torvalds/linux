// SPDX-License-Identifier: GPL-2.0
#include <sys/select.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "pager.h"
#include "run-command.h"
#include "sigchain.h"
#include "subcmd-config.h"

/*
 * This is split up from the rest of git so that we can do
 * something different on Windows.
 */

static int spawned_pager;
static int pager_columns;

void pager_init(const char *pager_env)
{
	subcmd_config.pager_env = pager_env;
}

static void pager_preexec(void)
{
	/*
	 * Work around bug in "less" by not starting it until we
	 * have real input
	 */
	fd_set in;

	FD_ZERO(&in);
	FD_SET(0, &in);
	select(1, &in, NULL, &in, NULL);

	setenv("LESS", "FRSX", 0);
}

static const char *pager_argv[] = { "sh", "-c", NULL, NULL };
static struct child_process pager_process;

static void wait_for_pager(void)
{
	fflush(stdout);
	fflush(stderr);
	/* signal EOF to pager */
	close(1);
	close(2);
	finish_command(&pager_process);
}

static void wait_for_pager_signal(int signo)
{
	wait_for_pager();
	sigchain_pop(signo);
	raise(signo);
}

void setup_pager(void)
{
	const char *pager = getenv(subcmd_config.pager_env);
	struct winsize sz;

	if (!isatty(1))
		return;
	if (ioctl(1, TIOCGWINSZ, &sz) == 0)
		pager_columns = sz.ws_col;
	if (!pager)
		pager = getenv("PAGER");
	if (!(pager || access("/usr/bin/pager", X_OK)))
		pager = "/usr/bin/pager";
	if (!(pager || access("/usr/bin/less", X_OK)))
		pager = "/usr/bin/less";
	if (!pager)
		pager = "cat";
	if (!*pager || !strcmp(pager, "cat"))
		return;

	spawned_pager = 1; /* means we are emitting to terminal */

	/* spawn the pager */
	pager_argv[2] = pager;
	pager_process.argv = pager_argv;
	pager_process.in = -1;
	pager_process.preexec_cb = pager_preexec;

	if (start_command(&pager_process))
		return;

	/* original process continues, but writes to the pipe */
	dup2(pager_process.in, 1);
	if (isatty(2))
		dup2(pager_process.in, 2);
	close(pager_process.in);

	/* this makes sure that the parent terminates after the pager */
	sigchain_push_common(wait_for_pager_signal);
	atexit(wait_for_pager);
}

int pager_in_use(void)
{
	return spawned_pager;
}

int pager_get_columns(void)
{
	char *s;

	s = getenv("COLUMNS");
	if (s)
		return atoi(s);

	return (pager_columns ? pager_columns : 80) - 2;
}
