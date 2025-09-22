/*	$OpenBSD: aicasm_insformat.h,v 1.4 2003/12/24 23:27:55 krw Exp $	*/
/*	$NetBSD: aicasm_insformat.h,v 1.1 2003/04/19 19:26:11 fvdl Exp $	*/

/*
 * Instruction formats for the sequencer program downloaded to
 * Aic7xxx SCSI host adapters
 *
 * Copyright (c) 1997, 1998, 2000 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/aic7xxx/aicasm/aicasm_insformat.h,v 1.8 2002/11/27 07:06:43 scottl Exp $
 */

struct ins_format1 {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	immediate	: 8,
			source		: 9,
			destination	: 9,
			ret		: 1,
			opcode		: 4,
			parity		: 1;
#else
	uint32_t	parity		: 1,
			opcode		: 4,
			ret		: 1,
			destination	: 9,
			source		: 9,
			immediate	: 8;
#endif
};

struct ins_format2 {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	shift_control	: 8,
			source		: 9,
			destination	: 9,
			ret		: 1,
			opcode		: 4,
			parity		: 1;
#else
	uint32_t	parity		: 1,
			opcode		: 4,
			ret		: 1,
			destination	: 9,
			source		: 9,
			shift_control	: 8;
#endif
};

struct ins_format3 {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	immediate	: 8,
			source		: 9,
			address		: 10,
			opcode		: 4,
			parity		: 1;
#else
	uint32_t	parity		: 1,
			opcode		: 4,
			address		: 10,
			source		: 9,
			immediate	: 8;
#endif
};

union ins_formats {
		struct ins_format1 format1;
		struct ins_format2 format2;
		struct ins_format3 format3;
		uint8_t		   bytes[4];
		uint32_t	   integer;
};
struct instruction {
	union	ins_formats format;
	u_int	srcline;
	struct symbol *patch_label;
	TAILQ_ENTRY(instruction) links;
};

#define	AIC_OP_OR	0x0
#define	AIC_OP_AND	0x1
#define AIC_OP_XOR	0x2
#define	AIC_OP_ADD	0x3
#define	AIC_OP_ADC	0x4
#define	AIC_OP_ROL	0x5
#define	AIC_OP_BMOV	0x6

#define	AIC_OP_JMP	0x8
#define AIC_OP_JC	0x9
#define AIC_OP_JNC	0xa
#define AIC_OP_CALL	0xb
#define	AIC_OP_JNE	0xc
#define	AIC_OP_JNZ	0xd
#define	AIC_OP_JE	0xe
#define	AIC_OP_JZ	0xf

/* Pseudo Ops */
#define	AIC_OP_SHL	0x10
#define	AIC_OP_SHR	0x20
#define	AIC_OP_ROR	0x30
