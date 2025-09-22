/*	$OpenBSD: fiq.h,v 1.1 2004/02/01 05:09:49 drahn Exp $	*/
/*	$NetBSD: fiq.h,v 1.1 2001/12/20 01:20:23 thorpej Exp $	*/

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

#ifndef _ARM_FIQ_H_
#define	_ARM_FIQ_H_

#include <sys/queue.h>

struct fiqregs {
	u_int	fr_r8;			/* FIQ mode r8 */
	u_int	fr_r9;			/* FIQ mode r9 */
	u_int	fr_r10;			/* FIQ mode r10 */
	u_int	fr_r11;			/* FIQ mode r11 */
	u_int	fr_r12;			/* FIQ mode r12 */
	u_int	fr_r13;			/* FIQ mode r13 */
};

struct fiqhandler {
	TAILQ_ENTRY(fiqhandler) fh_list;/* link in the FIQ handler stack */
	void	*fh_func;		/* FIQ handler routine */
	size_t	fh_size;		/* size of FIQ handler */
	int	fh_flags;		/* flags; see below */
	struct fiqregs *fh_regs;	/* pointer to regs structure */
};

#define	FH_CANPUSH	0x01	/* can push this handler out of the way */

int	fiq_claim(struct fiqhandler *);
void	fiq_release(struct fiqhandler *);

void	fiq_getregs(struct fiqregs *);
void	fiq_setregs(struct fiqregs *);

#endif /* _ARM_FIQ_H_ */
