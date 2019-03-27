/*-
 * SPDX-License-Identifier: (Beerware AND BSD-3-Clause)
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 *
 */

/*-
 * The following functions are based in the vn(4) driver: mdstart_swap(),
 * mdstart_vnode(), mdcreate_swap(), mdcreate_vnode() and mddestroy(),
 * and as such under the following copyright:
 *
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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
 * from: Utah Hdr: vn.c 1.13 94/04/02
 *
 *	from: @(#)vn.c	8.6 (Berkeley) 4/1/94
 * From: src/sys/dev/vn/vn.c,v 1.122 2000/12/16 16:06:03
 */

#include "opt_rootdevname.h"
#include "opt_geom.h"
#include "opt_md.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/limits.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mdioctl.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/sf_buf.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/disk.h>

#include <geom/geom.h>
#include <geom/geom_int.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>
#include <vm/uma.h>

#include <machine/bus.h>

#define MD_MODVER 1

#define MD_SHUTDOWN	0x10000		/* Tell worker thread to terminate. */
#define	MD_EXITING	0x20000		/* Worker thread is exiting. */

#ifndef MD_NSECT
#define MD_NSECT (10000 * 2)
#endif

struct md_req {
	unsigned	md_unit;	/* unit number */
	enum md_types	md_type;	/* type of disk */
	off_t		md_mediasize;	/* size of disk in bytes */
	unsigned	md_sectorsize;	/* sectorsize */
	unsigned	md_options;	/* options */
	int		md_fwheads;	/* firmware heads */
	int		md_fwsectors;	/* firmware sectors */
	char		*md_file;	/* pathname of file to mount */
	enum uio_seg	md_file_seg;	/* location of md_file */
	char		*md_label;	/* label of the device (userspace) */
	int		*md_units;	/* pointer to units array (kernel) */
	size_t		md_units_nitems; /* items in md_units array */
};

#ifdef COMPAT_FREEBSD32
struct md_ioctl32 {
	unsigned	md_version;
	unsigned	md_unit;
	enum md_types	md_type;
	uint32_t	md_file;
	off_t		md_mediasize;
	unsigned	md_sectorsize;
	unsigned	md_options;
	uint64_t	md_base;
	int		md_fwheads;
	int		md_fwsectors;
	uint32_t	md_label;
	int		md_pad[MDNPAD];
} __attribute__((__packed__));
CTASSERT((sizeof(struct md_ioctl32)) == 436);

#define	MDIOCATTACH_32	_IOC_NEWTYPE(MDIOCATTACH, struct md_ioctl32)
#define	MDIOCDETACH_32	_IOC_NEWTYPE(MDIOCDETACH, struct md_ioctl32)
#define	MDIOCQUERY_32	_IOC_NEWTYPE(MDIOCQUERY, struct md_ioctl32)
#define	MDIOCLIST_32	_IOC_NEWTYPE(MDIOCLIST, struct md_ioctl32)
#define	MDIOCRESIZE_32	_IOC_NEWTYPE(MDIOCRESIZE, struct md_ioctl32)
#endif /* COMPAT_FREEBSD32 */

static MALLOC_DEFINE(M_MD, "md_disk", "Memory Disk");
static MALLOC_DEFINE(M_MDSECT, "md_sectors", "Memory Disk Sectors");

static int md_debug;
SYSCTL_INT(_debug, OID_AUTO, mddebug, CTLFLAG_RW, &md_debug, 0,
    "Enable md(4) debug messages");
static int md_malloc_wait;
SYSCTL_INT(_vm, OID_AUTO, md_malloc_wait, CTLFLAG_RW, &md_malloc_wait, 0,
    "Allow malloc to wait for memory allocations");

#if defined(MD_ROOT) && !defined(MD_ROOT_FSTYPE)
#define	MD_ROOT_FSTYPE	"ufs"
#endif

#if defined(MD_ROOT)
/*
 * Preloaded image gets put here.
 */
#if defined(MD_ROOT_SIZE)
/*
 * We put the mfs_root symbol into the oldmfs section of the kernel object file.
 * Applications that patch the object with the image can determine
 * the size looking at the oldmfs section size within the kernel.
 */
u_char mfs_root[MD_ROOT_SIZE*1024] __attribute__ ((section ("oldmfs")));
const int mfs_root_size = sizeof(mfs_root);
#elif defined(MD_ROOT_MEM)
/* MD region already mapped in the memory */
u_char *mfs_root;
int mfs_root_size;
#else
extern volatile u_char __weak_symbol mfs_root;
extern volatile u_char __weak_symbol mfs_root_end;
__GLOBL(mfs_root);
__GLOBL(mfs_root_end);
#define mfs_root_size ((uintptr_t)(&mfs_root_end - &mfs_root))
#endif
#endif

static g_init_t g_md_init;
static g_fini_t g_md_fini;
static g_start_t g_md_start;
static g_access_t g_md_access;
static void g_md_dumpconf(struct sbuf *sb, const char *indent,
    struct g_geom *gp, struct g_consumer *cp __unused, struct g_provider *pp);

static struct cdev *status_dev = NULL;
static struct sx md_sx;
static struct unrhdr *md_uh;

static d_ioctl_t mdctlioctl;

static struct cdevsw mdctl_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	mdctlioctl,
	.d_name =	MD_NAME,
};

struct g_class g_md_class = {
	.name = "MD",
	.version = G_VERSION,
	.init = g_md_init,
	.fini = g_md_fini,
	.start = g_md_start,
	.access = g_md_access,
	.dumpconf = g_md_dumpconf,
};

DECLARE_GEOM_CLASS(g_md_class, g_md);


static LIST_HEAD(, md_s) md_softc_list = LIST_HEAD_INITIALIZER(md_softc_list);

#define NINDIR	(PAGE_SIZE / sizeof(uintptr_t))
#define NMASK	(NINDIR-1)
static int nshift;

static uma_zone_t md_pbuf_zone;

struct indir {
	uintptr_t	*array;
	u_int		total;
	u_int		used;
	u_int		shift;
};

struct md_s {
	int unit;
	LIST_ENTRY(md_s) list;
	struct bio_queue_head bio_queue;
	struct mtx queue_mtx;
	struct mtx stat_mtx;
	struct cdev *dev;
	enum md_types type;
	off_t mediasize;
	unsigned sectorsize;
	unsigned opencount;
	unsigned fwheads;
	unsigned fwsectors;
	char ident[32];
	unsigned flags;
	char name[20];
	struct proc *procp;
	struct g_geom *gp;
	struct g_provider *pp;
	int (*start)(struct md_s *sc, struct bio *bp);
	struct devstat *devstat;

	/* MD_MALLOC related fields */
	struct indir *indir;
	uma_zone_t uma;

	/* MD_PRELOAD related fields */
	u_char *pl_ptr;
	size_t pl_len;

	/* MD_VNODE related fields */
	struct vnode *vnode;
	char file[PATH_MAX];
	char label[PATH_MAX];
	struct ucred *cred;

	/* MD_SWAP related fields */
	vm_object_t object;
};

static struct indir *
new_indir(u_int shift)
{
	struct indir *ip;

	ip = malloc(sizeof *ip, M_MD, (md_malloc_wait ? M_WAITOK : M_NOWAIT)
	    | M_ZERO);
	if (ip == NULL)
		return (NULL);
	ip->array = malloc(sizeof(uintptr_t) * NINDIR,
	    M_MDSECT, (md_malloc_wait ? M_WAITOK : M_NOWAIT) | M_ZERO);
	if (ip->array == NULL) {
		free(ip, M_MD);
		return (NULL);
	}
	ip->total = NINDIR;
	ip->shift = shift;
	return (ip);
}

static void
del_indir(struct indir *ip)
{

	free(ip->array, M_MDSECT);
	free(ip, M_MD);
}

static void
destroy_indir(struct md_s *sc, struct indir *ip)
{
	int i;

	for (i = 0; i < NINDIR; i++) {
		if (!ip->array[i])
			continue;
		if (ip->shift)
			destroy_indir(sc, (struct indir*)(ip->array[i]));
		else if (ip->array[i] > 255)
			uma_zfree(sc->uma, (void *)(ip->array[i]));
	}
	del_indir(ip);
}

/*
 * This function does the math and allocates the top level "indir" structure
 * for a device of "size" sectors.
 */

static struct indir *
dimension(off_t size)
{
	off_t rcnt;
	struct indir *ip;
	int layer;

	rcnt = size;
	layer = 0;
	while (rcnt > NINDIR) {
		rcnt /= NINDIR;
		layer++;
	}

	/*
	 * XXX: the top layer is probably not fully populated, so we allocate
	 * too much space for ip->array in here.
	 */
	ip = malloc(sizeof *ip, M_MD, M_WAITOK | M_ZERO);
	ip->array = malloc(sizeof(uintptr_t) * NINDIR,
	    M_MDSECT, M_WAITOK | M_ZERO);
	ip->total = NINDIR;
	ip->shift = layer * nshift;
	return (ip);
}

