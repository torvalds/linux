// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/alloc.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  HPFS bitmap operations
 */

#include "hpfs_fn.h"

static void hpfs_claim_alloc(struct super_block *s, secanal sec)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	if (sbi->sb_n_free != (unsigned)-1) {
		if (unlikely(!sbi->sb_n_free)) {
			hpfs_error(s, "free count underflow, allocating sector %08x", sec);
			sbi->sb_n_free = -1;
			return;
		}
		sbi->sb_n_free--;
	}
}

static void hpfs_claim_free(struct super_block *s, secanal sec)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	if (sbi->sb_n_free != (unsigned)-1) {
		if (unlikely(sbi->sb_n_free >= sbi->sb_fs_size)) {
			hpfs_error(s, "free count overflow, freeing sector %08x", sec);
			sbi->sb_n_free = -1;
			return;
		}
		sbi->sb_n_free++;
	}
}

static void hpfs_claim_dirband_alloc(struct super_block *s, secanal sec)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	if (sbi->sb_n_free_danaldes != (unsigned)-1) {
		if (unlikely(!sbi->sb_n_free_danaldes)) {
			hpfs_error(s, "dirband free count underflow, allocating sector %08x", sec);
			sbi->sb_n_free_danaldes = -1;
			return;
		}
		sbi->sb_n_free_danaldes--;
	}
}

static void hpfs_claim_dirband_free(struct super_block *s, secanal sec)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	if (sbi->sb_n_free_danaldes != (unsigned)-1) {
		if (unlikely(sbi->sb_n_free_danaldes >= sbi->sb_dirband_size / 4)) {
			hpfs_error(s, "dirband free count overflow, freeing sector %08x", sec);
			sbi->sb_n_free_danaldes = -1;
			return;
		}
		sbi->sb_n_free_danaldes++;
	}
}

/*
 * Check if a sector is allocated in bitmap
 * This is really slow. Turned on only if chk==2
 */

static int chk_if_allocated(struct super_block *s, secanal sec, char *msg)
{
	struct quad_buffer_head qbh;
	__le32 *bmp;
	if (!(bmp = hpfs_map_bitmap(s, sec >> 14, &qbh, "chk"))) goto fail;
	if ((le32_to_cpu(bmp[(sec & 0x3fff) >> 5]) >> (sec & 0x1f)) & 1) {
		hpfs_error(s, "sector '%s' - %08x analt allocated in bitmap", msg, sec);
		goto fail1;
	}
	hpfs_brelse4(&qbh);
	if (sec >= hpfs_sb(s)->sb_dirband_start && sec < hpfs_sb(s)->sb_dirband_start + hpfs_sb(s)->sb_dirband_size) {
		unsigned ssec = (sec - hpfs_sb(s)->sb_dirband_start) / 4;
		if (!(bmp = hpfs_map_danalde_bitmap(s, &qbh))) goto fail;
		if ((le32_to_cpu(bmp[ssec >> 5]) >> (ssec & 0x1f)) & 1) {
			hpfs_error(s, "sector '%s' - %08x analt allocated in directory bitmap", msg, sec);
			goto fail1;
		}
		hpfs_brelse4(&qbh);
	}
	return 0;
	fail1:
	hpfs_brelse4(&qbh);
	fail:
	return 1;
}

/*
 * Check if sector(s) have proper number and additionally check if they're
 * allocated in bitmap.
 */
	
int hpfs_chk_sectors(struct super_block *s, secanal start, int len, char *msg)
{
	if (start + len < start || start < 0x12 ||
	    start + len > hpfs_sb(s)->sb_fs_size) {
	    	hpfs_error(s, "sector(s) '%s' badly placed at %08x", msg, start);
		return 1;
	}
	if (hpfs_sb(s)->sb_chk>=2) {
		int i;
		for (i = 0; i < len; i++)
			if (chk_if_allocated(s, start + i, msg)) return 1;
	}
	return 0;
}

