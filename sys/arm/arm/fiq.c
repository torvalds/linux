/*	$NetBSD: fiq.c,v 1.5 2002/04/03 23:33:27 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001, 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/armreg.h>
#include <machine/cpufunc.h>
#include <machine/fiq.h>
#include <vm/vm.h>
#include <machine/pcb.h>
#include <vm/pmap.h>
#include <machine/cpu.h>

TAILQ_HEAD(, fiqhandler) fiqhandler_stack =
    TAILQ_HEAD_INITIALIZER(fiqhandler_stack);

extern char *fiq_nullhandler_code;
extern uint32_t fiq_nullhandler_size;

/*
 * fiq_installhandler:
 *
 *	Actually install the FIQ handler down at the FIQ vector.
 *
 *	The FIQ vector is fixed by the hardware definition as the
 *	seventh 32-bit word in the vector page.
 *
 *	Note: If the FIQ is invoked via an extra layer of
 *	indirection, the actual FIQ code store lives in the
 *	data segment, so there is no need to manipulate
 *	the vector page's protection.
 */
static void
fiq_installhandler(void *func, size_t size)
{
	const uint32_t fiqvector = 7 * sizeof(uint32_t);

#if __ARM_ARCH < 6 && !defined(__ARM_FIQ_INDIRECT)
	vector_page_setprot(VM_PROT_READ|VM_PROT_WRITE);
#endif

	memcpy((void *)(vector_page + fiqvector), func, size);

#if __ARM_ARCH < 6 && !defined(__ARM_FIQ_INDIRECT)
	vector_page_setprot(VM_PROT_READ);
#endif
	icache_sync((vm_offset_t) fiqvector, size);
}

/*
 * fiq_claim:
 *
 *	Claim the FIQ vector.
 */
int
fiq_claim(struct fiqhandler *fh)
{
	struct fiqhandler *ofh;
	u_int oldirqstate;
	int error = 0;

	if (fh->fh_size > 0x100)
		return (EFBIG);

	oldirqstate = disable_interrupts(PSR_F);

	if ((ofh = TAILQ_FIRST(&fiqhandler_stack)) != NULL) {
		if ((ofh->fh_flags & FH_CANPUSH) == 0) {
			error = EBUSY;
			goto out;
		}

		/* Save the previous FIQ handler's registers. */
		if (ofh->fh_regs != NULL)
			fiq_getregs(ofh->fh_regs);
	}

	/* Set FIQ mode registers to ours. */
	if (fh->fh_regs != NULL)
		fiq_setregs(fh->fh_regs);

	TAILQ_INSERT_HEAD(&fiqhandler_stack, fh, fh_list);

	/* Now copy the actual handler into place. */
	fiq_installhandler(fh->fh_func, fh->fh_size);

	/* Make sure FIQs are enabled when we return. */
	oldirqstate &= ~PSR_F;

 out:
	restore_interrupts(oldirqstate);
	return (error);
}

/*
 * fiq_release:
 *
 *	Release the FIQ vector.
 */
void
fiq_release(struct fiqhandler *fh)
{
	u_int oldirqstate;
	struct fiqhandler *ofh;

	oldirqstate = disable_interrupts(PSR_F);

	/*
	 * If we are the currently active FIQ handler, then we
	 * need to save our registers and pop the next one back
	 * into the vector.
	 */
	if (fh == TAILQ_FIRST(&fiqhandler_stack)) {
		if (fh->fh_regs != NULL)
			fiq_getregs(fh->fh_regs);
		TAILQ_REMOVE(&fiqhandler_stack, fh, fh_list);
		if ((ofh = TAILQ_FIRST(&fiqhandler_stack)) != NULL) {
			if (ofh->fh_regs != NULL)
				fiq_setregs(ofh->fh_regs);
			fiq_installhandler(ofh->fh_func, ofh->fh_size);
		}
	} else
		TAILQ_REMOVE(&fiqhandler_stack, fh, fh_list);

	if (TAILQ_FIRST(&fiqhandler_stack) == NULL) {
		/* Copy the NULL handler back down into the vector. */
		fiq_installhandler(fiq_nullhandler_code, fiq_nullhandler_size);

		/* Make sure FIQs are disabled when we return. */
		oldirqstate |= PSR_F;
	}

	restore_interrupts(oldirqstate);
}