/*
 * Read a given sector
 */

static uintptr_t
s_read(struct indir *ip, off_t offset)
{
	struct indir *cip;
	int idx;
	uintptr_t up;

	if (md_debug > 1)
		printf("s_read(%jd)\n", (intmax_t)offset);
	up = 0;
	for (cip = ip; cip != NULL;) {
		if (cip->shift) {
			idx = (offset >> cip->shift) & NMASK;
			up = cip->array[idx];
			cip = (struct indir *)up;
			continue;
		}
		idx = offset & NMASK;
		return (cip->array[idx]);
	}
	return (0);
}

/*
 * Write a given sector, prune the tree if the value is 0
 */

static int
s_write(struct indir *ip, off_t offset, uintptr_t ptr)
{
	struct indir *cip, *lip[10];
	int idx, li;
	uintptr_t up;

	if (md_debug > 1)
		printf("s_write(%jd, %p)\n", (intmax_t)offset, (void *)ptr);
	up = 0;
	li = 0;
	cip = ip;
	for (;;) {
		lip[li++] = cip;
		if (cip->shift) {
			idx = (offset >> cip->shift) & NMASK;
			up = cip->array[idx];
			if (up != 0) {
				cip = (struct indir *)up;
				continue;
			}
			/* Allocate branch */
			cip->array[idx] =
			    (uintptr_t)new_indir(cip->shift - nshift);
			if (cip->array[idx] == 0)
				return (ENOSPC);
			cip->used++;
			up = cip->array[idx];
			cip = (struct indir *)up;
			continue;
		}
		/* leafnode */
		idx = offset & NMASK;
		up = cip->array[idx];
		if (up != 0)
			cip->used--;
		cip->array[idx] = ptr;
		if (ptr != 0)
			cip->used++;
		break;
	}
	if (cip->used != 0 || li == 1)
		return (0);
	li--;
	while (cip->used == 0 && cip != ip) {
		li--;
		idx = (offset >> lip[li]->shift) & NMASK;
		up = lip[li]->array[idx];
		KASSERT(up == (uintptr_t)cip, ("md screwed up"));
		del_indir(cip);
		lip[li]->array[idx] = 0;
		lip[li]->used--;
		cip = lip[li];
	}
	return (0);
}


static int
g_md_access(struct g_provider *pp, int r, int w, int e)
{
	struct md_s *sc;

	sc = pp->geom->softc;
	if (sc == NULL) {
		if (r <= 0 && w <= 0 && e <= 0)
			return (0);
		return (ENXIO);
	}
	r += pp->acr;
	w += pp->acw;
	e += pp->ace;
	if ((sc->flags & MD_READONLY) != 0 && w > 0)
		return (EROFS);
	if ((pp->acr + pp->acw + pp->ace) == 0 && (r + w + e) > 0) {
		sc->opencount = 1;
	} else if ((pp->acr + pp->acw + pp->ace) > 0 && (r + w + e) == 0) {
		sc->opencount = 0;
	}
	return (0);
}

static void
g_md_start(struct bio *bp)
{
	struct md_s *sc;

	sc = bp->bio_to->geom->softc;
	if ((bp->bio_cmd == BIO_READ) || (bp->bio_cmd == BIO_WRITE)) {
		mtx_lock(&sc->stat_mtx);
		devstat_start_transaction_bio(sc->devstat, bp);
		mtx_unlock(&sc->stat_mtx);
	}
	mtx_lock(&sc->queue_mtx);
	bioq_disksort(&sc->bio_queue, bp);
	mtx_unlock(&sc->queue_mtx);
	wakeup(sc);
}

#define	MD_MALLOC_MOVE_ZERO	1
#define	MD_MALLOC_MOVE_FILL	2
#define	MD_MALLOC_MOVE_READ	3
#define	MD_MALLOC_MOVE_WRITE	4
#define	MD_MALLOC_MOVE_CMP	5

static int
md_malloc_move_ma(vm_page_t **mp, int *ma_offs, unsigned sectorsize,
    void *ptr, u_char fill, int op)
{
	struct sf_buf *sf;
	vm_page_t m, *mp1;
	char *p, first;
	off_t *uc;
	unsigned n;
	int error, i, ma_offs1, sz, first_read;

	m = NULL;
	error = 0;
	sf = NULL;
	/* if (op == MD_MALLOC_MOVE_CMP) { gcc */
		first = 0;
		first_read = 0;
		uc = ptr;
		mp1 = *mp;
		ma_offs1 = *ma_offs;
	/* } */
	sched_pin();
	for (n = sectorsize; n != 0; n -= sz) {
		sz = imin(PAGE_SIZE - *ma_offs, n);
		if (m != **mp) {
			if (sf != NULL)
				sf_buf_free(sf);
			m = **mp;
			sf = sf_buf_alloc(m, SFB_CPUPRIVATE |
			    (md_malloc_wait ? 0 : SFB_NOWAIT));
			if (sf == NULL) {
				error = ENOMEM;
				break;
			}
		}
		p = (char *)sf_buf_kva(sf) + *ma_offs;
		switch (op) {
		case MD_MALLOC_MOVE_ZERO:
			bzero(p, sz);
			break;
		case MD_MALLOC_MOVE_FILL:
			memset(p, fill, sz);
			break;
		case MD_MALLOC_MOVE_READ:
			bcopy(ptr, p, sz);
			cpu_flush_dcache(p, sz);
			break;
		case MD_MALLOC_MOVE_WRITE:
			bcopy(p, ptr, sz);
			break;
		case MD_MALLOC_MOVE_CMP:
			for (i = 0; i < sz; i++, p++) {
				if (!first_read) {
					*uc = (u_char)*p;
					first = *p;
					first_read = 1;
				} else if (*p != first) {
					error = EDOOFUS;
					break;
				}
			}
			break;
		default:
			KASSERT(0, ("md_malloc_move_ma unknown op %d\n", op));
			break;
		}
		if (error != 0)
			break;
		*ma_offs += sz;
		*ma_offs %= PAGE_SIZE;
		if (*ma_offs == 0)
			(*mp)++;
		ptr = (char *)ptr + sz;
	}

	if (sf != NULL)
		sf_buf_free(sf);
	sched_unpin();
	if (op == MD_MALLOC_MOVE_CMP && error != 0) {
		*mp = mp1;
		*ma_offs = ma_offs1;
	}
	return (error);
}

static int
md_malloc_move_vlist(bus_dma_segment_t **pvlist, int *pma_offs,
    unsigned len, void *ptr, u_char fill, int op)
{
	bus_dma_segment_t *vlist;
	uint8_t *p, *end, first;
	off_t *uc;
	int ma_offs, seg_len;

	vlist = *pvlist;
	ma_offs = *pma_offs;
	uc = ptr;

	for (; len != 0; len -= seg_len) {
		seg_len = imin(vlist->ds_len - ma_offs, len);
		p = (uint8_t *)(uintptr_t)vlist->ds_addr + ma_offs;
		switch (op) {
		case MD_MALLOC_MOVE_ZERO:
			bzero(p, seg_len);
			break;
		case MD_MALLOC_MOVE_FILL:
			memset(p, fill, seg_len);
			break;
		case MD_MALLOC_MOVE_READ:
			bcopy(ptr, p, seg_len);
			cpu_flush_dcache(p, seg_len);
			break;
		case MD_MALLOC_MOVE_WRITE:
			bcopy(p, ptr, seg_len);
			break;
		case MD_MALLOC_MOVE_CMP:
			end = p + seg_len;
			first = *uc = *p;
			/* Confirm all following bytes match the first */
			while (++p < end) {
				if (*p != first)
					return (EDOOFUS);
			}
			break;
		default:
			KASSERT(0, ("md_malloc_move_vlist unknown op %d\n", op));
			break;
		}

		ma_offs += seg_len;
		if (ma_offs == vlist->ds_len) {
			ma_offs = 0;
			vlist++;
		}
		ptr = (uint8_t *)ptr + seg_len;
	}
	*pvlist = vlist;
	*pma_offs = ma_offs;

	return (0);
}

