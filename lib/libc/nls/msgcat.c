/***********************************************************
Copyright 1990, by Alfalfa Software Incorporated, Cambridge, Massachusetts.
Copyright 2010, Gabor Kovesdan <gabor@FreeBSD.org>

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that Alfalfa's name not be used in
advertising or publicity pertaining to distribution of the software
without specific, written prior permission.

ALPHALPHA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
ALPHALPHA BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

If you make any modifications, bugfixes or other changes to this software
we'd appreciate it if you could send a copy to us so we can keep things
up-to-date.  Many thanks.
				Kee Hinckley
				Alfalfa Software, Inc.
				267 Allston St., #3
				Cambridge, MA 02139  USA
				nazgul@alfalfa.com

******************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define _NLS_PRIVATE

#include "namespace.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/queue.h>

#include <arpa/inet.h>		/* for ntohl() */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <nl_types.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"

#include "../locale/xlocale_private.h"

#define _DEFAULT_NLS_PATH "/usr/share/nls/%L/%N.cat:/usr/share/nls/%N/%L:/usr/local/share/nls/%L/%N.cat:/usr/local/share/nls/%N/%L"

#define RLOCK(fail)	{ int ret;						\
			  if (__isthreaded &&					\
			      ((ret = _pthread_rwlock_rdlock(&rwlock)) != 0)) {	\
				  errno = ret;					\
				  return (fail);				\
			  }}
#define WLOCK(fail)	{ int ret;						\
			  if (__isthreaded &&					\
			      ((ret = _pthread_rwlock_wrlock(&rwlock)) != 0)) {	\
				  errno = ret;					\
				  return (fail);				\
			  }}
#define UNLOCK		{ if (__isthreaded)					\
			      _pthread_rwlock_unlock(&rwlock); }

#define	NLERR		((nl_catd) -1)
#define NLRETERR(errc)  { errno = errc; return (NLERR); }
#define SAVEFAIL(n, l, e)	{ WLOCK(NLERR);					\
				  np = malloc(sizeof(struct catentry));		\
				  if (np != NULL) {				\
				  	np->name = strdup(n);			\
					np->path = NULL;			\
					np->catd = NLERR;			\
					np->refcount = 0;			\
					np->lang = (l == NULL) ? NULL :		\
					    strdup(l);				\
					np->caterrno = e;			\
				  	SLIST_INSERT_HEAD(&cache, np, list);	\
				  }						\
				  UNLOCK;					\
				  errno = e;					\
				}

static nl_catd load_msgcat(const char *, const char *, const char *);

static pthread_rwlock_t		 rwlock = PTHREAD_RWLOCK_INITIALIZER;

struct catentry {
	SLIST_ENTRY(catentry)	 list;
	char			*name;
	char			*path;
	int			 caterrno;
	nl_catd			 catd;
	char			*lang;
	int			 refcount;
};

SLIST_HEAD(listhead, catentry) cache =
    SLIST_HEAD_INITIALIZER(cache);

