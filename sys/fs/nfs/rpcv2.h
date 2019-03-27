/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 * $FreeBSD$
 */

#ifndef _NFS_RPCV2_H_
#define	_NFS_RPCV2_H_

/*
 * Definitions for Sun RPC Version 2, from
 * "RPC: Remote Procedure Call Protocol Specification" RFC1057
 */

/* Version # */
#define	RPC_VER2		2

/* Authentication flavours */
#define	RPCAUTH_NULL			0
#define	RPCAUTH_UNIX			1
#define	RPCAUTH_SHORT			2
#define	RPCAUTH_KERB4			4
#define	RPCAUTH_GSS			6
#define	RPCAUTH_GSSKRB5			390003
#define	RPCAUTH_GSSKRB5INTEGRITY	390004
#define	RPCAUTH_GSSKRB5PRIVACY		390005

#define	RPCAUTH_MAXSIZ		400
#define	RPCVERF_MAXSIZ	12	/* For Kerb, can actually be 400 */

/*
 * RPCAUTH_UNIX defs.
 */
#define	RPCAUTHUNIX_MINSIZ	(5 * NFSX_UNSIGNED)
#define	RPCAUTH_UNIXGIDS 16

/*
 * RPCAUTH_GSS defs.
 */
#define	RPCAUTHGSS_VERS1	1

#define	RPCAUTHGSS_DATA		0
#define	RPCAUTHGSS_INIT		1
#define	RPCAUTHGSS_CONTINIT	2
#define	RPCAUTHGSS_DESTROY	3

#define	RPCAUTHGSS_SVCNONE	1
#define	RPCAUTHGSS_SVCINTEGRITY	2
#define	RPCAUTHGSS_SVCPRIVACY	3

#define	RPCAUTHGSS_MAXSEQ	0x80000000

#define	RPCAUTHGSS_WINDOW	64	/* # of bits in u_int64_t */
#define	RPCAUTHGSS_SEQWINDOW	(RPCAUTHGSS_WINDOW + 1)

#define	RPCAUTHGSS_MIC		1
#define	RPCAUTHGSS_WRAP		2

/*
 * Qop values for the types of security services.
 */
#define	GSS_KERBV_QOP		0

/*
 * Sizes of GSS stuff.
 */
#define	RPCGSS_KEYSIZ		8

#define	GSSX_AUTHHEAD	(5 * NFSX_UNSIGNED)
#define	GSSX_MYHANDLE	(sizeof (long) + sizeof (u_int64_t))
#define	GSSX_RPCHEADER	(13 * NFSX_UNSIGNED + GSSX_MYHANDLE)
#define	GSSX_MINWRAP	(2 * NFSX_UNSIGNED)
#define	GSSX_KERBVTOKEN	24
#define	GSSX_LOCALHANDLE (sizeof (void *))

/*
 * Stuff for the gssd.
 */
#define	RPCPROG_GSSD		0x20101010
#define	RPCGSSD_VERS		1
#define	RPCGSSD_INIT		1
#define	RPCGSSD_CONTINIT	2
#define	RPCGSSD_CONTINITDESTROY	3
#define	RPCGSSD_CLINIT		4
#define	RPCGSSD_CLINITUID	5
#define	RPCGSSD_CLCONT		6
#define	RPCGSSD_CLCONTUID	7
#define	RPCGSSD_CLINITNAME	8
#define	RPCGSSD_CLCONTNAME	9

/*
 * Stuff for the nfsuserd
 */
#define	RPCPROG_NFSUSERD	0x21010101
#define	RPCNFSUSERD_VERS	1
#define	RPCNFSUSERD_GETUID	1
#define	RPCNFSUSERD_GETGID	2
#define	RPCNFSUSERD_GETUSER	3
#define	RPCNFSUSERD_GETGROUP	4

/*
 * Some major status codes.
 */
