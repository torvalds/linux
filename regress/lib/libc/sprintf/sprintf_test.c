/*	$OpenBSD: sprintf_test.c,v 1.5 2021/09/02 09:46:21 deraadt Exp $ */

/*
 * Copyright (c) 2003 Theo de Raadt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

char correct[] =
	"|xx 01 02 03 04\n"
	"|xx 05 06 07 08\n"
	"|xx 09 10 11 12\n"
	"|xx 13 14 15 16\n"
	"|xx 17 18 19 20\n"
	"|xx 21 22 23 24\n"
	"|xx 25 26 27 28\n"
	"|xx 29 30 31 32\n"
	"|xx 33 34 35 36\n"
	"|xx 37 38 39 40\n"
	"|xx 41 42 43 44\n"
	"|xx 45 -1 1 -1 1\n";

char correct2[] =
	"1 0 -1 1 1 2 1 3 -1 4 1  \n"
	"1 -1 1 1 -1 1  \n";

int
main(int argc, char *argv[])
{
	char buf[1024];
	size_t sz1, sz2, sz3, sz4;
	ptrdiff_t p1, p2, p3, p4;

	/* Test positional arguments */
	snprintf(buf, sizeof buf,
	    "|xx %1$s %2$s %3$s %4$s\n"
	    "|xx %5$s %6$s %7$s %8$s\n"
	    "|xx %9$s %10$s %11$s %12$s\n"
	    "|xx %13$s %14$s %15$s %16$s\n"
	    "|xx %17$s %18$s %19$s %20$s\n"
	    "|xx %21$s %22$s %23$s %24$s\n"
	    "|xx %25$s %26$s %27$s %28$s\n"
	    "|xx %29$s %30$s %31$s %32$s\n"
	    "|xx %33$s %34$s %35$s %36$s\n"
	    "|xx %37$s %38$s %39$s %40$s\n"
	    "|xx %41$s %42$s %43$s %44$s\n"
	    "|xx %45$d %46$ld %47$lld %48$d %49$lld\n",
	    "01", "02", "03", "04", "05", "06",
	    "07", "08", "09", "10", "11", "12",
	    "13", "14", "15", "16", "17", "18",
	    "19", "20", "21", "22", "23", "24",
	    "25", "26", "27", "28", "29", "30",
	    "31", "32", "33", "34", "35", "36",
	    "37", "38", "39", "40", "41", "42",
	    "43", "44", 45, -1L, 1LL, -1, 1LL
	    );

	if (strcmp(buf, correct) != 0)
		exit(1);
	exit(0);
}
