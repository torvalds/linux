// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2019 HUAWEI, Inc.
 *             https://www.huawei.com/
 */
#include "internal.h"
#include <asm/unaligned.h>
#include <trace/events/erofs.h>

struct z_erofs_maprecorder {
	struct inode *inode;
	struct erofs_map_blocks *map;
	void *kaddr;

	unsigned long lcn;
	/* compression extent information gathered */
	u8  type, headtype;
	u16 clusterofs;
	u16 delta[2];
	erofs_blk_t pblk, compressedblks;
	erofs_off_t nextpackoff;
	bool partialref;
};

static int z_erofs_load_full_lcluster(struct z_erofs_maprecorder *m,
				      unsigned long lcn)
{
	struct inode *const inode = m->inode;
	struct erofs_inode *const vi = EROFS_I(inode);
	const erofs_off_t pos = Z_EROFS_FULL_INDEX_ALIGN(erofs_iloc(inode) +
			vi->inode_isize + vi->xattr_isize) +
			lcn * sizeof(struct z_erofs_lcluster_index);
	struct z_erofs_lcluster_index *di;
	unsigned int advise;

	m->kaddr = erofs_read_metabuf(&m->map->buf, inode->i_sb,
				      erofs_blknr(inode->i_sb, pos), EROFS_KMAP);
	if (IS_ERR(m->kaddr))
		return PTR_ERR(m->kaddr);

	m->nextpackoff = pos + sizeof(struct z_erofs_lcluster_index);
	m->lcn = lcn;
	di = m->kaddr + erofs_blkoff(inode->i_sb, pos);

	advise = le16_to_cpu(di->di_advise);
	m->type = advise & Z_EROFS_LI_LCLUSTER_TYPE_MASK;
	if (m->type == Z_EROFS_LCLUSTER_TYPE_NONHEAD) {
		m->clusterofs = 1 << vi->z_logical_clusterbits;
		m->delta[0] = le16_to_cpu(di->di_u.delta[0]);
		if (m->delta[0] & Z_EROFS_LI_D0_CBLKCNT) {
			if (!(vi->z_advise & (Z_EROFS_ADVISE_BIG_PCLUSTER_1 |
					Z_EROFS_ADVISE_BIG_PCLUSTER_2))) {
				DBG_BUGON(1);
				return -EFSCORRUPTED;
			}
			m->compressedblks = m->delta[0] &
				~Z_EROFS_LI_D0_CBLKCNT;
			m->delta[0] = 1;
		}
		m->delta[1] = le16_to_cpu(di->di_u.delta[1]);
	} else {
		m->partialref = !!(advise & Z_EROFS_LI_PARTIAL_REF);
		m->clusterofs = le16_to_cpu(di->di_clusterofs);
		if (m->clusterofs >= 1 << vi->z_logical_clusterbits) {
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
		m->pblk = le32_to_cpu(di->di_u.blkaddr);
	}
	return 0;
}

static unsigned int decode_compactedbits(unsigned int lobits,
					 u8 *in, unsigned int pos, u8 *type)
{
	const unsigned int v = get_unaligned_le32(in + pos / 8) >> (pos & 7);
	const unsigned int lo = v & ((1 << lobits) - 1);

	*type = (v >> lobits) & 3;
	return lo;
}

static int get_compacted_la_distance(unsigned int lobits,
				     unsigned int encodebits,
				     unsigned int vcnt, u8 *in, int i)
{
	unsigned int lo, d1 = 0;
	u8 type;

	DBG_BUGON(i >= vcnt);

	do {
		lo = decode_compactedbits(lobits, in, encodebits * i, &type);

		if (type != Z_EROFS_LCLUSTER_TYPE_NONHEAD)
			return d1;
		++d1;
	} while (++i < vcnt);

	/* vcnt - 1 (Z_EROFS_LCLUSTER_TYPE_NONHEAD) item */
	if (!(lo & Z_EROFS_LI_D0_CBLKCNT))
		d1 += lo - 1;
	return d1;
}

static int unpack_compacted_index(struct z_erofs_maprecorder *m,
				  unsigned int amortizedshift,
				  erofs_off_t pos, bool lookahead)
{
	struct erofs_inode *const vi = EROFS_I(m->inode);
	const unsigned int lclusterbits = vi->z_logical_clusterbits;
	unsigned int vcnt, base, lo, lobits, encodebits, nblk, eofs;
	int i;
	u8 *in, type;
	bool big_pcluster;

	if (1 << amortizedshift == 4 && lclusterbits <= 14)
		vcnt = 2;
	else if (1 << amortizedshift == 2 && lclusterbits <= 12)
		vcnt = 16;
	else
		return -EOPNOTSUPP;

	/* it doesn't equal to round_up(..) */
	m->nextpackoff = round_down(pos, vcnt << amortizedshift) +
			 (vcnt << amortizedshift);
	big_pcluster = vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_1;
	lobits = max(lclusterbits, ilog2(Z_EROFS_LI_D0_CBLKCNT) + 1U);
	encodebits = ((vcnt << amortizedshift) - sizeof(__le32)) * 8 / vcnt;
	eofs = erofs_blkoff(m->inode->i_sb, pos);
	base = round_down(eofs, vcnt << amortizedshift);
	in = m->kaddr + base;

	i = (eofs - base) >> amortizedshift;

	lo = decode_compactedbits(lobits, in, encodebits * i, &type);
	m->type = type;
	if (type == Z_EROFS_LCLUSTER_TYPE_NONHEAD) {
		m->clusterofs = 1 << lclusterbits;

		/* figure out lookahead_distance: delta[1] if needed */
		if (lookahead)
			m->delta[1] = get_compacted_la_distance(lobits,
						encodebits, vcnt, in, i);
		if (lo & Z_EROFS_LI_D0_CBLKCNT) {
			if (!big_pcluster) {
				DBG_BUGON(1);
				return -EFSCORRUPTED;
			}
			m->compressedblks = lo & ~Z_EROFS_LI_D0_CBLKCNT;
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
		lo = decode_compactedbits(lobits, in,
					  encodebits * (i - 1), &type);
		if (type != Z_EROFS_LCLUSTER_TYPE_NONHEAD)
			lo = 0;
		else if (lo & Z_EROFS_LI_D0_CBLKCNT)
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
			lo = decode_compactedbits(lobits, in,
						  encodebits * i, &type);
			if (type == Z_EROFS_LCLUSTER_TYPE_NONHEAD)
				i -= lo;

			if (i >= 0)
				++nblk;
		}
	} else {
		nblk = 0;
		while (i > 0) {
			--i;
			lo = decode_compactedbits(lobits, in,
						  encodebits * i, &type);
			if (type == Z_EROFS_LCLUSTER_TYPE_NONHEAD) {
				if (lo & Z_EROFS_LI_D0_CBLKCNT) {
					--i;
					nblk += lo & ~Z_EROFS_LI_D0_CBLKCNT;
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

static int z_erofs_load_compact_lcluster(struct z_erofs_maprecorder *m,
					 unsigned long lcn, bool lookahead)
{
	struct inode *const inode = m->inode;
	struct erofs_inode *const vi = EROFS_I(inode);
	const erofs_off_t ebase = sizeof(struct z_erofs_map_header) +
		ALIGN(erofs_iloc(inode) + vi->inode_isize + vi->xattr_isize, 8);
	unsigned int totalidx = erofs_iblks(inode);
	unsigned int compacted_4b_initial, compacted_2b;
	unsigned int amortizedshift;
	erofs_off_t pos;

	if (lcn >= totalidx)
		return -EINVAL;

	m->lcn = lcn;
	/* used to align to 32-byte (compacted_2b) alignment */
	compacted_4b_initial = (32 - ebase % 32) / 4;
	if (compacted_4b_initial == 32 / 4)
		compacted_4b_initial = 0;

	if ((vi->z_advise & Z_EROFS_ADVISE_COMPACTED_2B) &&
	    compacted_4b_initial < totalidx)
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
	m->kaddr = erofs_read_metabuf(&m->map->buf, inode->i_sb,
				      erofs_blknr(inode->i_sb, pos), EROFS_KMAP);
	if (IS_ERR(m->kaddr))
		return PTR_ERR(m->kaddr);
	return unpack_compacted_index(m, amortizedshift, pos, lookahead);
}

static int z_erofs_load_lcluster_from_disk(struct z_erofs_maprecorder *m,
					   unsigned int lcn, bool lookahead)
{
	switch (EROFS_I(m->inode)->datalayout) {
	case EROFS_INODE_COMPRESSED_FULL:
		return z_erofs_load_full_lcluster(m, lcn);
	case EROFS_INODE_COMPRESSED_COMPACT:
		return z_erofs_load_compact_lcluster(m, lcn, lookahead);
	default:
		return -EINVAL;
	}
}

static int z_erofs_extent_lookback(struct z_erofs_maprecorder *m,
				   unsigned int lookback_distance)
{
	struct super_block *sb = m->inode->i_sb;
	struct erofs_inode *const vi = EROFS_I(m->inode);
	const unsigned int lclusterbits = vi->z_logical_clusterbits;

	while (m->lcn >= lookback_distance) {
		unsigned long lcn = m->lcn - lookback_distance;
		int err;

		err = z_erofs_load_lcluster_from_disk(m, lcn, false);
		if (err)
			return err;

		switch (m->type) {
		case Z_EROFS_LCLUSTER_TYPE_NONHEAD:
			lookback_distance = m->delta[0];
			if (!lookback_distance)
				goto err_bogus;
			continue;
		case Z_EROFS_LCLUSTER_TYPE_PLAIN:
		case Z_EROFS_LCLUSTER_TYPE_HEAD1:
		case Z_EROFS_LCLUSTER_TYPE_HEAD2:
			m->headtype = m->type;
			m->map->m_la = (lcn << lclusterbits) | m->clusterofs;
			return 0;
		default:
			erofs_err(sb, "unknown type %u @ lcn %lu of nid %llu",
				  m->type, lcn, vi->nid);
			DBG_BUGON(1);
			return -EOPNOTSUPP;
		}
	}
err_bogus:
	erofs_err(sb, "bogus lookback distance %u @ lcn %lu of nid %llu",
		  lookback_distance, m->lcn, vi->nid);
	DBG_BUGON(1);
	return -EFSCORRUPTED;
}

static int z_erofs_get_extent_compressedlen(struct z_erofs_maprecorder *m,
					    unsigned int initial_lcn)
{
	struct super_block *sb = m->inode->i_sb;
	struct erofs_inode *const vi = EROFS_I(m->inode);
	struct erofs_map_blocks *const map = m->map;
	const unsigned int lclusterbits = vi->z_logical_clusterbits;
	unsigned long lcn;
	int err;

	DBG_BUGON(m->type != Z_EROFS_LCLUSTER_TYPE_PLAIN &&
		  m->type != Z_EROFS_LCLUSTER_TYPE_HEAD1 &&
		  m->type != Z_EROFS_LCLUSTER_TYPE_HEAD2);
	DBG_BUGON(m->type != m->headtype);

	if (m->headtype == Z_EROFS_LCLUSTER_TYPE_PLAIN ||
	    ((m->headtype == Z_EROFS_LCLUSTER_TYPE_HEAD1) &&
	     !(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_1)) ||
	    ((m->headtype == Z_EROFS_LCLUSTER_TYPE_HEAD2) &&
	     !(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_2))) {
		map->m_plen = 1ULL << lclusterbits;
		return 0;
	}
	lcn = m->lcn + 1;
	if (m->compressedblks)
		goto out;

	err = z_erofs_load_lcluster_from_disk(m, lcn, false);
	if (err)
		return err;

	/*
	 * If the 1st NONHEAD lcluster has already been handled initially w/o
	 * valid compressedblks, which means at least it mustn't be CBLKCNT, or
	 * an internal implemenatation error is detected.
	 *
	 * The following code can also handle it properly anyway, but let's
	 * BUG_ON in the debugging mode only for developers to notice that.
	 */
	DBG_BUGON(lcn == initial_lcn &&
		  m->type == Z_EROFS_LCLUSTER_TYPE_NONHEAD);

	switch (m->type) {
	case Z_EROFS_LCLUSTER_TYPE_PLAIN:
	case Z_EROFS_LCLUSTER_TYPE_HEAD1:
	case Z_EROFS_LCLUSTER_TYPE_HEAD2:
		/*
		 * if the 1st NONHEAD lcluster is actually PLAIN or HEAD type
		 * rather than CBLKCNT, it's a 1 lcluster-sized pcluster.
		 */
		m->compressedblks = 1 << (lclusterbits - sb->s_blocksize_bits);
		break;
	case Z_EROFS_LCLUSTER_TYPE_NONHEAD:
		if (m->delta[0] != 1)
			goto err_bonus_cblkcnt;
		if (m->compressedblks)
			break;
		fallthrough;
	default:
		erofs_err(sb, "cannot found CBLKCNT @ lcn %lu of nid %llu", lcn,
			  vi->nid);
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}
out:
	map->m_plen = erofs_pos(sb, m->compressedblks);
	return 0;
err_bonus_cblkcnt:
	erofs_err(sb, "bogus CBLKCNT @ lcn %lu of nid %llu", lcn, vi->nid);
	DBG_BUGON(1);
	return -EFSCORRUPTED;
}

static int z_erofs_get_extent_decompressedlen(struct z_erofs_maprecorder *m)
{
	struct inode *inode = m->inode;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_map_blocks *map = m->map;
	unsigned int lclusterbits = vi->z_logical_clusterbits;
	u64 lcn = m->lcn, headlcn = map->m_la >> lclusterbits;
	int err;

	do {
		/* handle the last EOF pcluster (no next HEAD lcluster) */
		if ((lcn << lclusterbits) >= inode->i_size) {
			map->m_llen = inode->i_size - map->m_la;
			return 0;
		}

		err = z_erofs_load_lcluster_from_disk(m, lcn, true);
		if (err)
			return err;

		if (m->type == Z_EROFS_LCLUSTER_TYPE_NONHEAD) {
			DBG_BUGON(!m->delta[1] &&
				  m->clusterofs != 1 << lclusterbits);
		} else if (m->type == Z_EROFS_LCLUSTER_TYPE_PLAIN ||
			   m->type == Z_EROFS_LCLUSTER_TYPE_HEAD1 ||
			   m->type == Z_EROFS_LCLUSTER_TYPE_HEAD2) {
			/* go on until the next HEAD lcluster */
			if (lcn != headlcn)
				break;
			m->delta[1] = 1;
		} else {
			erofs_err(inode->i_sb, "unknown type %u @ lcn %llu of nid %llu",
				  m->type, lcn, vi->nid);
			DBG_BUGON(1);
			return -EOPNOTSUPP;
		}
		lcn += m->delta[1];
	} while (m->delta[1]);

	map->m_llen = (lcn << lclusterbits) + m->clusterofs - map->m_la;
	return 0;
}

static int z_erofs_do_map_blocks(struct inode *inode,
				 struct erofs_map_blocks *map, int flags)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	bool ztailpacking = vi->z_advise & Z_EROFS_ADVISE_INLINE_PCLUSTER;
	bool fragment = vi->z_advise & Z_EROFS_ADVISE_FRAGMENT_PCLUSTER;
	struct z_erofs_maprecorder m = {
		.inode = inode,
		.map = map,
	};
	int err = 0;
	unsigned int lclusterbits, endoff, afmt;
	unsigned long initial_lcn;
	unsigned long long ofs, end;

	lclusterbits = vi->z_logical_clusterbits;
	ofs = flags & EROFS_GET_BLOCKS_FINDTAIL ? inode->i_size - 1 : map->m_la;
	initial_lcn = ofs >> lclusterbits;
	endoff = ofs & ((1 << lclusterbits) - 1);

	err = z_erofs_load_lcluster_from_disk(&m, initial_lcn, false);
	if (err)
		goto unmap_out;

	if (ztailpacking && (flags & EROFS_GET_BLOCKS_FINDTAIL))
		vi->z_idataoff = m.nextpackoff;

	map->m_flags = EROFS_MAP_MAPPED | EROFS_MAP_ENCODED;
	end = (m.lcn + 1ULL) << lclusterbits;

	switch (m.type) {
	case Z_EROFS_LCLUSTER_TYPE_PLAIN:
	case Z_EROFS_LCLUSTER_TYPE_HEAD1:
	case Z_EROFS_LCLUSTER_TYPE_HEAD2:
		if (endoff >= m.clusterofs) {
			m.headtype = m.type;
			map->m_la = (m.lcn << lclusterbits) | m.clusterofs;
			/*
			 * For ztailpacking files, in order to inline data more
			 * effectively, special EOF lclusters are now supported
			 * which can have three parts at most.
			 */
			if (ztailpacking && end > inode->i_size)
				end = inode->i_size;
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
	case Z_EROFS_LCLUSTER_TYPE_NONHEAD:
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
	if (m.partialref)
		map->m_flags |= EROFS_MAP_PARTIAL_REF;
	map->m_llen = end - map->m_la;

	if (flags & EROFS_GET_BLOCKS_FINDTAIL) {
		vi->z_tailextent_headlcn = m.lcn;
		/* for non-compact indexes, fragmentoff is 64 bits */
		if (fragment && vi->datalayout == EROFS_INODE_COMPRESSED_FULL)
			vi->z_fragmentoff |= (u64)m.pblk << 32;
	}
	if (ztailpacking && m.lcn == vi->z_tailextent_headlcn) {
		map->m_flags |= EROFS_MAP_META;
		map->m_pa = vi->z_idataoff;
		map->m_plen = vi->z_idata_size;
	} else if (fragment && m.lcn == vi->z_tailextent_headlcn) {
		map->m_flags |= EROFS_MAP_FRAGMENT;
	} else {
		map->m_pa = erofs_pos(inode->i_sb, m.pblk);
		err = z_erofs_get_extent_compressedlen(&m, initial_lcn);
		if (err)
			goto unmap_out;
	}

	if (m.headtype == Z_EROFS_LCLUSTER_TYPE_PLAIN) {
		if (map->m_llen > map->m_plen) {
			DBG_BUGON(1);
			err = -EFSCORRUPTED;
			goto unmap_out;
		}
		afmt = vi->z_advise & Z_EROFS_ADVISE_INTERLACED_PCLUSTER ?
			Z_EROFS_COMPRESSION_INTERLACED :
			Z_EROFS_COMPRESSION_SHIFTED;
	} else {
		afmt = m.headtype == Z_EROFS_LCLUSTER_TYPE_HEAD2 ?
			vi->z_algorithmtype[1] : vi->z_algorithmtype[0];
		if (!(EROFS_I_SB(inode)->available_compr_algs & (1 << afmt))) {
			erofs_err(inode->i_sb, "inconsistent algorithmtype %u for nid %llu",
				  afmt, vi->nid);
			err = -EFSCORRUPTED;
			goto unmap_out;
		}
	}
	map->m_algorithmformat = afmt;

	if ((flags & EROFS_GET_BLOCKS_FIEMAP) ||
	    ((flags & EROFS_GET_BLOCKS_READMORE) &&
	     (map->m_algorithmformat == Z_EROFS_COMPRESSION_LZMA ||
	      map->m_algorithmformat == Z_EROFS_COMPRESSION_DEFLATE ||
	      map->m_algorithmformat == Z_EROFS_COMPRESSION_ZSTD) &&
	      map->m_llen >= i_blocksize(inode))) {
		err = z_erofs_get_extent_decompressedlen(&m);
		if (!err)
			map->m_flags |= EROFS_MAP_FULL_MAPPED;
	}

unmap_out:
	erofs_unmap_metabuf(&m.map->buf);
	return err;
}

static int z_erofs_fill_inode_lazy(struct inode *inode)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct super_block *const sb = inode->i_sb;
	int err, headnr;
	erofs_off_t pos;
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
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

	pos = ALIGN(erofs_iloc(inode) + vi->inode_isize + vi->xattr_isize, 8);
	kaddr = erofs_read_metabuf(&buf, sb, erofs_blknr(sb, pos), EROFS_KMAP);
	if (IS_ERR(kaddr)) {
		err = PTR_ERR(kaddr);
		goto out_unlock;
	}

	h = kaddr + erofs_blkoff(sb, pos);
	/*
	 * if the highest bit of the 8-byte map header is set, the whole file
	 * is stored in the packed inode. The rest bits keeps z_fragmentoff.
	 */
	if (h->h_clusterbits >> Z_EROFS_FRAGMENT_INODE_BIT) {
		vi->z_advise = Z_EROFS_ADVISE_FRAGMENT_PCLUSTER;
		vi->z_fragmentoff = le64_to_cpu(*(__le64 *)h) ^ (1ULL << 63);
		vi->z_tailextent_headlcn = 0;
		goto done;
	}
	vi->z_advise = le16_to_cpu(h->h_advise);
	vi->z_algorithmtype[0] = h->h_algorithmtype & 15;
	vi->z_algorithmtype[1] = h->h_algorithmtype >> 4;

	headnr = 0;
	if (vi->z_algorithmtype[0] >= Z_EROFS_COMPRESSION_MAX ||
	    vi->z_algorithmtype[++headnr] >= Z_EROFS_COMPRESSION_MAX) {
		erofs_err(sb, "unknown HEAD%u format %u for nid %llu, please upgrade kernel",
			  headnr + 1, vi->z_algorithmtype[headnr], vi->nid);
		err = -EOPNOTSUPP;
		goto out_put_metabuf;
	}

	vi->z_logical_clusterbits = sb->s_blocksize_bits + (h->h_clusterbits & 7);
	if (!erofs_sb_has_big_pcluster(EROFS_SB(sb)) &&
	    vi->z_advise & (Z_EROFS_ADVISE_BIG_PCLUSTER_1 |
			    Z_EROFS_ADVISE_BIG_PCLUSTER_2)) {
		erofs_err(sb, "per-inode big pcluster without sb feature for nid %llu",
			  vi->nid);
		err = -EFSCORRUPTED;
		goto out_put_metabuf;
	}
	if (vi->datalayout == EROFS_INODE_COMPRESSED_COMPACT &&
	    !(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_1) ^
	    !(vi->z_advise & Z_EROFS_ADVISE_BIG_PCLUSTER_2)) {
		erofs_err(sb, "big pcluster head1/2 of compact indexes should be consistent for nid %llu",
			  vi->nid);
		err = -EFSCORRUPTED;
		goto out_put_metabuf;
	}

	if (vi->z_advise & Z_EROFS_ADVISE_INLINE_PCLUSTER) {
		struct erofs_map_blocks map = {
			.buf = __EROFS_BUF_INITIALIZER
		};

		vi->z_idata_size = le16_to_cpu(h->h_idata_size);
		err = z_erofs_do_map_blocks(inode, &map,
					    EROFS_GET_BLOCKS_FINDTAIL);
		erofs_put_metabuf(&map.buf);

		if (!map.m_plen ||
		    erofs_blkoff(sb, map.m_pa) + map.m_plen > sb->s_blocksize) {
			erofs_err(sb, "invalid tail-packing pclustersize %llu",
				  map.m_plen);
			err = -EFSCORRUPTED;
		}
		if (err < 0)
			goto out_put_metabuf;
	}

	if (vi->z_advise & Z_EROFS_ADVISE_FRAGMENT_PCLUSTER &&
	    !(h->h_clusterbits >> Z_EROFS_FRAGMENT_INODE_BIT)) {
		struct erofs_map_blocks map = {
			.buf = __EROFS_BUF_INITIALIZER
		};

		vi->z_fragmentoff = le32_to_cpu(h->h_fragmentoff);
		err = z_erofs_do_map_blocks(inode, &map,
					    EROFS_GET_BLOCKS_FINDTAIL);
		erofs_put_metabuf(&map.buf);
		if (err < 0)
			goto out_put_metabuf;
	}
done:
	/* paired with smp_mb() at the beginning of the function */
	smp_mb();
	set_bit(EROFS_I_Z_INITED_BIT, &vi->flags);
out_put_metabuf:
	erofs_put_metabuf(&buf);
out_unlock:
	clear_and_wake_up_bit(EROFS_I_BL_Z_BIT, &vi->flags);
	return err;
}

int z_erofs_map_blocks_iter(struct inode *inode, struct erofs_map_blocks *map,
			    int flags)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	int err = 0;

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

	if ((vi->z_advise & Z_EROFS_ADVISE_FRAGMENT_PCLUSTER) &&
	    !vi->z_tailextent_headlcn) {
		map->m_la = 0;
		map->m_llen = inode->i_size;
		map->m_flags = EROFS_MAP_MAPPED | EROFS_MAP_FULL_MAPPED |
				EROFS_MAP_FRAGMENT;
		goto out;
	}

	err = z_erofs_do_map_blocks(inode, map, flags);
out:
	trace_z_erofs_map_blocks_iter_exit(inode, map, flags, err);
	return err;
}

static int z_erofs_iomap_begin_report(struct inode *inode, loff_t offset,
				loff_t length, unsigned int flags,
				struct iomap *iomap, struct iomap *srcmap)
{
	int ret;
	struct erofs_map_blocks map = { .m_la = offset };

	ret = z_erofs_map_blocks_iter(inode, &map, EROFS_GET_BLOCKS_FIEMAP);
	erofs_put_metabuf(&map.buf);
	if (ret < 0)
		return ret;

	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = map.m_la;
	iomap->length = map.m_llen;
	if (map.m_flags & EROFS_MAP_MAPPED) {
		iomap->type = IOMAP_MAPPED;
		iomap->addr = map.m_flags & EROFS_MAP_FRAGMENT ?
			      IOMAP_NULL_ADDR : map.m_pa;
	} else {
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		/*
		 * No strict rule on how to describe extents for post EOF, yet
		 * we need to do like below. Otherwise, iomap itself will get
		 * into an endless loop on post EOF.
		 *
		 * Calculate the effective offset by subtracting extent start
		 * (map.m_la) from the requested offset, and add it to length.
		 * (NB: offset >= map.m_la always)
		 */
		if (iomap->offset >= inode->i_size)
			iomap->length = length + offset - map.m_la;
	}
	iomap->flags = 0;
	return 0;
}

const struct iomap_ops z_erofs_iomap_report_ops = {
	.iomap_begin = z_erofs_iomap_begin_report,
};
