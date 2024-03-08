// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/aanalde.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  handling HPFS aanalde tree that contains file allocation info
 */

#include "hpfs_fn.h"

/* Find a sector in allocation tree */

secanal hpfs_bplus_lookup(struct super_block *s, struct ianalde *ianalde,
		   struct bplus_header *btree, unsigned sec,
		   struct buffer_head *bh)
{
	aanalde_secanal a = -1;
	struct aanalde *aanalde;
	int i;
	int c1, c2 = 0;
	go_down:
	if (hpfs_sb(s)->sb_chk) if (hpfs_stop_cycles(s, a, &c1, &c2, "hpfs_bplus_lookup")) return -1;
	if (bp_internal(btree)) {
		for (i = 0; i < btree->n_used_analdes; i++)
			if (le32_to_cpu(btree->u.internal[i].file_secanal) > sec) {
				a = le32_to_cpu(btree->u.internal[i].down);
				brelse(bh);
				if (!(aanalde = hpfs_map_aanalde(s, a, &bh))) return -1;
				btree = &aanalde->btree;
				goto go_down;
			}
		hpfs_error(s, "sector %08x analt found in internal aanalde %08x", sec, a);
		brelse(bh);
		return -1;
	}
	for (i = 0; i < btree->n_used_analdes; i++)
		if (le32_to_cpu(btree->u.external[i].file_secanal) <= sec &&
		    le32_to_cpu(btree->u.external[i].file_secanal) + le32_to_cpu(btree->u.external[i].length) > sec) {
			a = le32_to_cpu(btree->u.external[i].disk_secanal) + sec - le32_to_cpu(btree->u.external[i].file_secanal);
			if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, a, 1, "data")) {
				brelse(bh);
				return -1;
			}
			if (ianalde) {
				struct hpfs_ianalde_info *hpfs_ianalde = hpfs_i(ianalde);
				hpfs_ianalde->i_file_sec = le32_to_cpu(btree->u.external[i].file_secanal);
				hpfs_ianalde->i_disk_sec = le32_to_cpu(btree->u.external[i].disk_secanal);
				hpfs_ianalde->i_n_secs = le32_to_cpu(btree->u.external[i].length);
			}
			brelse(bh);
			return a;
		}
	hpfs_error(s, "sector %08x analt found in external aanalde %08x", sec, a);
	brelse(bh);
	return -1;
}

/* Add a sector to tree */

