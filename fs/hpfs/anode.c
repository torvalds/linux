// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/ayesde.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  handling HPFS ayesde tree that contains file allocation info
 */

#include "hpfs_fn.h"

/* Find a sector in allocation tree */

secyes hpfs_bplus_lookup(struct super_block *s, struct iyesde *iyesde,
		   struct bplus_header *btree, unsigned sec,
		   struct buffer_head *bh)
{
	ayesde_secyes a = -1;
	struct ayesde *ayesde;
	int i;
	int c1, c2 = 0;
	go_down:
	if (hpfs_sb(s)->sb_chk) if (hpfs_stop_cycles(s, a, &c1, &c2, "hpfs_bplus_lookup")) return -1;
	if (bp_internal(btree)) {
		for (i = 0; i < btree->n_used_yesdes; i++)
			if (le32_to_cpu(btree->u.internal[i].file_secyes) > sec) {
				a = le32_to_cpu(btree->u.internal[i].down);
				brelse(bh);
				if (!(ayesde = hpfs_map_ayesde(s, a, &bh))) return -1;
				btree = &ayesde->btree;
				goto go_down;
			}
		hpfs_error(s, "sector %08x yest found in internal ayesde %08x", sec, a);
		brelse(bh);
		return -1;
	}
	for (i = 0; i < btree->n_used_yesdes; i++)
		if (le32_to_cpu(btree->u.external[i].file_secyes) <= sec &&
		    le32_to_cpu(btree->u.external[i].file_secyes) + le32_to_cpu(btree->u.external[i].length) > sec) {
			a = le32_to_cpu(btree->u.external[i].disk_secyes) + sec - le32_to_cpu(btree->u.external[i].file_secyes);
			if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, a, 1, "data")) {
				brelse(bh);
				return -1;
			}
			if (iyesde) {
				struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(iyesde);
				hpfs_iyesde->i_file_sec = le32_to_cpu(btree->u.external[i].file_secyes);
				hpfs_iyesde->i_disk_sec = le32_to_cpu(btree->u.external[i].disk_secyes);
				hpfs_iyesde->i_n_secs = le32_to_cpu(btree->u.external[i].length);
			}
			brelse(bh);
			return a;
		}
	hpfs_error(s, "sector %08x yest found in external ayesde %08x", sec, a);
	brelse(bh);
	return -1;
}

/* Add a sector to tree */

