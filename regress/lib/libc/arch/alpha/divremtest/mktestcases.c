/*	$OpenBSD: mktestcases.c,v 1.2 2001/01/29 02:05:40 niklas Exp $	*/
/*	$NetBSD: mktestcases.c,v 1.1 1995/04/24 05:53:37 cgd Exp $	*/

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

int
main()
{
	int i, j;
	unsigned long n1, n2;

	srandom(time(NULL));

	for (i = 1; /* i < 10240 */ 1; i++) {
		n1 = (unsigned)
		    (random() & ((random() & random()) | 0x80000000));
		n1 <<= 32;
		n1 |= (unsigned)(random() & random() & random());

		n2 = (unsigned)
		    (random() & ((random() & random()) | 0x80000000));
		n2 <<= 32;
		n2 |= (unsigned)(random() & random() & random());

		for (j = 0; j < 64; j++) {
			char *tab[] = { ".i", ".l", "-i", "-l" };

			printf("%s%s%s 0x%lx 0x%lx 0 0\n",
			    tab[(j >> 4) & 0x3],
			    tab[(j >> 2) & 0x3],
			    tab[(j >> 0) & 0x3],
			    n1, n2);
		}
	}

	exit(0);
}
