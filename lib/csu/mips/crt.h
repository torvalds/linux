/*-
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Copyright 2018 Andrew Turner
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 *
 * $FreeBSD$
 */

#ifndef _CRT_H_
#define	_CRT_H_

#define	HAVE_CTORS
#define	CTORS_CONSTRUCTORS
#ifdef __mips_o32
#define	INIT_CALL_SEQ(func)						\
    ".set noreorder		\n"					\
    "bal	1f		\n"					\
    "nop			\n"					\
    "1:				\n"					\
    ".cpload $ra		\n"					\
    ".set reorder		\n"					\
    ".local	" __STRING(func) "\n"					\
    "jal	" __STRING(func)
#else
#define	INIT_CALL_SEQ(func)						\
    ".set noreorder		\n"					\
    "bal	1f		\n"					\
    "nop			\n"					\
    "1:				\n"					\
    ".set reorder		\n"					\
    ".cpsetup $ra, $v0, 1b	\n"					\
    ".local	" __STRING(func) "\n"					\
    "jal	" __STRING(func)
#endif

#endif
