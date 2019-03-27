/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *
 * $FreeBSD$
 */

#ifndef _DLFCN_H_
#define	_DLFCN_H_

#include <sys/_types.h>

/*
 * Modes and flags for dlopen().
 */
#define	RTLD_LAZY	1	/* Bind function calls lazily. */
#define	RTLD_NOW	2	/* Bind function calls immediately. */
#define	RTLD_MODEMASK	0x3
#define	RTLD_GLOBAL	0x100	/* Make symbols globally available. */
#define	RTLD_LOCAL	0	/* Opposite of RTLD_GLOBAL, and the default. */
#define	RTLD_TRACE	0x200	/* Trace loaded objects and exit. */
#define	RTLD_NODELETE	0x01000	/* Do not remove members. */
#define	RTLD_NOLOAD	0x02000	/* Do not load if not already loaded. */

/*
 * Request arguments for dlinfo().
 */
#define	RTLD_DI_LINKMAP		2	/* Obtain link map. */
#define	RTLD_DI_SERINFO		4	/* Obtain search path info. */
#define	RTLD_DI_SERINFOSIZE	5	/*  ... query for required space. */
#define	RTLD_DI_ORIGIN		6	/* Obtain object origin */
#define	RTLD_DI_MAX		RTLD_DI_ORIGIN

/*
 * Special handle arguments for dlsym()/dlinfo().
 */
#define	RTLD_NEXT	((void *) -1)	/* Search subsequent objects. */
#define	RTLD_DEFAULT	((void *) -2)	/* Use default search algorithm. */
#define	RTLD_SELF	((void *) -3)	/* Search the caller itself. */

#if __BSD_VISIBLE

#ifndef	_SIZE_T_DECLARED
typedef __size_t        size_t;
#define	_SIZE_T_DECLARED
#endif

/*
 * Structure filled in by dladdr().
 */
typedef	struct dl_info {
	const char	*dli_fname;	/* Pathname of shared object. */
	void		*dli_fbase;	/* Base address of shared object. */
	const char	*dli_sname;	/* Name of nearest symbol. */
	void		*dli_saddr;	/* Address of nearest symbol. */
} Dl_info;

/*-
 * The actual type declared by this typedef is immaterial, provided that
 * it is a function pointer.  Its purpose is to provide a return type for
 * dlfunc() which can be cast to a function pointer type without depending
 * on behavior undefined by the C standard, which might trigger a compiler
 * diagnostic.  We intentionally declare a unique type signature to force
 * a diagnostic should the application not cast the return value of dlfunc()
 * appropriately.
 */
struct __dlfunc_arg {
	int	__dlfunc_dummy;
};

typedef	void (*dlfunc_t)(struct __dlfunc_arg);

/*
 * Structures, returned by the RTLD_DI_SERINFO dlinfo() request.
 */
typedef struct dl_serpath {
	char *		dls_name;	/* single search path entry */
	unsigned int	dls_flags;	/* path information */
} Dl_serpath;

typedef struct  dl_serinfo {
        size_t		dls_size;       /* total buffer size */
        unsigned int	dls_cnt;        /* number of path entries */
        Dl_serpath	dls_serpath[1]; /* there may be more than one */
} Dl_serinfo;

#endif /* __BSD_VISIBLE */

__BEGIN_DECLS
/* XSI functions first. */
int	 dlclose(void *);
char	*dlerror(void);
void	*dlopen(const char *, int);
void	*dlsym(void * __restrict, const char * __restrict);

#if __BSD_VISIBLE
void	*fdlopen(int, int);
int	 dladdr(const void * __restrict, Dl_info * __restrict);
dlfunc_t dlfunc(void * __restrict, const char * __restrict);
int	 dlinfo(void * __restrict, int, void * __restrict);
void	 dllockinit(void *_context,
	    void *(*_lock_create)(void *_context),
	    void (*_rlock_acquire)(void *_lock),
	    void (*_wlock_acquire)(void *_lock),
	    void (*_lock_release)(void *_lock),
	    void (*_lock_destroy)(void *_lock),
	    void (*_context_destroy)(void *_context));
void	*dlvsym(void * __restrict, const char * __restrict,
	    const char * __restrict);
#endif /* __BSD_VISIBLE */
__END_DECLS

#endif /* !_DLFCN_H_ */
