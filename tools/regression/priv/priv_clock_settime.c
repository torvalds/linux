/*-
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * Copyright (c) 2007 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project under contract to nCircle Network Security, Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR, NCIRCLE NETWORK SECURITY,
 * INC., OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Confirm that privilege is required to invoke clock_settime().  So as not
 * to mess up the clock too much, first query the time, then immediately set
 * it.  Test only CLOCK_REALTIME.
 */

#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>

#include "main.h"

static struct timespec	the_time;

int
priv_clock_settime_setup(int asroot, int injail, struct test *test)
{

	if (clock_gettime(CLOCK_REALTIME, &the_time) < 0) {
		warn("priv_clock_settime_setup: "
		    "clock_gettime(CLOCK_REALTIME)");
		return (-1);
	}
	return (0);
}

void
priv_clock_settime(int asroot, int injail, struct test *test)
{
	int error;

	error = clock_settime(CLOCK_REALTIME, &the_time);
	if (asroot && injail)
		expect("priv_clock_settime(asroot, injail)", error, -1,
		    EPERM);
	if (asroot && !injail)
		expect("priv_clock_settime(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_clock_settime(!asroot, injail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_clock_settime(!asroot, !injail", error, -1,
		    EPERM);
}

void
priv_clock_settime_cleanup(int asroot, int injail, struct test *test)
{

}
