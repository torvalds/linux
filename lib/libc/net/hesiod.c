/*	$NetBSD: hesiod.c,v 1.9 1999/02/11 06:16:38 simonb Exp $	*/

/* Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/* Copyright 1996 by the Massachusetts Institute of Technology.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */

/* This file is part of the hesiod library.  It implements the core
 * portion of the hesiod resolver.
 *
 * This file is loosely based on an interim version of hesiod.c from
 * the BIND IRS library, which was in turn based on an earlier version
 * of this file.  Extensive changes have been made on each step of the
 * path.
 *
 * This implementation is not truly thread-safe at the moment because
 * it uses res_send() and accesses _res.
 */

#include <sys/cdefs.h>

#if 0
static char *orig_rcsid = "$NetBSD: hesiod.c,v 1.9 1999/02/11 06:16:38 simonb Exp $";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/nameser.h>

#include <ctype.h>
#include <errno.h>
#include <hesiod.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct hesiod_p {
	char	*lhs;			/* normally ".ns" */
	char	*rhs;			/* AKA the default hesiod domain */
	int	 classes[2];		/* The class search order. */
};

#define	MAX_HESRESP	1024

static int	  read_config_file(struct hesiod_p *, const char *);
static char	**get_txt_records(int, const char *);
static int	  init_context(void);
static void	  translate_errors(void);


/*
 * hesiod_init --
 *	initialize a hesiod_p.
 */
int 
hesiod_init(context)
	void	**context;
{
	struct hesiod_p	*ctx;
	const char	*p, *configname;

	ctx = malloc(sizeof(struct hesiod_p));
	if (ctx) {
		*context = ctx;
		if (!issetugid())
			configname = getenv("HESIOD_CONFIG");
		else
			configname = NULL;
		if (!configname)
			configname = _PATH_HESIOD_CONF;
		if (read_config_file(ctx, configname) >= 0) {
			/*
			 * The default rhs can be overridden by an
			 * environment variable.
			 */
			if (!issetugid())
				p = getenv("HES_DOMAIN");
			else
				p = NULL;
			if (p) {
				if (ctx->rhs)
					free(ctx->rhs);
				ctx->rhs = malloc(strlen(p) + 2);
				if (ctx->rhs) {
					*ctx->rhs = '.';
					strcpy(ctx->rhs + 1,
					    (*p == '.') ? p + 1 : p);
					return 0;
				} else
					errno = ENOMEM;
			} else
				return 0;
		}
	} else
		errno = ENOMEM;

	if (ctx->lhs)
		free(ctx->lhs);
	if (ctx->rhs)
		free(ctx->rhs);
	if (ctx)
		free(ctx);
	return -1;
}

/*
 * hesiod_end --
 *	Deallocates the hesiod_p.
 */
void 
hesiod_end(context)
	void	*context;
{
	struct hesiod_p *ctx = (struct hesiod_p *) context;

	free(ctx->rhs);
	if (ctx->lhs)
		free(ctx->lhs);
	free(ctx);
}

/*
 * hesiod_to_bind --
 * 	takes a hesiod (name, type) and returns a DNS
 *	name which is to be resolved.
 */
char *
hesiod_to_bind(void *context, const char *name, const char *type)
{
	struct hesiod_p *ctx = (struct hesiod_p *) context;
	char		 bindname[MAXDNAME], *p, *ret, **rhs_list = NULL;
	const char	*rhs;
	int		 len;

	if (strlcpy(bindname, name, sizeof(bindname)) >= sizeof(bindname)) {
		errno = EMSGSIZE;
		return NULL;
	}

		/*
		 * Find the right right hand side to use, possibly
		 * truncating bindname.
		 */
	p = strchr(bindname, '@');
	if (p) {
		*p++ = 0;
		if (strchr(p, '.'))
			rhs = name + (p - bindname);
		else {
			rhs_list = hesiod_resolve(context, p, "rhs-extension");
			if (rhs_list)
				rhs = *rhs_list;
			else {
				errno = ENOENT;
				return NULL;
			}
		}
	} else
		rhs = ctx->rhs;

		/* See if we have enough room. */
	len = strlen(bindname) + 1 + strlen(type);
	if (ctx->lhs)
		len += strlen(ctx->lhs) + ((ctx->lhs[0] != '.') ? 1 : 0);
	len += strlen(rhs) + ((rhs[0] != '.') ? 1 : 0);
	if (len > sizeof(bindname) - 1) {
		if (rhs_list)
			hesiod_free_list(context, rhs_list);
		errno = EMSGSIZE;
		return NULL;
	}
		/* Put together the rest of the domain. */
	strcat(bindname, ".");
	strcat(bindname, type);
		/* Only append lhs if it isn't empty. */
	if (ctx->lhs && ctx->lhs[0] != '\0' ) {
		if (ctx->lhs[0] != '.')
			strcat(bindname, ".");
		strcat(bindname, ctx->lhs);
	}
	if (rhs[0] != '.')
		strcat(bindname, ".");
	strcat(bindname, rhs);

		/* rhs_list is no longer needed, since we're done with rhs. */
	if (rhs_list)
		hesiod_free_list(context, rhs_list);

		/* Make a copy of the result and return it to the caller. */
	ret = strdup(bindname);
	if (!ret)
		errno = ENOMEM;
	return ret;
}