static int
mdstart_malloc(struct md_s *sc, struct bio *bp)
{
	u_char *dst;
	vm_page_t *m;
	bus_dma_segment_t *vlist;
	int i, error, error1, ma_offs, notmapped;
	off_t secno, nsec, uc;
	uintptr_t sp, osp;

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
		break;
	default:
		return (EOPNOTSUPP);
	}

	notmapped = (bp->bio_flags & BIO_UNMAPPED) != 0;
	vlist = (bp->bio_flags & BIO_VLIST) != 0 ?
	    (bus_dma_segment_t *)bp->bio_data : NULL;
	if (notmapped) {
		m = bp->bio_ma;
		ma_offs = bp->bio_ma_offset;
		dst = NULL;
		KASSERT(vlist == NULL, ("vlists cannot be unmapped"));
	} else if (vlist != NULL) {
		ma_offs = bp->bio_ma_offset;
		dst = NULL;
	} else {
		dst = bp->bio_data;
	}

	nsec = bp->bio_length / sc->sectorsize;
	secno = bp->bio_offset / sc->sectorsize;
	error = 0;
	while (nsec--) {
		osp = s_read(sc->indir, secno);
		if (bp->bio_cmd == BIO_DELETE) {
			if (osp != 0)
				error = s_write(sc->indir, secno, 0);
		} else if (bp->bio_cmd == BIO_READ) {
			if (osp == 0) {
				if (notmapped) {
					error = md_malloc_move_ma(&m, &ma_offs,
					    sc->sectorsize, NULL, 0,
					    MD_MALLOC_MOVE_ZERO);
				} else if (vlist != NULL) {
					error = md_malloc_move_vlist(&vlist,
					    &ma_offs, sc->sectorsize, NULL, 0,
					    MD_MALLOC_MOVE_ZERO);
				} else
					bzero(dst, sc->sectorsize);
			} else if (osp <= 255) {
				if (notmapped) {
					error = md_malloc_move_ma(&m, &ma_offs,
					    sc->sectorsize, NULL, osp,
					    MD_MALLOC_MOVE_FILL);
				} else if (vlist != NULL) {
					error = md_malloc_move_vlist(&vlist,
					    &ma_offs, sc->sectorsize, NULL, osp,
					    MD_MALLOC_MOVE_FILL);
				} else
					memset(dst, osp, sc->sectorsize);
			} else {
				if (notmapped) {
					error = md_malloc_move_ma(&m, &ma_offs,
					    sc->sectorsize, (void *)osp, 0,
					    MD_MALLOC_MOVE_READ);
				} else if (vlist != NULL) {
					error = md_malloc_move_vlist(&vlist,
					    &ma_offs, sc->sectorsize,
					    (void *)osp, 0,
					    MD_MALLOC_MOVE_READ);
				} else {
					bcopy((void *)osp, dst, sc->sectorsize);
					cpu_flush_dcache(dst, sc->sectorsize);
				}
			}
			osp = 0;
		} else if (bp->bio_cmd == BIO_WRITE) {
			if (sc->flags & MD_COMPRESS) {
				if (notmapped) {
					error1 = md_malloc_move_ma(&m, &ma_offs,
					    sc->sectorsize, &uc, 0,
					    MD_MALLOC_MOVE_CMP);
					i = error1 == 0 ? sc->sectorsize : 0;
				} else if (vlist != NULL) {
					error1 = md_malloc_move_vlist(&vlist,
					    &ma_offs, sc->sectorsize, &uc, 0,
					    MD_MALLOC_MOVE_CMP);
					i = error1 == 0 ? sc->sectorsize : 0;
				} else {
					uc = dst[0];
					for (i = 1; i < sc->sectorsize; i++) {
						if (dst[i] != uc)
							break;
					}
				}
			} else {
				i = 0;
				uc = 0;
			}
			if (i == sc->sectorsize) {
				if (osp != uc)
					error = s_write(sc->indir, secno, uc);
			} else {
				if (osp <= 255) {
					sp = (uintptr_t)uma_zalloc(sc->uma,
					    md_malloc_wait ? M_WAITOK :
					    M_NOWAIT);
					if (sp == 0) {
						error = ENOSPC;
						break;
					}
					if (notmapped) {
						error = md_malloc_move_ma(&m,
						    &ma_offs, sc->sectorsize,
						    (void *)sp, 0,
						    MD_MALLOC_MOVE_WRITE);
					} else if (vlist != NULL) {
						error = md_malloc_move_vlist(
						    &vlist, &ma_offs,
						    sc->sectorsize, (void *)sp,
						    0, MD_MALLOC_MOVE_WRITE);
					} else {
						bcopy(dst, (void *)sp,
						    sc->sectorsize);
					}
					error = s_write(sc->indir, secno, sp);
				} else {
					if (notmapped) {
						error = md_malloc_move_ma(&m,
						    &ma_offs, sc->sectorsize,
						    (void *)osp, 0,
						    MD_MALLOC_MOVE_WRITE);
					} else if (vlist != NULL) {
						error = md_malloc_move_vlist(
						    &vlist, &ma_offs,
						    sc->sectorsize, (void *)osp,
						    0, MD_MALLOC_MOVE_WRITE);
					} else {
						bcopy(dst, (void *)osp,
						    sc->sectorsize);
					}
					osp = 0;
				}
			}
		} else {
			error = EOPNOTSUPP;
		}
		if (osp > 255)
			uma_zfree(sc->uma, (void*)osp);
		if (error != 0)
			break;
		secno++;
		if (!notmapped && vlist == NULL)
			dst += sc->sectorsize;
	}
	bp->bio_resid = 0;
	return (error);
}

static void
mdcopyto_vlist(void *src, bus_dma_segment_t *vlist, off_t offset, off_t len)
{
	off_t seg_len;

	while (offset >= vlist->ds_len) {
		offset -= vlist->ds_len;
		vlist++;
	}

	while (len != 0) {
		seg_len = omin(len, vlist->ds_len - offset);
		bcopy(src, (void *)(uintptr_t)(vlist->ds_addr + offset),
		    seg_len);
		offset = 0;
		src = (uint8_t *)src + seg_len;
		len -= seg_len;
		vlist++;
	}
}

static void
mdcopyfrom_vlist(bus_dma_segment_t *vlist, off_t offset, void *dst, off_t len)
{
	off_t seg_len;

	while (offset >= vlist->ds_len) {
		offset -= vlist->ds_len;
		vlist++;
	}

	while (len != 0) {
		seg_len = omin(len, vlist->ds_len - offset);
		bcopy((void *)(uintptr_t)(vlist->ds_addr + offset), dst,
		    seg_len);
		offset = 0;
		dst = (uint8_t *)dst + seg_len;
		len -= seg_len;
		vlist++;
	}
}

static int
mdstart_preload(struct md_s *sc, struct bio *bp)
{
	uint8_t *p;

	p = sc->pl_ptr + bp->bio_offset;
	switch (bp->bio_cmd) {
	case BIO_READ:
		if ((bp->bio_flags & BIO_VLIST) != 0) {
			mdcopyto_vlist(p, (bus_dma_segment_t *)bp->bio_data,
			    bp->bio_ma_offset, bp->bio_length);
		} else {
			bcopy(p, bp->bio_data, bp->bio_length);
		}
		cpu_flush_dcache(bp->bio_data, bp->bio_length);
		break;
	case BIO_WRITE:
		if ((bp->bio_flags & BIO_VLIST) != 0) {
			mdcopyfrom_vlist((bus_dma_segment_t *)bp->bio_data,
			    bp->bio_ma_offset, p, bp->bio_length);
		} else {
			bcopy(bp->bio_data, p, bp->bio_length);
		}
		break;
	}
	bp->bio_resid = 0;
	return (0);
}