secyes hpfs_add_sector_to_btree(struct super_block *s, secyes yesde, int fyesd, unsigned fsecyes)
{
	struct bplus_header *btree;
	struct ayesde *ayesde = NULL, *rayesde = NULL;
	struct fyesde *fyesde;
	ayesde_secyes a, na = -1, ra, up = -1;
	secyes se;
	struct buffer_head *bh, *bh1, *bh2;
	int n;
	unsigned fs;
	int c1, c2 = 0;
	if (fyesd) {
		if (!(fyesde = hpfs_map_fyesde(s, yesde, &bh))) return -1;
		btree = &fyesde->btree;
	} else {
		if (!(ayesde = hpfs_map_ayesde(s, yesde, &bh))) return -1;
		btree = &ayesde->btree;
	}
	a = yesde;
	go_down:
	if ((n = btree->n_used_yesdes - 1) < -!!fyesd) {
		hpfs_error(s, "ayesde %08x has yes entries", a);
		brelse(bh);
		return -1;
	}
	if (bp_internal(btree)) {
		a = le32_to_cpu(btree->u.internal[n].down);
		btree->u.internal[n].file_secyes = cpu_to_le32(-1);
		mark_buffer_dirty(bh);
		brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, a, &c1, &c2, "hpfs_add_sector_to_btree #1")) return -1;
		if (!(ayesde = hpfs_map_ayesde(s, a, &bh))) return -1;
		btree = &ayesde->btree;
		goto go_down;
	}
	if (n >= 0) {
		if (le32_to_cpu(btree->u.external[n].file_secyes) + le32_to_cpu(btree->u.external[n].length) != fsecyes) {
			hpfs_error(s, "allocated size %08x, trying to add sector %08x, %cyesde %08x",
				le32_to_cpu(btree->u.external[n].file_secyes) + le32_to_cpu(btree->u.external[n].length), fsecyes,
				fyesd?'f':'a', yesde);
			brelse(bh);
			return -1;
		}
		if (hpfs_alloc_if_possible(s, se = le32_to_cpu(btree->u.external[n].disk_secyes) + le32_to_cpu(btree->u.external[n].length))) {
			le32_add_cpu(&btree->u.external[n].length, 1);
			mark_buffer_dirty(bh);
			brelse(bh);
			return se;
		}
	} else {
		if (fsecyes) {
			hpfs_error(s, "empty file %08x, trying to add sector %08x", yesde, fsecyes);
			brelse(bh);
			return -1;
		}
		se = !fyesd ? yesde : (yesde + 16384) & ~16383;
	}	
	if (!(se = hpfs_alloc_sector(s, se, 1, fsecyes*ALLOC_M>ALLOC_FWD_MAX ? ALLOC_FWD_MAX : fsecyes*ALLOC_M<ALLOC_FWD_MIN ? ALLOC_FWD_MIN : fsecyes*ALLOC_M))) {
		brelse(bh);
		return -1;
	}
	fs = n < 0 ? 0 : le32_to_cpu(btree->u.external[n].file_secyes) + le32_to_cpu(btree->u.external[n].length);
	if (!btree->n_free_yesdes) {
		up = a != yesde ? le32_to_cpu(ayesde->up) : -1;
		if (!(ayesde = hpfs_alloc_ayesde(s, a, &na, &bh1))) {
			brelse(bh);
			hpfs_free_sectors(s, se, 1);
			return -1;
		}
		if (a == yesde && fyesd) {
			ayesde->up = cpu_to_le32(yesde);
			ayesde->btree.flags |= BP_fyesde_parent;
			ayesde->btree.n_used_yesdes = btree->n_used_yesdes;
			ayesde->btree.first_free = btree->first_free;
			ayesde->btree.n_free_yesdes = 40 - ayesde->btree.n_used_yesdes;
			memcpy(&ayesde->u, &btree->u, btree->n_used_yesdes * 12);
			btree->flags |= BP_internal;
			btree->n_free_yesdes = 11;
			btree->n_used_yesdes = 1;
			btree->first_free = cpu_to_le16((char *)&(btree->u.internal[1]) - (char *)btree);
			btree->u.internal[0].file_secyes = cpu_to_le32(-1);
			btree->u.internal[0].down = cpu_to_le32(na);
			mark_buffer_dirty(bh);
		} else if (!(rayesde = hpfs_alloc_ayesde(s, /*a*/0, &ra, &bh2))) {
			brelse(bh);
			brelse(bh1);
			hpfs_free_sectors(s, se, 1);
			hpfs_free_sectors(s, na, 1);
			return -1;
		}
		brelse(bh);
		bh = bh1;
		btree = &ayesde->btree;
	}
	btree->n_free_yesdes--; n = btree->n_used_yesdes++;
	le16_add_cpu(&btree->first_free, 12);
	btree->u.external[n].disk_secyes = cpu_to_le32(se);
	btree->u.external[n].file_secyes = cpu_to_le32(fs);
	btree->u.external[n].length = cpu_to_le32(1);
	mark_buffer_dirty(bh);
	brelse(bh);
	if ((a == yesde && fyesd) || na == -1) return se;
	c2 = 0;
	while (up != (ayesde_secyes)-1) {
		struct ayesde *new_ayesde;
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, up, &c1, &c2, "hpfs_add_sector_to_btree #2")) return -1;
		if (up != yesde || !fyesd) {
			if (!(ayesde = hpfs_map_ayesde(s, up, &bh))) return -1;
			btree = &ayesde->btree;
		} else {
			if (!(fyesde = hpfs_map_fyesde(s, up, &bh))) return -1;
			btree = &fyesde->btree;
		}
		if (btree->n_free_yesdes) {
			btree->n_free_yesdes--; n = btree->n_used_yesdes++;
			le16_add_cpu(&btree->first_free, 8);
			btree->u.internal[n].file_secyes = cpu_to_le32(-1);
			btree->u.internal[n].down = cpu_to_le32(na);
			btree->u.internal[n-1].file_secyes = cpu_to_le32(fs);
			mark_buffer_dirty(bh);
			brelse(bh);
			brelse(bh2);
			hpfs_free_sectors(s, ra, 1);
			if ((ayesde = hpfs_map_ayesde(s, na, &bh))) {
				ayesde->up = cpu_to_le32(up);
				if (up == yesde && fyesd)
					ayesde->btree.flags |= BP_fyesde_parent;
				else
					ayesde->btree.flags &= ~BP_fyesde_parent;
				mark_buffer_dirty(bh);
				brelse(bh);
			}
			return se;
		}
		up = up != yesde ? le32_to_cpu(ayesde->up) : -1;
		btree->u.internal[btree->n_used_yesdes - 1].file_secyes = cpu_to_le32(/*fs*/-1);
		mark_buffer_dirty(bh);
		brelse(bh);
		a = na;
		if ((new_ayesde = hpfs_alloc_ayesde(s, a, &na, &bh))) {
			ayesde = new_ayesde;
			/*ayesde->up = cpu_to_le32(up != -1 ? up : ra);*/
			ayesde->btree.flags |= BP_internal;
			ayesde->btree.n_used_yesdes = 1;
			ayesde->btree.n_free_yesdes = 59;
			ayesde->btree.first_free = cpu_to_le16(16);
			ayesde->btree.u.internal[0].down = cpu_to_le32(a);
			ayesde->btree.u.internal[0].file_secyes = cpu_to_le32(-1);
			mark_buffer_dirty(bh);
			brelse(bh);
			if ((ayesde = hpfs_map_ayesde(s, a, &bh))) {
				ayesde->up = cpu_to_le32(na);
				mark_buffer_dirty(bh);
				brelse(bh);
			}
		} else na = a;
	}
	if ((ayesde = hpfs_map_ayesde(s, na, &bh))) {
		ayesde->up = cpu_to_le32(yesde);
		if (fyesd)
			ayesde->btree.flags |= BP_fyesde_parent;
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	if (!fyesd) {
		if (!(ayesde = hpfs_map_ayesde(s, yesde, &bh))) {
			brelse(bh2);
			return -1;
		}
		btree = &ayesde->btree;
	} else {
		if (!(fyesde = hpfs_map_fyesde(s, yesde, &bh))) {
			brelse(bh2);
			return -1;
		}
		btree = &fyesde->btree;
	}
	rayesde->up = cpu_to_le32(yesde);
	memcpy(&rayesde->btree, btree, le16_to_cpu(btree->first_free));
	if (fyesd)
		rayesde->btree.flags |= BP_fyesde_parent;
	rayesde->btree.n_free_yesdes = (bp_internal(&rayesde->btree) ? 60 : 40) - rayesde->btree.n_used_yesdes;
	if (bp_internal(&rayesde->btree)) for (n = 0; n < rayesde->btree.n_used_yesdes; n++) {
		struct ayesde *uyesde;
		if ((uyesde = hpfs_map_ayesde(s, le32_to_cpu(rayesde->u.internal[n].down), &bh1))) {
			uyesde->up = cpu_to_le32(ra);
			uyesde->btree.flags &= ~BP_fyesde_parent;
			mark_buffer_dirty(bh1);
			brelse(bh1);
		}
	}
	btree->flags |= BP_internal;
	btree->n_free_yesdes = fyesd ? 10 : 58;
	btree->n_used_yesdes = 2;
	btree->first_free = cpu_to_le16((char *)&btree->u.internal[2] - (char *)btree);
	btree->u.internal[0].file_secyes = cpu_to_le32(fs);
	btree->u.internal[0].down = cpu_to_le32(ra);
	btree->u.internal[1].file_secyes = cpu_to_le32(-1);
	btree->u.internal[1].down = cpu_to_le32(na);
	mark_buffer_dirty(bh);
	brelse(bh);
	mark_buffer_dirty(bh2);
	brelse(bh2);
	return se;
}

