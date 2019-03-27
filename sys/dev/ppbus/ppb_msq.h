/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Nicolas Souchu
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */
#ifndef __PPB_MSQ_H
#define __PPB_MSQ_H

/*
 * Basic definitions
 */

/* microsequence parameter descriptor */
#define MS_INS_MASK	0x00ff	/* mask to retrieve the instruction position < 256	XXX */
#define MS_ARG_MASK	0x0f00	/* mask to retrieve the argument number */
#define MS_TYP_MASK	0xf000	/* mask to retrieve the type of the param */

/* offset of each mask (see above) */
#define MS_INS_OFFSET	0
#define MS_ARG_OFFSET	8
#define MS_TYP_OFFSET	12

/* list of parameter types */
#define MS_TYP_INT	0x0	/* integer */
#define MS_TYP_CHA	0x1	/* character */
#define MS_TYP_PTR	0x2	/* void pointer */
#define MS_TYP_FUN	0x3	/* function pointer */

#define MS_PARAM(ins,arg,typ) \
	(((ins<<MS_INS_OFFSET) & MS_INS_MASK) | \
	 ((arg<<MS_ARG_OFFSET) & MS_ARG_MASK) | \
	 ((typ<<MS_TYP_OFFSET) & MS_TYP_MASK))

#define MS_INS(param) ((param & MS_INS_MASK) >> MS_INS_OFFSET)
#define MS_ARG(param) ((param & MS_ARG_MASK) >> MS_ARG_OFFSET)
#define MS_TYP(param) ((param & MS_TYP_MASK) >> MS_TYP_OFFSET)

/* microsequence opcodes - do not change! */
#define MS_OP_GET       0	/* get <ptr>, <len>			*/
#define MS_OP_PUT       1	/* put <ptr>, <len>			*/

#define MS_OP_RFETCH	2	/* rfetch <reg>, <mask>, <ptr>		*/
#define MS_OP_RSET	3	/* rset <reg>, <mask>, <mask>		*/
#define MS_OP_RASSERT	4	/* rassert <reg>, <mask>		*/
#define MS_OP_DELAY     5	/* delay <val>				*/
#define MS_OP_SET       6	/* set <val>				*/
#define MS_OP_DBRA      7	/* dbra <offset>			*/
#define MS_OP_BRSET     8	/* brset <mask>, <offset>		*/
#define MS_OP_BRCLEAR   9	/* brclear <mask>, <offset>		*/
#define MS_OP_RET       10	/* ret <retcode>			*/
#define MS_OP_C_CALL	11	/* c_call <function>, <parameter>	*/
#define MS_OP_PTR	12	/* ptr <pointer>			*/
#define MS_OP_ADELAY	13	/* adelay <val>				*/
#define MS_OP_BRSTAT	14	/* brstat <mask>, <mask>, <offset>	*/
#define MS_OP_SUBRET	15	/* subret <code>			*/
#define MS_OP_CALL	16	/* call <microsequence>			*/
#define MS_OP_RASSERT_P	17	/* rassert_p <iter>, <reg>		*/
#define MS_OP_RFETCH_P	18	/* rfetch_p <iter>, <reg>, <mask>	*/
#define MS_OP_TRIG	19	/* trigger <reg>, <len>, <array>	*/

/* common masks */
#define MS_CLEAR_ALL	0x0
#define MS_ASSERT_NONE	0x0
#define MS_ASSERT_ALL	0xff
#define MS_FETCH_ALL	0xff

/* undefined parameter value */
#define MS_NULL		0
#define MS_UNKNOWN	MS_NULL

/* predifined parameters */
#define MS_ACCUM	-1	/* use accum previously set by MS_OP_SET */

/* these are register numbers according to our PC-like parallel port model */
#define MS_REG_DTR	0x0
#define MS_REG_STR	0x1
#define MS_REG_CTR	0x2
#define MS_REG_EPP_A	0x3
#define MS_REG_EPP_D	0x4

/*
 * Microsequence macro abstraction level
 */

/* register operations */
#define MS_RSET(reg,assert,clear) { MS_OP_RSET, {{ (reg) }, { (assert) }, { (clear) }}}
#define MS_RASSERT(reg,byte)	  { MS_OP_RASSERT, { { (reg) }, { (byte) }}}
#define MS_RCLR(reg,clear)	  { MS_OP_RSET, {{ (reg) }, { MS_ASSERT_NONE }, { (clear) }}}