static int
mdstart_vnode(struct md_s *sc, struct bio *bp)
{
	int error;
	struct uio auio;
	struct iovec aiov;
	struct iovec *piov;
	struct mount *mp;
	struct vnode *vp;
	struct buf *pb;
	bus_dma_segment_t *vlist;
	struct thread *td;
	off_t iolen, iostart, len, zerosize;
	int ma_offs, npages;

	switch (bp->bio_cmd) {
	case BIO_READ:
		auio.uio_rw = UIO_READ;
		break;
	case BIO_WRITE:
	case BIO_DELETE:
		auio.uio_rw = UIO_WRITE;
		break;
	case BIO_FLUSH:
		break;
	default:
		return (EOPNOTSUPP);
	}

	td = curthread;
	vp = sc->vnode;
	pb = NULL;
	piov = NULL;
	ma_offs = bp->bio_ma_offset;
	len = bp->bio_length;

	/*
	 * VNODE I/O
	 *
	 * If an error occurs, we set BIO_ERROR but we do not set
	 * B_INVAL because (for a write anyway), the buffer is
	 * still valid.
	 */

	if (bp->bio_cmd == BIO_FLUSH) {
		(void) vn_start_write(vp, &mp, V_WAIT);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_FSYNC(vp, MNT_WAIT, td);
		VOP_UNLOCK(vp, 0);
		vn_finished_write(mp);
		return (error);
	}

	auio.uio_offset = (vm_ooffset_t)bp->bio_offset;
	auio.uio_resid = bp->bio_length;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;

	if (bp->bio_cmd == BIO_DELETE) {
		/*
		 * Emulate BIO_DELETE by writing zeros.
		 */
		zerosize = ZERO_REGION_SIZE -
		    (ZERO_REGION_SIZE % sc->sectorsize);
		auio.uio_iovcnt = howmany(bp->bio_length, zerosize);
		piov = malloc(sizeof(*piov) * auio.uio_iovcnt, M_MD, M_WAITOK);
		auio.uio_iov = piov;
		while (len > 0) {
			piov->iov_base = __DECONST(void *, zero_region);
			piov->iov_len = len;
			if (len > zerosize)
				piov->iov_len = zerosize;
			len -= piov->iov_len;
			piov++;
		}
		piov = auio.uio_iov;
	} else if ((bp->bio_flags & BIO_VLIST) != 0) {
		piov = malloc(sizeof(*piov) * bp->bio_ma_n, M_MD, M_WAITOK);
		auio.uio_iov = piov;
		vlist = (bus_dma_segment_t *)bp->bio_data;
		while (len > 0) {
			piov->iov_base = (void *)(uintptr_t)(vlist->ds_addr +
			    ma_offs);
			piov->iov_len = vlist->ds_len - ma_offs;
			if (piov->iov_len > len)
				piov->iov_len = len;
			len -= piov->iov_len;
			ma_offs = 0;
			vlist++;
			piov++;
		}
		auio.uio_iovcnt = piov - auio.uio_iov;
		piov = auio.uio_iov;
	} else if ((bp->bio_flags & BIO_UNMAPPED) != 0) {
		pb = uma_zalloc(md_pbuf_zone, M_WAITOK);
		bp->bio_resid = len;
unmapped_step:
		npages = atop(min(MAXPHYS, round_page(len + (ma_offs &
		    PAGE_MASK))));
		iolen = min(ptoa(npages) - (ma_offs & PAGE_MASK), len);
		KASSERT(iolen > 0, ("zero iolen"));
		pmap_qenter((vm_offset_t)pb->b_data,
		    &bp->bio_ma[atop(ma_offs)], npages);
		aiov.iov_base = (void *)((vm_offset_t)pb->b_data +
		    (ma_offs & PAGE_MASK));
		aiov.iov_len = iolen;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_resid = iolen;
	} else {
		aiov.iov_base = bp->bio_data;
		aiov.iov_len = bp->bio_length;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
	}
	iostart = auio.uio_offset;
	if (auio.uio_rw == UIO_READ) {
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_READ(vp, &auio, 0, sc->cred);
		VOP_UNLOCK(vp, 0);
	} else {
		(void) vn_start_write(vp, &mp, V_WAIT);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_WRITE(vp, &auio, sc->flags & MD_ASYNC ? 0 : IO_SYNC,
		    sc->cred);
		VOP_UNLOCK(vp, 0);
		vn_finished_write(mp);
		if (error == 0)
			sc->flags &= ~MD_VERIFY;
	}

	/* When MD_CACHE is set, try to avoid double-caching the data. */
	if (error == 0 && (sc->flags & MD_CACHE) == 0)
		VOP_ADVISE(vp, iostart, auio.uio_offset - 1,
		    POSIX_FADV_DONTNEED);

	if (pb != NULL) {
		pmap_qremove((vm_offset_t)pb->b_data, npages);
		if (error == 0) {
			len -= iolen;
			bp->bio_resid -= iolen;
			ma_offs += iolen;
			if (len > 0)
				goto unmapped_step;
		}
		uma_zfree(md_pbuf_zone, pb);
	}

	free(piov, M_MD);
	if (pb == NULL)
		bp->bio_resid = auio.uio_resid;
	return (error);
}

static void
md_swap_page_free(vm_page_t m)
{

	vm_page_xunbusy(m);
	vm_page_lock(m);
	vm_page_free(m);
	vm_page_unlock(m);
}

static int
mdstart_swap(struct md_s *sc, struct bio *bp)
{
	vm_page_t m;
	u_char *p;
	vm_pindex_t i, lastp;
	bus_dma_segment_t *vlist;
	int rv, ma_offs, offs, len, lastend;

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
		break;
	default:
		return (EOPNOTSUPP);
	}

	p = bp->bio_data;
	ma_offs = (bp->bio_flags & (BIO_UNMAPPED|BIO_VLIST)) != 0 ?
	    bp->bio_ma_offset : 0;
	vlist = (bp->bio_flags & BIO_VLIST) != 0 ?
	    (bus_dma_segment_t *)bp->bio_data : NULL;

	/*
	 * offs is the offset at which to start operating on the
	 * next (ie, first) page.  lastp is the last page on
	 * which we're going to operate.  lastend is the ending
	 * position within that last page (ie, PAGE_SIZE if
	 * we're operating on complete aligned pages).
	 */
	offs = bp->bio_offset % PAGE_SIZE;
	lastp = (bp->bio_offset + bp->bio_length - 1) / PAGE_SIZE;
	lastend = (bp->bio_offset + bp->bio_length - 1) % PAGE_SIZE + 1;

	rv = VM_PAGER_OK;
	VM_OBJECT_WLOCK(sc->object);
	vm_object_pip_add(sc->object, 1);
	for (i = bp->bio_offset / PAGE_SIZE; i <= lastp; i++) {
		len = ((i == lastp) ? lastend : PAGE_SIZE) - offs;
		m = vm_page_grab(sc->object, i, VM_ALLOC_SYSTEM);
		if (bp->bio_cmd == BIO_READ) {
			if (m->valid == VM_PAGE_BITS_ALL)
				rv = VM_PAGER_OK;
			else
				rv = vm_pager_get_pages(sc->object, &m, 1,
				    NULL, NULL);
			if (rv == VM_PAGER_ERROR) {
				md_swap_page_free(m);
				break;
			} else if (rv == VM_PAGER_FAIL) {
				/*
				 * Pager does not have the page.  Zero
				 * the allocated page, and mark it as
				 * valid. Do not set dirty, the page
				 * can be recreated if thrown out.
				 */
				pmap_zero_page(m);
				m->valid = VM_PAGE_BITS_ALL;
			}
			if ((bp->bio_flags & BIO_UNMAPPED) != 0) {
				pmap_copy_pages(&m, offs, bp->bio_ma,
				    ma_offs, len);
			} else if ((bp->bio_flags & BIO_VLIST) != 0) {
				physcopyout_vlist(VM_PAGE_TO_PHYS(m) + offs,
				    vlist, ma_offs, len);
				cpu_flush_dcache(p, len);
			} else {
				physcopyout(VM_PAGE_TO_PHYS(m) + offs, p, len);
				cpu_flush_dcache(p, len);
			}
		} else if (bp->bio_cmd == BIO_WRITE) {
			if (len == PAGE_SIZE || m->valid == VM_PAGE_BITS_ALL)
				rv = VM_PAGER_OK;
			else
				rv = vm_pager_get_pages(sc->object, &m, 1,
				    NULL, NULL);
			if (rv == VM_PAGER_ERROR) {
				md_swap_page_free(m);
				break;
			} else if (rv == VM_PAGER_FAIL)
				pmap_zero_page(m);

			if ((bp->bio_flags & BIO_UNMAPPED) != 0) {
				pmap_copy_pages(bp->bio_ma, ma_offs, &m,
				    offs, len);
			} else if ((bp->bio_flags & BIO_VLIST) != 0) {
				physcopyin_vlist(vlist, ma_offs,
				    VM_PAGE_TO_PHYS(m) + offs, len);
			} else {
				physcopyin(p, VM_PAGE_TO_PHYS(m) + offs, len);
			}

			m->valid = VM_PAGE_BITS_ALL;
			if (m->dirty != VM_PAGE_BITS_ALL) {
				vm_page_dirty(m);
				vm_pager_page_unswapped(m);
			}
		} else if (bp->bio_cmd == BIO_DELETE) {
			if (len == PAGE_SIZE || m->valid == VM_PAGE_BITS_ALL)
				rv = VM_PAGER_OK;
			else
				rv = vm_pager_get_pages(sc->object, &m, 1,
				    NULL, NULL);
			if (rv == VM_PAGER_ERROR) {
				md_swap_page_free(m);
				break;
			} else if (rv == VM_PAGER_FAIL) {
				md_swap_page_free(m);
				m = NULL;
			} else {
				/* Page is valid. */
				if (len != PAGE_SIZE) {
					pmap_zero_page_area(m, offs, len);
					if (m->dirty != VM_PAGE_BITS_ALL) {
						vm_page_dirty(m);
						vm_pager_page_unswapped(m);
					}
				} else {
					vm_pager_page_unswapped(m);
					md_swap_page_free(m);
					m = NULL;
				}
			}
		}
		if (m != NULL) {
			vm_page_xunbusy(m);
			vm_page_lock(m);
			if (vm_page_active(m))
				vm_page_reference(m);
			else
				vm_page_activate(m);
			vm_page_unlock(m);
		}

		/* Actions on further pages start at offset 0 */
		p += PAGE_SIZE - offs;
		offs = 0;
		ma_offs += len;
	}
	vm_object_pip_wakeup(sc->object);
	VM_OBJECT_WUNLOCK(sc->object);
	return (rv != VM_PAGER_ERROR ? 0 : ENOSPC);
}