#if !defined(_GSSAPI_H_) && !defined(GSSAPI_H_) && !defined(_GSSAPI_GSSAPI_H_) && !defined(_RPCSEC_GSS_H)
#define	 GSS_S_COMPLETE                  0x00000000
#define	 GSS_S_CONTINUE_NEEDED           0x00000001
#define	 GSS_S_DUPLICATE_TOKEN           0x00000002
#define	 GSS_S_OLD_TOKEN                 0x00000004
#define	 GSS_S_UNSEQ_TOKEN               0x00000008
#define	 GSS_S_GAP_TOKEN                 0x00000010
#define	 GSS_S_BAD_MECH                  0x00010000
#define	 GSS_S_BAD_NAME                  0x00020000
#define	 GSS_S_BAD_NAMETYPE              0x00030000
#define	 GSS_S_BAD_BINDINGS              0x00040000
#define	 GSS_S_BAD_STATUS                0x00050000
#define	 GSS_S_BAD_MIC                   0x00060000
#define	 GSS_S_BAD_SIG                   0x00060000
#define	 GSS_S_NO_CRED                   0x00070000
#define	 GSS_S_NO_CONTEXT                0x00080000
#define	 GSS_S_DEFECTIVE_TOKEN           0x00090000
#define	 GSS_S_DEFECTIVE_CREDENTIAL      0x000a0000
#define	 GSS_S_CREDENTIALS_EXPIRED       0x000b0000
#define	 GSS_S_CONTEXT_EXPIRED           0x000c0000
#define	 GSS_S_FAILURE                   0x000d0000
#define	 GSS_S_BAD_QOP                   0x000e0000
#define	 GSS_S_UNAUTHORIZED              0x000f0000
#define	 GSS_S_UNAVAILABLE               0x00100000
#define	 GSS_S_DUPLICATE_ELEMENT         0x00110000
#define	 GSS_S_NAME_NOT_MN               0x00120000
#define	 GSS_S_CALL_INACCESSIBLE_READ    0x01000000
#define	 GSS_S_CALL_INACCESSIBLE_WRITE   0x02000000
#define	 GSS_S_CALL_BAD_STRUCTURE        0x03000000
#endif	/* _GSSAPI_H_ */

/* Rpc Constants */
#define	RPC_CALL	0
#define	RPC_REPLY	1
#define	RPC_MSGACCEPTED	0
#define	RPC_MSGDENIED	1
#define	RPC_PROGUNAVAIL	1
#define	RPC_PROGMISMATCH	2
#define	RPC_PROCUNAVAIL	3
#define	RPC_GARBAGE	4		/* I like this one */
#define	RPC_MISMATCH	0
#define	RPC_AUTHERR	1

/* Authentication failures */
#define	AUTH_BADCRED	1
#define	AUTH_REJECTCRED	2
#define	AUTH_BADVERF	3
#define	AUTH_REJECTVERF	4
#define	AUTH_TOOWEAK	5		/* Give em wheaties */
#define	AUTH_PROBCRED	13
#define	AUTH_CTXCRED	14

/* Sizes of rpc header parts */
#define	RPC_SIZ		24
#define	RPC_REPLYSIZ	28

/* RPC Prog definitions */
#define	RPCPROG_MNT	100005
#define	RPCMNT_VER1	1
#define	RPCMNT_VER3	3
#define	RPCMNT_MOUNT	1
#define	RPCMNT_DUMP	2
#define	RPCMNT_UMOUNT	3
#define	RPCMNT_UMNTALL	4
#define	RPCMNT_EXPORT	5
#define	RPCMNT_NAMELEN	255
#define	RPCMNT_PATHLEN	1024
#define	RPCPROG_NFS	100003
 
/* Structs for common parts of the rpc's */
struct rpcv2_time {
	u_int32_t rpc_sec;
	u_int32_t rpc_usec;
};

#endif	/* _NFS_RPCV2_H_ */