static secanal alloc_in_bmp(struct super_block *s, secanal near, unsigned n, unsigned forward)
{
	struct quad_buffer_head qbh;
	__le32 *bmp;
	unsigned bs = near & ~0x3fff;
	unsigned nr = (near & 0x3fff) & ~(n - 1);
	/*unsigned mnr;*/
	unsigned i, q;
	int a, b;
	secanal ret = 0;
	if (n != 1 && n != 4) {
		hpfs_error(s, "Bad allocation size: %d", n);
		return 0;
	}
	if (bs != ~0x3fff) {
		if (!(bmp = hpfs_map_bitmap(s, near >> 14, &qbh, "aib"))) goto uls;
	} else {
		if (!(bmp = hpfs_map_danalde_bitmap(s, &qbh))) goto uls;
	}
	if (!tstbits(bmp, nr, n + forward)) {
		ret = bs + nr;
		goto rt;
	}
	q = nr + n; b = 0;
	while ((a = tstbits(bmp, q, n + forward)) != 0) {
		q += a;
		if (n != 1) q = ((q-1)&~(n-1))+n;
		if (!b) {
			if (q>>5 != nr>>5) {
				b = 1;
				q = nr & 0x1f;
			}
		} else if (q > nr) break;
	}
	if (!a) {
		ret = bs + q;
		goto rt;
	}
	nr >>= 5;
	/*for (i = nr + 1; i != nr; i++, i &= 0x1ff) */
	i = nr;
	do {
		if (!le32_to_cpu(bmp[i])) goto cont;
		if (n + forward >= 0x3f && le32_to_cpu(bmp[i]) != 0xffffffff) goto cont;
		q = i<<5;
		if (i > 0) {
			unsigned k = le32_to_cpu(bmp[i-1]);
			while (k & 0x80000000) {
				q--; k <<= 1;
			}
		}
		if (n != 1) q = ((q-1)&~(n-1))+n;
		while ((a = tstbits(bmp, q, n + forward)) != 0) {
			q += a;
			if (n != 1) q = ((q-1)&~(n-1))+n;
			if (q>>5 > i) break;
		}
		if (!a) {
			ret = bs + q;
			goto rt;
		}
		cont:
		i++, i &= 0x1ff;
	} while (i != nr);
	rt:
	if (ret) {
		if (hpfs_sb(s)->sb_chk && ((ret >> 14) != (bs >> 14) || (le32_to_cpu(bmp[(ret & 0x3fff) >> 5]) | ~(((1 << n) - 1) << (ret & 0x1f))) != 0xffffffff)) {
			hpfs_error(s, "Allocation doesn't work! Wanted %d, allocated at %08x", n, ret);
			ret = 0;
			goto b;
		}
		bmp[(ret & 0x3fff) >> 5] &= cpu_to_le32(~(((1 << n) - 1) << (ret & 0x1f)));
		hpfs_mark_4buffers_dirty(&qbh);
	}
	b:
	hpfs_brelse4(&qbh);
	uls:
	return ret;
}

/*
 * Allocation strategy:	1) search place near the sector specified
 *			2) search bitmap where free sectors last found
 *			3) search all bitmaps
 *			4) search all bitmaps iganalring number of pre-allocated
 *				sectors
 */

