/*	$OpenBSD: dlfcn_stubs.c,v 1.18 2020/10/09 16:31:03 otto Exp $	*/

/*
 * Copyright (c) 1998 Per Fogelstrom, Opsycon AB
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>
#include <link.h>
#include <dlfcn.h>
#include <tib.h>

#include "init.h"

static int dlerror_ret;

void *
dlopen(const char *libname, int how)
{
	if (_dl_cb != NULL && _dl_cb->dlopen != NULL)
		return _dl_cb->dlopen(libname, how);
	return NULL;
}

int
dlclose(void *handle)
{
	if (_dl_cb != NULL && _dl_cb->dlclose != NULL)
		return _dl_cb->dlclose(handle);
	dlerror_ret = 1;
	return -1;
}

void *
dlsym(void *handle, const char *name)
{
	if (_dl_cb != NULL && _dl_cb->dlsym != NULL)
		return _dl_cb->dlsym(handle, name);
	dlerror_ret = 1;
	return NULL;
}

int
dlctl(void *handle, int command, void *data)
{
	if (_dl_cb != NULL && _dl_cb->dlctl != NULL)
		return _dl_cb->dlctl(handle, command, data);
	dlerror_ret = 1;
	return -1;
}
DEF_WEAK(dlctl);

char *
dlerror(void)
{
	if (_dl_cb != NULL && _dl_cb->dlerror != NULL)
		return _dl_cb->dlerror();
	if (dlerror_ret) {
		dlerror_ret = 0;
		return _dl_cb == NULL ? "No dynamic linker" :
		    "Incompatible dynamic linker";
	}
	return NULL;
}

int
dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *),
	void *data)
{
	if (_dl_cb != NULL && _dl_cb->dl_iterate_phdr != NULL)
		return _dl_cb->dl_iterate_phdr(callback, data);
#ifndef PIC
	if (_static_phdr_info.dlpi_phdr != NULL)
		return callback(&_static_phdr_info, sizeof(_static_phdr_info),
		    data);
#endif /* !PIC */
	return -1;
}
DEF_WEAK(dl_iterate_phdr);

int
dladdr(const void *addr, struct dl_info *info)
{
	if (_dl_cb != NULL && _dl_cb->dladdr != NULL)
		return _dl_cb->dladdr(addr, info);
	dlerror_ret = 1;
	return 0;
}
DEF_WEAK(dladdr);

#if 0
/* Thread Local Storage argument structure */
typedef struct {
	unsigned long int ti_module;
	unsigned long int ti_offset;
} tls_index;

void	*__tls_get_addr(tls_index *) __attribute__((weak));
#ifdef __i386
void	*___tls_get_addr(tls_index *) __attribute__((weak, __regparm__(1)));
#endif

#if defined(__amd64) || defined(__i386) || defined(__sparc64)
void *
__tls_get_addr(tls_index *ti)
{
	return NULL;
}

#ifdef __i386
__attribute__((__regparm__(1))) void *
___tls_get_addr(tls_index *ti)
{
	return NULL;
}
#endif /* __i386 */
#endif /* arch with TLS support enabled */
#endif