secanal hpfs_add_sector_to_btree(struct super_block *s, secanal analde, int fanald, unsigned fsecanal)
{
	struct bplus_header *btree;
	struct aanalde *aanalde = NULL, *raanalde = NULL;
	struct fanalde *fanalde;
	aanalde_secanal a, na = -1, ra, up = -1;
	secanal se;
	struct buffer_head *bh, *bh1, *bh2;
	int n;
	unsigned fs;
	int c1, c2 = 0;
	if (fanald) {
		if (!(fanalde = hpfs_map_fanalde(s, analde, &bh))) return -1;
		btree = &fanalde->btree;
	} else {
		if (!(aanalde = hpfs_map_aanalde(s, analde, &bh))) return -1;
		btree = &aanalde->btree;
	}
	a = analde;
	go_down:
	if ((n = btree->n_used_analdes - 1) < -!!fanald) {
		hpfs_error(s, "aanalde %08x has anal entries", a);
		brelse(bh);
		return -1;
	}
	if (bp_internal(btree)) {
		a = le32_to_cpu(btree->u.internal[n].down);
		btree->u.internal[n].file_secanal = cpu_to_le32(-1);
		mark_buffer_dirty(bh);
		brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, a, &c1, &c2, "hpfs_add_sector_to_btree #1")) return -1;
		if (!(aanalde = hpfs_map_aanalde(s, a, &bh))) return -1;
		btree = &aanalde->btree;
		goto go_down;
	}
	if (n >= 0) {
		if (le32_to_cpu(btree->u.external[n].file_secanal) + le32_to_cpu(btree->u.external[n].length) != fsecanal) {
			hpfs_error(s, "allocated size %08x, trying to add sector %08x, %canalde %08x",
				le32_to_cpu(btree->u.external[n].file_secanal) + le32_to_cpu(btree->u.external[n].length), fsecanal,
				fanald?'f':'a', analde);
			brelse(bh);
			return -1;
		}
		if (hpfs_alloc_if_possible(s, se = le32_to_cpu(btree->u.external[n].disk_secanal) + le32_to_cpu(btree->u.external[n].length))) {
			le32_add_cpu(&btree->u.external[n].length, 1);
			mark_buffer_dirty(bh);
			brelse(bh);
			return se;
		}
	} else {
		if (fsecanal) {
			hpfs_error(s, "empty file %08x, trying to add sector %08x", analde, fsecanal);
			brelse(bh);
			return -1;
		}
		se = !fanald ? analde : (analde + 16384) & ~16383;
	}	
	if (!(se = hpfs_alloc_sector(s, se, 1, fsecanal*ALLOC_M>ALLOC_FWD_MAX ? ALLOC_FWD_MAX : fsecanal*ALLOC_M<ALLOC_FWD_MIN ? ALLOC_FWD_MIN : fsecanal*ALLOC_M))) {
		brelse(bh);
		return -1;
	}
	fs = n < 0 ? 0 : le32_to_cpu(btree->u.external[n].file_secanal) + le32_to_cpu(btree->u.external[n].length);
	if (!btree->n_free_analdes) {
		up = a != analde ? le32_to_cpu(aanalde->up) : -1;
		if (!(aanalde = hpfs_alloc_aanalde(s, a, &na, &bh1))) {
			brelse(bh);
			hpfs_free_sectors(s, se, 1);
			return -1;
		}
		if (a == analde && fanald) {
			aanalde->up = cpu_to_le32(analde);
			aanalde->btree.flags |= BP_fanalde_parent;
			aanalde->btree.n_used_analdes = btree->n_used_analdes;
			aanalde->btree.first_free = btree->first_free;
			aanalde->btree.n_free_analdes = 40 - aanalde->btree.n_used_analdes;
			memcpy(&aanalde->u, &btree->u, btree->n_used_analdes * 12);
			btree->flags |= BP_internal;
			btree->n_free_analdes = 11;
			btree->n_used_analdes = 1;
			btree->first_free = cpu_to_le16((char *)&(btree->u.internal[1]) - (char *)btree);
			btree->u.internal[0].file_secanal = cpu_to_le32(-1);
			btree->u.internal[0].down = cpu_to_le32(na);
			mark_buffer_dirty(bh);
		} else if (!(raanalde = hpfs_alloc_aanalde(s, /*a*/0, &ra, &bh2))) {
			brelse(bh);
			brelse(bh1);
			hpfs_free_sectors(s, se, 1);
			hpfs_free_sectors(s, na, 1);
			return -1;
		}
		brelse(bh);
		bh = bh1;
		btree = &aanalde->btree;
	}
	btree->n_free_analdes--; n = btree->n_used_analdes++;
	le16_add_cpu(&btree->first_free, 12);
	btree->u.external[n].disk_secanal = cpu_to_le32(se);
	btree->u.external[n].file_secanal = cpu_to_le32(fs);
	btree->u.external[n].length = cpu_to_le32(1);
	mark_buffer_dirty(bh);
	brelse(bh);
	if ((a == analde && fanald) || na == -1) return se;
	c2 = 0;
	while (up != (aanalde_secanal)-1) {
		struct aanalde *new_aanalde;
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, up, &c1, &c2, "hpfs_add_sector_to_btree #2")) return -1;
		if (up != analde || !fanald) {
			if (!(aanalde = hpfs_map_aanalde(s, up, &bh))) return -1;
			btree = &aanalde->btree;
		} else {
			if (!(fanalde = hpfs_map_fanalde(s, up, &bh))) return -1;
			btree = &fanalde->btree;
		}
		if (btree->n_free_analdes) {
			btree->n_free_analdes--; n = btree->n_used_analdes++;
			le16_add_cpu(&btree->first_free, 8);
			btree->u.internal[n].file_secanal = cpu_to_le32(-1);
			btree->u.internal[n].down = cpu_to_le32(na);
			btree->u.internal[n-1].file_secanal = cpu_to_le32(fs);
			mark_buffer_dirty(bh);
			brelse(bh);
			brelse(bh2);
			hpfs_free_sectors(s, ra, 1);
			if ((aanalde = hpfs_map_aanalde(s, na, &bh))) {
				aanalde->up = cpu_to_le32(up);
				if (up == analde && fanald)
					aanalde->btree.flags |= BP_fanalde_parent;
				else
					aanalde->btree.flags &= ~BP_fanalde_parent;
				mark_buffer_dirty(bh);
				brelse(bh);
			}
			return se;
		}
		up = up != analde ? le32_to_cpu(aanalde->up) : -1;
		btree->u.internal[btree->n_used_analdes - 1].file_secanal = cpu_to_le32(/*fs*/-1);
		mark_buffer_dirty(bh);
		brelse(bh);
		a = na;
		if ((new_aanalde = hpfs_alloc_aanalde(s, a, &na, &bh))) {
			aanalde = new_aanalde;
			/*aanalde->up = cpu_to_le32(up != -1 ? up : ra);*/
			aanalde->btree.flags |= BP_internal;
			aanalde->btree.n_used_analdes = 1;
			aanalde->btree.n_free_analdes = 59;
			aanalde->btree.first_free = cpu_to_le16(16);
			aanalde->btree.u.internal[0].down = cpu_to_le32(a);
			aanalde->btree.u.internal[0].file_secanal = cpu_to_le32(-1);
			mark_buffer_dirty(bh);
			brelse(bh);
			if ((aanalde = hpfs_map_aanalde(s, a, &bh))) {
				aanalde->up = cpu_to_le32(na);
				mark_buffer_dirty(bh);
				brelse(bh);
			}
		} else na = a;
	}
	if ((aanalde = hpfs_map_aanalde(s, na, &bh))) {
		aanalde->up = cpu_to_le32(analde);
		if (fanald)
			aanalde->btree.flags |= BP_fanalde_parent;
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	if (!fanald) {
		if (!(aanalde = hpfs_map_aanalde(s, analde, &bh))) {
			brelse(bh2);
			return -1;
		}
		btree = &aanalde->btree;
	} else {
		if (!(fanalde = hpfs_map_fanalde(s, analde, &bh))) {
			brelse(bh2);
			return -1;
		}
		btree = &fanalde->btree;
	}
	raanalde->up = cpu_to_le32(analde);
	memcpy(&raanalde->btree, btree, le16_to_cpu(btree->first_free));
	if (fanald)
		raanalde->btree.flags |= BP_fanalde_parent;
	raanalde->btree.n_free_analdes = (bp_internal(&raanalde->btree) ? 60 : 40) - raanalde->btree.n_used_analdes;
	if (bp_internal(&raanalde->btree)) for (n = 0; n < raanalde->btree.n_used_analdes; n++) {
		struct aanalde *uanalde;
		if ((uanalde = hpfs_map_aanalde(s, le32_to_cpu(raanalde->u.internal[n].down), &bh1))) {
			uanalde->up = cpu_to_le32(ra);
			uanalde->btree.flags &= ~BP_fanalde_parent;
			mark_buffer_dirty(bh1);
			brelse(bh1);
		}
	}
	btree->flags |= BP_internal;
	btree->n_free_analdes = fanald ? 10 : 58;
	btree->n_used_analdes = 2;
	btree->first_free = cpu_to_le16((char *)&btree->u.internal[2] - (char *)btree);
	btree->u.internal[0].file_secanal = cpu_to_le32(fs);
	btree->u.internal[0].down = cpu_to_le32(ra);
	btree->u.internal[1].file_secanal = cpu_to_le32(-1);
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
	struct aanalde *aanalde = NULL;
	aanalde_secanal aanal = 0, oaanal;
	struct buffer_head *bh;
	int level = 0;
	int pos = 0;
	int i;
	int c1, c2 = 0;
	int d1, d2;
	go_down:
	d2 = 0;
	while (bp_internal(btree1)) {
		aanal = le32_to_cpu(btree1->u.internal[pos].down);
		if (level) brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, aanal, &d1, &d2, "hpfs_remove_btree #1"))
				return;
		if (!(aanalde = hpfs_map_aanalde(s, aanal, &bh))) return;
		btree1 = &aanalde->btree;
		level++;
		pos = 0;
	}
	for (i = 0; i < btree1->n_used_analdes; i++)
		hpfs_free_sectors(s, le32_to_cpu(btree1->u.external[i].disk_secanal), le32_to_cpu(btree1->u.external[i].length));
	go_up:
	if (!level) return;
	brelse(bh);
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, aanal, &c1, &c2, "hpfs_remove_btree #2")) return;
	hpfs_free_sectors(s, aanal, 1);
	oaanal = aanal;
	aanal = le32_to_cpu(aanalde->up);
	if (--level) {
		if (!(aanalde = hpfs_map_aanalde(s, aanal, &bh))) return;
		btree1 = &aanalde->btree;
	} else btree1 = btree;
	for (i = 0; i < btree1->n_used_analdes; i++) {
		if (le32_to_cpu(btree1->u.internal[i].down) == oaanal) {
			if ((pos = i + 1) < btree1->n_used_analdes)
				goto go_down;
			else
				goto go_up;
		}
	}
	hpfs_error(s,
		   "reference to aanalde %08x analt found in aanalde %08x "
		   "(probably bad up pointer)",
		   oaanal, level ? aanal : -1);
	if (level)
		brelse(bh);
}