static int
mdstart_null(struct md_s *sc, struct bio *bp)
{

	switch (bp->bio_cmd) {
	case BIO_READ:
		bzero(bp->bio_data, bp->bio_length);
		cpu_flush_dcache(bp->bio_data, bp->bio_length);
		break;
	case BIO_WRITE:
		break;
	}
	bp->bio_resid = 0;
	return (0);
}

static void
md_kthread(void *arg)
{
	struct md_s *sc;
	struct bio *bp;
	int error;

	sc = arg;
	thread_lock(curthread);
	sched_prio(curthread, PRIBIO);
	thread_unlock(curthread);
	if (sc->type == MD_VNODE)
		curthread->td_pflags |= TDP_NORUNNINGBUF;

	for (;;) {
		mtx_lock(&sc->queue_mtx);
		if (sc->flags & MD_SHUTDOWN) {
			sc->flags |= MD_EXITING;
			mtx_unlock(&sc->queue_mtx);
			kproc_exit(0);
		}
		bp = bioq_takefirst(&sc->bio_queue);
		if (!bp) {
			msleep(sc, &sc->queue_mtx, PRIBIO | PDROP, "mdwait", 0);
			continue;
		}
		mtx_unlock(&sc->queue_mtx);
		if (bp->bio_cmd == BIO_GETATTR) {
			int isv = ((sc->flags & MD_VERIFY) != 0);

			if ((sc->fwsectors && sc->fwheads &&
			    (g_handleattr_int(bp, "GEOM::fwsectors",
			    sc->fwsectors) ||
			    g_handleattr_int(bp, "GEOM::fwheads",
			    sc->fwheads))) ||
			    g_handleattr_int(bp, "GEOM::candelete", 1))
				error = -1;
			else if (sc->ident[0] != '\0' &&
			    g_handleattr_str(bp, "GEOM::ident", sc->ident))
				error = -1;
			else if (g_handleattr_int(bp, "MNT::verified", isv))
				error = -1;
			else
				error = EOPNOTSUPP;
		} else {
			error = sc->start(sc, bp);
		}

		if (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE) {
			/*
			 * Devstat uses (bio_bcount, bio_resid) for
			 * determining the length of the completed part of
			 * the i/o.  g_io_deliver() will translate from
			 * bio_completed to that, but it also destroys the
			 * bio so we must do our own translation.
			 */
			bp->bio_bcount = bp->bio_length;
			bp->bio_resid = (error == -1 ? bp->bio_bcount : 0);
			devstat_end_transaction_bio(sc->devstat, bp);
		}
		if (error != -1) {
			bp->bio_completed = bp->bio_length;
			g_io_deliver(bp, error);
		}
	}
}

static struct md_s *
mdfind(int unit)
{
	struct md_s *sc;

	LIST_FOREACH(sc, &md_softc_list, list) {
		if (sc->unit == unit)
			break;
	}
	return (sc);
}

static struct md_s *
mdnew(int unit, int *errp, enum md_types type)
{
	struct md_s *sc;
	int error;

	*errp = 0;
	if (unit == -1)
		unit = alloc_unr(md_uh);
	else
		unit = alloc_unr_specific(md_uh, unit);

	if (unit == -1) {
		*errp = EBUSY;
		return (NULL);
	}

	sc = (struct md_s *)malloc(sizeof *sc, M_MD, M_WAITOK | M_ZERO);
	sc->type = type;
	bioq_init(&sc->bio_queue);
	mtx_init(&sc->queue_mtx, "md bio queue", NULL, MTX_DEF);
	mtx_init(&sc->stat_mtx, "md stat", NULL, MTX_DEF);
	sc->unit = unit;
	sprintf(sc->name, "md%d", unit);
	LIST_INSERT_HEAD(&md_softc_list, sc, list);
	error = kproc_create(md_kthread, sc, &sc->procp, 0, 0,"%s", sc->name);
	if (error == 0)
		return (sc);
	LIST_REMOVE(sc, list);
	mtx_destroy(&sc->stat_mtx);
	mtx_destroy(&sc->queue_mtx);
	free_unr(md_uh, sc->unit);
	free(sc, M_MD);
	*errp = error;
	return (NULL);
}

static void
mdinit(struct md_s *sc)
{
	struct g_geom *gp;
	struct g_provider *pp;

	g_topology_lock();
	gp = g_new_geomf(&g_md_class, "md%d", sc->unit);
	gp->softc = sc;
	pp = g_new_providerf(gp, "md%d", sc->unit);
	pp->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE;
	pp->mediasize = sc->mediasize;
	pp->sectorsize = sc->sectorsize;
	switch (sc->type) {
	case MD_MALLOC:
	case MD_VNODE:
	case MD_SWAP:
		pp->flags |= G_PF_ACCEPT_UNMAPPED;
		break;
	case MD_PRELOAD:
	case MD_NULL:
		break;
	}
	sc->gp = gp;
	sc->pp = pp;
	g_error_provider(pp, 0);
	g_topology_unlock();
	sc->devstat = devstat_new_entry("md", sc->unit, sc->sectorsize,
	    DEVSTAT_ALL_SUPPORTED, DEVSTAT_TYPE_DIRECT, DEVSTAT_PRIORITY_MAX);
}

static int
mdcreate_malloc(struct md_s *sc, struct md_req *mdr)
{
	uintptr_t sp;
	int error;
	off_t u;

	error = 0;
	if (mdr->md_options & ~(MD_AUTOUNIT | MD_COMPRESS | MD_RESERVE))
		return (EINVAL);
	if (mdr->md_sectorsize != 0 && !powerof2(mdr->md_sectorsize))
		return (EINVAL);
	/* Compression doesn't make sense if we have reserved space */
	if (mdr->md_options & MD_RESERVE)
		mdr->md_options &= ~MD_COMPRESS;
	if (mdr->md_fwsectors != 0)
		sc->fwsectors = mdr->md_fwsectors;
	if (mdr->md_fwheads != 0)
		sc->fwheads = mdr->md_fwheads;
	sc->flags = mdr->md_options & (MD_COMPRESS | MD_FORCE);
	sc->indir = dimension(sc->mediasize / sc->sectorsize);
	sc->uma = uma_zcreate(sc->name, sc->sectorsize, NULL, NULL, NULL, NULL,
	    0x1ff, 0);
	if (mdr->md_options & MD_RESERVE) {
		off_t nsectors;

		nsectors = sc->mediasize / sc->sectorsize;
		for (u = 0; u < nsectors; u++) {
			sp = (uintptr_t)uma_zalloc(sc->uma, (md_malloc_wait ?
			    M_WAITOK : M_NOWAIT) | M_ZERO);
			if (sp != 0)
				error = s_write(sc->indir, u, sp);
			else
				error = ENOMEM;
			if (error != 0)
				break;
		}
	}
	return (error);
}