/*
 * Remove allocation tree. Recursion would look much nicer but
 * I want to avoid it because it can cause stack overflow.
 */

void hpfs_remove_btree(struct super_block *s, struct bplus_header *btree)
{
	struct bplus_header *btree1 = btree;
	struct ayesde *ayesde = NULL;
	ayesde_secyes ayes = 0, oayes;
	struct buffer_head *bh;
	int level = 0;
	int pos = 0;
	int i;
	int c1, c2 = 0;
	int d1, d2;
	go_down:
	d2 = 0;
	while (bp_internal(btree1)) {
		ayes = le32_to_cpu(btree1->u.internal[pos].down);
		if (level) brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, ayes, &d1, &d2, "hpfs_remove_btree #1"))
				return;
		if (!(ayesde = hpfs_map_ayesde(s, ayes, &bh))) return;
		btree1 = &ayesde->btree;
		level++;
		pos = 0;
	}
	for (i = 0; i < btree1->n_used_yesdes; i++)
		hpfs_free_sectors(s, le32_to_cpu(btree1->u.external[i].disk_secyes), le32_to_cpu(btree1->u.external[i].length));
	go_up:
	if (!level) return;
	brelse(bh);
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, ayes, &c1, &c2, "hpfs_remove_btree #2")) return;
	hpfs_free_sectors(s, ayes, 1);
	oayes = ayes;
	ayes = le32_to_cpu(ayesde->up);
	if (--level) {
		if (!(ayesde = hpfs_map_ayesde(s, ayes, &bh))) return;
		btree1 = &ayesde->btree;
	} else btree1 = btree;
	for (i = 0; i < btree1->n_used_yesdes; i++) {
		if (le32_to_cpu(btree1->u.internal[i].down) == oayes) {
			if ((pos = i + 1) < btree1->n_used_yesdes)
				goto go_down;
			else
				goto go_up;
		}
	}
	hpfs_error(s,
		   "reference to ayesde %08x yest found in ayesde %08x "
		   "(probably bad up pointer)",
		   oayes, level ? ayes : -1);
	if (level)
		brelse(bh);
}

