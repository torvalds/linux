/*	$OpenBSD: goodfreg.c,v 1.2 2003/07/12 04:23:16 jason Exp $	*/

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
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include "fpregs.h"

struct fpquad {
        u_int32_t x1;
        u_int32_t x2;
        u_int32_t x3;
        u_int32_t x4;
};

void asm_ldq_f0(struct fpquad *);
void asm_ldq_f4(struct fpquad *);
void asm_ldq_f8(struct fpquad *);
void asm_ldq_f12(struct fpquad *);
void asm_ldq_f16(struct fpquad *);
void asm_ldq_f20(struct fpquad *);
void asm_ldq_f24(struct fpquad *);
void asm_ldq_f28(struct fpquad *);
void asm_ldq_f32(struct fpquad *);
void asm_ldq_f36(struct fpquad *);
void asm_ldq_f40(struct fpquad *);
void asm_ldq_f44(struct fpquad *);
void asm_ldq_f48(struct fpquad *);
void asm_ldq_f52(struct fpquad *);
void asm_ldq_f56(struct fpquad *);
void asm_ldq_f60(struct fpquad *);

void asm_stq_f0(struct fpquad *);
void asm_stq_f4(struct fpquad *);
void asm_stq_f8(struct fpquad *);
void asm_stq_f12(struct fpquad *);
void asm_stq_f16(struct fpquad *);
void asm_stq_f20(struct fpquad *);
void asm_stq_f24(struct fpquad *);
void asm_stq_f28(struct fpquad *);
void asm_stq_f32(struct fpquad *);
void asm_stq_f36(struct fpquad *);
void asm_stq_f40(struct fpquad *);
void asm_stq_f44(struct fpquad *);
void asm_stq_f48(struct fpquad *);
void asm_stq_f52(struct fpquad *);
void asm_stq_f56(struct fpquad *);
void asm_stq_f60(struct fpquad *);

int compare_regs(union fpregs *, union fpregs *);
void dump_reg(union fpregs *);
void dump_regs(union fpregs *, union fpregs *, union fpregs *);
int compare_quads(struct fpquad *, struct fpquad *);
void check_saves(union fpregs *, union fpregs *, union fpregs *);
void c_stq(union fpregs *, int, struct fpquad *);
void c_ldq(union fpregs *, int, struct fpquad *);
void asm_ldq(int, struct fpquad *);
void asm_stq(int, struct fpquad *);
void check_reg(int, union fpregs *, union fpregs *, union fpregs *);
void check_regs(union fpregs *, union fpregs *, union fpregs *);
int main(void);

int
compare_regs(union fpregs *fr1, union fpregs *fr2)
{
	return (memcmp(fr1, fr2, sizeof(*fr2)));
}

void
dump_reg(union fpregs *fr)
{
	int i;

	for (i = 0; i < 64; i++) {
		if ((i & 3) == 0)
			printf("f%-2d:", i);
		printf(" %08x", fr->f_reg32[i]);
		if ((i & 3) == 3)
			printf("\n");
	}
}

