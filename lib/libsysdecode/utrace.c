/*-
 * Copyright (c) 1988, 1993
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
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysdecode.h>
#include "rtld_utrace.h"

#ifdef __LP64__
struct utrace_rtld32 {
	char sig[4];
	int event;
	uint32_t handle;
	uint32_t mapbase;
	uint32_t mapsize;
	int refcnt;
	char name[MAXPATHLEN];
};
#endif

static int
print_utrace_rtld(FILE *fp, void *p)
{
	struct utrace_rtld *ut = p;
	void *parent;
	int mode;

	switch (ut->event) {
	case UTRACE_DLOPEN_START:
		mode = ut->refcnt;
		fprintf(fp, "dlopen(%s, ", ut->name);
		switch (mode & RTLD_MODEMASK) {
		case RTLD_NOW:
			fprintf(fp, "RTLD_NOW");
			break;
		case RTLD_LAZY:
			fprintf(fp, "RTLD_LAZY");
			break;
		default:
			fprintf(fp, "%#x", mode & RTLD_MODEMASK);
		}
		if (mode & RTLD_GLOBAL)
			fprintf(fp, " | RTLD_GLOBAL");
		if (mode & RTLD_TRACE)
			fprintf(fp, " | RTLD_TRACE");
		if (mode & ~(RTLD_MODEMASK | RTLD_GLOBAL | RTLD_TRACE))
			fprintf(fp, " | %#x", mode &
			    ~(RTLD_MODEMASK | RTLD_GLOBAL | RTLD_TRACE));
		fprintf(fp, ")");
		break;
	case UTRACE_DLOPEN_STOP:
		fprintf(fp, "%p = dlopen(%s) ref %d", ut->handle, ut->name,
		    ut->refcnt);
		break;
	case UTRACE_DLCLOSE_START:
		fprintf(fp, "dlclose(%p) (%s, %d)", ut->handle, ut->name,
		    ut->refcnt);
		break;
	case UTRACE_DLCLOSE_STOP:
		fprintf(fp, "dlclose(%p) finished", ut->handle);
		break;
	case UTRACE_LOAD_OBJECT:
		fprintf(fp, "RTLD: loaded   %p @ %p - %p (%s)", ut->handle,
		    ut->mapbase, (char *)ut->mapbase + ut->mapsize - 1,
		    ut->name);
		break;
	case UTRACE_UNLOAD_OBJECT:
		fprintf(fp, "RTLD: unloaded %p @ %p - %p (%s)", ut->handle,
		    ut->mapbase, (char *)ut->mapbase + ut->mapsize - 1,
		    ut->name);
		break;
	case UTRACE_ADD_RUNDEP:
		parent = ut->mapbase;
		fprintf(fp, "RTLD: %p now depends on %p (%s, %d)", parent,
		    ut->handle, ut->name, ut->refcnt);
		break;
	case UTRACE_PRELOAD_FINISHED:
		fprintf(fp, "RTLD: LD_PRELOAD finished");
		break;
	case UTRACE_INIT_CALL:
		fprintf(fp, "RTLD: init %p for %p (%s)", ut->mapbase, ut->handle,
		    ut->name);
		break;
	case UTRACE_FINI_CALL:
		fprintf(fp, "RTLD: fini %p for %p (%s)", ut->mapbase, ut->handle,
		    ut->name);
		break;
	case UTRACE_DLSYM_START:
		fprintf(fp, "RTLD: dlsym(%p, %s)", ut->handle, ut->name);
		break;
	case UTRACE_DLSYM_STOP:
		fprintf(fp, "RTLD: %p = dlsym(%p, %s)", ut->mapbase, ut->handle,
		    ut->name);
		break;
	case UTRACE_RTLD_ERROR:
		fprintf(fp, "RTLD: error: %s\n", ut->name);
		break;

	default:
		return (0);
	}
	return (1);
}

struct utrace_malloc {
	void *p;
	size_t s;
	void *r;
};

#ifdef __LP64__
struct utrace_malloc32 {
	uint32_t p;
	uint32_t s;
	uint32_t r;
};
#endif

static void
print_utrace_malloc(FILE *fp, void *p)
{
	struct utrace_malloc *ut = p;

	if (ut->p == (void *)(intptr_t)(-1))
		fprintf(fp, "malloc_init()");
	else if (ut->s == 0)
		fprintf(fp, "free(%p)", ut->p);
	else if (ut->p == NULL)
		fprintf(fp, "%p = malloc(%zu)", ut->r, ut->s);
	else
		fprintf(fp, "%p = realloc(%p, %zu)", ut->r, ut->p, ut->s);
}

int
sysdecode_utrace(FILE *fp, void *p, size_t len)
{
#ifdef __LP64__
	struct utrace_rtld ur;
	struct utrace_rtld32 *pr;
	struct utrace_malloc um;
	struct utrace_malloc32 *pm;
#endif
	static const char rtld_utrace_sig[RTLD_UTRACE_SIG_SZ] = RTLD_UTRACE_SIG;

	if (len == sizeof(struct utrace_rtld) && bcmp(p, rtld_utrace_sig,
	    sizeof(rtld_utrace_sig)) == 0)
		return (print_utrace_rtld(fp, p));

	if (len == sizeof(struct utrace_malloc)) {
		print_utrace_malloc(fp, p);
		return (1);
	}

#ifdef __LP64__
	if (len == sizeof(struct utrace_rtld32) && bcmp(p, rtld_utrace_sig,
	    sizeof(rtld_utrace_sig)) == 0) {
		pr = p;
		memset(&ur, 0, sizeof(ur));
		memcpy(ur.sig, pr->sig, sizeof(ur.sig));
		ur.event = pr->event;
		ur.handle = (void *)(uintptr_t)pr->handle;
		ur.mapbase = (void *)(uintptr_t)pr->mapbase;
		ur.mapsize = pr->mapsize;
		ur.refcnt = pr->refcnt;
		memcpy(ur.name, pr->name, sizeof(ur.name));
		return (print_utrace_rtld(fp, &ur));
	}

	if (len == sizeof(struct utrace_malloc32)) {
		pm = p;
		memset(&um, 0, sizeof(um));
		um.p = pm->p == (uint32_t)-1 ? (void *)(intptr_t)-1 :
		    (void *)(uintptr_t)pm->p;
		um.s = pm->s;
		um.r = (void *)(uintptr_t)pm->r;
		print_utrace_malloc(fp, &um);
		return (1);
	}
#endif

	return (0);
}
