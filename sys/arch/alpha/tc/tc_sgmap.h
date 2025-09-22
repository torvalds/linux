/* $OpenBSD: tc_sgmap.h,v 1.2 2008/06/26 05:42:09 ray Exp $ */
/* $NetBSD: tc_sgmap.h,v 1.2 1997/06/07 00:02:20 thorpej Exp $ */

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define	SGMAP_TYPE		tc_sgmap
#define	SGMAP_PTE_TYPE		u_int32_t
#define	SGMAP_PTE_SPACING	2

/*
 * A TurboChannel SGMAP page table entry looks like this:
 *
 * 31                    23  22  21 20           4 3    0
 * |     Discarded     | V | F | P | Page address | UNP |
 *
 * The page address is bits <29:13> of the physical address of the
 * page.  The V bit is set if the PTE holds a valid mapping.
 * The F (funny) bit forces a parity error.  The P bit is a
 * hardware-generated parity bit.
 */
#define	SGPTE_PGADDR_SHIFT	9
#define	SGPTE_VALID		0x00800000

#include <alpha/dev/sgmapvar.h>
#include <alpha/dev/sgmap_typedep.h>
