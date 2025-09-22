/*	$OpenBSD: malloc.h,v 1.127 2025/02/05 18:29:17 mvs Exp $	*/
/*	$NetBSD: malloc.h,v 1.39 1998/07/12 19:52:01 augustss Exp $	*/

/*
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)malloc.h	8.5 (Berkeley) 5/3/95
 */

#ifndef _SYS_MALLOC_H_
#define	_SYS_MALLOC_H_

#include <sys/queue.h>

#define KERN_MALLOC_BUCKETS	1
#define KERN_MALLOC_BUCKET	2
#define KERN_MALLOC_KMEMNAMES	3
#define KERN_MALLOC_KMEMSTATS	4
#define KERN_MALLOC_MAXID	5

#define CTL_KERN_MALLOC_NAMES { \
	{ 0, 0 }, \
	{ "buckets", CTLTYPE_STRING }, \
	{ "bucket", CTLTYPE_NODE }, \
	{ "kmemnames", CTLTYPE_STRING }, \
	{ "kmemstat", CTLTYPE_NODE }, \
}

/*
 * flags to malloc
 */
#define	M_WAITOK	0x0001
#define	M_NOWAIT	0x0002
#define	M_CANFAIL	0x0004
#define	M_ZERO		0x0008

/*
 * Types of memory to be allocated
 */
#define	M_FREE		0	/* should be on free list */
/* 1 - free */
#define	M_DEVBUF	2	/* device driver memory */
/* 3 - free */
#define	M_PCB		4	/* protocol control blocks */
#define	M_RTABLE	5	/* routing tables */
#define	M_PF		6	/* packet filter structures */
/* 7 - free */
/* 8 - free */
#define	M_IFADDR	9	/* interface addresses */
#define	M_IFGROUP	10	/* interface groups */
#define	M_SYSCTL	11	/* sysctl persistent buffers */
#define	M_COUNTERS	12	/* per-CPU counters via counters_alloc(9) */
/* 13 - free */
#define	M_IOCTLOPS	14	/* ioctl data buffers */
/* 15-18 - free */
#define	M_IOV		19	/* large IOVs */
#define	M_MOUNT		20	/* VFS mount structs */
/* 21 - free */
#define	M_NFSREQ	22	/* NFS request headers */
#define	M_NFSMNT	23	/* NFS mount structures */
#define	M_LOG		24	/* messages in kernel log stash */
#define	M_VNODE		25	/* Dynamically allocated vnodes */
/* 26 - free */
#define	M_DQUOT		27	/* UFS quota entries */
#define	M_UFSMNT	28	/* UFS mount structures */
#define	M_SHM		29	/* SVID compatible shared memory segments */
#define	M_VMMAP		30	/* VM map structures */
#define	M_SEM		31	/* SVID compatible semaphores */
#define	M_DIRHASH	32	/* UFS directory hash structures */
#define	M_ACPI		33	/* ACPI structures */
#define	M_VMPMAP	34	/* VM pmap data */
/* 35-38 - free */
#define	M_FILEDESC	39	/* open file descriptor tables */
#define	M_SIGIO		40	/* sigio structures */
#define	M_PROC		41	/* proc structures */
#define	M_SUBPROC	42	/* proc sub-structures */
/* 43-45 - free */
#define	M_MFSNODE	46	/* MFS vnode private part */
/* 47-48 - free */
#define	M_NETADDR	49	/* export host address structures */
#define	M_NFSSVC	50	/* NFS server structures */
/* 51 - free */
#define	M_NFSD		52	/* NFS server daemon structures */
#define	M_IPMOPTS	53	/* internet multicast options */
#define	M_IPMADDR	54	/* internet multicast addresses */
#define	M_IFMADDR	55	/* link-level multicast addresses */
#define	M_MRTABLE	56	/* multicast routing tables */
#define	M_ISOFSMNT	57	/* ISOFS mount structures */
#define	M_ISOFSNODE	58	/* ISOFS vnode private part */
#define	M_MSDOSFSMNT	59	/* MSDOS FS mount structures */
#define	M_MSDOSFSFAT	60	/* MSDOS FS FAT tables */
#define	M_MSDOSFSNODE	61	/* MSDOS FS vnode private part */
#define	M_TTYS		62	/* allocated tty structures */
#define	M_EXEC		63	/* argument lists & other mem used by exec */
#define	M_MISCFSMNT	64	/* miscellaneous FS mount structures */
#define	M_FUSEFS	65	/* FUSE FS mount structures */
/* 66-73 - free */
#define	M_PFKEY		74	/* pfkey data */
#define	M_TDB		75	/* transforms database */
#define	M_XDATA		76	/* IPsec data */
/* 77 - free */
#define	M_PAGEDEP	78	/* file page dependencies */
#define	M_INODEDEP	79	/* inode dependencies */
#define	M_NEWBLK	80	/* new block allocation */
/* 81-82 - free */
#define	M_INDIRDEP	83	/* indirect block dependencies */
/* 84-91 - free */
#define	M_VMSWAP	92	/* VM swap structures */
/* 93-97 - free */
#define	M_UVMAMAP	98	/* UVM amap and related */
#define	M_UVMAOBJ	99	/* UVM aobj and related */
#define	M_PINSYSCALL	100	/* pinsyscall */
#define	M_USB		101	/* USB general */
#define	M_USBDEV	102	/* USB device driver */
#define	M_USBHC		103	/* USB host controller */
#define	M_WITNESS	104	/* witness(4) memory */
#define	M_MEMDESC	105	/* memory range */
/* 106-107 - free */
#define	M_CRYPTO_DATA	108	/* crypto(9) data buffers */
/* 109 - free */
#define	M_CREDENTIALS	110	/* ipsec(4) related credentials */
/* 111-122 - free */

