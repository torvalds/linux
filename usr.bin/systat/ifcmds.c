/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003, Trent Nelson, <trent@arpa.com>.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include "systat.h"
#include "extern.h"
#include "convtbl.h"

#include <stdlib.h>
#include <string.h>

int curscale = SC_AUTO;
char *matchline = NULL;
int showpps = 0;
int needsort = 0;

int
ifcmd(const char *cmd, const char *args)
{
	int scale;

	if (prefix(cmd, "scale")) {
		if ((scale = get_scale(args)) != -1)
			curscale = scale;
		else {
			move(CMDLINE, 0);
			clrtoeol();
			addstr("what scale? ");
			addstr(get_helplist());
		}
	} else if (prefix(cmd, "match")) {
		if (args != NULL && *args != '\0' && memcmp(args, "*", 2) != 0) {
			/* We got a valid match line */
			if (matchline != NULL)
				free(matchline);
			needsort = 1;
			matchline = strdup(args);
		} else {
			/* Empty or * pattern, turn filtering off */
			if (matchline != NULL)
				free(matchline);
			needsort = 1;
			matchline = NULL;
		}
	} else if (prefix(cmd, "pps"))
		showpps = !showpps;

	return (1);
}
