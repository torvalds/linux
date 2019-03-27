/*-
 * Copyright (c) 2016 John H. Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

static const char *signames[] = {
	[SIGHUP] = "SIGHUP",
	[SIGINT] = "SIGINT",
	[SIGQUIT] = "SIGQUIT",
	[SIGILL] = "SIGILL",
	[SIGTRAP] = "SIGTRAP",
	[SIGABRT] = "SIGABRT",
	[SIGEMT] = "SIGEMT",
	[SIGFPE] = "SIGFPE",
	[SIGKILL] = "SIGKILL",
	[SIGBUS] = "SIGBUS",
	[SIGSEGV] = "SIGSEGV",
	[SIGSYS] = "SIGSYS",
	[SIGPIPE] = "SIGPIPE",
	[SIGALRM] = "SIGALRM",
	[SIGTERM] = "SIGTERM",
	[SIGURG] = "SIGURG",
	[SIGSTOP] = "SIGSTOP",
	[SIGTSTP] = "SIGTSTP",
	[SIGCONT] = "SIGCONT",
	[SIGCHLD] = "SIGCHLD",
	[SIGTTIN] = "SIGTTIN",
	[SIGTTOU] = "SIGTTOU",
	[SIGIO] = "SIGIO",
	[SIGXCPU] = "SIGXCPU",
	[SIGXFSZ] = "SIGXFSZ",
	[SIGVTALRM] = "SIGVTALRM",
	[SIGPROF] = "SIGPROF",
	[SIGWINCH] = "SIGWINCH",
	[SIGINFO] = "SIGINFO",
	[SIGUSR1] = "SIGUSR1",
	[SIGUSR2] = "SIGUSR2",
	[SIGTHR] = "SIGTHR",
	[SIGLIBRT] = "SIGLIBRT",

	/* XXX: Solaris uses SIGRTMIN, SIGRTMIN+<x>...SIGRTMAX-<x>, SIGRTMAX */
	[SIGRTMIN] = "SIGRT0",
	[SIGRTMIN + 1] = "SIGRT1",
	[SIGRTMIN + 2] = "SIGRT2",
	[SIGRTMIN + 3] = "SIGRT3",
	[SIGRTMIN + 4] = "SIGRT4",
	[SIGRTMIN + 5] = "SIGRT5",
	[SIGRTMIN + 6] = "SIGRT6",
	[SIGRTMIN + 7] = "SIGRT7",
	[SIGRTMIN + 8] = "SIGRT8",
	[SIGRTMIN + 9] = "SIGRT9",
	[SIGRTMIN + 10] = "SIGRT10",
	[SIGRTMIN + 11] = "SIGRT11",
	[SIGRTMIN + 12] = "SIGRT12",
	[SIGRTMIN + 13] = "SIGRT13",
	[SIGRTMIN + 14] = "SIGRT14",
	[SIGRTMIN + 15] = "SIGRT15",
	[SIGRTMIN + 16] = "SIGRT16",
	[SIGRTMIN + 17] = "SIGRT17",
	[SIGRTMIN + 18] = "SIGRT18",
	[SIGRTMIN + 19] = "SIGRT19",
	[SIGRTMIN + 20] = "SIGRT20",
	[SIGRTMIN + 21] = "SIGRT21",
	[SIGRTMIN + 22] = "SIGRT22",
	[SIGRTMIN + 23] = "SIGRT23",
	[SIGRTMIN + 24] = "SIGRT24",
	[SIGRTMIN + 25] = "SIGRT25",
	[SIGRTMIN + 26] = "SIGRT26",
	[SIGRTMIN + 27] = "SIGRT27",
	[SIGRTMIN + 28] = "SIGRT28",
	[SIGRTMIN + 29] = "SIGRT29",
	[SIGRTMIN + 30] = "SIGRT30",
	[SIGRTMIN + 31] = "SIGRT31",
	[SIGRTMIN + 32] = "SIGRT32",
	[SIGRTMIN + 33] = "SIGRT33",
	[SIGRTMIN + 34] = "SIGRT34",
	[SIGRTMIN + 35] = "SIGRT35",
	[SIGRTMIN + 36] = "SIGRT36",
	[SIGRTMIN + 37] = "SIGRT37",
	[SIGRTMIN + 38] = "SIGRT38",
	[SIGRTMIN + 39] = "SIGRT39",
	[SIGRTMIN + 40] = "SIGRT40",
	[SIGRTMIN + 41] = "SIGRT41",
	[SIGRTMIN + 42] = "SIGRT42",
	[SIGRTMIN + 43] = "SIGRT43",
	[SIGRTMIN + 44] = "SIGRT44",
	[SIGRTMIN + 45] = "SIGRT45",
	[SIGRTMIN + 46] = "SIGRT46",
	[SIGRTMIN + 47] = "SIGRT47",
	[SIGRTMIN + 48] = "SIGRT48",
	[SIGRTMIN + 49] = "SIGRT49",
	[SIGRTMIN + 50] = "SIGRT50",
	[SIGRTMIN + 51] = "SIGRT51",
	[SIGRTMIN + 52] = "SIGRT52",
	[SIGRTMIN + 53] = "SIGRT53",
	[SIGRTMIN + 54] = "SIGRT54",
	[SIGRTMIN + 55] = "SIGRT55",
	[SIGRTMIN + 56] = "SIGRT56",
	[SIGRTMIN + 57] = "SIGRT57",
	[SIGRTMIN + 58] = "SIGRT58",
	[SIGRTMIN + 59] = "SIGRT59",
	[SIGRTMIN + 60] = "SIGRT60",
	[SIGRTMIN + 61] = "SIGRT61",
};

const char *
sysdecode_signal(int sig)
{

	if ((unsigned)sig < nitems(signames))
		return (signames[sig]);
	return (NULL);
}
