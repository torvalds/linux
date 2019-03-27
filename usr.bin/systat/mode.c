/*
 * Copyright 1997 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * mode.c - mechanisms for dealing with SGI-style modal displays.
 *
 * There are four generally-understood useful modes for status displays
 * of the sort exemplified by the IRIX ``netstat -C'' and ``osview''
 * programs.  We try to follow their example, although the user interface
 * and terminology slightly differ.
 *
 * RATE - the default mode - displays the precise rate of change in
 * each statistic in units per second, regardless of the actual display
 * update interval.
 *
 * DELTA - displays the change in each statistic over the entire
 * display update interval (i.e., RATE * interval).
 *
 * SINCE - displays the total change in each statistic since the module
 * was last initialized or reset.
 *
 * ABSOLUTE - displays the current value of each statistic.
 *
 * In the SGI programs, these modes are selected by the single-character
 * commands D, W, N, and A.  In systat, they are the slightly-harder-to-type
 * ``mode delta'', etc.  The initial value for SINCE mode is initialized
 * when the module is first started and can be reset using the ``reset''
 * command (as opposed to the SGI way where changing modes implicitly
 * resets).  A ``mode'' command with no arguments displays the current
 * mode in the command line.
 */

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include "systat.h"
#include "extern.h"
#include "mode.h"

enum mode currentmode = display_RATE;

static const char *const modes[] = { "rate", "delta", "since", "absolute" };

int
cmdmode(const char *cmd, const char *args)
{
	if (prefix(cmd, "mode")) {
		if (args[0] == '\0') {
			move(CMDLINE, 0);
			clrtoeol();
			printw("%s", modes[currentmode]);
		} else if (prefix(args, "rate")) {
			currentmode = display_RATE;
		} else if (prefix(args, "delta")) {
			currentmode = display_DELTA;
		} else if (prefix(args, "since")) {
			currentmode = display_SINCE;
		} else if (prefix(args, "absolute")) {
			currentmode = display_ABS;
		} else {
			printw("unknown mode `%s'", args);
		}
		return 1;
	}
	if(prefix(cmd, "reset")) {
		curcmd->c_reset();
		return 1;
	}
	return 0;
}