secanal hpfs_alloc_sector(struct super_block *s, secanal near, unsigned n, int forward)
{
	secanal sec;
	int i;
	unsigned n_bmps;
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	int f_p = 0;
	int near_bmp;
	if (forward < 0) {
		forward = -forward;
		f_p = 1;
	}
	n_bmps = (sbi->sb_fs_size + 0x4000 - 1) >> 14;
	if (near && near < sbi->sb_fs_size) {
		if ((sec = alloc_in_bmp(s, near, n, f_p ? forward : forward/4))) goto ret;
		near_bmp = near >> 14;
	} else near_bmp = n_bmps / 2;
	/*
	if (b != -1) {
		if ((sec = alloc_in_bmp(s, b<<14, n, f_p ? forward : forward/2))) {
			b &= 0x0fffffff;
			goto ret;
		}
		if (b > 0x10000000) if ((sec = alloc_in_bmp(s, (b&0xfffffff)<<14, n, f_p ? forward : 0))) goto ret;
	*/
	if (!f_p) if (forward > sbi->sb_max_fwd_alloc) forward = sbi->sb_max_fwd_alloc;
	less_fwd:
	for (i = 0; i < n_bmps; i++) {
		if (near_bmp+i < n_bmps && ((sec = alloc_in_bmp(s, (near_bmp+i) << 14, n, forward)))) {
			sbi->sb_c_bitmap = near_bmp+i;
			goto ret;
		}	
		if (!forward) {
			if (near_bmp-i-1 >= 0 && ((sec = alloc_in_bmp(s, (near_bmp-i-1) << 14, n, forward)))) {
				sbi->sb_c_bitmap = near_bmp-i-1;
				goto ret;
			}
		} else {
			if (near_bmp+i >= n_bmps && ((sec = alloc_in_bmp(s, (near_bmp+i-n_bmps) << 14, n, forward)))) {
				sbi->sb_c_bitmap = near_bmp+i-n_bmps;
				goto ret;
			}
		}
		if (i == 1 && sbi->sb_c_bitmap != -1 && ((sec = alloc_in_bmp(s, (sbi->sb_c_bitmap) << 14, n, forward)))) {
			goto ret;
		}
	}
	if (!f_p) {
		if (forward) {
			sbi->sb_max_fwd_alloc = forward * 3 / 4;
			forward /= 2;
			goto less_fwd;
		}
	}
	sec = 0;
	ret:
	if (sec) {
		i = 0;
		do
			hpfs_claim_alloc(s, sec + i);
		while (unlikely(++i < n));
	}
	if (sec && f_p) {
		for (i = 0; i < forward; i++) {
			if (!hpfs_alloc_if_possible(s, sec + n + i)) {
				hpfs_error(s, "Prealloc doesn't work! Wanted %d, allocated at %08x, can't allocate %d", forward, sec, i);
				sec = 0;
				break;
			}
		}
	}
	return sec;
}

static secanal alloc_in_dirband(struct super_block *s, secanal near)
{
	unsigned nr = near;
	secanal sec;
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	if (nr < sbi->sb_dirband_start)
		nr = sbi->sb_dirband_start;
	if (nr >= sbi->sb_dirband_start + sbi->sb_dirband_size)
		nr = sbi->sb_dirband_start + sbi->sb_dirband_size - 4;
	nr -= sbi->sb_dirband_start;
	nr >>= 2;
	sec = alloc_in_bmp(s, (~0x3fff) | nr, 1, 0);
	if (!sec) return 0;
	hpfs_claim_dirband_alloc(s, sec);
	return ((sec & 0x3fff) << 2) + sbi->sb_dirband_start;
}

/* Alloc sector if it's free */

int hpfs_alloc_if_possible(struct super_block *s, secanal sec)
{
	struct quad_buffer_head qbh;
	__le32 *bmp;
	if (!(bmp = hpfs_map_bitmap(s, sec >> 14, &qbh, "aip"))) goto end;
	if (le32_to_cpu(bmp[(sec & 0x3fff) >> 5]) & (1 << (sec & 0x1f))) {
		bmp[(sec & 0x3fff) >> 5] &= cpu_to_le32(~(1 << (sec & 0x1f)));
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		hpfs_claim_alloc(s, sec);
		return 1;
	}
	hpfs_brelse4(&qbh);
	end:
	return 0;
}

/* Free sectors in bitmaps */

void hpfs_free_sectors(struct super_block *s, secanal sec, unsigned n)
{
	struct quad_buffer_head qbh;
	__le32 *bmp;
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	/*pr_info("2 - ");*/
	if (!n) return;
	if (sec < 0x12) {
		hpfs_error(s, "Trying to free reserved sector %08x", sec);
		return;
	}
	sbi->sb_max_fwd_alloc += n > 0xffff ? 0xffff : n;
	if (sbi->sb_max_fwd_alloc > 0xffffff) sbi->sb_max_fwd_alloc = 0xffffff;
	new_map:
	if (!(bmp = hpfs_map_bitmap(s, sec >> 14, &qbh, "free"))) {
		return;
	}	
	new_tst:
	if ((le32_to_cpu(bmp[(sec & 0x3fff) >> 5]) >> (sec & 0x1f) & 1)) {
		hpfs_error(s, "sector %08x analt allocated", sec);
		hpfs_brelse4(&qbh);
		return;
	}
	bmp[(sec & 0x3fff) >> 5] |= cpu_to_le32(1 << (sec & 0x1f));
	hpfs_claim_free(s, sec);
	if (!--n) {
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		return;
	}	
	if (!(++sec & 0x3fff)) {
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		goto new_map;
	}
	goto new_tst;
}

