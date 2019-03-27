/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 David S. Miller, davem@nadzieja.rutgers.edu
 * Copyright (c) 1995 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by David Miller.
 * 4. The name of the author may not be used to endorse or promote products
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
 *	from: NetBSD: db_disasm.c,v 1.9 2000/08/16 11:29:42 pk Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>

#include <machine/db_machdep.h>
#include <machine/instr.h>

#define SIGN(v)			(((v)<0)?"-":"")

/*
 * All Sparc instructions are 32-bits, with the one exception being
 * the set instruction which is actually a macro which expands into
 * two instructions...
 *
 * There are 5 different fields that can be used to identify which
 * operation is encoded into a particular 32-bit insn. There are 3
 * formats for instuctions, which one being used is determined by
 * bits 30-31 of the insn. Here are the bit fields and their names:
 *
 * 1100 0000 0000 0000 0000 0000 0000 0000 op field, determines format
 * 0000 0001 1100 0000 0000 0000 0000 0000 op2 field, format 2 only
 * 0000 0001 1111 1000 0000 0000 0000 0000 op3 field, format 3 only
 * 0000 0000 0000 0000 0010 0000 0000 0000 f3i bit, format 3 only
 * 0000 0000 0000 0000 0001 0000 0000 0000 X bit, format 3 only
 */

/* FORMAT macros used in sparc_i table to decode each opcode */
#define FORMAT1(a)	(EIF_OP(a))
#define FORMAT2(a,b)	(EIF_OP(a) | EIF_F2_OP2(b))
/* For formats 3 and 4 */
#define FORMAT3(a,b,c)	(EIF_OP(a) | EIF_F3_OP3(b) | EIF_F3_I(c))
#define FORMAT3F(a,b,c)	(EIF_OP(a) | EIF_F3_OP3(b) | EIF_F3_OPF(c))

/* Helper macros to construct OP3 & OPF */
#define OP3_X(x,y)	((((x) & 3) << 4) | ((y) & 0xf))
#define OPF_X(x,y)	((((x) & 0x1f) << 4) | ((y) & 0xf))

/* COND condition codes field... */
#define COND2(y,x)	(((((y)<<4) & 1)|((x) & 0xf)) << 14)

struct sparc_insn {
	  unsigned int match;
	  const char* name;
	  const char* format;
};

static const char *const regs[] = {
	"g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
	"o0", "o1", "o2", "o3", "o4", "o5", "sp", "o7",
	"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
	"i0", "i1", "i2", "i3", "i4", "i5", "fp", "i7"
};

static const char *const priv_regs[] = {
	"tpc", "tnpc", "tstate", "tt", "tick", "tba", "pstate", "tl",
	"pil", "cwp", "cansave", "canrestore", "cleanwin", "otherwin",
	"wstate", "fq",
	"", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "ver"
};

static const char *const state_regs[] = {
	"y", "", "ccr", "asi", "tick", "pc", "fprs", "asr",
	"", "", "", "", "", "", "", "",
	"pcr", "pic", "dcr", "gsr", "set_softint", "clr_softint", "softint",
	"tick_cmpr", "sys_tick", "sys_tick_cmpr", "", "", "", "", "", "", ""
};

static const char *const ccodes[] = {
	"fcc0", "fcc1", "fcc2", "fcc3", "icc", "", "xcc", ""
};

static const char *const prefetch[] = {
	"n_reads", "one_read", "n_writes", "one_write", "page"
};


/* The sparc instruction table has a format field which tells what
   the operand structure for this instruction is. Here are the codes:

Modifiers (nust be first):
	a -- opcode has annul bit
	p -- opcode has branch prediction bit

Codes:
        1 -- source register operand stored in rs1
	2 -- source register operand stored in rs2
	d -- destination register operand stored in rd
	3 -- floating source register in rs1
	4 -- floating source register in rs2
	e -- floating destination register in rd
	i -- 13-bit immediate value stored in simm13
	j -- 11-bit immediate value stored in simm11
	l -- displacement using d16lo and d16hi
	m -- 22-bit fcc displacement value
	n -- 30-bit displacement used in call insns
	o -- %fcc number specified in cc1 and cc0 fields
	p -- address computed by the contents of rs1+rs2
	q -- address computed by the contents of rs1+simm13
	r -- prefetch
	s -- %asi is implicit in the insn, rs1 value not used
	t -- immediate 8-bit asi value
	u -- 19-bit fcc displacement value
	5 -- hard register, %fsr lower-half
	6 -- hard register, %fsr all
	7 -- [reg_addr rs1+rs2] imm_asi
	8 -- [reg_addr rs1+simm13] %asi
	9 -- logical or of the cmask and mmask fields (membar insn)
	0 -- icc or xcc condition codes register
	. -- %fcc, %icc, or %xcc in opf_cc field
	r -- prefection function stored in fcn field
	A -- privileged register encoded in rs1
	B -- state register encoded in rs1
	C -- %hi(value) where value is stored in imm22 field
	D -- 32-bit shift count in shcnt32
	E -- 64-bit shift count in shcnt64
	F -- software trap number stored in sw_trap
	G -- privileged register encoded in rd
	H -- state register encoded in rd

V8 only:
	Y -- write y register
	P -- write psr register
	T -- write tbr register
	W -- write wim register
*/


