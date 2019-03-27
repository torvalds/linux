/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999, Boris Popov
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * from: FreeBSD: src/lib/libncp/ncpl_rcfile.c,v 1.5 2007/01/09 23:27:39 imp Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/queue.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>

#include "nandsim_rcfile.h"

SLIST_HEAD(rcfile_head, rcfile);
static struct rcfile_head pf_head = {NULL};
static struct rcsection *rc_findsect(struct rcfile *rcp,
    const char *sectname, int sect_id);
static struct rcsection *rc_addsect(struct rcfile *rcp,
    const char *sectname);
static int rc_sect_free(struct rcsection *rsp);
static struct rckey *rc_sect_findkey(struct rcsection *rsp,
    const char *keyname);
static struct rckey *rc_sect_addkey(struct rcsection *rsp, const char *name,
    char *value);
static void rc_key_free(struct rckey *p);
static void rc_parse(struct rcfile *rcp);

static struct rcfile* rc_find(const char *filename);

/*
 * open rcfile and load its content, if already open - return previous handle
 */
int
rc_open(const char *filename, const char *mode,struct rcfile **rcfile)
{
	struct rcfile *rcp;
	FILE *f;
	rcp = rc_find(filename);
	if (rcp) {
		*rcfile = rcp;
		return (0);
	}
	f = fopen (filename, mode);
	if (f == NULL)
		return errno;
	rcp = malloc(sizeof(struct rcfile));
	if (rcp == NULL) {
		fclose(f);
		return ENOMEM;
	}
	bzero(rcp, sizeof(struct rcfile));
	rcp->rf_name = strdup(filename);
	rcp->rf_f = f;
	SLIST_INSERT_HEAD(&pf_head, rcp, rf_next);
	rc_parse(rcp);
	*rcfile = rcp;
	return (0);
}

int
rc_close(struct rcfile *rcp)
{
	struct rcsection *p,*n;

	fclose(rcp->rf_f);
	for (p = SLIST_FIRST(&rcp->rf_sect); p; ) {
		n = p;
		p = SLIST_NEXT(p,rs_next);
		rc_sect_free(n);
	}
	free(rcp->rf_name);
	SLIST_REMOVE(&pf_head, rcp, rcfile, rf_next);
	free(rcp);
	return (0);
}

static struct rcfile*
rc_find(const char *filename)
{
	struct rcfile *p;

	SLIST_FOREACH(p, &pf_head, rf_next)
		if (strcmp (filename, p->rf_name) == 0)
			return (p);
	return (0);
}

/* Find section with given name and id */
static struct rcsection *
rc_findsect(struct rcfile *rcp, const char *sectname, int sect_id)
{
	struct rcsection *p;

	SLIST_FOREACH(p, &rcp->rf_sect, rs_next)
		if (strcmp(p->rs_name, sectname) == 0 && p->rs_id == sect_id)
			return (p);
	return (NULL);
}

static struct rcsection *
rc_addsect(struct rcfile *rcp, const char *sectname)
{
	struct rcsection *p;
	int id = 0;
	p = rc_findsect(rcp, sectname, 0);
	if (p) {
		/*
		 * If section with that name already exists -- add one more,
		 * same named, but with different id (higher by one)
		 */
		while (p != NULL) {
			id = p->rs_id + 1;
			p = rc_findsect(rcp, sectname, id);
		}
	}
	p = malloc(sizeof(*p));
	if (!p)
		return (NULL);
	p->rs_name = strdup(sectname);
	p->rs_id = id;
	SLIST_INIT(&p->rs_keys);
	SLIST_INSERT_HEAD(&rcp->rf_sect, p, rs_next);
	return (p);
}

static int
rc_sect_free(struct rcsection *rsp)
{
	struct rckey *p,*n;

	for (p = SLIST_FIRST(&rsp->rs_keys); p; ) {
		n = p;
		p = SLIST_NEXT(p,rk_next);
		rc_key_free(n);
	}
	free(rsp->rs_name);
	free(rsp);
	return (0);
}

static struct rckey *
rc_sect_findkey(struct rcsection *rsp, const char *keyname)
{
	struct rckey *p;

	SLIST_FOREACH(p, &rsp->rs_keys, rk_next)
		if (strcmp(p->rk_name, keyname)==0)
			return (p);
	return (NULL);
}

static struct rckey *
rc_sect_addkey(struct rcsection *rsp, const char *name, char *value)
{
	struct rckey *p;
	p = rc_sect_findkey(rsp, name);
	if (p) {
		free(p->rk_value);
	} else {
		p = malloc(sizeof(*p));
		if (!p)
			return (NULL);
		SLIST_INSERT_HEAD(&rsp->rs_keys, p, rk_next);
		p->rk_name = strdup(name);
	}
	p->rk_value = value ? strdup(value) : strdup("");
	return (p);
}

static void
rc_key_free(struct rckey *p)
{
	free(p->rk_value);
	free(p->rk_name);
	free(p);
}

enum { stNewLine, stHeader, stSkipToEOL, stGetKey, stGetValue};

