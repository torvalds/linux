/*	$NetBSD: nsdispatch.c,v 1.9 1999/01/25 00:16:17 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997, 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * Jacques A. Vidrine, Safeport Network Services, and Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/stat.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#define _NS_PRIVATE
#include <nsswitch.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "un-namespace.h"
#include "nss_tls.h"
#include "libc_private.h"
#ifdef NS_CACHING
#include "nscache.h"
#endif

enum _nss_constants {
	/* Number of elements allocated when we grow a vector */
	ELEMSPERCHUNK =	8
};

/*
 * Global NSS data structures are mostly read-only, but we update
 * them when we read or re-read the nsswitch.conf.
 */
static	pthread_rwlock_t	nss_lock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * Runtime determination of whether we are dynamically linked or not.
 */
extern	int		_DYNAMIC __attribute__ ((weak));
#define	is_dynamic()	(&_DYNAMIC != NULL)

/*
 * default sourcelist: `files'
 */
const ns_src __nsdefaultsrc[] = {
	{ NSSRC_FILES, NS_SUCCESS },
	{ 0 },
};

/* Database, source mappings. */
static	unsigned int		 _nsmapsize;
static	ns_dbt			*_nsmap = NULL;

/* NSS modules. */
static	unsigned int		 _nsmodsize;
static	ns_mod			*_nsmod;

/* Placeholder for builtin modules' dlopen `handle'. */
static	int			 __nss_builtin_handle;
static	void			*nss_builtin_handle = &__nss_builtin_handle;

#ifdef NS_CACHING
/*
 * Cache lookup cycle prevention function - if !NULL then no cache lookups
 * will be made
 */
static	void			*nss_cache_cycle_prevention_func = NULL;
#endif

/*
 * We keep track of nsdispatch() nesting depth in dispatch_depth.  When a
 * fallback method is invoked from nsdispatch(), we temporarily set
 * fallback_depth to the current dispatch depth plus one.  Subsequent
 * calls at that exact depth will run in fallback mode (restricted to the
 * same source as the call that was handled by the fallback method), while
 * calls below that depth will be handled normally, allowing fallback
 * methods to perform arbitrary lookups.
 */
struct fb_state {
	int	dispatch_depth;
	int	fallback_depth;
};
static	void	fb_endstate(void *);
NSS_TLS_HANDLING(fb);

/*
 * Attempt to spew relatively uniform messages to syslog.
 */
#define nss_log(level, fmt, ...) \
	syslog((level), "NSSWITCH(%s): " fmt, __func__, __VA_ARGS__)
#define nss_log_simple(level, s) \
	syslog((level), "NSSWITCH(%s): " s, __func__)

/*
 * Dynamically growable arrays are used for lists of databases, sources,
 * and modules.  The following `vector' interface is used to isolate the
 * common operations.
 */
typedef	int	(*vector_comparison)(const void *, const void *);
typedef	void	(*vector_free_elem)(void *);
static	void	  vector_sort(void *, unsigned int, size_t,
		    vector_comparison);
static	void	  vector_free(void *, unsigned int *, size_t,
		    vector_free_elem);
static	void	 *vector_ref(unsigned int, void *, unsigned int, size_t);
static	void	 *vector_search(const void *, void *, unsigned int, size_t,
		    vector_comparison);
static	void	 *vector_append(const void *, void *, unsigned int *, size_t);


/*
 * Internal interfaces.
 */
static	int	 string_compare(const void *, const void *);
static	int	 mtab_compare(const void *, const void *);
static	int	 nss_configure(void);
static	void	 ns_dbt_free(ns_dbt *);
static	void	 ns_mod_free(ns_mod *);
static	void	 ns_src_free(ns_src **, int);
static	void	 nss_load_builtin_modules(void);
static	void	 nss_load_module(const char *, nss_module_register_fn);
static	void	 nss_atexit(void);
/* nsparser */
extern	FILE	*_nsyyin;


/*
 * The vector operations
 */
static void
vector_sort(void *vec, unsigned int count, size_t esize,
    vector_comparison comparison)
{
	qsort(vec, count, esize, comparison);
}


static void *
vector_search(const void *key, void *vec, unsigned int count, size_t esize,
    vector_comparison comparison)
{
	return (bsearch(key, vec, count, esize, comparison));
}


