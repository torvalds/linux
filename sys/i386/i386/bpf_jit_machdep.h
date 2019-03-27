/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2002-2003 NetGroup, Politecnico di Torino (Italy)
 * Copyright (C) 2005-2016 Jung-uk Kim <jkim@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _BPF_JIT_MACHDEP_H_
#define _BPF_JIT_MACHDEP_H_

/*
 * Registers
 */
#define EAX	0
#define ECX	1
#define EDX	2
#define EBX	3
#define ESP	4
#define EBP	5
#define ESI	6
#define EDI	7

#define AX	0
#define CX	1
#define DX	2
#define BX	3
#define SP	4
#define BP	5
#define SI	6
#define DI	7

#define AL	0
#define CL	1
#define DL	2
#define BL	3

/* Optimization flags */
#define	BPF_JIT_FRET	0x01
#define	BPF_JIT_FPKT	0x02
#define	BPF_JIT_FMEM	0x04
#define	BPF_JIT_FJMP	0x08
#define	BPF_JIT_FADK	0x10

#define	BPF_JIT_FLAG_ALL	\
    (BPF_JIT_FPKT | BPF_JIT_FMEM | BPF_JIT_FJMP | BPF_JIT_FADK)

/* A stream of native binary code */
typedef struct bpf_bin_stream {
	/* Current native instruction pointer. */
	int		cur_ip;

	/*
	 * Current BPF instruction pointer, i.e. position in
	 * the BPF program reached by the jitter.
	 */
	int		bpf_pc;

	/* Instruction buffer, contains the generated native code. */
	char		*ibuf;

	/* Jumps reference table. */
	u_int		*refs;
} bpf_bin_stream;

/*
 * Prototype of the emit functions.
 *
 * Different emit functions are used to create the reference table and
 * to generate the actual filtering code. This allows to have simpler
 * instruction macros.
 * The first parameter is the stream that will receive the data.
 * The second one is a variable containing the data.
 * The third one is the length, that can be 1, 2, or 4 since it is possible
 * to emit a byte, a short, or a word at a time.
 */
typedef void (*emit_func)(bpf_bin_stream *stream, u_int value, u_int n);

/*
 * Native instruction macros
 */

/* movl i32,r32 */
#define MOVid(i32, r32) do {						\
	emitm(&stream, (11 << 4) | (1 << 3) | (r32 & 0x7), 1);		\
	emitm(&stream, i32, 4);						\
} while (0)

/* movl sr32,dr32 */
#define MOVrd(sr32, dr32) do {						\
	emitm(&stream, 0x89, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* movl off(sr32),dr32 */
#define MOVodd(off, sr32, dr32) do {					\
	emitm(&stream, 0x8b, 1);					\
	emitm(&stream,							\
	    (1 << 6) | ((dr32 & 0x7) << 3) | (sr32 & 0x7), 1);		\
	emitm(&stream, off, 1);						\
} while (0)

/* movl (sr32,or32,1),dr32 */
#define MOVobd(sr32, or32, dr32) do {					\
	emitm(&stream, 0x8b, 1);					\
	emitm(&stream, ((dr32 & 0x7) << 3) | 4, 1);			\
	emitm(&stream, ((or32 & 0x7) << 3) | (sr32 & 0x7), 1);		\
} while (0)

/* movw (sr32,or32,1),dr16 */
#define MOVobw(sr32, or32, dr16) do {					\
	emitm(&stream, 0x8b66, 2);					\
	emitm(&stream, ((dr16 & 0x7) << 3) | 4, 1);			\
	emitm(&stream, ((or32 & 0x7) << 3) | (sr32 & 0x7), 1);		\
} while (0)

/* movb (sr32,or32,1),dr8 */
#define MOVobb(sr32, or32, dr8) do {					\
	emitm(&stream, 0x8a, 1);					\
	emitm(&stream, ((dr8 & 0x7) << 3) | 4, 1);			\
	emitm(&stream, ((or32 & 0x7) << 3) | (sr32 & 0x7), 1);		\
} while (0)

/* movl sr32,(dr32,or32,1) */
#define MOVomd(sr32, dr32, or32) do {					\
	emitm(&stream, 0x89, 1);					\
	emitm(&stream, ((sr32 & 0x7) << 3) | 4, 1);			\
	emitm(&stream, ((or32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* bswapl dr32 */
#define BSWAP(dr32) do {						\
	emitm(&stream, 0xf, 1);						\
	emitm(&stream, (0x19 << 3) | dr32, 1);				\
} while (0)

/* xchgb %al,%ah */
#define SWAP_AX() do {							\
	emitm(&stream, 0xc486, 2);					\
} while (0)

/* pushl r32 */
#define PUSH(r32) do {							\
	emitm(&stream, (5 << 4) | (0 << 3) | (r32 & 0x7), 1);		\
} while (0)

