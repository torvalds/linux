/*	$OpenBSD: openprom.c,v 1.2 2020/07/06 15:18:03 kettenis Exp $	*/
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
#include <machine/conf.h>

#include <dev/ofw/openfirm.h>

static int lastnode;			/* speed hack */
static int optionsnode;			/* node ID of ROM's options */

int	openpromcheckid(int, int);
int	openpromgetstr(int, char *, char **);
void    openpromattach(int);

void
openpromattach(int num)
{
}

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

/*
 * Verify target ID is valid (exists in the OPENPROM tree), as
 * listed from node ID sid forward.
 */
int
openpromcheckid(int sid, int tid)
{
	for (; sid != 0; sid = OF_peer(sid))
		if (sid == tid || openpromcheckid(OF_child(sid), tid))
			return (1);

	return (0);
}

int
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
	int node, len, ok, error;
	char *name, *value, *nextprop;

	if (optionsnode == 0)
		optionsnode = OF_getnodebyname(0, "options");

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
		ok = openpromcheckid(OF_peer(0), node);
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
		len = OF_getproplen(node, name);
		if (len > op->op_buflen) {
			error = ENOMEM;
			break;
		}
		op->op_buflen = len;
		/* -1 means no entry; 0 means no value */
		if (len <= 0)
			break;
		value = malloc(len, M_TEMP, M_WAITOK);
		OF_getprop(node, name, value, len);
		error = copyout(value, op->op_buf, len);
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
		value = nextprop = malloc(OFMAXPARAM, M_TEMP,
		    M_WAITOK | M_CANFAIL);
		if (nextprop == NULL) {
			error = ENOMEM;
			break;
		}
		error = OF_nextprop(node, name, nextprop);
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
		node = OF_peer(node);
		*(int *)data = lastnode = node;
		break;

	case OPIOCGETCHILD:
		if ((flags & FREAD) == 0)
			return (EBADF);
		if (node == 0)
			return (EINVAL);
		node = OF_child(node);
		*(int *)data = lastnode = node;
		break;

	default:
		return (ENOTTY);
	}

	free(name, M_TEMP, 0);
	free(value, M_TEMP, 0);

	return (error);
}
