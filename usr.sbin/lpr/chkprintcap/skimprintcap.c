/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2001  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project.
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/param.h>		/* needed for lp.h but not used here */
#include <dirent.h>		/* ditto */
#include "lp.h"
#include "lp.local.h"
#include "skimprintcap.h"

/*
 * Save the canonical queue name of the entry that is currently being
 * scanned, in case a warning message is printed for the current queue.
 * Only the first 'QENTRY_MAXLEN' characters will be saved, since this
 * is only for warning messages.   The variable includes space for the
 * string " (entry " and a trailing ")", when the scanner is in the
 * middle of an entry.  When the scanner is not in a specific entry,
 * the variable will be the a null string.
 */
#define QENTRY_MAXLEN	30
#define QENTRY_PREFIX	" (entry "
static char	 skim_entryname[sizeof(QENTRY_PREFIX) + QENTRY_MAXLEN + 2];

/*
 * isgraph is defined to work on an 'int', in the range 0 to 255, plus EOF.
 * Define a wrapper which can take 'char', either signed or unsigned.
 */
#define isgraphch(Anychar)    isgraph(((int) Anychar) & 255)

struct skiminfo *
skim_printcap(const char *pcap_fname, int verbosity)
{
	struct skiminfo *skinf;
	char buff[BUFSIZ];
	char *ch, *curline, *endfield, *lastchar;
	FILE *pc_file;
	int missing_nl;
	enum {NO_CONTINUE, WILL_CONTINUE, BAD_CONTINUE} is_cont, had_cont;
	enum {CMNT_LINE, ENTRY_LINE, TAB_LINE, TABERR_LINE} is_type, had_type;

	skinf = malloc(sizeof(struct skiminfo));
	if (skinf == NULL)
		return (NULL);
	memset(skinf, 0, sizeof(struct skiminfo));

	pc_file = fopen(pcap_fname, "r");
	if (pc_file == NULL) {
		warn("fopen(%s)", pcap_fname);
		skinf->fatalerr++;
		return (skinf);		/* fatal error */
	}

	skim_entryname[0] = '0';

	is_cont = NO_CONTINUE;
	is_type = CMNT_LINE;
	errno = 0;
	curline = fgets(buff, sizeof(buff), pc_file);
	while (curline != NULL) {
		skinf->lines++;

		/* Check for the expected newline char, and remove it */
		missing_nl = 0;
		lastchar = strchr(curline, '\n');
		if (lastchar != NULL)
			*lastchar = '\0';
		else {
			lastchar = strchr(curline, '\0');
			missing_nl = 1;
		}
		if (curline < lastchar)
			lastchar--;

		/*
		 * Check for `\' (continuation-character) at end of line.
		 * If there is none, then trim off spaces and check again.
		 * This would be a bad line because it looks like it is
		 * continued, but it will not be treated that way.
		 */
		had_cont = is_cont;
		is_cont = NO_CONTINUE;
		if (*lastchar == '\\') {
			is_cont = WILL_CONTINUE;
			lastchar--;
		} else {
			while ((curline < lastchar) && !isgraphch(*lastchar))
				lastchar--;
			if (*lastchar == '\\')
				is_cont = BAD_CONTINUE;
		}

		had_type = is_type;
		is_type = CMNT_LINE;
		switch (*curline) {
		case '\0':	/* treat zero-length line as comment */
		case '#':
			skinf->comments++;
			break;
		case ' ':
		case '\t':
			is_type = TAB_LINE;
			break;
		default:
			is_type = ENTRY_LINE;
			skinf->entries++;

			/* pick up the queue name, to use in warning messages */
			ch = curline;
			while ((ch <= lastchar) && (*ch != ':') && (*ch != '|'))
				ch++;
			ch--;			/* last char of queue name */
			strcpy(skim_entryname, QENTRY_PREFIX);
			if ((ch - curline) > QENTRY_MAXLEN) {
				strncat(skim_entryname, curline, QENTRY_MAXLEN
				    - 1);
				strcat(skim_entryname, "+");
			} else {
				strncat(skim_entryname, curline, (ch - curline
				    + 1));
			}
			strlcat(skim_entryname, ")", sizeof(skim_entryname));
			break;
		}

		/*
		 * Check to see if the previous line was a bad contination
		 * line.  The check is delayed until now so a warning message
		 * is not printed when a "bad continuation" is on a comment
		 * line, and it just "continues" into another comment line.
		*/
		if (had_cont == BAD_CONTINUE) {
			if ((had_type != CMNT_LINE) || (is_type != CMNT_LINE) ||
			    (verbosity > 1)) {
				skinf->warnings++;
				warnx("Warning: blanks after trailing '\\'," 
				    " at line %d%s", skinf->lines - 1,
				    skim_entryname);
			}
		}

		/* If we are no longer in an entry, then forget the name */
		if ((had_cont != WILL_CONTINUE) && (is_type != ENTRY_LINE)) {
			skim_entryname[0] = '\0';
		}

		/*
		 * Print out warning for missing newline, done down here
		 * so we are sure to have the right entry-name for it.
		*/
		if (missing_nl) {
			skinf->warnings++;
			warnx("Warning: No newline at end of line %d%s",
			    skinf->lines, skim_entryname);
		}

		/*
		 * Check for start-of-entry lines which do not include a
		 * ":" character (to indicate the end of the name field).
		 * This can cause standard printcap processing to ignore
		 * ALL of the following lines.
		 * XXXXX - May need to allow for the list-of-names to
		 *         continue on to the following line...
		*/
		if (is_type == ENTRY_LINE) {
			endfield = strchr(curline, ':');
			if (endfield == NULL) {
				skinf->warnings++;
				warnx("Warning: No ':' to terminate name-field"
				    " at line %d%s", skinf->lines,
				    skim_entryname);
			}
		}

		/*
		 * Now check for cases where this line is (or is-not) a
		 * continuation of the previous line, and a person skimming
		 * the file would assume it is not (or is) a continuation.
		*/
		switch (had_cont) {
		case NO_CONTINUE:
		case BAD_CONTINUE:
			if (is_type == TAB_LINE) {
				skinf->warnings++;
				warnx("Warning: values-line after line with" 
				    " NO trailing '\\', at line %d%s",
				    skinf->lines, skim_entryname);
			}
			break;

		case WILL_CONTINUE:
			if (is_type == ENTRY_LINE) {
				skinf->warnings++;
				warnx("Warning: new entry starts after line" 
				    " with trailing '\\', at line %d%s",
				    skinf->lines, skim_entryname);
			}
			break;
		}

		/* get another line from printcap and repeat loop */
		curline = fgets(buff, sizeof(buff), pc_file);
	}

	if (errno != 0) {
		warn("fgets(%s)", pcap_fname);
		skinf->fatalerr++;		/* fatal error */
	}

	if (skinf->warnings > 0)
		warnx("%4d warnings from skimming %s", skinf->warnings,
		    pcap_fname);

	if (verbosity)
		warnx("%4d lines (%d comments), %d entries for %s",
		    skinf->lines, skinf->comments, skinf->entries, pcap_fname);

	fclose(pc_file);
	return (skinf);
}
