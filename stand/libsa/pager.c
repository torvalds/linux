/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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
/*
 * Simple paged-output and paged-viewing functions
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "stand.h"
#include <string.h>

static int	p_maxlines = -1;
static int	p_freelines;

static char *pager_prompt1 = " --more--  <space> page down <enter> line down <q> quit ";
static char *pager_blank   = "                                                        ";

/*
 * 'open' the pager
 */
void
pager_open(void)
{
    int		nlines;
    char	*cp, *lp;
    
    nlines = 24;		/* sensible default */
    if ((cp = getenv("LINES")) != NULL) {
	nlines = strtol(cp, &lp, 0);
    }

    p_maxlines = nlines - 1;
    if (p_maxlines < 1)
	p_maxlines = 1;
    p_freelines = p_maxlines;
}

/*
 * 'close' the pager
 */
void
pager_close(void)
{
    p_maxlines = -1;
}

/*
 * Emit lines to the pager; may not return until the user
 * has responded to the prompt.
 *
 * Will return nonzero if the user enters 'q' or 'Q' at the prompt.
 *
 * XXX note that this watches outgoing newlines (and eats them), but
 *     does not handle wrap detection (req. count of columns).
 */

int
pager_output(const char *cp)
{
    int		action;

    if (cp == NULL)
	return(0);
    
    for (;;) {
	if (*cp == 0)
	    return(0);
	
	putchar(*cp);			/* always emit character */

	if (*(cp++) == '\n') {		/* got a newline? */
	    p_freelines--;
	    if (p_freelines <= 0) {
		printf("%s", pager_prompt1);
		action = 0;
		while (action == 0) {
		    switch(getchar()) {
		    case '\r':
		    case '\n':
			p_freelines = 1;
			action = 1;
			break;
		    case ' ':
			p_freelines = p_maxlines;
			action = 1;
			break;
		    case 'q':
		    case 'Q':
			action = 2;
			break;
		    default:
			break;
		    }
		}
		printf("\r%s\r", pager_blank);
		if (action == 2)
		    return(1);
	    }
	}
    }
}

/*
 * Display from (fd).
 */
int
pager_file(const char *fname)
{
    char	buf[80];
    size_t	hmuch;
    int		fd;
    int		result;
    
    if ((fd = open(fname, O_RDONLY)) == -1) {
	printf("can't open '%s': %s\n", fname, strerror(errno));
	return(-1);
    }

    for (;;) {
	hmuch = read(fd, buf, sizeof(buf) - 1);
	if (hmuch == -1) {
	    result = -1;
	    break;
	}
	if (hmuch == 0) {
	    result = 0;
	    break;
	}
	buf[hmuch] = 0;
	if (pager_output(buf)) {
	    result = 1;
	    break;
	}
    }
    close(fd);
    return(result);
}
