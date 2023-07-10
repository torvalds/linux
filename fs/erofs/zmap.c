// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2019 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#include "internal.h"
#include <asm/unaligned.h>
#include <trace/events/erofs.h>

int z_erofs_fill_inode(struct inode *inode)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);

	if (!erofs_sb_has_big_pcluster(sbi) &&
	    vi->datalayout == EROFS_INODE_FLAT_COMPRESSION_LEGACY) {
		vi->z_advise = 0;
		vi->z_algorithmtype[0] = 0;
		vi->z_algorithmtype[1] = 0;
		vi->z_logical_clusterbits = LOG_BLOCK_SIZE;
		set_bit(EROFS_I_Z_INITED_BIT, &vi->flags);
	}
	inode->i_mapping->a_ops = &z_erofs_aops;
	return 0;
}

static int z_erofs_fill_inode_lazy(struct inode *inode)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct super_block *const sb = inode->i_sb;
	int err;
	erofs_off_t pos;
	struct page *page;
	void *kaddr;
	struct z_erofs_map_header *h;

	if (test_bit(EROFS_I_Z_INITED_BIT, &vi->flags)) {
		/*
		 * paired with smp_mb() at the end of the function to ensure
		 * fields will only be observed after the bit is set.
		 */
		smp_mb();
		return 0;
	}

	if (wait_on_bit_lock(&vi->flags, EROFS_I_BL_Z_BIT, TASK_KILLABLE))
		return -ERESTARTSYS;

	err = 0;
	if (test_bit(EROFS_I_Z_INITED_BIT, &vi->flags))
		goto out_unlock;

	DBG_BUGON(!erofs_sb_has_big_pcluster(EROFS_SB(sb)) &&
		  vi->datalayout == EROFS_INODE_FLAT_COMPRESSION_LEGACY);

	pos = ALIGN(iloc(EROFS_SB(sb), vi->nid) + vi->inode_isize +
		    vi->xattr_isize, 8);
	page = erofs_get_meta_page(sb, erofs_blknr(pos));
	if (IS_ERR(page)) {
		err = PTR_ERR(page);
		goto out_unlock;
	}

	kaddr = kmap_atomic(page);

	h = kaddr + erofs_blkoff(pos);
	vi->z_advise = le16_to_cpu(h->h_advise);
	vi->z_algorithmtype[0] = h->h_algorithmtype & 15;
	vi->z_algorithmtype[1] = h->h_algorithmtype >> 4;

	if (vi->z_algorithmtype[0] >= Z_EROFS_COMPRESSION_MAX) {
		erofs_err(sb, "unknown compression format %u for nid %llu, please upgrade kernel",
			  vi->z_algorithmtype[0], vi->nid);
		err = -EOPNOTSUPP;
		goto unmap_done;
	}

	vi->z_logical_clusterbits = LOG_BLOCK_SIZE + (h->h_clusterbits & 7);
	if (!erofs_sb_has_big_pcluster(EROFS_SB(sb)) &&
	    vi->z_advise & (Z_EROFS_ADVISE_BIG_PCLUSTER_1 |
			    Z_EROFS_ADVISE_BIG_PCLUSTER_2)) {
		erofs_err(sb, "per-inode big pcluster without sb feature for nid %llu",
			  vi->nid);
		err = -EFSCORRUPTED;
		goto unmap_done;
	}
	if (vi->datalayout == EROFS_INODE_FLAT_COMPRESSION &&
	    !(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_1) ^
	    !(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_2)) {
		erofs_err(sb, "big pcluster head1/2 of compact indexes should be consistent for nid %llu",
			  vi->nid);
		err = -EFSCORRUPTED;
		goto unmap_done;
	}
	/* paired with smp_mb() at the beginning of the function */
	smp_mb();
	set_bit(EROFS_I_Z_INITED_BIT, &vi->flags);
unmap_done:
	kunmap_atomic(kaddr);
	unlock_page(page);
	put_page(page);
out_unlock:
	clear_and_wake_up_bit(EROFS_I_BL_Z_BIT, &vi->flags);
	return err;
}

struct z_erofs_maprecorder {
	struct inode *inode;
	struct erofs_map_blocks *map;
	void *kaddr;

