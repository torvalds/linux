/*	$OpenBSD: file.h,v 1.12 2014/12/13 14:44:59 miod Exp $ */

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
 *	$OpenBSD: file.h,v 1.12 2014/12/13 14:44:59 miod Exp $
 *
 */

#ifndef _FILE_H_
#define _FILE_H_

#define INFO_PRINT 1

const char	*FileTypeName(mopd_imagetype);

void		mopFilePutLX(u_char *, int, u_int32_t, int);
void		mopFilePutBX(u_char *, int, u_int32_t, int);
u_int32_t	mopFileGetLX(u_char *, int, int);
u_int32_t	mopFileGetBX(u_char *, int, int);
u_int64_t	mopFileGetLXX(u_char *, int, int);
u_int64_t	mopFileGetBXX(u_char *, int, int);
ssize_t		mopFileRead(struct dllist *, u_char *);
void		mopFileSwapX(u_char *, int, int);

int		CheckMopFile(int);
int		GetMopFileInfo(struct dllist *, int);

int		CheckElfFile(int);
int		GetElf32FileInfo(struct dllist *, int);
int		GetElf64FileInfo(struct dllist *, int);

int		CheckAOutFile(int);
int		GetAOutFileInfo(struct dllist *, int);

int		GetFileInfo(struct dllist *, int);

#endif /* _FILE_H_ */
