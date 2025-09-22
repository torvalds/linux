/*	$OpenBSD: mount.h,v 1.8 2014/10/26 03:03:34 guenther Exp $	*/

/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)mount.h	8.1 (Berkeley) 6/6/93
 */

#define MNTPATHLEN 1024
#define MNTNAMLEN 255

#define FHSIZE NFSX_V3FHMAX
typedef char fhandle[NFSX_V3FHMAX];
typedef struct fhstatus {
	u_long		fhs_stat;
	long		fhs_vers;
	long		fhs_auth;
	long		fhs_size;
	fhandle		fhs_fhandle;
} fhstatus;

bool_t xdr_fhandle(XDR *, fhandle *);


bool_t xdr_fhstatus(XDR *, fhstatus *);


typedef char *dirpath;
bool_t xdr_dirpath(XDR *, dirpath *);


typedef char *name;
bool_t xdr_name(XDR *, name *);


typedef struct mountbody *mountlist;
bool_t xdr_mountlist(XDR *, mountlist *);


struct mountbody {
	name ml_hostname;
	dirpath ml_directory;
	mountlist ml_next;
};
typedef struct mountbody mountbody;
bool_t xdr_mountbody(XDR *, mountbody *);


typedef struct groupnode *groups;
bool_t xdr_groups(XDR *, groups *);


struct groupnode {
	name gr_name;
	groups gr_next;
};
typedef struct groupnode groupnode;
bool_t xdr_groupnode(XDR *, groupnode *);


typedef struct exportnode *exports;
bool_t xdr_exports(XDR *, exports *);


struct exportnode {
	dirpath ex_dir;
	groups ex_groups;
	exports ex_next;
};
typedef struct exportnode exportnode;
bool_t xdr_exportnode(XDR *, exportnode *);


#define MOUNTPROG ((u_long)100005)
#define MOUNTVERS ((u_long)1)
#define MOUNTPROC_NULL ((u_long)0)
extern void *mountproc_null_1(void *, CLIENT *);
#define MOUNTPROC_MNT ((u_long)1)
extern fhstatus *mountproc_mnt_1(void *, CLIENT *);
#define MOUNTPROC_DUMP ((u_long)2)
extern mountlist *mountproc_dump_1(void *, CLIENT *);
#define MOUNTPROC_UMNT ((u_long)3)
extern void *mountproc_umnt_1(void *, CLIENT *);
#define MOUNTPROC_UMNTALL ((u_long)4)
extern void *mountproc_umntall_1(void *, CLIENT *);
#define MOUNTPROC_EXPORT ((u_long)5)
extern exports *mountproc_export_1(void *, CLIENT *);
#define MOUNTPROC_EXPORTALL ((u_long)6)
extern exports *mountproc_exportall_1(void *, CLIENT *);