static int
mdsetcred(struct md_s *sc, struct ucred *cred)
{
	char *tmpbuf;
	int error = 0;

	/*
	 * Set credits in our softc
	 */

	if (sc->cred)
		crfree(sc->cred);
	sc->cred = crhold(cred);

	/*
	 * Horrible kludge to establish credentials for NFS  XXX.
	 */

	if (sc->vnode) {
		struct uio auio;
		struct iovec aiov;

		tmpbuf = malloc(sc->sectorsize, M_TEMP, M_WAITOK);
		bzero(&auio, sizeof(auio));

		aiov.iov_base = tmpbuf;
		aiov.iov_len = sc->sectorsize;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_resid = aiov.iov_len;
		vn_lock(sc->vnode, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_READ(sc->vnode, &auio, 0, sc->cred);
		VOP_UNLOCK(sc->vnode, 0);
		free(tmpbuf, M_TEMP);
	}
	return (error);
}

static int
mdcreate_vnode(struct md_s *sc, struct md_req *mdr, struct thread *td)
{
	struct vattr vattr;
	struct nameidata nd;
	char *fname;
	int error, flags;

	fname = mdr->md_file;
	if (mdr->md_file_seg == UIO_USERSPACE) {
		error = copyinstr(fname, sc->file, sizeof(sc->file), NULL);
		if (error != 0)
			return (error);
	} else if (mdr->md_file_seg == UIO_SYSSPACE)
		strlcpy(sc->file, fname, sizeof(sc->file));
	else
		return (EDOOFUS);

	/*
	 * If the user specified that this is a read only device, don't
	 * set the FWRITE mask before trying to open the backing store.
	 */
	flags = FREAD | ((mdr->md_options & MD_READONLY) ? 0 : FWRITE) \
	    | ((mdr->md_options & MD_VERIFY) ? O_VERIFY : 0);
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, sc->file, td);
	error = vn_open(&nd, &flags, 0, NULL);
	if (error != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_vp->v_type != VREG) {
		error = EINVAL;
		goto bad;
	}
	error = VOP_GETATTR(nd.ni_vp, &vattr, td->td_ucred);
	if (error != 0)
		goto bad;
	if (VOP_ISLOCKED(nd.ni_vp) != LK_EXCLUSIVE) {
		vn_lock(nd.ni_vp, LK_UPGRADE | LK_RETRY);
		if (nd.ni_vp->v_iflag & VI_DOOMED) {
			/* Forced unmount. */
			error = EBADF;
			goto bad;
		}
	}
	nd.ni_vp->v_vflag |= VV_MD;
	VOP_UNLOCK(nd.ni_vp, 0);

	if (mdr->md_fwsectors != 0)
		sc->fwsectors = mdr->md_fwsectors;
	if (mdr->md_fwheads != 0)
		sc->fwheads = mdr->md_fwheads;
	snprintf(sc->ident, sizeof(sc->ident), "MD-DEV%ju-INO%ju",
	    (uintmax_t)vattr.va_fsid, (uintmax_t)vattr.va_fileid);
	sc->flags = mdr->md_options & (MD_ASYNC | MD_CACHE | MD_FORCE |
	    MD_VERIFY);
	if (!(flags & FWRITE))
		sc->flags |= MD_READONLY;
	sc->vnode = nd.ni_vp;

	error = mdsetcred(sc, td->td_ucred);
	if (error != 0) {
		sc->vnode = NULL;
		vn_lock(nd.ni_vp, LK_EXCLUSIVE | LK_RETRY);
		nd.ni_vp->v_vflag &= ~VV_MD;
		goto bad;
	}
	return (0);
bad:
	VOP_UNLOCK(nd.ni_vp, 0);
	(void)vn_close(nd.ni_vp, flags, td->td_ucred, td);
	return (error);
}

static int
mddestroy(struct md_s *sc, struct thread *td)
{

	if (sc->gp) {
		sc->gp->softc = NULL;
		g_topology_lock();
		g_wither_geom(sc->gp, ENXIO);
		g_topology_unlock();
		sc->gp = NULL;
		sc->pp = NULL;
	}
	if (sc->devstat) {
		devstat_remove_entry(sc->devstat);
		sc->devstat = NULL;
	}
	mtx_lock(&sc->queue_mtx);
	sc->flags |= MD_SHUTDOWN;
	wakeup(sc);
	while (!(sc->flags & MD_EXITING))
		msleep(sc->procp, &sc->queue_mtx, PRIBIO, "mddestroy", hz / 10);
	mtx_unlock(&sc->queue_mtx);
	mtx_destroy(&sc->stat_mtx);
	mtx_destroy(&sc->queue_mtx);
	if (sc->vnode != NULL) {
		vn_lock(sc->vnode, LK_EXCLUSIVE | LK_RETRY);
		sc->vnode->v_vflag &= ~VV_MD;
		VOP_UNLOCK(sc->vnode, 0);
		(void)vn_close(sc->vnode, sc->flags & MD_READONLY ?
		    FREAD : (FREAD|FWRITE), sc->cred, td);
	}
	if (sc->cred != NULL)
		crfree(sc->cred);
	if (sc->object != NULL)
		vm_object_deallocate(sc->object);
	if (sc->indir)
		destroy_indir(sc, sc->indir);
	if (sc->uma)
		uma_zdestroy(sc->uma);

	LIST_REMOVE(sc, list);
	free_unr(md_uh, sc->unit);
	free(sc, M_MD);
	return (0);
}

static int
mdresize(struct md_s *sc, struct md_req *mdr)
{
	int error, res;
	vm_pindex_t oldpages, newpages;

	switch (sc->type) {
	case MD_VNODE:
	case MD_NULL:
		break;
	case MD_SWAP:
		if (mdr->md_mediasize <= 0 ||
		    (mdr->md_mediasize % PAGE_SIZE) != 0)
			return (EDOM);
		oldpages = OFF_TO_IDX(round_page(sc->mediasize));
		newpages = OFF_TO_IDX(round_page(mdr->md_mediasize));
		if (newpages < oldpages) {
			VM_OBJECT_WLOCK(sc->object);
			vm_object_page_remove(sc->object, newpages, 0, 0);
			swap_pager_freespace(sc->object, newpages,
			    oldpages - newpages);
			swap_release_by_cred(IDX_TO_OFF(oldpages -
			    newpages), sc->cred);
			sc->object->charge = IDX_TO_OFF(newpages);
			sc->object->size = newpages;
			VM_OBJECT_WUNLOCK(sc->object);
		} else if (newpages > oldpages) {
			res = swap_reserve_by_cred(IDX_TO_OFF(newpages -
			    oldpages), sc->cred);
			if (!res)
				return (ENOMEM);
			if ((mdr->md_options & MD_RESERVE) ||
			    (sc->flags & MD_RESERVE)) {
				error = swap_pager_reserve(sc->object,
				    oldpages, newpages - oldpages);
				if (error < 0) {
					swap_release_by_cred(
					    IDX_TO_OFF(newpages - oldpages),
					    sc->cred);
					return (EDOM);
				}
			}
			VM_OBJECT_WLOCK(sc->object);
			sc->object->charge = IDX_TO_OFF(newpages);
			sc->object->size = newpages;
			VM_OBJECT_WUNLOCK(sc->object);
		}
		break;
	default:
		return (EOPNOTSUPP);
	}

	sc->mediasize = mdr->md_mediasize;
	g_topology_lock();
	g_resize_provider(sc->pp, sc->mediasize);
	g_topology_unlock();
	return (0);
}

static int
mdcreate_swap(struct md_s *sc, struct md_req *mdr, struct thread *td)
{
	vm_ooffset_t npage;
	int error;

	/*
	 * Range check.  Disallow negative sizes and sizes not being
	 * multiple of page size.
	 */
	if (sc->mediasize <= 0 || (sc->mediasize % PAGE_SIZE) != 0)
		return (EDOM);

	/*
	 * Allocate an OBJT_SWAP object.
	 *
	 * Note the truncation.
	 */

	if ((mdr->md_options & MD_VERIFY) != 0)
		return (EINVAL);
	npage = mdr->md_mediasize / PAGE_SIZE;
	if (mdr->md_fwsectors != 0)
		sc->fwsectors = mdr->md_fwsectors;
	if (mdr->md_fwheads != 0)
		sc->fwheads = mdr->md_fwheads;
	sc->object = vm_pager_allocate(OBJT_SWAP, NULL, PAGE_SIZE * npage,
	    VM_PROT_DEFAULT, 0, td->td_ucred);
	if (sc->object == NULL)
		return (ENOMEM);
	sc->flags = mdr->md_options & (MD_FORCE | MD_RESERVE);
	if (mdr->md_options & MD_RESERVE) {
		if (swap_pager_reserve(sc->object, 0, npage) < 0) {
			error = EDOM;
			goto finish;
		}
	}
	error = mdsetcred(sc, td->td_ucred);
 finish:
	if (error != 0) {
		vm_object_deallocate(sc->object);
		sc->object = NULL;
	}
	return (error);
}

static int
mdcreate_null(struct md_s *sc, struct md_req *mdr, struct thread *td)
{

	/*
	 * Range check.  Disallow negative sizes and sizes not being
	 * multiple of page size.
	 */
	if (sc->mediasize <= 0 || (sc->mediasize % PAGE_SIZE) != 0)
		return (EDOM);

	return (0);
}