static void *
vector_append(const void *elem, void *vec, unsigned int *count, size_t esize)
{
	void	*p;

	if ((*count % ELEMSPERCHUNK) == 0) {
		p = reallocarray(vec, *count + ELEMSPERCHUNK, esize);
		if (p == NULL) {
			nss_log_simple(LOG_ERR, "memory allocation failure");
			return (vec);
		}
		vec = p;
	}
	memmove((void *)(((uintptr_t)vec) + (*count * esize)), elem, esize);
	(*count)++;
	return (vec);
}


static void *
vector_ref(unsigned int i, void *vec, unsigned int count, size_t esize)
{
	if (i < count)
		return (void *)((uintptr_t)vec + (i * esize));
	else
		return (NULL);
}


#define VECTOR_FREE(v, c, s, f) \
	do { vector_free(v, c, s, f); v = NULL; } while (0)
static void
vector_free(void *vec, unsigned int *count, size_t esize,
    vector_free_elem free_elem)
{
	unsigned int	 i;
	void		*elem;

	for (i = 0; i < *count; i++) {
		elem = vector_ref(i, vec, *count, esize);
		if (elem != NULL)
			free_elem(elem);
	}
	free(vec);
	*count = 0;
}

/*
 * Comparison functions for vector_search.
 */
static int
string_compare(const void *a, const void *b)
{
      return (strcasecmp(*(const char * const *)a, *(const char * const *)b));
}


static int
mtab_compare(const void *a, const void *b)
{
      int     cmp;

      cmp = strcmp(((const ns_mtab *)a)->name, ((const ns_mtab *)b)->name);
      if (cmp != 0)
	      return (cmp);
      else
	      return (strcmp(((const ns_mtab *)a)->database,
		  ((const ns_mtab *)b)->database));
}

/*
 * NSS nsmap management.
 */
void
_nsdbtaddsrc(ns_dbt *dbt, const ns_src *src)
{
	const ns_mod	*modp;

	dbt->srclist = vector_append(src, dbt->srclist, &dbt->srclistsize,
	    sizeof(*src));
	modp = vector_search(&src->name, _nsmod, _nsmodsize, sizeof(*_nsmod),
	    string_compare);
	if (modp == NULL)
		nss_load_module(src->name, NULL);
}


#ifdef _NSS_DEBUG
void
_nsdbtdump(const ns_dbt *dbt)
{
	int i;

	printf("%s (%d source%s):", dbt->name, dbt->srclistsize,
	    dbt->srclistsize == 1 ? "" : "s");
	for (i = 0; i < (int)dbt->srclistsize; i++) {
		printf(" %s", dbt->srclist[i].name);
		if (!(dbt->srclist[i].flags &
		    (NS_UNAVAIL|NS_NOTFOUND|NS_TRYAGAIN)) &&
		    (dbt->srclist[i].flags & NS_SUCCESS))
			continue;
		printf(" [");
		if (!(dbt->srclist[i].flags & NS_SUCCESS))
			printf(" SUCCESS=continue");
		if (dbt->srclist[i].flags & NS_UNAVAIL)
			printf(" UNAVAIL=return");
		if (dbt->srclist[i].flags & NS_NOTFOUND)
			printf(" NOTFOUND=return");
		if (dbt->srclist[i].flags & NS_TRYAGAIN)
			printf(" TRYAGAIN=return");
		printf(" ]");
	}
	printf("\n");
}
#endif


/*
 * The first time nsdispatch is called (during a process's lifetime,
 * or after nsswitch.conf has been updated), nss_configure will
 * prepare global data needed by NSS.
 */