/* KAME IPv6 */
#define	M_IP6OPT	123	/* IPv6 options */
#define	M_IP6NDP	124	/* IPv6 Neighbor Discovery structures */
/* 125-126 - free */
#define	M_TEMP		127	/* miscellaneous temporary data buffers */

#define	M_NTFSMNT	128	/* NTFS mount structures */
#define	M_NTFSNTNODE	129	/* NTFS ntnode information */
#define	M_NTFSFNODE	130	/* NTFS fnode information */
#define	M_NTFSDIR	131	/* NTFS directory buffers */
#define	M_NTFSNTHASH	132	/* NTFS ntnode hash tables */
#define	M_NTFSNTVATTR	133	/* NTFS file attribute information */
#define	M_NTFSRDATA	134	/* NTFS resident data */
#define	M_NTFSDECOMP	135	/* NTFS decompression temporary storage */
#define	M_NTFSRUN	136	/* NTFS vrun storage */

#define	M_KEVENT	137	/* kqueue(2) data structures */

	/*		138	   free */
#define	M_SYNCACHE	139	/* SYN cache hash array */

#define	M_UDFMOUNT	140	/* UDF mount structures */
#define	M_UDFFENTRY	141	/* UDF file entries */
#define	M_UDFFID	142	/* UDF file IDs */

	/*		143	   free */

#define	M_AGP		144	/* AGP memory */

#define	M_DRM		145	/* Direct Rendering Manager */

#define	M_LAST		146	/* Must be last type + 1 */