	unsigned long lcn;
	/* compression extent information gathered */
	u8  type;
	u16 clusterofs;
	u16 delta[2];
	erofs_blk_t pblk, compressedlcs;
};

static int z_erofs_reload_indexes(struct z_erofs_maprecorder *m,
				  erofs_blk_t eblk)
{
	struct super_block *const sb = m->inode->i_sb;
	struct erofs_map_blocks *const map = m->map;
	struct page *mpage = map->mpage;

	if (mpage) {
		if (mpage->index == eblk) {
			if (!m->kaddr)
				m->kaddr = kmap_atomic(mpage);
			return 0;
		}

		if (m->kaddr) {
			kunmap_atomic(m->kaddr);
			m->kaddr = NULL;
		}
		put_page(mpage);
	}

	mpage = erofs_get_meta_page(sb, eblk);
	if (IS_ERR(mpage)) {
		map->mpage = NULL;
		return PTR_ERR(mpage);
	}
	m->kaddr = kmap_atomic(mpage);
	unlock_page(mpage);
	map->mpage = mpage;
	return 0;
}

static int legacy_load_cluster_from_disk(struct z_erofs_maprecorder *m,
					 unsigned long lcn)
{
	struct inode *const inode = m->inode;
	struct erofs_inode *const vi = EROFS_I(inode);
	const erofs_off_t ibase = iloc(EROFS_I_SB(inode), vi->nid);
	const erofs_off_t pos =
		Z_EROFS_VLE_LEGACY_INDEX_ALIGN(ibase + vi->inode_isize +
					       vi->xattr_isize) +
		lcn * sizeof(struct z_erofs_vle_decompressed_index);
	struct z_erofs_vle_decompressed_index *di;
	unsigned int advise, type;
	int err;

	err = z_erofs_reload_indexes(m, erofs_blknr(pos));
	if (err)
		return err;

	m->lcn = lcn;
	di = m->kaddr + erofs_blkoff(pos);

	advise = le16_to_cpu(di->di_advise);
	type = (advise >> Z_EROFS_VLE_DI_CLUSTER_TYPE_BIT) &
		((1 << Z_EROFS_VLE_DI_CLUSTER_TYPE_BITS) - 1);
	switch (type) {
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		m->clusterofs = 1 << vi->z_logical_clusterbits;
		m->delta[0] = le16_to_cpu(di->di_u.delta[0]);
		if (m->delta[0] & Z_EROFS_VLE_DI_D0_CBLKCNT) {
			if (!(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_1)) {
				DBG_BUGON(1);
				return -EFSCORRUPTED;
			}
			m->compressedlcs = m->delta[0] &
				~Z_EROFS_VLE_DI_D0_CBLKCNT;
			m->delta[0] = 1;
		}
		m->delta[1] = le16_to_cpu(di->di_u.delta[1]);
		break;
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		m->clusterofs = le16_to_cpu(di->di_clusterofs);
		if (m->clusterofs >= 1 << vi->z_logical_clusterbits) {
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
		m->pblk = le32_to_cpu(di->di_u.blkaddr);
		break;
	default:
		DBG_BUGON(1);
		return -EOPNOTSUPP;
	}
	m->type = type;
	return 0;
}

static unsigned int decode_compactedbits(unsigned int lobits,
					 unsigned int lomask,
					 u8 *in, unsigned int pos, u8 *type)
{
	const unsigned int v = get_unaligned_le32(in + pos / 8) >> (pos & 7);
	const unsigned int lo = v & lomask;

	*type = (v >> lobits) & 3;
	return lo;
}

static int unpack_compacted_index(struct z_erofs_maprecorder *m,
				  unsigned int amortizedshift,
				  unsigned int eofs)
{
	struct erofs_inode *const vi = EROFS_I(m->inode);
	const unsigned int lclusterbits = vi->z_logical_clusterbits;
	const unsigned int lomask = (1 << lclusterbits) - 1;
	unsigned int vcnt, base, lo, encodebits, nblk;
	int i;
	u8 *in, type;
	bool big_pcluster;

	if (1 << amortizedshift == 4)
		vcnt = 2;
	else if (1 << amortizedshift == 2 && lclusterbits == 12)
		vcnt = 16;
	else
		return -EOPNOTSUPP;

	big_pcluster = vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_1;
	encodebits = ((vcnt << amortizedshift) - sizeof(__le32)) * 8 / vcnt;
	base = round_down(eofs, vcnt << amortizedshift);
	in = m->kaddr + base;

	i = (eofs - base) >> amortizedshift;

	lo = decode_compactedbits(lclusterbits, lomask,
				  in, encodebits * i, &type);
	m->type = type;
	if (type == Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD) {
		m->clusterofs = 1 << lclusterbits;
		if (lo & Z_EROFS_VLE_DI_D0_CBLKCNT) {
			if (!big_pcluster) {
				DBG_BUGON(1);
				return -EFSCORRUPTED;
			}
			m->compressedlcs = lo & ~Z_EROFS_VLE_DI_D0_CBLKCNT;
			m->delta[0] = 1;
			return 0;
		} else if (i + 1 != (int)vcnt) {
			m->delta[0] = lo;
			return 0;
		}
		/*
		 * since the last lcluster in the pack is special,
		 * of which lo saves delta[1] rather than delta[0].
		 * Hence, get delta[0] by the previous lcluster indirectly.
		 */
		lo = decode_compactedbits(lclusterbits, lomask,
					  in, encodebits * (i - 1), &type);
		if (type != Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD)
			lo = 0;
		else if (lo & Z_EROFS_VLE_DI_D0_CBLKCNT)
			lo = 1;
		m->delta[0] = lo + 1;
		return 0;
	}
	m->clusterofs = lo;
	m->delta[0] = 0;
	/* figout out blkaddr (pblk) for HEAD lclusters */
	if (!big_pcluster) {
		nblk = 1;
		while (i > 0) {
			--i;
			lo = decode_compactedbits(lclusterbits, lomask,
						  in, encodebits * i, &type);
			if (type == Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD)
				i -= lo;

			if (i >= 0)
				++nblk;
		}
	} else {
		nblk = 0;
		while (i > 0) {
			--i;
			lo = decode_compactedbits(lclusterbits, lomask,
						  in, encodebits * i, &type);
			if (type == Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD) {
				if (lo & Z_EROFS_VLE_DI_D0_CBLKCNT) {
					--i;
					nblk += lo & ~Z_EROFS_VLE_DI_D0_CBLKCNT;
					continue;
				}
				/* bigpcluster shouldn't have plain d0 == 1 */
				if (lo <= 1) {
					DBG_BUGON(1);
					return -EFSCORRUPTED;
				}
				i -= lo - 2;
				continue;
			}
			++nblk;
		}
	}
	in += (vcnt << amortizedshift) - sizeof(__le32);
	m->pblk = le32_to_cpu(*(__le32 *)in) + nblk;
	return 0;
}

static int compacted_load_cluster_from_disk(struct z_erofs_maprecorder *m,
					    unsigned long lcn)
{
	struct inode *const inode = m->inode;
	struct erofs_inode *const vi = EROFS_I(inode);
	const unsigned int lclusterbits = vi->z_logical_clusterbits;
	const erofs_off_t ebase = ALIGN(iloc(EROFS_I_SB(inode), vi->nid) +
					vi->inode_isize + vi->xattr_isize, 8) +
		sizeof(struct z_erofs_map_header);
	const unsigned int totalidx = DIV_ROUND_UP(inode->i_size, EROFS_BLKSIZ);
	unsigned int compacted_4b_initial, compacted_2b;
	unsigned int amortizedshift;
	erofs_off_t pos;
	int err;

	if (lclusterbits != 12)
		return -EOPNOTSUPP;

	if (lcn >= totalidx)
		return -EINVAL;

	m->lcn = lcn;
	/* used to align to 32-byte (compacted_2b) alignment */
	compacted_4b_initial = (32 - ebase % 32) / 4;
	if (compacted_4b_initial == 32 / 4)
		compacted_4b_initial = 0;

	if (vi->z_advise & Z_EROFS_ADVISE_COMPACTED_2B)
		compacted_2b = rounddown(totalidx - compacted_4b_initial, 16);
	else
		compacted_2b = 0;

	pos = ebase;
	if (lcn < compacted_4b_initial) {
		amortizedshift = 2;
		goto out;
	}
	pos += compacted_4b_initial * 4;
	lcn -= compacted_4b_initial;

	if (lcn < compacted_2b) {
		amortizedshift = 1;
		goto out;
	}
	pos += compacted_2b * 2;
	lcn -= compacted_2b;
	amortizedshift = 2;
out:
	pos += lcn * (1 << amortizedshift);
	err = z_erofs_reload_indexes(m, erofs_blknr(pos));
	if (err)
		return err;
	return unpack_compacted_index(m, amortizedshift, erofs_blkoff(pos));
}

static int z_erofs_load_cluster_from_disk(struct z_erofs_maprecorder *m,
					  unsigned int lcn)
{
	const unsigned int datamode = EROFS_I(m->inode)->datalayout;

	if (datamode == EROFS_INODE_FLAT_COMPRESSION_LEGACY)
		return legacy_load_cluster_from_disk(m, lcn);

	if (datamode == EROFS_INODE_FLAT_COMPRESSION)
		return compacted_load_cluster_from_disk(m, lcn);

	return -EINVAL;
}

static int z_erofs_extent_lookback(struct z_erofs_maprecorder *m,
				   unsigned int lookback_distance)
{
	struct erofs_inode *const vi = EROFS_I(m->inode);
	struct erofs_map_blocks *const map = m->map;
	const unsigned int lclusterbits = vi->z_logical_clusterbits;
	unsigned long lcn = m->lcn;
	int err;

	if (lcn < lookback_distance) {
		erofs_err(m->inode->i_sb,
			  "bogus lookback distance @ nid %llu", vi->nid);
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}

	/* load extent head logical cluster if needed */
	lcn -= lookback_distance;
	err = z_erofs_load_cluster_from_disk(m, lcn);
	if (err)
		return err;

	switch (m->type) {
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		if (!m->delta[0]) {
			erofs_err(m->inode->i_sb,
				  "invalid lookback distance 0 @ nid %llu",
				  vi->nid);
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
		return z_erofs_extent_lookback(m, m->delta[0]);
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
		map->m_flags &= ~EROFS_MAP_ZIPPED;
		fallthrough;
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		map->m_la = (lcn << lclusterbits) | m->clusterofs;
		break;
	default:
		erofs_err(m->inode->i_sb,
			  "unknown type %u @ lcn %lu of nid %llu",
			  m->type, lcn, vi->nid);
		DBG_BUGON(1);
		return -EOPNOTSUPP;
	}
	return 0;
}

static int z_erofs_get_extent_compressedlen(struct z_erofs_maprecorder *m,
					    unsigned int initial_lcn)
{
	struct erofs_inode *const vi = EROFS_I(m->inode);
	struct erofs_map_blocks *const map = m->map;
	const unsigned int lclusterbits = vi->z_logical_clusterbits;
	unsigned long lcn;
	int err;

	DBG_BUGON(m->type != Z_EROFS_VLE_CLUSTER_TYPE_PLAIN &&
		  m->type != Z_EROFS_VLE_CLUSTER_TYPE_HEAD);
	if (!(map->m_flags & EROFS_MAP_ZIPPED) ||
	    !(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_1)) {
		map->m_plen = 1 << lclusterbits;
		return 0;
	}

	lcn = m->lcn + 1;
	if (m->compressedlcs)
		goto out;

	err = z_erofs_load_cluster_from_disk(m, lcn);
	if (err)
		return err;

	/*
	 * If the 1st NONHEAD lcluster has already been handled initially w/o
	 * valid compressedlcs, which means at least it mustn't be CBLKCNT, or
	 * an internal implemenatation error is detected.
	 *
	 * The following code can also handle it properly anyway, but let's
	 * BUG_ON in the debugging mode only for developers to notice that.
	 */
	DBG_BUGON(lcn == initial_lcn &&
		  m->type == Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD);

	switch (m->type) {
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		/*
		 * if the 1st NONHEAD lcluster is actually PLAIN or HEAD type
		 * rather than CBLKCNT, it's a 1 lcluster-sized pcluster.
		 */
		m->compressedlcs = 1;
		break;
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		if (m->delta[0] != 1)
			goto err_bonus_cblkcnt;
		if (m->compressedlcs)
			break;
		fallthrough;
	default:
		erofs_err(m->inode->i_sb,
			  "cannot found CBLKCNT @ lcn %lu of nid %llu",
			  lcn, vi->nid);
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}
out:
	map->m_plen = m->compressedlcs << lclusterbits;
	return 0;
err_bonus_cblkcnt:
	erofs_err(m->inode->i_sb,
		  "bogus CBLKCNT @ lcn %lu of nid %llu",
		  lcn, vi->nid);
	DBG_BUGON(1);
	return -EFSCORRUPTED;
}

int z_erofs_map_blocks_iter(struct inode *inode,
			    struct erofs_map_blocks *map,
			    int flags)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct z_erofs_maprecorder m = {
		.inode = inode,
		.map = map,
	};
	int err = 0;
	unsigned int lclusterbits, endoff;
	unsigned long initial_lcn;
	unsigned long long ofs, end;

	trace_z_erofs_map_blocks_iter_enter(inode, map, flags);

	/* when trying to read beyond EOF, leave it unmapped */
	if (map->m_la >= inode->i_size) {
		map->m_llen = map->m_la + 1 - inode->i_size;
		map->m_la = inode->i_size;
		map->m_flags = 0;
		goto out;
	}

	err = z_erofs_fill_inode_lazy(inode);
	if (err)
		goto out;

	lclusterbits = vi->z_logical_clusterbits;
	ofs = map->m_la;
	initial_lcn = ofs >> lclusterbits;
	endoff = ofs & ((1 << lclusterbits) - 1);

	err = z_erofs_load_cluster_from_disk(&m, initial_lcn);
	if (err)
		goto unmap_out;

	map->m_flags = EROFS_MAP_ZIPPED;	/* by default, compressed */
	end = (m.lcn + 1ULL) << lclusterbits;

	switch (m.type) {
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
		if (endoff >= m.clusterofs)
			map->m_flags &= ~EROFS_MAP_ZIPPED;
		fallthrough;
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		if (endoff >= m.clusterofs) {
			map->m_la = (m.lcn << lclusterbits) | m.clusterofs;
			break;
		}
		/* m.lcn should be >= 1 if endoff < m.clusterofs */
		if (!m.lcn) {
			erofs_err(inode->i_sb,
				  "invalid logical cluster 0 at nid %llu",
				  vi->nid);
			err = -EFSCORRUPTED;
			goto unmap_out;
		}
		end = (m.lcn << lclusterbits) | m.clusterofs;
		map->m_flags |= EROFS_MAP_FULL_MAPPED;
		m.delta[0] = 1;
		fallthrough;
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		/* get the corresponding first chunk */
		err = z_erofs_extent_lookback(&m, m.delta[0]);
		if (err)
			goto unmap_out;
		break;
	default:
		erofs_err(inode->i_sb,
			  "unknown type %u @ offset %llu of nid %llu",
			  m.type, ofs, vi->nid);
		err = -EOPNOTSUPP;
		goto unmap_out;
	}

	map->m_llen = end - map->m_la;
	map->m_pa = blknr_to_addr(m.pblk);
	map->m_flags |= EROFS_MAP_MAPPED;

	err = z_erofs_get_extent_compressedlen(&m, initial_lcn);
	if (err)
		goto out;
unmap_out:
	if (m.kaddr)
		kunmap_atomic(m.kaddr);

out:
	erofs_dbg("%s, m_la %llu m_pa %llu m_llen %llu m_plen %llu m_flags 0%o",
		  __func__, map->m_la, map->m_pa,
		  map->m_llen, map->m_plen, map->m_flags);

	trace_z_erofs_map_blocks_iter_exit(inode, map, flags, err);

	/* aggressively BUG_ON iff CONFIG_EROFS_FS_DEBUG is on */
	DBG_BUGON(err < 0 && err != -ENOMEM);
	return err;
}

