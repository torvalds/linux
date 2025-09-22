/*	$OpenBSD: ntfs.h,v 1.19 2022/01/11 03:13:59 jsg Exp $	*/
/*	$NetBSD: ntfs.h,v 1.5 2003/04/24 07:50:19 christos Exp $	*/

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
 *	Id: ntfs.h,v 1.5 1999/05/12 09:42:51 semenu Exp
 */

/*#define NTFS_DEBUG 1*/
typedef u_int64_t cn_t;
typedef u_int16_t wchar;

#define BBSIZE			1024
#define	BBOFF			((off_t)(0))
#define	BBLOCK			0
#define	NTFS_MFTINO		0
#define	NTFS_VOLUMEINO		3
#define	NTFS_ATTRDEFINO		4
#define	NTFS_ROOTINO		5
#define	NTFS_BITMAPINO		6
#define	NTFS_BOOTINO		7
#define	NTFS_BADCLUSINO		8
#define	NTFS_UPCASEINO		10
#define NTFS_MAXFILENAME	255

/*
 * UFS directories use 32bit inode numbers internally, regardless
 * of what the system on top of it uses.
 */
typedef u_int32_t	ntfsino_t;

struct fixuphdr {
	u_int32_t       fh_magic;
	u_int16_t       fh_foff;
	u_int16_t       fh_fnum;
} __packed;

#define NTFS_AF_INRUN	0x00000001
struct attrhdr {
	u_int32_t       a_type;
	u_int32_t       reclen;
	u_int8_t        a_flag;
	u_int8_t        a_namelen;
	u_int8_t        a_nameoff;
	u_int8_t        reserved1;
	u_int8_t        a_compression;
	u_int8_t        reserved2;
	u_int16_t       a_index;
} __packed;
#define NTFS_A_STD	0x10
#define NTFS_A_ATTRLIST	0x20
#define NTFS_A_NAME	0x30
#define NTFS_A_VOLUMENAME	0x60
#define NTFS_A_DATA	0x80
#define	NTFS_A_INDXROOT	0x90
#define	NTFS_A_INDX	0xA0
#define NTFS_A_INDXBITMAP 0xB0

#define NTFS_MAXATTRNAME	255
struct attr {
	struct attrhdr  a_hdr;
	union {
		struct {
			u_int16_t       a_datalen;
			u_int16_t       reserved1;
			u_int16_t       a_dataoff;
			u_int16_t       a_indexed;
		} __packed	a_S_r;
		struct {
			cn_t            a_vcnstart;
			cn_t            a_vcnend;
			u_int16_t       a_dataoff;
			u_int16_t       a_compressalg;
			u_int32_t       reserved1;
			u_int64_t       a_allocated;
			u_int64_t       a_datalen;
			u_int64_t       a_initialized;
		} __packed	a_S_nr;
	}               a_S;
} __packed;
#define a_r	a_S.a_S_r
#define a_nr	a_S.a_S_nr

typedef struct {
	u_int64_t       t_create;
	u_int64_t       t_write;
	u_int64_t       t_mftwrite;
	u_int64_t       t_access;
} __packed ntfs_times_t;

#define NTFS_FFLAG_RDONLY	0x01LL
#define NTFS_FFLAG_HIDDEN	0x02LL
#define NTFS_FFLAG_SYSTEM	0x04LL
#define NTFS_FFLAG_ARCHIVE	0x20LL
#define NTFS_FFLAG_COMPRESSED	0x0800LL
#define NTFS_FFLAG_DIR		0x10000000LL

struct attr_name {
	u_int32_t       n_pnumber;	/* Parent ntnode */
	u_int32_t       reserved;
	ntfs_times_t    n_times;
	u_int64_t       n_size;
	u_int64_t       n_attrsz;
	u_int64_t       n_flag;
	u_int8_t        n_namelen;
	u_int8_t        n_nametype;
	u_int16_t       n_name[1];
} __packed;

#define NTFS_IRFLAG_INDXALLOC	0x00000001
struct attr_indexroot {
	u_int32_t       ir_unkn1;	/* always 0x30 */
	u_int32_t       ir_unkn2;	/* always 0x1 */
	u_int32_t       ir_size;/* ??? */
	u_int32_t       ir_unkn3;	/* number of cluster */
	u_int32_t       ir_unkn4;	/* always 0x10 */
	u_int32_t       ir_datalen;	/* sizeof something */
	u_int32_t       ir_allocated;	/* same as above */
	u_int16_t       ir_flag;/* ?? always 1 */
	u_int16_t       ir_unkn7;
} __packed;

