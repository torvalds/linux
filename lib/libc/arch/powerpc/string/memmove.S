/* $OpenBSD: memmove.S,v 1.5 2022/06/10 01:56:02 guenther Exp $ */
/* $NetBSD: memmove.S,v 1.3 2011/01/15 07:31:12 matt Exp $ */

/* stropt/memmove.S, pl_string_common, pl_linux 10/11/04 11:45:37
 * ==========================================================================
 * Optimized memmove implementation for IBM PowerPC 405/440.
 *
 *	Copyright (c) 2003, IBM Corporation
 *	All rights reserved.
 *
 *	Redistribution and use in source and binary forms, with or
 *	without modification, are permitted provided that the following
 *	conditions are met:
 *
 *	* Redistributions of source code must retain the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer.
 *	* Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials
 *	provided with the distribution.
 *	* Neither the name of IBM nor the names of its contributors
 *	may be used to endorse or promote products derived from this
 *	software without specific prior written permission.
 *
 *	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *	CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *	MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 *	BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 *	OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *	PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 *	OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *	USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ==========================================================================
 */

#include "DEFS.h"

        .text

/* void *memcpy(void *to, const void *from, size_t len) */
#if 0
ENTRY(memcpy)
	RETGUARD_SETUP(memmove, %r11, %r12)
	mr	%r8, %r3		/* Save dst (return value)	*/
	b	.Lfwd
#endif

/* void bcopy(void *, void *, size_t) */
ENTRY_NB(bcopy)
	mr	%r6, %r3		/* swap src/dst */
	mr	%r3, %r4
	mr	%r4, %r6

/* void *memmove(void *, const void *, size_t) */
ENTRY(memmove)
	RETGUARD_SETUP(memmove, %r11, %r12)
	mr	%r8, %r3		/* Save dst (return value)	*/

	cmpw	%r4, %r8		/* Branch to reverse if 	*/
	blt	.Lreverse		/* src < dest. Don't want to	*/
					/* overwrite end of src with	*/
					/* start of dest 		*/

.Lfwd:
	addi	%r4, %r4, -4		/* Back up src and dst pointers */
	addi	%r8, %r8, -4		/* due to auto-update of 'load' */

	srwi.	%r9,%r5,2		/* How many words in total cnt	*/
	beq-	.Llast1			/* Handle byte by byte if < 4	*/
					/* bytes total 			*/
	mtctr	%r9			/* Count of words for loop	*/
	lwzu	%r7, 4(%r4)		/* Preload first word		*/

	b	.Lg1

.Lg0:					/* Main loop			*/

	lwzu	%r7, 4(%r4)		/* Load a new word		*/
	stwu	%r6, 4(%r8)		/* Store previous word		*/

.Lg1:

	bdz-	.Llast			/* Dec cnt, and branch if just	*/
					/* one word to store		*/
	lwzu	%r6, 4(%r4)		/* Load another word		*/
	stwu	%r7, 4(%r8)		/* Store previous word		*/
	bdnz+	.Lg0			/* Dec cnt, and loop again if	*/
					/* more words			*/
	mr	%r7, %r6		/* If word count -> 0, then...	*/

.Llast:

	stwu	%r7, 4(%r8)		/* ... store last word		*/

.Llast1:				/* Byte-by-byte copy		*/

	clrlwi.	%r5,%r5,30		/* If count -> 0, then ...	*/
	beq	.Ldone			/* we're done			*/

	mtctr	%r5			/* else load count for loop	*/

	lbzu	%r6, 4(%r4)		/* 1st byte: update addr by 4	*/
	stbu	%r6, 4(%r8)		/* since we pre-adjusted by 4	*/
	bdz-	.Ldone			/* in anticipation of main loop */

.Llast2:

	lbzu	%r6, 1(%r4)		/* But handle the rest by	*/
	stbu	%r6, 1(%r8)		/* updating addr by 1		*/
	bdnz+	.Llast2
	b	.Ldone

	/* We're here since src < dest. Don't want to overwrite end of	*/
	/* src with start of dest					*/

.Lreverse:

	add	%r4, %r4, %r5		/* Work from end to beginning	*/
	add	%r8, %r8, %r5 		/* so add count to string ptrs	*/
	srwi.	%r9,%r5,2		/* Words in total count		*/
	beq-	.Lrlast1		/* Handle byte by byte if < 4	*/
					/* bytes total 			*/

	mtctr	%r9			/* Count of words for loop 	*/

	lwzu	%r7, -4(%r4)		/* Preload first word		*/
	b	.Lrg1

.Lrg0:					/* Main loop			*/

	lwzu	%r7, -4(%r4)		/* Load a new word		*/
	stwu	%r6, -4(%r8)		/* Store previous word		*/

.Lrg1:

	bdz-	.Lrlast			/* Dec cnt, and branch if just	*/
					/* one word to store		*/

	lwzu	%r6, -4(%r4)		/* Load another word		*/
	stwu	%r7, -4(%r8)		/* Store previous word		*/

	bdnz+	.Lrg0			/* Dec cnt, and loop again if	*/
					/* more words			*/

	mr	%r7, %r6		/* If word count -> 0, then...	*/

.Lrlast:

	stwu	%r7, -4(%r8)		/* ... store last word		*/

.Lrlast1:				/* Byte-by-byte copy		*/

	clrlwi.	%r5,%r5,30		/* If count -> 0, then...	*/
	beq	.Ldone			/* ... we're done 		*/

	mtctr	%r5			/* else load count for loop 	*/

.Lrlast2:

	lbzu	%r6, -1(%r4)		/* Handle the rest, byte by 	*/
	stbu	%r6, -1(%r8)		/* byte				*/

	bdnz+	.Lrlast2	 	/* Dec ctr, and branch if more	*/
					/* bytes left			*/
.Ldone:
	RETGUARD_CHECK(memmove, %r11, %r12)
	blr
END_STRONG(memmove)
END_WEAK(bcopy)