static void
rc_parse(struct rcfile *rcp)
{
	FILE *f = rcp->rf_f;
	int state = stNewLine, c;
	struct rcsection *rsp = NULL;
	struct rckey *rkp = NULL;
	char buf[2048];
	char *next = buf, *last = &buf[sizeof(buf)-1];

	while ((c = getc (f)) != EOF) {
		if (c == '\r')
			continue;
		if (state == stNewLine) {
			next = buf;
			if (isspace(c))
				continue;	/* skip leading junk */
			if (c == '[') {
				state = stHeader;
				rsp = NULL;
				continue;
			}
			if (c == '#' || c == ';') {
				state = stSkipToEOL;
			} else {		/* something meaningful */
				state = stGetKey;
			}
		}
		if (state == stSkipToEOL || next == last) {/* ignore long lines */
			if (c == '\n') {
				state = stNewLine;
				next = buf;
			}
			continue;
		}
		if (state == stHeader) {
			if (c == ']') {
				*next = 0;
				next = buf;
				rsp = rc_addsect(rcp, buf);
				state = stSkipToEOL;
			} else
				*next++ = c;
			continue;
		}
		if (state == stGetKey) {
			if (c == ' ' || c == '\t')/* side effect: 'key name='*/
				continue;	  /* become 'keyname='	     */
			if (c == '\n') {	  /* silently ignore ... */
				state = stNewLine;
				continue;
			}
			if (c != '=') {
				*next++ = c;
				continue;
			}
			*next = 0;
			if (rsp == NULL) {
				fprintf(stderr, "Key '%s' defined before "
				    "section\n", buf);
				state = stSkipToEOL;
				continue;
			}
			rkp = rc_sect_addkey(rsp, buf, NULL);
			next = buf;
			state = stGetValue;
			continue;
		}
		/* only stGetValue left */
		if (state != stGetValue) {
			fprintf(stderr, "Well, I can't parse file "
			    "'%s'\n",rcp->rf_name);
			state = stSkipToEOL;
		}
		if (c != '\n') {
			*next++ = c;
			continue;
		}
		*next = 0;
		rkp->rk_value = strdup(buf);
		state = stNewLine;
		rkp = NULL;
	}	/* while */
	if (c == EOF && state == stGetValue) {
		*next = 0;
		rkp->rk_value = strdup(buf);
	}
}

int
rc_getstringptr(struct rcfile *rcp, const char *section, int sect_id,
    const char *key, char **dest)
{
	struct rcsection *rsp;
	struct rckey *rkp;

	*dest = NULL;
	rsp = rc_findsect(rcp, section, sect_id);
	if (!rsp)
		return (ENOENT);
	rkp = rc_sect_findkey(rsp,key);
	if (!rkp)
		return (ENOENT);
	*dest = rkp->rk_value;
	return (0);
}

int
rc_getstring(struct rcfile *rcp, const char *section, int sect_id,
    const char *key, unsigned int maxlen, char *dest)
{
	char *value;
	int error;

	error = rc_getstringptr(rcp, section, sect_id, key, &value);
	if (error)
		return (error);
	if (strlen(value) >= maxlen) {
		fprintf(stderr, "line too long for key '%s' in section '%s',"
		    "max = %d\n",key, section, maxlen);
		return (EINVAL);
	}
	strcpy(dest,value);
	return (0);
}

int
rc_getint(struct rcfile *rcp, const char *section, int sect_id,
    const char *key, int *value)
{
	struct rcsection *rsp;
	struct rckey *rkp;

	rsp = rc_findsect(rcp, section, sect_id);
	if (!rsp)
		return (ENOENT);
	rkp = rc_sect_findkey(rsp,key);
	if (!rkp)
		return (ENOENT);
	errno = 0;
	*value = strtol(rkp->rk_value,NULL,0);
	if (errno) {
		fprintf(stderr, "invalid int value '%s' for key '%s' in "
		    "section '%s'\n",rkp->rk_value,key,section);
		return (errno);
	}
	return (0);
}

/*
 * 1,yes,true
 * 0,no,false
 */
int
rc_getbool(struct rcfile *rcp, const char *section, int sect_id,
    const char *key, int *value)
{
	struct rcsection *rsp;
	struct rckey *rkp;
	char *p;

	rsp = rc_findsect(rcp, section, sect_id);
	if (!rsp)
		return (ENOENT);
	rkp = rc_sect_findkey(rsp,key);
	if (!rkp)
		return (ENOENT);
	p = rkp->rk_value;
	while (*p && isspace(*p)) p++;
	if (*p == '0' || strcasecmp(p,"no") == 0 ||
	    strcasecmp(p, "false") == 0 ||
	    strcasecmp(p, "off") == 0) {
		*value = 0;
		return (0);
	}
	if (*p == '1' || strcasecmp(p,"yes") == 0 ||
	    strcasecmp(p, "true") == 0 ||
	    strcasecmp(p, "on") == 0) {
		*value = 1;
		return (0);
	}
	fprintf(stderr, "invalid boolean value '%s' for key '%s' in section "
	    "'%s' \n",p, key, section);
	return (EINVAL);
}

/* Count how many sections with given name exists in configuration. */
int rc_getsectionscount(struct rcfile *f, const char *sectname)
{
	struct rcsection *p;
	int count = 0;

	p = rc_findsect(f, sectname, 0);
	if (p) {
		while (p != NULL) {
			count = p->rs_id + 1;
			p = rc_findsect(f, sectname, count);
		}
		return (count);
	} else
		return (0);
}

char **
rc_getkeys(struct rcfile *rcp, const char *sectname, int sect_id)
{
	struct rcsection *rsp;
	struct rckey *p;
	char **names_tbl;
	int i = 0, count = 0;

	rsp = rc_findsect(rcp, sectname, sect_id);
	if (rsp == NULL)
		return (NULL);

	SLIST_FOREACH(p, &rsp->rs_keys, rk_next)
		count++;

	names_tbl = malloc(sizeof(char *) * (count + 1));
	if (names_tbl == NULL)
		return (NULL);

	SLIST_FOREACH(p, &rsp->rs_keys, rk_next)
		names_tbl[i++] = p->rk_name;

	names_tbl[i] = NULL;
	return (names_tbl);
}

