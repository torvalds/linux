/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Portions Copyright 2006-2008 John Birrell jb@freebsd.org
 * Portions Copyright 2013 Justin Hibbits jhibbits@freebsd.org
 *
 * $FreeBSD$
 *
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/dtrace.h>
#include <machine/md_var.h>

#include "fbt.h"

#define FBT_PATCHVAL		0x7ffff808
#define FBT_MFLR_R0		0x7c0802a6
#define FBT_MTLR_R0		0x7c0803a6
#define FBT_BLR			0x4e800020
#define FBT_BCTR		0x4e800030
#define FBT_BRANCH		0x48000000
#define FBT_BR_MASK		0x03fffffc
#define FBT_IS_JUMP(instr)	((instr & ~FBT_BR_MASK) == FBT_BRANCH)

#define	FBT_ENTRY	"entry"
#define	FBT_RETURN	"return"
#define	FBT_AFRAMES	7

int
fbt_invop(uintptr_t addr, struct trapframe *frame, uintptr_t rval)
{
	solaris_cpu_t *cpu = &solaris_cpu[curcpu];
	fbt_probe_t *fbt = fbt_probetab[FBT_ADDR2NDX(addr)];
	uintptr_t tmp;

	for (; fbt != NULL; fbt = fbt->fbtp_hashnext) {
		if ((uintptr_t)fbt->fbtp_patchpoint == addr) {
			if (fbt->fbtp_roffset == 0) {
				cpu->cpu_dtrace_caller = addr;

				dtrace_probe(fbt->fbtp_id, frame->fixreg[3],
				    frame->fixreg[4], frame->fixreg[5],
				    frame->fixreg[6], frame->fixreg[7]);

				cpu->cpu_dtrace_caller = 0;
			} else {

				dtrace_probe(fbt->fbtp_id, fbt->fbtp_roffset,
				    rval, 0, 0, 0);
				/*
				 * The caller doesn't have the fbt item, so
				 * fixup tail calls here.
				 */
				if (fbt->fbtp_rval == DTRACE_INVOP_JUMP) {
					frame->srr0 = (uintptr_t)fbt->fbtp_patchpoint;
					tmp = fbt->fbtp_savedval & FBT_BR_MASK;
					/* Sign extend. */
					if (tmp & 0x02000000)
#ifdef __powerpc64__
						tmp |= 0xfffffffffc000000ULL;
#else
						tmp |= 0xfc000000UL;
#endif
					frame->srr0 += tmp;
				}
				cpu->cpu_dtrace_caller = 0;
			}

			return (fbt->fbtp_rval);
		}
	}

	return (0);
}

void
fbt_patch_tracepoint(fbt_probe_t *fbt, fbt_patchval_t val)
{

	*fbt->fbtp_patchpoint = val;
	__syncicache(fbt->fbtp_patchpoint, 4);
}

int
fbt_provide_module_function(linker_file_t lf, int symindx,
    linker_symval_t *symval, void *opaque)
{
	char *modname = opaque;
	const char *name = symval->name;
	fbt_probe_t *fbt, *retfbt;
	int j;
	uint32_t *instr, *limit;

#ifdef __powerpc64__
	/*
	 * PowerPC64 uses '.' prefixes on symbol names, ignore it, but only
	 * allow symbols with the '.' prefix, so that we don't get the function
	 * descriptor instead.
	 */
	if (name[0] == '.')
		name++;
	else
		return (0);
#endif

	if (fbt_excluded(name))
		return (0);

	instr = (uint32_t *) symval->value;
	limit = (uint32_t *) (symval->value + symval->size);

	for (; instr < limit; instr++)
		if (*instr == FBT_MFLR_R0)
			break;

	if (*instr != FBT_MFLR_R0)
		return (0);

	fbt = malloc(sizeof (fbt_probe_t), M_FBT, M_WAITOK | M_ZERO);
	fbt->fbtp_name = name;
	fbt->fbtp_id = dtrace_probe_create(fbt_id, modname,
	    name, FBT_ENTRY, FBT_AFRAMES, fbt);
	fbt->fbtp_patchpoint = instr;
	fbt->fbtp_ctl = lf;
	fbt->fbtp_loadcnt = lf->loadcnt;
	fbt->fbtp_savedval = *instr;
	fbt->fbtp_patchval = FBT_PATCHVAL;
	fbt->fbtp_rval = DTRACE_INVOP_MFLR_R0;
	fbt->fbtp_symindx = symindx;

	fbt->fbtp_hashnext = fbt_probetab[FBT_ADDR2NDX(instr)];
	fbt_probetab[FBT_ADDR2NDX(instr)] = fbt;

	lf->fbt_nentries++;

	retfbt = NULL;
again:
	if (instr >= limit)
		return (0);

	/*
	 * We (desperately) want to avoid erroneously instrumenting a
	 * jump table. To determine if we're looking at a true instruction
	 * sequence or an inline jump table that happens to contain the same
	 * byte sequences, we resort to some heuristic sleeze:  we treat this
	 * instruction as being contained within a pointer, and see if that
	 * pointer points to within the body of the function.  If it does, we
	 * refuse to instrument it.
	 */
	{
		uint32_t *ptr;

		ptr = *(uint32_t **)instr;

		if (ptr >= (uint32_t *) symval->value && ptr < limit) {
			instr++;
			goto again;
		}
	}

	if (*instr != FBT_MTLR_R0) {
		instr++;
		goto again;
	}

	instr++;

	for (j = 0; j < 12 && instr < limit; j++, instr++) {
		if ((*instr == FBT_BCTR) || (*instr == FBT_BLR) ||
		    FBT_IS_JUMP(*instr))
			break;
	}

	if (!(*instr == FBT_BCTR || *instr == FBT_BLR || FBT_IS_JUMP(*instr)))
		goto again;

	/*
	 * We have a winner!
	 */
	fbt = malloc(sizeof (fbt_probe_t), M_FBT, M_WAITOK | M_ZERO);
	fbt->fbtp_name = name;

	if (retfbt == NULL) {
		fbt->fbtp_id = dtrace_probe_create(fbt_id, modname,
		    name, FBT_RETURN, FBT_AFRAMES, fbt);
	} else {
		retfbt->fbtp_probenext = fbt;
		fbt->fbtp_id = retfbt->fbtp_id;
	}

	retfbt = fbt;
	fbt->fbtp_patchpoint = instr;
	fbt->fbtp_ctl = lf;
	fbt->fbtp_loadcnt = lf->loadcnt;
	fbt->fbtp_symindx = symindx;

	if (*instr == FBT_BCTR)
		fbt->fbtp_rval = DTRACE_INVOP_BCTR;
	else if (*instr == FBT_BLR)
		fbt->fbtp_rval = DTRACE_INVOP_BLR;
	else
		fbt->fbtp_rval = DTRACE_INVOP_JUMP;

	fbt->fbtp_roffset =
	    (uintptr_t)((uint8_t *)instr - (uint8_t *)symval->value);

	fbt->fbtp_savedval = *instr;
	fbt->fbtp_patchval = FBT_PATCHVAL;
	fbt->fbtp_hashnext = fbt_probetab[FBT_ADDR2NDX(instr)];
	fbt_probetab[FBT_ADDR2NDX(instr)] = fbt;

	lf->fbt_nentries++;

	instr += 4;
	goto again;
}
