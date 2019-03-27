/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 - 1999, 2001 Brian Somers <brian@Awfulhak.org>
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <signal.h>

#include "log.h"
#include "sig.h"

static int caused[NSIG];	/* An array of pending signals */
static int necessary;		/* Anything set ? */
static sig_type handler[NSIG];	/* all start at SIG_DFL */


/*
 * Record a signal in the "caused" array
 *
 * This function is the only thing actually called in signal context.  It
 * records that a signal has been caused and that sig_Handle() should be
 * called (in non-signal context) as soon as possible to process that
 * signal.
 */
static void
signal_recorder(int sig)
{
  caused[sig - 1]++;
  necessary = 1;
}


/*
 * Set up signal_recorder to handle the given sig and record ``fn'' as
 * the function to ultimately call in sig_Handle().  ``fn'' will not be
 * called in signal context (as sig_Handle() is not called in signal
 * context).
 */
sig_type
sig_signal(int sig, sig_type fn)
{
  sig_type Result;

  if (sig <= 0 || sig > NSIG) {
    /* Oops - we must be a bit out of date (too many sigs ?) */
    log_Printf(LogALERT, "Eeek! %s:%d: I must be out of date!\n",
	      __FILE__, __LINE__);
    return signal(sig, fn);
  }
  Result = handler[sig - 1];
  if (fn == SIG_DFL || fn == SIG_IGN) {
    signal(sig, fn);
    handler[sig - 1] = (sig_type) 0;
  } else {
    handler[sig - 1] = fn;
    signal(sig, signal_recorder);
  }
  caused[sig - 1] = 0;
  return Result;
}


/*
 * Call the handlers for any pending signals
 *
 * This function is called from a non-signal context - in fact, it's
 * called every time select() in DoLoop() returns - just in case
 * select() returned due to a signal being recorded by signal_recorder().
 */
int
sig_Handle()
{
  int sig;
  int got;
  int result;

  result = 0;
  if (necessary) {
    /* We've *probably* got something in `caused' set */
    necessary = 0;
    /* `necessary' might go back to 1 while we're in here.... */
    do {
      got = 0;
      for (sig = 0; sig < NSIG; sig++)
        if (caused[sig]) {
	  caused[sig]--;
	  got++;
	  result++;
	  (*handler[sig])(sig + 1);
        }
    } while (got);
  }

  return result;
}