/*
 * hesiod_resolve --
 *	Given a hesiod name and type, return an array of strings returned
 *	by the resolver.
 */
char **
hesiod_resolve(context, name, type)
	void		*context;
	const char	*name;
	const char	*type;
{
	struct hesiod_p	*ctx = (struct hesiod_p *) context;
	char		*bindname, **retvec;

	bindname = hesiod_to_bind(context, name, type);
	if (!bindname)
		return NULL;

	retvec = get_txt_records(ctx->classes[0], bindname);
	if (retvec == NULL && errno == ENOENT && ctx->classes[1])
		retvec = get_txt_records(ctx->classes[1], bindname);

	free(bindname);
	return retvec;
}

/*ARGSUSED*/
void 
hesiod_free_list(context, list)
	void	 *context;
	char	**list;
{
	char  **p;

	if (list == NULL)
		return;
	for (p = list; *p; p++)
		free(*p);
	free(list);
}


/* read_config_file --
 *	Parse the /etc/hesiod.conf file.  Returns 0 on success,
 *	-1 on failure.  On failure, it might leave values in ctx->lhs
 *	or ctx->rhs which need to be freed by the caller.
 */
static int 
read_config_file(ctx, filename)
	struct hesiod_p	*ctx;
	const char	*filename;
{
	char	*key, *data, *p, **which;
	char	 buf[MAXDNAME + 7];
	int	 n;
	FILE	*fp;

		/* Set default query classes. */
	ctx->classes[0] = C_IN;
	ctx->classes[1] = C_HS;

		/* Try to open the configuration file. */
	fp = fopen(filename, "re");
	if (!fp) {
		/* Use compiled in default domain names. */
		ctx->lhs = strdup(DEF_LHS);
		ctx->rhs = strdup(DEF_RHS);
		if (ctx->lhs && ctx->rhs)
			return 0;
		else {
			errno = ENOMEM;
			return -1;
		}
	}
	ctx->lhs = NULL;
	ctx->rhs = NULL;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		p = buf;
		if (*p == '#' || *p == '\n' || *p == '\r')
			continue;
		while (*p == ' ' || *p == '\t')
			p++;
		key = p;
		while (*p != ' ' && *p != '\t' && *p != '=')
			p++;
		*p++ = 0;

		while (isspace(*p) || *p == '=')
			p++;
		data = p;
		while (!isspace(*p))
			p++;
		*p = 0;

		if (strcasecmp(key, "lhs") == 0 ||
		    strcasecmp(key, "rhs") == 0) {
			which = (strcasecmp(key, "lhs") == 0)
			    ? &ctx->lhs : &ctx->rhs;
			*which = strdup(data);
			if (!*which) {
				fclose(fp);
				errno = ENOMEM;
				return -1;
			}
		} else {
			if (strcasecmp(key, "classes") == 0) {
				n = 0;
				while (*data && n < 2) {
					p = data;
					while (*p && *p != ',')
						p++;
					if (*p)
						*p++ = 0;
					if (strcasecmp(data, "IN") == 0)
						ctx->classes[n++] = C_IN;
					else
						if (strcasecmp(data, "HS") == 0)
							ctx->classes[n++] =
							    C_HS;
					data = p;
				}
				while (n < 2)
					ctx->classes[n++] = 0;
			}
		}
	}
	fclose(fp);

	if (!ctx->rhs || ctx->classes[0] == 0 ||
	    ctx->classes[0] == ctx->classes[1]) {
		errno = ENOEXEC;
		return -1;
	}
	return 0;
}

/*
 * get_txt_records --
 *	Given a DNS class and a DNS name, do a lookup for TXT records, and
 *	return a list of them.
 */
