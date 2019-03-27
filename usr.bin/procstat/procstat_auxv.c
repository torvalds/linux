/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Mikolaj Golub
 * Copyright (c) 2015 Allan Jude <allanjude@freebsd.org>
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/elf.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <vm/vm.h>

#include <err.h>
#include <errno.h>
#include <libprocstat.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

void
procstat_auxv(struct procstat *procstat, struct kinfo_proc *kipp)
{
	Elf_Auxinfo *auxv;
	u_int count, i;
	static char prefix[256];

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %-16s %-16s %-16s}\n", "PID", "COMM", "AUXV",
		    "VALUE");

	auxv = procstat_getauxv(procstat, kipp, &count);
	if (auxv == NULL)
		return;
        snprintf(prefix, sizeof(prefix), "%5d %-16s", kipp->ki_pid,
            kipp->ki_comm);

	xo_emit("{e:process_id/%5d/%d}{e:command/%-16s/%s}", kipp->ki_pid,
	    kipp->ki_comm);

	for (i = 0; i < count; i++) {
		switch(auxv[i].a_type) {
		case AT_NULL:
			return;
		case AT_IGNORE:
			break;
		case AT_EXECFD:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_EXECFD/%ld}\n",
			    prefix, "AT_EXECFD", (long)auxv[i].a_un.a_val);
			break;
		case AT_PHDR:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_PHDR/%p}\n",
			    prefix, "AT_PHDR", auxv[i].a_un.a_ptr);
			break;
		case AT_PHENT:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_PHENT/%ld}\n",
			    prefix, "AT_PHENT", (long)auxv[i].a_un.a_val);
			break;
		case AT_PHNUM:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_PHNUM/%ld}\n",
			    prefix, "AT_PHNUM", (long)auxv[i].a_un.a_val);
			break;
		case AT_PAGESZ:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_PAGESZ/%ld}\n",
			    prefix, "AT_PAGESZ", (long)auxv[i].a_un.a_val);
			break;
		case AT_BASE:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_BASE/%p}\n",
			    prefix, "AT_BASE", auxv[i].a_un.a_ptr);
			break;
		case AT_FLAGS:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_FLAGS/%#lx}\n",
			    prefix, "AT_FLAGS", (u_long)auxv[i].a_un.a_val);
			break;
		case AT_ENTRY:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_ENTRY/%p}\n",
			    prefix, "AT_ENTRY", auxv[i].a_un.a_ptr);
			break;
#ifdef AT_NOTELF
		case AT_NOTELF:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_NOTELF/%ld}\n",
			    prefix, "AT_NOTELF", (long)auxv[i].a_un.a_val);
			break;
#endif
#ifdef AT_UID
		case AT_UID:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_UID/%ld}\n",
			    prefix, "AT_UID", (long)auxv[i].a_un.a_val);
			break;
#endif
#ifdef AT_EUID
		case AT_EUID:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_EUID/%ld}\n",
			    prefix, "AT_EUID", (long)auxv[i].a_un.a_val);
			break;
#endif
#ifdef AT_GID
		case AT_GID:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_GID/%ld}\n",
			    prefix, "AT_GID", (long)auxv[i].a_un.a_val);
			break;
#endif
#ifdef AT_EGID
		case AT_EGID:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_EGID/%ld}\n",
			    prefix, "AT_EGID", (long)auxv[i].a_un.a_val);
			break;
#endif
		case AT_EXECPATH:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_EXECPATH/%p}\n",
			    prefix, "AT_EXECPATH", auxv[i].a_un.a_ptr);
			break;
		case AT_CANARY:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_CANARY/%p}\n",
			    prefix, "AT_CANARY", auxv[i].a_un.a_ptr);
			break;
		case AT_CANARYLEN:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_CANARYLEN/%ld}\n",
			    prefix, "AT_CANARYLEN", (long)auxv[i].a_un.a_val);
			break;
		case AT_OSRELDATE:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_OSRELDATE/%ld}\n",
			    prefix, "AT_OSRELDATE", (long)auxv[i].a_un.a_val);
			break;
		case AT_NCPUS:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_NCPUS/%ld}\n",
			    prefix, "AT_NCPUS", (long)auxv[i].a_un.a_val);
			break;
		case AT_PAGESIZES:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_PAGESIZES/%p}\n",
			    prefix, "AT_PAGESIZES", auxv[i].a_un.a_ptr);
			break;
		case AT_PAGESIZESLEN:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}"
			    "{:AT_PAGESIZESLEN/%ld}\n", prefix,
			    "AT_PAGESIZESLEN", (long)auxv[i].a_un.a_val);
			break;
		case AT_STACKPROT:
			if ((auxv[i].a_un.a_val & VM_PROT_EXECUTE) != 0)
				xo_emit("{dw:/%s}{Lw:/%-16s/%s}"
				    "{:AT_STACKPROT/%s}\n", prefix,
				    "AT_STACKPROT", "EXECUTABLE");
			else
				xo_emit("{dw:/%s}{Lw:/%-16s/%s}"
				    "{:AT_STACKPROT/%s}\n", prefix,
				    "AT_STACKPROT", "NONEXECUTABLE");
			break;
#ifdef AT_TIMEKEEP
		case AT_TIMEKEEP:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_TIMEKEEP/%p}\n",
			    prefix, "AT_TIMEKEEP", auxv[i].a_un.a_ptr);
			break;
#endif
#ifdef AT_EHDRFLAGS
		case AT_EHDRFLAGS:
			xo_emit("{dw:/%s}{Lw:/%-16s/%s}{:AT_EHDRFLAGS/%#lx}\n",
			    prefix, "AT_EHDRFLAGS", (u_long)auxv[i].a_un.a_val);
			break;
#endif
		default:
			xo_emit("{dw:/%s}{Lw:/%16ld/%ld}{:UNKNOWN/%#lx}\n",
			    prefix, auxv[i].a_type, auxv[i].a_un.a_val);
			break;
		}
	}
	xo_emit("\n");
	procstat_freeauxv(procstat, auxv);
}

