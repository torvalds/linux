/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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

#include <sys/types.h>

#include <machine/utrap.h>
#include <machine/sysarch.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fpu_extern.h"
#include "__sparc_utrap_private.h"

extern ssize_t __sys_write(int, const void *, size_t);
extern int __sys_kill(pid_t, int);
extern pid_t __sys_getpid(void);

static const char *utrap_msg[] = {
	"reserved",
	"instruction access exception",
	"instruction access error",
	"instruction access protection",
	"illtrap instruction",
	"illegal instruction",
	"privileged opcode",
	"floating point disabled",
	"floating point exception ieee 754",
	"floating point exception other",
	"tag overflow",
	"division by zero",
	"data access exception",
	"data access error",
	"data access protection",
	"memory address not aligned",
	"privileged action",
	"async data error",
	"trap instruction 16",
	"trap instruction 17",
	"trap instruction 18",
	"trap instruction 19",
	"trap instruction 20",
	"trap instruction 21",
	"trap instruction 22",
	"trap instruction 23",
	"trap instruction 24",
	"trap instruction 25",
	"trap instruction 26",
	"trap instruction 27",
	"trap instruction 28",
	"trap instruction 29",
	"trap instruction 30",
	"trap instruction 31",
};

void
__sparc_utrap(struct utrapframe *uf)
{
	int sig;

	switch (uf->uf_type) {
	case UT_FP_EXCEPTION_IEEE_754:
	case UT_FP_EXCEPTION_OTHER:
		sig = __fpu_exception(uf);
		break;
	case UT_ILLEGAL_INSTRUCTION:
		sig = __emul_insn(uf);
		break;
	case UT_MEM_ADDRESS_NOT_ALIGNED:
		sig = __unaligned_fixup(uf);
		break;
	default:
		break;
	}
	if (sig) {
		__utrap_write("__sparc_utrap: fatal ");
		__utrap_write(utrap_msg[uf->uf_type]);
		__utrap_write("\n");
		__utrap_kill_self(sig);
	}
	UF_DONE(uf);
}

void
__utrap_write(const char *str)
{
	int berrno;

	berrno = errno;
	__sys_write(STDERR_FILENO, str, strlen(str));
	errno = berrno;
}

void
__utrap_kill_self(int sig)
{
	int berrno;

	berrno = errno;
	__sys_kill(__sys_getpid(), sig);
	errno = berrno;
}

void
__utrap_panic(const char *msg)
{

	__utrap_write(msg);
	__utrap_write("\n");
	__utrap_kill_self(SIGKILL);
}
