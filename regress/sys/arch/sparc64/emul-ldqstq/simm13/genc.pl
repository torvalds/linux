#!/usr/bin/perl
#	$OpenBSD: genc.pl,v 1.5 2024/08/06 05:39:48 claudio Exp $
#
# Copyright (c) 2003 Jason L. Wright (jason@thought.net)
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

print <<MY__EOF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
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
void c_ldq(union fpregs *, int, struct fpquad *);
void test_asm_ldq(char *, void (*)(struct fpquad *));

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
			printf("\\n");
	}
}

void
dump_regs(union fpregs *fr1, union fpregs *fr2, union fpregs *fr3)
{
	printf("BEFORE ASM\\n");
	dump_reg(fr1);
	printf("AFTER ASM\\n");
	dump_reg(fr2);
	printf("MANUAL\\n");
	dump_reg(fr3);
}

void 
c_ldq(union fpregs *frp, int freg, struct fpquad *q)
{
	frp->f_reg32[freg] = q->x1;
	frp->f_reg32[freg + 1] = q->x2;
	frp->f_reg32[freg + 2] = q->x3;
	frp->f_reg32[freg + 3] = q->x4;
}

MY__EOF
;
for ($i = -4096; $i <= 4095; $i++) {
	if ($i < 0) {
		$name = -$i;
		$name = "_$name";
	} else {
		$name = $i;
	}
	print "void simm13_ldq_$name(struct fpquad *);\n";
	print "void simm13_stq_$name(struct fpquad *);\n";
}

print <<MY__EOF
void
test_asm_ldq(char *desc, void (*func)(struct fpquad *))
{
	union fpregs fr1, fr2, fr3;
	struct fpquad q;

	q.x1 = 0x01234567;
	q.x2 = 0x89abcdef;
	q.x3 = 0x55aa55aa;
	q.x4 = 0xaaaa5555;

	initfpregs(&fr1);
	initfpregs(&fr2);
	initfpregs(&fr3);

	loadfpregs(&fr1);
	(*func)(&q);
	savefpregs(&fr2);

	c_ldq(&fr3, 0, &q);

	if (compare_regs(&fr2, &fr3)) {
		dump_regs(&fr1, &fr2, &fr3);
		exit(1);
		errx(1, "%s failed", desc);
	}
}
MY__EOF
;

$j = 0;
for ($i = -4096; $i <= 4095; $i++) {
	if (($i % 256) == 0) {
		print "void test_$j(void);\n";
		$j++;
	}
}

$j = 0;
for ($i = -4096; $i <= 4095; $i++) {
	if ($i < 0) {
		$name = -$i;
		$name = "_$name";
	} else {
		$name = $i;
	}
	if (($i % 256) == 0) {
		print "void\n";
		print "test_$j(void) {\n";
		$j++;
	}
	print "	test_asm_ldq(\"ldq [%o + $i], %f0\", simm13_ldq_$name);\n";
	if (($i % 256) == 255) {
		print "}\n";
	}
}

print <<MY__EOF
int
main(void)
{
MY__EOF
;

for ($i = 0; $i < $j; $i++) {
	print "	test_$i();\n";
}
print "	return (0);\n";
print "}\n";