static const struct sparc_insn sparc_i[] = {

	/*
	 * Format 1: Call
	 */
	{(FORMAT1(1)), "call", "n"},

	/*
	 * Format 0: Sethi & Branches
	 */
	/* Illegal Instruction Trap */
	{(FORMAT2(0, 0)), "illtrap", "m"},

	/* Note: if imm22 is zero then this is actually a "nop" grrr... */
	{(FORMAT2(0, 0x4)), "sethi", "Cd"},

	/* Branch on Integer Co`ndition Codes "Bicc" */
	{(FORMAT2(0, 2) | EIF_F2_COND(8)), "ba", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(0)), "bn", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(9)), "bne", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(1)), "be", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(10)), "bg", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(2)), "ble", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(11)), "bge", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(3)), "bl", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(12)), "bgu", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(4)), "bleu", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(13)), "bcc", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(5)), "bcs", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(14)), "bpos", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(6)), "bneg", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(15)), "bvc", "a,m"},
	{(FORMAT2(0, 2) | EIF_F2_COND(7)), "bvs", "a,m"},

	/* Branch on Integer Condition Codes with Prediction "BPcc" */
	{(FORMAT2(0, 1) | EIF_F2_COND(8)), "ba", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(0)), "bn", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(9)), "bne", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(1)), "be", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(10)), "bg", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(2)), "ble", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(11)), "bge", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(3)), "bl", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(12)), "bgu", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(4)), "bleu", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(13)), "bcc", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(5)), "bcs", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(14)), "bpos", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(6)), "bneg", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(15)), "bvc", "ap,u"},
	{(FORMAT2(0, 1) | EIF_F2_COND(7)), "bvs", "ap,u"},

	/* Branch on Integer Register with Prediction "BPr" */
	{(FORMAT2(0, 3) | EIF_F2_RCOND(1)), "brz", "ap,1l"},
	{(FORMAT2(0, 3) | EIF_F2_A(1) | EIF_F2_P(1) |
	    EIF_F2_RCOND(2)), "brlex", "ap,1l"},
	{(FORMAT2(0, 3) | EIF_F2_RCOND(3)), "brlz", "ap,1l"},
	{(FORMAT2(0, 3) | EIF_F2_RCOND(5)), "brnz", "ap,1l"},
	{(FORMAT2(0, 3) | EIF_F2_RCOND(6)), "brgz", "ap,1l"},
	{(FORMAT2(0, 3) | EIF_F2_RCOND(7)), "brgez", "ap,1l"},

	/* Branch on Floating-Point Condition Codes with Prediction "FBPfcc" */
	{(FORMAT2(0, 5) | EIF_F2_COND(8)), "fba", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(0)), "fbn", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(7)), "fbu", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(6)), "fbg", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(5)), "fbug", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(4)), "fbl", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(3)), "fbul", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(2)), "fblg", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(1)), "fbne", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(9)), "fbe", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(10)), "fbue", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(11)), "fbge", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(12)), "fbuge", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(13)), "fble", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(14)), "fbule", "ap,m"},
	{(FORMAT2(0, 5) | EIF_F2_COND(15)), "fbo", "ap,m"},

	/* Branch on Floating-Point Condition Codes "FBfcc" */
	{(FORMAT2(0, 6) | EIF_F2_COND(8)), "fba", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(0)), "fbn", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(7)), "fbu", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(6)), "fbg", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(5)), "fbug", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(4)), "fbl", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(3)), "fbul", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(2)), "fblg", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(1)), "fbne", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(9)), "fbe", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(10)), "fbue", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(11)), "fbge", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(12)), "fbuge", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(13)), "fble", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(14)), "fbule", "a,m"},
	{(FORMAT2(0, 6) | EIF_F2_COND(15)), "fbo", "a,m"},



	/*
	 * Format 3/2: Arithmetic & misc (table 32, appendix E)
	 */
	{FORMAT3(2, OP3_X(0,0), 0), "add", "12d"},
	{FORMAT3(2, OP3_X(0,0), 1), "add", "1id"},
	{FORMAT3(2, OP3_X(1,0), 0), "addcc", "12d"},
	{FORMAT3(2, OP3_X(1,0), 1), "addcc", "1id"},
	{FORMAT3(2, OP3_X(2,0), 0), "taddcc", "12d"},
	{FORMAT3(2, OP3_X(2,0), 1), "taddcc", "1id"},
	{(FORMAT3(2, 0x30, 1) | EIF_F3_RD(0xf)), "sir", "i"},
	{FORMAT3(2, OP3_X(3,0), 0), "wr", "12H"},
	{FORMAT3(2, OP3_X(3,0), 1), "wr", "1iH"},

	{FORMAT3(2, OP3_X(0,1), 0), "and", "12d"},
	{FORMAT3(2, OP3_X(0,1), 1), "and", "1id"},
	{FORMAT3(2, OP3_X(1,1), 0), "andcc", "12d"},
	{FORMAT3(2, OP3_X(1,1), 1), "andcc", "1id"},
	{FORMAT3(2, OP3_X(2,1), 0), "tsubcc", "12d"},
	{FORMAT3(2, OP3_X(2,1), 1), "tsubcc", "1id"},
	{FORMAT3(2, OP3_X(3,1), 0), "saved", ""},
	{FORMAT3(2, OP3_X(3,1), 0) | EIF_F3_FCN(1), "restored", ""},

	{FORMAT3(2, OP3_X(0,2), 0), "or", "12d"},
	{FORMAT3(2, OP3_X(0,2), 1), "or", "1id"},
	{FORMAT3(2, OP3_X(1,2), 0), "orcc", "12d"},
	{FORMAT3(2, OP3_X(1,2), 1), "orcc", "1id"},
	{FORMAT3(2, OP3_X(2,2), 0), "taddcctv", "12d"},
	{FORMAT3(2, OP3_X(2,2), 1), "taddcctv", "1id"},
	{FORMAT3(2, OP3_X(3,2), 0), "wrpr", "12G"},
	{FORMAT3(2, OP3_X(3,2), 1), "wrpr", "1iG"},

	{FORMAT3(2, OP3_X(0,3), 0), "xor", "12d"},
	{FORMAT3(2, OP3_X(0,3), 1), "xor", "1id"},
	{FORMAT3(2, OP3_X(1,3), 0), "xorcc", "12d"},
	{FORMAT3(2, OP3_X(1,3), 1), "xorcc", "1id"},
	{FORMAT3(2, OP3_X(2,3), 0), "tsubcctv", "12d"},
	{FORMAT3(2, OP3_X(2,3), 1), "tsubcctv", "1id"},
	{FORMAT3(2, OP3_X(3,3), 0), "UNDEFINED", ""},

	{FORMAT3(2, OP3_X(0,4), 0), "sub", "12d"},
	{FORMAT3(2, OP3_X(0,4), 1), "sub", "1id"},
	{FORMAT3(2, OP3_X(1,4), 0), "subcc", "12d"},
	{FORMAT3(2, OP3_X(1,4), 1), "subcc", "1id"},
	{FORMAT3(2, OP3_X(2,4), 0), "mulscc", "12d"},
	{FORMAT3(2, OP3_X(2,4), 1), "mulscc", "1id"},
	{FORMAT3(2, OP3_X(3,4), 1), "FPop1", ""},	/* see below */

	{FORMAT3(2, OP3_X(0,5), 0), "andn", "12d"},
	{FORMAT3(2, OP3_X(0,5), 1), "andn", "1id"},
	{FORMAT3(2, OP3_X(1,5), 0), "andncc", "12d"},
	{FORMAT3(2, OP3_X(1,5), 1), "andncc", "1id"},
	{FORMAT3(2, OP3_X(2,5), 0), "sll", "12d"},
	{FORMAT3(2, OP3_X(2,5), 1), "sll", "1Dd"},
	{FORMAT3(2, OP3_X(2,5), 0) | EIF_F3_X(1), "sllx", "12d"},
	{FORMAT3(2, OP3_X(2,5), 1) | EIF_F3_X(1), "sllx", "1Ed"},
	{FORMAT3(2, OP3_X(3,5), 1), "FPop2", ""},	/* see below */

	{FORMAT3(2, OP3_X(0,6), 0), "orn", "12d"},
	{FORMAT3(2, OP3_X(0,6), 1), "orn", "1id"},
	{FORMAT3(2, OP3_X(1,6), 0), "orncc", "12d"},
	{FORMAT3(2, OP3_X(1,6), 1), "orncc", "1id"},
	{FORMAT3(2, OP3_X(2,6), 0), "srl", "12d"},
	{FORMAT3(2, OP3_X(2,6), 1), "srl", "1Dd"},
	{FORMAT3(2, OP3_X(2,6), 0) | EIF_F3_X(1), "srlx", "12d"},
	{FORMAT3(2, OP3_X(2,6), 1) | EIF_F3_X(1), "srlx", "1Ed"},
	{FORMAT3(2, OP3_X(3,6), 1), "impdep1", ""},

	{FORMAT3(2, OP3_X(0,7), 0), "xorn", "12d"},
	{FORMAT3(2, OP3_X(0,7), 1), "xorn", "1id"},
	{FORMAT3(2, OP3_X(1,7), 0), "xorncc", "12d"},
	{FORMAT3(2, OP3_X(1,7), 1), "xorncc", "1id"},
	{FORMAT3(2, OP3_X(2,7), 0), "sra", "12d"},
	{FORMAT3(2, OP3_X(2,7), 1), "sra", "1Dd"},
	{FORMAT3(2, OP3_X(2,7), 0) | EIF_F3_X(1), "srax", "12d"},
	{FORMAT3(2, OP3_X(2,7), 1) | EIF_F3_X(1), "srax", "1Ed"},
	{FORMAT3(2, OP3_X(3,7), 1), "impdep2", ""},

	{FORMAT3(2, OP3_X(0,8), 0), "addc", "12d"},
	{FORMAT3(2, OP3_X(0,8), 1), "addc", "1id"},
	{FORMAT3(2, OP3_X(1,8), 0), "addccc", "12d"},
	{FORMAT3(2, OP3_X(1,8), 1), "addccc", "1id"},
	{(FORMAT3(2, 0x28, 1) | EIF_F3_RS1(15)), "membar", "9"},
	{(FORMAT3(2, 0x28, 0) | EIF_F3_RS1(15)), "stbar", ""},
	{FORMAT3(2, OP3_X(2,8), 0), "rd", "Bd"},

	{FORMAT3(2, OP3_X(3,8), 0), "jmpl", "pd"},
	{FORMAT3(2, OP3_X(3,8), 1), "jmpl", "qd"},

	{FORMAT3(2, OP3_X(0,9), 0), "mulx", "12d"},
	{FORMAT3(2, OP3_X(0,9), 1), "mulx", "1id"},
	{FORMAT3(2, OP3_X(1,9), 0), "UNDEFINED", ""},
	{FORMAT3(2, OP3_X(2,9), 0), "UNDEFINED", ""},
	{FORMAT3(2, OP3_X(3,9), 0), "return", "p"},
	{FORMAT3(2, OP3_X(3,9), 1), "return", "q"},

	{FORMAT3(2, OP3_X(0,10), 0), "umul", "12d"},
	{FORMAT3(2, OP3_X(0,10), 1), "umul", "1id"},
	{FORMAT3(2, OP3_X(1,10), 0), "umulcc", "12d"},
	{FORMAT3(2, OP3_X(1,10), 1), "umulcc", "1id"},
	{FORMAT3(2, OP3_X(2,10), 0), "rdpr", "Ad"},
		/*
		 * OP3 = (3,10): TCC: Trap on Integer Condition Codes
		 */
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x8)), "ta", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x8)), "ta", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x0)), "tn", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x0)), "tn", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x9)), "tne", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x9)), "tne", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x1)), "te", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x1)), "te", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0xa)), "tg", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0xa)), "tg", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x2)), "tle", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x2)), "tle", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0xb)), "tge", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0xb)), "tge", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x3)), "tl", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x3)), "tl", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0xc)), "tgu", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0xc)), "tgu", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x4)), "tleu", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x4)), "tleu", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0xd)), "tcc", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0xd)), "tcc", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x5)), "tcs", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x5)), "tcs", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0xe)), "tpos", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0xe)), "tpos", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x6)), "tneg", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x6)), "tneg", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0xf)), "tvc", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0xf)), "tvc", "0F"},
		{(FORMAT3(2, OP3_X(3,10), 0) | EIF_F4_TCOND(0x7)), "tvs", "12F"},
		{(FORMAT3(2, OP3_X(3,10), 1) | EIF_F4_TCOND(0x7)), "tvs", "0F"},

	{FORMAT3(2, OP3_X(0,11), 0), "smul", "12d"},
	{FORMAT3(2, OP3_X(0,11), 1), "smul", "1id"},
	{FORMAT3(2, OP3_X(1,11), 0), "smulcc", "12d"},
	{FORMAT3(2, OP3_X(1,11), 1), "smulcc", "1id"},
	{FORMAT3(2, OP3_X(2,11), 0), "flushw", ""},
	{FORMAT3(2, OP3_X(3,11), 0), "flush", "p"},
	{FORMAT3(2, OP3_X(3,11), 1), "flush", "q"},

	{FORMAT3(2, OP3_X(0,12), 0), "subc", "12d"},
	{FORMAT3(2, OP3_X(0,12), 1), "subc", "1id"},
	{FORMAT3(2, OP3_X(1,12), 0), "subccc", "12d"},
	{FORMAT3(2, OP3_X(1,12), 1), "subccc", "1id"},
		/*
		 * OP3 = (2,12): MOVcc, Move Integer Register on Condition
		 */
		/* For Integer Condition Codes */
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,8)), "mova", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,8)), "mova", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,0)), "movn", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,0)), "movn", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,9)), "movne", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,9)), "movne", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,1)), "move", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,1)), "move", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,10)), "movg", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,10)), "movg", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,2)), "movle", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,2)), "movle", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,11)), "movge", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,11)), "movge", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,3)), "movl", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,3)), "movl", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,12)), "movgu", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,12)), "movgu", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,4)), "movleu", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,4)), "movleu", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,13)), "movcc", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,13)), "movcc", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,5)), "movcs", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,5)), "movcs", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,14)), "movpos", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,14)), "movpos", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,6)), "movneg", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,6)), "movneg", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,15)), "movvc", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,15)), "movvc", "02d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(1,7)), "movvs", "0jd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(1,7)), "movvs", "02d"},

		/* For Floating-Point Condition Codes */
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,8)), "mova", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,8)), "mova", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,0)), "movn", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,0)), "movn", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,7)), "movu", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,7)), "movu", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,6)), "movg", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,6)), "movg", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,5)), "movug", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,5)), "movug", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,4)), "movl", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,4)), "movl", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,3)), "movul", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,3)), "movul", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,2)), "movlg", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,2)), "movlg", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,1)), "movne", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,1)), "movne", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,9)), "move", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,9)), "move", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,10)), "movue", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,10)), "movue", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,11)), "movge", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,11)), "movge", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,12)), "movuge", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,12)), "movuge", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,13)), "movle", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,13)), "movle", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,14)), "movule", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,14)), "movule", "o2d"},
		{(FORMAT3(2, OP3_X(2,12), 1) | COND2(0,15)), "movo", "ojd"},
		{(FORMAT3(2, OP3_X(2,12), 0) | COND2(0,15)), "movo", "o2d"},

	{FORMAT3(2, OP3_X(3,12), 0), "save", "12d"},
	{FORMAT3(2, OP3_X(3,12), 1), "save", "1id"},

	{FORMAT3(2, OP3_X(0,13), 0), "udivx", "12d"},
	{FORMAT3(2, OP3_X(0,13), 1), "udivx", "1id"},
	{FORMAT3(2, OP3_X(1,13), 0), "UNDEFINED", ""},
	{FORMAT3(2, OP3_X(2,13), 0), "sdivx", "12d"},
	{FORMAT3(2, OP3_X(2,13), 1), "sdivx", "1id"},
	{FORMAT3(2, OP3_X(3,13), 0), "restore", "12d"},
	{FORMAT3(2, OP3_X(3,13), 1), "restore", "1id"},

	{FORMAT3(2, OP3_X(0,14), 0), "udiv", "12d"},
	{FORMAT3(2, OP3_X(0,14), 1), "udiv", "1id"},
	{FORMAT3(2, OP3_X(1,14), 0), "udivcc", "12d"},
	{FORMAT3(2, OP3_X(1,14), 1), "udivcc", "1id"},
	{FORMAT3(2, OP3_X(2,14), 0), "popc", "2d"},
	{FORMAT3(2, OP3_X(2,14), 1), "popc", "id"},

	{FORMAT3(2, OP3_X(3,14), 0), "done", ""},
	{FORMAT3(2, OP3_X(3,14) | EIF_F3_FCN(1), 1), "retry", ""},

	{FORMAT3(2, OP3_X(0,15), 0), "sdiv", "12d"},
	{FORMAT3(2, OP3_X(0,15), 1), "sdiv", "1id"},
	{FORMAT3(2, OP3_X(1,15), 0), "sdivcc", "12d"},
	{FORMAT3(2, OP3_X(1,15), 1), "sdivcc", "1id"},
		/*
		 * OP3 = (2,15): MOVr:
		 * 	Move Integer Register on Register Condition
		 */
		{(FORMAT3(2, OP3_X(2,15), 1) | EIF_F3_RCOND(1)), "movrz", "1jd"},
		{(FORMAT3(2, OP3_X(2,15), 0) | EIF_F3_RCOND(1)), "movrz", "12d"},
		{(FORMAT3(2, OP3_X(2,15), 1) | EIF_F3_RCOND(2)), "movrlez", "1jd"},
		{(FORMAT3(2, OP3_X(2,15), 0) | EIF_F3_RCOND(2)), "movrlez", "12d"},
		{(FORMAT3(2, OP3_X(2,15), 1) | EIF_F3_RCOND(3)), "movrlz", "1jd"},
		{(FORMAT3(2, OP3_X(2,15), 0) | EIF_F3_RCOND(3)), "movrlz", "12d"},
		{(FORMAT3(2, OP3_X(2,15), 1) | EIF_F3_RCOND(5)), "movrnz", "1jd"},
		{(FORMAT3(2, OP3_X(2,15), 0) | EIF_F3_RCOND(5)), "movrnz", "12d"},
		{(FORMAT3(2, OP3_X(2,15), 1) | EIF_F3_RCOND(6)), "movrgz", "1jd"},
		{(FORMAT3(2, OP3_X(2,15), 0) | EIF_F3_RCOND(6)), "movrgz", "12d"},
		{(FORMAT3(2, OP3_X(2,15), 1) | EIF_F3_RCOND(7)), "movrgez", "1jd"},
		{(FORMAT3(2, OP3_X(2,15), 0) | EIF_F3_RCOND(7)), "movrgez", "12d"},

	{FORMAT3(2, OP3_X(3,15), 0), "UNDEFINED", ""},


	/*
	 * Format 3/3: Load and store (appendix E, table 33)
	 */

	/* Loads */
	{(FORMAT3(3, OP3_X(0,0), 0)), "lduw", "pd"},
	{(FORMAT3(3, OP3_X(0,0), 1)), "lduw", "qd"},
	{(FORMAT3(3, OP3_X(1,0), 0)), "lduwa", "7d"},
	{(FORMAT3(3, OP3_X(1,0), 1)), "lduwa", "8d"},
	{(FORMAT3(3, OP3_X(2,0), 0)), "ldf", "pe"},
	{(FORMAT3(3, OP3_X(2,0), 1)), "ldf", "qe"},
	{(FORMAT3(3, OP3_X(3,0), 0)), "ldfa", "7e"},
	{(FORMAT3(3, OP3_X(3,0), 1)), "ldfa", "8e"},

	{(FORMAT3(3, OP3_X(0,1), 0)), "ldub", "pd"},
	{(FORMAT3(3, OP3_X(0,1), 1)), "ldub", "qd"},
	{(FORMAT3(3, OP3_X(1,1), 0)), "lduba", "7d"},
	{(FORMAT3(3, OP3_X(1,1), 1)), "lduba", "8d"},
	{(FORMAT3(3, OP3_X(2,1), 0) | EIF_F3_RD(0)), "lduw", "p5"},
	{(FORMAT3(3, OP3_X(2,1), 1) | EIF_F3_RD(0)), "lduw", "q5"},
	{(FORMAT3(3, OP3_X(2,1), 0) | EIF_F3_RD(1)), "ldx", "p6"},
	{(FORMAT3(3, OP3_X(2,1), 1) | EIF_F3_RD(1)), "ldx", "q6"},

	{(FORMAT3(3, OP3_X(0,2), 0)), "lduh", "pd"},
	{(FORMAT3(3, OP3_X(0,2), 1)), "lduh", "qd"},
	{(FORMAT3(3, OP3_X(1,2), 0)), "lduha", "7d"},
	{(FORMAT3(3, OP3_X(1,2), 1)), "lduha", "8d"},
	{(FORMAT3(3, OP3_X(2,2), 0)), "ldq", "pe"},
	{(FORMAT3(3, OP3_X(2,2), 1)), "ldq", "qe"},
	{(FORMAT3(3, OP3_X(3,2), 0)), "ldqa", "7e"},
	{(FORMAT3(3, OP3_X(3,2), 1)), "ldqa", "8e"},

	{(FORMAT3(3, OP3_X(0,3), 0)), "ldd", "pd"},
	{(FORMAT3(3, OP3_X(0,3), 1)), "ldd", "qd"},
	{(FORMAT3(3, OP3_X(1,3), 0)), "ldda", "7d"},
	{(FORMAT3(3, OP3_X(1,3), 1)), "ldda", "8d"},
	{(FORMAT3(3, OP3_X(2,3), 0)), "ldd", "pe"},
	{(FORMAT3(3, OP3_X(2,3), 1)), "ldd", "qe"},
	{(FORMAT3(3, OP3_X(3,3), 0)), "ldda", "7e"},
	{(FORMAT3(3, OP3_X(3,3), 1)), "ldda", "8e"},

	{(FORMAT3(3, OP3_X(0,4), 0)), "stw", "dp"},
	{(FORMAT3(3, OP3_X(0,4), 1)), "stw", "dq"},
	{(FORMAT3(3, OP3_X(1,4), 0)), "stwa", "d7"},
	{(FORMAT3(3, OP3_X(1,4), 1)), "stwa", "d8"},
	{(FORMAT3(3, OP3_X(2,4), 0)), "stf", "ep"},
	{(FORMAT3(3, OP3_X(2,4), 1)), "stf", "eq"},
	{(FORMAT3(3, OP3_X(3,4), 0)), "stfa", "e7"},
	{(FORMAT3(3, OP3_X(3,4), 1)), "stfa", "e8"},

	{(FORMAT3(3, OP3_X(0,5), 0)), "stb", "dp"},
	{(FORMAT3(3, OP3_X(0,5), 1)), "stb", "dq"},
	{(FORMAT3(3, OP3_X(1,5), 0)), "stba", "d7"},
	{(FORMAT3(3, OP3_X(1,5), 1)), "stba", "d8"},
	{(FORMAT3(3, OP3_X(2,5), 0)), "stw", "5p"},
	{(FORMAT3(3, OP3_X(2,5), 1)), "stw", "5q"},
	{(FORMAT3(3, OP3_X(2,5), 0) | EIF_F3_RD(1)), "stx", "6p"},
	{(FORMAT3(3, OP3_X(2,5), 1) | EIF_F3_RD(1)), "stx", "6q"},

	{(FORMAT3(3, OP3_X(0,6), 0)), "sth", "dp"},
	{(FORMAT3(3, OP3_X(0,6), 1)), "sth", "dq"},
	{(FORMAT3(3, OP3_X(1,6), 0)), "stha", "d7"},
	{(FORMAT3(3, OP3_X(1,6), 1)), "stha", "d8"},
	{(FORMAT3(3, OP3_X(2,6), 0)), "stq", "ep"},
	{(FORMAT3(3, OP3_X(2,6), 1)), "stq", "eq"},
	{(FORMAT3(3, OP3_X(3,6), 0)), "stqa", "e7"},
	{(FORMAT3(3, OP3_X(3,6), 1)), "stqa", "e8"},

	{(FORMAT3(3, OP3_X(0,7), 0)), "std", "dp"},
	{(FORMAT3(3, OP3_X(0,7), 1)), "std", "dq"},
	{(FORMAT3(3, OP3_X(1,7), 0)), "stda", "d7"},
	{(FORMAT3(3, OP3_X(1,7), 1)), "stda", "d8"},
	{(FORMAT3(3, OP3_X(2,7), 0)), "std", "ep"},
	{(FORMAT3(3, OP3_X(2,7), 1)), "std", "eq"},
	{(FORMAT3(3, OP3_X(3,7), 0)), "stda", "e7"},
	{(FORMAT3(3, OP3_X(3,7), 1)), "stda", "e8"},

	{(FORMAT3(3, OP3_X(0,8), 0)), "ldsw", "pd"},
	{(FORMAT3(3, OP3_X(0,8), 1)), "ldsw", "qd"},
	{(FORMAT3(3, OP3_X(1,8), 0)), "ldswa", "7d"},
	{(FORMAT3(3, OP3_X(1,8), 1)), "ldswa", "8d"},

	{(FORMAT3(3, OP3_X(0,9), 0)), "ldsb", "pd"},
	{(FORMAT3(3, OP3_X(0,9), 1)), "ldsb", "qd"},
	{(FORMAT3(3, OP3_X(1,9), 0)), "ldsba", "7d"},
	{(FORMAT3(3, OP3_X(1,9), 1)), "ldsba", "8d"},

	{(FORMAT3(3, OP3_X(0,10), 0)), "ldsh", "pd"},
	{(FORMAT3(3, OP3_X(0,10), 1)), "ldsh", "qd"},
	{(FORMAT3(3, OP3_X(1,10), 0)), "ldsha", "7d"},
	{(FORMAT3(3, OP3_X(1,10), 1)), "ldsha", "8d"},

	{(FORMAT3(3, OP3_X(0,11), 0)), "ldx", "pd"},
	{(FORMAT3(3, OP3_X(0,11), 1)), "ldx", "qd"},
	{(FORMAT3(3, OP3_X(1,11), 0)), "ldxa", "7d"},
	{(FORMAT3(3, OP3_X(1,11), 1)), "ldxa", "8d"},

	{(FORMAT3(3, OP3_X(3,12), 1)), "casa", "s2d"},
	{(FORMAT3(3, OP3_X(3,12), 0)), "casa", "t2d"},

	{(FORMAT3(3, OP3_X(0,13), 0)), "ldstub", "7d"},
	{(FORMAT3(3, OP3_X(0,13), 1)), "ldstub", "8d"},
	{(FORMAT3(3, OP3_X(1,13), 0)), "ldstuba", "pd"},
	{(FORMAT3(3, OP3_X(1,13), 1)), "ldstuba", "qd"},
	{(FORMAT3(3, OP3_X(2,13), 0)), "prefetch", "pr"},
	{(FORMAT3(3, OP3_X(2,13), 1)), "prefetch", "qr"},
	{(FORMAT3(3, OP3_X(3,13), 0)), "prefetcha", "7r"},
	{(FORMAT3(3, OP3_X(3,13), 1)), "prefetcha", "8r"},

	{(FORMAT3(3, OP3_X(0,14), 0)), "stx", "dp"},
	{(FORMAT3(3, OP3_X(0,14), 1)), "stx", "dq"},
	{(FORMAT3(3, OP3_X(1,14), 0)), "stxa", "d7"},
	{(FORMAT3(3, OP3_X(1,14), 1)), "stxa", "d8"},
	{(FORMAT3(3, OP3_X(3,14), 0)), "casxa", "t2d"},
	{(FORMAT3(3, OP3_X(3,14), 1)), "casxa", "s2d"},

	/* Swap Register */
	{(FORMAT3(3, OP3_X(0,15), 0)), "swap", "pd"},
	{(FORMAT3(3, OP3_X(0,15), 1)), "swap", "qd"},
	{(FORMAT3(3, OP3_X(1,15), 0)), "swapa", "7d"},
	{(FORMAT3(3, OP3_X(1,15), 1)), "swapa", "8d"},


	/*
	 * OP3 = (3,4): FPop1 (table 34)
	 */
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,1))), "fmovs", ".4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,2))), "fmovd", ".4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,3))), "fmovq", ".4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,5))), "fnegs", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,6))), "fnegd", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,7))), "fnegq", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,9))), "fabss", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,10))), "fabsd", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(0,11))), "fabsq", "4e"},

	{(FORMAT3F(2, OP3_X(3,4), OPF_X(2,9))), "fsqrts", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(2,10))), "fsqrtd", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(2,11))), "fsqrtq", "4e"},

	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,1))), "fadds", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,2))), "faddd", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,3))), "faddq", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,5))), "fsubs", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,6))), "fsubd", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,7))), "fsubq", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,9))), "fmuls", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,10))), "fmuld", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,11))), "fmulq", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,13))), "fdivs", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,14))), "fdivd", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(4,15))), "fdivq", "34e"},

	{(FORMAT3F(2, OP3_X(3,4), OPF_X(6,9))), "fsmuld", "34e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(6,14))), "fdmulq", "34e"},

	{(FORMAT3F(2, OP3_X(3,4), OPF_X(8,1))), "fstox", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(8,2))), "fdtox", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(8,3))), "fqtox", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(8,4))), "fxtos", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(8,8))), "fxtod", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(8,12))), "fxtoq", "4e"},

	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,4))), "fitos", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,6))), "fdtos", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,7))), "fqtos", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,8))), "fitod", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,9))), "fstod", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,11))), "fqtod", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,12))), "fitoq", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,13))), "fstoq", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(12,14))), "fdtoq", "4e"},

	{(FORMAT3F(2, OP3_X(3,4), OPF_X(13,1))), "fstoi", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(13,2))), "fdtoi", "4e"},
	{(FORMAT3F(2, OP3_X(3,4), OPF_X(13,3))), "fqtoi", "4e"},