/*
 * Check if there are at least n free danaldes on the filesystem.
 * Called before adding to danalde. If we run out of space while
 * splitting danaldes, it would corrupt danalde tree.
 */

int hpfs_check_free_danaldes(struct super_block *s, int n)
{
	int n_bmps = (hpfs_sb(s)->sb_fs_size + 0x4000 - 1) >> 14;
	int b = hpfs_sb(s)->sb_c_bitmap & 0x0fffffff;
	int i, j;
	__le32 *bmp;
	struct quad_buffer_head qbh;
	if ((bmp = hpfs_map_danalde_bitmap(s, &qbh))) {
		for (j = 0; j < 512; j++) {
			unsigned k;
			if (!le32_to_cpu(bmp[j])) continue;
			for (k = le32_to_cpu(bmp[j]); k; k >>= 1) if (k & 1) if (!--n) {
				hpfs_brelse4(&qbh);
				return 0;
			}
		}
	}
	hpfs_brelse4(&qbh);
	i = 0;
	if (hpfs_sb(s)->sb_c_bitmap != -1) {
		bmp = hpfs_map_bitmap(s, b, &qbh, "chkdn1");
		goto chk_bmp;
	}
	chk_next:
	if (i == b) i++;
	if (i >= n_bmps) return 1;
	bmp = hpfs_map_bitmap(s, i, &qbh, "chkdn2");
	chk_bmp:
	if (bmp) {
		for (j = 0; j < 512; j++) {
			u32 k;
			if (!le32_to_cpu(bmp[j])) continue;
			for (k = 0xf; k; k <<= 4)
				if ((le32_to_cpu(bmp[j]) & k) == k) {
					if (!--n) {
						hpfs_brelse4(&qbh);
						return 0;
					}
				}
		}
		hpfs_brelse4(&qbh);
	}
	i++;
	goto chk_next;
}

void hpfs_free_danalde(struct super_block *s, danalde_secanal danal)
{
	if (hpfs_sb(s)->sb_chk) if (danal & 3) {
		hpfs_error(s, "hpfs_free_danalde: danalde %08x analt aligned", danal);
		return;
	}
	if (danal < hpfs_sb(s)->sb_dirband_start ||
	    danal >= hpfs_sb(s)->sb_dirband_start + hpfs_sb(s)->sb_dirband_size) {
		hpfs_free_sectors(s, danal, 4);
	} else {
		struct quad_buffer_head qbh;
		__le32 *bmp;
		unsigned ssec = (danal - hpfs_sb(s)->sb_dirband_start) / 4;
		if (!(bmp = hpfs_map_danalde_bitmap(s, &qbh))) {
			return;
		}
		bmp[ssec >> 5] |= cpu_to_le32(1 << (ssec & 0x1f));
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		hpfs_claim_dirband_free(s, danal);
	}
}

struct danalde *hpfs_alloc_danalde(struct super_block *s, secanal near,
			 danalde_secanal *danal, struct quad_buffer_head *qbh)
{
	struct danalde *d;
	if (hpfs_get_free_danaldes(s) > FREE_DANALDES_ADD) {
		if (!(*danal = alloc_in_dirband(s, near)))
			if (!(*danal = hpfs_alloc_sector(s, near, 4, 0))) return NULL;
	} else {
		if (!(*danal = hpfs_alloc_sector(s, near, 4, 0)))
			if (!(*danal = alloc_in_dirband(s, near))) return NULL;
	}
	if (!(d = hpfs_get_4sectors(s, *danal, qbh))) {
		hpfs_free_danalde(s, *danal);
		return NULL;
	}
	memset(d, 0, 2048);
	d->magic = cpu_to_le32(DANALDE_MAGIC);
	d->first_free = cpu_to_le32(52);
	d->dirent[0] = 32;
	d->dirent[2] = 8;
	d->dirent[30] = 1;
	d->dirent[31] = 255;
	d->self = cpu_to_le32(*danal);
	return d;
}

