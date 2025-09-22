/*      $OpenBSD: profile.h,v 1.5 2019/04/19 09:19:22 visa Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)profile.h	8.1 (Berkeley) 6/10/93
 */
#ifndef	_MIPS64_PROFILE_H_
#define	_MIPS64_PROFILE_H_

#define	_MCOUNT_DECL static void ___mcount

/*XXX The cprestore instruction is a "dummy" to shut up as(1). */

#define	MCOUNT \
	__asm(".globl _mcount;"		\
	".type _mcount,@function;"	\
	"_mcount:;"			\
	".set noreorder;"		\
	".set noat;"			\
	".cpload $25;"			\
	".cprestore 4;"			\
	"sd $4,16($29);"		\
	"sd $5,24($29);"		\
	"sd $6,32($29);"		\
	"sd $7,40($29);"		\
	"sd $1,0($29);"			\
	"sd $31,8($29);"		\
	"move $5,$31;"			\
	".local ___mcount;"		\
	"jal ___mcount;"		\
	"move $4,$1;"			\
	"ld $4,16($29);"		\
	"ld $5,24($29);"		\
	"ld $6,32($29);"		\
	"ld $7,40($29);"		\
	"ld $31,8($29);"		\
	"ld $1,0($29);"			\
	"daddu $29,$29,16;"		\
	"j $31;"			\
	"move $31,$1;"			\
	".set reorder;"			\
	".set at");

#ifdef _KERNEL
/*
 * The following two macros do splhigh and splx respectively.
 * They have to be defined this way because these are real
 * functions on the MIPS, and we do not want to invoke mcount
 * recursively.
 */
#define	MCOUNT_ENTER	s = _splhigh()

#define	MCOUNT_EXIT	_splx(s)
#endif /* _KERNEL */

#endif /* !_MIPS64_PROFILE_H_ */