#ifdef xxx
	/*
	 * OP3 =(3,5): FPop2 (table 35)
	 */
	{(FORMAT3F(2, OP3_X(3,5), 81)), "fcmps", "o34"},
	{(FORMAT3F(2, OP3_X(3,5), 82)), "fcmpd", "o34"},
	{(FORMAT3F(2, OP3_X(3,5), 83)), "fcmpq", "o34"},
	{(FORMAT3F(2, OP3_X(3,5), 85)), "fcmpes", "o34"},
	{(FORMAT3F(2, OP3_X(3,5), 86)), "fcmped", "o34"},
	{(FORMAT3F(2, OP3_X(3,5), 87)), "fcmpeq", "o34"},

	/* Move Floating-Point Register on Condition "FMOVcc" */
	/* FIXME should check for single, double, and quad movements */
	/* Integer Condition Codes */
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,8)), "fmova", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,0)), "fmovn", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,9)), "fmovne", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,1)), "fmove", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,10)), "fmovg", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,2)), "fmovle", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,11)), "fmovge", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,3)), "fmovl", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,12)), "fmovgu", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,4)), "fmovleu", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,13)), "fmovcc", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,5)), "fmovcs", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,14)), "fmovpos", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,6)), "fmovneg", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,15)), "fmovvc", "04e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,7)), "fmovvs", "04e"},

	/* Floating-Point Condition Codes */
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,8)), "fmova", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,0)), "fmovn", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,7)), "fmovu", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,6)), "fmovg", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,5)), "fmovug", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,4)), "fmovk", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,3)), "fmovul", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,2)), "fmovlg", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,1)), "fmovne", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,9)), "fmove", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,10)), "fmovue", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,11)), "fmovge", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,12)), "fmovuge", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,13)), "fmovle", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,14)), "fmovule", "o4e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | COND2(0,15)), "fmovo", "o4e"},

	/* Move F-P Register on Integer Register Condition "FMOVr" */
	/* FIXME: check for short, double, and quad's */
	{(FORMAT3(2, OP3_X(3,5), 0) | EIF_F3_RCOND(1)), "fmovre", "14e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | EIF_F3_RCOND(2)), "fmovrlez", "14e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | EIF_F3_RCOND(3)), "fmovrlz", "14e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | EIF_F3_RCOND(5)), "fmovrne", "14e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | EIF_F3_RCOND(6)), "fmovrgz", "14e"},
	{(FORMAT3(2, OP3_X(3,5), 0) | EIF_F3_RCOND(7)), "fmovrgez", "14e"},