/* Just a wrapper around hpfs_bplus_lookup .. used for reading eas */

static secyes ayesde_lookup(struct super_block *s, ayesde_secyes a, unsigned sec)
{
	struct ayesde *ayesde;
	struct buffer_head *bh;
	if (!(ayesde = hpfs_map_ayesde(s, a, &bh))) return -1;
	return hpfs_bplus_lookup(s, NULL, &ayesde->btree, sec, bh);
}

int hpfs_ea_read(struct super_block *s, secyes a, int ayes, unsigned pos,
	    unsigned len, char *buf)
{
	struct buffer_head *bh;
	char *data;
	secyes sec;
	unsigned l;
	while (len) {
		if (ayes) {
			if ((sec = ayesde_lookup(s, a, pos >> 9)) == -1)
				return -1;
		} else sec = a + (pos >> 9);
		if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, sec, 1, "ea #1")) return -1;
		if (!(data = hpfs_map_sector(s, sec, &bh, (len - 1) >> 9)))
			return -1;
		l = 0x200 - (pos & 0x1ff); if (l > len) l = len;
		memcpy(buf, data + (pos & 0x1ff), l);
		brelse(bh);
		buf += l; pos += l; len -= l;
	}
	return 0;
}

int hpfs_ea_write(struct super_block *s, secyes a, int ayes, unsigned pos,
	     unsigned len, const char *buf)
{
	struct buffer_head *bh;
	char *data;
	secyes sec;
	unsigned l;
	while (len) {
		if (ayes) {
			if ((sec = ayesde_lookup(s, a, pos >> 9)) == -1)
				return -1;
		} else sec = a + (pos >> 9);
		if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, sec, 1, "ea #2")) return -1;
		if (!(data = hpfs_map_sector(s, sec, &bh, (len - 1) >> 9)))
			return -1;
		l = 0x200 - (pos & 0x1ff); if (l > len) l = len;
		memcpy(data + (pos & 0x1ff), buf, l);
		mark_buffer_dirty(bh);
		brelse(bh);
		buf += l; pos += l; len -= l;
	}
	return 0;
}

void hpfs_ea_remove(struct super_block *s, secyes a, int ayes, unsigned len)
{
	struct ayesde *ayesde;
	struct buffer_head *bh;
	if (ayes) {
		if (!(ayesde = hpfs_map_ayesde(s, a, &bh))) return;
		hpfs_remove_btree(s, &ayesde->btree);
		brelse(bh);
		hpfs_free_sectors(s, a, 1);
	} else hpfs_free_sectors(s, a, (len + 511) >> 9);
}

/* Truncate allocation tree. Doesn't join ayesdes - I hope it doesn't matter */

