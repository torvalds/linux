/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, 2002 Scott Long <scottl@freebsd.org>
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

#define UDF_HASHTBLSIZE 100

struct udf_node {
	struct vnode	*i_vnode;
	struct udf_mnt	*udfmp;
	ino_t		hash_id;
	long		diroff;
	struct file_entry *fentry;
};

struct udf_mnt {
	int			im_flags;
	struct mount		*im_mountp;
	struct g_consumer	*im_cp;
	struct bufobj		*im_bo;
	struct cdev *im_dev;
	struct vnode		*im_devvp;
	int			bsize;
	int			bshift;
	int			bmask;
	uint32_t		part_start;
	uint32_t		part_len;
	uint64_t		root_id;
	struct long_ad		root_icb;
	int			p_sectors;
	int			s_table_entries;
	struct udf_sparing_table *s_table;
	void			*im_d2l;	/* disk->local iconv handle */
#if 0
	void			*im_l2d;	/* local->disk iconv handle */
#endif
};

struct udf_dirstream {
	struct udf_node	*node;
	struct udf_mnt	*udfmp;
	struct buf	*bp;
	uint8_t		*data;
	uint8_t		*buf;
	int		fsize;
	int		off;
	int		this_off;
	int		offset;
	int		size;
	int		error;
	int		fid_fragment;
};

struct ifid {
	u_short	ifid_len;
	u_short	ifid_pad;
	int	ifid_ino;
	long	ifid_start;
};

#define	VFSTOUDFFS(mp)	((struct udf_mnt *)((mp)->mnt_data))
#define	VTON(vp)	((struct udf_node *)((vp)->v_data))

/*
 * The block layer refers to things in terms of 512 byte blocks by default.
 * btodb() is expensive, so speed things up.
 * XXX Can the block layer be forced to use a different block size?
 */
#define	RDSECTOR(devvp, sector, size, bp) \
	bread(devvp, sector << (udfmp->bshift - DEV_BSHIFT), size, NOCRED, bp)

MALLOC_DECLARE(M_UDFFENTRY);

static __inline int
udf_readdevblks(struct udf_mnt *udfmp, int sector, int size, struct buf **bp)
{
	return (RDSECTOR(udfmp->im_devvp, sector,
			 (size + udfmp->bmask) & ~udfmp->bmask, bp));
}

/*
 * Produce a suitable file number from an ICB.  The passed in ICB is expected
 * to be in little endian (meaning that it hasn't been swapped for big
 * endian machines yet).
 * XXX If the fileno resolves to 0, we might be in big trouble.
 * XXX Assumes the ICB is a long_ad.  This struct is compatible with short_ad,
 *     but not ext_ad.
 */
static __inline ino_t
udf_getid(struct long_ad *icb)
{
	return (le32toh(icb->loc.lb_num));
}

int udf_allocv(struct mount *, struct vnode **, struct thread *);
int udf_checktag(struct desc_tag *, uint16_t);
int udf_vget(struct mount *, ino_t, int, struct vnode **);

extern uma_zone_t udf_zone_trans;
extern uma_zone_t udf_zone_node;
extern uma_zone_t udf_zone_ds;

extern struct vop_vector udf_fifoops;
