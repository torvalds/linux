/*	$OpenBSD: openfirm.c,v 1.13 2020/04/02 19:27:51 gkoehler Exp $	*/
/*	$NetBSD: openfirm.c,v 1.1 1996/09/30 16:34:52 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stdarg.h>
#include <machine/cpu.h>
#include <machine/psl.h>

#include <dev/ofw/openfirm.h>
#include "ofw_machdep.h"

extern void ofbcopy(const void *, void *, size_t);

int
OF_peer(int phandle)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int phandle;
		int sibling;
	} args = {
		"peer",
		1,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.phandle = phandle;
	if (openfirmware(&args) == -1)
		ret = 0;
	else
		ret = args.sibling;
	ppc_mtmsr(s);
	return ret;
}

int
OF_child(int phandle)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int phandle;
		int child;
	} args = {
		"child",
		1,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.phandle = phandle;
	if (openfirmware(&args) == -1)
		ret = 0;
	else
		ret = args.child;
	ppc_mtmsr(s);
	return ret;
}

int
OF_parent(int phandle)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int phandle;
		int parent;
	} args = {
		"parent",
		1,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.phandle = phandle;
	if (openfirmware(&args) == -1)
		ret = 0;
	else
		ret = args.parent;
	ppc_mtmsr(s);
	return ret;
}

int
OF_getproplen(int handle, char *prop)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int phandle;
		char *prop;
		int size;
	} args = {
		"getproplen",
		2,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.phandle = handle;
	args.prop = prop;
	if (openfirmware(&args) == -1)
		ret = -1;
	else
		ret = args.size;
	ppc_mtmsr(s);
	return ret;
}

int
OF_getprop(int handle, char *prop, void *buf, int buflen)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int phandle;
		char *prop;
		void *buf;
		int buflen;
		int size;
	} args = {
		"getprop",
		4,
		1,
	};
	uint32_t s;
	int ret;

	if (buflen > NBPG)
		return -1;
	s = ofw_msr();
	args.phandle = handle;
	args.prop = prop;
	args.buf = OF_buf;
	args.buflen = buflen;
	if (openfirmware(&args) == -1)
		ret = -1;
	else {
		if (args.size > 0)
			ofbcopy(OF_buf, buf, args.size);
		ret = args.size;
	}
	ppc_mtmsr(s);
	return ret;
}

int
OF_setprop(int handle, char *prop, const void *buf, int buflen)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int phandle;
		char *prop;
		void *buf;
		int buflen;
		int size;
	} args = {
		"setprop",
		4,
		1,
	};
	uint32_t s;
	int ret;
	
	if (buflen > NBPG)
		return -1;
	s = ofw_msr();
	args.phandle = handle;
	args.prop = prop;
	ofbcopy(buf, OF_buf, buflen);
	args.buf = OF_buf;
	args.buflen = buflen;
	if (openfirmware(&args) == -1)
		ret = -1;
	else
		ret = args.size;
	ppc_mtmsr(s);
	return ret;
}

int
OF_nextprop(int handle, char *prop, void *nextprop)
{
	static struct {
		const char *name;
		int nargs;
		int nreturns;
		int phandle;
		const char *prop;
		char *buf;
		int flag;
	} args = {
		"nextprop",
		3,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.phandle = handle;
	args.prop = prop;
	args.buf = OF_buf;
	if (openfirmware(&args) == -1)
		ret = -1;
	else {
		strlcpy(nextprop, OF_buf, 32);
		ret = args.flag;
	}
	ppc_mtmsr(s);
	return ret;
}

int
OF_interpret(char *cmd, int nreturns, ...)
{
	va_list ap;
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *cmd;
		int status;
		int results[8];
	} args = {
		"interpret",
		1,
		2,
	};
	uint32_t s;
	int i, ret;

	if (nreturns > 8)
		return -1;
	if ((i = strlen(cmd)) >= NBPG)
		return -1;
	s = ofw_msr();
	ofbcopy(cmd, OF_buf, i + 1);
	args.cmd = OF_buf;
	args.nargs = 1;
	args.nreturns = nreturns + 1;
	if (openfirmware(&args) == -1)
		ret = -1;
	else {
		va_start(ap, nreturns);
		for (i = 0; i < nreturns; i++)
			*va_arg(ap, int *) = args.results[i];
		va_end(ap);
		ret = args.status;
	}
	ppc_mtmsr(s);
	return ret;
}


int
OF_finddevice(char *name)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *device;
		int phandle;
	} args = {
		"finddevice",
		1,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.device = name;
	if (openfirmware(&args) == -1)
		ret = -1;
	else
		ret = args.phandle;
	ppc_mtmsr(s);
	return ret;
}
static void OF_rboot(char *bootspec);

static void
OF_rboot(char *bootspec)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
	} args = {
		"reset-all",
		0,
		0,
	};
	uint32_t s;
	int l;

	if ((l = strlen(bootspec)) >= NBPG)
		panic("OF_boot");
	s = ofw_msr();
	openfirmware(&args);
	ppc_mtmsr(s);
	/* will attempt exit in OF_boot */
}


void
OF_boot(char *bootspec)
{
	OF_rboot(bootspec);
	printf ("OF_boot returned!");		/* just in case */
	OF_exit();
	while(1);
}

void
OF_exit(void)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
	} args = {
		"exit",
		0,
		0,
	};

	ofw_msr();
	openfirmware(&args);
	panic ("OF_exit returned!");		/* just in case */
	while (1);
}

void
OF_quiesce(void)
{
	static struct {
		const char *name;
		int nargs;
		int nreturns;
	} args = {
		"quiesce",
		0,
		0,
	};
	uint32_t s;

	s = ofw_msr();
	openfirmware(&args);
	ppc_mtmsr(s);
}

/* XXX What is the reason to have this instead of bcopy/memcpy? */
void
ofbcopy(const void *src, void *dst, size_t len)
{
        const char *sp = src;
        char *dp = dst;

        if (src == dst)
                return;

        while (len-- > 0)
                *dp++ = *sp++;
}

int
OF_getnodebyname(int start, const char *name)
{
	char nname[32];
	int len;
	int node = 0;
	int next;

	if (start == 0)
		start = OF_peer(0);

	for (node = start; node; node = next) {
		len = OF_getprop(node, "name", nname, sizeof(nname));
		nname[len] = 0;
		if (strcmp(nname, name) == 0) {
			return node;
		}
		if ((next = OF_child(node)) != 0)
			continue;
		while (node) {
			if ((next = OF_peer(node)) != 0)
				break;
			node = OF_parent(node);
		}
	}
	return node;
}
