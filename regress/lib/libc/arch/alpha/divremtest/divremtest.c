/*	$OpenBSD: divremtest.c,v 1.2 2001/01/29 02:05:39 niklas Exp $	*/
/*	$NetBSD: divremtest.c,v 1.1 1995/04/24 05:53:35 cgd Exp $	*/

/*
 * Copyright (c) 1995 Christopher G. Demetriou
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
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
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

void	testfile();
void	usage();

int generate;

int
main(argc, argv)
	int argc;
	char **argv;
{
	int c;

	signal(SIGFPE, SIG_IGN);

	while ((c = getopt(argc, argv, "g")) != -1)
		switch (c) {
		case 'g':
			generate = 1;
			break;

		default:
			usage();
			break;
	}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		testfile();
	else
		for (; argc != 0; argc--, argv++) {
			if (freopen(argv[0], "r", stdin) == NULL) {
				fprintf(stderr,
				    "divremtest: couldn't open %s\n",
				    argv[0]);
				exit(1);
			}

			testfile();
		}

	exit(0);
}

void
testfile()
{
	union operand {
		unsigned long	input;
		int		op_int;
		unsigned int	op_u_int;
		long		op_long;
		unsigned long	op_u_long;
	} op1, op2, divres, modres, divwant, modwant;
	char opspec[6];
	int encoded, i;

	while (scanf("%6c %lx %lx %lx %lx\n", opspec, &op1.input,
	    &op2.input, &divwant.input, &modwant.input) != EOF) {

		encoded = 0;

		for (i = 0; i < 6; i += 2) {
			int posval;

			switch (opspec[i]) {
			case '.':
				posval = 0;
				break;
			case '-':
				posval = 1;
				break;
			default:
				fprintf(stderr,
				    "unknown signedness spec %c\n",
				    opspec[i]);
				exit(1);
			}
			encoded |= posval << ((5 - i) * 4);
		}
			
		for (i = 1; i < 6; i += 2) {
			int posval;

			switch (opspec[i]) {
			case 'i':
				posval = 0;
				break;
			case 'l':
				posval = 1;
				break;
			default:
				fprintf(stderr, "unknown length spec %c\n",
				    opspec[i]);
				exit(1);
			}
			encoded |= posval << ((5 - i) * 4);
		}

		/* KILL ME!!! */
		switch (encoded) {

#define TRY_IT(a, b, c)							\
	divres.a = op1.b / op2.c;					\
	modres.a = op1.b % op2.c;					\
	if (generate) {							\
		printf("%6s 0x%016lx 0x%016lx 0x%016lx 0x%016lx\n",	\
		    opspec, op1.input, op2.input,			\
		    divres.a, modres.a);				\
	} else {							\
		if ((divres.a != divwant.a) ||				\
		    (modres.a != modwant.a)) {				\
			fprintf(stderr, "%6s 0x%016lx 0x%016lx\n",	\
		    	    opspec, op1.input, op2.input);		\
			fprintf(stderr, "FAILED:\n");			\
			fprintf(stderr,					\
			    "div:\twanted 0x%16lx, got 0x%16lx\n",	\
			    divwant.a, divres.a);			\
			fprintf(stderr,					\
			    "mod:\twanted 0x%16lx, got 0x%16lx\n",	\
			    modwant.a, modres.a);			\
									\
			exit(1);					\
		}							\
	}

#include "cases.c"

#undef TRY_IT

		default:
			fprintf(stderr,
			    "INTERNAL ERROR: unknown encoding %x\n", encoded);
			exit(1);
		}
	}
}

void
usage()
{

	fprintf(stderr, "usage: divremtest [-v] [testfile ...]\n");
	exit(1);
}