static int
kern_mdattach_locked(struct thread *td, struct md_req *mdr)
{
	struct md_s *sc;
	unsigned sectsize;
	int error, i;

	sx_assert(&md_sx, SA_XLOCKED);

	switch (mdr->md_type) {
	case MD_MALLOC:
	case MD_PRELOAD:
	case MD_VNODE:
	case MD_SWAP:
	case MD_NULL:
		break;
	default:
		return (EINVAL);
	}
	if (mdr->md_sectorsize == 0)
		sectsize = DEV_BSIZE;
	else
		sectsize = mdr->md_sectorsize;
	if (sectsize > MAXPHYS || mdr->md_mediasize < sectsize)
		return (EINVAL);
	if (mdr->md_options & MD_AUTOUNIT)
		sc = mdnew(-1, &error, mdr->md_type);
	else {
		if (mdr->md_unit > INT_MAX)
			return (EINVAL);
		sc = mdnew(mdr->md_unit, &error, mdr->md_type);
	}
	if (sc == NULL)
		return (error);
	if (mdr->md_label != NULL)
		error = copyinstr(mdr->md_label, sc->label,
		    sizeof(sc->label), NULL);
	if (error != 0)
		goto err_after_new;
	if (mdr->md_options & MD_AUTOUNIT)
		mdr->md_unit = sc->unit;
	sc->mediasize = mdr->md_mediasize;
	sc->sectorsize = sectsize;
	error = EDOOFUS;
	switch (sc->type) {
	case MD_MALLOC:
		sc->start = mdstart_malloc;
		error = mdcreate_malloc(sc, mdr);
		break;
	case MD_PRELOAD:
		/*
		 * We disallow attaching preloaded memory disks via
		 * ioctl. Preloaded memory disks are automatically
		 * attached in g_md_init().
		 */
		error = EOPNOTSUPP;
		break;
	case MD_VNODE:
		sc->start = mdstart_vnode;
		error = mdcreate_vnode(sc, mdr, td);
		break;
	case MD_SWAP:
		sc->start = mdstart_swap;
		error = mdcreate_swap(sc, mdr, td);
		break;
	case MD_NULL:
		sc->start = mdstart_null;
		error = mdcreate_null(sc, mdr, td);
		break;
	}
err_after_new:
	if (error != 0) {
		mddestroy(sc, td);
		return (error);
	}

	/* Prune off any residual fractional sector */
	i = sc->mediasize % sc->sectorsize;
	sc->mediasize -= i;

	mdinit(sc);
	return (0);
}

static int
kern_mdattach(struct thread *td, struct md_req *mdr)
{
	int error;

	sx_xlock(&md_sx);
	error = kern_mdattach_locked(td, mdr);
	sx_xunlock(&md_sx);
	return (error);
}

static int
kern_mddetach_locked(struct thread *td, struct md_req *mdr)
{
	struct md_s *sc;

	sx_assert(&md_sx, SA_XLOCKED);

	if (mdr->md_mediasize != 0 ||
	    (mdr->md_options & ~MD_FORCE) != 0)
		return (EINVAL);

	sc = mdfind(mdr->md_unit);
	if (sc == NULL)
		return (ENOENT);
	if (sc->opencount != 0 && !(sc->flags & MD_FORCE) &&
	    !(mdr->md_options & MD_FORCE))
		return (EBUSY);
	return (mddestroy(sc, td));
}

static int
kern_mddetach(struct thread *td, struct md_req *mdr)
{
	int error;

	sx_xlock(&md_sx);
	error = kern_mddetach_locked(td, mdr);
	sx_xunlock(&md_sx);
	return (error);
}

static int
kern_mdresize_locked(struct md_req *mdr)
{
	struct md_s *sc;

	sx_assert(&md_sx, SA_XLOCKED);

	if ((mdr->md_options & ~(MD_FORCE | MD_RESERVE)) != 0)
		return (EINVAL);

	sc = mdfind(mdr->md_unit);
	if (sc == NULL)
		return (ENOENT);
	if (mdr->md_mediasize < sc->sectorsize)
		return (EINVAL);
	if (mdr->md_mediasize < sc->mediasize &&
	    !(sc->flags & MD_FORCE) &&
	    !(mdr->md_options & MD_FORCE))
		return (EBUSY);
	return (mdresize(sc, mdr));
}

static int
kern_mdresize(struct md_req *mdr)
{
	int error;

	sx_xlock(&md_sx);
	error = kern_mdresize_locked(mdr);
	sx_xunlock(&md_sx);
	return (error);
}

static int
kern_mdquery_locked(struct md_req *mdr)
{
	struct md_s *sc;
	int error;

	sx_assert(&md_sx, SA_XLOCKED);

	sc = mdfind(mdr->md_unit);
	if (sc == NULL)
		return (ENOENT);
	mdr->md_type = sc->type;
	mdr->md_options = sc->flags;
	mdr->md_mediasize = sc->mediasize;
	mdr->md_sectorsize = sc->sectorsize;
	error = 0;
	if (mdr->md_label != NULL) {
		error = copyout(sc->label, mdr->md_label,
		    strlen(sc->label) + 1);
		if (error != 0)
			return (error);
	}
	if (sc->type == MD_VNODE ||
	    (sc->type == MD_PRELOAD && mdr->md_file != NULL))
		error = copyout(sc->file, mdr->md_file,
		    strlen(sc->file) + 1);
	return (error);
}

static int
kern_mdquery(struct md_req *mdr)
{
	int error;

	sx_xlock(&md_sx);
	error = kern_mdquery_locked(mdr);
	sx_xunlock(&md_sx);
	return (error);
}

static int
kern_mdlist_locked(struct md_req *mdr)
{
	struct md_s *sc;
	int i;

	sx_assert(&md_sx, SA_XLOCKED);

	/*
	 * Write the number of md devices to mdr->md_units[0].
	 * Write the unit number of the first (mdr->md_units_nitems - 2)
	 * units to mdr->md_units[1::(mdr->md_units - 2)] and terminate the
	 * list with -1.
	 *
	 * XXX: There is currently no mechanism to retrieve unit
	 * numbers for more than (MDNPAD - 2) units.
	 *
	 * XXX: Due to the use of LIST_INSERT_HEAD in mdnew(), the
	 * list of visible unit numbers not stable.
	 */
	i = 1;
	LIST_FOREACH(sc, &md_softc_list, list) {
		if (i < mdr->md_units_nitems - 1)
			mdr->md_units[i] = sc->unit;
		i++;
	}
	mdr->md_units[MIN(i, mdr->md_units_nitems - 1)] = -1;
	mdr->md_units[0] = i - 1;
	return (0);
}

static int
kern_mdlist(struct md_req *mdr)
{
	int error;

	sx_xlock(&md_sx);
	error = kern_mdlist_locked(mdr);
	sx_xunlock(&md_sx);
	return (error);
}

/* Copy members that are not userspace pointers. */
#define	MD_IOCTL2REQ(mdio, mdr) do {					\
	(mdr)->md_unit = (mdio)->md_unit;				\
	(mdr)->md_type = (mdio)->md_type;				\
	(mdr)->md_mediasize = (mdio)->md_mediasize;			\
	(mdr)->md_sectorsize = (mdio)->md_sectorsize;			\
	(mdr)->md_options = (mdio)->md_options;				\
	(mdr)->md_fwheads = (mdio)->md_fwheads;				\
	(mdr)->md_fwsectors = (mdio)->md_fwsectors;			\
	(mdr)->md_units = &(mdio)->md_pad[0];				\
	(mdr)->md_units_nitems = nitems((mdio)->md_pad);		\
} while(0)

/* Copy members that might have been updated */
#define MD_REQ2IOCTL(mdr, mdio) do {					\
	(mdio)->md_unit = (mdr)->md_unit;				\
	(mdio)->md_type = (mdr)->md_type;				\
	(mdio)->md_mediasize = (mdr)->md_mediasize;			\
	(mdio)->md_sectorsize = (mdr)->md_sectorsize;			\
	(mdio)->md_options = (mdr)->md_options;				\
	(mdio)->md_fwheads = (mdr)->md_fwheads;				\
	(mdio)->md_fwsectors = (mdr)->md_fwsectors;			\
} while(0)

static int
mdctlioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags,
    struct thread *td)
{
	struct md_req mdr;
	int error;

	if (md_debug)
		printf("mdctlioctl(%s %lx %p %x %p)\n",
			devtoname(dev), cmd, addr, flags, td);

	bzero(&mdr, sizeof(mdr));
	switch (cmd) {
	case MDIOCATTACH:
	case MDIOCDETACH:
	case MDIOCRESIZE:
	case MDIOCQUERY:
	case MDIOCLIST: {
		struct md_ioctl *mdio = (struct md_ioctl *)addr;
		if (mdio->md_version != MDIOVERSION)
			return (EINVAL);
		MD_IOCTL2REQ(mdio, &mdr);
		mdr.md_file = mdio->md_file;
		mdr.md_file_seg = UIO_USERSPACE;
		/* If the file is adjacent to the md_ioctl it's in kernel. */
		if ((void *)mdio->md_file == (void *)(mdio + 1))
			mdr.md_file_seg = UIO_SYSSPACE;
		mdr.md_label = mdio->md_label;
		break;
	}
#ifdef COMPAT_FREEBSD32
	case MDIOCATTACH_32:
	case MDIOCDETACH_32:
	case MDIOCRESIZE_32:
	case MDIOCQUERY_32:
	case MDIOCLIST_32: {
		struct md_ioctl32 *mdio = (struct md_ioctl32 *)addr;
		if (mdio->md_version != MDIOVERSION)
			return (EINVAL);
		MD_IOCTL2REQ(mdio, &mdr);
		mdr.md_file = (void *)(uintptr_t)mdio->md_file;
		mdr.md_file_seg = UIO_USERSPACE;
		mdr.md_label = (void *)(uintptr_t)mdio->md_label;
		break;
	}
#endif
	default:
		/* Fall through to handler switch. */
		break;
	}

