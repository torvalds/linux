/*	$OpenBSD: print.h,v 1.7 2003/06/02 21:38:39 maja Exp $ */

/*
 * Copyright (c) 1993-95 Mats O Jansson.  All rights reserved.
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
 *	$OpenBSD: print.h,v 1.7 2003/06/02 21:38:39 maja Exp $
 *
 */

#ifndef _PRINT_H_
#define _PRINT_H_

void	mopPrintHWA(FILE *, u_char *);
void	mopPrintBPTY(FILE *, u_char);
void	mopPrintPGTY(FILE *, u_char);
void	mopPrintOneline(FILE *, u_char *, int);
void	mopPrintHeader(FILE *, u_char *, int);
void	mopPrintMopHeader(FILE *, u_char *, int);
void	mopPrintDevice(FILE *, u_char);
void	mopPrintTime(FILE *, u_char *);
void	mopPrintInfo(FILE *, u_char *, int *, u_short, u_char, int);

#endif /* _PRINT_H_ */
