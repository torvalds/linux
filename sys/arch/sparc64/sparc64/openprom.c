/*	$OpenBSD: openprom.c,v 1.23 2022/10/16 01:22:39 jsg Exp $	*/
/*	$NetBSD: openprom.c,v 1.4 2002/01/10 06:21:53 briggs Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)openprom.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/device.h>

#include <machine/openpromio.h>
#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/mdesc.h>

#include <dev/clock_subr.h>
#include <dev/ofw/openfirm.h>

extern todr_chip_handle_t todr_handle;

#define OPROMMAXPARAM		32

static	int lastnode;			/* speed hack */
extern	int optionsnode;		/* node ID of ROM's options */

static int openpromcheckid(int, int);
static int openpromgetstr(int, char *, char **);

int
openpromopen(dev_t dev, int flags, int mode, struct proc *p)
{

	return (0);
}

int
openpromclose(dev_t dev, int flags, int mode, struct proc *p)
{

	return (0);
}

int
openpromread(dev_t dev, struct uio *uio, int flags)
{
#ifdef SUN4V
	int error;
	size_t data_len, len;
	caddr_t data, v;

	switch (minor(dev)) {
	case 1:
		data = mdesc;
		data_len = mdesc_len;
		break;
	case 2:
		data = pri;
		data_len = pri_len;
		break;
	default:
		data = NULL;
		data_len = 0;
		break;
	}

	if (data == NULL || data_len == 0)
		return (ENXIO);

	if (uio->uio_offset < 0)
		return (EINVAL);

	while (uio->uio_resid > 0) {
		if (uio->uio_offset >= data_len)
			break;

		v = data + uio->uio_offset;
		len = data_len - uio->uio_offset;
		if (len > uio->uio_resid)
			len = uio->uio_resid;

		error = uiomove(v, len, uio);
		if (error)
			return (error);
	}

	return (0);
#else
	return (ENXIO);
#endif
}

/*
 * Verify target ID is valid (exists in the OPENPROM tree), as
 * listed from node ID sid forward.
 */
static int
openpromcheckid(int sid, int tid)
{
	for (; sid != 0; sid = OF_peer(sid))
		if (sid == tid || openpromcheckid(OF_child(sid), tid))
			return (1);

	return (0);
}

static int
openpromgetstr(int len, char *user, char **cpp)
{
	int error;
	char *cp;

	/* Reject obvious bogus requests */
	if ((u_int)len > (8 * 1024) - 1)
		return (ENAMETOOLONG);

	*cpp = cp = malloc(len + 1, M_TEMP, M_WAITOK);
	error = copyin(user, cp, len);
	cp[len] = '\0';
	return (error);
}

int
openpromioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct opiocdesc *op;
	int node, len, ok, error, s;
	char *name, *value, *nextprop;

	/* All too easy... */
	if (cmd == OPIOCGETOPTNODE) {
		*(int *)data = optionsnode;
		return (0);
	}

	/* Verify node id */
	op = (struct opiocdesc *)data;
	node = op->op_nodeid;
	if (node != 0 && node != lastnode && node != optionsnode) {
		/* Not an easy one, must search for it */
		s = splhigh();
		ok = openpromcheckid(findroot(), node);
		splx(s);
		if (!ok)
			return (EINVAL);
		lastnode = node;
	}

	name = value = NULL;
	error = 0;
	switch (cmd) {

	case OPIOCGET:
		if ((flags & FREAD) == 0)
			return (EBADF);
		if (node == 0)
			return (EINVAL);
		error = openpromgetstr(op->op_namelen, op->op_name, &name);
		if (error)
			break;
		s = splhigh();
		len = getproplen(node, name);
		splx(s);
		if (len > op->op_buflen) {
			error = ENOMEM;
			break;
		}
		op->op_buflen = len;
		/* -1 means no entry; 0 means no value */
		if (len <= 0)
			break;
		value = malloc(len, M_TEMP, M_WAITOK);
		s = splhigh();
		error = getprop(node, name, 1, &len, (void **)&value);
		splx(s);
		if (error != 0)
			break;
		error = copyout(value, op->op_buf, len);
		break;

	case OPIOCSET:
		if ((flags & FWRITE) == 0)
			return (EBADF);
		if (node == 0)
			return (EINVAL);
		error = openpromgetstr(op->op_namelen, op->op_name, &name);
		if (error)
			break;
		error = openpromgetstr(op->op_buflen, op->op_buf, &value);
		if (error)
			break;
		s = splhigh();
		/*
		 * Make sure we can write to nvram, which on many
		 * machines lives on the clock chip (and shares its
		 * address space with the clock).
		 */
		if (todr_handle)
			todr_wenable(todr_handle, 1);
		len = OF_setprop(node, name, value, op->op_buflen + 1);
		if (todr_handle)
			todr_wenable(todr_handle, 0);
		splx(s);
		if (len != op->op_buflen)
			error = EINVAL;
		break;

	case OPIOCNEXTPROP:
		if ((flags & FREAD) == 0)
			return (EBADF);
		if (node == 0)
			return (EINVAL);
		error = openpromgetstr(op->op_namelen, op->op_name, &name);
		if (error)
			break;
		if (op->op_buflen <= 0) {
			error = ENAMETOOLONG;
			break;
		}
		value = nextprop = malloc(OPROMMAXPARAM, M_TEMP,
		    M_WAITOK | M_CANFAIL);
		if (nextprop == NULL) {
			error = ENOMEM;
			break;
		}
		s = splhigh();
		error = OF_nextprop(node, name, nextprop);
		splx(s);
		if (error == -1) {
			error = EINVAL;
			break;
		}
		if (error == 0) {
			char nul = '\0';

			op->op_buflen = 0;
			error = copyout(&nul, op->op_buf, sizeof(char));
			break;
		}
		len = strlen(nextprop);
		if (len > op->op_buflen)
			len = op->op_buflen;
		else
			op->op_buflen = len;
		error = copyout(nextprop, op->op_buf, len);
		break;

	case OPIOCGETNEXT:
		if ((flags & FREAD) == 0)
			return (EBADF);
		s = splhigh();
		node = nextsibling(node);
		splx(s);
		*(int *)data = lastnode = node;
		break;

	case OPIOCGETCHILD:
		if ((flags & FREAD) == 0)
			return (EBADF);
		if (node == 0)
			return (EINVAL);
		s = splhigh();
		node = firstchild(node);
		splx(s);
		*(int *)data = lastnode = node;
		break;

	default:
		return (ENOTTY);
	}

	free(name, M_TEMP, 0);
	free(value, M_TEMP, 0);

	return (error);
}
