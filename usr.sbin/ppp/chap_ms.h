/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997        Gabor Kincses <gabor@acm.org>
 *               1997 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Eric Rosenquist
 *                           Strata Software Limited.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* Max # of (Unicode) chars in an NT password */
#define MAX_NT_PASSWORD	256

/* Don't rely on sizeof(MS_ChapResponse) in case of struct padding */
#define MS_CHAP_RESPONSE_LEN    49
#define CHAP81_RESPONSE_LEN     49
#define CHAP81_NTRESPONSE_LEN   24
#define CHAP81_NTRESPONSE_OFF   24
#define CHAP81_HASH_LEN         16
#define CHAP81_AUTHRESPONSE_LEN	42
#define CHAP81_CHALLENGE_LEN    16

extern void mschap_NT(char *, char *);
extern void mschap_LANMan(char *, char *, char *);
extern void GenerateNTResponse(char *, char *, char *, char *, int , char *);
extern void HashNtPasswordHash(char *, char *);
extern void NtPasswordHash(char *, int, char *);
extern void GenerateAuthenticatorResponse(char *, int, char *, char *, char *, char *, char *);
extern void GetAsymetricStartKey(char *, char *, int, int, int);
extern void GetMasterKey(char *, char *, char *);
extern void GetNewKeyFromSHA(char *, char *, long, char *);
