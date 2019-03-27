/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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

#include <stand.h>
#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/linker.h>
#include <machine/pc/bios.h>
#include <machine/metadata.h>

#include "bootstrap.h"
#include "libuserboot.h"

#define GB (1024UL * 1024 * 1024)

void
bios_addsmapdata(struct preloaded_file *kfp)
{
	uint64_t lowmem, highmem;
	int smapnum, len;
	struct bios_smap smap[3], *sm;

	CALLBACK(getmem, &lowmem, &highmem);

	sm = &smap[0];

	sm->base = 0;				/* base memory */
	sm->length = 640 * 1024;
	sm->type = SMAP_TYPE_MEMORY;
	sm++;

	sm->base = 0x100000;			/* extended memory */
	sm->length = lowmem - 0x100000;
	sm->type = SMAP_TYPE_MEMORY;
	sm++;

	smapnum = 2;

        if (highmem != 0) {
                sm->base = 4 * GB;
                sm->length = highmem;
                sm->type = SMAP_TYPE_MEMORY;
		smapnum++;
        }

        len = smapnum * sizeof(struct bios_smap);
        file_addmetadata(kfp, MODINFOMD_SMAP, len, &smap[0]);
}