void
dump_regs(union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
{
	printf("BEFORE ASM\n");
	dump_reg(fr1);
	printf("AFTER ASM\n");
	dump_reg(fr2);
	printf("MANUAL\n");
	dump_reg(fr3);
}

int
compare_quads(struct fpquad *q1, struct fpquad *q2)
{
	return (memcmp(q1, q2, sizeof(*q2)));
}

/*
 * Verify that savefpregs, loadfpregs, and initfpregs actually seem to work.
 */
void
check_saves(union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
{
	memset(fr1, 0x55, sizeof(*fr1));
	memset(fr2, 0x55, sizeof(*fr2));
	memset(fr3, 0x55, sizeof(*fr3));
	loadfpregs(fr1);
	if (compare_regs(fr1, fr2))
		errx(1, "check_saves: loadfpregs1 differs");

	savefpregs(fr2);
	if (compare_regs(fr1, fr2) || compare_regs(fr2, fr3))
		errx(1, "check_saves: savefpregs1 differs");

	memset(fr1, 0xaa, sizeof(*fr1));
	memset(fr2, 0xaa, sizeof(*fr2));
	memset(fr3, 0xaa, sizeof(*fr3));
	loadfpregs(fr1);
	if (compare_regs(fr1, fr2))
		errx(1, "check_saves: loadfpregs2 differs");

	savefpregs(fr2);
	if (compare_regs(fr1, fr2) || compare_regs(fr2, fr3))
		errx(1, "check_saves: savefpregs2 differs");

	memset(fr1, 0xff, sizeof(*fr1));
	initfpregs(fr2);
	if (compare_regs(fr1, fr2))
		errx(1, "check_saves: initfpregs differs");
}

void
c_stq(union fpregs *frp, int freg, struct fpquad *q)
{
	q->x1 = frp->f_reg32[freg];
	q->x2 = frp->f_reg32[freg + 1];
	q->x3 = frp->f_reg32[freg + 2];
	q->x4 = frp->f_reg32[freg + 3];
}

void
c_ldq(union fpregs *frp, int freg, struct fpquad *q)
{
	frp->f_reg32[freg] = q->x1;
	frp->f_reg32[freg + 1] = q->x2;
	frp->f_reg32[freg + 2] = q->x3;
	frp->f_reg32[freg + 3] = q->x4;
}

void
asm_ldq(int freg, struct fpquad *q)
{
	switch (freg) {
	case 0:
		asm_ldq_f0(q);
		break;
	case 4:
		asm_ldq_f4(q);
		break;
	case 8:
		asm_ldq_f8(q);
		break;
	case 12:
		asm_ldq_f12(q);
		break;
	case 16:
		asm_ldq_f16(q);
		break;
	case 20:
		asm_ldq_f20(q);
		break;
	case 24:
		asm_ldq_f24(q);
		break;
	case 28:
		asm_ldq_f28(q);
		break;
	case 32:
		asm_ldq_f32(q);
		break;
	case 36:
		asm_ldq_f36(q);
		break;
	case 40:
		asm_ldq_f40(q);
		break;
	case 44:
		asm_ldq_f44(q);
		break;
	case 48:
		asm_ldq_f48(q);
		break;
	case 52:
		asm_ldq_f52(q);
		break;
	case 56:
		asm_ldq_f56(q);
		break;
	case 60:
		asm_ldq_f60(q);
		break;
	default:
		errx(1, "asm_ldq: bad freg %d", freg);
	}
}

void
asm_stq(int freg, struct fpquad *q)
{
	switch (freg) {
	case 0:
		asm_stq_f0(q);
		break;
	case 4:
		asm_stq_f4(q);
		break;
	case 8:
		asm_stq_f8(q);
		break;
	case 12:
		asm_stq_f12(q);
		break;
	case 16:
		asm_stq_f16(q);
		break;
	case 20:
		asm_stq_f20(q);
		break;
	case 24:
		asm_stq_f24(q);
		break;
	case 28:
		asm_stq_f28(q);
		break;
	case 32:
		asm_stq_f32(q);
		break;
	case 36:
		asm_stq_f36(q);
		break;
	case 40:
		asm_stq_f40(q);
		break;
	case 44:
		asm_stq_f44(q);
		break;
	case 48:
		asm_stq_f48(q);
		break;
	case 52:
		asm_stq_f52(q);
		break;
	case 56:
		asm_stq_f56(q);
		break;
	case 60:
		asm_stq_f60(q);
		break;
	default:
		errx(1, "asm_stq: bad freg %d", freg);
	}
}

void
check_reg(int freg, union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
 {
	struct fpquad q1, q2, q3;

	initfpregs(fr1);
	initfpregs(fr2);
	initfpregs(fr3);

	loadfpregs(fr1);
	q1.x1 = 0x01234567;
	q1.x2 = 0x89abcdef;
	q1.x3 = 0x55aa55aa;
	q1.x4 = 0xa5a5a5a5;
	asm_ldq(freg, &q1);
	savefpregs(fr2);

	c_ldq(fr3, freg, &q1);

	if (compare_regs(fr2, fr3)) {
		errx(1, "ldq: c/asm differ");
		dump_regs(fr1, fr2, fr3);
	}

	q2.x1 = q2.x2 = q2.x3 = q2.x4 = 0;
	q3.x1 = q3.x2 = q3.x3 = q3.x4 = 1;
	asm_stq(freg, &q2);
	c_stq(fr3, freg, &q3);

	if (compare_quads(&q1, &q3))
		errx(1, "c_stq %d differs...", freg);

	if (compare_quads(&q1, &q2))
		errx(1, "asm_stq %d differs...", freg);
}

void
check_regs(union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
{
	int i;

	for (i = 0; i < 16; i++)
		check_reg(i * 4, fr1, fr2, fr3);
}

int
main()
{
	union fpregs fr1, fr2, fr3;

	check_saves(&fr1, &fr2, &fr3);
	check_regs(&fr1, &fr2, &fr3);
	return (0);
}
