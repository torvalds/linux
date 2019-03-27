/*-
 * Copyright (c) 2014 Mark Johnston <markj@FreeBSD.org>
 * All rights reserved.
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

#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t saw;

static void
usr1(int sig __unused)
{

	saw = 1;
}

void	foo(void);
void	qux(void);

void
foo(void)
{
}
__weak_reference(foo, _foo);

static void
bar(void)
{
}
__strong_reference(bar, baz);

void
qux(void)
{
}
__strong_reference(qux, $qux);

int
main(int argc, char **argv)
{

	bar(); /* force the symbol to be emitted */

	if (argc == 1)
		return (EXIT_SUCCESS);
	if (argc == 2 && strcmp(argv[1], "-s") == 0) {
		if (signal(SIGUSR1, usr1) == SIG_ERR)
			err(1, "signal");
		if (kill(getpid(), SIGUSR1) != 0)
			err(1, "kill");
		return (saw == 1 ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return (EXIT_FAILURE);
}
