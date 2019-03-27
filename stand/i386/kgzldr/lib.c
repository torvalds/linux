/*-
 * Copyright (c) 1999 Global Technology Associates, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <stddef.h>

#include "kgzldr.h"

#define MEMSIZ  0x8000		/* Memory pool size */

int kgz_con;			/* Console control */

static size_t memtot;		/* Memory allocated: bytes */
static u_int memcnt;		/* Memory allocated: blocks */

/*
 * Library functions required by inflate().
 */

/*
 * Allocate memory block.
 */
unsigned char *
kzipmalloc(int size)
{
    static u_char mem[MEMSIZ];
    void *ptr;

    if (memtot + size > MEMSIZ)
        return NULL;
    ptr = mem + memtot;
    memtot += size;
    memcnt++;
    return ptr;
}

/*
 * Free allocated memory block.
 */
void
kzipfree(void *ptr)
{
    memcnt--;
    if (!memcnt)
        memtot = 0;
}

/*
 * Write a string to the console.
 */
void
putstr(const char *str)
{
    int c;

    while ((c = *str++)) {
        if (kgz_con & KGZ_CRT)
            crt_putchr(c);
        if (kgz_con & KGZ_SIO)
            sio_putchr(c);
    }
}