nl_catd
catopen(const char *name, int type)
{
	struct stat sbuf;
	struct catentry *np;
	char *base, *cptr, *cptr1, *nlspath, *pathP, *pcode;
	char *plang, *pter;
	int saverr, spcleft;
	const char *lang, *tmpptr;
	char path[PATH_MAX];

	/* sanity checking */
	if (name == NULL || *name == '\0')
		NLRETERR(EINVAL);

	if (strchr(name, '/') != NULL)
		/* have a pathname */
		lang = NULL;
	else {
		if (type == NL_CAT_LOCALE)
			lang = querylocale(LC_MESSAGES_MASK, __get_locale());
		else
			lang = getenv("LANG");

		if (lang == NULL || *lang == '\0' || strlen(lang) > ENCODING_LEN ||
		    (lang[0] == '.' &&
		    (lang[1] == '\0' || (lang[1] == '.' && lang[2] == '\0'))) ||
		    strchr(lang, '/') != NULL)
			lang = "C";
	}

	/* Try to get it from the cache first */
	RLOCK(NLERR);
	SLIST_FOREACH(np, &cache, list) {
		if ((strcmp(np->name, name) == 0) &&
		    ((lang != NULL && np->lang != NULL &&
		    strcmp(np->lang, lang) == 0) || (np->lang == lang))) {
			if (np->caterrno != 0) {
				/* Found cached failing entry */
				UNLOCK;
				NLRETERR(np->caterrno);
			} else {
				/* Found cached successful entry */
				np->refcount++;
				UNLOCK;
				return (np->catd);
			}
		}
	}
	UNLOCK;

	/* is it absolute path ? if yes, load immediately */
	if (strchr(name, '/') != NULL)
		return (load_msgcat(name, name, lang));

	/* sanity checking */
	if ((plang = cptr1 = strdup(lang)) == NULL)
		return (NLERR);
	if ((cptr = strchr(cptr1, '@')) != NULL)
		*cptr = '\0';
	pter = pcode = "";
	if ((cptr = strchr(cptr1, '_')) != NULL) {
		*cptr++ = '\0';
		pter = cptr1 = cptr;
	}
	if ((cptr = strchr(cptr1, '.')) != NULL) {
		*cptr++ = '\0';
		pcode = cptr;
	}

	if ((nlspath = getenv("NLSPATH")) == NULL || issetugid())
		nlspath = _DEFAULT_NLS_PATH;

	if ((base = cptr = strdup(nlspath)) == NULL) {
		saverr = errno;
		free(plang);
		errno = saverr;
		return (NLERR);
	}

	while ((nlspath = strsep(&cptr, ":")) != NULL) {
		pathP = path;
		if (*nlspath) {
			for (; *nlspath; ++nlspath) {
				if (*nlspath == '%') {
					switch (*(nlspath + 1)) {
					case 'l':
						tmpptr = plang;
						break;
					case 't':
						tmpptr = pter;
						break;
					case 'c':
						tmpptr = pcode;
						break;
					case 'L':
						tmpptr = lang;
						break;
					case 'N':
						tmpptr = (char *)name;
						break;
					case '%':
						++nlspath;
						/* FALLTHROUGH */
					default:
						if (pathP - path >=
						    sizeof(path) - 1)
							goto too_long;
						*(pathP++) = *nlspath;
						continue;
					}
					++nlspath;
			put_tmpptr:
					spcleft = sizeof(path) -
						  (pathP - path) - 1;
					if (strlcpy(pathP, tmpptr, spcleft) >=
					    spcleft) {
			too_long:
						free(plang);
						free(base);
						SAVEFAIL(name, lang, ENAMETOOLONG);
						NLRETERR(ENAMETOOLONG);
					}
					pathP += strlen(tmpptr);
				} else {
					if (pathP - path >= sizeof(path) - 1)
						goto too_long;
					*(pathP++) = *nlspath;
				}
			}
			*pathP = '\0';
			if (stat(path, &sbuf) == 0) {
				free(plang);
				free(base);
				return (load_msgcat(path, name, lang));
			}
		} else {
			tmpptr = (char *)name;
			--nlspath;
			goto put_tmpptr;
		}
	}
	free(plang);
	free(base);
	SAVEFAIL(name, lang, ENOENT);
	NLRETERR(ENOENT);
}

char *
catgets(nl_catd catd, int set_id, int msg_id, const char *s)
{
	struct _nls_cat_hdr *cat_hdr;
	struct _nls_msg_hdr *msg_hdr;
	struct _nls_set_hdr *set_hdr;
	int i, l, r, u;

	if (catd == NULL || catd == NLERR) {
		errno = EBADF;
		/* LINTED interface problem */
		return ((char *)s);
	}

	cat_hdr = (struct _nls_cat_hdr *)catd->__data;
	set_hdr = (struct _nls_set_hdr *)(void *)((char *)catd->__data +
	    sizeof(struct _nls_cat_hdr));

	/* binary search, see knuth algorithm b */
	l = 0;
	u = ntohl((u_int32_t)cat_hdr->__nsets) - 1;
	while (l <= u) {
		i = (l + u) / 2;
		r = set_id - ntohl((u_int32_t)set_hdr[i].__setno);

		if (r == 0) {
			msg_hdr = (struct _nls_msg_hdr *)
			    (void *)((char *)catd->__data +
			    sizeof(struct _nls_cat_hdr) +
			    ntohl((u_int32_t)cat_hdr->__msg_hdr_offset));

			l = ntohl((u_int32_t)set_hdr[i].__index);
			u = l + ntohl((u_int32_t)set_hdr[i].__nmsgs) - 1;
			while (l <= u) {
				i = (l + u) / 2;
				r = msg_id -
				    ntohl((u_int32_t)msg_hdr[i].__msgno);
				if (r == 0) {
					return ((char *) catd->__data +
					    sizeof(struct _nls_cat_hdr) +
					    ntohl((u_int32_t)
					    cat_hdr->__msg_txt_offset) +
					    ntohl((u_int32_t)
					    msg_hdr[i].__offset));
				} else if (r < 0) {
					u = i - 1;
				} else {
					l = i + 1;
				}
			}

			/* not found */
			goto notfound;

		} else if (r < 0) {
			u = i - 1;
		} else {
			l = i + 1;
		}
	}

notfound:
	/* not found */
	errno = ENOMSG;
	/* LINTED interface problem */
	return ((char *)s);
}

