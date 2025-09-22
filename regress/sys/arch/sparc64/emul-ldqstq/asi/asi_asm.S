/*	$OpenBSD: asi_asm.S,v 1.3 2024/08/06 05:39:48 claudio Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>
#define _LOCORE
#include <machine/ctlreg.h>

/* void asm_ldq_asi(int asi, struct fpquad *r)	%f0 = *r */
ENTRY(asm_ldqa_asi)
	rd	%asi, %o2
	wr	%o0, 0, %asi
	ldqa	[%o1] %asi, %f0
	retl
	 wr	%o2, 0, %asi

/* void asm_ldq_asi(int asi, struct fpquad *)	*r = %f0 */
ENTRY(asm_stqa_asi)
	rd	%asi, %o2
	wr	%o0, 0, %asi
	stqa	%f0, [%o1] %asi
	retl
	 wr	%o2, 0, %asi

ENTRY(asm_stqa_primary)
	retl
	 stqa %f0, [%o0] ASI_PRIMARY

ENTRY(asm_ldqa_primary)
	retl
	 ldqa [%o0] ASI_PRIMARY, %f0

ENTRY(asm_stqa_secondary)
	retl
	 stqa %f0, [%o0] ASI_SECONDARY

ENTRY(asm_ldqa_secondary)
	retl
	 ldqa [%o0] ASI_SECONDARY, %f0

ENTRY(asm_stqa_primary_nofault)
	retl
	 stqa %f0, [%o0] ASI_PRIMARY_NOFAULT

ENTRY(asm_ldqa_primary_nofault)
	retl
	 ldqa [%o0] ASI_PRIMARY_NOFAULT, %f0

ENTRY(asm_stqa_secondary_nofault)
	retl
	 stqa %f0, [%o0] ASI_SECONDARY_NOFAULT

ENTRY(asm_ldqa_secondary_nofault)
	retl
	 ldqa [%o0] ASI_SECONDARY_NOFAULT, %f0

ENTRY(asm_stqa_primary_little)
	retl
	 stqa %f0, [%o0] ASI_PRIMARY_LITTLE

ENTRY(asm_ldqa_primary_little)
	retl
	 ldqa [%o0] ASI_PRIMARY_LITTLE, %f0

ENTRY(asm_stqa_secondary_little)
	retl
	 stqa %f0, [%o0] ASI_SECONDARY_LITTLE

ENTRY(asm_ldqa_secondary_little)
	retl
	 ldqa [%o0] ASI_SECONDARY_LITTLE, %f0

ENTRY(asm_stqa_primary_nofault_little)
	retl
	 stqa %f0, [%o0] ASI_PRIMARY_NOFAULT_LITTLE

ENTRY(asm_ldqa_primary_nofault_little)
	retl
	 ldqa [%o0] ASI_PRIMARY_NOFAULT_LITTLE, %f0

ENTRY(asm_stqa_secondary_nofault_little)
	retl
	 stqa %f0, [%o0] ASI_SECONDARY_NOFAULT_LITTLE

ENTRY(asm_ldqa_secondary_nofault_little)
	retl
	 ldqa [%o0] ASI_SECONDARY_NOFAULT_LITTLE, %f0

