/*	$OpenBSD: softintr.c,v 1.23 2025/05/10 10:01:03 visa Exp $	*/
/*	$NetBSD: softintr.c,v 1.2 2003/07/15 00:24:39 lukem Exp $	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>

#include <machine/intr.h>
#ifdef MULTIPROCESSOR
#include <mips64/mips_cpu.h>
#endif

void
softintr(int si)
{
	atomic_setbits_int(&curcpu()->ci_softpending, 1 << si);
}

void
dosoftint()
{
	struct cpu_info *ci = curcpu();
	int sir, q, mask;
#ifdef MULTIPROCESSOR
	register_t sr;

	/* Enable interrupts */
	sr = getsr();
	ENABLEIPI();
	__mp_lock(&kernel_lock);
#endif

	while ((sir = ci->ci_softpending) != 0) {
		atomic_clearbits_int(&ci->ci_softpending, sir);

		for (q = NSOFTINTR - 1; q >= 0; q--) {
			mask = 1 << q;
			if (sir & mask)
				softintr_dispatch(q);
		}
	}

#ifdef MULTIPROCESSOR
	__mp_unlock(&kernel_lock);
	setsr(sr);
#endif
}