/* popl r32 */
#define POP(r32) do {							\
	emitm(&stream, (5 << 4) | (1 << 3) | (r32 & 0x7), 1);		\
} while (0)

/* leave */
#define LEAVE() do {							\
	emitm(&stream, 0xc9, 1);					\
} while (0)

/* ret */
#define RET() do {							\
	emitm(&stream, 0xc3, 1);					\
} while (0)

/* addl sr32,dr32 */
#define ADDrd(sr32, dr32) do {						\
	emitm(&stream, 0x01, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* addl i32,%eax */
#define ADD_EAXi(i32) do {						\
	emitm(&stream, 0x05, 1);					\
	emitm(&stream, i32, 4);						\
} while (0)

/* addl i8,r32 */
#define ADDib(i8, r32) do {						\
	emitm(&stream, 0x83, 1);					\
	emitm(&stream, (24 << 3) | r32, 1);				\
	emitm(&stream, i8, 1);						\
} while (0)

/* subl sr32,dr32 */
#define SUBrd(sr32, dr32) do {						\
	emitm(&stream, 0x29, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* subl i32,%eax */
#define SUB_EAXi(i32) do {						\
	emitm(&stream, 0x2d, 1);					\
	emitm(&stream, i32, 4);						\
} while (0)

/* subl i8,r32 */
#define SUBib(i8, r32) do {						\
	emitm(&stream, 0x83, 1);					\
	emitm(&stream, (29 << 3) | (r32 & 0x7), 1);			\
	emitm(&stream, i8, 1);						\
} while (0)

/* mull r32 */
#define MULrd(r32) do {							\
	emitm(&stream, 0xf7, 1);					\
	emitm(&stream, (7 << 5) | (r32 & 0x7), 1);			\
} while (0)

/* divl r32 */
#define DIVrd(r32) do {							\
	emitm(&stream, 0xf7, 1);					\
	emitm(&stream, (15 << 4) | (r32 & 0x7), 1);			\
} while (0)

/* andb i8,r8 */
#define ANDib(i8, r8) do {						\
	if (r8 == AL) {							\
		emitm(&stream, 0x24, 1);				\
	} else {							\
		emitm(&stream, 0x80, 1);				\
		emitm(&stream, (7 << 5) | r8, 1);			\
	}								\
	emitm(&stream, i8, 1);						\
} while (0)

/* andl i32,r32 */
#define ANDid(i32, r32) do {						\
	if (r32 == EAX) {						\
		emitm(&stream, 0x25, 1);				\
	} else {							\
		emitm(&stream, 0x81, 1);				\
		emitm(&stream, (7 << 5) | r32, 1);			\
	}								\
	emitm(&stream, i32, 4);						\
} while (0)

/* andl sr32,dr32 */
#define ANDrd(sr32, dr32) do {						\
	emitm(&stream, 0x21, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* testl i32,r32 */
#define TESTid(i32, r32) do {						\
	if (r32 == EAX) {						\
		emitm(&stream, 0xa9, 1);				\
	} else {							\
		emitm(&stream, 0xf7, 1);				\
		emitm(&stream, (3 << 6) | r32, 1);			\
	}								\
	emitm(&stream, i32, 4);						\
} while (0)

/* testl sr32,dr32 */
#define TESTrd(sr32, dr32) do {						\
	emitm(&stream, 0x85, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* orl sr32,dr32 */
#define ORrd(sr32, dr32) do {						\
	emitm(&stream, 0x09, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* orl i32,r32 */
#define ORid(i32, r32) do {						\
	if (r32 == EAX) {						\
		emitm(&stream, 0x0d, 1);				\
	} else {							\
		emitm(&stream, 0x81, 1);				\
		emitm(&stream, (25 << 3) | r32, 1);			\
	}								\
	emitm(&stream, i32, 4);						\
} while (0)

/* xorl sr32,dr32 */
#define XORrd(sr32, dr32) do {						\
	emitm(&stream, 0x31, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* xorl i32,r32 */
#define XORid(i32, r32) do {						\
	if (r32 == EAX) {						\
		emitm(&stream, 0x35, 1);				\
	} else {							\
		emitm(&stream, 0x81, 1);				\
		emitm(&stream, (25 << 3) | r32, 1);			\
	}								\
	emitm(&stream, i32, 4);						\
} while (0)

/* shll i8,r32 */
#define SHLib(i8, r32) do {						\
	emitm(&stream, 0xc1, 1);					\
	emitm(&stream, (7 << 5) | (r32 & 0x7), 1);			\
	emitm(&stream, i8, 1);						\
} while (0)