struct attr_attrlist {
	u_int32_t       al_type;	/* Attribute type */
	u_int16_t       reclen;		/* length of this entry */
	u_int8_t        al_namelen;	/* Attribute name len */
	u_int8_t        al_nameoff;	/* Name offset from entry start */
	u_int64_t       al_vcnstart;	/* VCN number */
	u_int32_t       al_inumber;	/* Parent ntnode */
	u_int32_t       reserved;
	u_int16_t       al_index;	/* Attribute index in MFT record */
	u_int16_t       al_name[1];	/* Name */
} __packed;

#define	NTFS_INDXMAGIC	(u_int32_t)(0x58444E49)
struct attr_indexalloc {
	struct fixuphdr ia_fixup;
	u_int64_t       unknown1;
	cn_t            ia_bufcn;
	u_int16_t       ia_hdrsize;
	u_int16_t       unknown2;
	u_int32_t       ia_inuse;
	u_int32_t       ia_allocated;
} __packed;

#define	NTFS_IEFLAG_SUBNODE	0x00000001
#define	NTFS_IEFLAG_LAST	0x00000002

struct attr_indexentry {
	u_int32_t       ie_number;
	u_int32_t       unknown1;
	u_int16_t       reclen;
	u_int16_t       ie_size;
	u_int32_t       ie_flag;/* 1 - has subnodes, 2 - last */
	u_int32_t       ie_fpnumber;
	u_int32_t       unknown2;
	ntfs_times_t    ie_ftimes;
	u_int64_t       ie_fallocated;
	u_int64_t       ie_fsize;
	u_int64_t       ie_fflag;
	u_int8_t        ie_fnamelen;
	u_int8_t        ie_fnametype;
	wchar           ie_fname[NTFS_MAXFILENAME];
	/* cn_t		ie_bufcn;	 buffer with subnodes */
} __packed;

#define	NTFS_FILEMAGIC	(u_int32_t)(0x454C4946)
#define	NTFS_FRFLAG_DIR	0x0002
struct filerec {
	struct fixuphdr fr_fixup;
	u_int8_t        reserved[8];
	u_int16_t       fr_seqnum;	/* Sequence number */
	u_int16_t       fr_nlink;
	u_int16_t       fr_attroff;	/* offset to attributes */
	u_int16_t       fr_flags;	/* 1-nonresident attr, 2-directory */
	u_int32_t       fr_size;/* hdr + attributes */
	u_int32_t       fr_allocated;	/* allocated length of record */
	u_int64_t       fr_mainrec;	/* main record */
	u_int16_t       fr_attrnum;	/* maximum attr number + 1 ??? */
} __packed;

#define	NTFS_ATTRNAME_MAXLEN	0x40
#define	NTFS_ADFLAG_NONRES	0x0080	/* Attrib can be non resident */
#define	NTFS_ADFLAG_INDEX	0x0002	/* Attrib can be indexed */
struct attrdef {
	wchar		ad_name[NTFS_ATTRNAME_MAXLEN];
	u_int32_t	ad_type;
	u_int32_t	reserved1[2];
	u_int32_t	ad_flag;
	u_int64_t	ad_minlen;
	u_int64_t	ad_maxlen;	/* -1 for nonlimited */
} __packed;

struct ntvattrdef {
	char		ad_name[0x40];
	int		ad_namelen;
	u_int32_t	ad_type;
} __packed;

#define	NTFS_BBID	"NTFS    "
#define	NTFS_BBIDLEN	8
struct bootfile {
	u_int8_t        reserved1[3];	/* asm jmp near ... */
	u_int8_t        bf_sysid[8];	/* 'NTFS    ' */
	u_int16_t       bf_bps;		/* bytes per sector */
	u_int8_t        bf_spc;		/* sectors per cluster */
	u_int8_t        reserved2[7];	/* unused (zeroed) */
	u_int8_t        bf_media;	/* media desc. (0xF8) */
	u_int8_t        reserved3[2];
	u_int16_t       bf_spt;		/* sectors per track */
	u_int16_t       bf_heads;	/* number of heads */
	u_int8_t        reserver4[12];
	u_int64_t       bf_spv;		/* sectors per volume */
	cn_t            bf_mftcn;	/* $MFT cluster number */
	cn_t            bf_mftmirrcn;	/* $MFTMirr cn */
	u_int8_t        bf_mftrecsz;	/* MFT record size (clust) */
					/* 0xF6 indicates 1/4 */
	u_int32_t       bf_ibsz;	/* index buffer size */
	u_int32_t       bf_volsn;	/* volume ser. num. */
} __packed;