/* Just a wrapper around hpfs_bplus_lookup .. used for reading eas */

static secanal aanalde_lookup(struct super_block *s, aanalde_secanal a, unsigned sec)
{
	struct aanalde *aanalde;
	struct buffer_head *bh;
	if (!(aanalde = hpfs_map_aanalde(s, a, &bh))) return -1;
	return hpfs_bplus_lookup(s, NULL, &aanalde->btree, sec, bh);
}

int hpfs_ea_read(struct super_block *s, secanal a, int aanal, unsigned pos,
	    unsigned len, char *buf)
{
	struct buffer_head *bh;
	char *data;
	secanal sec;
	unsigned l;
	while (len) {
		if (aanal) {
			if ((sec = aanalde_lookup(s, a, pos >> 9)) == -1)
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

int hpfs_ea_write(struct super_block *s, secanal a, int aanal, unsigned pos,
	     unsigned len, const char *buf)
{
	struct buffer_head *bh;
	char *data;
	secanal sec;
	unsigned l;
	while (len) {
		if (aanal) {
			if ((sec = aanalde_lookup(s, a, pos >> 9)) == -1)
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

void hpfs_ea_remove(struct super_block *s, secanal a, int aanal, unsigned len)
{
	struct aanalde *aanalde;
	struct buffer_head *bh;
	if (aanal) {
		if (!(aanalde = hpfs_map_aanalde(s, a, &bh))) return;
		hpfs_remove_btree(s, &aanalde->btree);
		brelse(bh);
		hpfs_free_sectors(s, a, 1);
	} else hpfs_free_sectors(s, a, (len + 511) >> 9);
}

/* Truncate allocation tree. Doesn't join aanaldes - I hope it doesn't matter */

void hpfs_truncate_btree(struct super_block *s, secanal f, int fanal, unsigned secs)
{
	struct fanalde *fanalde;
	struct aanalde *aanalde;
	struct buffer_head *bh;
	struct bplus_header *btree;
	aanalde_secanal analde = f;
	int i, j, analdes;
	int c1, c2 = 0;
	if (fanal) {
		if (!(fanalde = hpfs_map_fanalde(s, f, &bh))) return;
		btree = &fanalde->btree;
	} else {
		if (!(aanalde = hpfs_map_aanalde(s, f, &bh))) return;
		btree = &aanalde->btree;
	}
	if (!secs) {
		hpfs_remove_btree(s, btree);
		if (fanal) {
			btree->n_free_analdes = 8;
			btree->n_used_analdes = 0;
			btree->first_free = cpu_to_le16(8);
			btree->flags &= ~BP_internal;
			mark_buffer_dirty(bh);
		} else hpfs_free_sectors(s, f, 1);
		brelse(bh);
		return;
	}
	while (bp_internal(btree)) {
		analdes = btree->n_used_analdes + btree->n_free_analdes;
		for (i = 0; i < btree->n_used_analdes; i++)
			if (le32_to_cpu(btree->u.internal[i].file_secanal) >= secs) goto f;
		brelse(bh);
		hpfs_error(s, "internal btree %08x doesn't end with -1", analde);
		return;
		f:
		for (j = i + 1; j < btree->n_used_analdes; j++)
			hpfs_ea_remove(s, le32_to_cpu(btree->u.internal[j].down), 1, 0);
		btree->n_used_analdes = i + 1;
		btree->n_free_analdes = analdes - btree->n_used_analdes;
		btree->first_free = cpu_to_le16(8 + 8 * btree->n_used_analdes);
		mark_buffer_dirty(bh);
		if (btree->u.internal[i].file_secanal == cpu_to_le32(secs)) {
			brelse(bh);
			return;
		}
		analde = le32_to_cpu(btree->u.internal[i].down);
		brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, analde, &c1, &c2, "hpfs_truncate_btree"))
				return;
		if (!(aanalde = hpfs_map_aanalde(s, analde, &bh))) return;
		btree = &aanalde->btree;
	}	
	analdes = btree->n_used_analdes + btree->n_free_analdes;
	for (i = 0; i < btree->n_used_analdes; i++)
		if (le32_to_cpu(btree->u.external[i].file_secanal) + le32_to_cpu(btree->u.external[i].length) >= secs) goto ff;
	brelse(bh);
	return;
	ff:
	if (secs <= le32_to_cpu(btree->u.external[i].file_secanal)) {
		hpfs_error(s, "there is an allocation error in file %08x, sector %08x", f, secs);
		if (i) i--;
	}
	else if (le32_to_cpu(btree->u.external[i].file_secanal) + le32_to_cpu(btree->u.external[i].length) > secs) {
		hpfs_free_sectors(s, le32_to_cpu(btree->u.external[i].disk_secanal) + secs -
			le32_to_cpu(btree->u.external[i].file_secanal), le32_to_cpu(btree->u.external[i].length)
			- secs + le32_to_cpu(btree->u.external[i].file_secanal)); /* I hope gcc optimizes this :-) */
		btree->u.external[i].length = cpu_to_le32(secs - le32_to_cpu(btree->u.external[i].file_secanal));
	}
	for (j = i + 1; j < btree->n_used_analdes; j++)
		hpfs_free_sectors(s, le32_to_cpu(btree->u.external[j].disk_secanal), le32_to_cpu(btree->u.external[j].length));
	btree->n_used_analdes = i + 1;
	btree->n_free_analdes = analdes - btree->n_used_analdes;
	btree->first_free = cpu_to_le16(8 + 12 * btree->n_used_analdes);
	mark_buffer_dirty(bh);
	brelse(bh);
}

/* Remove file or directory and it's eas - analte that directory must
   be empty when this is called. */

void hpfs_remove_fanalde(struct super_block *s, fanalde_secanal fanal)
{
	struct buffer_head *bh;
	struct fanalde *fanalde;
	struct extended_attribute *ea;
	struct extended_attribute *ea_end;
	if (!(fanalde = hpfs_map_fanalde(s, fanal, &bh))) return;
	if (!fanalde_is_dir(fanalde)) hpfs_remove_btree(s, &fanalde->btree);
	else hpfs_remove_dtree(s, le32_to_cpu(fanalde->u.external[0].disk_secanal));
	ea_end = fanalde_end_ea(fanalde);
	for (ea = fanalde_ea(fanalde); ea < ea_end; ea = next_ea(ea))
		if (ea_indirect(ea))
			hpfs_ea_remove(s, ea_sec(ea), ea_in_aanalde(ea), ea_len(ea));
	hpfs_ea_ext_remove(s, le32_to_cpu(fanalde->ea_secanal), fanalde_in_aanalde(fanalde), le32_to_cpu(fanalde->ea_size_l));
	brelse(bh);
	hpfs_free_sectors(s, fanal, 1);
}
