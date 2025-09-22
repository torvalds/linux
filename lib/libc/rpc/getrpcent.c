/*	$OpenBSD: getrpcent.c,v 1.22 2020/10/13 04:42:28 guenther Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <limits.h>
#include <rpc/rpc.h>

/*
 * Internet version.
 */
static struct rpcdata {
	FILE	*rpcf;
	int	stayopen;
#define	MAXALIASES	35
	char	*rpc_aliases[MAXALIASES];
	struct	rpcent rpc;
	char	line[BUFSIZ+1];
} *rpcdata;

static	struct rpcent *interpret(char *val, int len);

static const char RPCDB[] = "/etc/rpc";

static struct rpcdata *
_rpcdata(void)
{
	struct rpcdata *d = rpcdata;

	if (d == NULL) {
		d = calloc(1, sizeof (struct rpcdata));
		rpcdata = d;
	}
	return (d);
}

struct rpcent *
getrpcbynumber(int number)
{
	struct rpcdata *d = _rpcdata();
	struct rpcent *p;

	if (d == NULL)
		return (0);
	setrpcent(0);
	while ((p = getrpcent())) {
		if (p->r_number == number)
			break;
	}
	endrpcent();
	return (p);
}

struct rpcent *
getrpcbyname(char *name)
{
	struct rpcent *rpc;
	char **rp;

	setrpcent(0);
	while ((rpc = getrpcent())) {
		if (strcmp(rpc->r_name, name) == 0)
			goto done;
		for (rp = rpc->r_aliases; *rp != NULL; rp++) {
			if (strcmp(*rp, name) == 0)
				goto done;
		}
	}
done:
	endrpcent();
	return (rpc);
}

void
setrpcent(int f)
{
	struct rpcdata *d = _rpcdata();

	if (d == NULL)
		return;
	if (d->rpcf == NULL)
		d->rpcf = fopen(RPCDB, "re");
	else
		rewind(d->rpcf);
	d->stayopen |= f;
}
DEF_WEAK(setrpcent);

void
endrpcent(void)
{
	struct rpcdata *d = _rpcdata();

	if (d == NULL)
		return;
	if (d->rpcf && !d->stayopen) {
		fclose(d->rpcf);
		d->rpcf = NULL;
	}
}
DEF_WEAK(endrpcent);

struct rpcent *
getrpcent(void)
{
	struct rpcdata *d = _rpcdata();

	if (d == NULL)
		return(NULL);
	if (d->rpcf == NULL && (d->rpcf = fopen(RPCDB, "re")) == NULL)
		return (NULL);
	/* -1 so there is room to append a \n below */
        if (fgets(d->line, sizeof(d->line) - 1, d->rpcf) == NULL)
		return (NULL);
	return (interpret(d->line, strlen(d->line)));
}
DEF_WEAK(getrpcent);

static struct rpcent *
interpret(char *val, int len)
{
	const char *errstr;
	struct rpcdata *d = _rpcdata();
	char *p;
	char *cp, *num, **q;

	if (d == NULL)
		return (0);
	strlcpy(d->line, val, sizeof(d->line));
	p = d->line;
	p[len] = '\n';
	if (*p == '#')
		return (getrpcent());
	cp = strpbrk(p, "#\n");
	if (cp == NULL)
		return (getrpcent());
	*cp = '\0';
	cp = strpbrk(p, " \t");
	if (cp == NULL)
		return (getrpcent());
	*cp++ = '\0';
	/* THIS STUFF IS INTERNET SPECIFIC */
	d->rpc.r_name = d->line;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	num = cp;
	cp = strpbrk(cp, " \t");
	if (cp != NULL)
		*cp++ = '\0';
	d->rpc.r_number = strtonum(num, 0, INT_MAX, &errstr);
	if (errstr)
		return (0);
	q = d->rpc.r_aliases = d->rpc_aliases;
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q < &(d->rpc_aliases[MAXALIASES - 1]))
			*q++ = cp;
		cp = strpbrk(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
	}
	*q = NULL;
	return (&d->rpc);
}

