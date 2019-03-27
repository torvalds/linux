/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2002-2003 NetGroup, Politecnico di Torino (Italy)
 * Copyright (C) 2005-2017 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include "opt_bpf.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/if.h>
#else
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#endif

#include <sys/types.h>

#include <net/bpf.h>
#include <net/bpf_jitter.h>

#include <i386/i386/bpf_jit_machdep.h>

/*
 * Emit routine to update the jump table.
 */
static void
emit_length(bpf_bin_stream *stream, __unused u_int value, u_int len)
{

	if (stream->refs != NULL)
		(stream->refs)[stream->bpf_pc] += len;
	stream->cur_ip += len;
}

/*
 * Emit routine to output the actual binary code.
 */
static void
emit_code(bpf_bin_stream *stream, u_int value, u_int len)
{

	switch (len) {
	case 1:
		stream->ibuf[stream->cur_ip] = (u_char)value;
		stream->cur_ip++;
		break;

	case 2:
		*((u_short *)(void *)(stream->ibuf + stream->cur_ip)) =
		    (u_short)value;
		stream->cur_ip += 2;
		break;

	case 4:
		*((u_int *)(void *)(stream->ibuf + stream->cur_ip)) = value;
		stream->cur_ip += 4;
		break;
	}

	return;
}

/*
 * Scan the filter program and find possible optimization.
 */
static int
bpf_jit_optimize(struct bpf_insn *prog, u_int nins)
{
	int flags;
	u_int i;

	/* Do we return immediately? */
	if (BPF_CLASS(prog[0].code) == BPF_RET)
		return (BPF_JIT_FRET);

	for (flags = 0, i = 0; i < nins; i++) {
		switch (prog[i].code) {
		case BPF_LD|BPF_W|BPF_ABS:
		case BPF_LD|BPF_H|BPF_ABS:
		case BPF_LD|BPF_B|BPF_ABS:
		case BPF_LD|BPF_W|BPF_IND:
		case BPF_LD|BPF_H|BPF_IND:
		case BPF_LD|BPF_B|BPF_IND:
		case BPF_LDX|BPF_MSH|BPF_B:
			flags |= BPF_JIT_FPKT;
			break;
		case BPF_LD|BPF_MEM:
		case BPF_LDX|BPF_MEM:
		case BPF_ST:
		case BPF_STX:
			flags |= BPF_JIT_FMEM;
			break;
		case BPF_JMP|BPF_JA:
		case BPF_JMP|BPF_JGT|BPF_K:
		case BPF_JMP|BPF_JGE|BPF_K:
		case BPF_JMP|BPF_JEQ|BPF_K:
		case BPF_JMP|BPF_JSET|BPF_K:
		case BPF_JMP|BPF_JGT|BPF_X:
		case BPF_JMP|BPF_JGE|BPF_X:
		case BPF_JMP|BPF_JEQ|BPF_X:
		case BPF_JMP|BPF_JSET|BPF_X:
			flags |= BPF_JIT_FJMP;
			break;
		case BPF_ALU|BPF_DIV|BPF_K:
		case BPF_ALU|BPF_MOD|BPF_K:
			flags |= BPF_JIT_FADK;
			break;
		}
		if (flags == BPF_JIT_FLAG_ALL)
			break;
	}

	return (flags);
}

/*
 * Function that does the real stuff.
 */