static int
nss_configure(void)
{
	static time_t	 confmod;
	static int	 already_initialized = 0;
	struct stat	 statbuf;
	int		 result, isthreaded;
	const char	*path;
#ifdef NS_CACHING
	void		*handle;
#endif

	result = 0;
	isthreaded = __isthreaded;
#if defined(_NSS_DEBUG) && defined(_NSS_SHOOT_FOOT)
	/* NOTE WELL:  THIS IS A SECURITY HOLE. This must only be built
	 * for debugging purposes and MUST NEVER be used in production.
	 */
	path = getenv("NSSWITCH_CONF");
	if (path == NULL)
#endif
		path = _PATH_NS_CONF;
#ifndef NS_REREAD_CONF
	/*
	 * Define NS_REREAD_CONF to have nsswitch notice changes
	 * to nsswitch.conf(5) during runtime.  This involves calling
	 * stat(2) every time, which can result in performance hit.
	 */
	if (already_initialized)
		return (0);
	already_initialized = 1;
#endif /* NS_REREAD_CONF */
	if (stat(path, &statbuf) != 0)
		return (0);
	if (statbuf.st_mtime <= confmod)
		return (0);
	if (isthreaded) {
		(void)_pthread_rwlock_unlock(&nss_lock);
		result = _pthread_rwlock_wrlock(&nss_lock);
		if (result != 0)
			return (result);
		if (stat(path, &statbuf) != 0)
			goto fin;
		if (statbuf.st_mtime <= confmod)
			goto fin;
	}
	_nsyyin = fopen(path, "re");
	if (_nsyyin == NULL)
		goto fin;
	VECTOR_FREE(_nsmap, &_nsmapsize, sizeof(*_nsmap),
	    (vector_free_elem)ns_dbt_free);
	VECTOR_FREE(_nsmod, &_nsmodsize, sizeof(*_nsmod),
	    (vector_free_elem)ns_mod_free);
	if (confmod == 0)
		(void)atexit(nss_atexit);
	nss_load_builtin_modules();
	_nsyyparse();
	(void)fclose(_nsyyin);
	vector_sort(_nsmap, _nsmapsize, sizeof(*_nsmap), string_compare);
	confmod = statbuf.st_mtime;

#ifdef NS_CACHING
	handle = libc_dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);
	if (handle != NULL) {
		nss_cache_cycle_prevention_func = dlsym(handle,
		    "_nss_cache_cycle_prevention_function");
		dlclose(handle);
	}
#endif
fin:
	if (isthreaded) {
		(void)_pthread_rwlock_unlock(&nss_lock);
		if (result == 0)
			result = _pthread_rwlock_rdlock(&nss_lock);
	}
	return (result);
}


void
_nsdbtput(const ns_dbt *dbt)
{
	unsigned int	 i;
	ns_dbt		*p;

	for (i = 0; i < _nsmapsize; i++) {
		p = vector_ref(i, _nsmap, _nsmapsize, sizeof(*_nsmap));
		if (string_compare(&dbt->name, &p->name) == 0) {
			/* overwrite existing entry */
			if (p->srclist != NULL)
				ns_src_free(&p->srclist, p->srclistsize);
			memmove(p, dbt, sizeof(*dbt));
			return;
		}
	}
	_nsmap = vector_append(dbt, _nsmap, &_nsmapsize, sizeof(*_nsmap));
}


static void
ns_dbt_free(ns_dbt *dbt)
{
	ns_src_free(&dbt->srclist, dbt->srclistsize);
	if (dbt->name)
		free((void *)dbt->name);
}


static void
ns_src_free(ns_src **src, int srclistsize)
{
	int	i;

	for (i = 0; i < srclistsize; i++)
		if ((*src)[i].name != NULL)
			/* This one was allocated by nslexer. You'll just
			 * have to trust me.
			 */
			free((void *)((*src)[i].name));
	free(*src);
	*src = NULL;
}



/*
 * NSS module management.
 */
/* The built-in NSS modules are all loaded at once. */
#define NSS_BACKEND(name, reg) \
ns_mtab	*reg(unsigned int *, nss_module_unregister_fn *);
#include "nss_backends.h"
#undef NSS_BACKEND

static void
nss_load_builtin_modules(void)
{
#define NSS_BACKEND(name, reg) nss_load_module(#name, reg);
#include "nss_backends.h"
#undef NSS_BACKEND
}


/* Load a built-in or dynamically linked module.  If the `reg_fn'
 * argument is non-NULL, assume a built-in module and use reg_fn to
 * register it.  Otherwise, search for a dynamic NSS module.
 */
