/*	$OpenBSD: bpf_image.c,v 1.12 2024/04/05 18:01:56 deraadt Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1992, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <string.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

char *
bpf_image(const struct bpf_insn *p, int n)
{
	int v;
	char *fmt, *op;
	static char image[256];
	char operand[64];

	v = p->k;
	switch (p->code) {

	default:
		op = "unimp";
		fmt = "0x%x";
		v = p->code;
		break;

	case BPF_RET|BPF_K:
		op = "ret";
		fmt = "#%d";
		break;

	case BPF_RET|BPF_A:
		op = "ret";
		fmt = "";
		break;

	case BPF_LD|BPF_W|BPF_ABS:
		op = "ld";
		fmt = "[%d]";
		break;

	case BPF_LD|BPF_H|BPF_ABS:
		op = "ldh";
		fmt = "[%d]";
		break;

	case BPF_LD|BPF_B|BPF_ABS:
		op = "ldb";
		fmt = "[%d]";
		break;

	case BPF_LD|BPF_W|BPF_LEN:
		op = "ld";
		fmt = "#pktlen";
		break;

	case BPF_LD|BPF_W|BPF_RND:
		op = "ld";
		fmt = "#random";
		break;

	case BPF_LD|BPF_W|BPF_IND:
		op = "ld";
		fmt = "[x + %d]";
		break;

	case BPF_LD|BPF_H|BPF_IND:
		op = "ldh";
		fmt = "[x + %d]";
		break;

	case BPF_LD|BPF_B|BPF_IND:
		op = "ldb";
		fmt = "[x + %d]";
		break;

	case BPF_LD|BPF_IMM:
		op = "ld";
		fmt = "#0x%x";
		break;

	case BPF_LDX|BPF_IMM:
		op = "ldx";
		fmt = "#0x%x";
		break;

	case BPF_LDX|BPF_MSH|BPF_B:
		op = "ldxb";
		fmt = "4*([%d]&0xf)";
		break;

	case BPF_LD|BPF_MEM:
		op = "ld";
		fmt = "M[%d]";
		break;

	case BPF_LDX|BPF_MEM:
		op = "ldx";
		fmt = "M[%d]";
		break;

	case BPF_ST:
		op = "st";
		fmt = "M[%d]";
		break;

	case BPF_STX:
		op = "stx";
		fmt = "M[%d]";
		break;

	case BPF_JMP|BPF_JA:
		op = "ja";
		fmt = "%d";
		v = n + 1 + p->k;
		break;

	case BPF_JMP|BPF_JGT|BPF_K:
		op = "jgt";
		fmt = "#0x%x";
		break;

	case BPF_JMP|BPF_JGE|BPF_K:
		op = "jge";
		fmt = "#0x%x";
		break;

	case BPF_JMP|BPF_JEQ|BPF_K:
		op = "jeq";
		fmt = "#0x%x";
		break;

	case BPF_JMP|BPF_JSET|BPF_K:
		op = "jset";
		fmt = "#0x%x";
		break;

	case BPF_JMP|BPF_JGT|BPF_X:
		op = "jgt";
		fmt = "x";
		break;

	case BPF_JMP|BPF_JGE|BPF_X:
		op = "jge";
		fmt = "x";
		break;

	case BPF_JMP|BPF_JEQ|BPF_X:
		op = "jeq";
		fmt = "x";
		break;

	case BPF_JMP|BPF_JSET|BPF_X:
		op = "jset";
		fmt = "x";
		break;

	case BPF_ALU|BPF_ADD|BPF_X:
		op = "add";
		fmt = "x";
		break;

	case BPF_ALU|BPF_SUB|BPF_X:
		op = "sub";
		fmt = "x";
		break;

	case BPF_ALU|BPF_MUL|BPF_X:
		op = "mul";
		fmt = "x";
		break;

	case BPF_ALU|BPF_DIV|BPF_X:
		op = "div";
		fmt = "x";
		break;

	case BPF_ALU|BPF_AND|BPF_X:
		op = "and";
		fmt = "x";
		break;

	case BPF_ALU|BPF_OR|BPF_X:
		op = "or";
		fmt = "x";
		break;

	case BPF_ALU|BPF_LSH|BPF_X:
		op = "lsh";
		fmt = "x";
		break;

	case BPF_ALU|BPF_RSH|BPF_X:
		op = "rsh";
		fmt = "x";
		break;

	case BPF_ALU|BPF_ADD|BPF_K:
		op = "add";
		fmt = "#%d";
		break;

	case BPF_ALU|BPF_SUB|BPF_K:
		op = "sub";
		fmt = "#%d";
		break;

	case BPF_ALU|BPF_MUL|BPF_K:
		op = "mul";
		fmt = "#%d";
		break;

	case BPF_ALU|BPF_DIV|BPF_K:
		op = "div";
		fmt = "#%d";
		break;

	case BPF_ALU|BPF_AND|BPF_K:
		op = "and";
		fmt = "#0x%x";
		break;

	case BPF_ALU|BPF_OR|BPF_K:
		op = "or";
		fmt = "#0x%x";
		break;

	case BPF_ALU|BPF_LSH|BPF_K:
		op = "lsh";
		fmt = "#%d";
		break;

	case BPF_ALU|BPF_RSH|BPF_K:
		op = "rsh";
		fmt = "#%d";
		break;

	case BPF_ALU|BPF_NEG:
		op = "neg";
		fmt = "";
		break;

	case BPF_MISC|BPF_TAX:
		op = "tax";
		fmt = "";
		break;

	case BPF_MISC|BPF_TXA:
		op = "txa";
		fmt = "";
		break;
	}
	(void)snprintf(operand, sizeof operand, fmt, v);
	(void)snprintf(image, sizeof image,
		      (BPF_CLASS(p->code) == BPF_JMP &&
		       BPF_OP(p->code) != BPF_JA) ?
		      "(%03d) %-8s %-16s jt %d\tjf %d"
		      : "(%03d) %-8s %s",
		      n, op, operand, n + 1 + p->jt, n + 1 + p->jf);
	return image;
}