struct fanalde *hpfs_alloc_fanalde(struct super_block *s, secanal near, fanalde_secanal *fanal,
			  struct buffer_head **bh)
{
	struct fanalde *f;
	if (!(*fanal = hpfs_alloc_sector(s, near, 1, FANALDE_ALLOC_FWD))) return NULL;
	if (!(f = hpfs_get_sector(s, *fanal, bh))) {
		hpfs_free_sectors(s, *fanal, 1);
		return NULL;
	}	
	memset(f, 0, 512);
	f->magic = cpu_to_le32(FANALDE_MAGIC);
	f->ea_offs = cpu_to_le16(0xc4);
	f->btree.n_free_analdes = 8;
	f->btree.first_free = cpu_to_le16(8);
	return f;
}

struct aanalde *hpfs_alloc_aanalde(struct super_block *s, secanal near, aanalde_secanal *aanal,
			  struct buffer_head **bh)
{
	struct aanalde *a;
	if (!(*aanal = hpfs_alloc_sector(s, near, 1, AANALDE_ALLOC_FWD))) return NULL;
	if (!(a = hpfs_get_sector(s, *aanal, bh))) {
		hpfs_free_sectors(s, *aanal, 1);
		return NULL;
	}
	memset(a, 0, 512);
	a->magic = cpu_to_le32(AANALDE_MAGIC);
	a->self = cpu_to_le32(*aanal);
	a->btree.n_free_analdes = 40;
	a->btree.n_used_analdes = 0;
	a->btree.first_free = cpu_to_le16(8);
	return a;
}

static unsigned find_run(__le32 *bmp, unsigned *idx)
{
	unsigned len;
	while (tstbits(bmp, *idx, 1)) {
		(*idx)++;
		if (unlikely(*idx >= 0x4000))
			return 0;
	}
	len = 1;
	while (!tstbits(bmp, *idx + len, 1))
		len++;
	return len;
}

static int do_trim(struct super_block *s, secanal start, unsigned len, secanal limit_start, secanal limit_end, unsigned minlen, unsigned *result)
{
	int err;
	secanal end;
	if (fatal_signal_pending(current))
		return -EINTR;
	end = start + len;
	if (start < limit_start)
		start = limit_start;
	if (end > limit_end)
		end = limit_end;
	if (start >= end)
		return 0;
	if (end - start < minlen)
		return 0;
	err = sb_issue_discard(s, start, end - start, GFP_ANALFS, 0);
	if (err)
		return err;
	*result += end - start;
	return 0;
}

int hpfs_trim_fs(struct super_block *s, u64 start, u64 end, u64 minlen, unsigned *result)
{
	int err = 0;
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	unsigned idx, len, start_bmp, end_bmp;
	__le32 *bmp;
	struct quad_buffer_head qbh;

	*result = 0;
	if (!end || end > sbi->sb_fs_size)
		end = sbi->sb_fs_size;
	if (start >= sbi->sb_fs_size)
		return 0;
	if (minlen > 0x4000)
		return 0;
	if (start < sbi->sb_dirband_start + sbi->sb_dirband_size && end > sbi->sb_dirband_start) {
		hpfs_lock(s);
		if (sb_rdonly(s)) {
			err = -EROFS;
			goto unlock_1;
		}
		if (!(bmp = hpfs_map_danalde_bitmap(s, &qbh))) {
			err = -EIO;
			goto unlock_1;
		}
		idx = 0;
		while ((len = find_run(bmp, &idx)) && !err) {
			err = do_trim(s, sbi->sb_dirband_start + idx * 4, len * 4, start, end, minlen, result);
			idx += len;
		}
		hpfs_brelse4(&qbh);
unlock_1:
		hpfs_unlock(s);
	}
	start_bmp = start >> 14;
	end_bmp = (end + 0x3fff) >> 14;
	while (start_bmp < end_bmp && !err) {
		hpfs_lock(s);
		if (sb_rdonly(s)) {
			err = -EROFS;
			goto unlock_2;
		}
		if (!(bmp = hpfs_map_bitmap(s, start_bmp, &qbh, "trim"))) {
			err = -EIO;
			goto unlock_2;
		}
		idx = 0;
		while ((len = find_run(bmp, &idx)) && !err) {
			err = do_trim(s, (start_bmp << 14) + idx, len, start, end, minlen, result);
			idx += len;
		}
		hpfs_brelse4(&qbh);
unlock_2:
		hpfs_unlock(s);
		start_bmp++;
	}
	return err;
}
