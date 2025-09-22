/*	$OpenBSD: asi.c,v 1.2 2003/07/12 07:09:25 jason Exp $	*/

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

#include <machine/psl.h>
#include <machine/ctlreg.h>

#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include "fpregs.h"

struct fpquad {
        u_int32_t x1;
        u_int32_t x2;
        u_int32_t x3;
        u_int32_t x4;
};

int compare_regs(union fpregs *, union fpregs *);
void dump_reg(union fpregs *);
void dump_regs(union fpregs *, union fpregs *, union fpregs *);
int compare_quads(struct fpquad *, struct fpquad *);
void c_stqa_asi(int, union fpregs *, struct fpquad *);
void c_ldqa_asi(int, union fpregs *, struct fpquad *);
void asm_ldqa_asi(int, struct fpquad *);
void asm_stqa_asi(int, struct fpquad *);
void asm_ldqa_imm(int asi, struct fpquad *);
void asm_stqa_imm(int asi, struct fpquad *);
void asm_stqa_primary(struct fpquad *);
void asm_ldqa_primary(struct fpquad *);
void asm_stqa_secondary(struct fpquad *);
void asm_ldqa_secondary(struct fpquad *);
void asm_stqa_primary_nofault(struct fpquad *);
void asm_ldqa_primary_nofault(struct fpquad *);
void asm_stqa_secondary_nofault(struct fpquad *);
void asm_ldqa_secondary_nofault(struct fpquad *);
void asm_stqa_primary_little(struct fpquad *);
void asm_ldqa_primary_little(struct fpquad *);
void asm_stqa_secondary_little(struct fpquad *);
void asm_ldqa_secondary_little(struct fpquad *);
void asm_stqa_primary_nofault_little(struct fpquad *);
void asm_ldqa_primary_nofault_little(struct fpquad *);
void asm_stqa_secondary_nofault_little(struct fpquad *);
void asm_ldqa_secondary_nofault_little(struct fpquad *);
void check_asi(int, union fpregs *, union fpregs *, union fpregs *);
void check_asi_asi(int, union fpregs *, union fpregs *, union fpregs *);
void check_asi_imm(int, union fpregs *, union fpregs *, union fpregs *);
int main(int, char *[]);

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

void
c_stqa_asi(int asi, union fpregs *frp, struct fpquad *q)
{
	if (asi == ASI_PRIMARY ||
	    asi == ASI_PRIMARY_NOFAULT ||
	    asi == ASI_SECONDARY ||
	    asi == ASI_SECONDARY_NOFAULT) {
		q->x1 = frp->f_reg32[0];
		q->x2 = frp->f_reg32[0 + 1];
		q->x3 = frp->f_reg32[0 + 2];
		q->x4 = frp->f_reg32[0 + 3];
		return;
	}
	if (asi == ASI_PRIMARY_LITTLE ||
	    asi == ASI_PRIMARY_NOFAULT_LITTLE ||
	    asi == ASI_SECONDARY_LITTLE ||
	    asi == ASI_SECONDARY_NOFAULT_LITTLE) {
		q->x4 = htole32(frp->f_reg32[0]);
		q->x3 = htole32(frp->f_reg32[0 + 1]);
		q->x2 = htole32(frp->f_reg32[0 + 2]);
		q->x1 = htole32(frp->f_reg32[0 + 3]);
		return;
	}
	errx(1, "c_stqa_asi: bad asi %d", asi);
}

void
c_ldqa_asi(int asi, union fpregs *frp, struct fpquad *q)
{
	if (asi == ASI_PRIMARY ||
	    asi == ASI_PRIMARY_NOFAULT ||
	    asi == ASI_SECONDARY ||
	    asi == ASI_SECONDARY_NOFAULT) {
		frp->f_reg32[0] = q->x1;
		frp->f_reg32[0 + 1] = q->x2;
		frp->f_reg32[0 + 2] = q->x3;
		frp->f_reg32[0 + 3] = q->x4;
		return;
	}
	if (asi == ASI_PRIMARY_LITTLE ||
	    asi == ASI_PRIMARY_NOFAULT_LITTLE ||
	    asi == ASI_SECONDARY_LITTLE ||
	    asi == ASI_SECONDARY_NOFAULT_LITTLE) {
		frp->f_reg32[0] = htole32(q->x4);
		frp->f_reg32[0 + 1] = htole32(q->x3);
		frp->f_reg32[0 + 2] = htole32(q->x2);
		frp->f_reg32[0 + 3] = htole32(q->x1);
		return;
	}
	errx(1, "c_ldqa_asi: bad asi %d", asi);
}

