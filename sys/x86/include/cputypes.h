/*-
 * Copyright (c) 1993 Christopher G. Demetriou
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _X86_CPUTYPES_H_
#define	_X86_CPUTYPES_H_

/*
 * Vendors of processor.
 */
#define	CPU_VENDOR_NSC		0x100b		/* NSC */
#define	CPU_VENDOR_IBM		0x1014		/* IBM */
#define	CPU_VENDOR_AMD		0x1022		/* AMD */
#define	CPU_VENDOR_SIS		0x1039		/* SiS */
#define	CPU_VENDOR_UMC		0x1060		/* UMC */
#define	CPU_VENDOR_NEXGEN	0x1074		/* Nexgen */
#define	CPU_VENDOR_CYRIX	0x1078		/* Cyrix */
#define	CPU_VENDOR_IDT		0x111d		/* Centaur/IDT/VIA */
#define	CPU_VENDOR_TRANSMETA	0x1279		/* Transmeta */
#define	CPU_VENDOR_INTEL	0x8086		/* Intel */
#define	CPU_VENDOR_RISE		0xdead2bad	/* Rise */
#define	CPU_VENDOR_CENTAUR	CPU_VENDOR_IDT

#endif /* !_X86_CPUTYPES_H_ */