/* shll %cl,dr32 */
#define SHL_CLrb(dr32) do {						\
	emitm(&stream, 0xd3, 1);					\
	emitm(&stream, (7 << 5) | (dr32 & 0x7), 1);			\
} while (0)

/* shrl i8,r32 */
#define SHRib(i8, r32) do {						\
	emitm(&stream, 0xc1, 1);					\
	emitm(&stream, (29 << 3) | (r32 & 0x7), 1);			\
	emitm(&stream, i8, 1);						\
} while (0)

/* shrl %cl,dr32 */
#define SHR_CLrb(dr32) do {						\
	emitm(&stream, 0xd3, 1);					\
	emitm(&stream, (29 << 3) | (dr32 & 0x7), 1);			\
} while (0)

/* negl r32 */
#define NEGd(r32) do {							\
	emitm(&stream, 0xf7, 1);					\
	emitm(&stream, (27 << 3) | (r32 & 0x7), 1);			\
} while (0)

/* cmpl sr32,dr32 */
#define CMPrd(sr32, dr32) do {						\
	emitm(&stream, 0x39, 1);					\
	emitm(&stream,							\
	    (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1);		\
} while (0)

/* cmpl i32,dr32 */
#define CMPid(i32, dr32) do {						\
	if (dr32 == EAX){						\
		emitm(&stream, 0x3d, 1);				\
		emitm(&stream, i32, 4);					\
	} else {							\
		emitm(&stream, 0x81, 1);				\
		emitm(&stream, (0x1f << 3) | (dr32 & 0x7), 1);		\
		emitm(&stream, i32, 4);					\
	}								\
} while (0)

/* jb off8 */
#define JBb(off8) do {							\
	emitm(&stream, 0x72, 1);					\
	emitm(&stream, off8, 1);					\
} while (0)

/* jae off8 */
#define JAEb(off8) do {							\
	emitm(&stream, 0x73, 1);					\
	emitm(&stream, off8, 1);					\
} while (0)

/* jne off8 */
#define JNEb(off8) do {							\
	emitm(&stream, 0x75, 1);					\
	emitm(&stream, off8, 1);					\
} while (0)

/* ja off8 */
#define JAb(off8) do {							\
	emitm(&stream, 0x77, 1);					\
	emitm(&stream, off8, 1);					\
} while (0)

/* jmp off32 */
#define JMP(off32) do {							\
	emitm(&stream, 0xe9, 1);					\
	emitm(&stream, off32, 4);					\
} while (0)

/* xorl r32,r32 */
#define ZEROrd(r32) do {						\
	emitm(&stream, 0x31, 1);					\
	emitm(&stream, (3 << 6) | ((r32 & 0x7) << 3) | (r32 & 0x7), 1);	\
} while (0)

/*
 * Conditional long jumps
 */
#define	JB	0x82
#define	JAE	0x83
#define	JE	0x84
#define	JNE	0x85
#define	JBE	0x86
#define	JA	0x87

#define	JCC(t, f) do {							\
	if (ins->jt != 0 && ins->jf != 0) {				\
		/* 5 is the size of the following jmp */		\
		emitm(&stream, ((t) << 8) | 0x0f, 2);			\
		emitm(&stream, stream.refs[stream.bpf_pc + ins->jt] -	\
		    stream.refs[stream.bpf_pc] + 5, 4);			\
		JMP(stream.refs[stream.bpf_pc + ins->jf] -		\
		    stream.refs[stream.bpf_pc]);			\
	} else if (ins->jt != 0) {					\
		emitm(&stream, ((t) << 8) | 0x0f, 2);			\
		emitm(&stream, stream.refs[stream.bpf_pc + ins->jt] -	\
		    stream.refs[stream.bpf_pc], 4);			\
	} else {							\
		emitm(&stream, ((f) << 8) | 0x0f, 2);			\
		emitm(&stream, stream.refs[stream.bpf_pc + ins->jf] -	\
		    stream.refs[stream.bpf_pc], 4);			\
	}								\
} while (0)

#define	JUMP(off) do {							\
	if ((off) != 0)							\
		JMP(stream.refs[stream.bpf_pc + (off)] -		\
		    stream.refs[stream.bpf_pc]);			\
} while (0)

#endif	/* _BPF_JIT_MACHDEP_H_ */