void
asm_stqa_imm(int asi, struct fpquad *q)
{
	switch (asi) {
	case ASI_PRIMARY:
		asm_stqa_primary(q);
		break;
	case ASI_SECONDARY:
		asm_stqa_secondary(q);
		break;
	case ASI_PRIMARY_NOFAULT:
		asm_stqa_primary_nofault(q);
		break;
	case ASI_SECONDARY_NOFAULT:
		asm_stqa_secondary_nofault(q);
		break;
	case ASI_PRIMARY_LITTLE:
		asm_stqa_primary_little(q);
		break;
	case ASI_SECONDARY_LITTLE:
		asm_stqa_secondary_little(q);
		break;
	case ASI_PRIMARY_NOFAULT_LITTLE:
		asm_stqa_primary_nofault_little(q);
		break;
	case ASI_SECONDARY_NOFAULT_LITTLE:
		asm_stqa_secondary_nofault_little(q);
		break;
	default:
		errx(1, "asm_stqa_imm: bad asi %d", asi);
	}
}

void
asm_ldqa_imm(int asi, struct fpquad *q)
{
	switch (asi) {
	case ASI_PRIMARY:
		asm_ldqa_primary(q);
		break;
	case ASI_SECONDARY:
		asm_ldqa_secondary(q);
		break;
	case ASI_PRIMARY_NOFAULT:
		asm_ldqa_primary_nofault(q);
		break;
	case ASI_SECONDARY_NOFAULT:
		asm_ldqa_secondary_nofault(q);
		break;
	case ASI_PRIMARY_LITTLE:
		asm_ldqa_primary_little(q);
		break;
	case ASI_SECONDARY_LITTLE:
		asm_ldqa_secondary_little(q);
		break;
	case ASI_PRIMARY_NOFAULT_LITTLE:
		asm_ldqa_primary_nofault_little(q);
		break;
	case ASI_SECONDARY_NOFAULT_LITTLE:
		asm_ldqa_secondary_nofault_little(q);
		break;
	default:
		errx(1, "asm_ldqa_imm: bad asi %d", asi);
	}
}

void
check_asi(int asi, union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
{
	check_asi_asi(asi, fr1, fr2, fr3);
	check_asi_imm(asi, fr1, fr2, fr3);
}

void
check_asi_asi(int asi, union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
{
	struct fpquad q1;

	initfpregs(fr1);
	initfpregs(fr2);
	initfpregs(fr3);

	q1.x1 = 0x01234567;
	q1.x2 = 0x89abcdef;
	q1.x3 = 0x55aa55aa;
	q1.x4 = 0xa5a5a5a5;

	loadfpregs(fr1);
	asm_ldqa_asi(asi, &q1);
	savefpregs(fr2);

	c_ldqa_asi(asi, fr3, &q1);

	if (compare_regs(fr2, fr3)) {
		printf("ASI 0x%x failed\n", asi);
		dump_regs(fr1, fr2, fr3);
		exit(1);
	}
}

void
check_asi_imm(int asi, union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
{
	struct fpquad q1;

	initfpregs(fr1);
	initfpregs(fr2);
	initfpregs(fr3);

	q1.x1 = 0x01234567;
	q1.x2 = 0x89abcdef;
	q1.x3 = 0x55aa55aa;
	q1.x4 = 0xa5a5a5a5;

	loadfpregs(fr1);
	asm_ldqa_imm(asi, &q1);
	savefpregs(fr2);

	c_ldqa_asi(asi, fr3, &q1);

	if (compare_regs(fr2, fr3)) {
		printf("ASI 0x%x failed\n", asi);
		dump_regs(fr1, fr2, fr3);
		exit(1);
	}
}

int
main(int argc, char *argv[])
{
	union fpregs fr1, fr2, fr3;

	check_asi(ASI_PRIMARY, &fr1, &fr2, &fr3);
	check_asi(ASI_PRIMARY_NOFAULT, &fr1, &fr2, &fr3);
	check_asi(ASI_PRIMARY_LITTLE, &fr1, &fr2, &fr3);
	check_asi(ASI_PRIMARY_NOFAULT_LITTLE, &fr1, &fr2, &fr3);
	check_asi(ASI_SECONDARY, &fr1, &fr2, &fr3);
	check_asi(ASI_SECONDARY_NOFAULT, &fr1, &fr2, &fr3);
	check_asi(ASI_SECONDARY_LITTLE, &fr1, &fr2, &fr3);
	check_asi(ASI_SECONDARY_NOFAULT_LITTLE, &fr1, &fr2, &fr3);
	return (0);
}