typedef wchar (ntfs_wget_func_t)(const char **);
typedef int (ntfs_wput_func_t)(char *, size_t, wchar);
typedef int (ntfs_wcmp_func_t)(wchar, wchar);

/*
 * Maximum number of ntnodes to keep in memory. We do not want to leave
 * large data structures hanging off vnodes indefinitely and the data
 * needed to reload the ntnode should already be in the buffer cache.
 */
#define LOADED_NTNODE_HI 16
struct ntnode;
TAILQ_HEAD(ntnodeq, ntnode);

#define	NTFS_SYSNODESNUM	0x0B
struct ntfsmount {
	struct mount   *ntm_mountp;	/* filesystem vfs structure */
	struct bootfile ntm_bootfile;
	dev_t           ntm_dev;	/* device mounted */
	struct vnode   *ntm_devvp;	/* block device mounted vnode */
	struct vnode   *ntm_sysvn[NTFS_SYSNODESNUM];
	u_int32_t       ntm_bpmftrec;
	uid_t           ntm_uid;
	gid_t           ntm_gid;
	mode_t          ntm_mode;
	u_long          ntm_flag;
	cn_t		ntm_cfree;
	struct ntvattrdef *ntm_ad;
	int		ntm_adnum;
	struct netexport ntm_export;	/* export information */
	ntfs_wget_func_t *ntm_wget;	/* decode string to Unicode string */
	ntfs_wput_func_t *ntm_wput;	/* encode Unicode string to string */
	ntfs_wcmp_func_t *ntm_wcmp;	/* compare to wide characters */
	int		ntm_ntnodes;	/* Number of loaded ntnodes. */
	struct ntnodeq	ntm_ntnodeq;	/* Queue of ntnodes (LRU). */
};

#define ntm_mftcn	ntm_bootfile.bf_mftcn
#define ntm_mftmirrcn	ntm_bootfile.bf_mftmirrcn
#define	ntm_mftrecsz	ntm_bootfile.bf_mftrecsz
#define	ntm_spc		ntm_bootfile.bf_spc
#define	ntm_bps		ntm_bootfile.bf_bps

#define	NTFS_NEXTREC(s, type) ((type)(((caddr_t) s) + (s)->reclen))

/* Convert mount ptr to ntfsmount ptr. */
#define VFSTONTFS(mp)	((struct ntfsmount *)((mp)->mnt_data))
#define VTONT(v)	FTONT(VTOF(v))
#define	VTOF(v)		((struct fnode *)((v)->v_data))
#define	FTOV(f)		((f)->f_vp)
#define	FTONT(f)	((f)->f_ip)
#define ntfs_cntobn(cn)	(daddr_t)((cn) * (ntmp->ntm_spc))
#define ntfs_cntob(cn)	(off_t)((cn) * (ntmp)->ntm_spc * (ntmp)->ntm_bps)
#define ntfs_btocn(off)	(cn_t)((off) / ((ntmp)->ntm_spc * (ntmp)->ntm_bps))
#define ntfs_btocl(off)	(cn_t)((off + ntfs_cntob(1) - 1) / ((ntmp)->ntm_spc * (ntmp)->ntm_bps))
#define ntfs_btocnoff(off)	(off_t)((off) % ((ntmp)->ntm_spc * (ntmp)->ntm_bps))
#define ntfs_bntob(bn)	(int32_t)((bn) * (ntmp)->ntm_bps)

#ifdef _KERNEL
#if defined(NTFS_DEBUG)
extern int ntfs_debug;
#define DNPRINTF(n, x...) do { if(ntfs_debug >= (n)) printf(x); } while(0)
#define DPRINTF(x...) DNPRINTF(1, x)
#define DDPRINTF(x...) DNPRINTF(2, x)
#else /* NTFS_DEBUG */
#define DNPRINTF(n, x...)
#define DPRINTF(x...)
#define DDPRINTF(x...)
#endif

extern const struct vops ntfs_vops;
#endif
