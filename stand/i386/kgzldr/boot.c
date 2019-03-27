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
#include <sys/reboot.h>
#include <sys/inflate.h>

#include "kgzldr.h"

#define KGZ_HEAD   0xa          /* leading bytes to ignore */
#define KGZ_TAIL   0x8          /* trailing bytes to ignore */

#define E_FMT  1		/* Error: Invalid format */
#define E_MEM  2		/* Error: Out of memory */

struct kgz_hdr {
    char	ident[4];	/* identification */
    uint32_t	dload;		/* decoded image load address */
    uint32_t	dsize;		/* decoded image size */
    uint32_t	isize;		/* image size in memory */
    uint32_t	entry;		/* program entry point */
    uint32_t	nsize;		/* encoded image size */
};
extern struct kgz_hdr kgz;	/* header */
extern uint8_t kgz_ndata[];	/* encoded image */

static const char *const msg[] = {
    "done",
    "invalid format",
    "out of memory"
};

static const u_char *ip;        /* input pointer */
static u_char *op;              /* output pointer */

static struct inflate infl;	/* inflate() parameters */

static int decode(void);
static int input(void *);
static int output(void *, u_char *, u_long);

/*
 * Uncompress and boot a kernel.
 */
int
boot(int howto)
{
    int err;

    kgz_con = howto & RB_SERIAL ? KGZ_SIO : KGZ_CRT;
    putstr("Uncompressing ... ");
    err = decode();
    putstr(msg[err]);
    putstr("\n");
    if (err) {
        putstr("System halted");
	for (;;)
	    ;
    }
    return err;
}

/*
 * Interface with inflate() to uncompress the data.
 */
static int
decode(void)
{
    static u_char slide[GZ_WSIZE];
    int err;

    ip = kgz_ndata + KGZ_HEAD;
    op = (u_char *)kgz.dload;
    infl.gz_input = input;
    infl.gz_output = output;
    infl.gz_slide = slide;
    err = inflate(&infl);
    return err ? err == 3 ? E_MEM : E_FMT : 0;
}

/*
 * Read a byte.
 */
static int
input(void *dummy)
{
    if ((size_t)(ip - kgz_ndata) + KGZ_TAIL > kgz.nsize)
        return GZ_EOF;
    return *ip++;
}

/*
 * Write some bytes.
 */
static int
output(void *dummy, u_char * ptr, u_long len)
{
    if (op - (u_char *)kgz.dload + len > kgz.dsize)
        return -1;
    while (len--)
        *op++ = *ptr++;
    return 0;
}
