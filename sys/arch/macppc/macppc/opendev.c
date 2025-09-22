/*	$OpenBSD: opendev.c,v 1.11 2024/11/07 15:40:02 miod Exp $	*/
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
OF_instance_to_package(int ihandle)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int ihandle;
		int phandle;
	} args = {
		"instance-to-package",
		1,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.ihandle = ihandle;
	if (openfirmware(&args) == -1)
		ret = -1;
	else
		ret = args.phandle;
	ppc_mtmsr(s);
	return ret;
}

int
OF_call_method(char *method, int ihandle, int nargs, int nreturns, ...)
{
	va_list ap;
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *method;
		int ihandle;
		int args_n_results[12];
	} args = {
		"call-method",
		2,
		1,
	};
	uint32_t s;
	int *ip, n, ret;

	if (nargs > 6)
		return -1;
	s = ofw_msr();
	args.nargs = nargs + 2;
	args.nreturns = nreturns + 1;
	args.method = method;
	args.ihandle = ihandle;
	va_start(ap, nreturns);
	for (ip = args.args_n_results + (n = nargs); --n >= 0;)
		*--ip = va_arg(ap, int);
	if (openfirmware(&args) == -1)
		ret = -1;
	else if (args.args_n_results[nargs])
		ret = args.args_n_results[nargs];
	else {
		for (ip = args.args_n_results + nargs + (n = args.nreturns);
		    --n > 0;)
			*va_arg(ap, int *) = *--ip;
		ret = 0;
	}
	va_end(ap);
	ppc_mtmsr(s);
	return ret;
}
int
OF_call_method_1(char *method, int ihandle, int nargs, ...)
{
	va_list ap;
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *method;
		int ihandle;
		int args_n_results[8];
	} args = {
		"call-method",
		2,
		2,
	};
	uint32_t s;
	int *ip, n, ret;

	if (nargs > 6)
		return -1;
	s = ofw_msr();
	args.nargs = nargs + 2;
	args.method = method;
	args.ihandle = ihandle;
	va_start(ap, nargs);
	for (ip = args.args_n_results + (n = nargs); --n >= 0;)
		*--ip = va_arg(ap, int);
	va_end(ap);
	if (openfirmware(&args) == -1)
		ret = -1;
	else if (args.args_n_results[nargs])
		ret = -1;
	else
		ret = args.args_n_results[nargs + 1];
	ppc_mtmsr(s);
	return ret;
}

int
OF_open(char *dname)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		char *dname;
		int handle;
	} args = {
		"open",
		1,
		1,
	};
	uint32_t s;
	int l, ret;

	if ((l = strlen(dname)) >= PAGE_SIZE)
		return -1;
	s = ofw_msr();
	ofbcopy(dname, OF_buf, l + 1);
	args.dname = OF_buf;
	if (openfirmware(&args) == -1)
		ret = -1;
	else
		ret = args.handle;
	ppc_mtmsr(s);
	return ret;
}

void
OF_close(int handle)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int handle;
	} args = {
		"close",
		1,
		0,
	};
	uint32_t s;

	s = ofw_msr();
	args.handle = handle;
	openfirmware(&args);
	ppc_mtmsr(s);
}

/*
 * This assumes that character devices don't read in multiples of PAGE_SIZE.
 */
int
OF_read(int handle, void *addr, int len)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int ihandle;
		void *addr;
		int len;
		int actual;
	} args = {
		"read",
		3,
		1,
	};
	uint32_t s;
	int act = 0, l, ret;

	s = ofw_msr();
	args.ihandle = handle;
	args.addr = OF_buf;
	for (; len > 0; len -= l, addr += l) {
		l = min(PAGE_SIZE, len);
		args.len = l;
		if (openfirmware(&args) == -1) {
			ret = -1;
			goto out;
		}
		if (args.actual > 0) {
			ofbcopy(OF_buf, addr, args.actual);
			act += args.actual;
		}
		if (args.actual < l) {
			if (act)
				ret = act;
			else
				ret = args.actual;
			goto out;
		}
	}
	ret = act;
out:
	ppc_mtmsr(s);
	return ret;
}

int
OF_write(int handle, void *addr, int len)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int ihandle;
		void *addr;
		int len;
		int actual;
	} args = {
		"write",
		3,
		1,
	};
	uint32_t s;
	int act = 0, l, ret;

	s = ofw_msr();
	args.ihandle = handle;
	args.addr = OF_buf;
	for (; len > 0; len -= l, addr += l) {
		l = min(PAGE_SIZE, len);
		ofbcopy(addr, OF_buf, l);
		args.len = l;
		if (openfirmware(&args) == -1) {
			ret = -1;
			goto out;
		}
		l = args.actual;
		act += l;
	}
	ret = act;
out:
	ppc_mtmsr(s);
	return ret;
}

int
OF_seek(int handle, u_quad_t pos)
{
	static struct {
		char *name;
		int nargs;
		int nreturns;
		int handle;
		int poshi;
		int poslo;
		int status;
	} args = {
		"seek",
		3,
		1,
	};
	uint32_t s;
	int ret;

	s = ofw_msr();
	args.handle = handle;
	args.poshi = (int)(pos >> 32);
	args.poslo = (int)pos;
	if (openfirmware(&args) == -1)
		ret = -1;
	else
		ret = args.status;
	ppc_mtmsr(s);
	return ret;
}