#endif
	/* FP logical insns -- UltraSPARC extens */
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,0))), "fzero", "e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,1))), "fzeros", "e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,14))), "fone", "e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,15))), "fones", "e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,4))), "fsrc1", "3e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,5))), "fsrc1s", "3e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,8))), "fsrc2", "4e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,9))), "fsrc2s", "4e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,10))), "fnot1", "3e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,11))), "fnot1s", "3e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,6))), "fnot2", "4e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,7))), "fnot2s", "4e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,12))), "for", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,13))), "fors", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,2))), "fnor", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,3))), "fnors", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,0))), "fand", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,1))), "fands", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,14))), "fnand", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,15))), "fnands", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,12))), "fxor", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,13))), "fxors", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,2))), "fxnor", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,3))), "fxnors", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,10))), "fornot1", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,11))), "fornot1s", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,6))), "fornot2", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(7,7))), "fornot2s", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,8))), "fandnot1", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,9))), "fandnot1s", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,4))), "fandnot2", "34e"},
	{(FORMAT3F(2, OP3_X(3,6), OPF_X(6,5))), "fandnot2s", "34e"},

	/* grrrr.... */
	{0, 0, 0}

};

db_addr_t
db_disasm(db_addr_t loc, bool altfmt)
{
	const struct sparc_insn* i_ptr = (struct sparc_insn *)&sparc_i;
	unsigned int insn, you_lose, bitmask;
	int matchp;
	const char* f_ptr, *cp;

	you_lose = 0;
	matchp = 0;
	insn = db_get_value(loc, 4, 0);

	if (insn == 0x01000000) {
		db_printf("nop\n");
		return loc + 4;
	}

	while (i_ptr->name) {
		/* calculate YOU_LOSE value */
		bitmask= (i_ptr->match);
		you_lose = (~bitmask);

		if (((bitmask>>30) & 0x3) == 0x1) {
			/* Call */
			you_lose = ((~0x1)<<30);
		} else if (((bitmask>>30) & 0x3) == 0x0) {
			if (((bitmask>>22) & 0x7) == 0x4) {
				/* Sethi */
				you_lose &= (FORMAT2(0x3,0x7));
			} else {
				/* Branches */
				you_lose &= (FORMAT2(0x3,0x7) |
				    EIF_F2_COND(0xf));
			}
		} else if (((bitmask>>30) & 0x3) == 0x2 &&
			   ((bitmask>>19) & 0x3f) == 0x34) /* XXX */ {
			/* FPop1 */
			you_lose &= (FORMAT3(0x3,0x3f,0x1) |
			    EIF_F3_OPF(0x1ff));
		} else if (((bitmask>>30) & 0x3) == 0x2 &&
			   ((bitmask>>19) & 0x3f) == 0x3a) /* XXX */ {
			/* Tcc */
			you_lose &= (FORMAT3(0x3,0x3f,0x1) | EIF_F4_TCOND(0xf));
		} else if (((bitmask>>30) & 0x3) == 0x2 &&
			   ((bitmask>>21) & 0xf) == 0x9 &&
			   ((bitmask>>19) & 0x3) != 0) /* XXX */ {
			/* shifts */
			you_lose &= (FORMAT3(0x3,0x3f,0x1)) | EIF_F3_X(1);
		} else if (((bitmask>>30) & 0x3) == 0x2 &&
			   ((bitmask>>19) & 0x3f) == 0x2c) /* XXX */ {
			/* cmov */
			you_lose &= (FORMAT3(0x3,0x3f,0x1) | COND2(1,0xf));
		} else if (((bitmask>>30) & 0x3) == 0x2 &&
			   ((bitmask>>19) & 0x3f) == 0x35) /* XXX */ {
			/* fmov */
			you_lose &= (FORMAT3(0x3,0x3f,0x1) | COND2(1,0xf));
		} else {
			you_lose &= (FORMAT3(0x3,0x3f,0x1));
		}

		if (((bitmask & insn) == bitmask) && ((you_lose & insn) == 0)) {
			matchp = 1;
			break;
		}
		i_ptr++;
	}

	if (!matchp) {
		db_printf("undefined\n");
		return loc + 4;
	}

	db_printf("%s", i_ptr->name);

	f_ptr = i_ptr->format;

	for (cp = f_ptr; *cp; cp++) {
		if (*cp == ',') {
			for (;f_ptr < cp; f_ptr++)
				switch (*f_ptr) {
				case 'a':
					if (insn & EIF_F2_A(1))
						db_printf(",a");
					break;
				case 'p':
					if (insn & EIF_F2_P(1))
						db_printf(",pt");
					else
						db_printf(",pn");
					break;
				}
			f_ptr++;
			break;
		}
	}
	db_printf("      \t");

	while (*f_ptr) {
		switch (*f_ptr) {
			int64_t val;
		case '1':
			db_printf("%%%s", regs[((insn >> 14) & 0x1f)]);
			break;
		case '2':
			db_printf("%%%s", regs[(insn & 0x1f)]);
			break;
		case 'd':
			db_printf("%%%s", regs[((insn >> 25) & 0x1f)]);
			break;
		case '3':
			db_printf("%%f%d", ((insn >> 14) & 0x1f));
			break;
		case '4':
			db_printf("%%f%d", (insn & 0x1f));
			break;
		case 'e':
			db_printf("%%f%d", ((insn >> 25) & 0x1f));
			break;
		case 'i':
			/* simm13 -- signed */
			val = IF_SIMM(insn, 13);
			db_printf("%s0x%x", SIGN(val), (int)abs(val));
			break;
		case 'j':
			/* simm11 -- signed */
			val = IF_SIMM(insn, 11);
			db_printf("%s0x%x", SIGN(val), (int)abs(val));
			break;
		case 'l':
			val = (((insn>>20)&0x3)<<13)|(insn & 0x1fff);
			val = IF_SIMM(val, 16);
			db_printsym((db_addr_t)(loc + (4 * val)), DB_STGY_ANY);
			break;
		case 'm':
			db_printsym((db_addr_t)(loc + (4 * IF_SIMM(insn, 22))),
				DB_STGY_ANY);
			break;
		case 'u':
			db_printsym((db_addr_t)(loc + (4 * IF_SIMM(insn, 19))),
			    DB_STGY_ANY);
			break;
		case 'n':
			db_printsym((db_addr_t)(loc + (4 * IF_SIMM(insn, 30))),
			    DB_STGY_PROC);
			break;
		case 's':
			db_printf("%%asi");
			break;
		case 't':
			db_printf("0x%-2.2x", ((insn >> 5) & 0xff));
			break;
		case 'o':
			db_printf("%%fcc%d", ((insn >> 25) & 0x3));
			break;
		case 'p':
		case '7':
			db_printf("[%%%s + %%%s]",
				  regs[((insn >> 14) & 0x1f)],
				  regs[(insn & 0x1f)]);
			if (*f_ptr == '7')
				db_printf(" %d", ((insn >> 5) & 0xff));
			break;
		case 'q':
		case '8':
			val = IF_SIMM(insn, 13);
			db_printf("[%%%s %c 0x%x]",
				regs[((insn >> 14) & 0x1f)],
				(int)((val<0)?'-':'+'),
				(int)abs(val));
			if (*f_ptr == '8')
				db_printf(" %%asi");
			break;
		case '5':
			db_printf("%%fsr");
			break;
		case '6':
			db_printf("%%fsr");
			break;
		case '9':
			db_printf("0x%xl",
				  ((insn & 0xf) | ((insn >> 4) & 0x7)));
			break;
		case '0':
			db_printf("%%%s", ccodes[((insn >> 11) & 0x3) + 4]);
			break;
		case '.':
			db_printf("%%%s", ccodes[((insn >> 11) & 0x7)]);
			break;
		case 'r':
			db_printf("#%s", prefetch[((insn >> 25) & 0x1f)]);
			break;
		case 'A':
			db_printf("%%%s", priv_regs[((insn >> 14) & 0x1f)]);
			break;
		case 'B':
			db_printf("%%%s", state_regs[((insn >> 14) & 0x1f)]);
			break;
		case 'C':
			db_printf("%%hi(0x%x)", ((insn & 0x3fffff) << 10));
			break;
		case 'D':
			db_printf("0x%x", (insn & 0x1f));
			break;
		case 'E':
			db_printf("%d", (insn & 0x3f));
			break;
		case 'F':
			db_printf("%d", (insn & 0x3f));
			break;
		case 'G':
			db_printf("%%%s", priv_regs[((insn >> 25) & 0x1f)]);
			break;
		case 'H':
			db_printf("%%%s", state_regs[((insn >> 25) & 0x1f)]);
			break;
		default:
			db_printf("(UNKNOWN)");
			break;
		}
		if (*(++f_ptr))
			db_printf(", ");
	}

	db_printf("\n");

	return (loc + 4);
}
