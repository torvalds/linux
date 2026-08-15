/* SPDX-License-Identifier: GPL-2.0 */
#include <signal.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <unistd.h>
#include "../tests.h"

/* We want to check these symbols in perf script */
noinline void leaf(void);
noinline void parent(void);

static volatile sig_atomic_t done asm("leafloop_done");

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

#if defined(__aarch64__)
/*
 * Write leaf() in assembly so it stays as a minimal leaf function with no
 * stack frame and won't get silently broken in the future by any Perf wide
 * compilation options like -fstack-protector-all.
 */
asm(
	".pushsection .text,\"ax\",%progbits\n"
	".global leaf\n"
	".type leaf, %function\n"
	"leaf:\n"
	"	adrp	x1, leafloop_done\n"
	"	ldr	w2, [x1, #:lo12:leafloop_done]\n"
	"	cbz	w2, leaf\n"
	"	ret\n"
	".size leaf, .-leaf\n"
	".popsection\n"
);

#else

noinline void leaf(void)
{
	while (!done)
		;
}

#endif

noinline void parent(void)
{
	leaf();
}

static int leafloop(int argc, const char **argv)
{
	int sec = 1;

	if (argc > 0)
		sec = atoi(argv[0]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	parent();
	return 0;
}

DEFINE_WORKLOAD(leafloop);
