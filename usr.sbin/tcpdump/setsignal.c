/*	$OpenBSD: setsignal.c,v 1.6 2015/10/14 04:55:17 guenther Exp $	*/

/*
 * Copyright (c) 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/types.h>

#include <signal.h>
#include <string.h>

#include "setsignal.h"

void
setsignal(int sig, void (*func)(int))
{
	struct sigaction sa;

	if (sigaction(sig, NULL, &sa) == 0 && sa.sa_handler != SIG_IGN) {
		sa.sa_handler = func;
		sa.sa_flags = SA_RESTART;
		if (sig == SIGCHLD)
			sa.sa_flags |= SA_NOCLDSTOP;
		sigemptyset(&sa.sa_mask);
		sigaction(sig, &sa, NULL);
	}
}

