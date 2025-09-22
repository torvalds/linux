/*	$OpenBSD: ntfs_subr.h,v 1.12 2024/05/13 01:15:53 jsg Exp $	*/
/*	$NetBSD: ntfs_subr.h,v 1.1 2002/12/23 17:38:33 jdolecek Exp $	*/

/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko
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
 *	Id: ntfs_subr.h,v 1.4 1999/05/12 09:43:02 semenu Exp
 */

#define	VA_LOADED		0x0001
#define	VA_PRELOADED		0x0002

struct ntvattr {
	LIST_ENTRY(ntvattr) 	va_list;

	u_int32_t		va_vflag;
	struct vnode	       *va_vp;
	struct ntnode 	       *va_ip;

	u_int32_t		va_flag;
	u_int32_t		va_type;
	u_int8_t		va_namelen;
	char			va_name[NTFS_MAXATTRNAME];

	u_int32_t		va_compression;
	u_int32_t		va_compressalg;
	u_int64_t		va_datalen;
	u_int64_t		va_allocated;
	cn_t	 		va_vcnstart;
	cn_t	 		va_vcnend;
	u_int16_t		va_index;
	union {
		struct {
			cn_t *		cn;
			cn_t *		cl;
			u_long		cnt;
		} vrun;
		caddr_t		datap;
		struct attr_name *name;
		struct attr_indexroot *iroot;
		struct attr_indexalloc *ialloc;
	} va_d;
};
#define	va_vruncn	va_d.vrun.cn
#define va_vruncl	va_d.vrun.cl
#define va_vruncnt	va_d.vrun.cnt
#define va_datap	va_d.datap
#define va_a_name	va_d.name
#define va_a_iroot	va_d.iroot
#define va_a_ialloc	va_d.ialloc

int ntfs_procfixups( struct ntfsmount *, u_int32_t, caddr_t, size_t );
int ntfs_parserun( cn_t *, cn_t *, u_int8_t *, u_long, u_long *);
int ntfs_runtocn( cn_t *, struct ntfsmount *, u_int8_t *, u_long, cn_t);
int ntfs_readntvattr_plain( struct ntfsmount *, struct ntnode *, struct ntvattr *, off_t, size_t, void *,size_t *, struct uio *);
int ntfs_readattr_plain( struct ntfsmount *, struct ntnode *, u_int32_t, char *, off_t, size_t, void *,size_t *, struct uio *);
int ntfs_readattr( struct ntfsmount *, struct ntnode *, u_int32_t, char *, off_t, size_t, void *, struct uio *);
int ntfs_filesize( struct ntfsmount *, struct fnode *, u_int64_t *, u_int64_t *);
struct timespec	ntfs_nttimetounix( u_int64_t );
int ntfs_runtovrun( cn_t **, cn_t **, u_long *, u_int8_t *);
int ntfs_attrtontvattr( struct ntfsmount *, struct ntvattr **, struct attr * );
void ntfs_freentvattr( struct ntvattr * );
int ntfs_isnamepermitted(struct ntfsmount *, struct attr_indexentry * );
int ntfs_ntvattrrele(struct ntvattr * );
int ntfs_ntvattrget(struct ntfsmount *, struct ntnode *, u_int32_t, const char *, cn_t , struct ntvattr **);
void ntfs_ntref(struct ntnode *);
void ntfs_ntrele(struct ntnode *);
int ntfs_loadntnode( struct ntfsmount *, struct ntnode * );
int ntfs_fget(struct ntfsmount *, struct ntnode *, int, char *, struct fnode **);
void ntfs_frele(struct fnode *);
int ntfs_ntreaddir(struct ntfsmount *, struct fnode *, u_int32_t, struct attr_indexentry **, struct proc *);
int ntfs_ntlookupfile(struct ntfsmount *, struct vnode *, struct componentname *, struct vnode **);
int ntfs_ntlookup(struct ntfsmount *, ntfsino_t, struct ntnode **);
int ntfs_ntget(struct ntnode *);
void ntfs_ntput(struct ntnode *);
int ntfs_toupper_use(struct mount *, struct ntfsmount *, struct proc *);
void ntfs_toupper_unuse(struct proc *);

/* ntfs_conv.c stuff */
ntfs_wget_func_t ntfs_utf8_wget;
ntfs_wput_func_t ntfs_utf8_wput;
ntfs_wcmp_func_t ntfs_utf8_wcmp;
