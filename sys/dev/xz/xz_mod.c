/*-
 * Copyright (c) 2010-2012 Aleksandr Rybalko
 * Copyright (c) 2019 Mellanox Technologies
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include "xz.h"
#include "xz_malloc.h"

/* Wraper for XZ decompressor memory pool */

static MALLOC_DEFINE(XZ_DEC, "XZ_DEC", "XZ decompressor data");

void *
xz_malloc(unsigned long size)
{
	void *addr;

	addr = malloc(size, XZ_DEC, M_NOWAIT);
	return (addr);
}

void
xz_free(void *addr)
{

	free(addr, XZ_DEC);
}

static int
xz_module_event_handler(module_t mod, int what, void *arg)
{
	int error;

	switch (what) {
	case MOD_LOAD:
#if XZ_INTERNAL_CRC32
		xz_crc32_init();
#endif
#if XZ_INTERNAL_CRC64
		xz_crc64_init();
#endif
		error = 0;
		break;
	case MOD_UNLOAD:
		error = 0;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static moduledata_t xz_moduledata = {
	"xz",
	xz_module_event_handler,
	NULL
};

DECLARE_MODULE(xz, xz_moduledata, SI_SUB_INIT_IF, SI_ORDER_ANY);
MODULE_VERSION(xz, 1);
