/*	$NetBSD: strerror.c,v 1.12 1997/01/25 00:37:50 cgd Exp $	*/

/*-
 * Copyright (c) 1993
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

#include "stand.h"

static struct 
{
    int		err;
    char	*msg;
} errtab[] = {
    {0,		"no error"},
    /* standard errors */
    {EPERM,		"operation not permitted"},
    {ENOENT,		"no such file or directory"},
    {EIO,		"input/output error"},
    {ENXIO,		"device not configured"},
    {ENOEXEC,		"exec format error"},
    {EBADF,		"bad file descriptor"},
    {ENOMEM,		"cannot allocate memory"},
    {ENODEV,		"operation not supported by device"},
    {ENOTDIR,		"not a directory"},
    {EISDIR,		"is a directory"},
    {EINVAL,		"invalid argument"},
    {EMFILE,		"too many open files"},
    {EFBIG,		"file too large"},
    {EROFS,		"read-only filesystem"},
    {EOPNOTSUPP,	"operation not supported"},
    {ETIMEDOUT,		"operation timed out"},
    {ESTALE,		"stale NFS file handle"},
    {EBADRPC,		"RPC struct is bad"},
    {EFTYPE,		"inappropriate file type or format"},

    {EADAPT,		"bad adaptor number"},
    {ECTLR,		"bad controller number"},
    {EUNIT,		"bad unit number"},
    {ESLICE,		"bad slice number"},
    {EPART,		"bad partition"},
    {ERDLAB,		"can't read disk label"},
    {EUNLAB,		"disk unlabelled"},
    {EOFFSET,		"illegal seek"},
    {0,		NULL}
};


char *
strerror(int err)
{
    static char	msg[32];
    int		i;

    for (i = 0; errtab[i].msg != NULL; i++)
	if (errtab[i].err == err)
	    return(errtab[i].msg);
    sprintf(msg, "unknown error (%d)", err);
    return(msg);
}