bpf_filter_func
bpf_jit_compile(struct bpf_insn *prog, u_int nins, size_t *size)
{
	bpf_bin_stream stream;
	struct bpf_insn *ins;
	int flags, fret, fpkt, fmem, fjmp, fadk;
	int save_esp;
	u_int i, pass;

	/*
	 * NOTE: Do not modify the name of this variable, as it's used by
	 * the macros to emit code.
	 */
	emit_func emitm;

	flags = bpf_jit_optimize(prog, nins);
	fret = (flags & BPF_JIT_FRET) != 0;
	fpkt = (flags & BPF_JIT_FPKT) != 0;
	fmem = (flags & BPF_JIT_FMEM) != 0;
	fjmp = (flags & BPF_JIT_FJMP) != 0;
	fadk = (flags & BPF_JIT_FADK) != 0;
	save_esp = (fpkt || fmem || fadk);	/* Stack is used. */

	if (fret)
		nins = 1;

	memset(&stream, 0, sizeof(stream));

	/* Allocate the reference table for the jumps. */
	if (fjmp) {
#ifdef _KERNEL
		stream.refs = malloc((nins + 1) * sizeof(u_int), M_BPFJIT,
		    M_NOWAIT | M_ZERO);
#else
		stream.refs = calloc(nins + 1, sizeof(u_int));
#endif
		if (stream.refs == NULL)
			return (NULL);
	}

	/*
	 * The first pass will emit the lengths of the instructions
	 * to create the reference table.
	 */
	emitm = emit_length;

	for (pass = 0; pass < 2; pass++) {
		ins = prog;

		/* Create the procedure header. */
		if (save_esp) {
			PUSH(EBP);
			MOVrd(ESP, EBP);
		}
		if (fmem)
			SUBib(BPF_MEMWORDS * sizeof(uint32_t), ESP);
		if (save_esp)
			PUSH(ESI);
		if (fpkt) {
			PUSH(EDI);
			PUSH(EBX);
			MOVodd(8, EBP, EBX);
			MOVodd(16, EBP, EDI);
		}

		for (i = 0; i < nins; i++) {
			stream.bpf_pc++;

			switch (ins->code) {
			default:
#ifdef _KERNEL
				return (NULL);
#else
				abort();
#endif

			case BPF_RET|BPF_K:
				MOVid(ins->k, EAX);
				if (save_esp) {
					if (fpkt) {
						POP(EBX);
						POP(EDI);
					}
					POP(ESI);
					LEAVE();
				}
				RET();
				break;

			case BPF_RET|BPF_A:
				if (save_esp) {
					if (fpkt) {
						POP(EBX);
						POP(EDI);
					}
					POP(ESI);
					LEAVE();
				}
				RET();
				break;

			case BPF_LD|BPF_W|BPF_ABS:
				MOVid(ins->k, ESI);
				CMPrd(EDI, ESI);
				JAb(12);
				MOVrd(EDI, ECX);
				SUBrd(ESI, ECX);
				CMPid(sizeof(int32_t), ECX);
				JAEb(7);
				ZEROrd(EAX);
				POP(EBX);
				POP(EDI);
				POP(ESI);
				LEAVE();
				RET();
				MOVobd(EBX, ESI, EAX);
				BSWAP(EAX);
				break;

			case BPF_LD|BPF_H|BPF_ABS:
				ZEROrd(EAX);
				MOVid(ins->k, ESI);
				CMPrd(EDI, ESI);
				JAb(12);
				MOVrd(EDI, ECX);
				SUBrd(ESI, ECX);
				CMPid(sizeof(int16_t), ECX);
				JAEb(5);
				POP(EBX);
				POP(EDI);
				POP(ESI);
				LEAVE();
				RET();
				MOVobw(EBX, ESI, AX);
				SWAP_AX();
				break;

			case BPF_LD|BPF_B|BPF_ABS:
				ZEROrd(EAX);
				MOVid(ins->k, ESI);
				CMPrd(EDI, ESI);
				JBb(5);
				POP(EBX);
				POP(EDI);
				POP(ESI);
				LEAVE();
				RET();
				MOVobb(EBX, ESI, AL);
				break;

			case BPF_LD|BPF_W|BPF_LEN:
				if (save_esp)
					MOVodd(12, EBP, EAX);
				else {
					MOVrd(ESP, ECX);
					MOVodd(12, ECX, EAX);
				}
				break;

			case BPF_LDX|BPF_W|BPF_LEN:
				if (save_esp)
					MOVodd(12, EBP, EDX);
				else {
					MOVrd(ESP, ECX);
					MOVodd(12, ECX, EDX);
				}
				break;

			case BPF_LD|BPF_W|BPF_IND:
				CMPrd(EDI, EDX);
				JAb(27);
				MOVid(ins->k, ESI);
				MOVrd(EDI, ECX);
				SUBrd(EDX, ECX);
				CMPrd(ESI, ECX);
				JBb(14);
				ADDrd(EDX, ESI);
				MOVrd(EDI, ECX);
				SUBrd(ESI, ECX);
				CMPid(sizeof(int32_t), ECX);
				JAEb(7);
				ZEROrd(EAX);
				POP(EBX);
				POP(EDI);
				POP(ESI);
				LEAVE();
				RET();
				MOVobd(EBX, ESI, EAX);
				BSWAP(EAX);
				break;

			case BPF_LD|BPF_H|BPF_IND:
				ZEROrd(EAX);
				CMPrd(EDI, EDX);
				JAb(27);
				MOVid(ins->k, ESI);
				MOVrd(EDI, ECX);
				SUBrd(EDX, ECX);
				CMPrd(ESI, ECX);
				JBb(14);
				ADDrd(EDX, ESI);
				MOVrd(EDI, ECX);
				SUBrd(ESI, ECX);
				CMPid(sizeof(int16_t), ECX);
				JAEb(5);
				POP(EBX);
				POP(EDI);
				POP(ESI);
				LEAVE();
				RET();
				MOVobw(EBX, ESI, AX);
				SWAP_AX();
				break;

			case BPF_LD|BPF_B|BPF_IND:
				ZEROrd(EAX);
				CMPrd(EDI, EDX);
				JAEb(13);
				MOVid(ins->k, ESI);
				MOVrd(EDI, ECX);
				SUBrd(EDX, ECX);
				CMPrd(ESI, ECX);
				JAb(5);
				POP(EBX);
				POP(EDI);
				POP(ESI);
				LEAVE();
				RET();
				ADDrd(EDX, ESI);
				MOVobb(EBX, ESI, AL);
				break;

			case BPF_LDX|BPF_MSH|BPF_B:
				MOVid(ins->k, ESI);
				CMPrd(EDI, ESI);
				JBb(7);
				ZEROrd(EAX);
				POP(EBX);
				POP(EDI);
				POP(ESI);
				LEAVE();
				RET();
				ZEROrd(EDX);
				MOVobb(EBX, ESI, DL);
				ANDib(0x0f, DL);
				SHLib(2, EDX);
				break;

			case BPF_LD|BPF_IMM:
				MOVid(ins->k, EAX);
				break;

			case BPF_LDX|BPF_IMM:
				MOVid(ins->k, EDX);
				break;

			case BPF_LD|BPF_MEM:
				MOVrd(EBP, ECX);
				MOVid(((int)ins->k - BPF_MEMWORDS) *
				    sizeof(uint32_t), ESI);
				MOVobd(ECX, ESI, EAX);
				break;

			case BPF_LDX|BPF_MEM:
				MOVrd(EBP, ECX);
				MOVid(((int)ins->k - BPF_MEMWORDS) *
				    sizeof(uint32_t), ESI);
				MOVobd(ECX, ESI, EDX);
				break;

			case BPF_ST:
				/*
				 * XXX this command and the following could
				 * be optimized if the previous instruction
				 * was already of this type
				 */
				MOVrd(EBP, ECX);
				MOVid(((int)ins->k - BPF_MEMWORDS) *
				    sizeof(uint32_t), ESI);
				MOVomd(EAX, ECX, ESI);
				break;

			case BPF_STX:
				MOVrd(EBP, ECX);
				MOVid(((int)ins->k - BPF_MEMWORDS) *
				    sizeof(uint32_t), ESI);
				MOVomd(EDX, ECX, ESI);
				break;

			case BPF_JMP|BPF_JA:
				JUMP(ins->k);
				break;

			case BPF_JMP|BPF_JGT|BPF_K:
			case BPF_JMP|BPF_JGE|BPF_K:
			case BPF_JMP|BPF_JEQ|BPF_K:
			case BPF_JMP|BPF_JSET|BPF_K:
			case BPF_JMP|BPF_JGT|BPF_X:
			case BPF_JMP|BPF_JGE|BPF_X:
			case BPF_JMP|BPF_JEQ|BPF_X:
			case BPF_JMP|BPF_JSET|BPF_X:
				if (ins->jt == ins->jf) {
					JUMP(ins->jt);
					break;
				}
				switch (ins->code) {
				case BPF_JMP|BPF_JGT|BPF_K:
					CMPid(ins->k, EAX);
					JCC(JA, JBE);
					break;

				case BPF_JMP|BPF_JGE|BPF_K:
					CMPid(ins->k, EAX);
					JCC(JAE, JB);
					break;

				case BPF_JMP|BPF_JEQ|BPF_K:
					CMPid(ins->k, EAX);
					JCC(JE, JNE);
					break;

				case BPF_JMP|BPF_JSET|BPF_K:
					TESTid(ins->k, EAX);
					JCC(JNE, JE);
					break;

				case BPF_JMP|BPF_JGT|BPF_X:
					CMPrd(EDX, EAX);
					JCC(JA, JBE);
					break;

				case BPF_JMP|BPF_JGE|BPF_X:
					CMPrd(EDX, EAX);
					JCC(JAE, JB);
					break;

				case BPF_JMP|BPF_JEQ|BPF_X:
					CMPrd(EDX, EAX);
					JCC(JE, JNE);
					break;

				case BPF_JMP|BPF_JSET|BPF_X:
					TESTrd(EDX, EAX);
					JCC(JNE, JE);
					break;
				}
				break;

			case BPF_ALU|BPF_ADD|BPF_X:
				ADDrd(EDX, EAX);
				break;

			case BPF_ALU|BPF_SUB|BPF_X:
				SUBrd(EDX, EAX);
				break;

			case BPF_ALU|BPF_MUL|BPF_X:
				MOVrd(EDX, ECX);
				MULrd(EDX);
				MOVrd(ECX, EDX);
				break;

			case BPF_ALU|BPF_DIV|BPF_X:
			case BPF_ALU|BPF_MOD|BPF_X:
				TESTrd(EDX, EDX);
				if (save_esp) {
					if (fpkt) {
						JNEb(7);
						ZEROrd(EAX);
						POP(EBX);
						POP(EDI);
					} else {
						JNEb(5);
						ZEROrd(EAX);
					}
					POP(ESI);
					LEAVE();
				} else {
					JNEb(3);
					ZEROrd(EAX);
				}
				RET();
				MOVrd(EDX, ECX);
				ZEROrd(EDX);
				DIVrd(ECX);
				if (BPF_OP(ins->code) == BPF_MOD)
					MOVrd(EDX, EAX);
				MOVrd(ECX, EDX);
				break;

			case BPF_ALU|BPF_AND|BPF_X:
				ANDrd(EDX, EAX);
				break;

			case BPF_ALU|BPF_OR|BPF_X:
				ORrd(EDX, EAX);
				break;

			case BPF_ALU|BPF_XOR|BPF_X:
				XORrd(EDX, EAX);
				break;

			case BPF_ALU|BPF_LSH|BPF_X:
				MOVrd(EDX, ECX);
				SHL_CLrb(EAX);
				break;

			case BPF_ALU|BPF_RSH|BPF_X:
				MOVrd(EDX, ECX);
				SHR_CLrb(EAX);
				break;

			case BPF_ALU|BPF_ADD|BPF_K:
				ADD_EAXi(ins->k);
				break;

			case BPF_ALU|BPF_SUB|BPF_K:
				SUB_EAXi(ins->k);
				break;

			case BPF_ALU|BPF_MUL|BPF_K:
				MOVrd(EDX, ECX);
				MOVid(ins->k, EDX);
				MULrd(EDX);
				MOVrd(ECX, EDX);
				break;

			case BPF_ALU|BPF_DIV|BPF_K:
			case BPF_ALU|BPF_MOD|BPF_K:
				MOVrd(EDX, ECX);
				ZEROrd(EDX);
				MOVid(ins->k, ESI);
				DIVrd(ESI);
				if (BPF_OP(ins->code) == BPF_MOD)
					MOVrd(EDX, EAX);
				MOVrd(ECX, EDX);
				break;

			case BPF_ALU|BPF_AND|BPF_K:
				ANDid(ins->k, EAX);
				break;

			case BPF_ALU|BPF_OR|BPF_K:
				ORid(ins->k, EAX);
				break;

			case BPF_ALU|BPF_XOR|BPF_K:
				XORid(ins->k, EAX);
				break;

			case BPF_ALU|BPF_LSH|BPF_K:
				SHLib((ins->k) & 0xff, EAX);
				break;

			case BPF_ALU|BPF_RSH|BPF_K:
				SHRib((ins->k) & 0xff, EAX);
				break;

			case BPF_ALU|BPF_NEG:
				NEGd(EAX);
				break;

			case BPF_MISC|BPF_TAX:
				MOVrd(EAX, EDX);
				break;

			case BPF_MISC|BPF_TXA:
				MOVrd(EDX, EAX);
				break;
			}
			ins++;
		}

		if (pass > 0)
			continue;

		*size = stream.cur_ip;
#ifdef _KERNEL
		stream.ibuf = malloc(*size, M_BPFJIT, M_EXEC | M_NOWAIT);
		if (stream.ibuf == NULL)
			break;
#else
		stream.ibuf = mmap(NULL, *size, PROT_READ | PROT_WRITE,
		    MAP_ANON, -1, 0);
		if (stream.ibuf == MAP_FAILED) {
			stream.ibuf = NULL;
			break;
		}
#endif

		/*
		 * Modify the reference table to contain the offsets and
		 * not the lengths of the instructions.
		 */
		if (fjmp)
			for (i = 1; i < nins + 1; i++)
				stream.refs[i] += stream.refs[i - 1];

		/* Reset the counters. */
		stream.cur_ip = 0;
		stream.bpf_pc = 0;

		/* The second pass creates the actual code. */
		emitm = emit_code;
	}

	/*
	 * The reference table is needed only during compilation,
	 * now we can free it.
	 */
	if (fjmp)
#ifdef _KERNEL
		free(stream.refs, M_BPFJIT);
#else
		free(stream.refs);
#endif

#ifndef _KERNEL
	if (stream.ibuf != NULL &&
	    mprotect(stream.ibuf, *size, PROT_READ | PROT_EXEC) != 0) {
		munmap(stream.ibuf, *size);
		stream.ibuf = NULL;
	}
#endif

	return ((bpf_filter_func)(void *)stream.ibuf);
}