static void
nss_load_module(const char *source, nss_module_register_fn reg_fn)
{
	char		 buf[PATH_MAX];
	ns_mod		 mod;
	nss_module_register_fn fn;

	memset(&mod, 0, sizeof(mod));
	mod.name = strdup(source);
	if (mod.name == NULL) {
		nss_log_simple(LOG_ERR, "memory allocation failure");
		return;
	}
	if (reg_fn != NULL) {
		/* The placeholder is required, as a NULL handle
		 * represents an invalid module.
		 */
		mod.handle = nss_builtin_handle;
		fn = reg_fn;
	} else if (!is_dynamic()) {
		goto fin;
	} else if (strcmp(source, NSSRC_CACHE) == 0 ||
	    strcmp(source, NSSRC_COMPAT) == 0 ||
	    strcmp(source, NSSRC_DB) == 0 ||
	    strcmp(source, NSSRC_DNS) == 0 ||
	    strcmp(source, NSSRC_FILES) == 0 ||
	    strcmp(source, NSSRC_NIS) == 0) {
		/*
		 * Avoid calling dlopen(3) for built-in modules.
		 */
		goto fin;
	} else {
		if (snprintf(buf, sizeof(buf), "nss_%s.so.%d", mod.name,
		    NSS_MODULE_INTERFACE_VERSION) >= (int)sizeof(buf))
			goto fin;
		mod.handle = libc_dlopen(buf, RTLD_LOCAL|RTLD_LAZY);
		if (mod.handle == NULL) {
#ifdef _NSS_DEBUG
			/* This gets pretty annoying since the built-in
			 * sources aren't modules yet.
			 */
			nss_log(LOG_DEBUG, "%s, %s", mod.name, dlerror());
#endif
			goto fin;
		}
		fn = (nss_module_register_fn)dlfunc(mod.handle,
		    "nss_module_register");
		if (fn == NULL) {
			(void)dlclose(mod.handle);
			mod.handle = NULL;
			nss_log(LOG_ERR, "%s, %s", mod.name, dlerror());
			goto fin;
		}
	}
	mod.mtab = fn(mod.name, &mod.mtabsize, &mod.unregister);
	if (mod.mtab == NULL || mod.mtabsize == 0) {
		if (mod.handle != nss_builtin_handle)
			(void)dlclose(mod.handle);
		mod.handle = NULL;
		nss_log(LOG_ERR, "%s, registration failed", mod.name);
		goto fin;
	}
	if (mod.mtabsize > 1)
		qsort(mod.mtab, mod.mtabsize, sizeof(mod.mtab[0]),
		    mtab_compare);
fin:
	_nsmod = vector_append(&mod, _nsmod, &_nsmodsize, sizeof(*_nsmod));
	vector_sort(_nsmod, _nsmodsize, sizeof(*_nsmod), string_compare);
}

static int exiting = 0;

static void
ns_mod_free(ns_mod *mod)
{

	free(mod->name);
	if (mod->handle == NULL)
		return;
	if (mod->unregister != NULL)
		mod->unregister(mod->mtab, mod->mtabsize);
	if (mod->handle != nss_builtin_handle && !exiting)
		(void)dlclose(mod->handle);
}

/*
 * Cleanup
 */
static void
nss_atexit(void)
{
	int isthreaded;

	exiting = 1;
	isthreaded = __isthreaded;
	if (isthreaded)
		(void)_pthread_rwlock_wrlock(&nss_lock);
	VECTOR_FREE(_nsmap, &_nsmapsize, sizeof(*_nsmap),
	    (vector_free_elem)ns_dbt_free);
	VECTOR_FREE(_nsmod, &_nsmodsize, sizeof(*_nsmod),
	    (vector_free_elem)ns_mod_free);
	if (isthreaded)
		(void)_pthread_rwlock_unlock(&nss_lock);
}

/*
 * Finally, the actual implementation.
 */
static nss_method
nss_method_lookup(const char *source, const char *database,
    const char *method, const ns_dtab disp_tab[], void **mdata)
{
	ns_mod	*mod;
	ns_mtab	*match, key;
	int	 i;

	if (disp_tab != NULL)
		for (i = 0; disp_tab[i].src != NULL; i++)
			if (strcasecmp(source, disp_tab[i].src) == 0) {
				*mdata = disp_tab[i].mdata;
				return (disp_tab[i].method);
			}
	mod = vector_search(&source, _nsmod, _nsmodsize, sizeof(*_nsmod),
	    string_compare);
	if (mod != NULL && mod->handle != NULL) {
		key.database = database;
		key.name = method;
		match = bsearch(&key, mod->mtab, mod->mtabsize,
		    sizeof(mod->mtab[0]), mtab_compare);
		if (match != NULL) {
			*mdata = match->mdata;
			return (match->method);
		}
	}

	*mdata = NULL;
	return (NULL);
}

static void
fb_endstate(void *p)
{
	free(p);
}

__weak_reference(_nsdispatch, nsdispatch);

