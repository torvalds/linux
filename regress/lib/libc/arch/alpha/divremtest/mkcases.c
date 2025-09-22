/*	$OpenBSD: mkcases.c,v 1.2 2001/01/29 02:05:39 niklas Exp $	*/
/*	$NetBSD: mkcases.c,v 1.1 1995/04/24 05:53:36 cgd Exp $	*/

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

char *tab[4] = { "u_int", "int", "u_long", "long" };

int
main()
{
	int i;

	for (i = 0; i < 64; i++) {
		printf(
"		case 0x%d%d%d%d%d%d:	/* %s <= %s op %s */\n",
			(i >> 5) & 1,
			(i >> 4) & 1,
			(i >> 3) & 1,
			(i >> 2) & 1,
			(i >> 1) & 1,
			(i >> 0) & 1,
			tab[(i >> 4) & 0x3],
			tab[(i >> 2) & 0x3],
			tab[(i >> 0) & 0x3]);
		printf(
"			TRY_IT(op_%s, op_%s, op_%s);\n",
			tab[(i >> 4) & 0x3],
			tab[(i >> 2) & 0x3],
			tab[(i >> 0) & 0x3]);
		printf(
"			break;\n\n");
	}

	exit(0);
}