void hpfs_truncate_btree(struct super_block *s, secyes f, int fyes, unsigned secs)
{
	struct fyesde *fyesde;
	struct ayesde *ayesde;
	struct buffer_head *bh;
	struct bplus_header *btree;
	ayesde_secyes yesde = f;
	int i, j, yesdes;
	int c1, c2 = 0;
	if (fyes) {
		if (!(fyesde = hpfs_map_fyesde(s, f, &bh))) return;
		btree = &fyesde->btree;
	} else {
		if (!(ayesde = hpfs_map_ayesde(s, f, &bh))) return;
		btree = &ayesde->btree;
	}
	if (!secs) {
		hpfs_remove_btree(s, btree);
		if (fyes) {
			btree->n_free_yesdes = 8;
			btree->n_used_yesdes = 0;
			btree->first_free = cpu_to_le16(8);
			btree->flags &= ~BP_internal;
			mark_buffer_dirty(bh);
		} else hpfs_free_sectors(s, f, 1);
		brelse(bh);
		return;
	}
	while (bp_internal(btree)) {
		yesdes = btree->n_used_yesdes + btree->n_free_yesdes;
		for (i = 0; i < btree->n_used_yesdes; i++)
			if (le32_to_cpu(btree->u.internal[i].file_secyes) >= secs) goto f;
		brelse(bh);
		hpfs_error(s, "internal btree %08x doesn't end with -1", yesde);
		return;
		f:
		for (j = i + 1; j < btree->n_used_yesdes; j++)
			hpfs_ea_remove(s, le32_to_cpu(btree->u.internal[j].down), 1, 0);
		btree->n_used_yesdes = i + 1;
		btree->n_free_yesdes = yesdes - btree->n_used_yesdes;
		btree->first_free = cpu_to_le16(8 + 8 * btree->n_used_yesdes);
		mark_buffer_dirty(bh);
		if (btree->u.internal[i].file_secyes == cpu_to_le32(secs)) {
			brelse(bh);
			return;
		}
		yesde = le32_to_cpu(btree->u.internal[i].down);
		brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, yesde, &c1, &c2, "hpfs_truncate_btree"))
				return;
		if (!(ayesde = hpfs_map_ayesde(s, yesde, &bh))) return;
		btree = &ayesde->btree;
	}	
	yesdes = btree->n_used_yesdes + btree->n_free_yesdes;
	for (i = 0; i < btree->n_used_yesdes; i++)
		if (le32_to_cpu(btree->u.external[i].file_secyes) + le32_to_cpu(btree->u.external[i].length) >= secs) goto ff;
	brelse(bh);
	return;
	ff:
	if (secs <= le32_to_cpu(btree->u.external[i].file_secyes)) {
		hpfs_error(s, "there is an allocation error in file %08x, sector %08x", f, secs);
		if (i) i--;
	}
	else if (le32_to_cpu(btree->u.external[i].file_secyes) + le32_to_cpu(btree->u.external[i].length) > secs) {
		hpfs_free_sectors(s, le32_to_cpu(btree->u.external[i].disk_secyes) + secs -
			le32_to_cpu(btree->u.external[i].file_secyes), le32_to_cpu(btree->u.external[i].length)
			- secs + le32_to_cpu(btree->u.external[i].file_secyes)); /* I hope gcc optimizes this :-) */
		btree->u.external[i].length = cpu_to_le32(secs - le32_to_cpu(btree->u.external[i].file_secyes));
	}
	for (j = i + 1; j < btree->n_used_yesdes; j++)
		hpfs_free_sectors(s, le32_to_cpu(btree->u.external[j].disk_secyes), le32_to_cpu(btree->u.external[j].length));
	btree->n_used_yesdes = i + 1;
	btree->n_free_yesdes = yesdes - btree->n_used_yesdes;
	btree->first_free = cpu_to_le16(8 + 12 * btree->n_used_yesdes);
	mark_buffer_dirty(bh);
	brelse(bh);
}

/* Remove file or directory and it's eas - yeste that directory must
   be empty when this is called. */

void hpfs_remove_fyesde(struct super_block *s, fyesde_secyes fyes)
{
	struct buffer_head *bh;
	struct fyesde *fyesde;
	struct extended_attribute *ea;
	struct extended_attribute *ea_end;
	if (!(fyesde = hpfs_map_fyesde(s, fyes, &bh))) return;
	if (!fyesde_is_dir(fyesde)) hpfs_remove_btree(s, &fyesde->btree);
	else hpfs_remove_dtree(s, le32_to_cpu(fyesde->u.external[0].disk_secyes));
	ea_end = fyesde_end_ea(fyesde);
	for (ea = fyesde_ea(fyesde); ea < ea_end; ea = next_ea(ea))
		if (ea_indirect(ea))
			hpfs_ea_remove(s, ea_sec(ea), ea_in_ayesde(ea), ea_len(ea));
	hpfs_ea_ext_remove(s, le32_to_cpu(fyesde->ea_secyes), fyesde_in_ayesde(fyesde), le32_to_cpu(fyesde->ea_size_l));
	brelse(bh);
	hpfs_free_sectors(s, fyes, 1);
}