	error = 0;
	switch (cmd) {
	case MDIOCATTACH:
#ifdef COMPAT_FREEBSD32
	case MDIOCATTACH_32:
#endif
		error = kern_mdattach(td, &mdr);
		break;
	case MDIOCDETACH:
#ifdef COMPAT_FREEBSD32
	case MDIOCDETACH_32:
#endif
		error = kern_mddetach(td, &mdr);
		break;
	case MDIOCRESIZE:
#ifdef COMPAT_FREEBSD32
	case MDIOCRESIZE_32:
#endif
		error = kern_mdresize(&mdr);
		break;
	case MDIOCQUERY:
#ifdef COMPAT_FREEBSD32
	case MDIOCQUERY_32:
#endif
		error = kern_mdquery(&mdr);
		break;
	case MDIOCLIST:
#ifdef COMPAT_FREEBSD32
	case MDIOCLIST_32:
#endif
		error = kern_mdlist(&mdr);
		break;
	default:
		error = ENOIOCTL;
	}

	switch (cmd) {
	case MDIOCATTACH:
	case MDIOCQUERY: {
		struct md_ioctl *mdio = (struct md_ioctl *)addr;
		MD_REQ2IOCTL(&mdr, mdio);
		break;
	}
#ifdef COMPAT_FREEBSD32
	case MDIOCATTACH_32:
	case MDIOCQUERY_32: {
		struct md_ioctl32 *mdio = (struct md_ioctl32 *)addr;
		MD_REQ2IOCTL(&mdr, mdio);
		break;
	}
#endif
	default:
		/* Other commands to not alter mdr. */
		break;
	}

	return (error);
}

static void
md_preloaded(u_char *image, size_t length, const char *name)
{
	struct md_s *sc;
	int error;

	sc = mdnew(-1, &error, MD_PRELOAD);
	if (sc == NULL)
		return;
	sc->mediasize = length;
	sc->sectorsize = DEV_BSIZE;
	sc->pl_ptr = image;
	sc->pl_len = length;
	sc->start = mdstart_preload;
	if (name != NULL)
		strlcpy(sc->file, name, sizeof(sc->file));
#ifdef MD_ROOT
	if (sc->unit == 0) {
#ifndef ROOTDEVNAME
		rootdevnames[0] = MD_ROOT_FSTYPE ":/dev/md0";
#endif
#ifdef MD_ROOT_READONLY
		sc->flags |= MD_READONLY;
#endif
	}
#endif
	mdinit(sc);
	if (name != NULL) {
		printf("%s%d: Preloaded image <%s> %zd bytes at %p\n",
		    MD_NAME, sc->unit, name, length, image);
	} else {
		printf("%s%d: Embedded image %zd bytes at %p\n",
		    MD_NAME, sc->unit, length, image);
	}
}

static void
g_md_init(struct g_class *mp __unused)
{
	caddr_t mod;
	u_char *ptr, *name, *type;
	unsigned len;
	int i;

	/* figure out log2(NINDIR) */
	for (i = NINDIR, nshift = -1; i; nshift++)
		i >>= 1;

	mod = NULL;
	sx_init(&md_sx, "MD config lock");
	g_topology_unlock();
	md_uh = new_unrhdr(0, INT_MAX, NULL);
#ifdef MD_ROOT
	if (mfs_root_size != 0) {
		sx_xlock(&md_sx);
#ifdef MD_ROOT_MEM
		md_preloaded(mfs_root, mfs_root_size, NULL);
#else
		md_preloaded(__DEVOLATILE(u_char *, &mfs_root), mfs_root_size,
		    NULL);
#endif
		sx_xunlock(&md_sx);
	}
#endif
	/* XXX: are preload_* static or do they need Giant ? */
	while ((mod = preload_search_next_name(mod)) != NULL) {
		name = (char *)preload_search_info(mod, MODINFO_NAME);
		if (name == NULL)
			continue;
		type = (char *)preload_search_info(mod, MODINFO_TYPE);
		if (type == NULL)
			continue;
		if (strcmp(type, "md_image") && strcmp(type, "mfs_root"))
			continue;
		ptr = preload_fetch_addr(mod);
		len = preload_fetch_size(mod);
		if (ptr != NULL && len != 0) {
			sx_xlock(&md_sx);
			md_preloaded(ptr, len, name);
			sx_xunlock(&md_sx);
		}
	}
	md_pbuf_zone = pbuf_zsecond_create("mdpbuf", nswbuf / 10);
	status_dev = make_dev(&mdctl_cdevsw, INT_MAX, UID_ROOT, GID_WHEEL,
	    0600, MDCTL_NAME);
	g_topology_lock();
}

static void
g_md_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp __unused, struct g_provider *pp)
{
	struct md_s *mp;
	char *type;

	mp = gp->softc;
	if (mp == NULL)
		return;

	switch (mp->type) {
	case MD_MALLOC:
		type = "malloc";
		break;
	case MD_PRELOAD:
		type = "preload";
		break;
	case MD_VNODE:
		type = "vnode";
		break;
	case MD_SWAP:
		type = "swap";
		break;
	case MD_NULL:
		type = "null";
		break;
	default:
		type = "unknown";
		break;
	}

	if (pp != NULL) {
		if (indent == NULL) {
			sbuf_printf(sb, " u %d", mp->unit);
			sbuf_printf(sb, " s %ju", (uintmax_t) mp->sectorsize);
			sbuf_printf(sb, " f %ju", (uintmax_t) mp->fwheads);
			sbuf_printf(sb, " fs %ju", (uintmax_t) mp->fwsectors);
			sbuf_printf(sb, " l %ju", (uintmax_t) mp->mediasize);
			sbuf_printf(sb, " t %s", type);
			if ((mp->type == MD_VNODE && mp->vnode != NULL) ||
			    (mp->type == MD_PRELOAD && mp->file[0] != '\0'))
				sbuf_printf(sb, " file %s", mp->file);
			sbuf_printf(sb, " label %s", mp->label);
		} else {
			sbuf_printf(sb, "%s<unit>%d</unit>\n", indent,
			    mp->unit);
			sbuf_printf(sb, "%s<sectorsize>%ju</sectorsize>\n",
			    indent, (uintmax_t) mp->sectorsize);
			sbuf_printf(sb, "%s<fwheads>%ju</fwheads>\n",
			    indent, (uintmax_t) mp->fwheads);
			sbuf_printf(sb, "%s<fwsectors>%ju</fwsectors>\n",
			    indent, (uintmax_t) mp->fwsectors);
			if (mp->ident[0] != '\0') {
				sbuf_printf(sb, "%s<ident>", indent);
				g_conf_printf_escaped(sb, "%s", mp->ident);
				sbuf_printf(sb, "</ident>\n");
			}
			sbuf_printf(sb, "%s<length>%ju</length>\n",
			    indent, (uintmax_t) mp->mediasize);
			sbuf_printf(sb, "%s<compression>%s</compression>\n", indent,
			    (mp->flags & MD_COMPRESS) == 0 ? "off": "on");
			sbuf_printf(sb, "%s<access>%s</access>\n", indent,
			    (mp->flags & MD_READONLY) == 0 ? "read-write":
			    "read-only");
			sbuf_printf(sb, "%s<type>%s</type>\n", indent,
			    type);
			if ((mp->type == MD_VNODE && mp->vnode != NULL) ||
			    (mp->type == MD_PRELOAD && mp->file[0] != '\0')) {
				sbuf_printf(sb, "%s<file>", indent);
				g_conf_printf_escaped(sb, "%s", mp->file);
				sbuf_printf(sb, "</file>\n");
			}
			if (mp->type == MD_VNODE)
				sbuf_printf(sb, "%s<cache>%s</cache>\n", indent,
				    (mp->flags & MD_CACHE) == 0 ? "off": "on");
			sbuf_printf(sb, "%s<label>", indent);
			g_conf_printf_escaped(sb, "%s", mp->label);
			sbuf_printf(sb, "</label>\n");
		}
	}
}

static void
g_md_fini(struct g_class *mp __unused)
{

	sx_destroy(&md_sx);
	if (status_dev != NULL)
		destroy_dev(status_dev);
	uma_zdestroy(md_pbuf_zone);
	delete_unrhdr(md_uh);
}