#define	INITKMEMNAMES { \
	"free",		/* 0 M_FREE */ \
	NULL, \
	"devbuf",	/* 2 M_DEVBUF */ \
	NULL, \
	"pcb",		/* 4 M_PCB */ \
	"rtable",	/* 5 M_RTABLE */ \
	"pf",		/* 6 M_PF */ \
	NULL, \
	NULL, \
	"ifaddr",	/* 9 M_IFADDR */ \
	"ifgroup",	/* 10 M_IFGROUP */ \
	"sysctl",	/* 11 M_SYSCTL */ \
	"counters",	/* 12 M_COUNTERS */ \
	NULL, \
	"ioctlops",	/* 14 M_IOCTLOPS */ \
	NULL, \
	NULL, \
	NULL, \
	NULL, \
	"iov",		/* 19 M_IOV */ \
	"mount",	/* 20 M_MOUNT */ \
	NULL, \
	"NFS req",	/* 22 M_NFSREQ */ \
	"NFS mount",	/* 23 M_NFSMNT */ \
	"log",		/* 24 M_LOG */ \
	"vnodes",	/* 25 M_VNODE */ \
	NULL, \
	"UFS quota",	/* 27 M_DQUOT */ \
	"UFS mount",	/* 28 M_UFSMNT */ \
	"shm",		/* 29 M_SHM */ \
	"VM map",	/* 30 M_VMMAP */ \
	"sem",		/* 31 M_SEM */ \
	"dirhash",	/* 32 M_DIRHASH */ \
	"ACPI", 	/* 33 M_ACPI */ \
	"VM pmap",	/* 34 M_VMPMAP */ \
	NULL,	/* 35 */ \
	NULL,	/* 36 */ \
	NULL,	/* 37 */ \
	NULL, \
	"file desc",	/* 39 M_FILEDESC */ \
	"sigio",	/* 40 M_SIGIO */ \
	"proc",		/* 41 M_PROC */ \
	"subproc",	/* 42 M_SUBPROC */ \
	NULL, \
	NULL, \
	NULL, \
	"MFS node",	/* 46 M_MFSNODE */ \
	NULL, \
	NULL, \
	"Export Host",	/* 49 M_NETADDR */ \
	"NFS srvsock",	/* 50 M_NFSSVC */ \
	NULL, \
	"NFS daemon",	/* 52 M_NFSD */ \
	"ip_moptions",	/* 53 M_IPMOPTS */ \
	"in_multi",	/* 54 M_IPMADDR */ \
	"ether_multi",	/* 55 M_IFMADDR */ \
	"mrt",		/* 56 M_MRTABLE */ \
	"ISOFS mount",	/* 57 M_ISOFSMNT */ \
	"ISOFS node",	/* 58 M_ISOFSNODE */ \
	"MSDOSFS mount", /* 59 M_MSDOSFSMNT */ \
	"MSDOSFS fat",	/* 60 M_MSDOSFSFAT */ \
	"MSDOSFS node",	/* 61 M_MSDOSFSNODE */ \
	"ttys",		/* 62 M_TTYS */ \
	"exec",		/* 63 M_EXEC */ \
	"miscfs mount",	/* 64 M_MISCFSMNT */ \
	"fusefs mount", /* 65 M_FUSEFS */ \
	NULL, \
	NULL, \
	NULL, \
	NULL, \
	NULL, \
	NULL, \
	NULL, \
	NULL, \
	"pfkey data",	/* 74 M_PFKEY */ \
	"tdb",		/* 75 M_TDB */ \
	"xform_data",	/* 76 M_XDATA */ \
	NULL, \
	"pagedep",	/* 78 M_PAGEDEP */ \
	"inodedep",	/* 79 M_INODEDEP */ \
	"newblk",	/* 80 M_NEWBLK */ \
	NULL, \
	NULL, \
	"indirdep",	/* 83 M_INDIRDEP */ \
	NULL, NULL, NULL, NULL, \
	NULL, NULL, NULL, NULL, \
	"VM swap",	/* 92 M_VMSWAP */ \
	NULL, NULL, NULL, NULL, NULL, \
	"UVM amap",	/* 98 M_UVMAMAP */ \
	"UVM aobj",	/* 99 M_UVMAOBJ */ \
	"pinsyscall",	/* 100 M_PINSYSCALL */ \
	"USB",		/* 101 M_USB */ \
	"USB device",	/* 102 M_USBDEV */ \
	"USB HC",	/* 103 M_USBHC */ \
	"witness",	/* 104 M_WITNESS */ \
	"memdesc",	/* 105 M_MEMDESC */ \
	NULL,	/* 106 */ \
	NULL, \
	"crypto data",	/* 108 M_CRYPTO_DATA */ \
	NULL, \
	"IPsec creds",	/* 110 M_CREDENTIALS */ \
	NULL, NULL, NULL, NULL, \
	NULL, NULL, NULL, NULL, \
	NULL, NULL, NULL, NULL, \
	"ip6_options",	/* 123 M_IP6OPT */ \
	"NDP",		/* 124 M_IP6NDP */ \
	NULL, \
	NULL, \
	"temp",		/* 127 M_TEMP */ \
	"NTFS mount",	/* 128 M_NTFSMNT */ \
	"NTFS node",	/* 129 M_NTFSNTNODE */ \
	"NTFS fnode",	/* 130 M_NTFSFNODE */ \
	"NTFS dir",	/* 131 M_NTFSDIR */ \
	"NTFS hash",	/* 132 M_NTFSNTHASH */ \
	"NTFS attr",	/* 133 M_NTFSNTVATTR */ \
	"NTFS data",	/* 134 M_NTFSRDATA */ \
	"NTFS decomp",	/* 135 M_NTFSDECOMP */ \
	"NTFS vrun",	/* 136 M_NTFSRUN */ \
	"kqueue",	/* 137 M_KEVENT */ \
	NULL,	/* 138 free */ \
	"SYN cache",	/* 139 M_SYNCACHE */ \
	"UDF mount",	/* 140 M_UDFMOUNT */ \
	"UDF file entry",	/* 141 M_UDFFENTRY */ \
	"UDF file id",	/* 142 M_UDFFID */ \
	NULL,	/* 143 free */ \
	"AGP Memory",	/* 144 M_AGP */ \
	"DRM",	/* 145 M_DRM */ \
}

