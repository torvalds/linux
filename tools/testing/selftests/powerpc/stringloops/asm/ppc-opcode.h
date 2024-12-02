/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2009 Freescale Semiconductor, Inc.
 *
 * provides masks and opcode images for use by code generation, emulation
 * and for instructions that older assemblers might not know about
 */
#ifndef _ASM_POWERPC_PPC_OPCODE_H
#define _ASM_POWERPC_PPC_OPCODE_H


#  define stringify_in_c(...)	__VA_ARGS__
#  define ASM_CONST(x)		x


#define PPC_INST_VCMPEQUD_RC		0x100000c7
#define PPC_INST_VCMPEQUB_RC		0x10000006

#define __PPC_RC21     (0x1 << 10)

/* macros to insert fields into opcodes */
#define ___PPC_RA(a)	(((a) & 0x1f) << 16)
#define ___PPC_RB(b)	(((b) & 0x1f) << 11)
#define ___PPC_RS(s)	(((s) & 0x1f) << 21)
#define ___PPC_RT(t)	___PPC_RS(t)

#define VCMPEQUD_RC(vrt, vra, vrb)	stringify_in_c(.long PPC_INST_VCMPEQUD_RC | \
			      ___PPC_RT(vrt) | ___PPC_RA(vra) | \
			      ___PPC_RB(vrb) | __PPC_RC21)

#define VCMPEQUB_RC(vrt, vra, vrb)	stringify_in_c(.long PPC_INST_VCMPEQUB_RC | \
			      ___PPC_RT(vrt) | ___PPC_RA(vra) | \
			      ___PPC_RB(vrb) | __PPC_RC21)

#endif /* _ASM_POWERPC_PPC_OPCODE_H */