int
_nsdispatch(void *retval, const ns_dtab disp_tab[], const char *database,
	    const char *method_name, const ns_src defaults[], ...)
{
	va_list		 ap;
	const ns_dbt	*dbt;
	const ns_src	*srclist;
	nss_method	 method, fb_method;
	void		*mdata;
	int		 isthreaded, serrno, i, result, srclistsize;
	struct fb_state	*st;
	int		 saved_depth;

#ifdef NS_CACHING
	nss_cache_data	 cache_data;
	nss_cache_data	*cache_data_p;
	int		 cache_flag;
#endif
	
	dbt = NULL;
	fb_method = NULL;

	isthreaded = __isthreaded;
	serrno = errno;
	if (isthreaded) {
		result = _pthread_rwlock_rdlock(&nss_lock);
		if (result != 0) {
			result = NS_UNAVAIL;
			goto fin;
		}
	}

	result = fb_getstate(&st);
	if (result != 0) {
		result = NS_UNAVAIL;
		goto fin;
	}

	result = nss_configure();
	if (result != 0) {
		result = NS_UNAVAIL;
		goto fin;
	}
	++st->dispatch_depth;
	if (st->dispatch_depth > st->fallback_depth) {
		dbt = vector_search(&database, _nsmap, _nsmapsize, sizeof(*_nsmap),
		    string_compare);
		fb_method = nss_method_lookup(NSSRC_FALLBACK, database,
		    method_name, disp_tab, &mdata);
	}

	if (dbt != NULL) {
		srclist = dbt->srclist;
		srclistsize = dbt->srclistsize;
	} else {
		srclist = defaults;
		srclistsize = 0;
		while (srclist[srclistsize].name != NULL)
			srclistsize++;
	}

#ifdef NS_CACHING
	cache_data_p = NULL;
	cache_flag = 0;
#endif
	for (i = 0; i < srclistsize; i++) {
		result = NS_NOTFOUND;
		method = nss_method_lookup(srclist[i].name, database,
		    method_name, disp_tab, &mdata);

		if (method != NULL) {
#ifdef NS_CACHING
			if (strcmp(srclist[i].name, NSSRC_CACHE) == 0 &&
			    nss_cache_cycle_prevention_func == NULL) {
#ifdef NS_STRICT_LIBC_EID_CHECKING
				if (issetugid() != 0)
					continue;
#endif
				cache_flag = 1;

				memset(&cache_data, 0, sizeof(nss_cache_data));
				cache_data.info = (nss_cache_info const *)mdata;
				cache_data_p = &cache_data;

				va_start(ap, defaults);
				if (cache_data.info->id_func != NULL)
					result = __nss_common_cache_read(retval,
					    cache_data_p, ap);
				else if (cache_data.info->marshal_func != NULL)
					result = __nss_mp_cache_read(retval,
					    cache_data_p, ap);
				else
					result = __nss_mp_cache_end(retval,
					    cache_data_p, ap);
				va_end(ap);
			} else {
				cache_flag = 0;
				errno = 0;
				va_start(ap, defaults);
				result = method(retval, mdata, ap);
				va_end(ap);
			}
#else /* NS_CACHING */
			errno = 0;
			va_start(ap, defaults);
			result = method(retval, mdata, ap);
			va_end(ap);
#endif /* NS_CACHING */

			if (result & (srclist[i].flags))
				break;
		} else {
			if (fb_method != NULL) {
				saved_depth = st->fallback_depth;
				st->fallback_depth = st->dispatch_depth + 1;
				va_start(ap, defaults);
				result = fb_method(retval,
				    (void *)srclist[i].name, ap);
				va_end(ap);
				st->fallback_depth = saved_depth;
			} else
				nss_log(LOG_DEBUG, "%s, %s, %s, not found, "
				    "and no fallback provided",
				    srclist[i].name, database, method_name);
		}
	}

#ifdef NS_CACHING
	if (cache_data_p != NULL &&
	    (result & (NS_NOTFOUND | NS_SUCCESS)) && cache_flag == 0) {
		va_start(ap, defaults);
		if (result == NS_SUCCESS) {
			if (cache_data.info->id_func != NULL)
				__nss_common_cache_write(retval, cache_data_p,
				    ap);
			else if (cache_data.info->marshal_func != NULL)
				__nss_mp_cache_write(retval, cache_data_p, ap);
		} else if (result == NS_NOTFOUND) {
			if (cache_data.info->id_func == NULL) {
				if (cache_data.info->marshal_func != NULL)
					__nss_mp_cache_write_submit(retval,
					    cache_data_p, ap);
			} else
				__nss_common_cache_write_negative(cache_data_p);
		}
		va_end(ap);
	}
#endif /* NS_CACHING */

	if (isthreaded)
		(void)_pthread_rwlock_unlock(&nss_lock);
	--st->dispatch_depth;
fin:
	errno = serrno;
	return (result);
}