#define MS_RFETCH(reg,mask,ptr) { MS_OP_RFETCH, {{ (reg) }, { (mask) }, { (ptr) }}}

/* trigger the port with array[char, delay,...] */
#define MS_TRIG(reg,len,array)	{ MS_OP_TRIG, {{ (reg) }, { (len) }, { (array) }}}

/* assert/fetch from/to ptr */
#define MS_RASSERT_P(n,reg)	  { MS_OP_RASSERT_P, {{ (n) }, { (reg) }}}
#define MS_RFETCH_P(n,reg,mask)	  { MS_OP_RFETCH_P, {{ (n) }, { (reg) }, { (mask) }}}

/* ptr manipulation */
#define MS_PTR(ptr)	{ MS_OP_PTR, {{ (ptr) }}}

#define MS_DASS(byte) MS_RASSERT(MS_REG_DTR,byte)
#define MS_SASS(byte) MS_RASSERT(MS_REG_STR,byte)
#define MS_CASS(byte) MS_RASSERT(MS_REG_CTR,byte)

#define MS_SET(accum)		{ MS_OP_SET, {{ (accum) }}}
#define MS_BRSET(mask,offset)	{ MS_OP_BRSET, {{ (mask) }, { (offset) }}}
#define MS_DBRA(offset)		{ MS_OP_DBRA, {{ (offset) }}}
#define MS_BRCLEAR(mask,offset)	{ MS_OP_BRCLEAR, {{ (mask) }, { (offset) }}}
#define MS_BRSTAT(mask_set,mask_clr,offset) \
		{ MS_OP_BRSTAT, {{ mask_set }, { mask_clr }, { (offset) }}}

/* C function or submicrosequence call */
#define MS_C_CALL(function,parameter) \
		{ MS_OP_C_CALL, {{ (function) }, { (parameter) }}}
#define MS_CALL(microseq) { MS_OP_CALL, {{ (microseq) }}}

/* mode dependent read/write operations
 * ppb_MS_xxx_init() call required otherwise default is
 * IEEE1284 operating mode */
#define MS_PUT(ptr,len) { MS_OP_PUT, {{ (ptr) }, { (len) }}}
#define MS_GET(ptr,len) { MS_OP_GET, {{ (ptr) }, { (len) }}}

/* delay in microseconds */
#define MS_DELAY(udelay) { MS_OP_DELAY, {{ (udelay) }}}

/* asynchroneous delay in ms */
#define MS_ADELAY(mdelay) { MS_OP_ADELAY, {{ (mdelay) }}}

/* return from submicrosequence execution or microseqence execution */
#define MS_SUBRET(code)	{ MS_OP_SUBRET,	{{ (code) }}}
#define MS_RET(code)	{ MS_OP_RET, {{ (code) }}}

/*
 * Function abstraction level
 */

#define ppb_MS_GET_init(bus,dev,body) ppb_MS_init(bus, dev, body, MS_OP_GET)

#define ppb_MS_PUT_init(bus,dev,body) ppb_MS_init(bus, dev, body, MS_OP_PUT)

extern int ppb_MS_init(
		device_t,			/* ppbus bus */
		device_t,			/* ppbus device */
		struct ppb_microseq *,		/* loop msq to assign */
		int opcode			/* MS_OP_GET, MS_OP_PUT */
		);

extern int ppb_MS_init_msq(
		struct ppb_microseq *,
		int,				/* number of parameters */
		...				/* descriptor, value, ... */
		);

extern int ppb_MS_exec(
		device_t,			/* ppbus bus */
		device_t,			/* ppbus device */
		int,				/* microseq opcode */
		union ppb_insarg,		/* param1 */
		union ppb_insarg,		/* param2 */
		union ppb_insarg,		/* param3 */
		int *				/* returned value */
		);

extern int ppb_MS_loop(
		device_t,			/* ppbus bus */
		device_t,			/* ppbus device */
		struct ppb_microseq *,		/* prologue msq of loop */
		struct ppb_microseq *,		/* body msq of loop */
		struct ppb_microseq *,		/* epilogue msq of loop */
		int,				/* number of iter */
		int *				/* returned value */
		);

extern int ppb_MS_microseq(
		device_t,			/* ppbus bus */
		device_t,			/* ppbus device */
		struct ppb_microseq *,		/* msq to execute */
		int *				/* returned value */
		);

#endif