struct kmemstats {
	long	ks_inuse;	/* # of packets of this type currently in use */
	long	ks_calls;	/* total packets of this type ever allocated */
	long 	ks_memuse;	/* total memory held in bytes */
	u_short	ks_limblocks;	/* number of times blocked for hitting limit */
	long	ks_maxused;	/* maximum number ever used */
	long	ks_limit;	/* most that are allowed to exist */
	long	ks_size;	/* sizes of this thing that are allocated */
	long	ks_spare;
};

/*
 * Array of descriptors that describe the contents of each page
 */
struct kmemusage {
	short ku_indx;		/* bucket index */
	union {
		u_short freecnt;/* for small allocations, free pieces in page */
		u_short pagecnt;/* for large allocations, pages alloced */
	} ku_un;
};
#define	ku_freecnt ku_un.freecnt
#define	ku_pagecnt ku_un.pagecnt

struct kmem_freelist;

/*
 * Set of buckets for each size of memory block that is retained
 */
struct kmembuckets {
	XSIMPLEQ_HEAD(, kmem_freelist) kb_freelist; /* list of free blocks */
	u_int64_t kb_calls;	/* total calls to allocate this size */
	u_int64_t kb_total;	/* total number of blocks allocated */
	u_int64_t kb_totalfree;	/* # of free elements in this bucket */
	u_int64_t kb_elmpercl;	/* # of elements in this sized allocation */
	u_int64_t kb_highwat;	/* high water mark */
	u_int64_t kb_couldfree;	/* over high water mark and could free */
};

/*
 * Constants for setting the parameters of the kernel memory allocator.
 *
 * 2 ** MINBUCKET is the smallest unit of memory that will be
 * allocated. It must be at least large enough to hold a pointer.
 *
 * Units of memory less or equal to MAXALLOCSAVE will permanently
 * allocate physical memory; requests for these size pieces of
 * memory are quite fast. Allocations greater than MAXALLOCSAVE must
 * always allocate and free physical memory; requests for these
 * size allocations should be done infrequently as they will be slow.
 *
 * Constraints: PAGE_SIZE <= MAXALLOCSAVE <= 2 ** (MINBUCKET + 14), and
 * MAXALLOCSIZE must be a power of two.
 */
#define MINBUCKET	4		/* 4 => min allocation of 16 bytes */

#ifdef _KERNEL

#define	MINALLOCSIZE	(1 << MINBUCKET)
#define	MAXALLOCSAVE	(2 * PAGE_SIZE)
#define	MALLOC_MAX	(65535 * PAGE_SIZE)

/*
 * Turn virtual addresses into kmem map indices
 */
#define	kmemxtob(alloc)	(kmembase + (alloc) * PAGE_SIZE)
#define	btokmemx(addr)	(((caddr_t)(addr) - kmembase) / PAGE_SIZE)
#define	btokup(addr)	(&kmemusage[((caddr_t)(addr) - kmembase) >> PAGE_SHIFT])

extern struct kmemstats kmemstats[];
extern struct kmemusage *kmemusage;
extern char *kmembase;
extern struct kmembuckets bucket[];

void	*malloc(size_t, int, int);
void	*mallocarray(size_t, size_t, int, int);
void	free(void *, int, size_t);
int	sysctl_malloc(int *, u_int, void *, size_t *, void *, size_t,
	    struct proc *);

void	malloc_printit(int (*)(const char *, ...));

void	poison_mem(void *, size_t);
int	poison_check(void *, size_t, size_t *, uint32_t *);
uint32_t poison_value(void *);

#endif /* _KERNEL */
#endif /* !_SYS_MALLOC_H_ */