static char **
get_txt_records(qclass, name)
	int		 qclass;
	const char	*name;
{
	HEADER		*hp;
	unsigned char	 qbuf[PACKETSZ], abuf[MAX_HESRESP], *p, *eom, *eor;
	char		*dst, **list;
	int		 ancount, qdcount, i, j, n, skip, type, class, len;

		/* Make sure the resolver is initialized. */
	if ((_res.options & RES_INIT) == 0 && res_init() == -1)
		return NULL;

		/* Construct the query. */
	n = res_mkquery(QUERY, name, qclass, T_TXT, NULL, 0,
	    NULL, qbuf, PACKETSZ);
	if (n < 0)
		return NULL;

		/* Send the query. */
	n = res_send(qbuf, n, abuf, MAX_HESRESP);
	if (n < 0 || n > MAX_HESRESP) {
		errno = ECONNREFUSED; /* XXX */
		return NULL;
	}
		/* Parse the header of the result. */
	hp = (HEADER *) (void *) abuf;
	ancount = ntohs(hp->ancount);
	qdcount = ntohs(hp->qdcount);
	p = abuf + sizeof(HEADER);
	eom = abuf + n;

		/*
		 * Skip questions, trying to get to the answer section
		 * which follows.
		 */
	for (i = 0; i < qdcount; i++) {
		skip = dn_skipname(p, eom);
		if (skip < 0 || p + skip + QFIXEDSZ > eom) {
			errno = EMSGSIZE;
			return NULL;
		}
		p += skip + QFIXEDSZ;
	}

		/* Allocate space for the text record answers. */
	list = malloc((ancount + 1) * sizeof(char *));
	if (!list) {
		errno = ENOMEM;
		return NULL;
	}
		/* Parse the answers. */
	j = 0;
	for (i = 0; i < ancount; i++) {
		/* Parse the header of this answer. */
		skip = dn_skipname(p, eom);
		if (skip < 0 || p + skip + 10 > eom)
			break;
		type = p[skip + 0] << 8 | p[skip + 1];
		class = p[skip + 2] << 8 | p[skip + 3];
		len = p[skip + 8] << 8 | p[skip + 9];
		p += skip + 10;
		if (p + len > eom) {
			errno = EMSGSIZE;
			break;
		}
		/* Skip entries of the wrong class and type. */
		if (class != qclass || type != T_TXT) {
			p += len;
			continue;
		}
		/* Allocate space for this answer. */
		list[j] = malloc((size_t)len);
		if (!list[j]) {
			errno = ENOMEM;
			break;
		}
		dst = list[j++];

		/* Copy answer data into the allocated area. */
		eor = p + len;
		while (p < eor) {
			n = (unsigned char) *p++;
			if (p + n > eor) {
				errno = EMSGSIZE;
				break;
			}
			memcpy(dst, p, (size_t)n);
			p += n;
			dst += n;
		}
		if (p < eor) {
			errno = EMSGSIZE;
			break;
		}
		*dst = 0;
	}

		/*
		 * If we didn't terminate the loop normally, something
		 * went wrong.
		 */
	if (i < ancount) {
		for (i = 0; i < j; i++)
			free(list[i]);
		free(list);
		return NULL;
	}
	if (j == 0) {
		errno = ENOENT;
		free(list);
		return NULL;
	}
	list[j] = NULL;
	return list;
}

		/*
		 *	COMPATIBILITY FUNCTIONS
		 */

static int	  inited = 0;
static void	 *context;
static int	  errval = HES_ER_UNINIT;

int
hes_init()
{
	init_context();
	return errval;
}

char *
hes_to_bind(name, type)
	const char	*name;
	const char	*type;
{
	static	char	*bindname;
	if (init_context() < 0)
		return NULL;
	if (bindname)
		free(bindname);
	bindname = hesiod_to_bind(context, name, type);
	if (!bindname)
		translate_errors();
	return bindname;
}

char **
hes_resolve(name, type)
	const char	*name;
	const char	*type;
{
	static char	**list;

	if (init_context() < 0)
		return NULL;

	/*
	 * In the old Hesiod interface, the caller was responsible for
	 * freeing the returned strings but not the vector of strings itself.
	 */
	if (list)
		free(list);

	list = hesiod_resolve(context, name, type);
	if (!list)
		translate_errors();
	return list;
}

int
hes_error()
{
	return errval;
}

void
hes_free(hp)
	char **hp;
{
	hesiod_free_list(context, hp);
}

static int
init_context()
{
	if (!inited) {
		inited = 1;
		if (hesiod_init(&context) < 0) {
			errval = HES_ER_CONFIG;
			return -1;
		}
		errval = HES_ER_OK;
	}
	return 0;
}

static void
translate_errors()
{
	switch (errno) {
	case ENOENT:
		errval = HES_ER_NOTFOUND;
		break;
	case ECONNREFUSED:
	case EMSGSIZE:
		errval = HES_ER_NET;
		break;
	case ENOMEM:
	default:
		/* Not a good match, but the best we can do. */
		errval = HES_ER_CONFIG;
		break;
	}
}
