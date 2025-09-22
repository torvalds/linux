/*      $OpenBSD: criov.c,v 1.20 2015/03/14 03:38:46 jsg Exp $	*/

/*
 * Copyright (c) 1999 Theo de Raadt
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>

void
cuio_copydata(struct uio *uio, int off, int len, caddr_t cp)
{
	struct iovec *iov = uio->uio_iov;
	int iol = uio->uio_iovcnt;
	unsigned count;

	if (off < 0)
		panic("cuio_copydata: off %d < 0", off);
	if (len < 0)
		panic("cuio_copydata: len %d < 0", len);
	while (off > 0) {
		if (iol == 0)
			panic("iov_copydata: empty in skip");
		if (off < iov->iov_len)
			break;
		off -= iov->iov_len;
		iol--;
		iov++;
	}
	while (len > 0) {
		if (iol == 0)
			panic("cuio_copydata: empty");
		count = min(iov->iov_len - off, len);
		bcopy(((caddr_t)iov->iov_base) + off, cp, count);
		len -= count;
		cp += count;
		off = 0;
		iol--;
		iov++;
	}
}

void
cuio_copyback(struct uio *uio, int off, int len, const void *_cp)
{
	struct iovec *iov = uio->uio_iov;
	int iol = uio->uio_iovcnt;
	unsigned count;
	caddr_t cp = (caddr_t)_cp;

	if (off < 0)
		panic("cuio_copyback: off %d < 0", off);
	if (len < 0)
		panic("cuio_copyback: len %d < 0", len);
	while (off > 0) {
		if (iol == 0)
			panic("cuio_copyback: empty in skip");
		if (off < iov->iov_len)
			break;
		off -= iov->iov_len;
		iol--;
		iov++;
	}
	while (len > 0) {
		if (iol == 0)
			panic("uio_copyback: empty");
		count = min(iov->iov_len - off, len);
		bcopy(cp, ((caddr_t)iov->iov_base) + off, count);
		len -= count;
		cp += count;
		off = 0;
		iol--;
		iov++;
	}
}

int
cuio_getptr(struct uio *uio, int loc, int *off)
{
	int ind, len;

	ind = 0;
	while (loc >= 0 && ind < uio->uio_iovcnt) {
		len = uio->uio_iov[ind].iov_len;
		if (len > loc) {
			*off = loc;
			return (ind);
		}
		loc -= len;
		ind++;
	}

	if (ind > 0 && loc == 0) {
		ind--;
		*off = uio->uio_iov[ind].iov_len;
		return (ind);
	}

	return (-1);
}

int
cuio_apply(struct uio *uio, int off, int len,
    int (*f)(caddr_t, caddr_t, unsigned int), caddr_t fstate)
{
	int rval, ind, uiolen;
	unsigned int count;

	if (len < 0)
		panic("cuio_apply: len %d < 0", len);
	if (off < 0)
		panic("cuio_apply: off %d < 0", off);
	
	ind = 0;
	while (off > 0) {
		if (ind >= uio->uio_iovcnt)
			panic("cuio_apply: ind %d >= uio_iovcnt %d for off",
			    ind, uio->uio_iovcnt);
		uiolen = uio->uio_iov[ind].iov_len;
		if (off < uiolen)
			break;
		off -= uiolen;
		ind++;
	}
	while (len > 0) {
		if (ind >= uio->uio_iovcnt)
			panic("cuio_apply: ind %d >= uio_iovcnt %d for len",
			    ind, uio->uio_iovcnt);
		count = min(uio->uio_iov[ind].iov_len - off, len);

		rval = f(fstate, (char *)uio->uio_iov[ind].iov_base + off,
		    count);
		if (rval)
			return (rval);

		len -= count;
		off = 0;
		ind++;
	}

	return (0);
}
