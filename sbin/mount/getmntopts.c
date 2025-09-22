/*	$OpenBSD: getmntopts.c,v 1.13 2019/06/28 13:32:44 deraadt Exp $	*/
/*	$NetBSD: getmntopts.c,v 1.3 1995/03/18 14:56:58 cgd Exp $	*/

/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mount.h>

#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "mntopts.h"

int
getmntopts(const char *optionp, const struct mntopt *m0, int *flagp)
{
	char *p, *q;
	union mntval val;
	int ret = 0;

	p = q = strdup(optionp);
	if (p == NULL)
		err(1, NULL);
	while (p != NULL) {
		ret |= getmntopt(&p, &val, m0, flagp);
	}
	free(q);
	return (ret);
}

int
getmntopt(char **optionp, union mntval *valuep, const struct mntopt *m0,
    int *flagp)
{
	const struct mntopt *m;
	char *opt, *value, *endp;
	long l;
	int inverse, negative, needval, ret = 0;

	/* Pull out the next option. */
	do {
		if (*optionp == NULL)
			return (0);
		opt = strsep(optionp, ",");
	} while (opt == NULL || *opt == '\0');

	/* Check for "no" prefix. */
	if (opt[0] == 'n' && opt[1] == 'o') {
		negative = 1;
		opt += 2;
	} else
		negative = 0;

	/* Stash the value for options with assignments in them. */
	if ((value = strchr(opt, '=')) != NULL)
		*value++ = '\0';

	/* Scan option table. */
	for (m = m0; m->m_option != NULL; ++m)
		if (strcasecmp(opt, m->m_option) == 0)
			break;

	/* Save flag, or fail if option is not recognised. */
	if (m->m_option) {
		needval = (m->m_oflags & (MFLAG_INTVAL|MFLAG_STRVAL)) != 0;
		if (needval != (value != NULL) && !(m->m_oflags & MFLAG_OPT))
			errx(1, "-o %s: option %s a value", opt,
			    needval ? "needs" : "does not need");
		inverse = (m->m_oflags & MFLAG_INVERSE) ? 1 : 0;
		if (m->m_oflags & MFLAG_SET) {
			if (negative == inverse)
				*flagp |= m->m_flag;
			else
				*flagp &= ~m->m_flag;
		}
		else if (negative == inverse)
			ret = m->m_flag;
	} else
		errx(1, "-o %s: option not supported", opt);

	/* Store the value for options with assignments in them. */
	if (value != NULL) {
		if (m->m_oflags & MFLAG_INTVAL) {
			errno = 0;
			l = strtol(value, &endp, 10);
			if (endp == value || l == -1 || l > INT_MAX ||
			    (l == LONG_MAX && errno == ERANGE))
				errx(1, "%s: illegal value '%s'",
				    opt, value);
			valuep->ival = (int)l;
		} else
			valuep->strval = value;
	} else
		memset(valuep, 0, sizeof(*valuep));
	return (ret);
}