static void
catfree(struct catentry *np)
{

	if (np->catd != NULL && np->catd != NLERR) {
		munmap(np->catd->__data, (size_t)np->catd->__size);
		free(np->catd);
	}
	SLIST_REMOVE(&cache, np, catentry, list);
	free(np->name);
	free(np->path);
	free(np->lang);
	free(np);
}

int
catclose(nl_catd catd)
{
	struct catentry *np;

	/* sanity checking */
	if (catd == NULL || catd == NLERR) {
		errno = EBADF;
		return (-1);
	}

	/* Remove from cache if not referenced any more */
	WLOCK(-1);
	SLIST_FOREACH(np, &cache, list) {
		if (catd == np->catd) {
			np->refcount--;
			if (np->refcount == 0)
				catfree(np);
			break;
		}
	}
	UNLOCK;
	return (0);
}

/*
 * Internal support functions
 */

static nl_catd
load_msgcat(const char *path, const char *name, const char *lang)
{
	struct stat st;
	nl_catd	catd;
	struct catentry *np;
	void *data;
	int fd;

	/* path/name will never be NULL here */

	/*
	 * One more try in cache; if it was not found by name,
	 * it might still be found by absolute path.
	 */
	RLOCK(NLERR);
	SLIST_FOREACH(np, &cache, list) {
		if ((np->path != NULL) && (strcmp(np->path, path) == 0)) {
			np->refcount++;
			UNLOCK;
			return (np->catd);
		}
	}
	UNLOCK;

	if ((fd = _open(path, O_RDONLY | O_CLOEXEC)) == -1) {
		SAVEFAIL(name, lang, errno);
		NLRETERR(errno);
	}

	if (_fstat(fd, &st) != 0) {
		_close(fd);
		SAVEFAIL(name, lang, EFTYPE);
		NLRETERR(EFTYPE);
	}

	/*
	 * If the file size cannot be held in size_t we cannot mmap()
	 * it to the memory.  Probably, this will not be a problem given
	 * that catalog files are usually small.
	 */
	if (st.st_size > SIZE_T_MAX) {
		_close(fd);
		SAVEFAIL(name, lang, EFBIG);
		NLRETERR(EFBIG);
	}

	if ((data = mmap(0, (size_t)st.st_size, PROT_READ,
	    MAP_FILE|MAP_SHARED, fd, (off_t)0)) == MAP_FAILED) {
		int saved_errno = errno;
		_close(fd);
		SAVEFAIL(name, lang, saved_errno);
		NLRETERR(saved_errno);
	}
	_close(fd);

	if (ntohl((u_int32_t)((struct _nls_cat_hdr *)data)->__magic) !=
	    _NLS_MAGIC) {
		munmap(data, (size_t)st.st_size);
		SAVEFAIL(name, lang, EFTYPE);
		NLRETERR(EFTYPE);
	}

	if ((catd = malloc(sizeof (*catd))) == NULL) {
		munmap(data, (size_t)st.st_size);
		SAVEFAIL(name, lang, ENOMEM);
		NLRETERR(ENOMEM);
	}

	catd->__data = data;
	catd->__size = (int)st.st_size;

	/* Caching opened catalog */
	WLOCK(NLERR);
	if ((np = malloc(sizeof(struct catentry))) != NULL) {
		np->name = strdup(name);
		np->path = strdup(path);
		np->catd = catd;
		np->lang = (lang == NULL) ? NULL : strdup(lang);
		np->refcount = 1;
		np->caterrno = 0;
		SLIST_INSERT_HEAD(&cache, np, list);
	}
	UNLOCK;
	return (catd);
}
