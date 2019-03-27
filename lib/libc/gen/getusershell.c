/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1985, 1993
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

#include <sys/cdefs.h>
__SCCSID("@(#)getusershell.c	8.1 (Berkeley) 6/4/93");
__RCSID("$NetBSD: getusershell.c,v 1.17 1999/01/25 01:09:34 lukem Exp $");
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/file.h>

#include <ctype.h>
#include <errno.h>
#include <nsswitch.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>

#ifdef HESIOD
#include <hesiod.h>
#endif
#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yp_prot.h>
#endif
#include "un-namespace.h"

static const char *const *curshell;
static StringList	 *sl;

static const char *const *initshells(void);

/*
 * Get a list of shells from "shells" nsswitch database
 */
char *
getusershell(void)
{
	char *ret;

	if (curshell == NULL)
		curshell = initshells();
	/*LINTED*/
	ret = (char *)*curshell;
	if (ret != NULL)
		curshell++;
	return (ret);
}

void
endusershell(void)
{
	if (sl) {
		sl_free(sl, 1);
		sl = NULL;
	}
	curshell = NULL;
}

void
setusershell(void)
{

	curshell = initshells();
}


static int	_local_initshells(void *, void *, va_list);

/*ARGSUSED*/
static int
_local_initshells(void	*rv, void *cb_data, va_list ap)
{
	char	*sp, *cp;
	FILE	*fp;
	char	 line[MAXPATHLEN + 2];

	if (sl)
		sl_free(sl, 1);
	sl = sl_init();

	if ((fp = fopen(_PATH_SHELLS, "re")) == NULL)
		return NS_UNAVAIL;

	while (fgets(line, MAXPATHLEN + 1, fp) != NULL) {
		cp = line;
		while (*cp != '#' && *cp != '/' && *cp != '\0')
			cp++;
		if (*cp == '#' || *cp == '\0')
			continue;
		sp = cp;
		while (!isspace(*cp) && *cp != '#' && *cp != '\0')
			cp++;
		*cp = '\0';
		sl_add(sl, strdup(sp));
	}
	(void)fclose(fp);
	return NS_SUCCESS;
}

#ifdef HESIOD
static int	_dns_initshells(void *, void *, va_list);

/*ARGSUSED*/
static int
_dns_initshells(void *rv, void *cb_data, va_list ap)
{
	char	  shellname[] = "shells-XXXXX";
	int	  hsindex, hpi, r;
	char	**hp;
	void	 *context;

	if (sl)
		sl_free(sl, 1);
	sl = sl_init();
	r = NS_UNAVAIL;
	if (hesiod_init(&context) == -1)
		return (r);

	for (hsindex = 0; ; hsindex++) {
		snprintf(shellname, sizeof(shellname)-1, "shells-%d", hsindex);
		hp = hesiod_resolve(context, shellname, "shells");
		if (hp == NULL) {
			if (errno == ENOENT) {
				if (hsindex == 0)
					r = NS_NOTFOUND;
				else
					r = NS_SUCCESS;
			}
			break;
		} else {
			for (hpi = 0; hp[hpi]; hpi++)
				sl_add(sl, hp[hpi]);
			free(hp);
		}
	}
	hesiod_end(context);
	return (r);
}
#endif /* HESIOD */

#ifdef YP
static int	_nis_initshells(void *, void *, va_list);

/*ARGSUSED*/
static int
_nis_initshells(void *rv, void *cb_data, va_list ap)
{
	static char *ypdomain;
	char	*key, *data;
	char	*lastkey;
	int	 keylen, datalen;
	int	 r;

	if (sl)
		sl_free(sl, 1);
	sl = sl_init();

	if (ypdomain == NULL) {
		switch (yp_get_default_domain(&ypdomain)) {
		case 0:
			break;
		case YPERR_RESRC:
			return NS_TRYAGAIN;
		default:
			return NS_UNAVAIL;
		}
	}

	/*
	 * `key' and `data' point to strings dynamically allocated by
	 * the yp_... functions.
	 * `data' is directly put into the stringlist of shells.
	 */
	key = data = NULL;
	if (yp_first(ypdomain, "shells", &key, &keylen, &data, &datalen))
		return NS_UNAVAIL;
	do {
		data[datalen] = '\0';		/* clear trailing \n */
		sl_add(sl, data);

		lastkey = key;
		r = yp_next(ypdomain, "shells", lastkey, keylen,
		    &key, &keylen, &data, &datalen);
		free(lastkey);
	} while (r == 0);
	
	if (r == YPERR_NOMORE) {
		/*
		 * `data' and `key' ought to be NULL - do not try to free them.
		 */
		return NS_SUCCESS;
	}

	return NS_UNAVAIL;
}
#endif /* YP */

static const char *const *
initshells(void)
{
	static const ns_dtab dtab[] = {
		NS_FILES_CB(_local_initshells, NULL)
		NS_DNS_CB(_dns_initshells, NULL)
		NS_NIS_CB(_nis_initshells, NULL)
		{ 0 }
	};
	if (sl)
		sl_free(sl, 1);
	sl = sl_init();

	if (_nsdispatch(NULL, dtab, NSDB_SHELLS, "initshells", __nsdefaultsrc)
	    != NS_SUCCESS) {
		if (sl)
			sl_free(sl, 1);
		sl = sl_init();
		/*
		 * Local shells should NOT be added here.  They should be
		 * added in /etc/shells.
		 */
		sl_add(sl, strdup(_PATH_BSHELL));
		sl_add(sl, strdup(_PATH_CSHELL));
	}
	sl_add(sl, NULL);

	return (const char *const *)(sl->sl_str);
}
